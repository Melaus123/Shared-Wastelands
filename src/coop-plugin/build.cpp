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
// Lever: `buildtest into <near|host-key-substring> <sid> [indoors|<dx>,<dz>]` (indoors = form 2, isIndoorsOf only, at (-25, 0, -25); <dx>,<dz> = form 0 at that offset from the host's centre, else (25, 0, 25)).
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
//   AddConstructionProgress 0x558B10 (b, amount)     - vt+0x230 of every class but WallBuilding, whose vt+0x230 (0x5594A0) calls it
//                                                      on the wall holding the progress record (its group's lead); main thread (Read).
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
//   upgrade|downgrade <sub> vt+0x258 / vt+0x260 on an own, complete piece - the engine's own in-place upgrade (T-609 U).
#include "build.h"
#include "farm.h"           /* par16: farms ride this channel (MSG_BUILD kinds 6 / 7) */
#include "addresses.h"      /* the five names below come from the address table */
#include "store.h"          /* EngineWritesBlocked, StoreMainThreadId */
#include "items.h"          /* ObjectPositionKey, ObjectByPositionKey - the one P7n key builder and its resolver */
#include "../common/poskey.h"   /* WireForm: whether a restore row's key reached the standing piece by its hashed form */
#include "zones.h"          /* SectorOf - the same cell arithmetic as the engine's 0x9B1EC0 */
#include "spawn.h"          /* MirrorCapacity, MirrorSlot, SafeReadPosition */
#include "playerfaction.h"  /* LocalPlayerFaction */
#include "policy.h"         /* PolicyVtableRvaPod, PolicyClassNamePod - the RTTI class name (the wall test) */
#include "team.h"           /* T-805: TeamSameAnyThread - the owner's game checks a DISMANTLE_ASK's sender */
#include "relations.h"      /* T-903: RelationsStanceLevels - the owner's PLAYERS-tab setting towards this game's player */
#include "../common/nametag.h"   /* T-903: nametag::kFriendly */
#include "doors.h"          /* P15 fold 1: DoorsForgetOwner - a building's door rows are dropped before its setDestroyed flip */
#include "net/session.h"    /* IsUidMine, SendBuild */
#include "ownstore.h"        /* help1 fold: OwnWriterOn, OwnStoreCommittedSeq - pp.build landed (MED 4) */
#include "../common/buildwire.h"   /* build1-b: the MSG_BUILD payload */
#include "../common/powerwire.h"   /* P47: POWER_STATE / POWER_ASK and their rules */
#include "../common/farmwire.h"    /* P47: coopfarm::kProdWriter* - the production writer's three answers */
#include "config.h"                /* P47: RoleIsSingle - a lone game runs its own power */
#include "../common/keptwire.h"    /* KEPT: the players' kept pieces laid back into a reset world (and zonekeep.h) */
#include "../common/townkeep.h"    /* TOWNS: the players' outposts carried into a reset world */
#include "../common/baseshow.h"    /* T-811: SHOW - the teammates' outposts on the map */
#include "../common/boxowner.h"    /* WorldNodeOf - a world resource node is nobody's piece */
#include "../common/slotwire.h"    /* P18 fold 2: StandInSlotOfId / IsLegacyPeerId - a saved owner id read as a stand-in record */
#include "../common/boxowner.h"    /* build1h: HouseOwnerOfNewPiece */
#include "../common/ownrec.h"      /* mmo8a: pp.build rows (EncodeBuildRec / BuildRow) */
#include "../common/placeinput.h"  /* a placement input height = live y less the ground height */
#include "../common/tradecharge.h" /* buyhouse cats=<n>: ParseCats / PayToSet, the purse set the buytest levers use */
#include "../common/townreoffer.h" /* T-805 b: BuyHeldAsk / PurseCovers - the buy callback's held-residents question */
#include "../common/townwake.h"    /* T-936: a newly founded outpost woken - the queue, the one-look verdict, the area-record check */
#include <cstddef>
#include <vector>
#include "coop_log.h"
#include "hooks.h"     /* coop::AddHook (own MinHook) */
#include "game/Character.h"
#include "game/Faction.h"
#include "game/GameWorld.h"   /* T328: activeCharacters */
#include "game/hand.h"   /* build1-e: a host's interior pieces are hands (as spawn.cpp BED1) */
#include "game/GameData.h"   /* a building's record's "is foliage" mark (BdFoliagePod) */
#include <Windows.h>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>        /* house2: the removed-keys list */
#include "../common/groundkey.h"   /* P3: coopground::GroundBuildingSource - BuildGroundSource */
#include "../common/walkdrop.h"   /* T-609 fold 1 (F4): GoneAfterDismantle - a building leaves the walk list only when it reads dismantled */
#include <deque>
#include <io.h>         /* house2 fold: _commit / _fileno */
#include <sstream>
#include <string>
#include "u8file.h"   /* UTF-8 paths through the wide Windows file calls */

namespace coop {

int ItKeptBoxCount(void* b);   /* items.cpp: the item stacks in a box, every section (-1 = no inventory, or not read). MAIN THREAD */
int ItKeptBoxMine(void* b, const float* pos, const char* key);   /* items.cpp: 1 = this game writes box b now. MAIN THREAD */
int TownGenBuyHeld(void* building);   /* towngen.cpp, MAIN THREAD (owner 809 b / 813 b): a house being bought - its residents on hold made now, or the entry dropped; -1 off main, 0 none waiting, else townreoffer::kOne* */
int TownGenBuyHeldGroups(void** out, int cap);   /* towngen.cpp, MAIN THREAD: the groups the last TownGenBuyHeld run made */
int TownGenPlatoonHomeKey(const void* platoon, char* out, int cap);   /* towngen.cpp: the group's home building key - 1 key, 0 no home, -1 unreadable */
int GroundTerrainHeightAt(float x, float z, float* out);   /* combat.cpp - the terrain height at (x,z) (getTerrainHeight, SEH-wrapped): 1 height, 0 no terrain (-99), -1 no row / faulted. MAIN THREAD */
long long ZoneTornGen();   /* items.cpp: one more each time a zone is taken apart (either thread) */
int PowerSwitchNow(void* b);   /* P47 (H-P47b): below - a power piece's switch (1 / 0), -1 not a power piece or unreadable. MAIN THREAD */
void PowerCompletionPutBack(void* b, int before);   /* P47 (H-P47b): below - after the mod's own completion apply. MAIN THREAD */

// PROBE-START: P082
static unsigned long long kBdCommitRva = 0;    static AddrReg kBdCommitRva_reg("PlacementCommit", &kBdCommitRva);              /* Steam_1.0.65 0x4D6810 */
static unsigned long long kBdCreateRva = 0;    static AddrReg kBdCreateRva_reg("CreateBuilding", &kBdCreateRva);               /* Steam_1.0.65 0x57C1E0 */
static unsigned long long kBdProgressRva = 0;  static AddrReg kBdProgressRva_reg("AddConstructionProgress", &kBdProgressRva);  /* Steam_1.0.65 0x558B10 */
static unsigned long long kBdDismantleRva = 0; static AddrReg kBdDismantleRva_reg("AddDismantleProgress", &kBdDismantleRva);   /* Steam_1.0.65 0x2A2820 */
static unsigned long long kBdConfirmDismantleRva = 0; static AddrReg kBdConfirmDismantleRva_reg("BuildingConfirmDismantle", &kBdConfirmDismantleRva);   /* Steam_1.0.65 0x54F410, 1.0.68 0x54FEA0 - T-805 */
static unsigned long long kBdDestroyRva = 0;   static AddrReg kBdDestroyRva_reg("SetDestroyed", &kBdDestroyRva);               /* Steam_1.0.65 0x5567F0 */
// PROBE-END: P082
static unsigned long long kBdSetFactionRva = 0; static AddrReg kBdSetFactionRva_reg("Building_setFaction", &kBdSetFactionRva);  /* Steam_1.0.65 0x556EC0 - P18: the owner-change hook */
static unsigned long long kBdIsForSaleRva = 0; static AddrReg kBdIsForSaleRva_reg("Building_isForSale", &kBdIsForSaleRva);  /* Steam_1.0.65 0x29C4C0 - P18 fold 1 (item 5): Building vt+0x2C0, hooked */
static unsigned long long kBdUpgradeRva = 0; static AddrReg kBdUpgradeRva_reg("BuildingUpgrade", &kBdUpgradeRva);  /* Steam_1.0.65 0x2A49A0, 1.0.68 0x2A49E0 - T-609 U: Building::upgrade (vt+0x258), hooked */
static unsigned long long kBdDowngradeRva = 0; static AddrReg kBdDowngradeRva_reg("BuildingDowngrade", &kBdDowngradeRva);  /* Steam_1.0.65 0x2A4D70, 1.0.68 0x2A4DB0 - T-609 U: Building::downgrade (vt+0x260), hooked */
static unsigned long long kBdBuyMeRva = 0; static AddrReg kBdBuyMeRva_reg("BuildingBuyMeCallback", &kBdBuyMeRva);  /* Steam_1.0.65 0x7ACB20, 1.0.68 0x7AD6C0 - T-805 b: the buy dialogue's answer, hooked */
static unsigned long long kBdSaleValueRva = 0; static AddrReg kBdSaleValueRva_reg("Building_calculateSaleValue", &kBdSaleValueRva);  /* Steam_1.0.65 0x7AC760 - the price the buy callback charges (buyhouse prints it) */
static unsigned long long kOwnRemoveRva = 0; static AddrReg kOwnRemoveRva_reg("Ownerships_removeOwnedObject", &kOwnRemoveRva);  /* Steam_1.0.65 0x7EB550 - P18 fold 1 (item 2) */
static unsigned long long kOwnAddRva = 0; static AddrReg kOwnAddRva_reg("Ownerships_addOwnedObject", &kOwnAddRva);  /* Steam_1.0.65 0x7EBF60 - P18 fold 1 (item 2) */
static unsigned long long kFacHomedInRva = 0; static AddrReg kFacHomedInRva_reg("Faction_platoonsHomedIn", &kFacHomedInRva);  /* Steam_1.0.65 0x6BA220 - P18 fold 1 (item 2): the buy callback's resident list */
static unsigned long long kPlatNewHomeRva = 0; static AddrReg kPlatNewHomeRva_reg("Platoon_findNewHome", &kPlatNewHomeRva);  /* Steam_1.0.65 0x99D810 - P18 fold 1 (item 2) */
static unsigned long long kBdRefundRva = 0;    static AddrReg kBdRefundRva_reg("DismantleRefund", &kBdRefundRva);            /* Steam_1.0.65 0x29DBD0 - build1-d: counted only */
static unsigned long long kBdOrderRva = 0;     static AddrReg kBdOrderRva_reg("AddOrderSelectedCharacters", &kBdOrderRva);   /* Steam_1.0.65 0x7F9280 - house1b: the order gate */
static unsigned long long kBdJobRva = 0;       static AddrReg kBdJobRva_reg("AddJobSelectedCharacters", &kBdJobRva);       /* Steam_1.0.65 0x7F4EF0 - house2: the Shift-job gate */
static unsigned long long kPwChargeRva = 0;    static AddrReg kPwChargeRva_reg("PowerBatteryCharge", &kPwChargeRva);        /* Steam_1.0.65 0x299460, 1.0.68 0x2994A0 - P47: UseableStuff vt+0xD8, the battery charge, hooked */
static unsigned long long kPwInputsRva = 0;    static AddrReg kPwInputsRva_reg("ProductionUpdateInputs", &kPwInputsRva);    /* Steam_1.0.65 0x29A330, 1.0.68 0x29A370 - P47: ProductionBuilding vt+0x5B8, the input (fuel) step, hooked */

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
/* T-609 U: Building::upgrade (1.0.65 0x2A49A0) and downgrade (0x2A4D70) sit at these slots of every building class's vtable read
   (StorageBuildingVt +0x258 / +0x260, both game versions); a class that overrides upgrade (the wall's 0x2A5DE0 upgrades its linked
   walls through +0x258) ends in 0x2A49A0. Each reads its target record first (+0x268 'upgrades to', +0x270 'downgrades to', a
   GameData*) and changes nothing when it is 0. */
const size_t kVtUpgrade = 0x258, kVtDowngrade = 0x260, kVtUpgradeRecord = 0x268, kVtDowngradeRecord = 0x270;
/* build1-c: Building::ConstructionState (the pointer vt+0x228 returns; ANSWERS build1):
   +0 complete, +2 isDismantled, +4 progress, lektor<BuildMaterial*> mats at +0x10 = {allocator 8, u32 count +0x18,
   u32 maxSize +0x1C, BuildMaterial** +0x20}, +0x28 needed. BuildMaterial: GameData* +0, f32 total +8, f32 delivered +0xC. */
const size_t kStMatCount = 0x18, kStMatArray = 0x20, kMatDelivered = 0xC;   /* engine (1.0.65): addConstructionProgress 0x558B10 walks
   +0x18 / +0x20 and reads / writes material+0xC; the save 0x553D60 and load 0x554BC0 carry it as 'construction mats'; state+0x28 is read as needed by 0x558B10 and the health bar 0x546770 (progress / needed) - the engine code that sets needed is not identified (T-902) */
const unsigned int kStMatSane = 1024;  /* a count above this is not a materials list */
const size_t kBNeedsOperating = 0x3A9;  /* ProductionBuilding: set = a worker operates it (update 0x29AD50 runs the step itself only while clear) */
const size_t kBProdInputCount = 0x480;  /* ProductionBuilding: the input records' count (operate 0x2986C0 reads it) */
const size_t kObjData = 0x40;          /* RootObjectBase::data (GameData*) */
const size_t kGdName = 0x28, kGdType = 0x50, kGdStringId = 0x58;   /* GameData (game/gamelayout.h) */
const size_t kBDestroyed = 0x1A1;      /* Building "destroyed" (serialise 0x551BA0) */
const size_t kBBroken = 0x3B6;         /* Building "broken" (save 0x2A1470 / load 0x2A1710, getter 0x0F6CC0); set through vt+0x310 */
const size_t kBOwnDismantled = 0x162;  /* review-build1d D2: Building isDismantled, the piece's OWN byte (0x2A2820 tests it) */
const size_t kBOwnComplete = 0x160;    /* T-903 fold: Building complete, the piece's OWN byte - updatePhysicalWithProgress 0x558680 runs its finish
                                          step (vt+0x240) at progress >= needed only while it is 0 (build/decomp_558680.txt) */
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
const int    kWallCompleteTries = 10;  /* T-221: tries kRetryGap apart while a wall copy's complete-now call leaves it unfinished */
const unsigned int kWallCompleteSlowMul = 20;   /* T-221: after those, tries kRetryGap x this apart for as long as the STATE is held */
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
typedef void (*BdBuyMeFn)(void* b, int result);
BdBuyMeFn orig_buyme = 0;
int g_hkBuyMe = 0;        /* T-805 b: 1 installed, -1 failed, -2 no address row */
long long g_buyHeldAsked = 0, g_buyHeldRan = 0, g_buyHeldGroups = 0, g_buyHeldNotKept = 0, g_buyHeldLines = 0;   /* MAIN THREAD */
/* T-609 U: Building::upgrade / downgrade change a building IN PLACE (the same object) and swap its record (+0x40) for the target
   record, so its mod key (record stringID + position) changes with it. Both are hooked to log the key and record before and after
   each call, and this game's own row moves to the new key (T-609 U fix: BdRekeyMain). The second argument is forwarded whole (0x2A49A0 does not read rdx; its two direct callers pass 0). */
typedef void (*BdUpgradeFn)(void* b, unsigned long long arg);
typedef void* (*BdUpgradeRecordFn)(void* b);
BdUpgradeFn orig_upgrade = 0, orig_downgrade = 0;
int g_hkUpgrade = 0, g_hkDowngrade = 0;   /* 1 installed, -1 failed, -2 no address row */
volatile long long g_upgSeen = 0, g_dngSeen = 0, g_upgOffMain = 0, g_upgKeyChanged = 0, g_upgOwnRow = 0, g_upgLines = 0;   /* ANY THREAD (Interlocked) */
long long g_testUpgrade = 0, g_testUpgradeRefused = 0;   /* MAIN THREAD: the TEST-ONLY lever */
const long long kUpgLineCap = 40;
const int kUpgSidCap = 96;
/* T-609 U fix: an own piece re-keyed (the old key's REMOVE, a row at the new key, its PLACE owed - BdRekeyMain). Off the main thread
   the hook queues the pair: a worker writes under g_rkCs; the main thread only TRIES the lock (BdRekeyDrain), never waits on a worker */
struct BdRekeyRec { char k0[kKeyCap]; char k1[kKeyCap]; char sid[kUpgSidCap]; int down; long gen; };
BdRekeyRec g_rkQ[coopbuild::kBuildRekeyQueueCap];
int g_rkN = 0;                                         /* under g_rkCs */
BdRekeyRec g_rkTake[coopbuild::kBuildRekeyQueueCap];  /* MAIN THREAD: the drain's copy */
CRITICAL_SECTION g_rkCs;
volatile LONG g_rkInit = 0;
volatile long long g_rekeyQueued = 0, g_rekeyDropped = 0;   /* ANY THREAD (Interlocked) */
long long g_rekeyed = 0, g_rekeyBusy = 0, g_rekeyLines = 0;   /* MAIN THREAD */
const long long kRekeyLineCap = 400;
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
const size_t kBResidentSquad = 0xD0;   /* P18 fold 1: Building::residentSquad (a hand: +0x8 its type, 0xB = none) - the buy callback clears it:
   buyMeCallback 0x7ACB20 writes +0xD8 = 0xB, +0xE4 = 0 (8 bytes), +0xE0 = 0 (4 bytes); setResidentSquad 0x552780 fills the same hand (1.0.65, T-902) */
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
long long g_wallGroupMarked = 0, g_wallGroupResolves = 0, g_wallGroupLooks = 0, g_wallGroupCacheCleared = 0, g_wallOwnApplied = 0,
          g_wallSharedApplied = 0, g_wallCompleteApplied = 0, g_wallCompleteNoop = 0, g_wallCompleteSlow = 0, g_keptOwnRecComplete = 0,
          g_keptOwnRefused = 0;   /* T-221: wall progress */
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
long long g_dismantleRefusedMember = 0, g_dismantleRefusedOther = 0, g_testOrder = 0;   /* T-805: the dismantle gate; the order lever */
/* T-805 (owner 797 a): a member's dismantle asked of the owner's game. Asker: sent, owner out of reach, a repeat within kDmAskGapMs,
   answers done / refused. Owner: asks received, done, refused, faulted, dropped (queue full). */
long long g_dmAskSent = 0, g_dmAskOffline = 0, g_dmAskRepeat = 0, g_dmAnsDone = 0, g_dmAnsRefused = 0;
long long g_dmAskRecv = 0, g_dmAskDone = 0, g_dmAskRefused = 0, g_dmAskFaulted = 0, g_dmAskDropped = 0;
volatile LONG g_dmOffMainRefused = 0;   /* T-805 fold: a task 96 on another player's piece refused off the main thread (no line) */
long long g_dmInstantOnCopy = 0;        /* T-805 fold: probe - an instant dismantle (FLT_MAX) on a copy outside the mod's own writes */
struct BdDmAsk { std::string key; int slot; };
std::vector<BdDmAsk> g_dmAskIn;   /* the owner's side: asks waiting for the K2 safe point */
std::string g_dmAskLastKey;
DWORD g_dmAskLastAt = 0;
const DWORD kDmAskGapMs = 3000;   /* the same piece asked again within this gap (a double click) is not sent twice */
int g_inOrderLever = 0;   /* T-805: 1 while `buildtest order` calls the order gate (the gate lines name that road) */
static unsigned long long kBdPlayerIfaceRva = 0; static AddrReg kBdPlayerIfaceRva_reg("PlayerInterfaceGlobal", &kBdPlayerIfaceRva);   /* Steam_1.0.65 0x2133630 (var): PlayerInterface* - the order lever */
long long g_refundSeen = 0, g_refundOffMain = 0;              /* 0x29DBD0 calls, any thread (interlocked) */
long long g_refundCalledOnCopy = 0, g_refundOnCopy = 0;       /* MAIN THREAD: on a coop-peer piece; of those, with delivered mats > 0 (a drop) */
BdRefundFn orig_refund = 0;
int g_hkRefund = 0;
/* build1-f ROSTER (MAIN THREAD) */
long long g_rosterSent = 0, g_rosterStateSent = 0, g_rosterNotLive = 0, g_rosterNoPos = 0, g_rosterLinkDown = 0, g_rosterGone = 0;
long long g_rosterNotMine = 0, g_rosterRounds = 0, g_rosterUnencodable = 0;
long long g_scanRuns = 0, g_scanFound = 0, g_scanRegistered = 0, g_scanFurnitureSkipped = 0, g_scanInteriorSkipped = 0;
long long g_scanDoorSkipped = 0, g_scanKnown = 0, g_scanNoKey = 0, g_scanNoPos = 0, g_scanDismantled = 0, g_scanTruncated = 0;
long long g_scanWorldProduction = 0;   /* own-faction world resource nodes (coopown::WorldNodeOf) the scans left unregistered */
long long g_scanPlayerProduction = 0;  /* own-faction production buildings with no build record that are not world resource nodes, registered */
long long g_restoreWorldNodeDropped = 0, g_copyWorldNodeDropped = 0, g_placeWorldNodeRefused = 0;   /* world resource nodes: saved rows dropped, '~c' rows retired, PLACEs refused */
int BdNodeSayDue(long long n) { return (n >= 1 && (n <= 5 || n % 100 == 0)) ? 1 : 0; }   /* a world-node line: the first 5, then every 100th */
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
    int broken;          /* +0x3B6 read after the call */
    int dismBefore;      /* build1-d: isDismantled read BEFORE the call (-1 not read) - a REMOVE needs a real 0 -> 1 flip */
    int nMats;           /* build1-c: delivered mats read after the call (-1 unreadable) */
    int harvest;         /* a progress call off the main thread on a copy a stand-in holds - the main thread measures its materials (BdHelpHarvest) */
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
    int broken, sentBroken;                                 /* an own piece: +0x3B6 as last read, and what the other games were last sent */
    BdReg() : progress(0.0f), needed(0.0f), complete(0), dismantled(0), destroyed(0), via(0), own(0), nMats(0), sentOk(0),
              sentComplete(0), sentN(0), sentProgress(0.0f), removed(0), hasPos(0), hostForm(0), keySame(1), placeOk(0), floor(0), outside(0), ownerSlot(-1), handed(0),
              nonce(0), gen(g_bdGen), helpBaseOk(0), helpForce(0), helpAckDirty(0), helpAckWait(0), helpAckSeq(0), helpAckAt(0),
              helpTailKnown(0), helpTailSeq(0), helpTailLive(0), stateAnnounce(0), sentDestroyed(0), ownerKnown(0), broken(0), sentBroken(0)
    {
        for (unsigned int i = 0; i < coopbuild::kBuildMaxMats; ++i) { mats[i] = 0.0f; sentMats[i] = 0.0f; helpBase[i] = 0.0f; }
        pos[0] = 0.0f; pos[1] = 0.0f; pos[2] = 0.0f;
        for (int i = 0; i < 3; ++i) placePos[i] = 0.0f;
        placeRot[0] = 1.0f; placeRot[1] = 0.0f; placeRot[2] = 0.0f; placeRot[3] = 0.0f;
    }
};
std::map<std::string, BdReg> g_reg;   /* MAIN THREAD */
/* MAIN THREAD: own pieces this game placed itself (the commit or the placing lever) in the world now loaded -> the area's "loaded
   since" stamp (MyMapLoadedSinceTS of the piece's own sector) at the placement (BdMadeHereMark); at most kMadeHereCap - a piece
   not marked is asked about as usual */
std::map<std::string, double> g_madeHere;
const size_t kMadeHereCap = 4096;
void BdMadeHereMark(void* b, const char* key);
int g_bdKeptDraining = 0;   /* MAIN THREAD: 1 while the kept rows are laid (BdKeptDrain) - a piece made there is not marked as placed here */
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
/* T-903: help per kind - [0] build, [1] add materials, [2] repair. given = an order on a copy let through as help; refused = the
   owner is neither a faction-mate nor has set this player friendly (decision 920 a). repairDamagedHelp = repair orders on a complete
   damaged copy given; repairWorkRec = repair work recorded on such a copy (it goes as HELP_WORK); on the owner's game repairApplied /
   repairWhole (the piece was already whole here) / repairFaulted. */
long long g_helpGivenK[3] = { 0, 0, 0 }, g_helpRefusedK[3] = { 0, 0, 0 };
long long g_repairDamagedHelp = 0, g_repairWorkRec = 0, g_helpRepairApplied = 0, g_repairWhole = 0, g_repairFaulted = 0;
volatile LONG g_helpRefusedOff = 0;   /* T-903: a help order of a player who is no faction-mate, refused off the main thread (counted, no line) */
long long g_helpApplyRefused = 0;     /* T-903 fold (review MED 2): the owner's game - help work from a player it allows no help, refunded */
long long g_helpApplyHeld = 0;        /* T-903 re-check fold (item 2): help work held - this game's slot or the sender's standing not known yet */
long long g_deadHandlesIgnored = 0;   /* T-903 (F1026): copy searches inside a host whose dead handles counted as no piece (BuildDeadHandlesIgnore) - the copy made */
long long g_helpRunRefused = 0;       /* T-903 fold (review MED 2): the helper's game - the engine's work on a copy of a player who allows no help, not done */
long long g_helpHarvested = 0, g_helpHarvestRefused = 0;   /* the helper's game: materials put into a copy off the main thread - recorded as help / refused and put back */
long long g_helpHarvestCarried = 0, g_helpHarvestLost = 0;   /* refused items not handed back at once: carried as an owed entry (handed back through the owner's refund) / the engine's add faulted (never retried) */
long long g_helpHarvestNoBase = 0;   /* looks at a copy whose baseline was not known yet - nothing measured */
std::vector<std::string> g_bdHarvestDue;   /* MAIN THREAD: live keys of copies whose materials the next K2 safe point measures (BdHarvestDrain), each once */
int BdHelpKind(int t) { return t == coopbuild::kBuildTaskBuild ? 0 : (t == coopbuild::kBuildTaskAddMaterials ? 1 : 2); }
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
    int keptRow;           /* KEPT: the kept row (keptwire.h) this restore lays; -1 = none */
    void* keptMade;        /* KEPT: the piece this entry's make produced (BdMarkRestored), read by BdKeptPendDone at the same safe point */
    BdPending() : fromSlot(-1), keptRow(-1), keptMade(0) {}
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
    int completeTries;   /* T-221: tries this STATE was kept because a wall copy's complete-now call left it unfinished */
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
/* T-903: the counts of the last host search, read by the copy pending line. g_bdExactNote: BdFindInHost. g_bdPiecesNote: BdHostPieces
   (keyed = -1), and BdFindInHostNear sets its keyed. listed = the interior list's own size (-1 unreadable), n = entries read, cut =
   the walk stopped early (or BdHostPieces' cap), torn = a zone came down during the walk, typed = building entries, resolved = those
   whose hand gave a building with the same five ids, keyed = resolved pieces a key was built for. MAIN THREAD. */
struct BdHostSearchNote
{
    int listed, n, cut, torn, typed, resolved, keyed;
};
BdHostSearchNote g_bdExactNote, g_bdPiecesNote;
void BdHostNoteSet(BdHostSearchNote* o, int listed, int n, int cut, int torn, int typed, int resolved, int keyed)
{
    o->listed = listed; o->n = n; o->cut = cut; o->torn = torn; o->typed = typed; o->resolved = resolved; o->keyed = keyed;
}
void* BdFindInHost(void* host, const char* key, const char* alt, int* verdict)
{
    int listed = -1, trunc = 0, torn = 0;
    const int n = InteriorPieceIds(host, g_bdIds, kBdInteriorCap, &listed, &trunc, &torn);
    void* found = 0;
    int hits = 0, typed = 0, keyed = 0, resolved = 0;   /* review-build1e D1: 'none' only when every building piece was resolved and keyed */
    for (int i = 0; i < n; ++i)
    {
        const unsigned int* f = g_bdIds + (size_t)i * 5;
        if (f[0] != 0 || (f[3] == 0 && f[4] == 0)) continue;   /* BUILDING (0) only, as 0x54A220 keeps */
        ++typed;
        hand h(f[3], f[4], (itemType)f[0], f[1], f[2]);
        void* b = (void*)h.asBuilding();
        unsigned int own[5];
        if (b == 0 || ObjectHandIds(b, own) == 0 || std::memcmp(own, f, sizeof(own)) != 0) continue;
        ++resolved;
        char k[kKeyCap];
        k[0] = 0;
        if (ObjectPositionKey(b, k, kKeyCap, "", 3, 1) == 0) continue;
        ++keyed;
        if (std::strcmp(k, key) != 0 && (alt == 0 || alt[0] == 0 || std::strcmp(k, alt) != 0)) continue;
        if (b != found) { found = b; ++hits; }
    }
    BdHostNoteSet(&g_bdExactNote, listed, n, trunc, torn, typed, resolved, keyed);
    if (hits == 1) { *verdict = kObjKeyFound; return found; }
    if (hits > 1) { *verdict = kObjKeyAmbiguous; return 0; }
    *verdict = (listed >= 0 && trunc == 0 && torn == 0 && n == listed && keyed == typed) ? kObjKeyNone : kObjKeyTruncated;
    return 0;
}
/* KEPT: every building piece inside a host, resolved and identity-checked as BdFindInHost does. *complete = 1 when the host's list
   was read whole and every piece on it resolved. Returns the pieces written to out (at most cap). MAIN THREAD. */
int BdHostPieces(void* host, void** out, int cap, int* complete)
{
    int listed = -1, trunc = 0, torn = 0;
    const int n = InteriorPieceIds(host, g_bdIds, kBdInteriorCap, &listed, &trunc, &torn);
    int got = 0, typed = 0, resolved = 0;
    for (int i = 0; i < n; ++i)
    {
        const unsigned int* f = g_bdIds + (size_t)i * 5;
        if (f[0] != 0 || (f[3] == 0 && f[4] == 0)) continue;   /* BUILDING (0) only, as 0x54A220 keeps */
        ++typed;
        hand h(f[3], f[4], (itemType)f[0], f[1], f[2]);
        void* b = (void*)h.asBuilding();
        unsigned int own[5];
        if (b == 0 || ObjectHandIds(b, own) == 0 || std::memcmp(own, f, sizeof(own)) != 0) continue;
        ++resolved;
        if (got < cap) out[got++] = b; else trunc = 1;
    }
    BdHostNoteSet(&g_bdPiecesNote, listed, n, trunc, torn, typed, resolved, -1);
    if (complete != 0) *complete = (listed >= 0 && trunc == 0 && torn == 0 && n == listed && resolved == typed) ? 1 : 0;
    return got;
}
/* KEPT: furniture in its host by the window every other search uses (the exact key, else BoxKeyNearKey: the same sector and record, x
   and z within a tenth - a layout key drifts across a reload). *verdict as BdFindInHost's: found, ambiguous (two pieces in the window),
   none (the host's whole list read and every piece keyed), truncated. MAIN THREAD. */
void* BdFindInHostNear(void* host, const char* key, int* verdict)
{
    static void* pcs[kBdInteriorCap];
    int complete = 0;
    const int n = BdHostPieces(host, pcs, kBdInteriorCap, &complete);
    void* found = 0;
    int hits = 0, keyed = 0;
    for (int i = 0; i < n; ++i)
    {
        char k[kKeyCap];
        k[0] = 0;
        if (ObjectPositionKey(pcs[i], k, kKeyCap, "", 3, 1) == 0) continue;
        ++keyed;
        if (std::strcmp(k, key) != 0 && BoxKeyNearKey(k, key) == 0) continue;
        if (pcs[i] != found) { found = pcs[i]; ++hits; }
    }
    g_bdPiecesNote.keyed = keyed;
    if (hits == 1) { *verdict = kObjKeyFound; return found; }
    if (hits > 1) { *verdict = kObjKeyAmbiguous; return 0; }
    *verdict = (complete != 0 && keyed == n) ? kObjKeyNone : kObjKeyTruncated;
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
int g_btIntoOff = 0;     /* `into ... <dx>,<dz>`: form 0 at that offset from the host's centre (g_btIntoDx / g_btIntoDz) */
float g_btIntoDx = 25.0f, g_btIntoDz = 25.0f;
float g_btDx = 30.0f, g_btDz = 0.0f, g_btAmount = 0.0f;
/* TEST-ONLY: the piece the last `buildtest place` made on this game, and its key then. The address is only COMPARED against the loaded
   buildings (BuildTestPlacedLive), never read on its own, so a piece that unloaded or was freed no longer matches and the lever refuses */
void* g_btPlaced = 0;
char g_btPlacedKey[kKeyCap] = { 0 };

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
/* T-221: a wall's progress record (vt+0x228). Walls joined in a group (at placement, or when their area loads) share the record of
   the group's lead, which holds it at its own +0x160 and lists the group (count +0x390, which the engine's wall completion 0x561A20
   walks to complete every member); a wall made at runtime starts ungrouped and keeps its own record with a count of 0. */
const size_t kWallOwnRecord = 0x160;
const size_t kWallGroupCount = 0x390;
uintptr_t BdWallRecordPod(void* b)   /* the record's address, 0 = unreadable */
{
    __try { return (uintptr_t)((BdStateFn)((*(void***)b)[kVtState / 8]))(b); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int BdWallGroupCountPod(void* b)   /* the wall's group count (+0x390), -1 = unreadable or not a plausible count */
{
    __try
    {
        const unsigned int n = *(const unsigned int*)((const char*)b + kWallGroupCount);
        return n <= 100000u ? (int)n : -1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int BdWallOwnRecord(void* b)   /* coopbuild::BuildWallKeepsOwnRecord: 1 = the record is the wall's alone, 0 = a group's, -1 = unreadable */
{
    const uintptr_t r = BdWallRecordPod(b);
    if (r == 0) return -1;
    return coopbuild::BuildWallKeepsOwnRecord(r == (uintptr_t)b + kWallOwnRecord ? 1 : 0, BdWallGroupCountPod(b));
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
/* A production building's needs-operating flag (+0x3A9: 1 set, 0 clear) and input count (+0x480), -1 each when unreadable.
   Plain values out (no C++ object in the __try). */
void BdProdNodeReadPod(const void* b, int* needsOp, int* inputs)
{
    *needsOp = -1; *inputs = -1;
    if (b == 0) return;
    __try
    {
        *needsOp = (*((const unsigned char*)b + kBNeedsOperating) != 0) ? 1 : 0;
        const unsigned int n = *(const unsigned int*)((const char*)b + kBProdInputCount);
        *inputs = (n > 1024u) ? -1 : (int)n;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *needsOp = -1; *inputs = -1; }
}
/* The piece's game-data record's "is foliage" mark (GameData boolFields): 1 set, 0 clear or not in the record, -1 unreadable.
   The base game sets it on the world's ore rocks and not on the mines and wells players build (coopown::WorldNodeOf). Plain
   values only in the __try (C2712): the map is walked and each key compared by its bytes; no std::string is made. */
int BdFoliagePod(const void* b)
{
    if (b == 0) return -1;
    __try
    {
        const ::GameData* gd = *(const ::GameData* const*)((const char*)b + kObjData);
        if (gd == 0) return -1;
        const GameHashMap<std::string, bool>::type& m = gd->boolFields;
        for (GameHashMap<std::string, bool>::type::const_iterator it = m.begin(); it != m.end(); ++it)
            if (it->first.size() == 10 && std::memcmp(it->first.c_str(), "is foliage", 10) == 0) return it->second ? 1 : 0;
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* 1 when the live piece is a world resource node (coopown::WorldNodeOf). hasRow is the caller's: the restore, the copy list
   and a PLACE judge the piece itself (0) - the row or message in hand is the record under test. */
int BdWorldNodeLive(void* b, int hasRow)
{
    if (b == 0 || ProductionBuildingIs(b) == 0) return 0;
    int needsOp = -1, inputs = -1;
    BdProdNodeReadPod(b, &needsOp, &inputs);
    return coopown::WorldNodeOf(1, needsOp, inputs, hasRow, BdFoliagePod(b));
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
   site sets all 7 of its arguments (the disassembly; the decompile shows 3) and the other four are not read here, so it is not
   called; their homes are moved, the AI re-plans from those. The buy
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
int BdIsForSalePod(void* b)   /* Building::isForSale through the vtable (vt+0x2C0) - with the hook, this mod's answer: 1 / 0, -1 faulted */
{
    __try { return ((BdIsForSaleFn)((*(void***)b)[0x2C0 / 8]))(b) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* T-805 b: the ENGINE's own isForSale, never this mod's answer - the hook's original when the building's vtable slot is the hooked
   function (directly, or through a one-jump stub), else the slot itself (a class with its own isForSale is not hooked). 1 / 0, -1 faulted */
int BdIsForSaleOrigPod(void* b)
{
    const uintptr_t target = (kBdIsForSaleRva != 0) ? (uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kBdIsForSaleRva : 0;
    __try
    {
        const unsigned char* const slot = (const unsigned char*)((*(void***)b)[0x2C0 / 8]);
        const unsigned char* dest = slot;
        if (slot != 0 && slot[0] == 0xE9) dest = slot + 5 + *(const int*)(slot + 1);
        if (orig_isforsale != 0 && g_hkForSale == 1 && target != 0 && ((uintptr_t)slot == target || (uintptr_t)dest == target)) return orig_isforsale(b) ? 1 : 0;
        return ((BdIsForSaleFn)slot)(b) ? 1 : 0;
    }
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
/* the classes whose vtables were read with setBroken 0x0F6CD0 at +0x310 and whose save / load write the flag +0x3B6 (0x2A1470 /
   0x2A1710, directly or through the parent's): UseableStuff and every class under it (keptwire::ClassUseable); the base
   Building's, GatewayBuilding's, DoorStuff's and WallBuilding's +0x310 are other functions */
int BdIsBrokenClass(const char* cls)
{
    return keptwire::ClassUseable(cls);
}
/* the engine's own setBroken (vt+0x310 -> 0x0F6CD0) through the vtable; the flag it sets is +0x3B6 (kBBroken) */
int BdSetBrokenCall(void* b, bool v)
{
    __try { ((BdSetBoolFn)((*(void***)b)[kVtSetBroken / 8]))(b, v); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
long long g_brokenSkipped = 0;
/* setBroken on a live piece whose class has it at vt+0x310 (BdIsBrokenClass); any other class is logged and left alone (0) */
int BdSetBrokenPod(void* b, bool v)
{
    char cls[64];
    cls[0] = 0;
    const unsigned int vt = (b != 0) ? PolicyVtableRvaPod(b) : 0u;
    if (vt == 0 || PolicyClassNamePod(vt, cls, (int)sizeof cls) == 0) cls[0] = 0;
    if (BdIsBrokenClass(cls) == 0)
    {
        ++g_brokenSkipped;
        if (g_brokenSkipped <= 20)
            DebugLog(std::string("[BUILD] setBroken skipped: class '") + (cls[0] != 0 ? cls : "?") + "' has no setBroken at vt+0x310"
                     " - the piece's broken flag is left as it is (brokenSkipped=" + std::to_string(g_brokenSkipped) + ")");
        return 0;
    }
    return BdSetBrokenCall(b, v);
}
int BdByteAtOr0(const void* p, size_t off);
/* the broken flag (+0x3B6, kBBroken) of a live piece, read only on a class whose vt+0x310 is setBroken (BdIsBrokenClass) - the flag
   is that class's field; any other class reads 0 */
int BdBrokenRead(void* b)
{
    if (b == 0) return 0;
    char cls[64];
    cls[0] = 0;
    const unsigned int vt = PolicyVtableRvaPod(b);
    if (vt == 0 || PolicyClassNamePod(vt, cls, (int)sizeof cls) == 0) return 0;
    return BdIsBrokenClass(cls) != 0 ? BdByteAtOr0(b, kBBroken) : 0;
}
int BdDismantleCallPod(void* b, float amount, int* ret)
{
    __try { *ret = (int)(((BdDismantleFn)((*(void***)b)[kVtDismantle / 8]))(b, amount) & 0xFF); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int BdDismOf(void* b, int stateDism);   /* review-build1d D2, defined below in this same namespace */
/* T-609 fold 1 (F4) + re-check: after a dismantle this mod ran, the building stays in the memoised building list only when the
   call did not fault and the building reads as NOT dismantled (BdReadState + BdDismOf, as REMOVE and P14 judge it - REMOVE retries
   it); a faulted call or an unreadable state drops it (it may already be queued to be freed - the safe direction). ItemsBuildingGone
   counts the ones that stay. MAIN THREAD. */
void BdGoneIfDismantled(void* b, int callOk)
{
    float p = 0, n = 0;
    int c = 0, d = 0, x = 0;
    const int stOk = callOk != 0 ? BdReadState(b, &p, &n, &c, &d, &x) : 0;
    const int dism = stOk != 0 ? BdDismOf(b, d) : 0;
    ItemsBuildingGone(b, walkdrop::GoneAfterDismantle(callOk, stOk, dism));
}
/* T-609 part 2 (H4): every dismantle this mod runs (REMOVE, the P14 tombstone, the buildtest lever) takes the piece out of the
   memoised building list straight after the call - the engine frees it a frame later, before that list's next refresh. */
int BdDismantlePod(void* b, float amount, int* ret)
{
    const int ok = BdDismantleCallPod(b, amount, ret);
    BdGoneIfDismantled(b, ok);   /* fold 1 (F4): only a call that took it down */
    return ok;
}
/* T-805: Building::confirmDismantle 0x54F410 (void(Building*, int answer, ...), decomp 54f410): with answer 2 it tears down the
   interior (0x54EA40), runs vt+0x248(FLT_MAX), vt+0xC8, and hands every mounted building to GameWorld::destroy - the panel's own
   dismantle, mounted pieces included. MAIN THREAD. 1 = called, 0 = no address or faulted. */
typedef void (*BdConfirmDismantleFn)(void* b, int answer, void* a3, void* a4);
int BdConfirmDismantleCallPod(void* b)
{
    __try
    {
        const BdConfirmDismantleFn fn = (BdConfirmDismantleFn)((uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kBdConfirmDismantleRva);
        fn(b, 2, 0, 0);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int BdConfirmDismantlePod(void* b)
{
    if (kBdConfirmDismantleRva == 0 || b == 0) return 0;
    const int ok = BdConfirmDismantleCallPod(b);
    BdGoneIfDismantled(b, ok);   /* T-609 (H4), fold 1 (F4): as BdDismantlePod. Its MOUNTED pieces (handed to GameWorld::destroy inside
                                    0x54F410) are not dropped: this project has no reader of a building's mounted list (0x54AF40 only
                                    counts them) and GameWorld::destroy is not hooked, so they leave at the next drain's fresh walk -
                                    the exposure there was before T-609 (fold 1 F6: FOUND, NOT FIXED) */
    return ok;
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
void BdQueueEvent(int kind, void* b, float amount, int onMain, int dismBefore, int harvest = 0)
{
    BdEvent e;
    std::memset(&e, 0, sizeof e);
    e.kind = kind;
    e.harvest = harvest;
    e.amount = amount;
    e.onMain = onMain;
    e.dismBefore = dismBefore;   /* build1-d */
    e.keyOk = ObjectPositionKey(b, e.key, kKeyCap, "", 3, 0);
    if (e.keyOk == 0) e.key[0] = 0;
    e.stateOk = BdReadState(b, &e.progress, &e.needed, &e.complete, &e.dismantled, &e.destroyed);
    if (e.stateOk != 0) e.dismantled = BdDismOf(b, e.dismantled);   /* review-build1d D2: a wall segment's own +0x162 */
    e.broken = (e.stateOk != 0) ? BdBrokenRead(b) : 0;   /* the piece's broken flag, carried by its STATE */
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
/* a copy's baseline set to the amounts given (n slots; the rest 0) */
void BdHelpBaseSet(BdReg& r, const float* m, int n)
{
    for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i) r.helpBase[i] = (i < n) ? m[i] : 0.0f;
    r.helpBaseOk = 1;
}
void BdHelpBaseRead(BdReg& r, void* b)
{
    float m[coopbuild::kBuildMaxMats];
    int tot = -1;
    const int n = BdReadMats(b, m, (int)coopbuild::kBuildMaxMats, &tot);
    if (n < 0) { r.helpBaseOk = 0; return; }
    BdHelpBaseSet(r, m, n);
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
/* help1, MAIN THREAD: work measured on a copy recorded for the owner - progress dP, the ruin reset, the material gains dm[0..nd) - as
   pending help (BdHelpTick turns it into HELP_WORK), with each gained material's item sid (a HELP_GONE refund names it) and the copy's
   position (a refund's receiver). key = the row's key. */
void BdHelpRecordWork(const std::string& key, const BdReg& r, void* b, float dP, int reset, float need, const float* dm, int nd)
{
    BdHelpRow& h = BdHelpRowFor(key, r);
    h.pendP += dP;
    for (int i = 0; i < nd; ++i) h.pendM[i] += dm[i];
    if (nd > h.pendN) h.pendN = nd;
    if (reset != 0) h.pendReset = 1;
    if (h.pendAny == 0) { h.pendAny = 1; h.pendSince = ::GetTickCount(); }
    h.need = need;
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
/* help1, MAIN THREAD, from detour_progress right after the engine's call on a copy (the owner's stand-in holds it; not the mod's own
   write). p0 / n0 / m0 were read before the call; needed was raised for it (not on a ruin). */
void BdHelpAfter(void* b, float p0, float n0, int ruin, int m0n, const float* m0, int repair)   /* T-903: repair = a complete damaged copy */
{
    /* help1 fold (review LOW 11): the raised needed was already put back by BdProgressRaised's __finally (only while it still held the raised value) */
    float p1 = 0.0f, n1 = 0.0f;
    int c1 = 0, d1 = 0, x1 = 0;
    if (BdReadState(b, &p1, &n1, &c1, &d1, &x1) == 0) return;
    char k[kKeyCap];
    k[0] = 0;
    const int keyOk = ObjectPositionKey(b, k, kKeyCap, "", 3, 0);
    int capped = 0;
    const float capTo = coopbuild::BuildHelpCopyCap(p1, n1, (ruin != 0 || x1 != 0 || repair != 0) ? 1 : 0, &capped);   /* T-903: a complete piece is never capped */
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
        if (coopbuild::BuildHelpMatGain(m1, m1n, r.helpBaseOk, r.helpBase, m0n, m0, dm) != 0) any = 1;
        nd = m1n;
    }
    if (m1n >= 0) BdHelpBaseSet(r, m1, m1n);
    if (any == 0) return;
    if (repair != 0)   /* T-903: repair on a complete damaged copy - the health it restored goes to the owner's game as help work */
    {
        ++g_repairWorkRec;
        char rl[320];
        _snprintf_s(rl, sizeof(rl), _TRUNCATE, "[BUILD] help repair work key=%.63s %.2f->%.2f/%.2f on a complete damaged copy - goes to the owner's game as help work; repairWorkRec=%lld",
                    k, p0, p1, n1, g_repairWorkRec);
        BdLogHelpFreq(rl);
    }
    BdHelpRecordWork(it->first, r, b, dP, reset, n1, dm, nd);
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
   while a road to another game is open - net::SideRoadOpen, the world server's or the session link (all of them again when one
   opens, and any left unconfirmed for kHelpResendMs; the owner applies each seq once, so a re-send is answered as a dup). help1 fold (review MED 5/6): a new
   entry is made only once the owner's STATE told this session its last applied seq (the copy row's tail) - its seq is past that, past
   this row's own and past pp.help's (BuildHelpStartSeq). help1 fold (MED 8): the owed file is saved at most every kHelpSaveMs and at
   link-down, not at every new entry. */
void BdHelpTick()
{
    const int up = net::SideRoadOpen() ? 1 : 0;   /* the road SendBuildToSlot itself needs - the world server's or the session link; a game joined through the world server has no session link */
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
                    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help work key=%.63s owner=slot%d seq=%u dP=%.3f dMats=%.2f owed unacked=%u (no road to another game - sent when one opens)",
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
/* T-903 (decisions 920 / 920 a, owner 2026-10-17), MAIN THREAD: may this game's player help build, add materials to or repair a piece
   of the player whose stand-in `owner` is? Yes for a faction-mate (PolicyTeammateFaction - the T-805 dismantle gate's own any-thread
   test), or when that player has set this player FRIENDLY: the PLAYERS tab's friendly / neutral / enemy buttons (and `relate`) set the
   SETTER's relation towards the other player's faction to +100 / 0 / -100 through the engine's setRelation (relations.cpp RelateSet),
   one value per side; the owner's side is forwarded and written here as "stand-in -> this player's faction", and read with
   RelationsStanceLevels as `them` - the side the PLAYERS tab and the name tag show as theirs (friendly = nametag::LevelOf: 50 or more,
   or the ally flag). The owner's side is the one asked: it is the owner's building. coopbuild::HelpAllowed holds the rule. */
int BdHelpAllowedHere(::Faction* owner, int* mate, int* friendly)
{
    *mate = (owner != 0 && PolicyTeammateFaction((void*)owner)) ? 1 : 0;
    *friendly = 0;
    ::Faction* me = LocalPlayerFaction();
    if (owner != 0 && me != 0)
    {
        int you = nametag::kUnknown, them = nametag::kUnknown;
        RelationsStanceLevels(me, owner, &you, &them);
        *friendly = (them == nametag::kFriendly) ? 1 : 0;
    }
    return coopbuild::HelpAllowed(*mate, *friendly);
}
/* T-903, MAIN THREAD: a help order (build / add materials / repair) on a copy of a player who allows this player no help - not given,
   counted per kind, logged. Returns 2 (BdHelpOrder's "not given"). */
int BdHelpRefuse(int t, ::Faction* owner, const char* key, const char* road)
{
    const int kd = BdHelpKind(t);
    ++g_helpRefusedK[kd];
    char line[420];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] order task=%d (%s) on a copy of slot%d key=%s NOT GIVEN (%s) - that player is not a faction-mate and has not set this player friendly (decision 920 a); helpRefused[build,materials,repair]=%lld,%lld,%lld",
                t, kd == 0 ? "build" : (kd == 1 ? "materials" : "repair"), StandInSlotOf(owner), key, road, g_helpRefusedK[0], g_helpRefusedK[1], g_helpRefusedK[2]);
    BdLogHelp(line);
    return 2;
}
/* help1 (owner 172): a build or add-materials order on a COPY of another player's piece (its faction is that player's stand-in) is help
   - given and counted (orderOnCopy); the helper's game then sends the work to the owner (HELP_WORK). NOT on a wall copy: a wall's progress
   call (0x5594A0) does reach the hook (it calls 0x558B10 on the wall holding the progress record), but the helper's work on a wall copy is
   not yet shown to reach the owner's wall and to leave the copy short of complete here; refused and counted (helpWallRefused) until it is
   (T-221). 1 = help given, 2 = refused (a wall copy), 0 = not such an order. */
int BdHelpOrder(int t, void* subject, const char* road)
{
    if (t == coopbuild::kBuildTaskRepair)   /* P15: a repair on a RUIN copy - help (the owner's game runs the ruin reset first); a wall ruin copy - refused */
    {
        ::Faction* rf = (::Faction*)BdOwnerPod(subject);
        int rx = 0;
        BdByteAt(subject, kBDestroyed, &rx);
        float rp = 0.0f, rn = 0.0f;
        int rc = 0, rd = 0, rx2 = 0;
        const int rdmg = (BdReadState(subject, &rp, &rn, &rc, &rd, &rx2) != 0 && rd == 0) ? coopbuild::BuildPieceDamaged(rc, rx > 0 ? 1 : 0, rp, rn) : 0;   /* T-903 */
        const int rv = coopbuild::BuildRepairHelpVerdict(t, (rf != 0 && IsStandInFaction(rf)) ? 1 : 0, rx > 0 ? 1 : 0, rdmg, BdIsWall(subject));   /* T-903: a damaged copy too */
        if (rv == coopbuild::kBuildRepairOrderNone) return 0;
        char rk[kKeyCap];
        rk[0] = 0;
        if (ObjectPositionKey(subject, rk, kKeyCap, "", 3, 0) == 0) { rk[0] = '?'; rk[1] = 0; }
        char rl[380];
        if (rv == coopbuild::kBuildRepairOrderRefused)
        {
            ++g_repairRefused;
            _snprintf_s(rl, sizeof(rl), _TRUNCATE, "[BUILD] order task=95 (repair) on a WALL %s copy of slot%d key=%s NOT GIVEN (%s) - only the owner's game repairs its wall (wall work is not synced, T-221); repairRefused=%lld",
                        rx > 0 ? "ruin" : "damaged", StandInSlotOf(rf), rk, road, g_repairRefused);
            BdLogHelp(rl);
            return 2;
        }
        int rmate = 0, rfr = 0;
        if (BdHelpAllowedHere(rf, &rmate, &rfr) == 0) return BdHelpRefuse(t, rf, rk, road);   /* T-903 (decision 920 a) */
        ++g_helpGivenK[2];
        if (rv == coopbuild::kBuildRepairOrderDamagedHelp)   /* T-903: the health the helper restores on its copy goes to the owner's game */
        {
            ++g_repairDamagedHelp;
            _snprintf_s(rl, sizeof(rl), _TRUNCATE, "[BUILD] order task=95 (repair) on a damaged copy of slot%d key=%s %.2f/%.2f - help (%s, %s): the repair work goes to the owner's game as help work; repairDamagedHelp=%lld",
                        StandInSlotOf(rf), rk, rp, rn, road, rmate != 0 ? "faction-mate" : "set friendly", g_repairDamagedHelp);
            BdLogHelp(rl);
            return 1;
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
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] order task=%d on a WALL copy of slot%d key=%s NOT GIVEN (%s) - help on a wall copy is not carried to the owner's game yet (T-221); helpWallRefused=%lld",
                    t, StandInSlotOf(f), k, road, g_helpWallRefused);
        BdLogHelp(line);
        return 2;
    }
    int hmate = 0, hfr = 0;
    if (BdHelpAllowedHere(f, &hmate, &hfr) == 0) return BdHelpRefuse(t, f, k, road);   /* T-903 (decision 920 a) */
    ++g_helpGivenK[BdHelpKind(t)];
    ++g_orderOnCopy;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] order task=%d on a copy of slot%d key=%s - help (%s); orderOnCopy=%lld",
                t, StandInSlotOf(f), k, road, g_orderOnCopy);
    BdLogHelp(line);
    return 1;
}
/* help1 fold 2 (re-check LOW 7), ANY THREAD (a guarded vtable call, RTTI and pointer compares - BdIsWall / IsStandInFaction are any-thread):
   off the main thread the wall-copy refusal is made too (the handed test is not) - counted (helpWallRefusedOff, the REPORT line), no
   line. 1 = refused. */
/* T-903: off the main thread a help order (build / add materials / repair) on a copy of a player who is no faction-mate is refused
   too (helpRefusedOff, no line): the friendly setting is not read there - its accessor is main-thread only - so off the main thread
   only a faction-mate may help. T-903 fold (review LOW 4): a repair on a wall copy, ruin or damaged, is refused here too. */
int BdHelpWallOff(int t, void* subject)
{
    if (t != coopbuild::kBuildTaskBuild && t != coopbuild::kBuildTaskAddMaterials && t != coopbuild::kBuildTaskRepair) return 0;
    ::Faction* f = (::Faction*)BdOwnerPod(subject);
    const int standIn = (f != 0 && IsStandInFaction(f)) ? 1 : 0;
    if (t != coopbuild::kBuildTaskRepair && coopbuild::BuildHelpOrderVerdict(t, standIn, BdIsWall(subject)) == coopbuild::kBuildHelpOrderWallRefused)
    {
        ::InterlockedIncrement(&g_helpWallRefusedOff);
        return 1;
    }
    if (t == coopbuild::kBuildTaskRepair && standIn != 0)
    {   /* T-903 fold (review LOW 4): a repair on a wall ruin copy or a damaged wall copy is refused off the main thread too - the same
           BuildRepairHelpVerdict BdHelpOrder asks on the main thread (guarded reads only); counted helpWallRefusedOff */
        int rx = 0;
        BdByteAt(subject, kBDestroyed, &rx);
        float rp = 0.0f, rn = 0.0f;
        int rc = 0, rd = 0, rx2 = 0;
        const int rdmg = (BdReadState(subject, &rp, &rn, &rc, &rd, &rx2) != 0 && rd == 0) ? coopbuild::BuildPieceDamaged(rc, rx > 0 ? 1 : 0, rp, rn) : 0;
        if (coopbuild::BuildRepairHelpVerdict(t, 1, rx > 0 ? 1 : 0, rdmg, BdIsWall(subject)) == coopbuild::kBuildRepairOrderRefused)
        {
            ::InterlockedIncrement(&g_helpWallRefusedOff);
            return 1;
        }
    }
    if (standIn != 0 && coopbuild::HelpAllowed(PolicyTeammateFaction((void*)f) ? 1 : 0, 0) == 0)
    {
        ::InterlockedIncrement(&g_helpRefusedOff);
        return 1;
    }
    return 0;
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
/* T-805 (owner 797 a): a team member may dismantle a teammate's piece - as a DISMANTLE_ASK the owner's game carries out */
int BdMemberMayDismantle() { return 1; }
/* T-805, MAIN THREAD: a dismantle order (task 96) on a piece another player owns - its faction is that player's stand-in, or the
   save's player record that names a slot - handed or not. A player of another faction gets none (decision 548). A team member's is
   not given to the engine here: this game asks the owner's game (DISMANTLE_ASK), which dismantles its own piece the engine's way -
   the refund drops the materials at the piece there and reaches every game by the ground road, and its REMOVE takes the copy here.
   Nothing changes on this game before the owner's game acts, so a refused or lost ask loses nothing and a repeated one cannot dismantle
   twice. With the owner's game out of reach the order is refused here. 1 = not given to the engine (logged, counted). */
int BdDismantleGate(int t, void* subject, const char* road)
{
    if (t != coopbuild::kBuildTaskDismantle || subject == 0) return 0;
    ::Faction* f = (::Faction*)BdOwnerPod(subject);
    if (f == 0 || IsPlayerFaction(f)) return 0;
    int slot = StandInSlotOf(f);
    if (slot < 0) slot = StandInRecordSlot(f);
    const int other = (slot >= 0 || IsStandInFaction(f)) ? 1 : 0;
    const int member = (other != 0 && PolicyTeammateFaction((void*)f)) ? 1 : 0;
    char k[kKeyCap];
    k[0] = 0;
    const int keyOk = ObjectPositionKey(subject, k, kKeyCap, "", 3, 0);
    if (keyOk == 0) { k[0] = '?'; k[1] = 0; }
    /* the owner's game reached by its own slot only - never the raw session peer (-2), which may be a third game */
    const int reach = (member != 0 && slot >= 0 && keyOk != 0 && net::BuildSlotTarget(slot) == slot) ? 1 : 0;
    const int v = coopbuild::BuildDismantleOrderVerdict(t, other, member, BdMemberMayDismantle(), reach);
    if (v == coopbuild::kBuildDmOrderGoes) return 0;
    char line[480];
    if (v == coopbuild::kBuildDmOrderAsk)
    {
        /* the owner files the piece under the builder's key: a copy whose own created key differs asks by that key */
        std::string askKey(k);
        std::map<std::string, BdReg>::iterator ar = BdRowByLiveKey(askKey);   /* the row's own key, an alias only while the row still carries it */
        if (ar != g_reg.end()) askKey = ar->first;
        const DWORD now = ::GetTickCount();
        if (g_dmAskLastKey == askKey && (DWORD)(now - g_dmAskLastAt) < kDmAskGapMs) { ++g_dmAskRepeat; return 1; }
        coopbuild::BuildMsg a;
        a.kind = coopbuild::kBuildDismantleAsk;
        a.key = askKey;
        a.reason = coopbuild::kBuildDmAsk;
        const bool sent = coopbuild::BuildEncodable(a) && net::SendBuildToSlot(a, slot);
        if (sent)
        {
            ++g_dmAskSent;
            g_dmAskLastKey = askKey; g_dmAskLastAt = now;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] -> DISMANTLE_ASK key=%.63s (live key %.63s) to slot%d (member) via %s - the owner's game dismantles its piece; its materials drop at the piece and its REMOVE takes the copy here; dmAskSent=%lld",
                        askKey.c_str(), k, slot, road, g_dmAskSent);
        }
        else
        {
            ++g_dmAskOffline;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] order task=96 on slot%d's piece key=%.63s NOT GIVEN (member) via %s - the owner's game could not be reached (DISMANTLE_ASK not sent); dmAskOffline=%lld",
                        slot, k, road, g_dmAskOffline);
        }
        BdLogCapped(line);
        return 1;
    }
    if (v == coopbuild::kBuildDmOrderOwnerAway) ++g_dmAskOffline;
    else if (member != 0) ++g_dismantleRefusedMember;
    else ++g_dismantleRefusedOther;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] order task=96 on slot%d's piece key=%.63s NOT GIVEN (%s) via %s - %s; dismantleRefusedMember=%lld dismantleRefusedOther=%lld dmAskOffline=%lld",
                slot, k, member != 0 ? "member" : "not a member", road,
                v == coopbuild::kBuildDmOrderOwnerAway ? "the owner's game is not reachable now (or the piece has no key) - the owner's game must carry out a member's dismantle"
                : (v == coopbuild::kBuildDmOrderRefusedMember ? "a member's dismantle is not allowed on this build"
                   : "a player of another faction gets no dismantle on a player's buildings (decision 548)"),
                g_dismantleRefusedMember, g_dismantleRefusedOther, g_dmAskOffline);
    BdLogCapped(line);
    return 1;
}
/* T-805 fold, ANY THREAD (a guarded vtable call and pointer compares - BdOwnerPod / IsStandInFaction / StandInRecordSlot are
   any-thread): off the main thread a dismantle (task 96) on a piece another player owns is refused - its ask needs the main thread -
   counted (dmOffMainRefused, the REPORT line), no line. 1 = refused. */
int BdDismantleOff(int t, void* subject)
{
    if (t != coopbuild::kBuildTaskDismantle || subject == 0) return 0;
    ::Faction* f = (::Faction*)BdOwnerPod(subject);
    if (f == 0 || (!IsStandInFaction(f) && StandInRecordSlot(f) < 0)) return 0;
    ::InterlockedIncrement(&g_dmOffMainRefused);
    return 1;
}
/* house2: the gated TaskTypes (build 2, add-materials 71, repair 95, dismantle 96) are coopbuild::BuildTaskGated (suite-tested) */
void detour_order(void* pi, void* indoors, void* task, void* subject, void* shift, void* addDontClear, void* location)
{
    const int t = (int)(unsigned int)(uintptr_t)task;
    const char* road = (g_commitDepth > 0 && ::GetCurrentThreadId() == g_commitTid) ? "the placement commit's own build order"
                     : (g_inOrderLever != 0 ? "the order lever" : "a click");
    /* PROBE P140 BEGIN */
    if (t == 0x6B) P140PlayerOrder(pi, t, subject, ((unsigned char)(uintptr_t)shift) != 0 ? 1 : 0, road);   /* T-806: a player's use-cage order, per selected character */
    /* PROBE P140 END */
    if (BdOnMain() != 0 && BdDismantleGate(t, subject, road) != 0) return;   /* T-805: a dismantle on another player's piece */
    if (BdOnMain() == 0 && BdDismantleOff(t, subject) != 0) return;   /* T-805 fold: the same piece off the main thread - refused */
    /* help1 (owner 172) + T-903 (decision 920 a): a build / add-materials order on a copy of another player's piece, or a repair on a
       ruin or damaged copy, is help - given when that player is a faction-mate or has set this player friendly, else not given
       (BdHelpRefuse); this was the house1b refusal for a handed piece. What still reaches the refusal below: a repair on a handed piece
       that is neither a ruin nor damaged here (nothing to repair), and an order on a handed piece no stand-in owns. */
    const int helpOnCopy = (coopbuild::BuildTaskGated(t) && subject != 0 && BdOnMain() != 0)
        ? BdHelpOrder(t, subject, road) : 0;
    if (helpOnCopy == 2) return;   /* help1 fold (review HIGH 2): a wall copy - refused (BdHelpOrder logged it) */
    if (coopbuild::BuildTaskGated(t) && subject != 0 && BdOnMain() == 0 && BdHelpWallOff(t, subject) != 0) return;   /* fold 2 (LOW 7): off the main thread too */
    if (helpOnCopy == 0 && coopbuild::BuildTaskGated(t) && subject != 0 && BdOnMain() != 0
        && BdIsHandedAway(subject) != 0)
    {
        const int inCommit = (g_commitDepth > 0 && ::GetCurrentThreadId() == g_commitTid) ? 1 : 0;
        if (inCommit != 0) ++g_orderSkipped; else ++g_orderBlocked;
        char line[300];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] order task=%d on a handed piece NOT GIVEN (%s) - the house owner's game builds and dismantles it; orderSkipped=%lld orderBlocked=%lld",
                    t, road, g_orderSkipped, g_orderBlocked);
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
    /* PROBE P140 BEGIN */
    if (t == 0x6B) P140PlayerOrder(pi, t, subject, ((unsigned char)(uintptr_t)shift) != 0 ? 1 : 0, "a Shift job");   /* T-806 */
    /* PROBE P140 END */
    if (coopbuild::BuildTaskGated(t) && subject != 0)
    {
        if (BdOnMain() == 0)
        {
            if (BdDismantleOff(t, subject) != 0) return;   /* T-805 fold: a dismantle on another player's piece, off the main thread */
            if (BdHelpWallOff(t, subject) != 0) return;   /* help1 fold 2 (LOW 7): a wall copy is refused off the main thread too */
            ++g_jobKept;
        }
        else
        {
            if (BdDismantleGate(t, subject, "a Shift job") != 0) return;   /* T-805: a dismantle on another player's piece */
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
                BdHelpBaseRead(r, b);   /* help1: the copy's baseline - a helper's later deliveries are measured against it */
            }
            if (r.own != 0 && r.via != 3) BdOwnDirty(2);   /* mmo8a: an own piece placed here - its pp.build row is written now */
            if (r.own != 0 && r.via != 3 && (via == 1 || via == 2) && g_bdKeptDraining == 0) BdMadeHereMark(b, key);   /* BuildMadeHere: a placement by the commit or the lever, never a kept piece laid back */
            else g_madeHere.erase(std::string(key));
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
    else if (r.broken != r.sentBroken) why = "broken";   /* the writer's +0x3B6 flip */
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
    m.broken = (unsigned char)(r.broken != 0 ? 1 : 0);
    const int n = (r.nMats < 0) ? 0 : (r.nMats > (int)coopbuild::kBuildMaxMats ? (int)coopbuild::kBuildMaxMats : r.nMats);
    m.nMats = (unsigned char)n;
    for (int i = 0; i < n; ++i) m.mats[i] = r.mats[i];
    const std::vector<coopbuild::BuildHelpAck>* ha = BdHelpAcksLandedRead(key, r);   /* help1: the tail - each helper's last applied seq and refused materials; fold (MED 4): as pp.build holds them on disk */
    coopbuild::BuildHelpTailOf(ha, BdHelpAcksRead(key, r), &m.helpAcks);   /* help1 fold 3 (finding 1): + each slot's LIVE last applied (the helper keeps, never renumbers, what it covers) */
    if (!coopbuild::BuildEncodable(m)) { ++g_stateUnencodable; return; }
    const bool sent = net::SendBuild(m);
    char line[400];
    std::sprintf(line, "[BUILD] -> STATE key=%.63s %.2f/%.2f complete=%d destroyed=%d broken=%d mats=%d why=%s help=%u%s", key.c_str(), r.progress, r.needed,
                 (int)m.complete, (int)m.destroyed, (int)m.broken, n, why, (unsigned int)m.helpAcks.size(), sent ? "" : " NOT SENT (link down)");
    BdLogState(line);
    if (!sent) { ++g_stateLinkDown; return; }   /* not marked sent: the next event tries again */
    ++g_stateSent;
    r.helpForce = 0;
    if (r.stateAnnounce != 0) { r.stateAnnounce = 0; ++g_stateAnnounced; }   /* help1 fold 5: any real STATE is the announcement */
    r.sentProgress = r.progress; r.sentComplete = r.complete; r.sentN = n;
    if (r.destroyed != r.sentDestroyed) { if (r.destroyed != 0) ++g_destroySent; else ++g_repairSent; }   /* P15 */
    r.sentDestroyed = r.destroyed;
    r.sentBroken = r.broken;
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
    int help = 0, rep = 0, refused = 0;   /* T-903: rep = repair on a complete damaged copy; fold (review MED 2): refused = the help rule says no */
    float p0 = 0.0f, n0 = 0.0f, raisedTo = 0.0f;
    int c0 = 0, d0 = 0, x0 = 0, m0n = -1;
    float m0[coopbuild::kBuildMaxMats];
    const int cand = (g_bdSelf == 0 && amount < FLT_MAX * 0.5f && BdOnMain() != 0 && IsStandInFaction((::Faction*)BdOwnerPod(b))
        && BdReadState(b, &p0, &n0, &c0, &d0, &x0) != 0 && (c0 == 0 || (x0 != 0 && BdIsWall(b) == 0) || (coopbuild::BuildPieceDamaged(c0, x0, p0, n0) != 0 && BdIsWall(b) == 0)) && d0 == 0) ? 1 : 0;   /* P15 fold 1 (LOW 5): never a wall ruin */   /* P15: a COMPLETE ruin copy's repair is help too (the reset goes to the owner) */
    if (cand != 0)
    {   /* T-903 fold (review MED 2): the help rule is asked at the work itself, not only at the order - a job already running, a job kept
           across a save from the old any-partner rule, or a player set back to neutral / enemy since the order */
        int hmate = 0, hfr = 0;
        if (BdHelpAllowedHere((::Faction*)BdOwnerPod(b), &hmate, &hfr) == 0) refused = 1;
    }
    if (refused != 0)
    {   /* the engine's step on that copy is not run here and nothing is sent; counted, the frequent-line cap */
        ++g_helpRunRefused;
        char rk[kKeyCap];
        rk[0] = 0;
        if (ObjectPositionKey(b, rk, kKeyCap, "", 3, 0) == 0) { rk[0] = '?'; rk[1] = 0; }
        char rl[360];
        _snprintf_s(rl, sizeof(rl), _TRUNCATE, "[BUILD] help work on a copy of slot%d key=%s NOT DONE: that player is not a faction-mate and has not set this player friendly (decision 920 a) - the engine's step is skipped; helpRunRefused=%lld",
                    StandInSlotOf((::Faction*)BdOwnerPod(b)), rk, g_helpRunRefused);
        BdLogHelpFreq(rl);
        ::InterlockedIncrement64(&g_progressSeen);
        return;
    }
    if (cand != 0)
    {
        int tot = -1;
        m0n = BdReadMats(b, m0, (int)coopbuild::kBuildMaxMats, &tot);
        help = 1;
        rep = (c0 != 0 && x0 == 0) ? 1 : 0;   /* T-903: the engine's repair restores health up to needed - nothing is raised */
        if (c0 == 0 && x0 == 0 && n0 > 0.0f)
        {
            raisedTo = (n0 + std::fabs(amount)) * 1000.0f + 1000.0f;
            if (BdSetNeededPod(b, raisedTo) == 0) { help = 0; raisedTo = 0.0f; }
        }
    }
    if (raisedTo > 0.0f) BdProgressRaised(b, amount, raisedTo, n0);   /* help1 fold (review LOW 11): put back in __finally */
    else orig_progress(b, amount);
    if (help != 0) BdHelpAfter(b, p0, n0, x0, m0n, m0, rep);
    ::InterlockedIncrement64(&g_progressSeen);
    /* build1-b (T321: ~1400 FLT_MAX completions at world load, queueDropped 1141): an FLT_MAX-class amount while the world
       is loading is the engine finishing pieces it has just read, not construction - counted, never queued */
    if (amount >= FLT_MAX * 0.5f && EngineWritesBlocked()) { ::InterlockedIncrement64(&g_loadBurstSkipped); return; }
    /* the engine's add-materials task runs on its AI worker: it puts the items into the piece, then makes this amount-0 call. On a copy a
       stand-in holds (not a wall, not dismantled) that call is marked, so the main thread measures the copy's materials against its
       baseline and either records them as help or refuses them (BdHelpHarvest) - the help rule and the help records are main-thread only,
       so nothing else is done here. g_bdSelf is not asked: it counts the main thread's own writes. POD reads only. */
    int harvest = 0;
    const int onMain = BdOnMain();
    if (onMain == 0 && amount < FLT_MAX * 0.5f && IsStandInFaction((::Faction*)BdOwnerPod(b)) && BdIsWall(b) == 0)
    {
        float hp = 0.0f, hn = 0.0f;
        int hc = 0, hd = 0, hx = 0;
        if (BdReadState(b, &hp, &hn, &hc, &hd, &hx) != 0 && hd == 0) harvest = 1;
    }
    BdQueueEvent(1, b, amount, onMain, -1, harvest);
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
    /* T-805 fold probe: an instant dismantle (FLT_MAX - the panel's confirmDismantle 0x54F410, or the buildtest dismantle lever) on a
       COPY outside the mod's own writes runs on this game only; counted, the first 5 logged */
    if (amount >= FLT_MAX * 0.5f && g_bdSelf == 0 && BdOnMain() != 0 && IsStandInFaction((::Faction*)BdOwnerPod(b)))
    {
        ++g_dmInstantOnCopy;
        if (g_dmInstantOnCopy <= 5)
        {
            char pk[kKeyCap];
            pk[0] = 0;
            if (ObjectPositionKey(b, pk, kKeyCap, "", 3, 0) == 0) { pk[0] = '?'; pk[1] = 0; }
            char pl[360];
            _snprintf_s(pl, sizeof(pl), _TRUNCATE, "[BUILD] probe: an instant dismantle (vt+0x248 FLT_MAX - the panel's confirmDismantle or the buildtest lever) on a COPY of slot%d key=%.63s - on this game only; dmInstantOnCopy=%lld (first 5 logged)",
                        StandInSlotOf((::Faction*)BdOwnerPod(b)), pk, g_dmInstantOnCopy);
            DebugLog(std::string(pl));
        }
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

}   /* T-609 U fix: the two below are defined in namespace coop, beside BuildTick, outside this unnamed namespace */
void BdRekeyMain(const char* k0, const char* k1, const char* sid, int down, long gen, void* b);
void BdRekeyQueue(const char* k0, const char* k1, const char* sid, int down);
extern int g_showLinkDue;   /* owner 936 / 937: 1 = the copies' links to the teammates' shown towns are looked at the next drain (defined in namespace coop with the SHOW code) */
namespace {
/* T-609 U: one call of the engine's upgrade (down = 0) or downgrade (down = 1) on building b. The key (ObjectPositionKey, any thread)
   and the record's stringID are read before and after the original; the registry is read on the main thread only - off it the line
   says own=- reg=-. One line per call up to kUpgLineCap; the REPORT t609upgrade counts every call. */
void BdUpgradeCall(void* b, unsigned long long arg, int down)
{
    char k0[kKeyCap], k1[kKeyCap], s0[kUpgSidCap], s1[kUpgSidCap];
    k0[0] = 0; k1[0] = 0; s0[0] = 0; s1[0] = 0;
    const int onMain = BdOnMain();
    ObjectPositionKey(b, k0, kKeyCap, "", 3, 0);
    BdSidOf(b, s0, kUpgSidCap);
    if (down != 0) orig_downgrade(b, arg);
    else orig_upgrade(b, arg);
    ObjectPositionKey(b, k1, kKeyCap, "", 3, 0);
    BdSidOf(b, s1, kUpgSidCap);
    ::InterlockedIncrement64(down != 0 ? &g_dngSeen : &g_upgSeen);
    if (onMain == 0) ::InterlockedIncrement64(&g_upgOffMain);
    if (std::strcmp(k0, k1) != 0) ::InterlockedIncrement64(&g_upgKeyChanged);
    int reg = -1, own = -1;
    if (onMain != 0)
    {
        std::map<std::string, BdReg>::const_iterator it = g_reg.find(std::string(k0));
        reg = (it != g_reg.end()) ? 1 : 0;
        own = (it != g_reg.end() && it->second.own != 0 && it->second.via != 3) ? 1 : 0;
        if (own == 1) ::InterlockedIncrement64(&g_upgOwnRow);
    }
    /* T-609 U fix: the key (or the record) changed - this game's own row moves to the new key: at once on the main thread, else
       queued for BuildTick. Each linked wall passes through here on its own, so a wall group is re-keyed piece by piece. */
    if (std::strcmp(k0, k1) != 0 || std::strcmp(s0, s1) != 0)
    {
        if (onMain != 0) BdRekeyMain(k0, k1, s1, down, g_bdGen, b);
        else if (k0[0] != 0 && k1[0] != 0 && s1[0] != 0) BdRekeyQueue(k0, k1, s1, down);
        else ::InterlockedIncrement64(&g_rekeyDropped);   /* a key or the new record could not be read: nothing to move */
    }
    if (::InterlockedIncrement64(&g_upgLines) > kUpgLineCap) return;
    char l[480];
    _snprintf_s(l, sizeof(l), _TRUNCATE, "[BUILD] %s %s -> %s record %s -> %s own=%s reg=%s%s", down != 0 ? "downgrade" : "upgrade",
                k0[0] != 0 ? k0 : "?", k1[0] != 0 ? k1 : "?", s0[0] != 0 ? s0 : "?", s1[0] != 0 ? s1 : "?",
                own < 0 ? "-" : (own != 0 ? "1" : "0"), reg < 0 ? "-" : (reg != 0 ? "1" : "0"),
                onMain != 0 ? "" : " (off the main thread: the registry is not read)");
    DebugLog(l);
}
void detour_upgrade(void* b, unsigned long long arg) { BdUpgradeCall(b, arg, 0); }
void detour_downgrade(void* b, unsigned long long arg) { BdUpgradeCall(b, arg, 1); }

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
    g_btPlaced = b;   /* `buildtest progress placed` and the bagtest box `placed` name exactly this piece */
    g_btPlacedKey[0] = 0;
    if (ObjectPositionKey(b, g_btPlacedKey, kKeyCap, "", 3, 0) == 0) g_btPlacedKey[0] = 0;
}
/* TEST-ONLY: the piece the last `buildtest place` made, when its address is still among the loaded buildings and its key still reads as
   it did when made; else 0 and *why says which */
void* BdTestPlacedLive(std::string* why)
{
    if (g_btPlaced == 0) { *why = "no piece was made by buildtest place on this game in this world"; return 0; }
    static void* list[4096];
    int zones = 0, trunc = 0;
    const int n = LoadedBuildings(list, 4096, &zones, &trunc);
    for (int i = 0; i < n; ++i)
    {
        if (list[i] != g_btPlaced) continue;
        char k[kKeyCap];
        k[0] = 0;
        if (ObjectPositionKey(list[i], k, kKeyCap, "", 3, 0) == 0 || std::strcmp(k, g_btPlacedKey) != 0)
        {
            *why = "the address of the piece buildtest place made now holds a building whose key reads '" + std::string(k) + "', not '" + std::string(g_btPlacedKey) + "'";
            return 0;
        }
        return list[i];
    }
    *why = "the piece buildtest place made (key " + std::string(g_btPlacedKey) + ") is not among the loaded buildings"
           + std::string(trunc != 0 ? " (the loaded list was cut)" : " (its zone unloaded, or it was removed)");
    return 0;
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
    if (g_btIndoors == 0 && g_btIntoOff != 0) { d[0] = g_btIntoDx; d[2] = g_btIntoDz; }   /* `into ... <dx>,<dz>`: form 0 at that offset from the host's centre */
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

/* T-609 U: a building's target record - 'upgrades to' (vt+0x268) or 'downgrades to' (vt+0x270); 0 = none or the call faulted */
void* BdUpgradeRecordPod(void* b, int down)
{
    __try { return ((BdUpgradeRecordFn)((*(void***)b)[(down != 0 ? kVtDowngradeRecord : kVtUpgradeRecord) / 8]))(b); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* T-609 U: the engine's own upgrade (vt+0x258) or downgrade (vt+0x260) through the vtable, so a class's own override runs and the hook
   (detour_upgrade / detour_downgrade) logs the call like any other. 1 = returned, 0 = faulted */
int BdUpgradeCallPod(void* b, int down)
{
    __try { ((BdUpgradeFn)((*(void***)b)[(down != 0 ? kVtDowngrade : kVtUpgrade) / 8]))(b, 0ull); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* T-609 U TEST-ONLY `buildtest upgrade|downgrade <key-substring>` (MAIN THREAD, the K2 safe point): the engine's own upgrade or
   downgrade on one OWN, COMPLETED, loaded piece found by key substring; refused with the reason otherwise
   (coopbuild::BuildUpgradeLeverVerdict). The registry is left as the engine's change leaves it. */
void BdTestUpgrade(int down)
{
    const std::string verb = (down != 0) ? "downgrade" : "upgrade";
    std::string key;
    if (!BdFindReg(g_btArg, &key))
    {
        ++g_testNoEntry;
        DebugLog("[BUILD] buildtest " + verb + " SKIPPED: no registry entry whose key contains '" + g_btArg + "'");
        return;
    }
    int tv = 0;
    std::map<std::string, BdReg>::iterator row = g_reg.find(key);
    void* b = (row != g_reg.end()) ? BdFindPiece(key, row->second, &tv) : 0;
    char k2[kKeyCap];
    k2[0] = 0;
    if (b == 0 || ObjectPositionKey(b, k2, kKeyCap, "", 3, 0) == 0 || key != std::string(k2))
    {
        ++g_testNotLoaded;
        DebugLog("[BUILD] buildtest " + verb + " SKIPPED: key=" + key + (b == 0 ? " is not loaded here (no live piece)" : " live piece's key reads '" + std::string(k2) + "'"));
        return;
    }
    float p0 = 0, n0 = 0;
    int c0 = 0, d0 = 0, x0 = 0;
    const int stOk = BdReadState(b, &p0, &n0, &c0, &d0, &x0);
    void* rec = BdUpgradeRecordPod(b, down);
    char target[kUpgSidCap];
    target[0] = 0;
    if (rec != 0) GameDataSidPod(rec, target, kUpgSidCap);
    const int v = coopbuild::BuildUpgradeLeverVerdict(row->second.own != 0 ? 1 : 0, row->second.via == 3 ? 1 : 0, stOk, c0, d0, x0, rec != 0 ? 1 : 0, BdIsWall(b) != 0 ? 1 : 0);
    if (v != coopbuild::kBuildUpgradeGo)
    {
        ++g_testUpgradeRefused;
        DebugLog("[BUILD] buildtest " + verb + " REFUSED: key=" + key + " - " + coopbuild::BuildUpgradeLeverWhy(v, down));
        return;
    }
    char s0[kUpgSidCap], s1[kUpgSidCap], k3[kKeyCap];
    s0[0] = 0; s1[0] = 0; k3[0] = 0;
    BdSidOf(b, s0, kUpgSidCap);
    const int ok = BdUpgradeCallPod(b, down);
    BdSidOf(b, s1, kUpgSidCap);
    ObjectPositionKey(b, k3, kKeyCap, "", 3, 0);
    if (!ok) ++g_testFaulted;
    else ++g_testUpgrade;
    char l[480];
    _snprintf_s(l, sizeof(l), _TRUNCATE, "[BUILD] buildtest %s %s key=%s -> %s record %s -> %s (target %s; vt+0x%X)", verb.c_str(), ok ? "CALLED" : "FAULTED",
                key.c_str(), k3[0] != 0 ? k3 : "?", s0[0] != 0 ? s0 : "?", s1[0] != 0 ? s1 : "?", target[0] != 0 ? target : "?",
                (unsigned int)(down != 0 ? kVtDowngrade : kVtUpgrade));
    DebugLog(std::string(l));
}

void BdTestOnEntry(LONG kind)
{
    const char* verb = (kind == 2) ? "progress" : "dismantle";
    std::string key;
    void* placed = 0;
    if (g_btArg == "placed")
    {   /* the exact piece the last `buildtest place` made, found by its address among the loaded buildings */
        std::string why;
        placed = BdTestPlacedLive(&why);
        if (placed == 0)
        {
            ++g_testNotLoaded;
            DebugLog(std::string("[BUILD] buildtest ") + verb + " placed REFUSED: " + why);
            return;
        }
        key = g_btPlacedKey;
    }
    else if (!BdFindReg(g_btArg, &key))
    {
        ++g_testNoEntry;
        DebugLog(std::string("[BUILD] buildtest ") + verb + " SKIPPED: no registry entry whose key contains '" + g_btArg + "'");
        return;
    }
    int tv = 0;
    std::map<std::string, BdReg>::iterator trow = g_reg.find(key);
    void* b = (placed != 0) ? placed : ((trow != g_reg.end()) ? BdFindPiece(key, trow->second, &tv) : 0);   /* build1-e: furniture inside its host */
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

/* TEST-ONLY `buildtest broken <key-substring> 0|1`: the engine's own setBroken (vt+0x310 -> 0x0F6CD0) on the piece; an own piece's
   STATE carries the flag to the other games */
void BdTestBroken()
{
    std::string key;
    if (!BdFindReg(g_btArg, &key))
    {
        ++g_testNoEntry;
        DebugLog("[BUILD] buildtest broken SKIPPED: no registry entry whose key contains '" + g_btArg + "'");
        return;
    }
    int tv = 0;
    std::map<std::string, BdReg>::iterator trow = g_reg.find(key);
    void* b = (trow != g_reg.end()) ? BdFindPiece(key, trow->second, &tv) : 0;
    if (b == 0)
    {
        ++g_testNotLoaded;
        DebugLog("[BUILD] buildtest broken SKIPPED: key=" + key + " is not loaded here (no live piece)");
        return;
    }
    const int want = (g_btAmount > 0.5f) ? 1 : 0;
    const int b0 = BdBrokenRead(b);
    const int ok = BdSetBrokenPod(b, want != 0);
    const int b1 = BdBrokenRead(b);
    if (!ok) ++g_testFaulted;
    trow->second.broken = b1;
    BdMaybeSendState(trow->first, trow->second);
    char l[400];
    std::sprintf(l, "[BUILD] buildtest broken %s key=%.63s row=%s +0x3B6 %d -> %d (asked %d; vt+0x310 setBroken)", ok ? "CALLED" : "FAULTED", key.c_str(),
                 trow->second.via == 3 ? "copy" : "own", b0, b1, want);
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
    std::vector<std::string> worldDrop;   /* read-back rows whose piece is a world resource node */
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
        if (b != 0 && e.rec.hostKey.empty() && BdWorldNodeLive(b, 0) != 0) { worldDrop.push_back(it->first); continue; }
        BdCopyListDiagOne(it->first, e, b, v, "zone-load");
        if (e.evict != 0) evict.push_back(it->first);
    }
    for (size_t i = 0; i < worldDrop.size(); ++i)
    {   /* a world resource node is nobody's building: its '~c' row and its registry copy row go; nothing is dismantled or sent */
        const std::string wk = worldDrop[i];
        std::map<std::string, BdReg>::iterator cr = g_reg.find(wk);
        if (cr != g_reg.end() && cr->second.via == 3 && cr->second.own == 0) g_reg.erase(cr);
        if (g_copyList.find(wk) == g_copyList.end()) continue;   /* retired already as an earlier entry's alias: not retired or counted twice */
        BdCopyListRetire(wk, "the piece at that key is a world resource node");
        ++g_copyWorldNodeDropped;
        if (BdNodeSayDue(g_copyWorldNodeDropped) != 0)
        {
            char wl[400];
            _snprintf_s(wl, sizeof(wl), _TRUNCATE, "[BUILD] copy row key=%s dropped: the piece at that key is a world resource node, not a player's building (nothing dismantled); copyWorldNodeDropped=%lld",
                        wk.c_str(), g_copyWorldNodeDropped);
            DebugLog(std::string(wl));
        }
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
    s.nextTry = 0; s.fromPlace = 1; s.waitLogged = 0; s.deferTries = 0; s.completeTries = 0;   /* P15 fold 1 */
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
    p.keptMade = b;   /* KEPT: the object made here - its destroyed flag and overlap check go on it, not on a search by key */
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

/* MAIN THREAD (BdCopyOne): a piece HANDED TO THIS GAME takes its PLACE's progress - the placer sends no STATE for a piece it handed
   away, so without this an unfinished piece would stand here as made. The guarded write BdMarkRestored uses, kept below needed;
   only ever raised. */
void BdHandedTakeProgress(BdReg& r, void* b, const coopbuild::BuildMsg& m)
{
    if (m.complete != 0 || !(m.progress > 0.0f) || r.complete != 0 || BdIsWall(b) != 0) return;
    const unsigned long long rUpd = Rva("UpdatePhysicalWithProgress");
    const float need = (r.needed > 0.0f) ? r.needed : m.needed;
    float pr = m.progress;
    if (need > 0.0f && pr > need * 0.999f) pr = need * 0.999f;
    if (rUpd == 0 || !(pr > r.progress)) return;
    int matsLive = -1, stage = 0;
    const int ok = BdApplyStatePod(b, 1, pr, 0, 0, &matsLive, (uintptr_t)::GetModuleHandleA(0) + (uintptr_t)rUpd, &stage);
    float cp = 0, cn = 0;
    int cc = 0, cd = 0, cx = 0;
    if (BdReadState(b, &cp, &cn, &cc, &cd, &cx) != 0) { r.progress = cp; r.needed = cn; r.complete = cc; }
    char line[300];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] handed key=%.63s took its PLACE's progress %.2f -> %.2f/%.2f (%s, stage %d)", m.key.c_str(), m.progress, r.progress, r.needed,
                ok != 0 ? "applied" : "FAULTED", stage);
    BdLogCapped(line);
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
/* T-903: a host search's verdict in one word, and its counts (BdHostSearchNote) as the copy pending line prints them */
const char* BdVerdictShort(int v)
{
    switch (v)
    {
    case kObjKeyFound: return "found";
    case kObjKeyNone: return "none";
    case kObjKeyAmbiguous: return "ambiguous";
    case kObjKeyTruncated: return "truncated";
    default: return "other";
    }
}
void BdHostNoteText(const BdHostSearchNote& o, int verdict, char* out, size_t cap)
{
    _snprintf_s(out, cap, _TRUNCATE, "%s[listed=%d n=%d cut=%d torn=%d typed=%d resolved=%d unresolved=%d keyed=%d]", BdVerdictShort(verdict),
                o.listed, o.n, o.cut, o.torn, o.typed, o.resolved, o.typed - o.resolved, o.keyed);
}
/* T-903 (F1026): a hand's engine lookup, guarded. which 0 = hand::asBuilding (0x791D70), 1 = hand::asObjectBase (0x79C890).
   1 = the call returned (*out its answer, null included), 0 = it faulted. */
int BdHandLookupPod(const hand* h, int which, void** out)
{
    *out = 0;
    __try { *out = (which == 0) ? (void*)h->asBuilding() : (void*)h->asObjectBase(); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *out = 0; return 0; }
}
/* T-903 (F1026), MAIN THREAD: a host's interior list read again and every BUILDING handle on it sorted for BuildDeadHandlesIgnore -
   good, hit (at `key` or within BoxKeyNearKey's window of it), dead (both lookups null, neither faulted) or other - with the list's
   read counts. *sig = the dead handles' set (BuildDeadSetSig). Fills every field of *x except areaLoaded, setAge and the row flags. */
void BdHostDeadScan(void* host, const char* key, coopbuild::BuildDeadHandleIn* x, std::string* sig)
{
    int listed = -1, cut = 0, torn = 0;
    const int n = InteriorPieceIds(host, g_bdIds, kBdInteriorCap, &listed, &cut, &torn);
    x->listed = listed; x->n = n; x->cut = cut; x->torn = torn;
    x->typed = 0; x->good = 0; x->hits = 0; x->dead = 0; x->other = 0;
    std::vector<unsigned int> deadIds;
    for (int i = 0; i < n; ++i)
    {
        const unsigned int* f = g_bdIds + (size_t)i * 5;
        if (f[0] != 0 || (f[3] == 0 && f[4] == 0)) continue;   /* the searches' own filter: BUILDING (0) with an index or serial */
        ++x->typed;
        hand h(f[3], f[4], (itemType)f[0], f[1], f[2]);
        void* b = 0;
        if (BdHandLookupPod(&h, 0, &b) == 0) { ++x->other; continue; }   /* asBuilding faulted */
        if (b == 0)
        {
            void* o = 0;
            if (BdHandLookupPod(&h, 1, &o) == 0 || o != 0) { ++x->other; continue; }   /* asObjectBase faulted, or answered an object */
            ++x->dead;
            deadIds.push_back(f[1]); deadIds.push_back(f[3]); deadIds.push_back(f[4]);   /* container, index, serial */
            continue;
        }
        unsigned int own[5];
        if (ObjectHandIds(b, own) == 0 || std::memcmp(own, f, sizeof(own)) != 0) { ++x->other; continue; }
        char k[kKeyCap];
        k[0] = 0;
        if (ObjectPositionKey(b, k, kKeyCap, "", 3, 1) == 0) { ++x->other; continue; }
        if (std::strcmp(k, key) == 0 || BoxKeyNearKey(k, key) != 0) ++x->hits;
        else ++x->good;
    }
    *sig = coopbuild::BuildDeadSetSig(deadIds);
}
/* T-903 (F1026), MAIN THREAD: each host's dead set as last seen (host key -> signature, the safe point it was first seen unchanged),
   and the hosts whose dead handles were already said once */
struct BdDeadSetMem
{
    int had;
    std::string sig;
    unsigned int at;
    BdDeadSetMem() : had(0), at(0) {}
};
std::map<std::string, BdDeadSetMem> g_deadSets;
std::set<std::string> g_deadSaid;
const size_t kDeadSetsCap = 512;   /* more hosts than this remembered at once: the memory starts over (every wait restarts - only slower) */
/* T-903 (F1026), MAIN THREAD: a furniture copy's truncated search inside `host` - 1 = its dead handles count as no piece
   (coopbuild::BuildDeadHandlesIgnore: every condition held), counted and said once per host; 0 = the hold stays. */
int BdDeadHandlesNoPiece(const BdPending& p, void* host, const float* hostPos)
{
    const coopbuild::BuildMsg& m = p.m;
    coopbuild::BuildDeadHandleIn x;
    std::string sig;
    BdHostDeadScan(host, m.key.c_str(), &x, &sig);
    x.areaLoaded = (BdAreaReady(p, hostPos[0], hostPos[1], hostPos[2]) != 0 && ZoneBuildingsInHereTri(hostPos[0], hostPos[1], hostPos[2]) > 0) ? 1 : 0;
    if (g_deadSets.size() >= kDeadSetsCap && g_deadSets.find(m.hostKey) == g_deadSets.end()) g_deadSets.clear();
    BdDeadSetMem& mem = g_deadSets[m.hostKey];
    x.setAge = coopbuild::BuildDeadSetStep(&mem.had, &mem.sig, &mem.at, sig, g_drainNo);
    x.regRow = g_reg.find(m.key) != g_reg.end() ? 1 : 0;
    x.copyListRow = g_copyList.find(m.key) != g_copyList.end() ? 1 : 0;
    x.removedMark = (BdRemovedAnyNonce(m.key) != 0 || g_recentRemoved.find(m.key) != g_recentRemoved.end()) ? 1 : 0;
    if (coopbuild::BuildDeadHandlesIgnore(x) == 0) return 0;
    ++g_deadHandlesIgnored;
    if (g_deadSaid.size() < kDeadSetsCap && g_deadSaid.insert(m.hostKey).second)
    {
        char line[600];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] copy key=%.63s host=%.63s: the host's interior list holds %d handles that answer null from both"
                    " engine lookups, the same set for %lld safe points, in a fully loaded area; the other %d pieces resolved and keyed, none at or near"
                    " the key - counted as no piece, the copy is made (said once per host; deadHandlesIgnored=%lld)", m.key.c_str(), m.hostKey.c_str(),
                    x.dead, x.setAge, x.good, g_deadHandlesIgnored);
        DebugLog(std::string(line));
    }
    return 1;
}
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
        ::Faction* third = OwnerFactionForSlot((int)m.ownerSlot, true);   /* its stand-in, or a placeholder for a player not in this world now - an absent owner's piece is made too */
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
        else if (BdByteAtOr0(host, 0x162) != 0) why = "the host is dismantled here";   /* a ruined host (+0x1A1) still takes the piece: the placer's game made it in that ruin */
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
    const int lvExact = lv;   /* T-903: the exact search's verdict, and the nearby search's (nearRan) - for the copy pending line */
    int nearRan = 0, nearV = 0;
    if (live == 0 && furn != 0 && lv != kObjKeyAmbiguous)
    {   /* furniture the exact search missed - a copy's or a restore's: looked for in the drift window, as the kept road does (a piece's
           height in its key can change across a reload). One piece in the window is the piece, even when the host's list also held
           entries that did not resolve; two = ambiguous, it waits; none keeps the more cautious of the two verdicts - never an empty
           twin built beside it (the one exception, dead handles, follows this block) */
        int nv = 0;
        void* const inWindow = BdFindInHostNear(host, m.key.c_str(), &nv);
        nearRan = 1; nearV = nv;
        if (inWindow != 0) { live = inWindow; lv = kObjKeyFound; }
        else if (nv == kObjKeyAmbiguous || (lv == kObjKeyNone && nv != kObjKeyNone)) lv = nv;
    }
    /* T-903 (F1026): a copy's search held as truncated only because some handles on the host's list resolve to nothing. A handle that
       answers null from both engine lookups, unchanged across two searches at least kBuildDeadHandleWait safe points apart, in a fully
       loaded area, with every other piece resolved and keyed and none at or near the key, for a key this game holds no row of and never
       removed, counts as no piece (BdDeadHandlesNoPiece / coopbuild::BuildDeadHandlesIgnore): the search is complete and empty and the
       copy is made below. Any condition short - the hold stays (it may be this very piece not loaded yet: never an empty twin). Copies
       only; a restore keeps the hold. */
    if (live == 0 && furn != 0 && p.restore == 0 && lv == kObjKeyTruncated && BdDeadHandlesNoPiece(p, host, hostPos) != 0) lv = kObjKeyNone;
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
            /* T-903: which search gave the verdict (the nearby search runs only when the exact one was not ambiguous; its verdict
               replaces the exact one's when it is ambiguous or the exact one was none) and both searches' counts */
            const char* gaveUp = (nearRan == 0) ? "exact" : (lv != lvExact ? "near" : (nearV == lv ? "both" : "exact"));
            char ex[200], nr[200];
            BdHostNoteText(g_bdExactNote, lvExact, ex, sizeof(ex));
            if (nearRan != 0) BdHostNoteText(g_bdPiecesNote, nearV, nr, sizeof(nr));
            else _snprintf_s(nr, sizeof(nr), _TRUNCATE, "notRun");
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] copy pending key=%s host=%s hostResolved=1: the search inside the host was %s - retried every %u safe points;"
                        " gaveUp=%s exact=%s near=%s", m.key.c_str(), m.hostKey.c_str(), BdVerdictWord(lv), kRetryGap, gaveUp, ex, nr);
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
                const int worldNode = (furn == 0) ? BdWorldNodeLive(live, 0) : 0;   /* an ore rock the save named as this player's piece */
                const int act = coopbuild::BuildRestorePresentAct(1, pr != g_reg.end() ? 1 : 0, pr != g_reg.end() ? pr->second.own : 0,
                                                                  (pr != g_reg.end() && pr->second.via == 3) ? 1 : 0, worldNode);
                if (act == coopbuild::kBuildRsPresentDropWorld)
                {   /* its saved row leaves pp.build at the next write and its registry row goes; no tombstone and no REMOVE (the plan) */
                    const coopbuild::BuildRsPresentPlan pl = coopbuild::BuildRsPresentPlanOf(act);
                    if (pl.retireRow != 0)
                    {
                        if (pr != g_reg.end() && pr->second.via != 3) g_reg.erase(pr);
                        coopown::BuildRetireNote(&g_ownRetireOwed, m.key, g_bdGen);
                        BdOwnDirty(2);
                    }
                    ++g_restoreWorldNodeDropped;
                    if (BdNodeSayDue(g_restoreWorldNodeDropped) != 0)
                    {
                        std::sprintf(line, "[BUILD] restore key=%s present owner=mine: a world resource node - its saved row is dropped from this player's records (no REMOVE sent; the rock stays); restoreWorldNodeDropped=%lld",
                                     m.key.c_str(), g_restoreWorldNodeDropped);
                        DebugLog(std::string(line));
                    }
                    return 1;
                }
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
            char wireSaid[poskey::kKeyCap]; wireSaid[0] = 0;
            const int byWire = (mineLive != 0 && furn == 0 && poskey::WireForm(m.key.c_str(), wireSaid, poskey::kKeyCap) == 2) ? 1 : 0;   /* only a piece adopted as this player's own */
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] restore key=%s %s owner=%s progress=%.2f/%.2f%s%s%s", m.key.c_str(),
                         mineLive != 0 ? "present (the loaded area already holds it)" : "refused (another owner's piece stands at this key)",
                         mineLive != 0 ? "mine" : BdWhose(o, 0), lp, ln,
                         regd != 0 ? " - no row here: registered as this game's own (via=scan), its PLACE + STATE owed to the other game" : "",
                         byWire != 0 ? " - adopted: the standing piece answered to this key's wire form " : "",
                         byWire != 0 ? wireSaid : "");
            BdLogRestore(line);
            return 1;
        }
        if (furn == 0 && BdWorldNodeLive(live, 0) != 0)
        {   /* a world resource node is nobody's building: no copy row, no copy-list row, no owner write here */
            ++g_placeWorldNodeRefused;
            if (BdNodeSayDue(g_placeWorldNodeRefused) != 0)
            {
                std::sprintf(line, "[BUILD] <- PLACE key=%s refused: the piece at that key here is a world resource node; placeWorldNodeRefused=%lld",
                             m.key.c_str(), g_placeWorldNodeRefused);
                DebugLog(std::string(line));
            }
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
        const int ocBuy = (ocWorld != 0) ? coopbuild::BuildOwnerAdoptIsPurchase(1, 0, BdIsForSaleOrigPod(live), (o != (void*)want) ? 1 : 0, (want != 0 && (IsStandInFaction(want) || toMe != 0)) ? 1 : 0) : 0;   /* the new owner: a player's stand-in, or this game's own player for a house handed to it (never a third owner a record names); T-805 b: the ENGINE's isForSale, not this mod's answer */
        /* T-805 b (owner 809 b / 813 b): residents on hold for the house are made first (the re-offer's own steps - a game that holds the
           area makes them; otherwise the entry is dropped), so the world side below collects and rehomes them as the buyer's game did.
           Asked after the sale test: residents made fill the house's resident hand, which isForSale also reads (vt+0x1D8,
           decomp_29c4c0) - asked before, the test could answer no. The owner is read again: populateBuilding may set it. */
        if (ocBuy != 0 && TownGenBuyHeld(live) > 0) { void* const o2 = BdOwnerPod(live); if (o2 != 0) o = o2; }
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
            const int hc = StoreHomeClearForBuilding(m.key.c_str());   /* the groups whose home was the bought house keep no key to it */
            g_ocLastKey = m.key;
            char ol[1100];
            _snprintf_s(ol, sizeof(ol), _TRUNCATE, "[BUILD] P18 owner applied key=%.63s: '%.60s' -> '%.60s' (the sender's stand-in; this game's copy was for sale - a house bought) through the engine's own setFaction vt+0xA0 - %s;"
                        " world side: residents %s (%u platoons homed here, %u new homes), town list out=%d in=%d, residentSquad cleared=%d, doors %u re-owned / %u open (%d), interior %u re-owned (%d);"
                        " NOT repeated: 0x54EA40 furniture teardown, 0x38A5E0 location nodes, interior +0x100 walk, town step 0x296900/0x928010, interior+0x30 vt+0x60, 0x4D2340, residents' AI orders 0x5D1640; group homes cleared=%d",
                        m.key.c_str(), BdFactionName((void*)o).c_str(), BdFactionName(BdOwnerPod(live)).c_str(),
                        oa == coopbuild::kBuildOaApplied ? "applied" : (oa == coopbuild::kBuildOaSkip ? "SKIP (not written: already the named owner or an empty target)" : "FAULTED"),
                        fx.res == 1 ? "moved" : (fx.res == 0 ? "none / no address row" : (fx.res == -2 ? "SKIPPED (too many platoons to list safely)" : "FAULTED")), fx.resN, fx.newHomes,
                        fx.townOut, fx.townIn, fx.squad, fx.doorsN, fx.doorsOpened, fx.doors, fx.interiorN, fx.interior, hc);
            BdLogOc(ol);
        }
        else if (ocWorld != 0) ++g_ocAdoptOther;   /* P18 fold 1 (item 6): not for sale here - an ordinary adopt from nobody / a third party */
        BdReg& r = g_reg[m.key];
        r.sid = m.sid; r.owner = BdFactionName(BdOwnerPod(live)); r.progress = lp; r.needed = ln;
        r.complete = lc; r.dismantled = 0; r.destroyed = lx; r.via = 3;
        r.helpTailKnown = 0; r.helpTailSeq = 0; r.helpTailLive = 0;   /* help1 fold 2 (HIGH 1): a (re)made copy - new help waits for the owner's STATE again */
        r.hasPos = 1; for (int i = 0; i < 3; ++i) r.pos[i] = (furn != 0) ? hostPos[i] : m.pos[i];   /* review-build1d D3; build1-e: the host's */
        r.hostKey = m.hostKey; r.hostForm = m.hostForm; r.liveKey.clear(); r.keySame = 1;   /* build1-e: found by the builder's key */
        {   /* found in the drift window: the piece's own key differs - kept as its live key so later lookups and the box road find it */
            char ak[kKeyCap];
            ak[0] = 0;
            if (ObjectPositionKey(live, ak, kKeyCap, "", 3, 1) != 0 && m.key != ak)
            {
                r.liveKey = ak; r.keySame = 0;
                g_liveAlias[r.liveKey] = m.key;
            }
        }
        if (furn != 0) ++g_hostResolved;
        ++g_copyAdopted;
        BdHelpBaseRead(r, live);   /* help1: the copy's baseline */
        if (p.tries > 0) ++g_copyAdoptAfterNull;   /* review-build1b C3 residue: the engine added it before returning null */
        if (toMe != 0) BdHandedTakeProgress(r, live, m);   /* handed to this game: no STATE follows from the placer */
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
        if (toMe != 0) BdHandedTakeProgress(r, b, m);   /* handed to this game: no STATE follows from the placer */
        BdMarkOwner(r, m, toMe, recSlot);   /* build1h */
        if (toMe == 0) BdQueuePlaceState(m);
    }
    char hf[160];
    hf[0] = 0;
    if (furn != 0) std::sprintf(hf, " host=%.63s hostResolved=1 form=%d floor=%d", m.hostKey.c_str(), (int)m.hostForm, m.floor);
    std::sprintf(line, "[BUILD] copy created key=%s%s owner='%.60s' ownerAtReturn=%s fixed=%d zone %d,%d fresh(+0xA8)=%d complete=%d"
                 " progress as made=%.2f/%.2f (sent %.2f/%.2f) vt2D8=%s own=%d handed=%d restored=%d%s%s",
                 m.key.c_str(), hf, r.owner.c_str(), at, fixed, s.x, s.y, fresh, cc, cp, cn, m.progress, m.needed, mined ? "ok" : "FAULTED",
                 (toMe != 0 || p.restore != 0) ? 1 : 0, handed, p.restore,
                 keySame != 0 ? "" : " KEY MISMATCH live=", keySame != 0 ? "" : (k2ok != 0 ? k2 : "?"));
    BdLogCapped(line);
    g_showLinkDue = 1;   /* owner 936: a copy made later is linked to its teammate's shown town at the next drain (BdShowLinkTick) */
    ItFurnMarkStale(2);   /* T-454: the copy createBuilding just made here */
    if (p.restore == 0 && toMe == 0) ItParityCopyMade(b, k2ok != 0 ? k2 : m.key.c_str());   /* a box among the copies: its contents are asked once the zone list finds it */
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
        whole[i] = ((i < n) ? coopbuild::BuildHelpWholeItems(back[i]) : 0) + owe[i];
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
   GONE from that placement's record (BdHelpGoneFor). T-903 fold (review MED 2): work from a player this game's player allows no help
   (HelpAllowed on the owner's own stance, asked when it is applied) is refunded whole and confirmed - never applied. */
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
    {   /* T-903 fold (review MED 2): the help rule asked HERE, when the work is applied, on the owner's OWN stance - a faction-mate
           (TeamSameAnyThread on the two players' slots) or a player this game's player has set friendly (RelationsStanceLevels: this
           player's faction -> the sender's stand-in, `you`). Work from anyone else - a job running when the setting changed, a job kept
           across a save from the old any-partner rule, a helper on an older build - is confirmed with every delivered amount refunded,
           as for a complete piece, and nothing applied (helpApplyRefused). Re-check fold (item 2): while this game's own slot is unknown,
           or the sender's standing is unread (no stand-in yet, or its relation not read), the work is HELD and judged again at the next
           safe point (helpApplyHeld) - refused only for a known non-teammate not set friendly (coopbuild::BuildHelpApplyVerdict). */
        const int me = MySlotForWire();
        const int amate = (me >= 0 && in.slot != me && TeamSameAnyThread(me, in.slot)) ? 1 : 0;
        ::Faction* myF = LocalPlayerFaction();
        ::Faction* senderF = StandInForSlot(in.slot);
        int you = nametag::kUnknown, them = nametag::kUnknown;
        if (myF != 0 && senderF != 0) RelationsStanceLevels(myF, senderF, &you, &them);
        const int av = coopbuild::BuildHelpApplyVerdict(me >= 0 ? 1 : 0, amate, (myF != 0 && senderF != 0 && you != nametag::kUnknown) ? 1 : 0,
                                                        you == nametag::kFriendly ? 1 : 0);
        if (av == coopbuild::kBuildHelpApplyHold)
        {
            if (in.heldLogged == 0)
            {
                in.heldLogged = 1;
                ++g_helpApplyHeld;
                _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u held: %s - judged again at the next safe point, never refused on that helpApplyHeld=%lld",
                            m.key.c_str(), in.slot, m.seq, me < 0 ? "this game's own slot is not known yet" : "that player's standing is not read yet (no stand-in, or its relation unread)", g_helpApplyHeld);
                BdLogHelpFreq(line);
            }
            return 2;
        }
        if (av == coopbuild::kBuildHelpApplyRefuse)
        {
            float back = 0.0f;
            coopbuild::BuildHelpAck* fa = coopbuild::BuildHelpAckFor(&r.helpAcks, slot, 1);
            for (int i = 0; i < (int)m.nMats; ++i) { fa->ex[i] += m.mats[i]; back += m.mats[i]; }
            if (m.nMats > fa->nEx) fa->nEx = m.nMats;
            fa->seq = m.seq;
            ++g_helpApplyRefused;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u REFUSED (not a faction-mate, and this player has not set that player friendly - decision 920 a): nothing applied, mats %.2f refunded to the helper (confirmed once pp.build holds it) helpApplyRefused=%lld",
                        m.key.c_str(), in.slot, m.seq, back, g_helpApplyRefused);
            BdLogHelp(line);
            BdHelpAckChanged(m.key, r);
            return 1;
        }
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
    if (c0 != 0 && d0 == 0 && x0 == 0 && m.reset == 0 && m.progress > 0.0f && BdIsWall(b) == 0)
    {   /* T-903: repair work - the helper restored health on its copy of this COMPLETE piece. The message is the same HELP_WORK (no new
           kind, no protocol change): work whose piece is complete here is repair. Health is progress (state+4, needed state+0x28): the
           new health, never above needed, through the STATE's guarded write - progress, setBroken(0) above 0, updatePhysicalWithProgress
           0x558680 (BdApplyStatePod) - the steps the engine's own repair takes: addConstructionProgress 0x558B10 on a complete piece adds
           to the progress, calls setBroken(0) (vt+0x310) above 0, runs the finish vt+0x240 only while the complete byte is 0, then calls
           0x558680 (build/decomp_558b10.txt). Health raised to exactly needed does not finish the piece again: 0x558680 at progress >=
           needed runs its finish step ('Building construction complete', vt+0x240) only while the piece's own complete byte +0x160 is 0
           (build/decomp_558680.txt). T-903 fold (review HIGH 1): that byte is read here and, when it is not set, the health stays below
           0.999 x needed (BuildRepairFinishSafe), so 0x558680 never finishes a piece from this road. Delivered materials go back to the
           helper as for a complete piece. A piece already whole here is confirmed with nothing changed (repairWhole). Fold (review LOW 5):
           a write that faults is confirmed with its materials refunded - once, never applied again (repairFaulted). */
        const float to = coopbuild::BuildRepairFinishSafe(coopbuild::BuildRepairApplyTo(p0, m.progress, n0), n0, BdByteAtOr0(b, kBOwnComplete));
        const unsigned long long rUpdR = Rva("UpdatePhysicalWithProgress");
        int rok = 1, rstg = 0, rml = -1;
        if (to > p0) rok = (rUpdR != 0) ? BdApplyStatePod(b, 1, to, 0, 0, &rml, (uintptr_t)::GetModuleHandleA(0) + (uintptr_t)rUpdR, &rstg) : 0;
        if (rok == 0)
        {
            ++g_repairFaulted;
            ++g_helpFaulted;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u repair FAULTED at stage %d%s - confirmed with its mats refunded, never applied again repairFaulted=%lld",
                        m.key.c_str(), in.slot, m.seq, rstg, rUpdR == 0 ? " (UpdatePhysicalWithProgress not in the address table)" : "", g_repairFaulted);
            BdLogHelp(line);
        }
        coopbuild::BuildHelpAck* ra = coopbuild::BuildHelpAckFor(&r.helpAcks, slot, 1);
        float back = 0.0f;
        for (int i = 0; i < (int)m.nMats; ++i) { ra->ex[i] += m.mats[i]; back += m.mats[i]; }
        if (m.nMats > ra->nEx) ra->nEx = m.nMats;
        ra->seq = m.seq;
        float rp1 = 0.0f, rn1 = 0.0f;
        int rc1 = 0, rd1 = 0, rx1 = 0, rt1 = -1;
        if (BdReadState(b, &rp1, &rn1, &rc1, &rd1, &rx1) != 0) { r.progress = rp1; r.needed = rn1; r.complete = rc1; r.destroyed = rx1; }
        r.nMats = BdReadMats(b, r.mats, (int)coopbuild::kBuildMaxMats, &rt1);
        r.gen = g_bdGen;
        if (rok != 0)
        {
            ++g_helpApplied;
            if (to > p0) ++g_helpRepairApplied; else ++g_repairWhole;
        }
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- HELP key=%.63s from=slot%d seq=%u repair %.2f->%.2f/%.2f%s mats %.2f refunded to the helper (confirmed once pp.build holds it) repairApplied=%lld repairWhole=%lld",
                    m.key.c_str(), in.slot, m.seq, p0, rp1, rn1, rok == 0 ? " (the write FAULTED - refunded once)" : ((to > p0) ? "" : " (already whole here - nothing changed)"), back, g_helpRepairApplied, g_repairWhole);
        BdLogHelp(line);
        BdHelpAckChanged(m.key, r);
        return 1;
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

/* T-903, THE HELPER'S GAME, MAIN THREAD (the K2 safe point): a copy's materials measured against its baseline - after the engine's
   add-materials task ran off the main thread (where its call is neither help nor refused), and just before an owner's STATE overwrites
   them. The help rule is asked here, when the materials are committed. Allowed: the gains are recorded as help work for the owner as a
   main-thread help step records them (BdHelpRecordWork) and the baseline moves on. Not allowed: the gained slots are put back to the
   baseline (the STATE's mats-only write) and the items handed to this game's own character nearest the copy, as a refund is; items not
   taken stay owed as one more help entry of this placement (as a HELP_GONE carries them), which the owner's game refuses and hands back
   through its STATE's refund (kept due until taken). key = the row's key, r = the row (0: none), b = the live piece (0: not found);
   why = what asked (the line). */
void BdHelpHarvest(const std::string& key, BdReg* r, void* b, const char* why)
{
    float p = 0.0f, n = 0.0f;
    int c = 0, d = 0, x = 0;
    const int found = (r != 0 && r->removed == 0 && b != 0 && BdReadState(b, &p, &n, &c, &d, &x) != 0) ? 1 : 0;
    ::Faction* owner = (found != 0) ? (::Faction*)BdOwnerPod(b) : 0;
    const int isCopy = (found != 0 && r->via == 3 && IsStandInFaction(owner)) ? 1 : 0;
    const int wall = (isCopy != 0) ? BdIsWall(b) : 0;
    float now[coopbuild::kBuildMaxMats], gain[coopbuild::kBuildMaxMats];
    for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i) { now[i] = 0.0f; gain[i] = 0.0f; }
    int tot = -1;
    const int nNow = (isCopy != 0 && wall == 0 && d == 0) ? BdReadMats(b, now, (int)coopbuild::kBuildMaxMats, &tot) : -1;
    const int baseOk = (isCopy != 0) ? r->helpBaseOk : 0;
    const int gained = (nNow > 0 && baseOk != 0) ? coopbuild::BuildHelpMatGain(now, nNow, 1, r->helpBase, -1, 0, gain) : 0;
    int mate = 0, fr = 0;
    const int allowed = (gained != 0) ? BdHelpAllowedHere(owner, &mate, &fr) : 0;
    const int v = coopbuild::BuildHelpHarvestVerdict(found, isCopy, wall, d, baseOk, gained, allowed);
    if (v == coopbuild::kBuildHarvestGone || v == coopbuild::kBuildHarvestNotCopy || v == coopbuild::kBuildHarvestSkip) return;
    char line[560];
    if (v == coopbuild::kBuildHarvestNoBase)
    {   /* the baseline is left unset: moving it to the amounts now would take a delivery already in them for the mod's own */
        ++g_helpHarvestNoBase;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help materials (off the main thread) key=%.63s NOT MEASURED (%s): the copy's baseline is not known yet (set by the owner's next STATE) - nothing taken; helpHarvestNoBase=%lld",
                    key.c_str(), why, g_helpHarvestNoBase);
        BdLogHelpFreq(line);
        return;
    }
    if (v == coopbuild::kBuildHarvestNoGain)
    {
        if (nNow >= 0) BdHelpBaseSet(*r, now, nNow);   /* as a main-thread help step: the baseline follows the amounts read */
        return;
    }
    float sum = 0.0f;
    for (int i = 0; i < nNow; ++i) sum += gain[i];
    if (v == coopbuild::kBuildHarvestRecord)
    {
        BdHelpRecordWork(key, *r, b, 0.0f, 0, n, gain, nNow);
        BdHelpBaseSet(*r, now, nNow);
        ++g_helpHarvested;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help materials (off the main thread) key=%.63s dMats=%.2f recorded for slot%d (%s) - they go to the owner's game as help work; helpHarvested=%lld",
                    key.c_str(), sum, StandInSlotOf(owner), why, g_helpHarvested);
        BdLogHelpFreq(line);
        return;
    }
    /* refused: each gained slot back to the baseline; its item read first (as a STATE's refund reads it) */
    std::string sid[coopbuild::kBuildMaxMats];
    float back[coopbuild::kBuildMaxMats];
    for (int i = 0; i < nNow; ++i)
    {
        back[i] = (gain[i] > 0.0f) ? r->helpBase[i] : now[i];
        if (!(gain[i] > 0.0f)) continue;
        char s[128];
        s[0] = 0;
        void* gd = BdMatDataPod(b, i);
        if (gd != 0) BdStrPod((const char*)gd + kGdStringId, s, (int)sizeof(s));
        sid[i] = s;
    }
    int ml = -1, stg = 0;
    if (BdApplyStatePod(b, 0, 0.0f, nNow, back, &ml, 0, &stg) == 0)
    {   /* the materials are still on the copy: no item is handed back (never twice) and the refusal is not counted yet; the baseline is
           kept, so the next look refuses them again */
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help materials (off the main thread) key=%.63s dMats=%.2f REFUSED (%s): slot%d's piece takes no help from this player - the copy's materials could NOT be put back (the write faulted at stage %d), no items returned; tried again at the next look",
                    key.c_str(), sum, why, StandInSlotOf(owner), stg);
        BdLogHelpFreq(line);
        return;
    }
    ++g_helpHarvestRefused;
    BdHelpBaseSet(*r, back, nNow);
    ::Character* ch = BdNearestOwnChar(r->pos);
    int whole[coopbuild::kBuildMaxMats];
    int givenAt[coopbuild::kBuildMaxMats];   /* 1 given, 0 not taken (carried below), < 0 the engine's add faulted */
    int items = 0, owed = 0, lost = 0;
    const char* noWhy = "";
    for (int i = 0; i < (int)coopbuild::kBuildMaxMats; ++i)
    {
        whole[i] = (i < nNow) ? coopbuild::BuildHelpWholeItems(gain[i]) : 0;
        givenAt[i] = 1;
        if (whole[i] <= 0) continue;
        if (sid[i].empty() || ch == 0) { givenAt[i] = 0; owed += whole[i]; noWhy = sid[i].empty() ? "a material's item is not known here" : "no own character"; continue; }
        int units = 0;   /* a refused add may follow a partial placement: those units were taken, only the rest is owed */
        const int given = ItemsGiveMaterial(ch, sid[i], whole[i], &units);
        givenAt[i] = given;
        if (given == 1) { items += whole[i]; continue; }
        if (given < 0) { lost += whole[i]; noWhy = "the engine's add FAULTED - never retried"; continue; }   /* a faulted add may have half-filed the item */
        if (units > 0 && units < whole[i]) { items += units; whole[i] -= units; }
        owed += whole[i];
        noWhy = "the inventory refused them";
    }
    char miss[200];
    miss[0] = 0;
    if (owed > 0)
    {   /* the items not taken ride one more entry of this placement: the owner's game refuses it and its STATE's refund hands them back,
           kept due until the inventory takes them; the HELP_GONE refund names their items if the owner's piece is gone first */
        BdHelpRow& h = BdHelpRowFor(key, *r);
        coopbuild::BuildHelpEntry ce;
        if (coopbuild::BuildHelpGoneCarry(whole, givenAt, h.nextSeq, h.nonce, &ce) != 0)
        {
            ++h.nextSeq;
            h.owed.push_back(ce);
            for (int i = 0; i < nNow; ++i) if (givenAt[i] == 0 && whole[i] > 0 && h.sid[i].empty() && !sid[i].empty()) h.sid[i] = sid[i];
            h.pos[0] = r->pos[0]; h.pos[1] = r->pos[1]; h.pos[2] = r->pos[2]; h.posOk = 1;
            BdHelpSaveSoon();
            g_helpHarvestCarried += owed;
            _snprintf_s(miss, sizeof(miss), _TRUNCATE, ", %d still owed (%s) - carried as entry seq=%u", owed, noWhy, ce.seq);
        }
    }
    if (lost > 0)
    {
        g_helpHarvestLost += lost;
        const size_t ml2 = std::strlen(miss);
        _snprintf_s(miss + ml2, sizeof(miss) - ml2, _TRUNCATE, ", %d NOT returned (%s)", lost, noWhy);
    }
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] help materials (off the main thread) key=%.63s dMats=%.2f REFUSED (%s): slot%d's piece takes no help from this player (not a faction-mate, not set friendly) - the copy's materials put back and %d items returned to the nearest own character%s; helpHarvestRefused=%lld carried=%lld lost=%lld",
                key.c_str(), sum, why, StandInSlotOf(owner), items, miss, g_helpHarvestRefused, g_helpHarvestCarried, g_helpHarvestLost);
    BdLogHelpFreq(line);
}
/* T-903, MAIN THREAD, the K2 safe point: the copies an off-main add-materials step changed (marked by BuildTick), each once */
void BdHarvestDrain()
{
    if (g_bdHarvestDue.empty()) return;
    std::vector<std::string> due;
    due.swap(g_bdHarvestDue);
    for (size_t i = 0; i < due.size(); ++i)
    {
        std::map<std::string, BdReg>::iterator it = BdRowByLiveKey(due[i]);
        if (it == g_reg.end()) continue;
        int fv = 0;
        void* b = (it->second.removed == 0) ? BdFindPiece(it->first, it->second, &fv) : 0;   /* re-found: never a kept pointer */
        BdHelpHarvest(it->first, &it->second, b, "after an add-materials step");
    }
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
    BdHelpHarvest(key, &row->second, b, "before the owner's STATE");   /* a helper's materials not yet measured are recorded or refused before the STATE overwrites them */
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
    const int wall = BdIsWall(b);
    int wallOwn = -1;   /* a wall copy: 1 = its progress record is its alone, 0 = a group's, -1 = unreadable (taken as a group's) */
    if (wall != 0)
    {   /* coopbuild::BuildWallCopyAction. A wall copy made here at runtime keeps its own record, which 0x558680 redraws it from, so it
           takes the owner's progress as a share of its OWN needed (the owner's STATE carries its group's record), kept below 0.999 of it.
           A copy whose record is a group's (grouped when its area loaded - a member, or the lead holding the record every member reads)
           takes materials only: 0x558680 reads the segment's own fields, and a progress write there would land in every member's record.
           A complete STATE completes either kind through the engine's complete-now path (vt+0x230 FLT_MAX). */
        wallOwn = BdWallOwnRecord(b);
        float wp = 0.0f;
        const int wa = coopbuild::BuildWallCopyAction(wallOwn > 0 ? 1 : 0, (int)m.complete, m.progress, m.needed, ln, &wp);
        if (wa == coopbuild::kBuildWallProgress) ok = BdApplyStatePod(b, 1, wp, (int)m.nMats, m.mats, &matsLive, upd, &stage);
        else
        {
            ok = BdApplyStatePod(b, 0, 0.0f, (int)m.nMats, m.mats, &matsLive, upd, &stage);
            if (ok && wa == coopbuild::kBuildWallComplete && lc == 0) { ok = BdProgressPod(b, FLT_MAX); completeCalled = 1; }
        }
        ++g_stateWall;
        if (wallOwn > 0) ++g_wallOwnApplied; else ++g_wallSharedApplied;
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
        if (ok && lc == 0)
        {
            const int pwBefore = PowerSwitchNow(b);   /* P47 (H-P47b): a power piece's switch before the engine's complete-now path switches it on */
            ok = BdProgressPod(b, FLT_MAX); completeCalled = 1;   /* the engine's own complete-now path */
            if (ok) PowerCompletionPutBack(b, pwBefore);   /* P47 (H-P47b): a copy's switch put back to the writer's last state */
        }
    }
    if (!ok)
    {
        ++g_stateFaulted;
        std::sprintf(line, "[BUILD] <- STATE key=%.63s FAULTED at stage %d%s", key.c_str(), stage, completeCalled ? " (vt+0x230 FLT_MAX)" : "");
        BdLogState(line);
        return 1;
    }
    if (completeCalled != 0 && wall != 0)
    {   /* the complete flag read back: 0x5594A0 does nothing when a shared record's link does not resolve, so a wall copy can still be
           unfinished (or the read-back fails) - the STATE is kept and the call tried again (coopbuild::BuildWallCompleteRetryGap: every
           kRetryGap safe points for kWallCompleteTries tries, then every kRetryGap x kWallCompleteSlowMul while the STATE is held). It is
           never counted or logged as applied while the copy does not read complete. */
        float cp = 0, cn = 0;
        int cc = 0, cd = 0, cx = 0;
        const int cr = BdReadState(b, &cp, &cn, &cc, &cd, &cx);
        if (cr == 0 || cc == 0)
        {
            ++g_wallCompleteNoop;
            const unsigned int gap = coopbuild::BuildWallCompleteRetryGap(s.completeTries, kWallCompleteTries, kRetryGap, kWallCompleteSlowMul);
            if (s.completeTries >= kWallCompleteTries) ++g_wallCompleteSlow;
            if (s.completeTries < 1000000) ++s.completeTries;
            std::sprintf(line, "[BUILD] <- STATE key=%.63s wall(%s) complete NOT TAKEN: vt+0x230(FLT_MAX) ran and the copy %s - tried again in %u safe points (try %d; slow after %d); wallCompleteNoop=%lld wallCompleteSlow=%lld",
                         key.c_str(), wallOwn > 0 ? "own" : "group", cr != 0 ? "still reads complete=0" : "could not be read back", gap, s.completeTries, kWallCompleteTries,
                         g_wallCompleteNoop, g_wallCompleteSlow);
            BdLogState(line);
            s.nextTry = g_drainNo + gap;
            return 0;
        }
        ++g_wallCompleteApplied;
    }
    if (m.nMats > 0 && matsLive >= 0 && matsLive != (int)m.nMats) ++g_matsCountDiff;
    ++g_stateApplied;
    if (completeCalled) ++g_completeApplied;
    int brokenNow = BdBrokenRead(b);
    if (s.fromPlace == 0 && wall == 0 && brokenNow != (int)m.broken)   /* the owner's broken flag, after the progress write (which clears it) */
    {
        BdSetBrokenPod(b, m.broken != 0);
        brokenNow = BdBrokenRead(b);
    }
    float ap = 0, an = 0;
    int ac = 0, ad = 0, ax = 0, aTotal = -1;
    BdReadState(b, &ap, &an, &ac, &ad, &ax);
    BdReg& r = row->second;
    r.progress = ap; r.needed = an; r.complete = ac; r.dismantled = ad; r.destroyed = ax;
    r.broken = brokenNow;
    r.nMats = BdReadMats(b, r.mats, (int)coopbuild::kBuildMaxMats, &aTotal);
    BdHelpOnState(key, r, b, m, upd, wall, s.fromPlace);   /* help1: the confirmation, the refund, this game's unconfirmed work back on, the baseline */
    if (s.fromPlace == 0) { r.ownerLast = m; r.ownerKnown = 1; }   /* P15: the owner's last state - put back if this game's engine makes the copy a ruin */
    std::sprintf(line, "[BUILD] <- STATE applied key=%.63s %.2f/%.2f complete=%d broken=%d mats=%d/%d (sent %.2f/%.2f complete=%d broken=%d)%s%s%s", key.c_str(),
                 ap, an, ac, brokenNow, (int)m.nMats, matsLive, m.progress, m.needed, (int)m.complete, (int)m.broken,
                 completeCalled ? " via vt+0x230(FLT_MAX)" : "", s.fromPlace ? " from=PLACE" : "", wall != 0 ? (wallOwn > 0 ? " wall(own)" : " wall(group)") : "");
    BdLogState(line);
    if (lc == 0 && ac != 0) ItFurnMarkStale(3);   /* T-454: the owner's STATE completed the copy (0 -> 1) */
    if (lc == 0 && ac != 0) ItParityCopyCompleted(b, key.c_str());   /* a box among the copies has an inventory now: its contents are asked */
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
    {   /* a world resource node (coopown::WorldNodeOf: its record is marked foliage, a worker operates it, it takes no inputs, no
           build record names it): its faction mark is the engine's, read by each game on its own copy - never registered as this
           game's own piece (no PLACE;
           its box stays on the area rule, items.cpp ItBoxOwnerOf). Any other production building the registry lacks a row for (a
           player-built machine, mine or well) is registered. */
        const int hasRow = (g_reg.find(std::string(key)) != g_reg.end() || BdRowByLiveKey(std::string(key)) != g_reg.end()) ? 1 : 0;
        int needsOp = -1, inputs = -1;
        BdProdNodeReadPod(b, &needsOp, &inputs);
        if (coopown::WorldNodeOf(1, needsOp, inputs, hasRow, BdFoliagePod(b)) != 0)
        {
            if (zoneScan != 0) ++g_zsSkipped;
            ++g_scanWorldProduction;
            if (BdNodeSayDue(g_scanWorldProduction) != 0)
            {
                char wl[400];
                _snprintf_s(wl, sizeof(wl), _TRUNCATE, "[BUILD] %s key=%s: a world resource node (a production building marked foliage, a worker operates it, no inputs, no build record) - not registered as an own piece; scanWorldProduction=%lld",
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
                char pl[400];
                _snprintf_s(pl, sizeof(pl), _TRUNCATE, "[BUILD] %s key=%.63s: a production building with no build record that is not a world resource node (needsOperating=%d inputs=%d) - registered as an own piece; scanPlayerProduction=%lld",
                            zoneScan != 0 ? "ZONESCAN" : "ROSTER scan", key, needsOp, inputs, g_scanPlayerProduction);
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
   answer, refused for a house a stand-in owns (coopbuild::BuildForSaleKeep) - pointer compares, no lock, an interlocked count. A house
   whose residents are on hold stays for sale (owner 809 b): the buy callback below makes them first. */
bool detour_isforsale(void* b)
{
    const bool r = orig_isforsale(b);
    if (!r || b == 0) return r;
    void* o = *(void* const*)((const char*)b + kRootOwner);   /* the engine's own read in isForSale (param_1[2]) */
    if (coopbuild::BuildForSaleKeep(1, (o != 0 && IsStandInFaction((::Faction*)o)) ? 1 : 0) == 0) { ::InterlockedIncrement64(&g_ocNotForSale); return false; }
    return r;
}

}   /* the two below are defined in namespace coop, outside this unnamed namespace */
int BdSaleValuePod(void* b);   /* below, with the buyhouse lever */
int BdMoneyPod(void* f);
namespace {
static int BdPlatoonStatePod(void* p, int* awake, int* kept)   /* Platoon+0x1D8 the active platoon (awake), +0x115 the kept byte setPersistentSquad writes */
{
    __try
    {
        *awake = (*(void* const*)((const char*)p + 0x1D8) != 0) ? 1 : 0;
        *kept = (*(const unsigned char*)((const char*)p + 0x115) != 0) ? 1 : 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *awake = -1; *kept = -1; return 0; }
}
/* after the engine's purchase: one log line per group TownGenBuyHeld made, with the home the engine gave it. Nothing is changed: the
   engine sets setPersistentSquad (0x7EB230) only for an AWAKE group's members it sends away (decomp_7acb20, its last loop); an asleep
   group keeps its kept flag, as it does without this mod */
static void BdBuyHeldAfter(void* b)
{
    void* gp[64];
    const int gn = TownGenBuyHeldGroups(gp, 64);
    char key[kKeyCap];
    if (ObjectPositionKey(b, key, kKeyCap, "", 3, 1) == 0) _snprintf_s(key, sizeof(key), _TRUNCATE, "?");
    ::Faction* const mine = LocalPlayerFaction();
    const int bought = (mine != 0 && BdOwnerPod(b) == (void*)mine) ? 1 : 0;
    g_buyHeldGroups += gn;
    for (int i = 0; i < gn; ++i)
    {
        void* const p = gp[i];
        char hk[128]; hk[0] = 0;
        const int hr = TownGenPlatoonHomeKey(p, hk, 128);
        int awake = -1, kept = -1;
        BdPlatoonStatePod(p, &awake, &kept);
        if (++g_buyHeldLines > 200) continue;
        char line[400];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] buy rehome key=%.63s groups=%d newHome=%.63s kept=%d awake=%d (group %d of %d made at the purchase; bought=%d%s)",
                    key, gn, hr == 1 ? hk : (hr == 0 ? "none" : "unread"), kept, awake, i + 1, gn, bought, "");
        DebugLog(std::string(line));
    }
}
/* T-805 b (owner decisions 809 b / 813 b): Building::buyMeCallback (1.0.65 0x7ACB20 / 1.0.68 0x7AD6C0, build/decomp_7acb20.txt), the
   buy dialogue's answer. Only result 2 buys; then the engine's afford test (Ownerships::takeMoney, vtable slot 0, with
   calculateSaleValue's price - it takes the money when it says yes) refuses ("Can't afford"), or the purchase goes through: the owner's
   groups homed in the house (0x6BA220, by each group's home pointer) are collected, the house changes hands, and each collected group
   gets a new home (0x99D810: a non-player building already on its list, else a random for-sale building of its town in a loaded zone);
   an awake group's members get a walk order to it, or with no home a leave order and setPersistentSquad(false). Residents on hold for
   the house are made HERE, before the original, through the re-offer's own steps (towngen.cpp TownGenBuyHeld), so the engine's own
   purchase collects and rehomes them; when they cannot be made the waiting entry is dropped and the purchase goes on (owner 813 b).
   Asked only on result 2 when the ENGINE's isForSale (not this mod's answer) says yes and the purse covers the price as the afford
   test asks it (townreoffer::BuyHeldAsk). MAIN THREAD only (the UI); another thread, a load or a teardown passes straight through. */
void detour_buyme(void* b, int result)
{
    int held = 0;
    if (result == 2 && b != 0 && BdOnMain() != 0 && !EngineWritesBlocked())
    {
        ::Faction* const mine = LocalPlayerFaction();
        const int covers = townreoffer::PurseCovers(BdSaleValuePod(b), mine != 0 ? BdMoneyPod((void*)mine) : -1);
        if (townreoffer::BuyHeldAsk(result, BdIsForSaleOrigPod(b), covers) != 0) { ++g_buyHeldAsked; held = TownGenBuyHeld(b); if (held > 0) ++g_buyHeldRan; }
    }
    orig_buyme(b, result);
    if (held > 0) BdBuyHeldAfter(b);
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
        /* the house is this game's player's now: the groups whose home it was (the purchase blanked their home in the engine) keep no key
           to it, so no road gives it back to them (store.cpp StoreHomeClearForBuilding) */
        const int hc = (act == coopbuild::kBuildOdScan || act == coopbuild::kBuildOdKnown) ? StoreHomeClearForBuilding(e.key.c_str()) : 0;
        char line[560];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] P18 owner change key=%.63s '%.60s' -> this game's player: %s; group homes cleared=%d", e.key.c_str(), e.before.c_str(), what, hc);
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
    st.broken = (unsigned char)(BdBrokenRead(b) != 0 ? 1 : 0);
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
    r.broken = (int)st.broken;
    if (nmN >= 0) { r.nMats = nmN; for (int i = 0; i < k; ++i) r.mats[i] = nm[i]; }
    /* what the other game now holds - later STATEs (BdMaybeSendState) are measured against it */
    r.sentOk = 1; r.sentProgress = p; r.sentComplete = c; r.sentN = stSent ? k : 0;
    if (stSent) r.sentDestroyed = x;   /* P15: the roster's STATE carried the destroyed bit */
    if (stSent) r.sentBroken = r.broken;
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
/* TEST-ONLY, MAIN THREAD: the piece the last `buildtest place` made while it is loaded (build.h) */
void* BuildTestPlacedLive(std::string* why) { return BdTestPlacedLive(why); }

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
/* T-811 (owner 874 a): the map-icon hooks - the icon (Town vt+0x378) and the map layer (vt+0x380) of a teammate's base this game shows
   are the engine's own player-outpost ones of the row's size level; every other town goes to the engine unchanged. The detours are
   with the shown bases (BdShowLevelOf). */
static unsigned long long kBdTownMapIconRva = 0; static AddrReg kBdTownMapIconRva_reg("TownMapIcon", &kBdTownMapIconRva);      /* Steam_1.0.65 0x48CEE0: Town vt+0x378 - the map marker's icon name */
static unsigned long long kBdTownMapLayerRva = 0; static AddrReg kBdTownMapLayerRva_reg("TownMapLayer", &kBdTownMapLayerRva);   /* Steam_1.0.65 0x48B220: Town vt+0x380 - the map marker's layer */
typedef void* (*BdTownMapIconFn)(void* town, void* outName);
typedef unsigned (*BdTownMapLayerFn)(void* town);
/* T-811 log (decision 875): the map's own colour pick for a marker and the five colours it picks from (baseshow::MarkerColourSlotOf) */
static unsigned long long kBdMapMarkerColourRva = 0; static AddrReg kBdMapMarkerColourRva_reg("MapMarkerColour", &kBdMapMarkerColourRva);   /* Steam_1.0.65 0x48E890 (1.0.68 0x48F320): (Town*) -> the marker colour */
static unsigned long long kBdMapMarkerSlotsRva = 0; static AddrReg kBdMapMarkerSlotsRva_reg("MapMarkerColourSlots", &kBdMapMarkerSlotsRva);   /* Steam_1.0.65 0x212E528 (1.0.68 0x212F538): MarkerAlly, the first of five */
typedef const float* (*BdMapMarkerColourFn)(void* town);
BdTownMapIconFn orig_townMapIcon = 0;
BdTownMapLayerFn orig_townMapLayer = 0;
void* detour_townMapIcon(void* town, void* outName);
unsigned detour_townMapLayer(void* town);
int g_hkMapIcon = -2, g_hkMapLayer = -2;
void InstallPower();   /* P47: below */
void PowerMarkPiece(void* b);   /* P47: below - a power piece the mod wrote, looked at by the next K2 drain */
int BdInstallTownWake(uintptr_t base);   /* T-936: below, beside BdShowTownByHand (this namespace) */
void InstallBuild()
{
    if (g_installed != 0) return;
    g_installed = 1;
    ::InitializeCriticalSection(&g_qcs);
    ::InterlockedExchange(&g_qInit, 1);
    ::InitializeCriticalSection(&g_rkCs);   /* T-609 U fix */
    ::InterlockedExchange(&g_rkInit, 1);
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
    /* T-805 b: Building::buyMeCallback - residents on hold for a house being bought are made before the engine's purchase */
    g_hkBuyMe = BdHook(base, kBdBuyMeRva, (void*)&detour_buyme, (void**)&orig_buyme);
    char bmAt[40];
    _snprintf_s(bmAt, sizeof(bmAt), _TRUNCATE, "0x%llX ", kBdBuyMeRva);
    const std::string bmmsg = std::string("[BUILD] T-805 b buy hook: Building::buyMeCallback ") + bmAt + HkWord(g_hkBuyMe)
        + " (a house whose residents are on hold: they are made before the engine's purchase so it rehomes them, or the entry is dropped)";
    if (g_hkBuyMe == 1) DebugLog(bmmsg);
    else ErrorLog(bmmsg);
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
    /* T-811 (owner 874 a): a teammate's base this game shows draws the engine's growing outpost icon */
    g_hkMapIcon = (kBdTownMapIconRva == 0) ? -2
        : ((coop::AddHook((void*)(rbase + (uintptr_t)kBdTownMapIconRva), (void*)&detour_townMapIcon, (void**)&orig_townMapIcon) == coop::SUCCESS && orig_townMapIcon != 0) ? 1 : -1);
    g_hkMapLayer = (g_hkMapIcon != 1) ? -3 : (kBdTownMapLayerRva == 0) ? -2
        : ((coop::AddHook((void*)(rbase + (uintptr_t)kBdTownMapLayerRva), (void*)&detour_townMapLayer, (void**)&orig_townMapLayer) == coop::SUCCESS && orig_townMapLayer != 0) ? 1 : -1);
    char hkIcon[40], hkLayer[40];   /* this game version's own addresses (the table row's) */
    _snprintf_s(hkIcon, sizeof(hkIcon), _TRUNCATE, "0x%llX ", kBdTownMapIconRva);
    _snprintf_s(hkLayer, sizeof(hkLayer), _TRUNCATE, "0x%llX ", kBdTownMapLayerRva);
    const std::string smsg = std::string("[SHOW] T-811 map-icon hooks: TownMapIcon ") + hkIcon + HkWord(g_hkMapIcon) + ", TownMapLayer " + hkLayer
        + (g_hkMapLayer == -3 ? "not tried (the icon hook is not installed)" : HkWord(g_hkMapLayer));
    if (g_hkMapIcon == 1 && g_hkMapLayer == 1) DebugLog(smsg);
    else ErrorLog(smsg);
    /* T-609 U: Building::upgrade / downgrade - each call logged with the building's key and record before and after */
    g_hkUpgrade = BdHook(base, kBdUpgradeRva, (void*)&detour_upgrade, (void**)&orig_upgrade);
    g_hkDowngrade = BdHook(base, kBdDowngradeRva, (void*)&detour_downgrade, (void**)&orig_downgrade);
    char ugAt[40], dgAt[40];   /* this game version's own addresses (the table rows') */
    _snprintf_s(ugAt, sizeof(ugAt), _TRUNCATE, "0x%llX ", kBdUpgradeRva);
    _snprintf_s(dgAt, sizeof(dgAt), _TRUNCATE, "0x%llX ", kBdDowngradeRva);
    const std::string ugmsg = std::string("[BUILD] T-609 U upgrade hooks: Building::upgrade ") + ugAt + HkWord(g_hkUpgrade)
        + ", Building::downgrade " + dgAt + HkWord(g_hkDowngrade) + " (each call logged with the key and record before and after; nothing else is done)";
    if (g_hkUpgrade == 1 && g_hkDowngrade == 1) DebugLog(ugmsg);
    else ErrorLog(ugmsg);
    InstallFarm();   /* par16: FarmBuilding::update / operate - one writer per farm */
    InstallPower();   /* P47: the battery charge and the fuel step - one writer per power piece */
    BdInstallTownWake(base);   /* T-936 (owner 936): a newly founded player outpost woken at the next safe point */
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
        s.m = r.ownerLast; s.nextTry = 0; s.fromPlace = 0; s.waitLogged = 0; s.deferTries = 0; s.completeTries = 0;   /* P15 fold 1 */
    }
    ++g_copyDestroyReverted;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] copy key=%.63s made a ruin HERE by this game's engine - the owner's game is the writer: its last STATE (whole, %.2f/%.2f complete=%d) goes back on at the next safe point; copyDestroyReverted=%lld",
                key.c_str(), r.ownerLast.progress, r.ownerLast.needed, (int)r.ownerLast.complete, g_copyDestroyReverted);
    BdLogState(line);
}

/* MAIN THREAD (BuildTick). Walls joined in a group share one progress record, held by the group's lead; a wall's progress call
   (0x5594A0) passes the work on to the lead (0x558B10 on it), so only the lead's key gets progress events. Every other OWN wall whose
   record (vt+0x228) is the lead's takes the lead's row values - progress, needed, complete and delivered materials, all of that one
   record - and sends its own STATE, so each grouped wall's copy follows. A key's group is resolved once: a wall whose group count
   (+0x390) reads 0 has none; otherwise the own pieces in the lead's key sector and the eight around it (coopbuild::BuildWallGroupNear)
   are looked up live until the count's worth of members is found. A looked-up piece that is not a wall, or not found now, is
   remembered as such. Everything is dropped when what grouping depends on changes: the registry's row count (a placement or a
   removal), the world generation, a zone unloading (BoxWalkGen) or a zone finishing its load here (g_zsZones) - walls group when an
   area loads. A resolution made while the lead's own zone is not marked loaded is not kept. */
std::map<std::string, std::vector<std::string> > g_wallGroups;   /* lead key -> the other own wall keys sharing its record */
std::map<std::string, int> g_wallKeyIs;   /* own free-standing keys looked at since the cache was last cleared: 1 a wall, 0 not, 2 not found */
size_t g_wallCacheRows = 0;
long g_wallCacheGen = -1;
long long g_wallCacheWalkGen = -1, g_wallCacheZones = -1;
std::vector<std::string> BdWallGroupOf(const std::string& lead, const BdReg& lr)
{
    const long long wg = ZoneTornGen();   /* zone teardowns only (BoxWalkGen also moves once per drain) */
    if (g_wallCacheRows != g_reg.size() || g_wallCacheGen != g_bdGen || g_wallCacheWalkGen != wg || g_wallCacheZones != g_zsZones)
    {
        if (!g_wallGroups.empty() || !g_wallKeyIs.empty()) ++g_wallGroupCacheCleared;
        g_wallGroups.clear(); g_wallKeyIs.clear();
        g_wallCacheRows = g_reg.size(); g_wallCacheGen = g_bdGen; g_wallCacheWalkGen = wg; g_wallCacheZones = g_zsZones;
    }
    std::map<std::string, int>::const_iterator lk = g_wallKeyIs.find(lead);
    if (lk != g_wallKeyIs.end() && lk->second == 0) return std::vector<std::string>();   /* not a wall */
    std::map<std::string, std::vector<std::string> >::const_iterator gi = g_wallGroups.find(lead);
    if (gi != g_wallGroups.end()) return gi->second;
    if (!lr.hostKey.empty()) { g_wallKeyIs[lead] = 0; return std::vector<std::string>(); }   /* furniture is never a wall */
    int fv = 0;
    void* b = BdFindPiece(lead, lr, &fv);
    if (b == 0) return std::vector<std::string>();   /* not found now: nothing kept, looked up again at its next event */
    if (BdIsWall(b) == 0) { g_wallKeyIs[lead] = 0; return std::vector<std::string>(); }
    g_wallKeyIs[lead] = 1;
    const uintptr_t rec = BdWallRecordPod(b);
    if (rec == 0) return std::vector<std::string>();
    const int gc = BdWallGroupCountPod(b);
    int lsx = 0, lsy = 0;
    const int lsOk = BoxKeySector(lead.c_str(), &lsx, &lsy);
    const int keep = (lsOk != 0 && ZoneLoadedSectorTri(lsx, lsy) == 1) ? 1 : 0;
    std::vector<std::string> members;
    if (gc == 0) { if (keep != 0) g_wallGroups[lead] = members; return members; }   /* leads no group */
    ++g_wallGroupResolves;
    for (std::map<std::string, BdReg>::const_iterator it = g_reg.begin(); it != g_reg.end(); ++it)
    {
        if (gc > 0 && (int)members.size() >= gc) break;   /* the whole group found */
        const BdReg& r = it->second;
        if (it->first == lead || r.own == 0 || r.via == 3 || r.removed != 0 || r.dismantled != 0 || !r.hostKey.empty()) continue;
        std::map<std::string, int>::const_iterator wk = g_wallKeyIs.find(it->first);
        if (wk != g_wallKeyIs.end() && wk->second != 1) continue;   /* not a wall, or not found since the last clear */
        int sx = 0, sy = 0;
        if (lsOk != 0 && BoxKeySector(it->first.c_str(), &sx, &sy) != 0 && coopbuild::BuildWallGroupNear(lsx, lsy, sx, sy) == 0) continue;
        ++g_wallGroupLooks;
        int v = 0;
        void* w = BdFindPiece(it->first, r, &v);
        if (w == 0) { g_wallKeyIs[it->first] = 2; continue; }   /* its area not loaded here: looked at again after the next load */
        const int isWall = BdIsWall(w);
        g_wallKeyIs[it->first] = isWall;
        if (isWall != 0 && BdWallRecordPod(w) == rec) members.push_back(it->first);
    }
    if (keep != 0) g_wallGroups[lead] = members;
    return members;
}
void BdWallGroupsMark(const std::vector<std::string>& keys, std::vector<std::string>& touched)
{
    for (size_t i = 0; i < keys.size(); ++i)
    {
        std::map<std::string, BdReg>::iterator lr = g_reg.find(keys[i]);
        if (lr == g_reg.end() || lr->second.own == 0 || lr->second.via == 3 || lr->second.removed != 0) continue;
        const std::vector<std::string> mem = BdWallGroupOf(lr->first, lr->second);
        const BdReg& L = lr->second;
        for (size_t j = 0; j < mem.size(); ++j)
        {
            std::map<std::string, BdReg>::iterator mr = g_reg.find(mem[j]);
            if (mr == g_reg.end() || mr->second.own == 0 || mr->second.via == 3 || mr->second.removed != 0 || mr->second.dismantled != 0) continue;
            BdReg& r = mr->second;
            if (L.complete != 0 && r.complete == 0) ItFurnMarkStale(3);   /* T-454: complete 0 -> 1 against the row's last state */
            r.progress = L.progress; r.needed = L.needed; r.complete = L.complete;
            if (L.nMats >= 0)
            {
                r.nMats = L.nMats > (int)coopbuild::kBuildMaxMats ? (int)coopbuild::kBuildMaxMats : L.nMats;
                for (int k = 0; k < r.nMats; ++k) r.mats[k] = L.mats[k];
            }
            r.gen = g_bdGen;
            BdOwnDirty(1);   /* pp.build keeps each grouped wall's state too */
            ++g_wallGroupMarked;
            size_t t = 0;
            while (t < touched.size() && touched[t] != mr->first) ++t;
            if (t == touched.size()) touched.push_back(mr->first);
        }
    }
}

/* T-609 U fix, MAIN THREAD (the hook on the main thread, or BdRekeyDrain): building b's key changed from k0 to k1 under the engine's
   upgrade / downgrade - the same object with a new record (sid). b = the piece when the hook ran on the main thread (0 from the
   queue: then it is looked up by its new key). THIS game's own row moves: the old key goes the dismantle's way (BdSendRemove: the
   row marked removed so pp.build drops it, its tombstone - kept in pp.build and re-sent every roster round - and the REMOVE sent now
   or owed), and a row under the new key takes its placement with the new sid, nothing sent yet and a fresh placement nonce; its
   PLACE + STATE are owed (g_rsOwePlace - BdRosterTick sends every owed REMOVE first). A copy is never moved here: its owner's game
   re-keys it and sends the REMOVE + PLACE. The old key's tombstone cannot end the new piece: every near match (BoxKeyNearKey,
   BdTombDropNear, ItBoxResolve) needs the same record. */
void BdRekeyMain(const char* k0, const char* k1, const char* sid, int down, long gen, void* b)
{
    const int keysOk = (k0 != 0 && k1 != 0 && sid != 0 && k0[0] != 0 && k1[0] != 0 && sid[0] != 0) ? 1 : 0;
    const std::string ok(keysOk != 0 ? k0 : ""), nk(keysOk != 0 ? k1 : "");
    std::map<std::string, BdReg>::iterator it = (keysOk != 0) ? g_reg.find(ok) : g_reg.end();
    std::map<std::string, BdReg>::iterator nt = (keysOk != 0 && ok != nk) ? g_reg.find(nk) : g_reg.end();
    int newRow = coopbuild::kBuildRekeyNewNone;
    if (nt != g_reg.end() && nt->second.via == 3) newRow = coopbuild::kBuildRekeyNewCopy;
    else if (nt != g_reg.end() && nt->second.own != 0 && nt->second.removed == 0 && nt->second.dismantled == 0) newRow = coopbuild::kBuildRekeyNewOwn;
    const int rowOk = (it != g_reg.end()) ? 1 : 0;
    const int act = coopbuild::BuildRekeyAct(keysOk, (keysOk != 0 && ok == nk) ? 1 : 0, (gen == g_bdGen) ? 1 : 0, rowOk,
                                             (rowOk != 0 && it->second.own != 0) ? 1 : 0, (rowOk != 0 && it->second.via == 3) ? 1 : 0,
                                             (rowOk != 0 && (it->second.removed != 0 || it->second.dismantled != 0)) ? 1 : 0, newRow);
    const char* what = (down != 0) ? "downgrade" : "upgrade";
    if (coopbuild::BuildRekeyDropped(act) != 0)
    {
        ::InterlockedIncrement64(&g_rekeyDropped);
        if (g_rekeyLines < kRekeyLineCap)
        {
            ++g_rekeyLines;
            char dl[400];
            _snprintf_s(dl, sizeof(dl), _TRUNCATE, "[BUILD] rekey %s key=%.63s -> %.63s NOT MOVED: %s", what, (k0 != 0 && k0[0] != 0) ? k0 : "?",
                        (k1 != 0 && k1[0] != 0) ? k1 : "?",
                        act == coopbuild::kBuildRekeyNoKey ? "a key or the new record could not be read"
                        : (act == coopbuild::kBuildRekeyStale ? "queued in an earlier world" : "the new key holds another game's copy here"));
            DebugLog(std::string(dl));
        }
    }
    if (act == coopbuild::kBuildRekeySame || act == coopbuild::kBuildRekeyNoKey) return;
    void* piece = b;
    if (act == coopbuild::kBuildRekeyOwn || act == coopbuild::kBuildRekeyRemoveOnly)
    {
        BdReg& r = it->second;
        const long long sent0 = g_removeSent;
        BdSendRemove(it->first, r, (int)coopbuild::kBuildReasonDismantled);
        /* review S1: a tombstone noted but no REMOVE sent or owed (the old row's PLACE never went out this session, but the other game may
           hold a copy from its save) - owe the REMOVE now, so the roster's gate sends it before the new key's PLACE */
        if (g_removeSent == sent0 && g_tombInfo.find(ok) != g_tombInfo.end() && coopbuild::BuildOweRemoveAdd(&g_dmOweRemove, ok) != 0) ++g_dmOweAdded;
        r.gen = g_bdGen;   /* review S2: the removed row carries this world's stamp, so the save retires its pp.build row (not skipped as an earlier world's) */
        int owed = 0;
        for (size_t i = 0; i < g_dmOweRemove.size(); ++i) if (g_dmOweRemove[i] == ok) { owed = 1; break; }
        const int rm = coopbuild::BuildRekeyRemoveOutcome(g_removeSent > sent0 ? 1 : 0, owed, r.sentOk);
        if (act == coopbuild::kBuildRekeyOwn)
        {
            if (nt != g_reg.end() && nt->second.via != 3) BdHelpPlacementEnds(nk, nt->second);   /* a removed row there (an earlier placement): its help goes to its own gone record, as BdOnNewPiece */
            BdReg nr;
            nr.sid = sid; nr.owner = r.owner; nr.progress = r.progress; nr.needed = r.needed;
            nr.complete = r.complete; nr.dismantled = 0; nr.destroyed = r.destroyed; nr.via = r.via; nr.own = 1;
            nr.nMats = r.nMats;
            for (unsigned int i = 0; i < coopbuild::kBuildMaxMats; ++i) nr.mats[i] = r.mats[i];
            nr.hasPos = r.hasPos;
            for (int i = 0; i < 3; ++i) nr.pos[i] = r.pos[i];
            nr.hostKey = r.hostKey; nr.hostForm = r.hostForm; nr.liveKey.clear(); nr.keySame = 1;
            nr.placeOk = r.placeOk;
            for (int i = 0; i < 3; ++i) nr.placePos[i] = r.placePos[i];
            for (int i = 0; i < 4; ++i) nr.placeRot[i] = r.placeRot[i];
            nr.floor = r.floor; nr.outside = r.outside; nr.ownerSlot = r.ownerSlot; nr.handed = r.handed;
            nr.broken = r.broken;
            nr.gen = g_bdGen;
            nr.nonce = BdNewNonce();   /* a new placement: the other game's removed list holds the old key's nonce, never this one */
            g_reg[nk] = nr;
            BdReg& live = g_reg[nk];
            if (piece == 0) { int v = 0; piece = BdFindPiece(nk, live, &v); if (v != kObjKeyFound) piece = 0; }
            if (piece != 0)   /* the live state after the engine's call (the roster's STATE reads it again when it sends) */
            {
                float p = 0, n = 0;
                int c = 0, d = 0, x = 0;
                if (BdReadState(piece, &p, &n, &c, &d, &x) != 0) { live.progress = p; live.needed = n; live.complete = c; live.destroyed = x; }
                float nm[coopbuild::kBuildMaxMats];
                int nmTotal = -1;
                const int nmN = BdReadMats(piece, nm, (int)coopbuild::kBuildMaxMats, &nmTotal);
                if (nmN >= 0) { live.nMats = nmN; for (int j = 0; j < nmN && j < (int)coopbuild::kBuildMaxMats; ++j) live.mats[j] = nm[j]; }
                live.broken = BdBrokenRead(piece);
            }
            g_placeNonceRestore.erase(nk);   /* pp.build's PLACE nonce for the new key (if any) was an earlier placement's */
            BdTombDrop(nk, "T-609 U: this game's own piece was re-keyed onto the key (its upgrade / downgrade)", 0);   /* an owed REMOVE of the key still goes first */
            BdTombDropNear(nk);
            BdCopyListRetire(nk, "this game's own piece was re-keyed onto the key (its upgrade / downgrade)");
            {
                std::map<std::string, double>::iterator mh = g_madeHere.find(ok);   /* the same piece, placed here: its mark moves */
                if (mh != g_madeHere.end()) { const double since = mh->second; g_madeHere.erase(mh); g_madeHere[nk] = since; }
                else g_madeHere.erase(nk);
            }
            g_rsOwePlace.push_back(nk);
            g_wallCacheGen = -1;   /* the wall-group cache names keys: built again at its next use */
            BdOwnDirty(2);
        }
        ++g_rekeyed;
        if (g_rekeyLines < kRekeyLineCap)
        {
            ++g_rekeyLines;
            char line[400];
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] rekey %s key=%.63s -> %.63s own=1 %s %s", what, ok.c_str(), nk.c_str(),
                        coopbuild::BuildRekeyRemoveWord(rm), act == coopbuild::kBuildRekeyOwn ? "placeOwed" : "placeListed");
            DebugLog(std::string(line));
        }
    }
    /* the building's door rows were made under its old key (a gate's key is the gate building's): dropped, so the next door event
       makes them under the new one (DoorsForgetOwner, as before a ruin flip) */
    if (piece != 0) DoorsForgetOwner(piece);
}
/* T-609 U fix, ANY THREAD but the main one (BdUpgradeCall): the pair queued for BdRekeyDrain, stamped with the world it was made in.
   A worker waits for the lock only behind another worker or the main thread's short copy; a full queue drops the pair. PODs only. */
void BdRekeyQueue(const char* k0, const char* k1, const char* sid, int down)
{
    BdRekeyRec e;
    std::memset(&e, 0, sizeof e);
    std::strncpy(e.k0, k0, kKeyCap - 1);
    std::strncpy(e.k1, k1, kKeyCap - 1);
    std::strncpy(e.sid, sid, kUpgSidCap - 1);
    e.down = down;
    e.gen = (long)::InterlockedCompareExchange((volatile LONG*)&g_bdGen, 0, 0);
    if (::InterlockedCompareExchange(&g_rkInit, 0, 0) == 0) { ::InterlockedIncrement64(&g_rekeyDropped); return; }
    ::EnterCriticalSection(&g_rkCs);
    const int pushed = coopbuild::BuildRekeyQueuePush(g_rkQ, &g_rkN, coopbuild::kBuildRekeyQueueCap, e);
    ::LeaveCriticalSection(&g_rkCs);
    ::InterlockedIncrement64(pushed != 0 ? &g_rekeyQueued : &g_rekeyDropped);
}
/* T-609 U fix, MAIN THREAD (BuildTick, before its event drain - a progress event on the new key then finds the new row): the queued
   pairs in the order the hooks ran. The lock is TRIED, never waited for: a busy lock leaves the pairs for the next tick. */
void BdRekeyDrain()
{
    if (::InterlockedCompareExchange(&g_rkInit, 0, 0) == 0) return;
    if (::TryEnterCriticalSection(&g_rkCs) == 0) { ++g_rekeyBusy; return; }
    const int n = g_rkN;
    if (n > 0) std::memcpy(g_rkTake, g_rkQ, sizeof(BdRekeyRec) * (size_t)n);
    g_rkN = 0;
    ::LeaveCriticalSection(&g_rkCs);
    for (int i = 0; i < n; ++i) BdRekeyMain(g_rkTake[i].k0, g_rkTake[i].k1, g_rkTake[i].sid, g_rkTake[i].down, g_rkTake[i].gen, 0);
}

void BuildTick()
{
    BdSweepRemoved();   /* review-build1d D5 */
    FarmTick();   /* par16: farm verdicts, a held game's harvest sent to the writer */
    if (::InterlockedCompareExchange(&g_qInit, 0, 0) == 0) return;
    BdRekeyDrain();   /* T-609 U fix: the re-keys queued off the main thread, before the events that may name their new keys */
    ::EnterCriticalSection(&g_qcs);
    const int n = g_qn;
    if (n > 0) std::memcpy(g_drain, g_q, sizeof(BdEvent) * (size_t)n);
    g_qn = 0;
    ::LeaveCriticalSection(&g_qcs);
    std::vector<std::string> touched;   /* build1-c: own pieces whose progress event arrived this tick - one STATE check per key */
    std::vector<std::string> progKeys;   /* own keys with a progress event this tick - a wall group's lead among them marks its group */
    for (int i = 0; i < n; ++i)
    {
        const BdEvent& e = g_drain[i];
        if (e.kind == 1 && e.harvest != 0 && e.keyOk != 0)
        {   /* a copy's materials changed off the main thread: measured at the next K2 safe point (BdHarvestDrain), each key once */
            const std::string hk(e.key);
            size_t q = 0;
            while (q < g_bdHarvestDue.size() && g_bdHarvestDue[q] != hk) ++q;
            if (q == g_bdHarvestDue.size()) g_bdHarvestDue.push_back(hk);
        }
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
                it->second.broken = e.broken;
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
                    size_t pk = 0;
                    while (pk < progKeys.size() && progKeys[pk] != it->first) ++pk;
                    if (pk == progKeys.size()) progKeys.push_back(it->first);
                }
            }
        }
        const char* k = (e.keyOk != 0) ? e.key : "?";
        char line[400];
        if (e.kind == 1)
            std::sprintf(line, "[BUILD] progress key=%s %.2f/%.2f complete=%d amount=%g thread=%s", k, e.progress, e.needed, e.complete, e.amount,
                         e.onMain ? "main" : "OTHER");
        else if (e.kind == 2)
            std::sprintf(line, "[BUILD] dismantled key=%s progress=%.2f/%.2f isDismantled=%d amount=%g thread=%s", k, e.progress, e.needed,
                         e.dismantled, e.amount, e.onMain ? "main" : "OTHER");
        else
            std::sprintf(line, "[BUILD] destroyed key=%s progress=%.2f/%.2f destroyed=%d by=%s", k, e.progress, e.needed, e.destroyed,
                         e.amount > 0.5f ? "the mod (a STATE applied)" : "this game's engine");   /* P15: either flip */
        if (e.kind == 1) BdLogProg(line);
        else BdLogCapped(line);
    }
    if (!progKeys.empty()) BdWallGroupsMark(progKeys, touched);   /* each grouped own wall takes its lead's record and sends its own STATE */
    for (size_t t = 0; t < touched.size(); ++t)   /* coalesced: the registry now holds the tick's last whole state */
    {
        std::map<std::string, BdReg>::iterator it = g_reg.find(touched[t]);
        if (it != g_reg.end()) BdMaybeSendState(it->first, it->second);
    }
    BdHelpTick();   /* help1: the helper's recorded work out as HELP_WORK, owed across a link loss */
    BdHelpAckTick();   /* help1 fold (review MED 4): confirmations go once pp.build holding them has landed */
    BdRosterTick();   /* build1-f */
}

/* ---- KEPT / TEST-ONLY: A PIECE'S OWN STATE, READ AND WRITTEN (kept list format 3; bldstate / bldset). The offsets are the ones the
   saves write (Read): Building +0x18 'name' (0x551890; the load assigns it with the engine's string assign 0x69B80 - here the mod's own
   string code does the same, as items.cpp P105MakeStr does: one CRT, /MD); +0x161 'pause' (the load 0x554BC0 writes the byte); the
   power classes' +0x3B4 'gets battery', +0x3B5 'power on' (set through vt+0x318 setPowerOn 0x2973B0, which also tells the power
   network) and +0x3C4 'batterycharge' (0x2A1470); production machines' *(+0x448) 'production amount' and the input records +0x488
   (count +0x480, stride 0x20, stock +0) 'consumption amount-N' (0x2A1F60); the interior (+0x1F0): its layout's name +0x98 ('interior
   layout name'; the load sets it through the layout's own setter, vt+0x60 of interior+0x30) and *(+0xD8)+0x68 ('exterior layout
   name', 0x553D60 / 0x554BC0); a DoorLock - the piece's own through vt+0x400 getDoorLock (every building vtable read has the slot;
   the power classes return +0x438), a door's at its +0x370 - holds +0 level, +4 hardness, +0x20 locked, +0x21 broken (0x29FF90). ---- */
const size_t kBdName = 0x18, kBdPause = 0x161, kBdGetsBattery = 0x3B4, kBdPowerOn = 0x3B5, kBdBattery = 0x3C4;
const size_t kBdProdBar = 0x448, kBdProdInputs = 0x488, kBdProdStride = 0x20;
const size_t kBdIntName = 0x98, kBdIntExt = 0xD8, kBdExtName = 0x68;
const size_t kVtBdSetPowerOn = 0x318, kVtBdGetLock = 0x400, kVtBdLayoutName = 0x60;
const size_t kBdLockLevel = 0x0, kBdLockHard = 0x4, kBdLockLocked = 0x20, kBdLockBroken = 0x21;
const unsigned int kBdProdInputSane = 64;
const int kBdTextCap = 160;
typedef void  (*BdSetStrFn)(void* self, const std::string* s);
void* DoorsKeptDoorAt(void* building, int index);   /* doors.cpp, this namespace */
std::string DoorsKeptRead(void* door);
void* DoorsLockOf(void* door);
/* an engine std::string at s set to the n bytes at p (the mod's own string code; see above). 1 done, 0 faulted */
int BdStrSetPod(char* s, const char* p, size_t n)
{
    __try { ((std::string*)s)->assign(p, n); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the piece's own DoorLock (vt+0x400), 0 = none */
void* BdGetLockPod(void* b)
{
    __try { return ((BdGetPtrFn)((*(void***)b)[kVtBdGetLock / 8]))(b); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int BdLockReadPod(const void* lk, int* level, float* hard, int* locked, int* broken)
{
    if (lk == 0) return 0;
    __try
    {
        const char* c = (const char*)lk;
        *level = *(const int*)(c + kBdLockLevel); *hard = *(const float*)(c + kBdLockHard);
        *locked = *(const unsigned char*)(c + kBdLockLocked); *broken = *(const unsigned char*)(c + kBdLockBroken);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the level (0 written too: the lock's load skips a level of 0, so a lock just made may hold another), the hardness when hard >= 0, the
   locked byte when locked >= 0, the broken byte */
int BdLockWritePod(void* lk, int level, float hard, int locked, int broken)
{
    if (lk == 0) return 0;
    __try
    {
        char* c = (char*)lk;
        *(int*)(c + kBdLockLevel) = level;
        if (hard >= 0.0f) *(float*)(c + kBdLockHard) = hard;
        if (locked >= 0) *(unsigned char*)(c + kBdLockLocked) = (unsigned char)(locked != 0 ? 1 : 0);
        *(unsigned char*)(c + kBdLockBroken) = (unsigned char)(broken != 0 ? 1 : 0);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
struct BdPowerPod { int gets, on, pause; float battery; };
int BdPowerReadPod(const void* b, BdPowerPod* o)
{
    __try
    {
        const char* c = (const char*)b;
        o->gets = *(const unsigned char*)(c + kBdGetsBattery); o->on = *(const unsigned char*)(c + kBdPowerOn);
        o->battery = *(const float*)(c + kBdBattery); o->pause = *(const unsigned char*)(c + kBdPause);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the classes whose vtables were read (build/vtable_*.txt) with the load 0x554BC0 at vt+0x380, which reads the record's "pause" into
   the byte +0x161 (decomp_554bc0): Building and every building class under it in the game's RTTI (keptwire::ClassBuilding): the load
   is the same function in every one */
int BdKeptPauseClass(const char* cls)
{
    return keptwire::ClassBuilding(cls);
}
void BdClassOf(void* b, char* cls, int cap);   /* below */
/* setPower: gets battery and the battery written, then setPowerOn (vt+0x318) only when the switch differs (*called = 1); setPause: the
   pause byte. The caller checks the power fields' class (a kept row's class check, or bldset's); BdPowerWritePod checks the pause's. */
int BdPowerWriteRawPod(void* b, const BdPowerPod& v, int setPower, int setPause, int* called)
{
    *called = 0;
    __try
    {
        char* c = (char*)b;
        if (setPower != 0)
        {
            *(unsigned char*)(c + kBdGetsBattery) = (unsigned char)(v.gets != 0 ? 1 : 0);
            *(float*)(c + kBdBattery) = v.battery;
            if ((*(const unsigned char*)(c + kBdPowerOn) != 0 ? 1 : 0) != (v.on != 0 ? 1 : 0))
            {
                ((BdSetBoolFn)((*(void***)b)[kVtBdSetPowerOn / 8]))(b, v.on != 0);
                *called = 1;
            }
        }
        if (setPause != 0) *(unsigned char*)(c + kBdPause) = (unsigned char)(v.pause != 0 ? 1 : 0);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the pause byte +0x161 is written only on a class whose load writes "pause" there (BdKeptPauseClass); on any other it is not written
   (an error line once per class) and a write asked for the pause alone answers 0 */
int BdPowerWritePod(void* b, const BdPowerPod& v, int setPower, int setPause, int* called)
{
    *called = 0;
    if (b == 0) return 0;
    if (setPause != 0)
    {
        char cls[64];
        cls[0] = 0;
        BdClassOf(b, cls, (int)sizeof cls);
        if (BdKeptPauseClass(cls) == 0)
        {
            static std::set<std::string> said;
            if (said.insert(std::string(cls)).second)
                ErrorLog("[BUILD] the pause byte (+0x161) is NOT written on class '" + std::string(cls[0] != 0 ? cls : "?") + "': not a class whose load writes 'pause' there (said once per class)");
            setPause = 0;
            if (setPower == 0) return 0;
        }
    }
    return BdPowerWriteRawPod(b, v, setPower, setPause, called);
}
/* the production bar (*hasBar = 0: the machine has none) and up to cap input stocks (*n = the live count; -1 = not readable) */
int BdProdReadPod(const void* b, float* bar, int* hasBar, float* stocks, int cap, int* n)
{
    *hasBar = 0; *bar = 0.0f; *n = -1;
    __try
    {
        const char* c = (const char*)b;
        const float* pb = *(const float* const*)(c + kBdProdBar);
        if (pb != 0) { *bar = *pb; *hasBar = 1; }
        const unsigned int cnt = *(const unsigned int*)(c + kBProdInputCount);
        const char* in = *(const char* const*)(c + kBdProdInputs);
        if (cnt > kBdProdInputSane || (cnt > 0 && in == 0)) return 1;
        for (unsigned int i = 0; i < cnt && (int)i < cap; ++i) stocks[i] = *(const float*)(in + (size_t)i * kBdProdStride);
        *n = (int)cnt;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the bar when setBar and the machine has one (*barSet = 1), then each stock whose index the live input records hold (*wrote counts) */
int BdProdWritePod(void* b, int setBar, float bar, const zonekeep::KeptStock* st, int ns, int* wrote, int* barSet)
{
    *wrote = 0; *barSet = 0;
    __try
    {
        char* c = (char*)b;
        float* pb = *(float**)(c + kBdProdBar);
        if (setBar != 0 && pb != 0) { *pb = bar; *barSet = 1; }
        const unsigned int cnt = *(const unsigned int*)(c + kBProdInputCount);
        char* in = *(char**)(c + kBdProdInputs);
        if (ns <= 0 || cnt > kBdProdInputSane || in == 0) return 1;
        for (int j = 0; j < ns; ++j)
        {
            if (st[j].index < 0 || (unsigned int)st[j].index >= cnt) continue;
            *(float*)(in + (size_t)st[j].index * kBdProdStride) = st[j].amount;
            ++*wrote;
        }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the interior layout's own name setter (vt+0x60 of interior+0x30), as the load calls it */
int BdLayoutSetNamePod(void* interior, const std::string* s)
{
    if (interior == 0) return 0;
    __try { char* lay = (char*)interior + kInteriorLayout; ((BdSetStrFn)((*(void***)lay)[kVtBdLayoutName / 8]))(lay, s); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the name and - when the piece has an interior here (*hasInt) - its layout names (*hasExt: the exterior layout is there) */
void BdNamesRead(void* b, char* name, char* in, char* ex, int cap, int* hasInt, int* hasExt)
{
    name[0] = 0; in[0] = 0; ex[0] = 0; *hasInt = 0; *hasExt = 0;
    BdStrPod((const char*)b + kBdName, name, cap);
    char* it = (char*)BdPtrAt(b, kBInterior);
    if (it == 0) return;
    *hasInt = 1;
    BdStrPod(it + kBdIntName, in, cap);
    char* ext = (char*)BdPtrAt(it, kBdIntExt);
    if (ext != 0) { *hasExt = 1; BdStrPod(ext + kBdExtName, ex, cap); }
}
void BdClassOf(void* b, char* cls, int cap)
{
    cls[0] = 0;
    const unsigned int vt = PolicyVtableRvaPod(b);
    if (vt == 0 || PolicyClassNamePod(vt, cls, cap) == 0) cls[0] = 0;
}
/* the power classes and the production classes - one set each for the kept lay (BdKeptPowerClass, BdKeptExtras) and the TEST-ONLY
   bldset / bldstate gates. Power: the classes whose vtables were read with setPowerOn 0x2973B0 at +0x318 and whose save / load write
   +0x3B4 / +0x3B5 / +0x3C4 (0x2A1470 / 0x2A1710, directly or through the parent's) - UseableStuff and every class under it
   (keptwire::ClassUseable; the base Building's +0x318 is another function). Production: ProductionBuilding and every class under it,
   whose save / load write *(+0x448) and the input records +0x480 / +0x488 (0x2A1F60 / 0x2A2360, directly or through
   ProductionBuilding's) (keptwire::ClassProduction) */
int BdIsPowerClass(const char* cls) { return keptwire::ClassUseable(cls); }
int BdIsProdClass(const char* cls) { return keptwire::ClassProduction(cls); }
/* the piece a TEST-ONLY lever names: a registry key holding the text (BdFindReg), else the live piece at that exact position key */
void* BdBldPick(const std::string& needle, std::string* key)
{
    int tv = 0;
    if (BdFindReg(needle, key))
    {
        std::map<std::string, BdReg>::iterator r = g_reg.find(*key);
        void* b = (r != g_reg.end()) ? BdFindPiece(*key, r->second, &tv) : 0;
        if (b != 0) return b;
    }
    *key = needle;
    return ObjectByPositionKey(needle.c_str());
}
/* P47: the power a piece receives from its grid (+0x3C0, the battery charge 0x299460 charges from it), the power a generator makes
   when on (+0x3BC, read by the output 0x297720), a battery's capacity (+0x3C8), and the town's grid totals +0x474 / +0x47C / +0x480
   (0x299330 reads +0x474 and +0x480) */
const size_t kPwOutput = 0x3BC, kPwReceived = 0x3C0, kPwCapacity = 0x3C8, kTownPwA = 0x474, kTownPwB = 0x47C, kTownPwC = 0x480;
/* P47 fold 2, TEST-ONLY (bldstate): the town's grid members and the values only its grid update 0x92BE90 writes.
   Members (64-bit counts): consumers +0x3E0 - the count of the 32-byte list at +0x3D8 (0x92BE90 walks +0x3D8 for +0x3E0 records;
   the remove 0x92C4E0 lowers it by 1); producers +0x410 and batteries +0x450 - the sizes of the sets at +0x3F0 / +0x430 that the
   add 0x92CBB0 adds to and the remove 0x92C4E0 removes from (0x92BE90 reads 0 as "none" and divides the surplus by +0x450's count).
   Grid update 0x92BE90 (it zeroes +0x484 / +0x488 at its start and sets +0x470 at its end): +0x470 (byte) onBattery - 1 when the
   producers' output +0x474 was all taken by the consumers, else 0 (the next update's consumers draw on the batteries only while it
   is 1; 0x299330 reads it);
   +0x484 batteryDrawn - when onBattery: the batteries' total +0x480 less what the consumers left of it, else 0;
   +0x488 batteryCharged - when not onBattery: the producers' surplus handed to the batteries (vt+0x528 each), else 0 */
const size_t kTownGridConsumers = 0x3E0, kTownGridProducers = 0x410, kTownGridBatteries = 0x450;   /* 0x92BE90 / 0x92C4E0 / 0x92CBB0 */
const size_t kTownGridOnBattery = 0x470, kTownGridBatDrawn = 0x484, kTownGridBatCharged = 0x488;   /* written by 0x92BE90 */
struct BdTownGridPod { long long consumers, producers, batteries; int onBattery; float drawn, charged; };
/* t's grid members and update values; 0 on a fault */
int BdTownGridReadPod(const void* t, BdTownGridPod* g)
{
    __try
    {
        const char* tc = (const char*)t;
        g->consumers = *(const long long*)(tc + kTownGridConsumers);
        g->producers = *(const long long*)(tc + kTownGridProducers);
        g->batteries = *(const long long*)(tc + kTownGridBatteries);
        g->onBattery = *(const unsigned char*)(tc + kTownGridOnBattery);
        g->drawn = *(const float*)(tc + kTownGridBatDrawn);
        g->charged = *(const float*)(tc + kTownGridBatCharged);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* v[0..2] the piece's received, output, capacity; v[3..5] the town's three totals. 2 = both read, 1 = the piece only, 0 = a fault */
int BdPowerGridPod(const void* b, const void* t, float* v)
{
    __try
    {
        const char* c = (const char*)b;
        v[0] = *(const float*)(c + kPwReceived); v[1] = *(const float*)(c + kPwOutput); v[2] = *(const float*)(c + kPwCapacity);
        if (t == 0) return 1;
        const char* tc = (const char*)t;
        v[3] = *(const float*)(tc + kTownPwA); v[4] = *(const float*)(tc + kTownPwB); v[5] = *(const float*)(tc + kTownPwC);
        return 2;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* H-P47c evidence, TEST-ONLY (bldstate), reads only: the TownList update 0x92C980 walks the TownList's list of ALL towns (+0x58
   count, +0x60 array of Town*; not an active list), calls each town's vt+0xE0 then vt+0x270, and for a town whose vt+0x270 answers 0
   puts it into the grid-update set (TownList+0x180B8, 0x949800) when the town's byte +0x1AC (active) is non-zero and takes it out
   (0x80C230) when it is zero (decomp_92c980). *active = t's +0x1AC; *inList = 1 when t is in the all-towns list, 0 not, -1 the list
   not read; *n = the list's count. 0 = t's byte faulted. (The k...Active... names below are older names for the all-towns list.) */
const size_t kTownActive = 0x1AC, kTownListActiveN = 0x58, kTownListActiveArr = 0x60;
const unsigned kTownListActiveCap = 4096;   /* a count past this is taken as unreadable */
void* BdTownListPtr();   /* below */
int BdTownActivePod(const void* t, int* active, int* inList, unsigned* n)
{
    *inList = -1; *n = 0;
    __try { *active = *(const unsigned char*)((const char*)t + kTownActive); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    void* tl = BdTownListPtr();
    if (tl == 0) return 1;
    __try
    {
        const char* c = (const char*)tl;
        const unsigned cnt = *(const unsigned*)(c + kTownListActiveN);
        void* const* arr = *(void* const* const*)(c + kTownListActiveArr);
        if (cnt > kTownListActiveCap || (cnt > 0 && arr == 0)) return 1;
        int found = 0;
        for (unsigned i = 0; i < cnt; ++i) if (arr[i] == t) { found = 1; break; }
        *n = cnt; *inList = found;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *inList = -1; *n = 0; }
    return 1;
}
/* H-P47c evidence, TEST-ONLY (bldstate), MAIN THREAD (CommandChannelTick). *state = t's +0xD8 (read as a 32-bit int - the width is
   not verified; the wake-up vt+0x228 0x92CCE0 skips recording the area when it is 0, 8 or 10); *areas = the recorded-area count +0x1D0
   (read as 64-bit - the width is not verified); *vt270 = t's vt+0x270 called with only `this`, as 0x92C980 calls it (-1 on a fault);
   *inSet = 1 when t is in the grid-update set TownList+0x180B8, 0 not (at most min(count, kTownListGridSetCap) nodes walked), -1 the
   set not read; *setN = the set's count +0x180D8. The set's first node is *(*(TownList+0x180F0) + *(TownList+0x180D0) * 8); a node's
   next is node[0], its Town* node[2] (64-bit slots). 0 = t's fields faulted */
typedef long long (*BdTownAskFn)(void* town);
const size_t kTownState = 0xD8, kTownAreaN = 0x1D0, kVtTownGridAsk = 0x270;
const size_t kTownListGridSetN = 0x180D8, kTownListGridSetHead = 0x180D0, kTownListGridSetBuckets = 0x180F0;
const unsigned long long kTownListGridSetCap = 4096;
int BdTownGridSetPod(void* t, int* state, long long* areas, long long* vt270, int* inSet, unsigned long long* setN)
{
    *state = -1; *areas = -1; *vt270 = -1; *inSet = -1; *setN = 0;
    __try
    {
        const char* c = (const char*)t;
        *state = *(const int*)(c + kTownState);
        *areas = *(const long long*)(c + kTownAreaN);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    __try
    {
        void** vt = *(void***)t;
        *vt270 = ((BdTownAskFn)vt[kVtTownGridAsk / 8])(t);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *vt270 = -1; }
    void* tl = BdTownListPtr();
    if (tl == 0) return 1;
    __try
    {
        const char* c = (const char*)tl;
        const unsigned long long cnt = *(const unsigned long long*)(c + kTownListGridSetN);
        *setN = cnt;
        int found = 0;
        if (cnt != 0)
        {
            const char* buckets = *(const char* const*)(c + kTownListGridSetBuckets);
            const long long head = *(const long long*)(c + kTownListGridSetHead);
            const long long* node = *(const long long* const*)(buckets + head * 8);
            const unsigned long long lim = cnt < kTownListGridSetCap ? cnt : kTownListGridSetCap;
            for (unsigned long long i = 0; i < lim && node != 0; ++i)
            {
                if ((void*)(uintptr_t)node[2] == t) { found = 1; break; }
                node = (const long long*)(uintptr_t)node[0];
            }
        }
        *inSet = found;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *inSet = -1; }
    return 1;
}
std::string BdTownName(void* t);   /* below */
void* TownOfBuilding(void* building);   /* spawn.cpp: the engine's BuildingGetTown (P47 bldstate) */
void BdPowerGridLine(void* b, const std::string& key)
{
    void* t = TownOfBuilding(b);
    float v[6] = { 0, 0, 0, 0, 0, 0 };
    const int r = BdPowerGridPod(b, t, v);
    int slot = -1;
    const int w = ProdWriterDecide(b, &slot);
    char l[1000];
    _snprintf_s(l, sizeof(l), _TRUNCATE, "[BUILD] bldstate power key=%.63s received(+0x3C0)=%.4f output(+0x3BC)=%.4f capacity(+0x3C8)=%.4f town='%.80s' town[+0x474,+0x47C,+0x480]=%.4f,%.4f,%.4f writer=%s slot%d%s",
                key.c_str(), v[0], v[1], v[2], t != 0 ? BdTownName(t).c_str() : "(none)", v[3], v[4], v[5],
                w == coopfarm::kProdWriterHere ? "this-game" : (w == coopfarm::kProdWriterOther ? "other-game" : "none"), slot,
                r == 0 ? " (unreadable)" : (r == 1 ? " (no town)" : ""));
    DebugLog(std::string(l));
    BdTownGridPod g;
    std::memset(&g, 0, sizeof g);
    if (t == 0) return;
    if (BdTownGridReadPod(t, &g) == 0) { DebugLog("[BUILD] bldstate grid key=" + key + " (the town's grid fields did not read)"); return; }
    int act = -1, inList = -1;
    unsigned listN = 0;
    if (BdTownActivePod(t, &act, &inList, &listN) == 0) act = -1;   /* H-P47c */
    int tstate = -1, inSet = -1;
    long long areas = -1, vt270 = -1;
    unsigned long long setN = 0;
    BdTownGridSetPod(t, &tstate, &areas, &vt270, &inSet, &setN);   /* H-P47c; a fault leaves the -1 / 0 values */
    _snprintf_s(l, sizeof(l), _TRUNCATE, "[BUILD] bldstate grid key=%.63s town='%.80s' members[consumers(+0x3E0),producers(+0x410),batteries(+0x450)]=%lld,%lld,%lld"
                " update[onBattery(+0x470),batteryDrawn(+0x484),batteryCharged(+0x488)]=%d,%.4f,%.4f active(+0x1AC)=%d inAllTowns(TownList+0x58)=%d of %u"
                " state(+0xD8)=%d areas(+0x1D0)=%lld vt270=%lld inGridSet(TownList+0x180B8)=%d of %llu",
                key.c_str(), BdTownName(t).c_str(), g.consumers, g.producers, g.batteries, g.onBattery, g.drawn, g.charged, act, inList, listN,
                tstate, areas, vt270, inSet, setN);
    DebugLog(std::string(l));
}
/* TEST-ONLY, read-only `bldstate <key-substring|key>`: the piece's own state as the kept lay writes it - its class, name and layout
   names, pause, power switch, gets battery, battery, broken and destroyed; a production machine's bar and input stocks; a farm's
   growth, death, start, plants harvested and plant count; its own lock; each door's lock (level, hardness, locked, broken) and state.
   [BUILD] bldstate lines. MAIN THREAD */
std::string BuildStateCommand(const std::string& arg)
{
    if (arg.empty()) return "error bldstate: usage bldstate <key-substring|key>";
    std::string key;
    void* b = BdBldPick(arg, &key);
    if (b == 0) { DebugLog("[BUILD] bldstate '" + arg + "' -> no loaded piece (no registry key holds it and no piece stands at that key)"); return "error bldstate no such piece"; }
    char cls[64], name[kBdTextCap], in[kBdTextCap], ex[kBdTextCap];
    int hi = 0, he = 0;
    BdClassOf(b, cls, (int)sizeof cls);
    BdNamesRead(b, name, in, ex, kBdTextCap, &hi, &he);
    BdPowerPod pw; std::memset(&pw, 0, sizeof pw);
    const int pok = BdPowerReadPod(b, &pw);
    char l[900];
    _snprintf_s(l, sizeof(l), _TRUNCATE, "[BUILD] bldstate key=%.63s class=%.60s name='%.128s' interior='%.128s'%s exterior='%.128s'%s pause=%d power=%d getsBattery=%d battery=%.6f broken=%d destroyed=%d%s",
                key.c_str(), cls, name, in, hi != 0 ? "" : "(no interior)", ex, he != 0 ? "" : "(none)", pw.pause, pw.on, pw.gets, pw.battery,
                BdBrokenRead(b), BdByteAtOr0(b, kBDestroyed), pok != 0 ? "" : " (power fields unreadable)");
    DebugLog(std::string(l));
    if (BdIsPowerClass(cls) != 0) BdPowerGridLine(b, key);   /* P47 */
    if (BdIsProdClass(cls) != 0)
    {
        float bar = 0.0f, st[16];
        int hb = 0, n = -1;
        BdProdReadPod(b, &bar, &hb, st, 16, &n);
        std::string s;
        for (int i = 0; i < n && i < 16; ++i) { char t[40]; _snprintf_s(t, sizeof(t), _TRUNCATE, " %d:%.6f", i, st[i]); s += t; }
        _snprintf_s(l, sizeof(l), _TRUNCATE, "[BUILD] bldstate key=%.63s machine bar=%.6f%s stocks=%d%s", key.c_str(), bar, hb != 0 ? "" : "(none)", n, s.c_str());
        DebugLog(std::string(l));
    }
    if (std::strcmp(cls, ".?AVFarmBuilding@@") == 0)
    {
        float g = 0, d = 0, s = 0;
        int h = 0, n = 0;
        const int fr = FarmKeptRead(b, &g, &d, &s, &h, &n);
        _snprintf_s(l, sizeof(l), _TRUNCATE, "[BUILD] bldstate key=%.63s farm plants=%d growth=%.6f death=%.6f start=%.6f harvested=%d%s", key.c_str(), n, g, d, s, h, fr != 0 ? "" : " (unreadable)");
        DebugLog(std::string(l));
    }
    int lv = 0, lk = 0, lb = 0;
    float lh = 0.0f;
    void* own = BdGetLockPod(b);
    if (own != 0 && BdLockReadPod(own, &lv, &lh, &lk, &lb) != 0)
        _snprintf_s(l, sizeof(l), _TRUNCATE, "[BUILD] bldstate key=%.63s lock own lvl=%d hardness=%.6f locked=%d broken=%d", key.c_str(), lv, lh, lk, lb);
    else _snprintf_s(l, sizeof(l), _TRUNCATE, "[BUILD] bldstate key=%.63s lock own=none", key.c_str());
    DebugLog(std::string(l));
    int doors = 0;
    for (int i = 0; i < 32; ++i)
    {
        void* door = DoorsKeptDoorAt(b, i);
        if (door == 0) break;
        ++doors;
        void* dl = DoorsLockOf(door);
        const std::string ds = DoorsKeptRead(door);
        if (dl != 0 && BdLockReadPod(dl, &lv, &lh, &lk, &lb) != 0)
            _snprintf_s(l, sizeof(l), _TRUNCATE, "[BUILD] bldstate key=%.63s door %d lvl=%d hardness=%.6f lockLocked=%d lockBroken=%d %s", key.c_str(), i, lv, lh, lk, lb, ds.c_str());
        else _snprintf_s(l, sizeof(l), _TRUNCATE, "[BUILD] bldstate key=%.63s door %d lock=none %s", key.c_str(), i, ds.c_str());
        DebugLog(std::string(l));
    }
    return "ok bldstate key=" + key + " class=" + std::string(cls) + " doors=" + zonekeep::ZkNum((long long)doors);
}
/* TEST-ONLY `bldset <key-substring|key> <field> <value>`: armed here, written at the K2 safe point (BdBldSetDrain) */
volatile LONG g_bsPending = 0;
std::string g_bsArg, g_bsField;
float g_bsVal = 0.0f;
std::string BuildSetArm(const std::string& arg)
{
    std::istringstream is(arg);
    std::string key, field, val;
    is >> key >> field >> val;
    const char* usage = "error bldset: usage bldset <key-substring|key> pause|power 0|1 | battery|progress|stock <value> | locklvl|doorlvl <0-100>";
    if (key.empty() || field.empty() || val.empty()) return usage;
    const float v = (float)std::atof(val.c_str());
    if (field == "pause" || field == "power") { if (val != "0" && val != "1") return usage; }
    else if (field == "battery" || field == "progress" || field == "stock") { if (!(v >= 0.0f && v <= 1000000.0f)) return "error bldset: the value must be in [0, 1e6]"; }
    else if (field == "locklvl" || field == "doorlvl") { if (!(v >= 0.0f && v <= 100.0f)) return "error bldset: a lock level must be in [0, 100]"; }
    else return usage;
    if (EngineWritesBlocked()) return "error bldset: engine writes are blocked";
    g_bsArg = key; g_bsField = field; g_bsVal = v;
    ::InterlockedExchange(&g_bsPending, 1);
    DebugLog("[BUILD] bldset " + arg + " ARMED - applied at the next safe point (threadSafeRagdollUpdates)");
    return "ok bldset armed";
}
/* the one field bldset names, as text */
std::string BdBldField(void* b, const std::string& field)
{
    char t[120];
    t[0] = 0;
    if (field == "pause" || field == "power" || field == "battery")
    {
        BdPowerPod pw; std::memset(&pw, 0, sizeof pw);
        if (BdPowerReadPod(b, &pw) == 0) return "unreadable";
        if (field == "pause") _snprintf_s(t, sizeof(t), _TRUNCATE, "pause=%d", pw.pause);
        else if (field == "power") _snprintf_s(t, sizeof(t), _TRUNCATE, "power=%d", pw.on);
        else _snprintf_s(t, sizeof(t), _TRUNCATE, "battery=%.6f getsBattery=%d", pw.battery, pw.gets);
    }
    else if (field == "progress" || field == "stock")
    {
        float bar = 0.0f, st[16];
        int hb = 0, n = -1;
        if (BdProdReadPod(b, &bar, &hb, st, 16, &n) == 0) return "unreadable";
        if (field == "progress") _snprintf_s(t, sizeof(t), _TRUNCATE, "bar=%.6f%s", bar, hb != 0 ? "" : "(none)");
        else _snprintf_s(t, sizeof(t), _TRUNCATE, "stock0=%.6f stocks=%d", n > 0 ? st[0] : -1.0f, n);
    }
    else
    {
        void* lk = (field == "doorlvl") ? DoorsLockOf(DoorsKeptDoorAt(b, 0)) : BdGetLockPod(b);
        int lv = 0, lc = 0, lb = 0;
        float lh = 0.0f;
        if (lk == 0 || BdLockReadPod(lk, &lv, &lh, &lc, &lb) == 0) return "no lock";
        _snprintf_s(t, sizeof(t), _TRUNCATE, "lvl=%d hardness=%.6f locked=%d broken=%d", lv, lh, lc, lb);
    }
    return std::string(t);
}
void BdBldSetDrain()
{
    if (::InterlockedExchange(&g_bsPending, 0) == 0) return;
    if (EngineWritesBlocked()) { DebugLog("[BUILD] bldset skipped: engine writes are blocked (world loading or tearing down)"); return; }
    std::string key;
    void* b = BdBldPick(g_bsArg, &key);
    if (b == 0) { DebugLog("[BUILD] bldset SKIPPED: no loaded piece whose registry key holds '" + g_bsArg + "' (nor one at that exact key)"); return; }
    char cls[64];
    BdClassOf(b, cls, (int)sizeof cls);
    const std::string f = g_bsField;
    const std::string before = BdBldField(b, f);
    std::string result = "CALLED";
    int ok = 1, called = 0;
    if (f == "pause" || f == "power" || f == "battery")
    {
        BdPowerPod pw; std::memset(&pw, 0, sizeof pw);
        BdPowerReadPod(b, &pw);
        if (f == "pause") { pw.pause = g_bsVal > 0.5f ? 1 : 0; ok = BdPowerWritePod(b, pw, 0, 1, &called); }
        else if (BdIsPowerClass(cls) == 0) { ok = 0; result = "REFUSED (not a class read with setPowerOn at vt+0x318)"; }
        else
        {
            if (f == "power") pw.on = g_bsVal > 0.5f ? 1 : 0; else pw.battery = g_bsVal;
            ok = BdPowerWritePod(b, pw, 1, 0, &called);
        }
    }
    else if (f == "progress" || f == "stock")
    {
        int wrote = 0, barSet = 0;
        zonekeep::KeptStock s; s.index = 0; s.amount = g_bsVal;
        if (BdIsProdClass(cls) == 0) { ok = 0; result = "REFUSED (not a production class)"; }
        else if (f == "progress") { ok = BdProdWritePod(b, 1, g_bsVal, 0, 0, &wrote, &barSet); if (ok != 0 && barSet == 0) { ok = 0; result = "REFUSED (the machine has no bar here)"; } }
        else { ok = BdProdWritePod(b, 0, 0.0f, &s, 1, &wrote, &barSet); if (ok != 0 && wrote == 0) { ok = 0; result = "REFUSED (no input record 0 here)"; } }
    }
    else
    {
        const int ownOk = (f == "doorlvl") ? 1 : keptwire::ClassBuilding(cls);   /* the piece's own lock only on the kept lay's lock classes */
        void* lk = (f == "doorlvl") ? DoorsLockOf(DoorsKeptDoorAt(b, 0)) : (ownOk != 0 ? BdGetLockPod(b) : 0);
        int lv = 0, lc = 0, lb = 0;
        float lh = 0.0f;
        if (ownOk == 0) { ok = 0; result = "REFUSED (not a class read with getDoorLock at vt+0x400)"; }
        else if (lk == 0 || BdLockReadPod(lk, &lv, &lh, &lc, &lb) == 0) { ok = 0; result = f == "doorlvl" ? "REFUSED (no door 0 with a lock)" : "REFUSED (no lock of its own)"; }
        else ok = BdLockWritePod(lk, keptwire::LockLevelClamp((int)g_bsVal), -1.0f, -1, lb);
    }
    if (ok == 0 && result == "CALLED") result = "FAULTED";
    if (ok != 0 && (f == "power" || f == "battery" || f == "stock")) PowerMarkPiece(b);   /* P47: the writer's next look sends it (a stock of 0 at once) */
    const std::string after = BdBldField(b, f);
    char l[700];
    _snprintf_s(l, sizeof(l), _TRUNCATE, "[BUILD] bldset %s key=%.63s class=%.60s %s=%g: %s -> %s%s", result.c_str(), key.c_str(), cls, f.c_str(), g_bsVal, before.c_str(), after.c_str(),
                called != 0 ? " (setPowerOn called)" : "");
    DebugLog(std::string(l));
}

/* T-805 TEST-ONLY `buildtest order <key-substring> repair|dismantle|build|materials` (T-903: build and materials too; final-form test code: nothing happens unless the test tool sends
   it): the engine's own addOrderSelectedCharacters (0x7F9280) called at its HOOKED entry - the road a click takes - so detour_order's
   gates judge the order and write their lines. The engine gives a passed order to this game's characters selected now; with none
   selected only the gate lines show. Refused while the order gate is not hooked (the call would skip the gates). */
int g_btOrderTask = 0;
void* BdPlayerIfacePod()
{
    if (kBdPlayerIfaceRva == 0) return 0;
    __try { return *(void* const*)((uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kBdPlayerIfaceRva); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int BdTestOrderCallPod(void* pi, void* b, int task, const float* at)
{
    __try
    {
        const BdOrderFn entry = (BdOrderFn)((uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kBdOrderRva);
        entry(pi, 0, (void*)(uintptr_t)(unsigned int)task, b, 0, 0, (void*)at);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void BdTestOrder()
{
    std::string key;
    if (!BdFindReg(g_btArg, &key))
    {
        ++g_testNoEntry;
        DebugLog("[BUILD] buildtest order SKIPPED: no registry entry whose key contains '" + g_btArg + "'");
        return;
    }
    int tv = 0;
    std::map<std::string, BdReg>::iterator trow = g_reg.find(key);
    void* b = (trow != g_reg.end()) ? BdFindPiece(key, trow->second, &tv) : 0;
    char k2[kKeyCap];
    k2[0] = 0;
    if (b == 0 || ObjectPositionKey(b, k2, kKeyCap, "", 3, 0) == 0 || (key != std::string(k2) && trow->second.liveKey != std::string(k2)))   /* T-805 fold: a copy's own created key (alias) is the piece too */
    {
        ++g_testNotLoaded;
        DebugLog("[BUILD] buildtest order SKIPPED: key=" + key + (b == 0 ? " is not loaded here (no live piece)" : " live piece's key reads '" + std::string(k2) + "'"));
        return;
    }
    if (g_hkOrder != 1 || kBdOrderRva == 0) { DebugLog("[BUILD] buildtest order SKIPPED: the order gate (0x7F9280) is not hooked - the call would skip the gates"); return; }
    void* pi = BdPlayerIfacePod();
    if (pi == 0) { DebugLog("[BUILD] buildtest order SKIPPED: no PlayerInterface here (PlayerInterfaceGlobal unread)"); return; }
    float at[3];
    at[0] = 0.0f; at[1] = 0.0f; at[2] = 0.0f;
    BdCopyF((const char*)b + kObjPos, at, 3);
    const std::string owner = BdFactionName(BdOwnerPod(b));
    const long long h0 = g_repairOrderHelp, r0 = g_repairRefused, o0 = g_orderBlocked, m0 = g_dismantleRefusedMember, d0 = g_dismantleRefusedOther;
    const long long a0 = g_dmAskSent, f0 = g_dmAskOffline;
    const long long oc0 = g_orderOnCopy, rd0 = g_repairDamagedHelp, hr0 = g_helpRefusedK[0] + g_helpRefusedK[1] + g_helpRefusedK[2];   /* T-903 */
    g_inOrderLever = 1;
    const int ok = BdTestOrderCallPod(pi, b, g_btOrderTask, at);
    g_inOrderLever = 0;
    if (ok != 0) ++g_testOrder; else ++g_testFaulted;
    char l[800];
    _snprintf_s(l, sizeof(l), _TRUNCATE, "[BUILD] buildtest order %s task=%d (%s) key=%.63s row=%s owner=%.60s: gates moved repairOrderHelp +%lld repairRefused +%lld orderBlocked +%lld"
                " orderOnCopy +%lld repairDamagedHelp +%lld helpRefused +%lld"
                " dismantleRefusedMember +%lld dismantleRefusedOther +%lld dmAskSent +%lld dmAskOffline +%lld (addOrderSelectedCharacters at its hooked entry; a passed order goes to the characters selected now); testOrder=%lld",
                ok != 0 ? "CALLED" : "FAULTED", g_btOrderTask, g_btOrderTask == coopbuild::kBuildTaskRepair ? "repair" : (g_btOrderTask == coopbuild::kBuildTaskDismantle ? "dismantle"
                : (g_btOrderTask == coopbuild::kBuildTaskBuild ? "build" : "materials")), key.c_str(),
                trow->second.via == 3 ? "copy" : "own", owner.c_str(), g_repairOrderHelp - h0, g_repairRefused - r0, g_orderBlocked - o0,
                g_orderOnCopy - oc0, g_repairDamagedHelp - rd0, g_helpRefusedK[0] + g_helpRefusedK[1] + g_helpRefusedK[2] - hr0,
                g_dismantleRefusedMember - m0, g_dismantleRefusedOther - d0, g_dmAskSent - a0, g_dmAskOffline - f0, g_testOrder);
    DebugLog(std::string(l));
}

std::string BdTestTown(const std::string& arg);   /* T-899 TEST-ONLY read, below with the outposts */
std::string BuildTestArm(const std::string& arg)
{
    std::istringstream is(arg);
    std::string sub, a1, a2, a3;
    is >> sub >> a1 >> a2 >> a3;
    const char* usage = "error buildtest: usage buildtest place <sid> [dx dz] | progress <key-substring|placed> <amount> | dismantle <key-substring|placed>"
                        " | into <near|host-key-substring> <sid> [indoors|<dx>,<dz>] | mats <key-substring> <n> | destroy <key-substring> | broken <key-substring> 0|1"
                        " | order <key-substring> repair|dismantle|build|materials | town <sid|key-substring> | upgrade|downgrade <key-substring>";
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
    if (sub == "town")   /* T-899: TEST-ONLY - read now, nothing written */
    {
        if (a1.empty() || !a2.empty()) return usage;
        return BdTestTown(a1);
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
    else if (sub == "order")   /* T-805: TEST-ONLY - the engine's own order through the hooked order gate */
    {
        if (a1.empty() || (a2 != "repair" && a2 != "dismantle" && a2 != "build" && a2 != "materials")) return usage;
        g_btOrderTask = (a2 == "repair") ? coopbuild::kBuildTaskRepair : (a2 == "dismantle") ? coopbuild::kBuildTaskDismantle
                      : (a2 == "build") ? coopbuild::kBuildTaskBuild : coopbuild::kBuildTaskAddMaterials;   /* T-903: build / materials too */
        kind = 8;
    }
    else if (sub == "broken")   /* TEST-ONLY - the engine's own setBroken (vt+0x310) on the piece */
    {
        if (a1.empty() || (a2 != "0" && a2 != "1")) return usage;
        g_btAmount = (a2 == "1") ? 1.0f : 0.0f;
        kind = 7;
    }
    else if (sub == "upgrade" || sub == "downgrade")   /* T-609 U: TEST-ONLY - the engine's own upgrade / downgrade on an own, complete piece */
    {
        if (a1.empty() || !a2.empty()) return usage;
        kind = (sub == "upgrade") ? 9 : 10;
    }
    else if (sub == "into")   /* build1-e */
    {
        float ox = 0.0f, oz = 0.0f;
        const int offGiven = (!a3.empty() && a3 != "indoors" && coopbuild::BuildIntoOffsetParse(a3, &ox, &oz) != 0) ? 1 : 0;
        if (a1.empty() || a2.empty() || (!a3.empty() && a3 != "indoors" && offGiven == 0)) return usage;   /* a key substring or near; indoors = form 2; <dx>,<dz> = form 0 at that offset */
        g_btHost = a1; g_btIndoors = (a3 == "indoors") ? 1 : 0;
        g_btIntoOff = offGiven; g_btIntoDx = ox; g_btIntoDz = oz;
        kind = 4;
    }
    else return usage;
    g_btArg = (kind == 4) ? a2 : a1;
    ::InterlockedExchange(&g_btPending, kind);
    ++g_testArmed;
    DebugLog("[BUILD] buildtest " + arg + " ARMED - applied at the next safe point (threadSafeRagdollUpdates)");
    return "ok buildtest armed";
}

void BdOutpostDrain();   /* outposttest (TEST-ONLY), below with the outposts */
void BuildTestDrain()
{
    BdOutpostDrain();   /* TEST-ONLY outposttest found / link, armed apart from buildtest */
    BdBldSetDrain();   /* TEST-ONLY bldset, armed apart from buildtest */
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
    else if (kind == 7) BdTestBroken();
    else if (kind == 8) BdTestOrder();   /* T-805 */
    else if (kind == 9 || kind == 10) BdTestUpgrade(kind == 10 ? 1 : 0);   /* T-609 U */
    else BdTestOnEntry(kind);
}

/* build1-b: the K2 safe point (combat.cpp detour_tsRagdollUpdates, right after BuildTestDrain). At most two createBuilding
   calls per frame; an entry that cannot be made yet stays pending (the next safe point is the retry - every frame). */
/* ---- KEPT (src/common/keptwire.h): THE PLAYERS' KEPT PIECES LAID BACK INTO A RESET WORLD. MAIN THREAD (the K2 safe point).
   store.cpp holds the rows the world server sent and sends what this drain asks. Per row, three parts:
   - THE PIECE: only while this game holds the row's area (MineHeldTS strict) it CLAIMs; once the claim is its own it searches the
     row's key (the +/-0.1 drift match, ObjectByPositionKey, then the host's own list for furniture) in a loaded area: none -> made
     (a different piece standing there is named once it stands),
     found -> nothing made (present; an unowned town piece is handed back to its player at no charge), ambiguous / truncated / not
     loaded -> it waits. This game's own piece is made by the restore road (a BdPending with restore=1); another player's is made
     as that player's (its stand-in, a placeholder when the player never joined this world) and handed to it (BdOnNewPiece's owed
     hand-off, which reaches the owner whenever it joins).
   - THE ITEMS: the box's writer (ItBoxWriterAt) CLAIMs and lays them once the piece stands (the 'has' rule: a box already holding
     an item of a kept template gets nothing).
   - THE OWNER: the owner's own game finds its piece (never makes one), registers it as its own and reports LAID owner once its
     pp.build record holding it is committed.
   A sector's rows go buildings-that-hold-furniture first, then the other free-standing pieces, then the furniture; at most
   kKeptSearchPerDrain searches per safe point, and a search in an area waits kRetryGap safe points after a lay there. ---- */
/* declared in this file's own namespace, the one their definitions are in (store.cpp, items.cpp) */
void KeptRowsCopy(std::vector<keptwire::Row>* out);   /* store.cpp, the kept block */
size_t KeptRowsHeld();
size_t KeptRowsActive();
void KeptRowNumbers(std::vector<unsigned>* out);
const keptwire::Row* KeptRowFind(unsigned row);
const std::map<std::string, int>& KeptKeys();
long KeptWorldGen();
void KeptForgetWorld();
int KeptMySlot();
void KeptStoreTick();
bool KeptSendPart(unsigned op, unsigned row, unsigned part);
bool KeptSendLaid(unsigned row, unsigned part, unsigned how, const std::string& liveKey, int sx, int sy);
std::string KeptStoreText();
int ItKeptLayItems(void* b, const float* pos, const char* key, const std::vector<zonekeep::KeptItem>& items, int* laid, std::string* why);   /* items.cpp */
int ItKeptRetryWaits(void* b, const float* pos, const char* key, std::string* why);   /* items.cpp: the box's waiting kept items tried again */
int ItKeptRemake(void* b, const float* pos, const char* key, const std::vector<zonekeep::KeptItem>& items, const std::vector<std::string>& names, std::string* why);
void ItKeptWaitTokens(const char* key, std::vector<std::string>* out);   /* items.cpp: the box's waiting items, as the laid-here record names them */
void ItKeptForget(const char* why);                                   /* items.cpp: a world teardown - the waiting kept items are left, named */
int KeptHereRead(std::string* text, std::string* where);              /* store.cpp: this game's record of what it laid in this world */
int KeptHereWrite(const std::string& text);
std::string KeptWorldIdNow();
int ItKeptBoxMine(void* b, const float* pos, const char* key);
int ItKeptBoxCount(void* b);
void* DoorsKeptDoorAt(void* building, int index);                  /* doors.cpp */
std::string DoorsKeptApply(void* door, int state, int locked, int wants);
std::string DoorsKeptRead(void* door);
struct BdKeptLoc
{
    unsigned pieceClaimAt, itemsClaimAt;   /* the safe point a CLAIM went out (0 = none waiting for its answer) */
    unsigned nextLook;                     /* the next safe point this row is looked at */
    int pieceQueued;                       /* this game's own piece is on the restore road (a BdPending carrying keptRow) */
    unsigned pieceHow, itemsHow;           /* what this game laid here in this world (0 = nothing) - reported again, never laid twice */
    int ownerStep;                         /* the owner part: 0 look, 1 registered (pp.build write asked), 2 written (ownerSeq), 3 reported */
    unsigned long long ownerSeq;
    int refusals;                          /* LAID refused sent from here - after kKeptRefuseCap the row is left to another game */
    int saidWait, saidGrant;               /* one wait / grant line per row and step */
    int overlapSaid;                       /* the overlap check ran for this row (once: its line is never repeated) */
    unsigned extrasDone;                   /* keptwire kX* parts of the piece's own state laid on this game's piece (BdKeptExtras) */
    unsigned extrasEnded;                  /* parts never tried again: looks spent, class not verified, or the piece's template is not the row's */
    unsigned doorsDone;                    /* the kept doors (bit = door index) whose owner, lock and open state were laid here */
    unsigned extrasNext;                   /* the next safe point this game looks for its piece to lay them */
    int extrasTries, extrasSaid;           /* looks on a loaded piece so far (kKeptExtrasTries at most); the parts still waiting were named */
    int itemsWaiting;                      /* kept items this game laid wait for a place in no inventory (items.cpp): tried again here */
    unsigned waitNext;                     /* the next safe point they are tried again */
    std::string boxKey;                    /* the box they wait for */
    std::vector<std::string> waitsToMake;  /* after a reload: the record's waiting items, made again from the row (BdKeptRemake) */
    std::string pieceKey;                  /* the live key of the piece laid here (its own state is laid on it) */
    unsigned extrasWant;                   /* the own state parts the row holds (keptwire::ExtrasWanted), as last seen */
    int layHere;                           /* the own state is laid on this game's piece (keptwire::ExtrasLayHere), as last seen */
    int sx, sy;
    BdKeptLoc() : pieceClaimAt(0), itemsClaimAt(0), nextLook(0), pieceQueued(0), pieceHow(0), itemsHow(0), ownerStep(0), ownerSeq(0), refusals(0),
                  saidWait(0), saidGrant(0), overlapSaid(0), extrasDone(0), extrasEnded(0), doorsDone(0), extrasNext(0), extrasTries(0), extrasSaid(0),
                  itemsWaiting(0), waitNext(0), extrasWant(0), layHere(0), sx(-1), sy(-1) {}
};
std::map<unsigned, BdKeptLoc> g_kept;      /* per world (BuildForgetWorld) */
std::map<int, unsigned> g_keptAreaLay;     /* sector (x * 64 + y) -> the safe point of the last lay there */
int g_keptSearches = 0;
const int kKeptSearchPerDrain = 4;
const unsigned kKeptClaimWait = 4 * kRetryGap;   /* a CLAIM with no answer by then is asked again */
const int kKeptRefuseCap = 3;
const float kKeptOverlapRadius = 3.0f;           /* Guess: a different piece standing this close is named in the overlap line */
const float kKeptOverlapRadiusFurn = 1.0f;       /* Guess: the same for furniture against the other furniture of its host */
const float kKeptOverlapHeightFurn = 1.5f;       /* Guess: furniture this far above or below stands on another floor */
long long g_keptLines = 0, g_keptMade = 0, g_keptPresent = 0, g_keptHandedBack = 0, g_keptQueued = 0, g_keptRefused = 0, g_keptOverlaps = 0;
long long g_keptItemsLaid = 0, g_keptOwnerReported = 0;

void BdKeptLog(const char* line)
{
    ++g_keptLines;
    if (g_keptLines <= 600 || g_keptLines % 100 == 0) DebugLog(std::string(line));
}
/* ---- THIS GAME'S OWN RECORD OF WHAT IT LAID IN THIS WORLD (keptwire::EncodeHere): a file in this player's records store for the
   world (store.cpp KeptHereRead / KeptHereWrite). Each change to what this game laid (per row the piece's how, the items' how and the
   items still waiting for a place, the own state parts laid or ended, the doors laid) is noted in memory with the engine's save
   request count (BdKeptHereSave); a finished save of this game's own moves every change it holds into the record, which is then
   written whole (BdKeptHereSaveFinished). So the record says what this game's last finished save holds: a part it lists is never laid
   again here; a part this game laid that the save it runs on does not hold (a reload of an earlier save) is claimed again and laid
   once more. The record is read when this game first works the world's kept rows (no records store open yet: read again later); a
   file that is there and cannot be read is never written over (said once), and while it is not read nothing is claimed again. ---- */
const int kHereUnread = 0, kHereGood = 1, kHereHeld = 2;
int g_keptHereLoaded = kHereUnread;   /* the record of g_keptHereWorld: not read yet, read (or none yet), or held (there, not readable) */
std::string g_keptHereWorld;
int g_keptHereNoStoreSaid = 0, g_keptHereHeldSaid = 0;
long long g_keptHereWrites = 0, g_keptHereWriteFails = 0, g_keptHereNoted = 0, g_keptHereCommitted = 0, g_keptHereDropped = 0;
struct BdHereEvent { long seq; int beforeCut; unsigned row; keptwire::HereRow h; std::string world; };
std::vector<BdHereEvent> g_keptHerePend;                 /* changes noted and not yet held by a finished save, oldest first */
std::map<unsigned, keptwire::HereRow> g_keptHereNote;    /* each row as last noted (a change is noted once) */
std::map<unsigned, keptwire::HereRow> g_keptHereSaved;   /* the record: what the last finished save holds */
long g_keptHereCut = -1;      /* the save request count at the last world teardown while changes from before it wait (-1 = none) */
long g_keptHereLastReq = -1;  /* the newest finished save request */
int g_keptHereDirty = 0;      /* the record changed and is not written yet */
/* the row's line as this game holds it now; its waiting items: those the record named at the read and not made again yet, and the
   box's waiting items now (items.cpp ItKeptWaitTokens) */
keptwire::HereRow BdKeptHereOf(const BdKeptLoc& L)
{
    keptwire::HereRow h;
    h.pieceHow = L.pieceHow; h.itemsHow = L.itemsHow;
    h.extrasDone = L.extrasDone; h.extrasEnded = L.extrasEnded; h.doorsDone = L.doorsDone;
    if (L.itemsHow != 0)
    {
        h.waits = L.waitsToMake;
        std::vector<std::string> now;
        if (!L.boxKey.empty()) ItKeptWaitTokens(L.boxKey.c_str(), &now);
        h.waits.insert(h.waits.end(), now.begin(), now.end());
    }
    return h;
}
/* every row whose line changed since it was last noted: noted with the save request count now. Always 1 */
int BdKeptHereSave()
{
    const long seq = StoreSaveRequestSeq();
    for (std::map<unsigned, BdKeptLoc>::const_iterator it = g_kept.begin(); it != g_kept.end(); ++it)
    {
        const keptwire::HereRow h = BdKeptHereOf(it->second);
        std::map<unsigned, keptwire::HereRow>::const_iterator n = g_keptHereNote.find(it->first);
        if (n != g_keptHereNote.end() ? keptwire::HereRowSame(n->second, h) != 0 : keptwire::HereRowEmpty(h) != 0) continue;
        g_keptHereNote[it->first] = h;
        BdHereEvent e;
        e.seq = seq; e.beforeCut = 0; e.row = it->first; e.h = h; e.world = g_keptHereWorld;
        g_keptHerePend.push_back(e);
        ++g_keptHereNoted;
    }
    return 1;
}
/* the record written whole when it changed - never over a file that is there and could not be read */
void BdKeptHereWrite()
{
    if (g_keptHereDirty == 0) return;
    if (g_keptHereLoaded == kHereHeld)
    {
        if (g_keptHereHeldSaid == 0)
        {
            g_keptHereHeldSaid = 1;
            ErrorLog(std::string("[KEPT] laid-here record: the file could not be read at the start, so it is NOT written over this run - what this game's saves hold is not remembered across a reload, and nothing it laid is claimed again"));
        }
        return;
    }
    if (g_keptHereLoaded != kHereGood) return;
    const int r = KeptHereWrite(keptwire::EncodeHere(g_keptHereWorld, g_keptHereSaved));
    if (r > 0) { ++g_keptHereWrites; g_keptHereDirty = 0; return; }
    if (r < 0) return;   /* no records store: written once one is open */
    ++g_keptHereWriteFails;
    ErrorLog("[KEPT] laid-here record could NOT be written (writes=" + zonekeep::ZkNum(g_keptHereWrites) + " failed=" + zonekeep::ZkNum(g_keptHereWriteFails)
             + ") - written again at the next finished save or teardown");
}
/* the changes a finished save request reqSeq holds go into the record (keptwire::HereFinishAction); a change from before the last
   world teardown that only a later save could hold, or one of another world, is dropped. A change this game's table does not know
   (it was noted before a reload) goes into the table too: this game knows it laid that part. */
void BdKeptHereCommit(long reqSeq)
{
    if (g_keptHereLoaded != kHereGood || reqSeq < 0) return;   /* the changes wait until the record is read */
    std::vector<BdHereEvent> keep;
    long long c = 0, d = 0;
    int before = 0;
    for (size_t i = 0; i < g_keptHerePend.size(); ++i)
    {
        const BdHereEvent& e = g_keptHerePend[i];
        const int a = e.world != g_keptHereWorld ? keptwire::kHereDrop : keptwire::HereFinishAction(e.seq, e.beforeCut, g_keptHereCut, reqSeq);
        if (a == keptwire::kHereKeep) { keep.push_back(e); before |= e.beforeCut; continue; }
        if (a == keptwire::kHereDrop) { ++d; continue; }
        ++c;
        g_keptHereSaved[e.row] = e.h;
        g_keptHereDirty = 1;
        BdKeptLoc& L = g_kept[e.row];
        if (L.pieceHow == 0) L.pieceHow = e.h.pieceHow;
        if (L.itemsHow == 0 && e.h.itemsHow != 0) { L.itemsHow = e.h.itemsHow; L.waitsToMake = e.h.waits; }
        L.extrasDone |= e.h.extrasDone; L.extrasEnded |= e.h.extrasEnded; L.doorsDone |= e.h.doorsDone;
    }
    g_keptHerePend.swap(keep);
    if (before == 0) g_keptHereCut = -1;
    g_keptHereCommitted += c; g_keptHereDropped += d;
    if (c + d > 0)
        DebugLog("[KEPT] laid-here record: save request " + zonekeep::ZkNum((long long)reqSeq) + " finished - " + zonekeep::ZkNum(c) + " noted change(s) it holds go into the record, "
                 + zonekeep::ZkNum(d) + " dropped (from before a world teardown, no save of that world holds them), " + zonekeep::ZkNum((long long)g_keptHerePend.size())
                 + " wait for a later save");
}
/* the record read for the world the kept rows belong to; a row it lists goes into this game's table (its waiting items are made
   again from the row). No records store yet: read again on a later drain. */
void BdKeptHereLoad()
{
    g_keptHereWorld = KeptWorldIdNow();
    std::string text, where, why;
    const int r = KeptHereRead(&text, &where);
    if (r == -1)
    {
        if (g_keptHereNoStoreSaid == 0)
        {
            g_keptHereNoStoreSaid = 1;
            DebugLog(std::string("[KEPT] laid-here record: no records store is open here yet - read once one is; what this game lays is noted in memory until then, and nothing it laid is claimed again"));
        }
        return;
    }
    std::map<unsigned, keptwire::HereRow> m;
    int other = 0;
    if (r == -2 || (r == 1 && !keptwire::DecodeHere(text, g_keptHereWorld, &m, &why, &other) && other == 0))
    {
        g_keptHereLoaded = kHereHeld;
        if (g_keptHereHeldSaid == 0)
        {
            g_keptHereHeldSaid = 1;
            ErrorLog("[KEPT] laid-here record " + where + (r == -2 ? std::string(" is there and could NOT be read") : " is not a record this build reads (" + why + ")")
                     + " - it is NOT written over this run, and nothing this game laid is claimed again");
        }
        return;
    }
    if (r == 1 && other != 0) DebugLog("[KEPT] laid-here record " + where + " is another world's (" + why + ") - this world's record replaces it at the first finished save");
    if (g_keptHereDirty != 0)   /* changes a finished save held that could not be written before a teardown: newer than the file */
        for (std::map<unsigned, keptwire::HereRow>::const_iterator it = g_keptHereSaved.begin(); it != g_keptHereSaved.end(); ++it) m[it->first] = it->second;
    g_keptHereSaved.swap(m);
    g_keptHereNote = g_keptHereSaved;
    for (size_t i = 0; i < g_keptHerePend.size(); ++i) if (g_keptHerePend[i].world == g_keptHereWorld) g_keptHereNote[g_keptHerePend[i].row] = g_keptHerePend[i].h;
    g_keptHereLoaded = kHereGood;
    long long waits = 0;
    for (std::map<unsigned, keptwire::HereRow>::const_iterator it = g_keptHereSaved.begin(); it != g_keptHereSaved.end(); ++it)
    {
        BdKeptLoc& L = g_kept[it->first];
        if (it->second.pieceHow != 0) L.pieceHow = it->second.pieceHow;
        if (it->second.itemsHow != 0 && L.itemsHow == 0) { L.itemsHow = it->second.itemsHow; L.waitsToMake = it->second.waits; waits += (long long)it->second.waits.size(); }
        L.extrasDone |= it->second.extrasDone; L.extrasEnded |= it->second.extrasEnded; L.doorsDone |= it->second.doorsDone;
    }
    DebugLog("[KEPT] laid-here record " + (r == 0 ? std::string("none yet at ") + where + " - nothing this game's saves hold was laid here" : "read from " + where + ": "
             + zonekeep::ZkNum((long long)g_keptHereSaved.size()) + " row(s), " + zonekeep::ZkNum(waits) + " waiting item name(s) - a part it lists is reported again, never laid again here; a waiting item is made again from its row"));
    BdKeptHereCommit(g_keptHereLastReq);
    BdKeptHereWrite();
}
/* store.cpp: a profile save of this game's own (request reqSeq) finished with its quick.save on disk */
void BdKeptHereSaveFinished(long reqSeq)
{
    if (reqSeq > g_keptHereLastReq) g_keptHereLastReq = reqSeq;
    if (g_keptHereLoaded == kHereUnread && !g_keptHerePend.empty()) BdKeptHereLoad();
    BdKeptHereCommit(reqSeq);
    BdKeptHereWrite();
}
/* a world teardown (or another world's rows): the record is written if it changed, the changes no save asked for before now can hold
   are dropped (keptwire::HereTeardownKeeps), the others wait for the save in flight, and the record is read again for the next world */
void BdKeptHereTeardown()
{
    BdKeptHereWrite();
    const long cut = StoreSaveRequestSeq();
    std::vector<BdHereEvent> keep;
    long long d = 0;
    for (size_t i = 0; i < g_keptHerePend.size(); ++i)
    {
        BdHereEvent e = g_keptHerePend[i];
        if (e.beforeCut == 0 && keptwire::HereTeardownKeeps(e.seq, cut) != 0) { e.beforeCut = 1; keep.push_back(e); }
        else ++d;
    }
    if (d > 0 || !keep.empty())
        DebugLog("[KEPT] laid-here record at a teardown: " + zonekeep::ZkNum(d) + " noted change(s) no save holds dropped (a reload lays those parts again), "
                 + zonekeep::ZkNum((long long)keep.size()) + " wait for the save asked for before it (save request " + zonekeep::ZkNum((long long)cut) + " or earlier)");
    g_keptHereDropped += d;
    g_keptHerePend.swap(keep);
    g_keptHereCut = g_keptHerePend.empty() ? -1 : cut;
    g_keptHereNote.clear();
    if (g_keptHereDirty == 0) g_keptHereSaved.clear();   /* a record not written yet is kept for the next read */
    g_keptHereLoaded = kHereUnread; g_keptHereHeldSaid = 0; g_keptHereNoStoreSaid = 0;
}
/* 1 = this game still has work on row `row` whatever its state on the world server: own state parts on the piece it laid, items it
   laid that wait for a place, or waiting items to make again (store.cpp keeps such a row and the drain works it) */
int BdKeptRowWorkLeft(unsigned row)
{
    std::map<unsigned, BdKeptLoc>::const_iterator it = g_kept.find(row);
    if (it == g_kept.end()) return 0;
    const BdKeptLoc& L = it->second;
    if (L.itemsWaiting != 0 || !L.waitsToMake.empty()) return 1;
    return (L.pieceHow != 0 && L.pieceHow != keptwire::kHowRefused && L.layHere != 0 && keptwire::ExtrasFinished(L.extrasWant, L.extrasDone, L.extrasEnded) == 0) ? 1 : 0;
}
/* 1 = items this game laid in row `row`'s box still wait for a place, or wait to be made again (its SETTLED waits) */
int BdKeptItemsWaitLeft(unsigned row)
{
    std::map<unsigned, BdKeptLoc>::const_iterator it = g_kept.find(row);
    return (it != g_kept.end() && (it->second.itemsWaiting != 0 || !it->second.waitsToMake.empty())) ? 1 : 0;
}
/* the piece's own state needs nothing more on this game: every wanted part laid or ended, or none is laid here (present) */
int BdKeptExtrasFinished(const keptwire::Row& w, const BdKeptLoc& L)
{
    if (keptwire::ExtrasLayHere(w.flags, L.pieceHow) == 0) return 1;
    return keptwire::ExtrasFinished(keptwire::ExtrasWanted(w.p), L.extrasDone, L.extrasEnded);
}
/* the LAID, and this game's own note of it (the per-world table that keeps a part from being laid twice here) */
void BdKeptLaid(const keptwire::Row& w, BdKeptLoc& L, unsigned part, unsigned how, const std::string& liveKey, const char* what)
{
    int sx = -1, sy = -1;
    keptwire::SectorOfPiece(w.p, &sx, &sy);
    char wireKey[poskey::kKeyCap];   /* the wire takes keys of at most kKeyCap - 1 characters: a 64-byte key of kKeyCap..63 goes as its '#<hash>' form */
    wireKey[0] = 0;
    const std::string sendKey = (liveKey.size() >= (size_t)poskey::kKeyCap && poskey::WireForm(liveKey.c_str(), wireKey, poskey::kKeyCap) == 2) ? std::string(wireKey) : liveKey;
    const bool sent = KeptSendLaid(w.row, part, how, sendKey, sx, sy);
    if (how == keptwire::kHowRefused) ++L.refusals;
    else if (part == keptwire::kPartPiece) { L.pieceHow = how; if (!liveKey.empty()) L.pieceKey = liveKey; }
    else if (part == keptwire::kPartItems) L.itemsHow = how;
    if (how != keptwire::kHowRefused) BdKeptHereSave();
    if (how != keptwire::kHowRefused && part == keptwire::kPartPiece && sx >= 0) g_keptAreaLay[sx * 64 + sy] = g_drainNo;
    char line[700];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u %s LAID how=%s key=%.63s live=%.63s owner slot %d: %s%s", w.row, keptwire::PartName(part), keptwire::HowName(how),
                w.p.key.c_str(), liveKey.empty() ? "-" : liveKey.c_str(), w.p.ownerSlot, what, sent ? "" : " - NOT SENT (the world-server link is down; reported again when the claim is this game's again)");
    BdKeptLog(line);
}
/* a piece part laid here (made, present or handed back) or refused: its LAID at once. The own state parts not laid yet are laid on
   this game's piece by the drain while parts remain, whatever the row's state on the world server (BdKeptRow). */
void BdKeptPieceLaid(const keptwire::Row& w, BdKeptLoc& L, unsigned how, const std::string& liveKey, const char* what)
{
    BdKeptLaid(w, L, keptwire::kPartPiece, how, liveKey, what);
    if (how == keptwire::kHowRefused || BdKeptExtrasFinished(w, L) != 0) return;
    const unsigned left = keptwire::ExtrasWanted(w.p) & ~(L.extrasDone | L.extrasEnded);
    BdKeptLog(("[KEPT] row " + zonekeep::ZkNum((long long)w.row) + " piece " + keptwire::HowName(how) + " here key=" + (liveKey.empty() ? w.p.key : liveKey)
               + " - LAID sent; its own state still to lay here, while the piece is loaded: " + keptwire::ExtrasNames(left)).c_str());
}
/* furniture's host on this game: the live building at the row's host key, its position and rotation. 1 = found; 0 = waits (*why) */
int BdKeptHost(const keptwire::Row& w, void** host, float* hostPos, float* hostQ, const char** why)
{
    *host = ObjectByPositionKey(w.p.hostKey.c_str());
    const int hv = ObjectByPositionKeyVerdict();
    if (*host == 0) { *why = BdVerdictWord(hv); return 0; }
    if (BdByteAtOr0(*host, 0x162) != 0) { *why = "the host is dismantled here"; return 0; }   /* a ruined host (+0x1A1) still takes the piece */
    if (!BdCopyF((const char*)*host + kObjPos, hostPos, 3) || BdOrientPod(*host, hostQ) == 0) { *why = "the host's position or rotation could not be read"; return 0; }
    return 1;
}
/* the piece's placement inputs, as createBuilding takes them: free-standing = the height above the ground at (x, z); furniture = the
   host's own frame (keptwire::LocalOfWorld). 1 = in pos / rot */
int BdKeptInputs(const keptwire::Row& w, int furn, const float* hostPos, const float* hostQ, float* pos, float* rot)
{
    if (furn != 0) { keptwire::LocalOfWorld(hostPos, hostQ, w.p.pos, w.p.rot, pos, rot); return 1; }
    for (int i = 0; i < 4; ++i) rot[i] = w.p.rot[i];
    return BdPlaceInputFromLive(w.p.key, w.p.pos, pos);
}
/* A DIFFERENT PIECE STANDING ON A KEPT PIECE JUST MADE: named in an error line, once per row (the kept piece stands anyway). A
   free-standing piece is compared with the loaded pieces of its area, furniture with the other furniture of its host (and only on
   its floor). Not named: the piece itself, a kept piece of the same player (laid back moments before: the same owner and a key
   among this world's kept rows) and a wall touching a wall (walls are laid end to end). */
int BdKeptIsKeptOfSlot(const char* key, int slot)
{
    const std::map<std::string, int>& keys = KeptKeys();
    std::map<std::string, int>::const_iterator e = keys.find(key);
    if (e != keys.end()) return e->second == slot ? 1 : 0;
    for (e = keys.begin(); e != keys.end(); ++e) if (e->second == slot && BoxKeyNearKey(key, e->first.c_str()) != 0) return 1;
    return 0;
}
void BdKeptOverlap(const keptwire::Row& w, BdKeptLoc& L, void* self, void* host)
{
    if (L.overlapSaid != 0 || self == 0) return;
    L.overlapSaid = 1;
    static void* list[512];
    const int furn = host != 0 ? 1 : 0;
    int sx = -1, sy = -1, trunc = 0, complete = 0;
    keptwire::SectorOfPiece(w.p, &sx, &sy);
    const int n = furn != 0 ? BdHostPieces(host, list, 512, &complete) : LoadedBuildingsInSector(sx, sy, list, 512, &trunc);
    float sp[3];
    if (!BdCopyF((const char*)self + kObjPos, sp, 3)) for (int i = 0; i < 3; ++i) sp[i] = w.p.pos[i];
    const int selfWall = furn == 0 ? BdIsWall(self) : 0;
    void* selfOwner = BdOwnerPod(self);
    const float rad = furn != 0 ? kKeptOverlapRadiusFurn : kKeptOverlapRadius;
    for (int i = 0; i < n; ++i)
    {
        if (list[i] == self) continue;
        float lp[3];
        if (!BdCopyF((const char*)list[i] + kObjPos, lp, 3)) continue;
        if (keptwire::NearXZ(lp[0], lp[2], sp[0], sp[2], rad) == 0) continue;
        if (furn != 0 && (lp[1] - sp[1] > kKeptOverlapHeightFurn || sp[1] - lp[1] > kKeptOverlapHeightFurn)) continue;
        if (selfWall != 0 && BdIsWall(list[i]) != 0) continue;
        char k[kKeyCap], sid[96];
        k[0] = 0;
        if (ObjectPositionKey(list[i], k, kKeyCap, "", 3, furn) == 0) k[0] = 0;
        if (k[0] != 0 && BdOwnerPod(list[i]) == selfOwner && BdKeptIsKeptOfSlot(k, w.p.ownerSlot) != 0) continue;
        BdSidOf(list[i], sid, (int)sizeof sid);
        ++g_keptOverlaps;
        char line[700];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u OVERLAP at (%.1f, %.1f) in area %d,%d%s: the kept piece %.63s ('%.60s', owner slot %d) was laid where another piece stands - %.63s ('%.60s') at (%.1f, %.1f); overlaps=%lld",
                    w.row, sp[0], sp[2], sx, sy, furn != 0 ? " (inside its host)" : "", w.p.key.c_str(), w.p.tmpl.c_str(), w.p.ownerSlot, k[0] != 0 ? k : "?", sid, lp[0], lp[2], g_keptOverlaps);
        ErrorLog(std::string(line));
    }
}
/* the row's progress and delivered materials onto a piece just made (the guarded write BdMarkRestored uses; progress kept below
   what completes it), then the destroyed flag, then the broken flag (after the progress write, which clears it) */
void BdKeptApplyState(const keptwire::Row& w, void* b)
{
    if (w.p.complete == 0 && BdIsWall(b) == 0)
    {
        float mats[coopbuild::kBuildMaxMats];
        const int nm = keptwire::MatsArray(w.p.mats, mats, (int)coopbuild::kBuildMaxMats);
        float p = 0, need = 0;
        int c = 0, d = 0, x = 0;
        BdReadState(b, &p, &need, &c, &d, &x);
        float pr = w.p.progress < 0.0f ? 0.0f : w.p.progress;
        if (need > 0.0f && pr > need * 0.999f) pr = need * 0.999f;
        const unsigned long long rUpd = Rva("UpdatePhysicalWithProgress");
        const int writeProgress = (rUpd != 0 && pr > p) ? 1 : 0;
        if (writeProgress != 0 || nm > 0)
        {
            int matsLive = -1, stage = 0;
            BdApplyStatePod(b, writeProgress, pr, nm, mats, &matsLive, (uintptr_t)::GetModuleHandleA(0) + (uintptr_t)rUpd, &stage);
        }
    }
    if (w.p.destroyed != 0) { DoorsForgetOwner(b); BdDestroyPod(b, true); }   /* the building's door rows go before its doors do */
    if (w.p.broken != BdBrokenRead(b)) BdSetBrokenPod(b, w.p.broken != 0);
}
/* an own piece's ruin and broken flags just set by a kept lay: its registry row takes both, read live from the piece, and its STATE
   carries them to the other games */
void BdKeptNoteBroken(const std::string& key, void* b)
{
    std::map<std::string, BdReg>::iterator r = g_reg.find(key);
    if (r == g_reg.end()) return;
    float p = 0, n = 0;
    int c = 0, d = 0, x = 0;
    if (BdReadState(b, &p, &n, &c, &d, &x) != 0) r->second.destroyed = x;
    r->second.broken = BdBrokenRead(b);
    BdMaybeSendState(r->first, r->second);
}
/* a kept ruin's doors are not laid (a ruin has no doors): named with its lay */
void BdKeptRuinDoors(const keptwire::Row& w)
{
    if (w.p.doors.empty() || w.p.destroyed == 0) return;
    char line[300];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u doors: %u kept door(s) not laid - the piece is a ruin, which has no doors", w.row, (unsigned)w.p.doors.size());
    BdKeptLog(line);
}
/* ---- THE PIECE'S OWN STATE LAID BACK (kept list format 3): its name and layout names, the pause, gets battery, the battery and the
   power switch, the production bar, the input stocks, the farm, its own lock, and each door - its owner, its locked and open state
   (doors.cpp DoorsKeptApply) and its lock's level, hardness and broken byte. EVERY game lays them on its own live piece once, while
   the row's piece part is laid and not yet done; the game that laid the piece lays them while parts remain, whatever the row's state
   (its LAID went at once). None is laid on a piece whose first lay found it standing (keptwire::ExtrasLayHere, the row's present
   flag). This game's record (BdKeptHereSave) keeps a part laid here and held by a finished save from being laid again after a reload. A part that cannot be laid yet (no interior, no plants, a door not made yet, a kept input
   index past the machine's records here) is looked at again; after kKeptExtrasTries looks on a loaded piece it is ended and named in
   an error line. A part whose raw fields or engine calls are not verified on the live piece's class is ended at once and named ("not
   laid: class not verified"); so is every part on a piece whose template is not the row's. ---- */
const int kKeptExtrasTries = 30;   /* Guess: looks on a loaded piece, kRetryGap safe points apart */
/* the power classes (BdIsPowerClass, the same set bldset uses): UseableStuff and every class under it, read with setPowerOn 0x2973B0
   at vt+0x318 (rva 0x03FA71). The power fields +0x3B4 / +0x3B5 / +0x3C4 are that function's. */
int BdKeptPowerClass(const char* cls)
{
    return BdIsPowerClass(cls);
}
/* the classes whose vtables were read with getDoorLock at vt+0x400 (Building's, GatewayBuilding's and WallBuilding's 0x297C80,
   DoorStuff's 0x2ADC30, UseableStuff's and every class under it 0x0F6D40): Building and every building class under it
   (keptwire::ClassBuilding; bldset's own-lock gate is the same set) */
int BdKeptLockClass(const char* cls)
{
    return keptwire::ClassBuilding(cls);
}
void BdKeptExtras(const keptwire::Row& w, void* b, unsigned* done, unsigned* doorsDone, unsigned* ended, std::string* did, std::string* waits)
{
    const zonekeep::KeptPiece& p = w.p;
    const unsigned want = keptwire::ExtrasWanted(p);
    char t[700];
    char cls[64];
    BdClassOf(b, cls, (int)sizeof cls);
    unsigned unverified = 0;
    if (BdKeptPowerClass(cls) == 0) unverified |= keptwire::kXPower;
    if (BdIsProdClass(cls) == 0) unverified |= keptwire::kXProd | keptwire::kXInputs;   /* *(+0x448), +0x480 / +0x488: the production classes */
    if (std::strstr(cls, "Farm") == 0) unverified |= keptwire::kXFarm;   /* the farm's own fields (farm.cpp FarmKeptLay) */
    if (BdKeptLockClass(cls) == 0) unverified |= keptwire::kXLock;
    if (BdKeptPauseClass(cls) == 0) unverified |= keptwire::kXPause;   /* the byte +0x161 is the pause only where the load writes it */
    unverified &= want & ~(*done | *ended);
    if (unverified != 0)
    {
        *ended |= unverified;
        ErrorLog("[KEPT] row " + zonekeep::ZkNum((long long)w.row) + " own state on key=" + p.key + " not laid: class not verified ('" + std::string(cls[0] != 0 ? cls : "?")
                 + "' is not a class read with these fields or calls): " + keptwire::ExtrasNames(unverified));
        *did += "; not laid: class not verified - " + keptwire::ExtrasNames(unverified);
    }
    const unsigned open = want & ~(*done | *ended);
    if ((open & keptwire::kXName) != 0)
    {
        char cur[kBdTextCap], now[kBdTextCap];
        cur[0] = 0; now[0] = 0;
        BdStrPod((const char*)b + kBdName, cur, kBdTextCap);
        const int ok = (p.name == cur) ? 1 : BdStrSetPod((char*)b + kBdName, p.name.data(), p.name.size());
        BdStrPod((const char*)b + kBdName, now, kBdTextCap);
        if (ok != 0) *done |= keptwire::kXName; else *waits += " name (the write faulted)";
        _snprintf_s(t, sizeof(t), _TRUNCATE, "; name kept '%.128s' (was '%.128s', now '%.128s')", p.name.c_str(), cur, now);
        *did += t;
    }
    if ((open & keptwire::kXLayout) != 0)
    {
        char n0[kBdTextCap], i0[kBdTextCap], e0[kBdTextCap], n1[kBdTextCap], i1[kBdTextCap], e1[kBdTextCap];
        int hi = 0, he = 0;
        BdNamesRead(b, n0, i0, e0, kBdTextCap, &hi, &he);
        if (hi == 0) *waits += " layout names (no interior here yet)";
        else
        {
            void* interior = BdPtrAt(b, kBInterior);
            int ok = 1;
            if (p.interiorName != i0) ok = BdLayoutSetNamePod(interior, &p.interiorName) != 0 ? ok : 0;
            if (he != 0 && p.exteriorName != e0)
            {
                char* ext = (char*)BdPtrAt(interior, kBdIntExt);
                ok = (ext != 0 && BdStrSetPod(ext + kBdExtName, p.exteriorName.data(), p.exteriorName.size()) != 0) ? ok : 0;
            }
            BdNamesRead(b, n1, i1, e1, kBdTextCap, &hi, &he);
            if (ok != 0) *done |= keptwire::kXLayout; else *waits += " layout names (a write faulted)";
            _snprintf_s(t, sizeof(t), _TRUNCATE, "; layout names kept '%.128s' / '%.128s' (was '%.128s' / '%.128s', now '%.128s' / '%.128s'%s)", p.interiorName.c_str(), p.exteriorName.c_str(),
                        i0, e0, i1, e1, he != 0 ? "" : ", no exterior layout here");
            *did += t;
        }
    }
    const unsigned pp = open & (keptwire::kXPause | keptwire::kXPower);
    if (pp != 0)
    {
        BdPowerPod v0, v1, k;
        std::memset(&v0, 0, sizeof v0);
        v1 = v0;
        BdPowerReadPod(b, &v0);
        k = v0;
        if ((pp & keptwire::kXPower) != 0) { k.gets = p.getsBattery; k.on = p.powerOn; k.battery = p.battery; }
        if ((pp & keptwire::kXPause) != 0) k.pause = p.pause;
        int called = 0;
        const int ok = BdPowerWritePod(b, k, (pp & keptwire::kXPower) != 0 ? 1 : 0, (pp & keptwire::kXPause) != 0 ? 1 : 0, &called);
        BdPowerReadPod(b, &v1);
        if (ok != 0) *done |= pp; else *waits += " pause / power (the write faulted)";
        if (ok != 0 && (pp & keptwire::kXPower) != 0) PowerMarkPiece(b);   /* P47: a switch laid here is looked at by the next K2 drain */
        if ((pp & keptwire::kXPause) != 0) { _snprintf_s(t, sizeof(t), _TRUNCATE, "; pause kept %d (was %d, now %d)", p.pause, v0.pause, v1.pause); *did += t; }
        if ((pp & keptwire::kXPower) != 0)
        {
            _snprintf_s(t, sizeof(t), _TRUNCATE, "; power kept getsBattery %d power %d battery %.6f (was %d %d %.6f, now %d %d %.6f%s)", p.getsBattery, p.powerOn, p.battery,
                        v0.gets, v0.on, v0.battery, v1.gets, v1.on, v1.battery, called != 0 ? "; setPowerOn called" : "");
            *did += t;
        }
    }
    const unsigned pr = open & (keptwire::kXProd | keptwire::kXInputs);
    if (pr != 0)
    {
        float bar0 = 0.0f, bar1 = 0.0f, s0[16], s1[16];
        int hb0 = 0, hb1 = 0, n0 = -1, n1 = -1, wrote = 0, barSet = 0;
        for (int i = 0; i < 16; ++i) { s0[i] = 0.0f; s1[i] = 0.0f; }
        BdProdReadPod(b, &bar0, &hb0, s0, 16, &n0);
        const int inputsNow = ((pr & keptwire::kXInputs) != 0 && keptwire::InputsAllFit(p.inputs, n0) != 0) ? 1 : 0;   /* every kept index has its record here */
        const int ns = inputsNow != 0 ? (int)p.inputs.size() : 0;
        const int ok = BdProdWritePod(b, (pr & keptwire::kXProd) != 0 ? 1 : 0, p.production, ns > 0 ? &p.inputs[0] : 0, ns, &wrote, &barSet);
        const int rd1 = BdProdReadPod(b, &bar1, &hb1, s1, 16, &n1);   /* 0 = the read faulted: hb1 says nothing then */
        if (ok == 0) *waits += " machine (the write faulted)";
        else
        {
            if ((pr & keptwire::kXProd) != 0)
            {
                if (barSet != 0) *done |= keptwire::kXProd;
                else if (rd1 == 0) *waits += " production bar (the machine did not read back after the write)";
                else if (keptwire::ProdBarNothingToLay(hb1, p.production) != 0) { *done |= keptwire::kXProd; *did += "; bar kept 0 - this machine has no bar, as the engine's own load leaves it"; }
                else *waits += " production bar (none here yet)";
            }
            if ((pr & keptwire::kXInputs) != 0)
            {
                if (inputsNow != 0) *done |= keptwire::kXInputs;
                else if (n0 < 0) *waits += " input stocks (no records here yet)";
                else { _snprintf_s(t, sizeof(t), _TRUNCATE, " input stocks (a kept index past the %d input record(s) here)", n0); *waits += t; }
            }
        }
        if ((pr & keptwire::kXProd) != 0 && barSet != 0) { _snprintf_s(t, sizeof(t), _TRUNCATE, "; bar kept %.6f (was %.6f, now %.6f)", p.production, bar0, bar1); *did += t; }
        if (ok != 0 && inputsNow != 0)
            for (int j = 0; j < ns; ++j)
            {
                const int i = p.inputs[(size_t)j].index;
                if (i >= 0 && i < 16) { _snprintf_s(t, sizeof(t), _TRUNCATE, "; stock %d kept %.6f (was %.6f, now %.6f)", i, p.inputs[(size_t)j].amount, s0[i], s1[i]); *did += t; }
            }
    }
    if ((open & keptwire::kXFarm) != 0)
    {
        int plants = 0;
        const int ok = FarmKeptLay(b, p.growth, p.death, p.start, p.harvested, &plants);
        if (ok == 0) *waits += " farm (no plants here yet)";
        else
        {
            *done |= keptwire::kXFarm;
            float g = 0, d = 0, s = 0;
            int h = 0, n = 0;
            FarmKeptRead(b, &g, &d, &s, &h, &n);
            _snprintf_s(t, sizeof(t), _TRUNCATE, "; farm kept growth %.6f death %.6f start %.6f harvested %d (now %.6f %.6f %.6f %d, %d plant(s))", p.growth, p.death, p.start, p.harvested,
                        g, d, s, h, n);
            *did += t;
        }
    }
    if ((open & keptwire::kXLock) != 0)
    {
        void* lk = BdGetLockPod(b);
        int v0 = 0, c0 = 0, b0 = 0, v1 = 0, c1 = 0, b1 = 0;
        float h0 = 0.0f, h1 = 0.0f;
        if (lk == 0 || BdLockReadPod(lk, &v0, &h0, &c0, &b0) == 0) *waits += " its own lock (none here yet)";
        else
        {
            /* the record's unnumbered 'broken' is the lock's (its save writes it after the building's own) - the one value kept for both */
            const int ok = BdLockWritePod(lk, keptwire::LockLevelClamp(p.lockLevel), p.lockHardness, p.lockLocked, p.broken);
            BdLockReadPod(lk, &v1, &h1, &c1, &b1);
            if (ok != 0) *done |= keptwire::kXLock; else *waits += " its own lock (the write faulted)";
            _snprintf_s(t, sizeof(t), _TRUNCATE, "; own lock kept lvl %d hardness %.6f locked %d broken %d (was %d %.6f %d %d, now %d %.6f %d %d)", p.lockLevel, p.lockHardness, p.lockLocked, p.broken,
                        v0, h0, c0, b0, v1, h1, c1, b1);
            *did += t;
        }
    }
    if ((open & keptwire::kXDoors) != 0)
    {
        int missing = 0;
        const int mySlot = KeptMySlot();
        for (size_t j = 0; j < p.doors.size(); ++j)
        {
            const zonekeep::KeptDoor& d = p.doors[j];
            const unsigned bit = 1u << ((unsigned)d.index & 31u);
            if ((*doorsDone & bit) != 0) continue;
            void* door = DoorsKeptDoorAt(b, d.index);
            if (door == 0) { ++missing; continue; }
            void* owner = 0;
            if (d.ownerSlot >= 0) owner = (d.ownerSlot == mySlot) ? (void*)LocalPlayerFaction() : (void*)OwnerFactionForSlot(d.ownerSlot, true);
            if (d.ownerSlot >= 0 && owner == 0)
            {
                ++missing;
                _snprintf_s(t, sizeof(t), _TRUNCATE, " door %d (no faction for its owner, slot %d, here yet)", d.index, d.ownerSlot);
                *waits += t;
                continue;
            }
            if (owner != 0 && BdOwnerPod(door) != owner) BdSetFactionPod(door, owner);
            const std::string ap = DoorsKeptApply(door, keptwire::KeptDoorOpen(d.state) != 0 ? 1 : 0, d.locked, d.wantsLock);
            void* lk = DoorsLockOf(door);
            int v0 = 0, c0 = 0, b0 = 0, v1 = 0, c1 = 0, b1 = 0;
            float h0 = 0.0f, h1 = 0.0f;
            char lt[200];
            if (lk == 0 || BdLockReadPod(lk, &v0, &h0, &c0, &b0) == 0) _snprintf_s(lt, sizeof(lt), _TRUNCATE, "no lock here (kept lvl %d)", d.lockLevel);
            else
            {
                BdLockWritePod(lk, keptwire::LockLevelClamp(d.lockLevel), d.hardness, -1, d.broken);
                BdLockReadPod(lk, &v1, &h1, &c1, &b1);
                _snprintf_s(lt, sizeof(lt), _TRUNCATE, "lock lvl %d hardness %.6f broken %d (was %d %.6f %d, now %d %.6f %d)", d.lockLevel, d.hardness, d.broken, v0, h0, b0, v1, h1, b1);
            }
            *doorsDone |= bit;
            _snprintf_s(t, sizeof(t), _TRUNCATE, "; door %d laid (kept state=%d laid %s, locked=%d wants=%d gate=%d owner slot %d): owner '%.60s'; %.200s; %.200s", d.index, d.state,
                        keptwire::KeptDoorOpen(d.state) != 0 ? "open" : "closed", d.locked, d.wantsLock, d.gateCode, d.ownerSlot, BdFactionName(BdOwnerPod(door)).c_str(), lt, ap.c_str());
            *did += t;
        }
        if (missing == 0) *done |= keptwire::kXDoors;
        else { _snprintf_s(t, sizeof(t), _TRUNCATE, " %d door(s) (not made here yet, or their owner's faction not here yet)", missing); *waits += t; }
    }
}
/* one look on piece b, loaded here: the parts not laid or ended yet laid now, and one [KEPT] line naming each part laid (kept, was, now)
   and each part still waiting. A piece whose template is not the row's gets none (each part ended, named in an error line). After
   kKeptExtrasTries looks the parts still waiting are ended and named in an error line. */
void BdKeptExtrasNow(const keptwire::Row& w, BdKeptLoc& L, void* b, const char* how)
{
    const unsigned want = keptwire::ExtrasWanted(w.p);
    if (b == 0 || keptwire::ExtrasFinished(want, L.extrasDone, L.extrasEnded) != 0) return;
    ++L.extrasTries;
    char sid[96];
    sid[0] = 0;
    BdSidOf(b, sid, (int)sizeof sid);
    if (sid[0] != 0 && w.p.tmpl != sid)
    {
        const unsigned left = want & ~(L.extrasDone | L.extrasEnded);
        L.extrasEnded |= left;
        BdKeptHereSave();
        ErrorLog("[KEPT] row " + zonekeep::ZkNum((long long)w.row) + " own state NOT laid on key=" + w.p.key + " (" + how + "): the piece here is '" + sid
                 + "', the row's template is '" + w.p.tmpl + "' (a substituted template) - not laid: " + keptwire::ExtrasNames(left));
        return;
    }
    const unsigned d0 = L.extrasDone, e0 = L.extrasEnded, o0 = L.doorsDone;
    std::string did, waits;
    BdKeptExtras(w, b, &L.extrasDone, &L.doorsDone, &L.extrasEnded, &did, &waits);
    if (L.extrasDone != d0 || L.extrasEnded != e0 || L.doorsDone != o0) BdKeptHereSave();
    if (!did.empty() || L.extrasSaid == 0)
    {
        L.extrasSaid = 1;
        const std::string line = "[KEPT] row " + zonekeep::ZkNum((long long)w.row) + " own state laid key=" + w.p.key + " (" + how + ", look " + zonekeep::ZkNum((long long)L.extrasTries)
                                 + " of " + zonekeep::ZkNum((long long)kKeptExtrasTries) + ")" + (did.empty() ? std::string(": nothing laid yet") : did)
                                 + (waits.empty() ? std::string() : " - waits:" + waits);
        BdKeptLog(line.c_str());
    }
    if (keptwire::ExtrasFinished(want, L.extrasDone, L.extrasEnded) == 0 && L.extrasTries >= kKeptExtrasTries)
    {
        const unsigned left = want & ~(L.extrasDone | L.extrasEnded);
        L.extrasEnded |= left;
        BdKeptHereSave();
        ErrorLog("[KEPT] row " + zonekeep::ZkNum((long long)w.row) + " own state NOT laid in full on key=" + w.p.key + " after " + zonekeep::ZkNum((long long)L.extrasTries)
                 + " looks while the piece was loaded here - not laid: " + keptwire::ExtrasNames(left) + " (waiting:" + waits + ")");
    }
}
/* EVERY GAME, while the row's piece part is laid and not done, and the game that laid the piece while parts remain: this game's own
   piece found and the parts not laid yet laid on it. A look is counted only when the piece is loaded here. */
void BdKeptExtrasStep(const keptwire::Row& w, BdKeptLoc& L)
{
    if (keptwire::ExtrasFinished(keptwire::ExtrasWanted(w.p), L.extrasDone, L.extrasEnded) != 0 || g_drainNo < L.extrasNext) return;
    if (g_keptSearches >= kKeptSearchPerDrain) return;
    ++g_keptSearches;
    L.extrasNext = g_drainNo + kRetryGap;
    const std::string key = L.pieceKey.empty() ? w.p.key : L.pieceKey;
    void* live = ObjectByPositionKey(key.c_str());
    if (live == 0 || BdByteAtOr0(live, kBOwnDismantled) != 0) return;   /* not loaded here: no look counted */
    BdKeptExtrasNow(w, L, live, "this game's piece");
}
/* THE OWNER'S GAME puts the row's ruin and broken flag on its own piece, so its own STATE carries them to every game (a hand-over
   PLACE has no destroyed field: without this the owner's STATE would repair the ruin the laying game made) */
void BdKeptOwnerState(const keptwire::Row& w, void* live, const std::string& lk)
{
    float p = 0, n = 0;
    int c = 0, d = 0, x = 0;
    if (BdReadState(live, &p, &n, &c, &d, &x) == 0 || d != 0) return;
    const int ruin = (w.p.destroyed != 0 && x == 0) ? 1 : 0;
    if (ruin != 0) { DoorsForgetOwner(live); BdDestroyPod(live, true); }   /* the building's door rows go before its doors do */
    const int br0 = BdBrokenRead(live);
    if (w.p.broken != br0) BdSetBrokenPod(live, w.p.broken != 0);
    if (ruin == 0 && w.p.broken == br0) return;
    BdKeptNoteBroken(lk, live);
    int x1 = 0;
    BdReadState(live, &p, &n, &c, &d, &x1);
    char line[500];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u owner: the kept state put on this player's own piece %.63s - destroyed %d -> %d, broken %d -> %d; its STATE carries it to the other games",
                w.row, lk.c_str(), x, x1, br0, BdBrokenRead(live));
    BdKeptLog(line);
}
/* createBuilding for another player's kept piece, owned by `owner` (the recipe BdCopyOneBody uses). 0 = not made (*why) */
void* BdKeptCreate(const keptwire::Row& w, int furn, void* host, float* pos, float* rot, void* owner, const char** why)
{
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    const unsigned long long rGet = Rva("GdcGetData"), rCont = Rva("GdContainer");
    void* factory = (coop::GameWorldPtr() != 0) ? BdPtrAt((const void*)coop::GameWorldPtr(), kWorldFactory) : 0;
    if (kBdCreateRva == 0 || rGet == 0 || rCont == 0 || factory == 0) { *why = "CreateBuilding / GdcGetData / GdContainer or the factory is missing"; return 0; }
    void* gd = BdGetData(base + (uintptr_t)rGet, base + (uintptr_t)rCont, &w.p.tmpl);
    int type = -1;
    if (gd != 0) BdIntAt(gd, kGdType, &type);
    if (gd == 0 || type != 0) { *why = gd == 0 ? "its record is not in this game's data" : "its record is not a BUILDING record"; return 0; }
    void* fo = 0;
    if (furn != 0)
    {
        char* interior = (char*)BdPtrAt(host, kBInterior);
        if (interior == 0) { *why = "the host has no interior (+0x1F0) here"; return 0; }
        fo = (void*)(interior + kInteriorLayout);
    }
    const BdCreateFn fn = (orig_create != 0) ? orig_create : (BdCreateFn)(base + (uintptr_t)kBdCreateRva);
    int faulted = 0;
    void* b = (furn != 0) ? BdCallCreateEx(fn, factory, gd, pos, owner, rot, fo, host, w.p.complete != 0, w.p.floor, false, &faulted)
                          : BdCallCreate(fn, factory, gd, pos, owner, rot, w.p.complete != 0, &faulted);
    if (b == 0) { *why = faulted != 0 ? "createBuilding FAULTED" : "createBuilding returned null"; return 0; }
    if (BdOwnerPod(b) != owner) BdSetFactionPod(b, owner);
    return b;
}
/* an unowned town piece that matches the row: given back to its player at no charge - for a house for sale the purchase's world side
   without the money (BdBuyFxBefore -> the owner write -> BdBuyFxAfter); this game's own slot registers it as its own, another
   slot's is handed to that player. 1 = given back */
int BdKeptHandBack(const keptwire::Row& w, void* live, int furn, void* host, const float* hostPos, const float* hostQ, int mySlot)
{
    const int slot = w.p.ownerSlot;
    ::Faction* to = (slot == mySlot) ? LocalPlayerFaction() : OwnerFactionForSlot(slot, true);
    if (to == 0) return 0;
    void* o = BdOwnerPod(live);
    static BdBuyFx fx;   /* MAIN THREAD; 2 KB - not on the stack */
    std::memset(&fx, 0, sizeof(fx));
    const int sale = (furn == 0 && BdIsForSalePod(live) == 1) ? 1 : 0;
    if (sale != 0) BdBuyFxBefore(live, o, &fx);
    if (BdSetFactionPod(live, (void*)to) == 0) return 0;
    if (sale != 0) BdBuyFxAfter(live, (void*)to, &fx);
    if (w.p.destroyed != 0 && BdByteAtOr0(live, kBDestroyed) == 0) { DoorsForgetOwner(live); BdDestroyPod(live, true); }   /* the kept ruin */
    if (w.p.broken != BdBrokenRead(live)) BdSetBrokenPod(live, w.p.broken != 0);
    BdKeptRuinDoors(w);   /* the doors' owner, lock and open state are laid by the door step right after (BdKeptExtras), over the purchase's open doors */
    float lp[3], lq[4], in[3], rq[4];
    if (!BdCopyF((const char*)live + kObjPos, lp, 3) || BdOrientPod(live, lq) == 0) { for (int i = 0; i < 3; ++i) lp[i] = w.p.pos[i]; for (int i = 0; i < 4; ++i) lq[i] = w.p.rot[i]; }
    if (furn != 0) keptwire::LocalOfWorld(hostPos, hostQ, lp, lq, in, rq);
    else { for (int i = 0; i < 4; ++i) rq[i] = lq[i]; if (BdPlaceInputFromLive(w.p.key, lp, in) == 0) for (int i = 0; i < 3; ++i) in[i] = lp[i]; }
    float p = 0, n = 0;
    int c = 0, d = 0, x = 0;
    BdReadState(live, &p, &n, &c, &d, &x);
    char k[kKeyCap];
    k[0] = 0;
    const std::string key = ObjectPositionKey(live, k, kKeyCap, "", 3, 0) != 0 ? std::string(k) : w.p.key;
    if (slot == mySlot)
    {
        BdReg& r = BdRegisterOwnLive(live, key, in, rq, p, n, c, x, 0);
        r.broken = BdBrokenRead(live);
        if (furn != 0) { char hk[kKeyCap]; hk[0] = 0; if (ObjectPositionKey(host, hk, kKeyCap, "", 3, 0) != 0) r.hostKey = hk; r.hostForm = coopbuild::kBuildHostPlain; r.floor = w.p.floor; r.outside = 0; }
        g_rsOwePlace.push_back(key);   /* the other games are owed its PLACE + STATE (BdRosterTick) */
        BdOwnDirty(2);
    }
    else BdOnNewPiece(live, 2, in, rq, furn != 0 ? 3 : 1, furn != 0 ? host : 0, coopbuild::kBuildHostPlain, w.p.floor, 0, slot, 0);
    ++g_keptHandedBack;
    if (sale != 0) g_ocLastKey = key;   /* KEPT: `buyhouse show last` names the house given back */
    char line[700];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u handed back key=%.63s: the matching piece was '%.60s' (nobody's player) - now '%.60s' (slot %d), no charge; for sale=%d, world side: residents %s, town list out=%d in=%d, doors %u, interior %u; handedBack=%lld",
                w.row, key.c_str(), BdFactionName(o).c_str(), BdFactionName(BdOwnerPod(live)).c_str(), slot, sale,
                sale == 0 ? "not run (not a house for sale)" : (fx.res == 1 ? "moved" : (fx.res == 0 ? "none" : "SKIPPED or FAULTED")), fx.townOut, fx.townIn, fx.doorsN, fx.interiorN, g_keptHandedBack);
    BdKeptLog(line);
    return 1;
}
/* the restore road finished a kept row of this game's own (BuildCopyDrain): its LAID; a piece made here gets the row's destroyed
   and broken flags, its doors and the overlap check */
/* ---- OUTPOSTS (src/common/townkeep.h): THE PLAYERS' OUTPOSTS CARRIED INTO A RESET WORLD. MAIN THREAD (the K2 safe point, the session
   dispatch and the command channel). A player's outpost is a Town in the town list's player-town list (TownList +0x68: count +0x70,
   array +0x78 - decomp_933f40). A Town's name is the std::string at +0x18 (slot +0x10 assigns it - decomp_d3f40), its handle words
   are at +0x60 (type, container, stamp, index, serial: the five words a building's area record names as its town), slot +0x40 gives
   its centre (decomp_9263f0), slot +0x58 its faction (0x593BB0) and slot +0xA0 sets the faction. A building's town is the handle at
   +0x1D0 (its five words at +0x1D8, type first), read with the engine's own BuildingGetTown (coop::TownOfBuilding: the top parent's
   town, else the nearest town by position) and written as the engine's own outpost founding 0x4D6300 re-towns a standing building
   (0x791A70: the town's five handle words copied in, nothing else) for a top-level piece only - a piece standing inside a building
   takes its host's town. The pointer into a town's model-piece store at +0x1F8 (Town+0x250, set by createPhysical 0x55FF50 from the
   town its words name) stays with the store that holds the piece's models until they are torn down, as with the engine's own
   founding; a town is destroyed only while its store is empty (BdTownStoreReadPod, baseshow::StoreKeep). (+0x80, words +0x88, is the
   hand of the building a piece stands inside, not a town.)
   - THIS GAME'S OUTPOSTS are said to the world server (TOWNS SAY): looked at every 5 s, sent when they changed, on a new link slot and
     again every 60 s - name, centre and handle words of each player town of this player's faction (at most kMaxTownsPerSlot).
   - THE KEPT OUTPOSTS (TOWNS LIST) come before the first kept rows of a link, each with the keys of its kept pieces. Before a kept piece
     of an outpost is laid here (made, made again on the restore road, found standing, or found by its owner's game) the outpost is
     taken or founded (townkeep TownLayStep): the town this world already holds for it, else a town of the piece owner's faction
     standing within kTownSameRadius of the outpost's centre, else a new one founded the engine's way (TownFoundPlayer: the
     player_town_template town at that centre, added to the player-town list), named and given the owner's faction (this player's, or
     the owner's stand-in). The laid piece is then linked to it (+0x1D8); a piece standing inside a building keeps its host's town (nothing
     written). A founding that fails is not tried again in this world; the
     piece is laid without it. ---- */
static unsigned long long kBdTownFoundRva = 0; static AddrReg kBdTownFoundRva_reg("TownFoundPlayer", &kBdTownFoundRva);   /* Steam_1.0.65 0x933F40: TownList: found a player outpost at a position */
static unsigned long long kBdTownListRva = 0;  static AddrReg kBdTownListRva_reg("TownList", &kBdTownListRva);           /* Steam_1.0.65 0x21330A0 (var): a POINTER to the TownList */
typedef void* (*BdTownFoundFn)(void* list, const float* pos);
typedef void  (*BdTownSetNameFn)(void* town, const std::string* name);
typedef void  (*BdTownSetFactionFn)(void* town, void* faction, bool flag);
typedef void* (*BdTownGetFn)(void* town);
typedef float* (*BdTownPosFn)(void* town, float* out);
const size_t kTownListPlayerN = 0x70, kTownListPlayerArr = 0x78;
const size_t kTownHandWords = 0x60;   /* RootObjectBase handle (+0x58) + 8: type, container, stamp, index, serial */
const size_t kTownNameStr = 0x18;     /* RootObjectBase name */
const size_t kBdTownHandWords = 0x1D8;   /* Building: its town's five handle words (type first) - the hand at +0x1D0 */
const size_t kBdParentHand = 0x88;       /* Building: the type word of the hand of the building it stands inside (0xB = none) */
const size_t kBdLayout = 0x238;          /* Building: the layout it is part of (non-zero: it stands inside a building) */
const int kVtTownSetName = 0x10, kVtTownPos = 0x40, kVtTownFaction = 0x58, kVtTownSetFaction = 0xA0;
const int kTownWalkCap = 256;
const DWORD kTownLookMs = 5000, kTownSayAgainMs = 60000;
std::vector<townkeep::TownRow> g_townRows;     /* the kept outposts the world server sent on this link */
std::map<std::string, size_t> g_townKeyIdx;     /* a kept piece's key -> its outpost in g_townRows */
long g_townRowsGen = -1;                        /* KeptWorldGen() when g_townRows last came in: rows of another world's link are dropped with its kept table */
long long g_townLinkHost = 0;                   /* kept pieces of an outpost standing inside a building: their town is their host's, nothing written */
std::map<size_t, void*> g_townLive;             /* an outpost -> the town standing for it in this world */
std::set<size_t> g_townFailed;                  /* outposts whose founding failed in this world: not tried again */
std::vector<char> g_townSaid;                   /* the last SAY sent */
int g_townSaidSlot = -1;
DWORD g_townSaidAt = 0, g_townLookAt = 0;
long long g_townRowsIn = 0, g_townBad = 0, g_townFounded = 0, g_townStanding = 0, g_townLinked = 0, g_townFoundFail = 0, g_townSays = 0;
bool TownsSendUp(const std::vector<char>& b);   /* store.cpp: TOWNS up on the world-server link */
void* BdTownListPtr()
{
    if (kBdTownListRva == 0) return 0;
    __try { return *(void**)(uintptr_t)AddrAbs(kBdTownListRva); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the player towns, at most cap; -1 = the list is not readable */
int BdPlayerTownsPod(void** out, int cap)
{
    void* tl = BdTownListPtr();
    if (tl == 0) return -1;
    __try
    {
        const char* c = (const char*)tl;
        const unsigned n = *(const unsigned*)(c + kTownListPlayerN);
        void* const* arr = *(void* const* const*)(c + kTownListPlayerArr);
        if (n > 0 && arr == 0) return -1;
        int k = 0;
        for (unsigned i = 0; i < n && k < cap; ++i) if (arr[i] != 0) out[k++] = arr[i];
        return k;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int BdTownReadPod(void* t, float* pos, int* hand, void** faction)
{
    __try
    {
        void** vt = *(void***)t;
        ((BdTownPosFn)vt[kVtTownPos / 8])(t, pos);
        *faction = ((BdTownGetFn)vt[kVtTownFaction / 8])(t);
        std::memcpy(hand, (const char*)t + kTownHandWords, 20);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* TownFoundPlayer(TownList*, const Vector3*): the new Town (0 = missing from the table, faulted or none) */
void* BdTownFoundPod(const float* pos)
{
    void* tl = BdTownListPtr();
    if (tl == 0 || kBdTownFoundRva == 0) return 0;
    __try { return ((BdTownFoundFn)(uintptr_t)AddrAbs(kBdTownFoundRva))(tl, pos); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int BdTownSetNamePod(void* t, const std::string* s)
{
    __try { ((BdTownSetNameFn)((*(void***)t)[kVtTownSetName / 8]))(t, s); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int BdTownSetFactionPod(void* t, void* f)
{
    __try { ((BdTownSetFactionFn)((*(void***)t)[kVtTownSetFaction / 8]))(t, f, false); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* T-811: a town's size level (Town+0x388 'plevel', set by the engine 0x48B770 from the buildings near it), 0..3; 0 when unread */
const size_t kTownLevel = 0x388;
int BdTownLevelPod(void* t)
{
    int v = 0;
    __try { v = *(const int*)((const char*)t + kTownLevel); }
    __except (EXCEPTION_EXECUTE_HANDLER) { v = 0; }
    return (v < 0 || v > townkeep::kLevelMax) ? 0 : v;
}
void* TownOfBuilding(void* building);   /* spawn.cpp (namespace coop): the engine's BuildingGetTown, guarded, ANY THREAD */
/* a building's town: the engine's own lookup (BuildingGetTown - the top parent's town, hand +0x1D0, else the nearest town by position;
   0 only for no building or a fault) */
void* BdBuildingTownPod(void* b)
{
    if (b == 0) return 0;
    return TownOfBuilding(b);
}
/* 1 = building b stands at the top level (its parent hand at +0x88 is none, type 0xB, and it is part of no layout at +0x238); 0 = it
   stands inside a building (its town is its host's); -1 = a fault */
int BdBuildingTopLevelPod(void* b)
{
    __try { return (*(int*)((char*)b + kBdParentHand) == 0xB && *(void**)((char*)b + kBdLayout) == 0) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* a building's own town handle words (Building +0x1D8, type first; type 0xB = none): 1 = read, 0 = a fault */
int BdBuildingTownHandPod(void* b, int* hand)
{
    __try { std::memcpy(hand, (const char*)b + kBdTownHandWords, 20); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* P47 (T-609 L): switched-on power pieces whose town link was rewritten - each switched off before the copy (it leaves its old town's
   grid) and on after it (it joins the new town's grid) */
long long g_pwRelinked = 0;
/* the switch +0x3B5 set through setPowerOn 0x2973B0 (vt+0x318), which joins / leaves the grid of the town the piece's town hand names.
   1 = called (the switch was the other way), 0 = already so or a fault */
int BdPowerSwitchPod(void* b, int on)
{
    __try
    {
        if ((*(const unsigned char*)((const char*)b + kBdPowerOn) != 0 ? 1 : 0) == (on != 0 ? 1 : 0)) return 0;
        ((BdSetBoolFn)((*(void***)b)[kVtBdSetPowerOn / 8]))(b, on != 0);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* 1 = b is a complete power piece (BdIsPowerClass; complete read live, BdReadState) that was switched on and is now switched off.
   A piece still under construction, or one whose construction state does not read, is left as it is */
int BdPowerLeaveGrid(void* b)
{
    char cls[64];
    BdClassOf(b, cls, (int)sizeof cls);
    if (BdIsPowerClass(cls) == 0) return 0;
    float prog = 0.0f, need = 0.0f;
    int complete = 0, dism = 0, destroyed = 0;
    if (BdReadState(b, &prog, &need, &complete, &dism, &destroyed) == 0 || complete == 0) return 0;
    return BdPowerSwitchPod(b, 0);
}
/* a town's five handle words (t+0x60) copied to Building +0x1D8, nothing else: the way the engine's own outpost founding 0x4D6300
   re-towns a standing building (0x791A70(b+0x1D0, town)). The pointer into a town's model-piece store at +0x1F8 is left as it is: it
   names the store that holds this building's models (createPhysical 0x55FF50 set it), and the model teardown frees them through it.
   (The engine's set-town routine 0x926190, used only by its town merge, moves +0x1F8 but not the pieces - with it the teardown would
   free nothing; the merge moves the store first, 0x930510.) 1 = written, 0 = no town or a fault */
int BdTownWordsCopyPod(void* b, void* t)
{
    if (b == 0 || t == 0) return 0;
    __try
    {
        std::memcpy((char*)b + kBdTownHandWords, (const char*)t + kTownHandWords, 20);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* a building linked to town t as the engine re-towns a standing building (BdTownWordsCopyPod: t's five handle words to Building
   +0x1D8; +0x1F8 stays with the store holding its models - a town still holding pieces is not destroyed, baseshow::StoreKeep).
   1 = written; -1 = the piece stands inside a building (BdBuildingTopLevelPod) - its town is its host's, nothing written; 0 = a fault
   (or no town: nothing written).
   A complete, switched-on power piece (BdPowerLeaveGrid; one under construction is left as it is) is switched off before the
   copy and on after it: its grid is the one its town hand names
   (setPowerOn 0x2973B0 reaches it through +0x1D0), so the copy alone would leave it on the old town's grid. MAIN THREAD */
int BdBuildingSetTownPod(void* b, void* t)
{
    if (t == 0) return 0;
    const int top = BdBuildingTopLevelPod(b);
    if (top < 0) return 0;
    if (top == 0) return -1;
    const int wasOn = BdPowerLeaveGrid(b);
    const int r = BdTownWordsCopyPod(b, t);
    if (wasOn != 0 && BdPowerSwitchPod(b, 1) != 0 && r == 1) ++g_pwRelinked;   /* a faulted copy: switched back on where it was */
    return r;
}
/* the raw type words of a building's parent hand (+0x88, 0xB = stands inside none) and its town hand (+0x1D8): 1 = read, 0 = a fault */
int BdBuildingHandTypesPod(void* b, int* parentType, int* townType)
{
    __try { *parentType = *(int*)((char*)b + kBdParentHand); *townType = *(int*)((char*)b + kBdTownHandWords); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
std::string BdTownName(void* t)
{
    char nm[kBdTextCap];
    nm[0] = 0;
    if (t != 0) BdStrPod((const char*)t + kTownNameStr, nm, kBdTextCap);
    return std::string(nm);
}
std::string BdTownText(void* t)
{
    float p[3] = { 0.0f, 0.0f, 0.0f };
    int h[5] = { 0, 0, 0, 0, 0 };
    void* f = 0;
    const int ok = BdTownReadPod(t, p, h, &f);
    char b[400];
    _snprintf_s(b, sizeof(b), _TRUNCATE, "'%.80s' at (%.1f, %.1f, %.1f) handle %d/%d/%d/%d/%d faction '%.60s'%s", BdTownName(t).c_str(), p[0], p[1], p[2],
                h[0], h[1], h[2], h[3], h[4], BdFactionName(f).c_str(), ok != 0 ? "" : " (its reads FAULTED)");
    return std::string(b);
}
/* TOWNS LIST: the kept outposts; a first message starts the list again (and this world's taken towns are looked for again) */
void BdShowKeysNote(unsigned flags, std::vector<townkeep::TownRow>* rows);   /* below, with the SHOW code */
void BuildTownsNote(const char* p, size_t n)
{
    unsigned op = 0;
    int first = 0;
    std::vector<townkeep::TownRow> rows;
    char line[400];
    const int read = townkeep::DecodeTowns(p, n, &op, &first, &rows);
    if (read != 0 && op == townkeep::kOpShowKeys) { BdShowKeysNote((unsigned)first, &rows); return; }   /* protocol 101: the shown rows' piece keys */
    if (read == 0 || op != townkeep::kOpList)
    {
        ++g_townBad;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] a TOWNS message from the world server could not be read (%u bytes) - ignored; townsBad=%lld", (unsigned)n, g_townBad);
        ErrorLog(line);
        return;
    }
    if (first != 0) { g_townRows.clear(); g_townLive.clear(); g_townFailed.clear(); }
    for (size_t i = 0; i < rows.size(); ++i)
    {
        g_townRows.push_back(rows[i]);
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] outpost row %u: slot %d '%.80s' at (%.1f, %.1f, %.1f), %u kept piece(s)", (unsigned)(g_townRows.size() - 1), rows[i].owner,
                    rows[i].name.c_str(), rows[i].pos[0], rows[i].pos[1], rows[i].pos[2], (unsigned)rows[i].keys.size());
        BdKeptLog(line);
    }
    g_townKeyIdx = townkeep::KeyIndex(g_townRows);
    g_townRowsGen = KeptWorldGen();
    g_townRowsIn += (long long)rows.size();
}
/* owner 938: THE TEAM GATE of kept outpost idx here (townkeep::KeptTownTeamGate) - its owner this game's own player (or fac this
   player's own faction) or a teammate on this game's copy of the team table (TeamSameAnyThread, as the power ASK's teammate check;
   TeamTableKnown = the table of the current link is in). fac 0 = no faction to judge by. MAIN THREAD */
long long g_townTeamWait = 0, g_townNotMate = 0;   /* townTeamWait: outposts that waited for the team table, each once a world */
std::set<int> g_townNotMateSaid;   /* MAIN THREAD: owners whose outposts were said to get no town here, this world (said again after a join) */
std::set<std::string> g_townTeamWaitSaid;   /* MAIN THREAD: the outposts (owner and centre bits) counted waiting, this world */
int BdKeptTownGate(size_t idx, ::Faction* fac)
{
    if (idx >= g_townRows.size()) return townkeep::kTeamGateWait;
    const int owner = g_townRows[idx].owner;
    const int me = KeptMySlot();
    const int ownFac = (fac != 0 && fac == LocalPlayerFaction()) ? 1 : 0;
    const int same = (owner >= 0 && me >= 0 && owner != me && TeamSameAnyThread(me, owner)) ? 1 : 0;
    return townkeep::KeptTownTeamGate(owner, me, ownFac, TeamTableKnown() ? 1 : 0, same);
}
/* the gate's verdict counted; a non-teammate owner said once a world, a wait the first 10 times */
void BdKeptTownGateNote(size_t idx, int gate)
{
    if (idx >= g_townRows.size()) return;
    const townkeep::TownRow& t = g_townRows[idx];
    if (gate == townkeep::kTeamGateTown) { g_townNotMateSaid.erase(t.owner); return; }
    char line[400];
    if (gate == townkeep::kTeamGateWait)
    {
        unsigned pb[3];
        std::memcpy(pb, t.pos, sizeof(pb));
        char wk[64];
        _snprintf_s(wk, sizeof(wk), _TRUNCATE, "%d|%08x%08x%08x", t.owner, pb[0], pb[1], pb[2]);
        if (g_townTeamWaitSaid.size() >= 4096 || !g_townTeamWaitSaid.insert(std::string(wk)).second) return;   /* each outpost once a world */
        ++g_townTeamWait;
        if (g_townTeamWait > 10) return;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] outpost '%.80s' of slot %d waits: this game's team table is not in yet - no town taken or founded for it until it is (townTeamWait=%lld)",
                    t.name.c_str(), t.owner, g_townTeamWait);
        BdKeptLog(line);
        return;
    }
    if (!g_townNotMateSaid.insert(t.owner).second) return;
    ++g_townNotMate;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] slot %d is not this player's teammate - its kept outposts ('%.80s' and any other) get no town here, their pieces stand as laid (owner 938; townNotMate=%lld)",
                t.owner, t.name.c_str(), g_townNotMate);
    BdKeptLog(line);
}
/* the town standing for the outpost of a kept piece in this world, taken or founded now (0 = the piece is of no outpost, or none
   could be had: the piece is laid without one; or owner 938's team gate holds it - not this player's or a teammate's, or the team
   table not in yet - and nothing is settled for it). fac: the piece owner's faction here. */
void* BdKeptTownForKey(const std::string& pieceKey, ::Faction* fac)
{
    std::map<std::string, size_t>::const_iterator k = g_townKeyIdx.find(pieceKey);
    if (k == g_townKeyIdx.end() || fac == 0) return 0;
    const size_t idx = k->second;
    if (idx >= g_townRows.size() || g_townFailed.count(idx) != 0) return 0;
    const int gate = BdKeptTownGate(idx, fac);
    BdKeptTownGateNote(idx, gate);
    if (gate != townkeep::kTeamGateTown) return 0;
    const townkeep::TownRow& t = g_townRows[idx];
    char line[600];
    void* towns[kTownWalkCap];
    const int n = BdPlayerTownsPod(towns, kTownWalkCap);
    if (n < 0)
    {
        g_townFailed.insert(idx);
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] outpost '%.80s' of slot %d: the town list is not readable here (TownList missing from the address table, or the read faulted) - its pieces are laid without it", t.name.c_str(), t.owner);
        ErrorLog(line);
        return 0;
    }
    std::map<size_t, void*>::const_iterator c = g_townLive.find(idx);
    int cachedOk = 0;
    if (c != g_townLive.end())
        for (int i = 0; i < n && cachedOk == 0; ++i)
        {
            float lp[3]; int h[5]; void* f = 0;
            if (towns[i] == c->second && BdTownReadPod(towns[i], lp, h, &f) != 0 && f == (void*)fac) cachedOk = 1;
        }
    void* standing = 0;
    if (cachedOk == 0)
        for (int i = 0; i < n && standing == 0; ++i)
        {
            float lp[3]; int h[5]; void* f = 0;
            if (BdTownReadPod(towns[i], lp, h, &f) != 0 && f == (void*)fac && townkeep::TownStands(lp, t) != 0) standing = towns[i];
        }
    const int step = townkeep::TownLayStep(cachedOk, standing != 0 ? 1 : 0, kBdTownFoundRva != 0 ? 1 : 0);
    if (step == townkeep::kStepCached) return c->second;
    if (step == townkeep::kStepStanding)
    {
        g_townLive[idx] = standing; ++g_townStanding;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] outpost '%.80s' of slot %d: a town of its player's faction already stands at its centre - %s - taken, none founded (standing=%lld)",
                    t.name.c_str(), t.owner, BdTownText(standing).c_str(), g_townStanding);
        BdKeptLog(line);
        return standing;
    }
    if (step == townkeep::kStepNone)
    {
        g_townFailed.insert(idx);
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] outpost '%.80s' of slot %d NOT founded: TownFoundPlayer is not in the address table - its pieces are laid without it", t.name.c_str(), t.owner);
        ErrorLog(line);
        return 0;
    }
    void* town = BdTownFoundPod(t.pos);
    if (town == 0)
    {
        g_townFailed.insert(idx); ++g_townFoundFail;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] outpost '%.80s' of slot %d NOT founded: TownFoundPlayer FAULTED or returned none - its pieces are laid without it (foundFail=%lld)",
                    t.name.c_str(), t.owner, g_townFoundFail);
        ErrorLog(line);
        return 0;
    }
    const std::string name(t.name);
    const int named = name.empty() ? 1 : BdTownSetNamePod(town, &name);
    float lp[3]; int h[5]; void* f0 = 0;
    BdTownReadPod(town, lp, h, &f0);
    const int facOk = (f0 == (void*)fac) ? 1 : BdTownSetFactionPod(town, (void*)fac);
    g_townLive[idx] = town; ++g_townFounded;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] outpost FOUNDED for slot %d: %s (kept '%.80s' at (%.1f, %.1f, %.1f)%s%s) - its %u kept piece(s) are linked to it as they are laid; founded=%lld",
                t.owner, BdTownText(town).c_str(), t.name.c_str(), t.pos[0], t.pos[1], t.pos[2], named != 0 ? "" : "; the name write FAULTED", facOk != 0 ? "" : "; the faction write FAULTED",
                (unsigned)t.keys.size(), g_townFounded);
    BdKeptLog(line);
    return town;
}
/* the note for a kept piece of an outpost standing inside a building: its town is its host's, nothing written. row -1 = a loaded piece */
void BdKeptTownHostNote(long row, const std::string& pieceKey)
{
    ++g_townLinkHost;
    if (g_townLinkHost > 40 && g_townLinkHost % 100 != 0) return;
    char who[40], line[500];
    if (row >= 0) _snprintf_s(who, sizeof(who), _TRUNCATE, "row %ld", row);
    else _snprintf_s(who, sizeof(who), _TRUNCATE, "loaded");
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] %s piece key=%.63s stands inside a building - its town is its host's, nothing written; linkHost=%lld", who, pieceKey.c_str(), g_townLinkHost);
    BdKeptLog(line);
}
/* a laid piece of an outpost linked to its town (BdBuildingSetTownPod: the town's handle words at Building +0x1D8, top-level pieces
   only; a piece standing inside a building keeps its host's town). 1 = linked, already in it (its own handle words are the town's),
   or stands inside a building (nothing to do); 0 = no piece or no town, or the write faulted */
void* BdKeptTownFor(const keptwire::Row& w, ::Faction* fac) { return BdKeptTownForKey(w.p.key, fac); }
int BdKeptTownLinkKey(long row, const std::string& pieceKey, void* b, void* town)
{
    if (b == 0 || town == 0) return 0;
    char who[40];
    if (row >= 0) _snprintf_s(who, sizeof(who), _TRUNCATE, "row %ld", row);
    else _snprintf_s(who, sizeof(who), _TRUNCATE, "loaded");
    float tp[3]; int th[5]; void* tf = 0;
    int bh[5];
    /* already in it: its own handle words (+0x1D8) are the town's - not the engine's lookup, whose nearest-town fallback can name
       this town for a piece whose own words are none or stale */
    if (BdTownReadPod(town, tp, th, &tf) != 0 && BdBuildingTownHandPod(b, bh) != 0 && townkeep::HandSame(bh, th)) return 1;
    void* cur = BdBuildingTownPod(b);
    char line[500];
    const std::string was = cur != 0 ? "'" + BdTownName(cur) + "'" : std::string("none");
    const int set = BdBuildingSetTownPod(b, town);
    if (set == 0)
    {
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] %s piece key=%.63s: linking it to its outpost '%.80s' FAULTED - left as it is", who, pieceKey.c_str(), BdTownName(town).c_str());
        ErrorLog(line);
        return 0;
    }
    if (set < 0) { BdKeptTownHostNote(row, pieceKey); return 1; }
    ++g_townLinked;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] %s piece key=%.63s linked to its outpost '%.80s' (its town was %.90s); linked=%lld", who, pieceKey.c_str(), BdTownName(town).c_str(), was.c_str(), g_townLinked);
    BdKeptLog(line);
    void* now = BdBuildingTownPod(b);   /* read back the engine's way */
    if (now != town)
    {
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] %s piece key=%.63s linked to '%.80s' but the game reads its town as '%.80s'", who, pieceKey.c_str(), BdTownName(town).c_str(),
                    now != 0 ? BdTownName(now).c_str() : "none");
        ErrorLog(line);
    }
    return 1;
}
void BdKeptTownLink(const keptwire::Row& w, void* b, void* town) { BdKeptTownLinkKey((long)w.row, w.p.key, b, town); }
/* a kept piece of an outpost to its town: a piece standing inside a building (BdBuildingTopLevelPod) keeps its host's town and no
   town is taken or founded for it (a town founded for it would have no member, and the engine's player-town refresh destroys such a
   town); otherwise the outpost's town is taken or founded (BdKeptTownForKey) and the piece linked to it, whatever town it stood in
   (on a teammate's game the engine can give a copied piece a world town; a bought or handed-back house is on no outpost's key list -
   the world server keeps it out). row -1 = a loaded piece (BdTownsLoadedTick). 1 = linked, already in its outpost, or stands inside a
   building (nothing to do); 0 = nothing done (no outpost key, no town to take, a fault). MAIN THREAD */
int BdKeptTownPlaceKey(long row, const std::string& pieceKey, void* b, ::Faction* fac)
{
    if (b == 0 || fac == 0 || g_townKeyIdx.find(pieceKey) == g_townKeyIdx.end()) return 0;   /* a piece of no outpost */
    const int top = BdBuildingTopLevelPod(b);
    if (top < 0) return 0;   /* a fault */
    if (top == 0) { BdKeptTownHostNote(row, pieceKey); return 1; }
    return BdKeptTownLinkKey(row, pieceKey, b, BdKeptTownForKey(pieceKey, fac));
}
int BdKeptTownPlace(const keptwire::Row& w, void* b, ::Faction* fac) { return BdKeptTownPlaceKey((long)w.row, w.p.key, b, fac); }
/* EVERY GAME, not only the layer: each kept outpost's pieces loaded here are linked to the outpost's town, taken or founded on this game
   as the lay does (the piece owner's faction: this player's own, else the owner's stand-in, none made; a town of that faction within
   kTownSameRadius of the centre is taken, so none is founded twice; a piece inside a building keeps its host's town), so this game
   shows the outpost and its area records name it. Every kTownLookMs ONE walk of the loaded buildings (LoadedBuildings) is matched
   against the outposts' keys (the exact key, else the one key within the drift window - BoxKeyNearKey), pieces of an outpost owner's
   faction only. A piece the place step settles (linked, already in its outpost, or standing inside a building) is settled for this world (g_townLoadedDone), and no walk runs once every outpost key is settled. A piece on no list that walk reads (inside a building's
   interior) is not looked for here. MAIN THREAD (BuildCopyDrain) */
DWORD g_townLoadedAt = 0;
size_t g_townLoadedCursor = 0;   /* reset with each new town list; the walk reads every loaded piece and keeps no cursor */
std::set<std::string> g_townLoadedDone;   /* MAIN THREAD: outpost keys whose piece was settled here (BdKeptTownPlaceKey non-zero) - not looked for again this world */
/* MAIN THREAD: looks after which an open outpost key's area had its buildings loaded here (ZoneLoadedSectorTri) and the key was still not
   settled. At kTownLoadedMissMax the key is settled too (its piece is gone, inside a building, or cannot be linked), so the 5 s walk stops
   once nothing more can come of it. At most kTownLinksPerLook pieces are linked a look; the rest at the next one. */
std::map<std::string, int> g_townLoadedMiss;
long long g_townLoadedGaveUp = 0;
const int kTownLoadedMissMax = 6;
const int kTownLinksPerLook = 16;
/* the outposts' key a loaded piece's key names: the exact key, else the one key within the drift window (two: none) */
std::map<std::string, size_t>::const_iterator BdTownKeyOf(const std::string& k)
{
    std::map<std::string, size_t>::const_iterator ki = g_townKeyIdx.find(k);
    if (ki != g_townKeyIdx.end()) return ki;
    std::string pre;
    if (!coopbuild::BuildKeyNearPrefix(k, &pre)) return g_townKeyIdx.end();
    std::map<std::string, size_t>::const_iterator hit = g_townKeyIdx.end();
    int n = 0;
    for (std::map<std::string, size_t>::const_iterator c = g_townKeyIdx.lower_bound(pre); c != g_townKeyIdx.end() && c->first.compare(0, pre.size(), pre) == 0; ++c)
        if (BoxKeyNearKey(k.c_str(), c->first.c_str()) != 0) { hit = c; ++n; }
    return (n == 1) ? hit : g_townKeyIdx.end();
}
void BdTownsLoadedTick()
{
    if (g_townRows.empty() || kBdTownListRva == 0 || EngineWritesBlocked()) return;
    const DWORD now = ::GetTickCount();
    if (now - g_townLoadedAt < kTownLookMs) return;
    g_townLoadedAt = now;
    const int mySlot = KeptMySlot();
    if (mySlot < 0 || LocalPlayerFaction() == 0) return;
    /* owner 938: an outpost of an owner neither this player nor a teammate, or with the team table not in yet, is neither walked nor
       counted against (not settled): it is looked at again once the gate lets it through (a join, the table coming in) */
    std::vector<int> gates(g_townRows.size(), townkeep::kTeamGateWait);
    for (size_t r = 0; r < g_townRows.size(); ++r)
    {
        if (g_townFailed.count(r) != 0) continue;
        gates[r] = BdKeptTownGate(r, (g_townRows[r].owner == mySlot) ? LocalPlayerFaction() : 0);
        BdKeptTownGateNote(r, gates[r]);
    }
    size_t open = 0;
    for (std::map<std::string, size_t>::const_iterator ki = g_townKeyIdx.begin(); ki != g_townKeyIdx.end(); ++ki)
        if (ki->second < g_townRows.size() && g_townFailed.count(ki->second) == 0 && g_townLoadedDone.count(ki->first) == 0 && gates[ki->second] == townkeep::kTeamGateTown) ++open;
    if (open == 0) return;   /* every outpost key settled here, or held by the team gate: no walk */
    std::vector< ::Faction*> facs(g_townRows.size(), (::Faction*)0);
    int anyFac = 0;
    for (size_t r = 0; r < g_townRows.size(); ++r)
    {
        if (g_townFailed.count(r) != 0 || gates[r] != townkeep::kTeamGateTown) continue;   /* facs[r] 0: its keys wait, as for an owner with no faction here */
        facs[r] = (g_townRows[r].owner == mySlot) ? LocalPlayerFaction() : OwnerFactionForSlot(g_townRows[r].owner, false);
        if (facs[r] != 0) anyFac = 1;
    }
    if (anyFac == 0) return;
    static void* list[4096];
    int zones = 0, trunc = 0;
    const long long gen0 = BoxWalkGen();   /* taken before the list: a zone deactivation frees what the list names */
    const int n = LoadedBuildings(list, 4096, &zones, &trunc);
    std::set<std::string> tried;   /* the keys a piece was linked for this look */
    int links = 0, capped = 0;
    for (int i = 0; i < n; ++i)
    {
        if (BoxWalkGen() != gen0) return;   /* the rest at the next look */
        void* b = list[i];
        if (b == 0) continue;
        void* own = BdOwnerPod(b);
        if (own == 0) continue;
        int ownerOk = 0;
        for (size_t r = 0; r < facs.size() && ownerOk == 0; ++r) if (facs[r] != 0 && (void*)facs[r] == own) ownerOk = 1;
        if (ownerOk == 0) continue;   /* no outpost owner's piece: no key built */
        char k[kKeyCap];
        k[0] = 0;
        if (ObjectPositionKey(b, k, kKeyCap, "", 3, 1) == 0) continue;
        std::map<std::string, size_t>::const_iterator ki = BdTownKeyOf(std::string(k));
        if (ki == g_townKeyIdx.end()) continue;
        const size_t r = ki->second;
        if (r >= g_townRows.size() || g_townFailed.count(r) != 0 || g_townLoadedDone.count(ki->first) != 0) continue;
        ::Faction* fac = facs[r];
        if (fac == 0 || own != (void*)fac) continue;
        float lp = 0, ln = 0;
        int lc = 0, ld = 0, lx = 0;
        if (!BdReadState(b, &lp, &ln, &lc, &ld, &lx) || ld != 0) continue;   /* unreadable, or being dismantled */
        const std::string key = ki->first;
        if (links >= kTownLinksPerLook) { capped = 1; break; }   /* enough links for one frame: the rest at the next look */
        ++links;
        tried.insert(key);
        if (BdKeptTownPlaceKey(-1, key, b, fac) != 0) g_townLoadedDone.insert(key);   /* linked, already in it, or inside a building: settled for this world */
    }
    /* a look counts against an open key only when its area's buildings are all loaded here and the look covered it: a capped look covers
       the keys it linked, a whole look (no cap, an untruncated list) every key. Keys of an owner with no faction here wait for it. */
    for (std::map<std::string, size_t>::const_iterator ki = g_townKeyIdx.begin(); ki != g_townKeyIdx.end(); ++ki)
    {
        if (ki->second >= g_townRows.size() || g_townFailed.count(ki->second) != 0 || g_townLoadedDone.count(ki->first) != 0) continue;
        if (facs[ki->second] == 0) continue;
        if (capped != 0 ? tried.count(ki->first) == 0 : trunc != 0) continue;
        int sx = -1, sy = -1;
        if (BoxKeySector(ki->first.c_str(), &sx, &sy) == 0 || ZoneLoadedSectorTri(sx, sy) != 1) continue;   /* its area is not loaded here: its piece may still come */
        int& miss = g_townLoadedMiss[ki->first];
        ++miss;
        if (miss < kTownLoadedMissMax) continue;
        g_townLoadedDone.insert(ki->first);
        ++g_townLoadedGaveUp;
        if (g_townLoadedGaveUp <= 40 || g_townLoadedGaveUp % 100 == 0)
        {
            char line[400];
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] outpost key=%.63s: its area's buildings are loaded here and no piece at it was linked or settled after %d looks (gone, inside a building's interior, or not linkable) - not looked for again this world; townLoadedGaveUp=%lld",
                        ki->first.c_str(), kTownLoadedMissMax, g_townLoadedGaveUp);
            BdKeptLog(line);
        }
        g_townLoadedMiss.erase(ki->first);
    }
}
/* the piece owner's faction here for a kept row: this player's own, else the owner's stand-in (none made) */
::Faction* BdKeptTownFaction(const keptwire::Row& w, int mySlot)
{
    return (w.p.ownerSlot == mySlot) ? LocalPlayerFaction() : OwnerFactionForSlot(w.p.ownerSlot, false);
}
/* THE KEYS OF EACH TOWN: this player's registered pieces whose town (the engine's BuildingGetTown, BdBuildingTownPod) is one of this player's towns -> their
   registry keys' wire form under that town (sorted, each once, at most kMaxTownKeys; past it, or a key with no wire form, the town's
   list is cut). Walked every kTownKeysWalkMs and only while this player has a town: ONE walk of the loaded buildings (LoadedBuildings),
   each piece this player's faction owns keyed and matched to its registry row (the exact key, else the one row key within the drift
   window). Furniture is not listed: it sits on its host's layout, not on the zone lists this walk reads.
   g_townKeysAnyUnread: an own standing registered piece (not furniture) the walk did not see sits in an area whose buildings are not
   loaded here (ZoneLoadedSectorTri, by its key's sector), or the walk could not list every loaded building - its town is not known, so
   every outpost's list is said as cut and the world server keeps the keys it has. An own piece in a loaded area that did not key or
   no longer stands is left out of the lists. MAIN THREAD */
struct BdTownKeys { std::vector<std::string> keys; int cut; BdTownKeys() : cut(0) {} };
std::map<void*, BdTownKeys> g_townKeysOf;
int g_townKeysAnyUnread = 1;
bool g_townKeysWalked = false;
DWORD g_townKeysAt = 0;
const DWORD kTownKeysWalkMs = 20000;
/* a SAY is marked read whole only once this game has been looking at the world it loaded (StoreWorldGenNow) for kTownWholeAfterMs: a
   town list read just after a load may not hold every town yet, and the world server clears a player's outposts on empty SAYs read whole */
long g_townWorldGen = -1;
DWORD g_townWorldAt = 0;
const DWORD kTownWholeAfterMs = 120000;
/* the registry row a loaded piece's key names: the exact key, else the one row key within the drift window (two: none) */
std::map<std::string, BdReg>::const_iterator BdTownRegRowOf(const std::string& k)
{
    std::map<std::string, BdReg>::const_iterator ri = g_reg.find(k);
    if (ri != g_reg.end()) return ri;
    ri = BdRowByLiveKey(k);   /* a copy's own created key -> the builder's row (g_liveAlias), as the box road asks */
    if (ri != g_reg.end()) return ri;
    std::string pre;
    if (!coopbuild::BuildKeyNearPrefix(k, &pre)) return g_reg.end();
    std::map<std::string, BdReg>::const_iterator hit = g_reg.end();
    int n = 0;
    for (std::map<std::string, BdReg>::const_iterator c = g_reg.lower_bound(pre); c != g_reg.end() && c->first.compare(0, pre.size(), pre) == 0; ++c)
        if (BoxKeyNearKey(k.c_str(), c->first.c_str()) != 0) { hit = c; ++n; }
    return (n == 1) ? hit : g_reg.end();
}
void BdTownKeysWalk(DWORD now, const std::set<void*>& myTowns)
{
    if (g_townKeysWalked && now - g_townKeysAt < kTownKeysWalkMs) return;
    g_townKeysWalked = true;
    g_townKeysAt = now;
    ::Faction* mine = LocalPlayerFaction();
    if (mine == 0) return;
    static void* list[4096];
    int zones = 0, trunc = 0;
    const long long gen0 = BoxWalkGen();   /* taken before the list: a zone deactivation frees what the list names */
    const int n = LoadedBuildings(list, 4096, &zones, &trunc);
    std::map<void*, BdTownKeys> by;
    for (std::set<void*>::const_iterator t = myTowns.begin(); t != myTowns.end(); ++t) by[*t];   /* a town walked with no piece seen has an empty list */
    std::set<std::string> seen;   /* the registry keys of own pieces this walk saw */
    int unread = (n < 0 || trunc != 0) ? 1 : 0;   /* the list could not name every loaded building */
    for (int i = 0; i < n; ++i)
    {
        if (BoxWalkGen() != gen0)
        {   /* the world moved under the list: walked again at the next look; the lists are said as cut until then */
            g_townKeysWalked = false;
            g_townKeysAnyUnread = 1;
            return;
        }
        void* b = list[i];
        if (b == 0 || BdOwnerPod(b) != (void*)mine) continue;
        char k[kKeyCap];
        k[0] = 0;
        if (ObjectPositionKey(b, k, kKeyCap, "", 3, 1) == 0) continue;
        std::map<std::string, BdReg>::const_iterator r = BdTownRegRowOf(std::string(k));
        if (r == g_reg.end()) continue;   /* not a registered piece */
        seen.insert(r->first);
        void* town = BdBuildingTownPod(b);
        if (town == 0 || myTowns.count(town) == 0) continue;
        BdTownKeys& tk = by[town];
        char w[poskey::kKeyCap];
        if (poskey::WireForm(r->first.c_str(), w, (int)sizeof(w)) == 0) { tk.cut = 1; continue; }
        tk.keys.push_back(std::string(w));
    }
    if (unread == 0)
    {   /* an own standing piece the walk did not see: cut only when its area's buildings are not all loaded here (one engine read a sector) */
        signed char zl[4096];
        std::memset(zl, -2, sizeof(zl));
        for (std::map<std::string, BdReg>::const_iterator r = g_reg.begin(); r != g_reg.end() && unread == 0; ++r)
        {
            const BdReg& g = r->second;
            if (g.own == 0 || g.via == 3 || g.removed != 0 || g.dismantled != 0 || g.destroyed != 0 || !g.hostKey.empty()) continue;
            if (seen.count(r->first) != 0) continue;
            int sx = -1, sy = -1;
            const int c = (BoxKeySector(r->first.c_str(), &sx, &sy) != 0) ? coopbuild::BuildZoneSectorIndex(sx, sy) : -1;
            if (c < 0 || c >= (int)sizeof(zl)) { unread = 1; continue; }   /* its area cannot be named */
            if (zl[c] == -2) zl[c] = (signed char)ZoneLoadedSectorTri(sx, sy);
            if (zl[c] != 1) unread = 1;   /* not loaded here (or not askable): the walk could not see it */
        }
    }
    for (std::map<void*, BdTownKeys>::iterator i = by.begin(); i != by.end(); ++i)
    {
        std::vector<std::string>& v = i->second.keys;
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
        if (v.size() > townkeep::kMaxTownKeys) { v.resize(townkeep::kMaxTownKeys); i->second.cut = 1; }
    }
    g_townKeysOf.swap(by);
    g_townKeysAnyUnread = unread;
}
/* SAY: this game's own outposts to the world server, each with the keys of its pieces (looked at every kTownLookMs; sent when changed,
   on a new slot, and every kTownSayAgainMs; split over several messages when it would pass one) */
void BdTownsSayTick()
{
    const DWORD now = ::GetTickCount();
    if (now - g_townLookAt < kTownLookMs) return;
    g_townLookAt = now;
    const int slot = KeptMySlot();
    if (slot < 0 || kBdTownListRva == 0 || EngineWritesBlocked()) return;
    ::Faction* mine = LocalPlayerFaction();
    if (mine == 0) return;
    void* towns[kTownWalkCap];
    const int n = BdPlayerTownsPod(towns, kTownWalkCap);
    if (n < 0) return;
    const long worldGen = StoreWorldGenNow();
    if (worldGen != g_townWorldGen) { g_townWorldGen = worldGen; g_townWorldAt = now; }
    std::vector<townkeep::TownRow> rows;
    std::vector<void*> rowTown;   /* each row's town */
    int whole = (n < kTownWalkCap && now - g_townWorldAt >= kTownWholeAfterMs) ? 1 : 0;   /* a walk that reached its cap may have left towns out */
    for (int i = 0; i < n; ++i)
    {
        townkeep::TownRow t;
        void* f = 0;
        if (BdTownReadPod(towns[i], t.pos, t.hand, &f) == 0) { whole = 0; continue; }   /* unread: it may be this player's */
        if (f != (void*)mine) continue;
        if (rows.size() >= townkeep::kMaxTownsPerSlot) { whole = 0; continue; }
        t.name = townkeep::NameFit(BdTownName(towns[i]));
        t.level = BdTownLevelPod(towns[i]);   /* T-811: the size level - a teammate's game draws this outpost with the icon of it */
        rows.push_back(t);
        rowTown.push_back(towns[i]);
    }
    if (rowTown.empty())
    {   /* this player has no town: no walk; the first town found is walked for at once */
        g_townKeysOf.clear();
        g_townKeysAnyUnread = 1;
        g_townKeysWalked = false;
    }
    else BdTownKeysWalk(now, std::set<void*>(rowTown.begin(), rowTown.end()));
    std::vector<townkeep::TownRow> sayRows;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        townkeep::TownRow& t = rows[i];
        std::map<void*, BdTownKeys>::const_iterator k = g_townKeysOf.find(rowTown[i]);
        if (k != g_townKeysOf.end()) { t.keys = k->second.keys; t.cut = k->second.cut; }
        if (k == g_townKeysOf.end() || g_townKeysAnyUnread != 0) t.cut = 1;   /* a town not walked yet, or pieces the walk could not see: the world server keeps the keys it has */
        if (townkeep::RowOk(t, townkeep::kOpSay)) sayRows.push_back(t);
        else whole = 0;
    }
    rows.swap(sayRows);
    std::vector<std::vector<char> > msgs;
    if (!townkeep::EncodeTownsSay(&msgs, rows, whole)) return;
    std::vector<char> b;
    for (size_t m = 0; m < msgs.size(); ++m) b.insert(b.end(), msgs[m].begin(), msgs[m].end());
    const bool same = (b == g_townSaid && slot == g_townSaidSlot);
    if (same && now - g_townSaidAt < kTownSayAgainMs) return;
    for (size_t m = 0; m < msgs.size(); ++m) if (!TownsSendUp(msgs[m])) return;   /* the link went: the whole SAY goes again at the next look */
    g_townSaid = b; g_townSaidSlot = slot; g_townSaidAt = now; ++g_townSays;
    if (same) return;   /* the same outposts again: no line */
    std::string names;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        char t[260];
        _snprintf_s(t, sizeof(t), _TRUNCATE, "%s'%.80s' at (%.1f, %.1f, %.1f) handle %d/%d/%d/%d/%d, %u key(s)%s", i == 0 ? "" : ", ", rows[i].name.c_str(), rows[i].pos[0], rows[i].pos[1], rows[i].pos[2],
                    rows[i].hand[0], rows[i].hand[1], rows[i].hand[2], rows[i].hand[3], rows[i].hand[4], (unsigned)rows[i].keys.size(), rows[i].cut != 0 ? " (list cut)" : "");
        names += t;
    }
    DebugLog("[KEPT] outposts said to the world server: " + zonekeep::ZkNum((long long)rows.size()) + " of this player's" + std::string(whole != 0 ? "" : " (its town list not read whole)")
             + " in " + zonekeep::ZkNum((long long)msgs.size()) + " message(s)" + (names.empty() ? std::string() : ": " + names));
}
/* ---- T-811 TEAMMATES' BASES ON THE MAP (src/common/baseshow.h). MAIN THREAD (the session dispatch, BuildCopyDrain, the saveGame
   pre-hook). The world server sends the outposts of this player's teammates (SHOW). Each is shown by a player town of the owner's
   stand-in faction: one of that faction standing at the outpost's centre is taken, else one is founded the engine's way
   (TownFoundPlayer - the engine marks the town it founds as found (Town+0x180), so the map lists it; no other town's found mark is
   touched), given the stand-in faction and named "<outpost name> (<player name>)". The map draws the engine's own marker for it
   (MapScreen 0x490C70 lists the found towns; the marker's icon is Town vt+0x378 0x48CEE0). A town is named by its handle words
   (Town+0x60) between drains, never by its address. A base whose row went (the list came whole) is taken out of the world the
   engine's way (TownDestroy: TownList 0x92A4F0, the call the town merge 0x930970 ends with) when no loaded piece names it as its
   town and its model-piece store is empty (baseshow::StoreKeep - the merge moves the store before it destroys, 0x930510; the mod
   re-towns by words only, so it waits for the models to be torn down); one that cannot be yet stays tracked and is tried again every
   kShowRetryMs. When the engine has been asked for a save
   (SaveManager+0xA0), the bases founded here are taken out at this same safe point before it runs and founded again after it.
   The mod knows its own bases by the town itself (baseshow::LeftoverMark), so one a save still held is taken up after a reload.
   Applied at the drain after a SHOW came, a world loaded, the kept list came, a save ran, or a retry is due. ---- */
static unsigned long long kBdTownDestroyRva = 0; static AddrReg kBdTownDestroyRva_reg("TownDestroy", &kBdTownDestroyRva);   /* Steam_1.0.65 0x92A4F0: TownList: destroy one town */
typedef void (*BdTownDestroyFn)(void* list, void* town);
std::vector<baseshow::ShowRow> g_showRows;   /* the teammates' outposts the world server last sent */
unsigned g_showFlags = 0;
std::vector<baseshow::Shown> g_shown;        /* each base shown in this world: owner, centre, its town's handle words */
int g_showDirty = 0;                          /* 1 = applied at the next drain that can */
long g_showWorldGen = -1;                     /* StoreWorldGenNow() g_shown belongs to: another world drops it */
long long g_showIn = 0, g_showBad = 0, g_showFounded = 0, g_showTaken = 0, g_showRenamed = 0, g_showRemoved = 0, g_showLeft = 0, g_showFoundFail = 0;
long long g_showNoFaction = 0, g_showSaveOut = 0, g_showSaveOffThread = 0;
long long g_townKeepStore = 0;                       /* P47 store gate: take-outs refused while the town's model-piece store held pieces */
std::vector<baseshow::StoreLogged> g_townKeepLogged;  /* the towns a store-gate refusal was logged for (this world) */
const size_t kTownKeepLogCap = 256;
/* SHOW: the list replaces the one held (MAIN THREAD, the session dispatch) */
void BuildShowNote(const char* p, size_t n)
{
    unsigned flags = 0;
    std::vector<baseshow::ShowRow> rows;
    char line[300];
    if (baseshow::Decode(p, n, &flags, &rows) == 0)
    {
        ++g_showBad;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[SHOW] a SHOW message from the world server could not be read (%u bytes) - ignored; showBad=%lld", (unsigned)n, g_showBad);
        ErrorLog(line);
        return;
    }
    g_showRows.swap(rows);
    g_showFlags = flags;
    g_showDirty = 1;
    ++g_showIn;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[SHOW] teammates' outposts from the world server: %u row(s)%s; showIn=%lld", (unsigned)g_showRows.size(),
                (flags & baseshow::kFlagCut) != 0 ? " (list cut)" : "", g_showIn);
    DebugLog(line);
}
/* TownDestroy(TownList*, Town*): 1 = called, 0 = missing from the table or faulted */
int BdTownDestroyPod(void* t)
{
    void* tl = BdTownListPtr();
    if (tl == 0 || t == 0 || kBdTownDestroyRva == 0) return 0;
    __try { ((BdTownDestroyFn)(uintptr_t)AddrAbs(kBdTownDestroyRva))(tl, t); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* P47 STORE GATE: a town's model-piece store (Town+0x250, decomp 0x55FF50 / 0x92CFF0, 1.0.65 read): the count of its table keyed by
   building (+0x28) and of its sign-highlight pair list (+0x90), both 64-bit. 1 = read, 0 = no town or a fault (baseshow::StoreKeep
   keeps a town whose store does not read) */
const size_t kTownStore = 0x250, kTownStoreKeys = 0x28, kTownStoreSigns = 0x90;
int BdTownStoreReadPod(void* t, unsigned long long* keys, unsigned long long* signs)
{
    *keys = 0; *signs = 0;
    if (t == 0) return 0;
    __try
    {
        const char* st = (const char*)t + kTownStore;
        *keys = *(const unsigned long long*)(st + kTownStoreKeys);
        *signs = *(const unsigned long long*)(st + kTownStoreSigns);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *keys = 0; *signs = 0; return 0; }
}
/* the player town whose handle words are `hand` (0 = none standing, or no handle) */
void* BdShowTownByHand(const int* hand)
{
    if (!townkeep::HandKnown(hand)) return 0;
    void* towns[kTownWalkCap];
    const int n = BdPlayerTownsPod(towns, kTownWalkCap);
    for (int i = 0; i < n; ++i)
    {
        float lp[3]; int h[5]; void* f = 0;
        if (BdTownReadPod(towns[i], lp, h, &f) != 0 && townkeep::HandSame(h, hand)) return towns[i];
    }
    return 0;
}
/* ---- T-936 (owner decision 936, 2026-10-17): A NEWLY FOUNDED PLAYER OUTPOST IS WOKEN AT ONCE, on every game (src/common/townwake.h).
   TownFoundPlayer makes the town asleep (+0x1AC = 0) and never wakes it, so its power grid ran only after its area unloaded and loaded
   again (T1338). The founding is hooked - the game's own founding and the mod's three (the outposttest lever, the kept-outposts relink,
   a teammate's outpost copied here) all go through it - and the new town is QUEUED by its handle words (and the address the founding
   returned, only ever compared with the player-town list). BdTownWake (MAIN THREAD, BuildCopyDrain - the K2 safe point) wakes it with
   the town's own wake-up, vt+0x228 (Steam_1.0.65 0x92CCE0: this, the ZoneMap of its area), once its area is loaded here and the ZoneMap
   the mod located names that area in its own +0x18 / +0x1C words; awake already = not called; 60 s without its area = dropped (the
   game wakes it itself on the area's load). ---- */
typedef void (*BdTownWakeFn)(void* town, void* zone);
const int kVtTownWake = 0x228;
BdTownFoundFn orig_townFound = 0;
int g_hkTownFound = 0;   /* 1 installed, -1 failed, -2 no address row */
townwake::Entry g_twQ[townwake::kQueueCap];
int g_twN = 0;   /* under g_twCs */
townwake::Entry g_twTake[townwake::kQueueCap];   /* MAIN THREAD: the drain's copy */
CRITICAL_SECTION g_twCs;
volatile LONG g_twInit = 0;
volatile long long g_twQueued = 0, g_twDropped = 0, g_twOffMain = 0;   /* ANY THREAD (Interlocked) */
long long g_twWoken = 0, g_twNotLoaded = 0, g_twAreaMismatch = 0, g_twFaulted = 0, g_twAlready = 0, g_twBusy = 0, g_twLines = 0;   /* MAIN THREAD */
DWORD g_twLookAt = 0;
const long long kTwLineCap = 40;
/* the town's handle words (+0x60), memory only (no engine call): 1 read, 0 a fault */
int BdTownHandPod(void* t, int* hand)
{
    __try { std::memcpy(hand, (const char*)t + kTownHandWords, 20); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* review fold S1: the wake-up's own address row (TownWake: Steam_1.0.65 0x92CCE0, Steam_1.0.68 0x92DBB0 - both read from the exes
   2026-10-17, the slot +0x228 of the Town / TownBase / Nest vtables in both, through a one-jump thunk 0x51064 / 0x510A0). The town's
   slot is followed through at most one 5-byte jump and must land on the row's address, or nothing is called. */
static unsigned long long kBdTownWakeRva = 0; static AddrReg kBdTownWakeRva_reg("TownWake", &kBdTownWakeRva);
long long g_twSlotMismatch = 0;   /* MAIN THREAD: the town's slot did not lead to the TownWake row */
/* the town's wake-up (vt+0x228) with the ZoneMap of its area: 1 called, 0 a fault (or nothing to call with), -1 the slot is not TownWake */
int BdTownWakePod(void* t, void* zone)
{
    if (t == 0 || zone == 0 || kBdTownWakeRva == 0) return 0;
    __try
    {
        const unsigned char* f = (const unsigned char*)((*(void***)t)[kVtTownWake / 8]);
        if (f != 0 && f[0] == 0xE9) f = f + 5 + *(const int*)(f + 1);   /* the thunk's jump */
        if ((uintptr_t)f != (uintptr_t)AddrAbs(kBdTownWakeRva)) return -1;
        ((BdTownWakeFn)f)(t, zone);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* ANY THREAD (the founding's caller): the new town queued for BdTownWake. A worker waits for the lock only behind another founding
   or the main thread's short copy; a full queue drops the town (counted). PODs only. */
void BdTownWakeQueue(void* t)
{
    townwake::Entry e;
    std::memset(&e, 0, sizeof e);
    if (BdTownHandPod(t, e.hand) == 0) std::memset(e.hand, 0, sizeof e.hand);
    e.ptr = (unsigned long long)(uintptr_t)t;
    e.queuedAt = (unsigned)::GetTickCount();
    e.gen = (long)::InterlockedCompareExchange((volatile LONG*)&g_bdGen, 0, 0);
    e.waited = 0;
    if (BdOnMain() == 0) ::InterlockedIncrement64(&g_twOffMain);
    if (::InterlockedCompareExchange(&g_twInit, 0, 0) == 0) { ::InterlockedIncrement64(&g_twDropped); return; }
    ::EnterCriticalSection(&g_twCs);
    const int r = townwake::Push(g_twQ, &g_twN, townwake::kQueueCap, e);
    ::LeaveCriticalSection(&g_twCs);
    if (r == townwake::kPushQueued) ::InterlockedIncrement64(&g_twQueued);
    else if (r == townwake::kPushFull) ::InterlockedIncrement64(&g_twDropped);
}
/* TownFoundPlayer(TownList*, const Vector3*): called through; a new town is only queued - never woken inside the engine's founding */
void* detour_townFound(void* list, const float* pos)
{
    void* t = orig_townFound(list, pos);
    if (t != 0) BdTownWakeQueue(t);
    return t;
}
/* InstallBuild: the founding hook (the TownFoundPlayer row: Steam_1.0.65 0x933F40, 1.0.68 0x934E10) */
int BdInstallTownWake(uintptr_t base)
{
    ::InitializeCriticalSection(&g_twCs);   /* before the hook: the detour queues under it */
    ::InterlockedExchange(&g_twInit, 1);
    g_hkTownFound = BdHook(base, kBdTownFoundRva, (void*)&detour_townFound, (void**)&orig_townFound);
    char at[40];
    _snprintf_s(at, sizeof(at), _TRUNCATE, "0x%llX ", kBdTownFoundRva);
    const std::string msg = std::string("[BUILD] T-936 town-wake hook: TownFoundPlayer ") + at + HkWord(g_hkTownFound)
        + " (a newly founded player outpost is woken at the next safe point once its area is loaded here, as the game wakes a town on an area load)";
    if (g_hkTownFound == 1) DebugLog(msg);
    else ErrorLog(msg);
    return g_hkTownFound;
}
/* MAIN THREAD (BuildCopyDrain, the K2 safe point; engine writes not blocked): each queued town looked at (townwake::Verdict) - woken,
   kept for a later look, or taken out with a count. The queue is copied out under a TRIED lock (a busy lock waits for the next look)
   and the towns kept are pushed back, so no founding waits behind the engine's wake-up. */
void BdTownWake()
{
    if (::InterlockedCompareExchange(&g_twInit, 0, 0) == 0) return;
    const DWORD now = ::GetTickCount();
    if (g_twLookAt != 0 && (DWORD)(now - g_twLookAt) < townwake::kLookGapMs) return;
    if (EngineWritesBlocked()) return;   /* looked at again at the next drain */
    if (::TryEnterCriticalSection(&g_twCs) == 0) { ++g_twBusy; return; }
    const int n = g_twN;
    if (n > 0) std::memcpy(g_twTake, g_twQ, sizeof(townwake::Entry) * (size_t)n);
    g_twN = 0;
    ::LeaveCriticalSection(&g_twCs);
    if (n <= 0) return;
    g_twLookAt = now;
    void* towns[kTownWalkCap];
    const int tn = BdPlayerTownsPod(towns, kTownWalkCap);
    for (int i = 0; i < n; ++i)
    {
        townwake::Entry& e = g_twTake[i];
        void* t = 0;
        float p[3] = { 0.0f, 0.0f, 0.0f };
        for (int k = 0; k < tn && t == 0; ++k)
        {
            float lp[3]; int h[5]; void* f = 0;
            const int rd = BdTownReadPod(towns[k], lp, h, &f);
            const bool same = townwake::HandKnown(e.hand) ? (rd != 0 && townwake::HandSame(h, e.hand)) : ((unsigned long long)(uintptr_t)towns[k] == e.ptr);
            if (same && rd != 0) { t = towns[k]; p[0] = lp[0]; p[1] = lp[1]; p[2] = lp[2]; if (!townwake::HandKnown(e.hand)) std::memcpy(e.hand, h, sizeof e.hand); }
        }
        const unsigned age = (unsigned)(now - (DWORD)e.queuedAt);
        int active = 0, inList = -1;
        unsigned cnt = 0;
        if (t != 0 && BdTownActivePod(t, &active, &inList, &cnt) == 0)
        {
            ++g_twFaulted;   /* its +0x1AC did not read: not called */
            continue;
        }
        int sx = -1, sy = -1, loaded = 0, areaOk = 0;
        void* zone = 0;
        const int posOk = (t != 0) ? townwake::SectorOfXZ(p[0], p[2], &sx, &sy) : 0;
        if (t != 0 && active == 0 && posOk != 0)
        {
            loaded = ZoneLoadedSectorTri(sx, sy);
            if (loaded == 1)
            {
                int rx = -1, ry = -1;
                const int rd = ZoneMapOfSectorPod(sx, sy, &zone, &rx, &ry);
                areaOk = townwake::AreaRecordMatches(rd, sx, sy, rx, ry);
                if (areaOk == 0 && g_twLines < kTwLineCap)
                {
                    ++g_twLines;
                    char line[400];
                    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] town wake REFUSED: '%.80s' sector %d,%d - the area record located for it reads %s %d,%d (not this area) - not called; townWakeAreaMismatch=%lld",
                                BdTownName(t).c_str(), sx, sy, rd == 1 ? "area" : "UNREAD", rx, ry, g_twAreaMismatch + 1);
                    ErrorLog(line);
                }
            }
        }
        const int v = townwake::Verdict(t != 0 ? 1 : 0, posOk, active, loaded, areaOk, age, e.gen == g_bdGen ? 1 : 0);
        if (v == townwake::kWaitArea && e.waited == 0) { e.waited = 1; ++g_twNotLoaded; }
        if (townwake::TakesOut(v) == 0)
        {
            ::EnterCriticalSection(&g_twCs);   /* main thread: held only by a founding's short push */
            const int r = townwake::Push(g_twQ, &g_twN, townwake::kQueueCap, e);
            ::LeaveCriticalSection(&g_twCs);
            if (r == townwake::kPushFull) ::InterlockedIncrement64(&g_twDropped);
            continue;
        }
        if (v == townwake::kAlready) { ++g_twAlready; continue; }
        if (v == townwake::kAreaMismatch) { ++g_twAreaMismatch; continue; }
        if (v != townwake::kWake) { ::InterlockedIncrement64(&g_twDropped); continue; }   /* timeout, another world, off the grid */
        const int wr = BdTownWakePod(t, zone);
        if (wr < 0) { ++g_twSlotMismatch; continue; }
        if (wr == 0) { ++g_twFaulted; continue; }
        ++g_twWoken;
        if (g_twLines < kTwLineCap)
        {
            ++g_twLines;
            int a1 = -1, il = -1; unsigned c1 = 0;
            if (BdTownActivePod(t, &a1, &il, &c1) == 0) a1 = -1;
            char line[400];
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] town woken: '%.80s' sector %d,%d (founded this session) active 0->%d after %u ms; townWoken=%lld",
                        BdTownName(t).c_str(), sx, sy, a1, age, g_twWoken);
            DebugLog(std::string(line));
        }
    }
}
/* the loaded top-level pieces linked to town t: their own town handle words (+0x1D8) are t's - memory reads only, not the engine's
   lookup (its nearest-town fallback would count a piece linked to no town); *unread = 1 when the loaded pieces, or t's handle words,
   could not all be read */
int BdShowLinked(void* t, int* unread)
{
    static void* list[4096];
    int zones = 0, trunc = 0;
    const int n = LoadedBuildings(list, 4096, &zones, &trunc);
    *unread = (n < 0 || trunc != 0) ? 1 : 0;
    float tp[3]; int th[5]; void* tf = 0;
    if (BdTownReadPod(t, tp, th, &tf) == 0) { *unread = 1; return 0; }
    int k = 0;
    for (int i = 0; i < n; ++i)
    {
        if (list[i] == 0) continue;
        int bh[5];
        const int top = BdBuildingTopLevelPod(list[i]);
        if (top < 0 || (top == 1 && BdBuildingTownHandPod(list[i], bh) == 0)) { *unread = 1; continue; }
        if (top == 1 && townkeep::HandSame(bh, th)) ++k;
    }
    return k;
}
/* a shown base taken out of this world (baseshow::GoneStep); 1 = no town of it stands now (its handle words cleared) */
int BdShowTakeOut(baseshow::Shown* s, const char* why)
{
    void* t = BdShowTownByHand(s->hand);
    if (t == 0) { for (int i = 0; i < 5; ++i) s->hand[i] = 0; return 1; }
    int unread = 0;
    const int linked = s->ours != 0 ? BdShowLinked(t, &unread) : 0;
    char line[400];
    if (baseshow::GoneStep(s->ours, linked, unread, kBdTownDestroyRva != 0 ? 1 : 0) != baseshow::kGoneDestroy)
    {
        ++g_showLeft;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[SHOW] base of slot %d (handle %d/%d/%d/%d/%d) left standing (%s): %s; showLeft=%lld", s->owner, s->hand[0], s->hand[1], s->hand[2],
                    s->hand[3], s->hand[4], why, s->ours == 0 ? "a town that stood already, not founded here" : unread != 0 ? "the loaded pieces could not all be read"
                    : linked != 0 ? "a loaded piece names it as its town" : "TownDestroy is not in the address table", g_showLeft);
        DebugLog(line);
        return 0;
    }
    /* P47 store gate: a town whose model-piece store still holds pieces (a copy re-towned by its words keeps its pieces there until
       its models are torn down) is not destroyed - TownDestroy visits no building, so a live building would point into a freed town.
       Kept and tried again: the row-went path every kShowRetryMs, the save path at the apply after the save ran (or, the save holding
       it, taken up after a reload by its mark). The store empties when the area's models are torn down (the area unloads). */
    unsigned long long storeKeys = 0, storeSigns = 0;
    const int storeRead = BdTownStoreReadPod(t, &storeKeys, &storeSigns);
    if (baseshow::StoreKeep(storeRead, storeKeys, storeSigns) != 0)
    {
        ++g_showLeft;
        ++g_townKeepStore;
        if (baseshow::StoreLogOnce(&g_townKeepLogged, s->hand, kTownKeepLogCap) != 0)
        {
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[SHOW] base of slot %d (handle %d/%d/%d/%d/%d) left standing (%s): its model-piece store %s (pieces %llu, sign pairs %llu) -"
                        " tried again once the area's models are torn down; townKeepStore=%lld", s->owner, s->hand[0], s->hand[1], s->hand[2], s->hand[3], s->hand[4], why,
                        storeRead != 0 ? "still holds pieces" : "did not read", storeKeys, storeSigns, g_townKeepStore);
            DebugLog(line);
        }
        return 0;
    }
    if (BdTownDestroyPod(t) == 0)
    {
        ++g_showLeft;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[SHOW] base of slot %d (handle %d/%d/%d/%d/%d) NOT taken out (%s): TownDestroy FAULTED; showLeft=%lld", s->owner, s->hand[0], s->hand[1],
                    s->hand[2], s->hand[3], s->hand[4], why, g_showLeft);
        ErrorLog(line);
        return 0;
    }
    for (int i = 0; i < 5; ++i) s->hand[i] = 0;
    ++g_showRemoved;
    return 1;
}
/* T-811 review fold: the save request, the retry clock and the leftovers' take-up */
DWORD g_showRetryAt = 0;    /* 0 = no base waiting to be taken out; else the tick it is tried again */
long g_showKeptSeen = -2;   /* g_townRowsGen the last apply saw: a kept list coming in applies again */
int g_showSaveHeld = 0;     /* 1 = the bases founded here were taken out for a save the engine was asked for: none is founded until it ran */
long long g_showTakenUp = 0, g_showSavedIn = 0;
/* 1 = the engine has a save (or load) request waiting (SaveManager+0xA0) */
int BdShowSavePending()
{
    void* sm = OwnSaveManagerPtr();
    return (sm != 0 && StoreSaveRequestCode(sm) > 0) ? 1 : 0;
}
bool BdShowTracked(const int* h)
{
    for (size_t i = 0; i < g_shown.size(); ++i) if (townkeep::HandKnown(g_shown[i].hand) && townkeep::HandSame(g_shown[i].hand, h)) return true;
    return false;
}
/* ---- THE COPIES OF A TEAMMATE'S OUTPOST IN ITS SHOWN TOWN (owner 936 / 937, 2026-10-17; world-server protocol 101) ----
   The world server follows each SHOW with TOWNS SHOW KEYS: per shown row, the said keys of the pieces linked to that outpost on its
   owner's game (townkeep.h kOpShowKeys, baseshow.h KeyRowsFor). Every kTownLookMs (and at the next drain after a SHOW KEYS, a base
   founded or taken up, or a copy made) ONE walk of the loaded buildings: each copy of a teammate's piece here (a registry row this
   game did not place, own == 0, owned by that slot's stand-in) whose key's wire form is on the key row of a base shown for its owner is
   linked to that base's town (BdBuildingSetTownPod: a switched-on power piece is switched off and on, so it leaves its old grid and
   joins the town's); a copy standing inside a building keeps its host's town. A copy in a shown town whose key is on none of that
   base's keys, while its key row is not cut and said that key earlier this world (or the base's row went), is put back - one the
   engine put there at creation whose key the row never said is left (T1342; baseshow::SeenNote / UnlinkOk, counted engineLeft) - (BdShowPutBackCopy) in the town building creation
   0x57C1E0 would choose for its spot with the going town and every shown town left out (baseshow::PutBackPick; none holds the spot:
   the wilderness town). Before a base founded here is taken out (its row went, or a save was asked for) its copies are put back the
   same way (BdShowUnlinkTown), and no look links while a save is on its way, so a save holds no copy linked to a base founded here.
   Nothing is remembered but the keys each row said this world (g_showKeysSeen, empty after a reload: a copy met in a shown town
   after a reload whose key is not on its row is left until that row says it and drops it again). Nothing is settled: each look
   checks every copy again. MAIN THREAD (BuildCopyDrain). */
std::vector<townkeep::TownRow> g_showKeyRows;   /* the last whole SHOW KEYS list */
std::vector<baseshow::SeenRow> g_showKeysSeen;  /* T1342: per key row the keys it said this world (baseshow::SeenNote; BuildForgetWorld clears) */
townkeep::ShowKeysAssembly g_showKeysAsm;
int g_showLinkDue = 0;                          /* declared in namespace coop above the unnamed namespace (the copy-created mark) */
DWORD g_showLinkAt = 0;
long long g_showKeysIn = 0, g_showKeysBad = 0;
long long g_slLinked = 0, g_slPutBack = 0, g_slPutBackWild = 0, g_slFault = 0, g_slLines = 0;   /* totals (putBackWild: of putBack, given the wilderness town) */
long long g_slAlready = 0, g_slNoTown = 0, g_slInside = 0, g_slEngineLeft = 0;                  /* as of the last whole look (engineLeft: a copy in a
                                                                                                   shown town whose key its row never said - where the engine put it, left) */
const long long kShowLinkLineCap = 40;
/* SHOW KEYS (possibly split): the list replaces the one held once whole (MAIN THREAD, the session dispatch) */
void BdShowKeysNote(unsigned flags, std::vector<townkeep::TownRow>* rows)
{
    std::vector<townkeep::TownRow> done;
    const int took = townkeep::ShowKeysTake(&g_showKeysAsm, flags, *rows, &done);
    char line[300];
    if (took == townkeep::kSayOrphan)
    {
        ++g_showKeysBad;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[SHOW] a SHOW KEYS message that starts no list (or a list past %u rows) - ignored, the keys held stay; showKeysBad=%lld",
                    (unsigned)townkeep::kMaxShowKeyRows, g_showKeysBad);
        ErrorLog(line);
        return;
    }
    if (took == townkeep::kSayPart) return;
    g_showKeyRows.swap(done);
    baseshow::SeenNote(&g_showKeysSeen, g_showKeyRows);   /* T1342: a row's keys said once count as said until the row goes or the world changes */
    ++g_showKeysIn;
    g_showLinkDue = 1;
    size_t keys = 0, cut = 0;
    for (size_t i = 0; i < g_showKeyRows.size(); ++i) { keys += g_showKeyRows[i].keys.size(); if (g_showKeyRows[i].cut != 0) ++cut; }
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[SHOW] teammates' outpost keys from the world server: %u row(s), %u key(s), %u row(s) cut; showKeysIn=%lld",
                (unsigned)g_showKeyRows.size(), (unsigned)keys, (unsigned)cut, g_showKeysIn);
    DebugLog(line);
}
/* P47 PUT-BACK: the town a copy leaving a shown town is given - baseshow::PutBackPick over the TownList's list of all towns (+0x58
   count, +0x60 array: the list 0x57C1E0's own town choice walks), each read as 0x9279C0 reads it, in its order: state +0xD8 (8 =
   dead), then vt+0x270, then the faction vt+0x58 (a player town: Faction+0x250 set), then the radius vt+0x2A0 and the position +0x48 /
   +0x50. excl / nExcl: the going town and every shown town. None holds the spot: the wilderness town (TownList+0x180F8, *wild = 1) -
   where 0x9279C0 itself falls back; a load chooses the town again (0x57C1E0). The list walked is every town, not 0x9279C0's 3x3 grid
   cells around the spot (baseshow.h PutBackPick says where the two differ). The pick is written as town words only
   (BdBuildingSetTownPod). 0 = the list does not read, a fault, or the wilderness town is none or excluded. MAIN THREAD */
const size_t kTownListAllN = 0x58, kTownListAllArr = 0x60, kTownListWild = 0x180F8, kFactionPlayerIf = 0x250;   /* kTownState: above */
const int kTownStateDead = 8, kVtTownBusy = 0x270, kVtTownRadius = 0x2A0;
const int kPutBackCap = 4096, kShowExclCap = 256;
typedef float (*BdTownRadiusFn)(void* town);
static baseshow::PutBackTown g_pbTowns[kPutBackCap];   /* MAIN THREAD scratch */
static void* g_pbTownPtr[kPutBackCap];
void* BdShowPutBackTownPod(void* b, void* const* excl, int nExcl, int* wild)
{
    *wild = 0;
    void* tl = BdTownListPtr();
    if (b == 0 || tl == 0) return 0;
    __try
    {
        const float bx = *(const float*)((const char*)b + kObjPos), bz = *(const float*)((const char*)b + kObjPos + 8);
        const char* c = (const char*)tl;
        const unsigned n = *(const unsigned*)(c + kTownListAllN);
        void* const* arr = *(void* const* const*)(c + kTownListAllArr);
        if (n > 0 && arr == 0) return 0;
        int k = 0;
        for (unsigned i = 0; i < n && k < kPutBackCap; ++i)
        {
            void* t = arr[i];
            if (t == 0) continue;
            baseshow::PutBackTown& p = g_pbTowns[k];
            p.excluded = 0; p.dead = 0; p.busy = 0; p.playerTown = 0; p.d2 = 0.0f; p.radius = 0.0f;
            for (int j = 0; j < nExcl; ++j) if (excl[j] == t) p.excluded = 1;
            if (p.excluded == 0) p.dead = *(const int*)((const char*)t + kTownState) == kTownStateDead ? 1 : 0;
            void** vt = *(void***)t;
            if (p.excluded == 0 && p.dead == 0) p.busy = ((BdTownGetFn)vt[kVtTownBusy / 8])(t) != 0 ? 1 : 0;
            if (p.excluded == 0 && p.dead == 0 && p.busy == 0)
            {
                void* f = ((BdTownGetFn)vt[kVtTownFaction / 8])(t);
                p.playerTown = (f != 0 && *(void* const*)((const char*)f + kFactionPlayerIf) != 0) ? 1 : 0;
            }
            if (p.excluded == 0 && p.dead == 0 && p.busy == 0 && p.playerTown == 0)
            {
                p.radius = ((BdTownRadiusFn)vt[kVtTownRadius / 8])(t);
                const float dz = *(const float*)((const char*)t + kObjPos + 8) - bz, dx = *(const float*)((const char*)t + kObjPos) - bx;
                p.d2 = dz * dz + dx * dx;
            }
            g_pbTownPtr[k++] = t;
        }
        const int pick = baseshow::PutBackPick(g_pbTowns, k);
        if (pick >= 0) return g_pbTownPtr[pick];
        void* w = *(void* const*)(c + kTownListWild);
        if (w == 0) return 0;
        for (int j = 0; j < nExcl; ++j) if (excl[j] == w) return 0;
        *wild = 1;
        return w;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *wild = 0; return 0; }
}
/* the put-back's exclusions: `also` (the going town, when not 0) and every shown base's town standing here. -1 = more than cap (none
   is put back: a shown town could be chosen) */
int BdShowExclTowns(void** out, int cap, void* also)
{
    int k = 0;
    if (also != 0) out[k++] = also;
    for (size_t i = 0; i < g_shown.size(); ++i)
    {
        void* t = BdShowTownByHand(g_shown[i].hand);
        if (t == 0 || t == also) continue;
        if (k >= cap) return -1;
        out[k++] = t;
    }
    return k;
}
/* a copy taken out of a shown town, put back in the town BdShowPutBackTownPod chooses (BdBuildingSetTownPod); 1 = done */
int BdShowPutBackCopy(void* b, const std::string& regKey, const char* why, void* const* excl, int nExcl)
{
    if (nExcl < 0) { ++g_slFault; return 0; }
    int wild = 0;
    void* t = BdShowPutBackTownPod(b, excl, nExcl, &wild);
    if (t == 0) { ++g_slFault; return 0; }
    const int set = BdBuildingSetTownPod(b, t);
    if (set == 0) { ++g_slFault; return 0; }
    if (set < 0) return 0;
    ++g_slPutBack;
    if (wild != 0) ++g_slPutBackWild;
    if (g_slLines < kShowLinkLineCap || g_slPutBack % 100 == 0)
    {
        ++g_slLines;
        char line[460];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[SHOW] copy key=%.63s taken out of its teammate's shown town (%s): put back in '%.80s'%s; showPutBack=%lld putBackWild=%lld",
                    regKey.c_str(), why, BdTownName(t).c_str(), wild != 0 ? " (the wilderness town: no other town's area holds its spot; a load chooses again)" : "",
                    g_slPutBack, g_slPutBackWild);
        DebugLog(line);
    }
    return 1;
}
/* the registry row of a loaded copy of slot `slot`'s piece (g_reg end = none: not registered, this game's own, gone, furniture) */
std::map<std::string, BdReg>::const_iterator BdShowCopyRow(void* b, int slot)
{
    char k[kKeyCap];
    k[0] = 0;
    if (ObjectPositionKey(b, k, kKeyCap, "", 3, 1) == 0) return g_reg.end();
    std::map<std::string, BdReg>::const_iterator r = BdTownRegRowOf(std::string(k));
    if (r == g_reg.end()) return r;
    const BdReg& g = r->second;
    if (g.own != 0 || g.removed != 0 || g.destroyed != 0 || g.dismantled != 0 || !g.hostKey.empty() || (g.ownerSlot >= 0 && g.ownerSlot != slot)) return g_reg.end();
    return r;
}
/* before a base founded here is taken out (its row went - team leave step 4 - or a save was asked for), its loaded copies linked to
   it are put back (BdShowPutBackCopy; BdShowTakeOut refuses while a loaded piece names the town). budget: the writes this frame may
   still make (0 = no cap: the save path, whose save may run at the next frame); each put-back takes one. 1 = no copy of the owner's
   is left in it; 0 = one is left (a fault, or the world moved under the list; BdShowTakeOut then refuses and it is tried again);
   -1 = the budget ran out first (the rest at the next apply) */
int BdShowUnlinkTown(const baseshow::Shown& s, int* budget, const char* why)
{
    void* t = BdShowTownByHand(s.hand);
    if (t == 0) return 1;
    float tp[3]; int th[5]; void* tf = 0;
    if (BdTownReadPod(t, tp, th, &tf) == 0) return 0;
    static void* excl[kShowExclCap];
    const int nExcl = BdShowExclTowns(excl, kShowExclCap, t);
    static void* list[4096];
    int zones = 0, trunc = 0;
    const long long gen0 = BoxWalkGen();
    const int n = LoadedBuildings(list, 4096, &zones, &trunc);
    int left = 0;
    for (int i = 0; i < n; ++i)
    {
        if (BoxWalkGen() != gen0) return 0;
        void* b = list[i];
        int bh[5];
        if (b == 0 || BdBuildingTopLevelPod(b) != 1 || BdBuildingTownHandPod(b, bh) == 0 || !townkeep::HandSame(bh, th)) continue;
        std::map<std::string, BdReg>::const_iterator r = BdShowCopyRow(b, s.owner);
        if (r == g_reg.end()) continue;   /* not a copy of the owner's piece: BdShowTakeOut decides */
        if (budget != 0 && *budget <= 0) return -1;
        if (budget != 0) --*budget;
        if (BdShowPutBackCopy(b, r->first, why, excl, nExcl) == 0) ++left;
    }
    return left == 0 ? 1 : 0;
}
void BdShowLinkTick()
{
    const DWORD now = ::GetTickCount();
    if (g_showLinkDue == 0 && now - g_showLinkAt < kTownLookMs) return;
    if (g_shown.empty() || g_showKeysIn == 0 || kBdTownListRva == 0 || EngineWritesBlocked() || BdOnMain() == 0) return;
    if (g_showSaveHeld != 0 || BdShowSavePending() != 0) return;   /* review fold 1: a save is on its way - no copy linked into it; linked again after it ran (due kept) */
    g_showLinkDue = 0;
    g_showLinkAt = now;
    const int mySlot = KeptMySlot();
    if (mySlot < 0) return;
    /* each shown base: its town here and its words, its key row */
    std::vector<void*> town(g_shown.size(), (void*)0);
    std::vector<int> keyRow(g_shown.size(), -1);
    std::vector<int> words(g_shown.size() * 5, 0);
    std::map<void*, int> slotOfFac;
    std::map<std::string, size_t> base;   /* "<owner>|<wire key>" -> its base (a base whose row went is on none) */
    for (size_t s = 0; s < g_shown.size(); ++s)
    {
        const baseshow::Shown& sh = g_shown[s];
        if (sh.owner < 0 || sh.owner == mySlot) continue;
        ::Faction* fac = OwnerFactionForSlot(sh.owner, false);
        if (fac != 0) slotOfFac[(void*)fac] = sh.owner;
        void* t = BdShowTownByHand(sh.hand);
        float tp[3]; void* tf = 0;
        if (t != 0 && BdTownReadPod(t, tp, &words[s * 5], &tf) != 0 && townkeep::HandKnown(&words[s * 5])) town[s] = t;
        keyRow[s] = baseshow::KeyRowOf(sh, g_showKeyRows);
        if (keyRow[s] < 0 || sh.going != 0) continue;
        const std::vector<std::string>& ks = g_showKeyRows[(size_t)keyRow[s]].keys;
        char pre[24];
        _snprintf_s(pre, sizeof(pre), _TRUNCATE, "%d|", sh.owner);
        for (size_t k = 0; k < ks.size(); ++k) base.insert(std::make_pair(std::string(pre) + ks[k], s));   /* the first base naming a key keeps it */
    }
    if (slotOfFac.empty()) return;
    static void* list[4096];
    int zones = 0, trunc = 0;
    const long long gen0 = BoxWalkGen();   /* taken before the list: a zone deactivation frees what the list names */
    const int n = LoadedBuildings(list, 4096, &zones, &trunc);
    long long already = 0, noTown = 0, inside = 0, engineLeft = 0;
    int writes = 0, capped = 0;
    static void* excl[kShowExclCap];
    const int nExcl = BdShowExclTowns(excl, kShowExclCap, 0);   /* every shown town (the going ones among them): no put-back into one */
    for (int i = 0; i < n; ++i)
    {
        if (BoxWalkGen() != gen0) return;   /* the world moved under the list: the next look */
        void* b = list[i];
        if (b == 0) continue;
        std::map<void*, int>::const_iterator fs = slotOfFac.find(BdOwnerPod(b));
        if (fs == slotOfFac.end()) continue;   /* no shown teammate's stand-in owns it */
        const int slot = fs->second;
        std::map<std::string, BdReg>::const_iterator r = BdShowCopyRow(b, slot);
        if (r == g_reg.end()) continue;
        char w[poskey::kKeyCap];
        if (poskey::WireForm(r->first.c_str(), w, (int)sizeof(w)) == 0) continue;
        char pre[24];
        _snprintf_s(pre, sizeof(pre), _TRUNCATE, "%d|", slot);
        std::map<std::string, size_t>::const_iterator bi = base.find(std::string(pre) + w);
        const int top = BdBuildingTopLevelPod(b);
        int bh[5];
        if (top < 0 || BdBuildingTownHandPod(b, bh) == 0) { ++g_slFault; continue; }
        if (top == 0) { if (bi != base.end()) ++inside; continue; }   /* inside a building: its host's town */
        const size_t row = bi != base.end() ? bi->second : 0;
        const int townStands = (bi != base.end() && town[row] != 0) ? 1 : 0;
        const int inRowTown = (townStands != 0 && townkeep::HandSame(bh, &words[row * 5])) ? 1 : 0;
        int inShown = -1;
        if (bi == base.end())
            for (size_t s = 0; s < g_shown.size() && inShown < 0; ++s)
                if (g_shown[s].owner == slot && town[s] != 0 && townkeep::HandSame(bh, &words[s * 5])) inShown = (int)s;
        int unlinkOk = 0;
        if (inShown >= 0)
        {
            const baseshow::Shown& sh = g_shown[(size_t)inShown];
            const int kr = keyRow[(size_t)inShown];
            const int cut = kr >= 0 ? g_showKeyRows[(size_t)kr].cut : 0;
            const int said = kr >= 0 ? baseshow::SeenHas(g_showKeysSeen, g_showKeyRows[(size_t)kr], std::string(w)) : 0;   /* T1342 */
            unlinkOk = baseshow::UnlinkOk(sh.going, kr >= 0 ? 1 : 0, cut, said);
            if (baseshow::EngineLeft(sh.going, kr >= 0 ? 1 : 0, cut, said) != 0) ++engineLeft;   /* the engine put it there at creation: left */
        }
        const int step = baseshow::LinkStep(bi != base.end() ? 1 : 0, townStands, inRowTown, inShown >= 0 ? 1 : 0, unlinkOk);
        if (step == baseshow::kLinkAlready) { ++already; continue; }
        if (step == baseshow::kLinkNoTown) { ++noTown; continue; }
        if (step != baseshow::kLinkDo && step != baseshow::kLinkUndo) continue;
        if (writes >= kTownLinksPerLook) { capped = 1; continue; }   /* enough writes for one frame: the rest at the next drain */
        ++writes;
        if (step == baseshow::kLinkUndo) { BdShowPutBackCopy(b, r->first, "its key dropped from its base's keys, or its base going", excl, nExcl); continue; }
        void* t = town[row];
        void* cur = BdBuildingTownPod(b);
        const std::string was = cur != 0 ? BdTownName(cur) : std::string("none");
        const int set = BdBuildingSetTownPod(b, t);
        if (set == 0) { ++g_slFault; continue; }
        if (set < 0) { ++inside; continue; }
        ++g_slLinked;
        if (g_slLines < kShowLinkLineCap || g_slLinked % 100 == 0)
        {
            ++g_slLines;
            char line[460];
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[SHOW] copy key=%.63s of slot %d linked to its teammate's shown town '%.80s' (the engine read its town as '%.80s', words %d/%d/%d/%d/%d); showLinked=%lld",
                        r->first.c_str(), slot, BdTownName(t).c_str(), was.c_str(), bh[0], bh[1], bh[2], bh[3], bh[4], g_slLinked);
            DebugLog(line);
        }
    }
    if (capped != 0) g_showLinkDue = 1;   /* the rest at the next drain */
    g_slAlready = already; g_slNoTown = noTown; g_slInside = inside; g_slEngineLeft = engineLeft;
}
/* the held list applied to this world: the mod's own bases standing untracked taken up, bases whose row went taken out (kept and tried
   again while they cannot be), each row's town kept, taken or founded, faction and name set */
void BdShowApply()
{
    const long gen = StoreWorldGenNow();
    if (gen != g_showWorldGen) { g_showWorldGen = gen; g_shown.clear(); g_showDirty = 1; g_showRetryAt = 0; g_showSaveHeld = 0; g_townKeepLogged.clear(); }   /* another world: the old one's towns went with it */
    const DWORD now = ::GetTickCount();
    const bool retryDue = g_showRetryAt != 0 && (long)(now - g_showRetryAt) >= 0;
    if ((g_showDirty == 0 && !retryDue && g_townRowsGen == g_showKeptSeen) || kBdTownListRva == 0 || EngineWritesBlocked()) return;
    const int mySlot = KeptMySlot();
    if (mySlot < 0 || LocalPlayerFaction() == 0 || BdTownListPtr() == 0) return;
    if (g_showSaveHeld != 0 || BdShowSavePending() != 0) return;   /* a save is on its way: applied after it ran */
    g_showDirty = 0;
    g_showRetryAt = 0;
    g_showKeptSeen = g_townRowsGen;
    const long long founded0 = g_showFounded, taken0 = g_showTaken, renamed0 = g_showRenamed, removed0 = g_showRemoved, up0 = g_showTakenUp;
    /* 1. the mod's own bases standing here untracked (a reload, or a save that held them) - once a SHOW came and this world's kept list is in */
    if (g_showIn > 0 && g_townRowsGen >= 0 && g_townRowsGen == KeptWorldGen())
    {
        void* towns[kTownWalkCap];
        const int n = BdPlayerTownsPod(towns, kTownWalkCap);
        for (int i = 0; i < n; ++i)
        {
            float lp[3]; int h[5]; void* f = 0;
            if (BdTownReadPod(towns[i], lp, h, &f) == 0 || f == 0 || !townkeep::HandKnown(h) || BdShowTracked(h)) continue;
            int nearKept = 0;
            for (size_t r = 0; r < g_townRows.size() && nearKept == 0; ++r) nearKept = townkeep::TownStands(lp, g_townRows[r]);
            const int slot = StandInRecordSlot((::Faction*)f);
            if (slot == mySlot || baseshow::LeftoverMark(slot, baseshow::LabelForm(BdTownName(towns[i])) ? 1 : 0, nearKept) == 0) continue;
            baseshow::Shown s;
            s.owner = slot;
            for (int j = 0; j < 3; ++j) s.pos[j] = lp[j];
            for (int j = 0; j < 5; ++j) s.hand[j] = h[j];
            s.ours = 1;
            g_shown.push_back(s);
            ++g_showTakenUp;
        }
    }
    std::vector<int> rowTo;
    std::vector<size_t> gone;
    baseshow::Match(g_shown, g_showRows, &rowTo, &gone);
    std::vector<baseshow::Shown> next;
    const bool removeOk = baseshow::RemoveAllowed(g_showFlags);
    int waiting = 0, unlinkMore = 0;
    int unlinkBudget = kTownLinksPerLook;   /* review fold 5: the put-backs one apply makes; the rest at the next drain */
    for (size_t i = 0; i < gone.size(); ++i)
    {
        baseshow::Shown s = g_shown[gone[i]];
        if (!removeOk) { next.push_back(s); continue; }   /* the list was cut: a missing row is not a removed one */
        if (s.ours == 0) continue;                         /* a town that stood already (a kept outpost's): not this mod's to take out */
        s.going = 1;
        /* owner 936: the copies this game linked to it go back first, so it can be taken out (else tried again) */
        if (BdShowUnlinkTown(s, &unlinkBudget, "its row went") < 0) { next.push_back(s); ++waiting; unlinkMore = 1; continue; }   /* more copies: the next drain, then taken out */
        if (BdShowTakeOut(&s, "its row went") == 0) { next.push_back(s); ++waiting; }   /* still standing: kept, tried again */
    }
    void* towns[kTownWalkCap];
    const int n = BdPlayerTownsPod(towns, kTownWalkCap);   /* read after the take-outs: no town taken out above is in it */
    for (size_t r = 0; r < g_showRows.size(); ++r)
    {
        const baseshow::ShowRow& row = g_showRows[r];
        if (row.owner == mySlot) continue;
        ::Faction* fac = OwnerFactionForSlot(row.owner, true);
        if (fac == 0)
        {   /* no stand-in now: a base already tracked for the row stays tracked as it is */
            ++g_showNoFaction;
            if (rowTo[r] >= 0) next.push_back(g_shown[(size_t)rowTo[r]]);
            continue;
        }
        baseshow::Shown s;
        if (rowTo[r] >= 0) s = g_shown[(size_t)rowTo[r]];
        s.going = 0;
        s.owner = row.owner;
        s.level = row.level;   /* the outpost icon the map hook draws */
        for (int j = 0; j < 3; ++j) s.pos[j] = row.pos[j];
        void* own = BdShowTownByHand(s.hand);
        void* standing = 0;
        if (own == 0)
            for (int i = 0; i < n && standing == 0; ++i)
            {
                float lp[3]; int h[5]; void* f = 0;
                if (BdTownReadPod(towns[i], lp, h, &f) != 0 && f == (void*)fac && keptwire::NearXZ(lp[0], lp[2], row.pos[0], row.pos[2], baseshow::kSameBaseRadius) != 0) standing = towns[i];
            }
        const int step = baseshow::RowStep(own != 0 ? 1 : 0, standing != 0 ? 1 : 0, kBdTownFoundRva != 0 ? 1 : 0);
        void* t = 0;
        if (step == baseshow::kRowKeep) t = own;
        else if (step == baseshow::kRowTake)
        {   /* a town of the stand-in at the centre: the mod's own when it carries the mark, else a kept outpost's */
            t = standing;
            int nearKept = 0;
            float lp[3] = { 0.0f, 0.0f, 0.0f }; int h[5]; void* f = 0;
            if (BdTownReadPod(t, lp, h, &f) != 1) nearKept = 1;   /* an unreadable town is never taken as the mod's own: it is left standing */
            for (size_t k = 0; k < g_townRows.size() && nearKept == 0; ++k) nearKept = townkeep::TownStands(lp, g_townRows[k]);
            s.ours = baseshow::LeftoverMark(StandInRecordSlot(fac), baseshow::LabelForm(BdTownName(t)) ? 1 : 0, nearKept);
            ++g_showTaken;
        }
        else if (step == baseshow::kRowFound)
        {
            t = BdTownFoundPod(row.pos);
            if (t == 0) { ++g_showFoundFail; continue; }
            s.ours = 1;
            ++g_showFounded;
            float lp[3]; int h[5]; void* f0 = 0;
            BdTownReadPod(t, lp, h, &f0);
            if (f0 != (void*)fac && BdTownSetFactionPod(t, (void*)fac) == 0)
            {
                char fl[200];
                _snprintf_s(fl, sizeof(fl), _TRUNCATE, "[SHOW] base of slot %d founded, its faction write FAULTED", row.owner);
                ErrorLog(fl);
            }
        }
        else { ++g_showFoundFail; continue; }   /* TownFoundPlayer is not in the address table */
        float lp[3]; void* f1 = 0;
        BdTownReadPod(t, lp, s.hand, &f1);
        std::string player = row.ownerName;
        if (player.empty()) player = StandInDisplayName(fac);
        if (player.empty()) { char pn[40]; _snprintf_s(pn, sizeof(pn), _TRUNCATE, "Player %d", row.owner); player = pn; }
        const std::string label = baseshow::Label(row.name, player);
        if (BdTownName(t) != label && BdTownSetNamePod(t, &label) != 0) ++g_showRenamed;
        next.push_back(s);
    }
    g_shown.swap(next);
    if (g_showFounded != founded0 || g_showTaken != taken0 || g_showTakenUp != up0) g_showLinkDue = 1;   /* owner 936: the copies linked to a new / adopted town at the next drain */
    if (waiting > 0) g_showRetryAt = (now + (DWORD)baseshow::kShowRetryMs) | 1u;
    if (unlinkMore != 0) g_showDirty = 1;   /* review fold 5: the rest of the copies put back at the next drain */
    /* P47 review: a base kept by the store gate is retried every kShowRetryMs - the line only when this apply changed something */
    static int s_lastWaiting = -1; static size_t s_lastRows = (size_t)-1, s_lastShown = (size_t)-1;
    const bool quiet = g_showTakenUp == up0 && g_showFounded == founded0 && g_showTaken == taken0 && g_showRenamed == renamed0 && g_showRemoved == removed0
                       && waiting == s_lastWaiting && g_showRows.size() == s_lastRows && g_shown.size() == s_lastShown && removeOk;
    s_lastWaiting = waiting; s_lastRows = g_showRows.size(); s_lastShown = g_shown.size();
    if (quiet) return;
    char line[460];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[SHOW] applied %u row(s)%s: %u base(s) tracked, taken up +%lld founded +%lld taken +%lld named +%lld taken out +%lld, %d waiting;"
                " founded=%lld taken=%lld takenOut=%lld left=%lld foundFail=%lld noFaction=%lld townKeepStore=%lld", (unsigned)g_showRows.size(), removeOk ? "" : " (list cut: none taken out)",
                (unsigned)g_shown.size(), g_showTakenUp - up0, g_showFounded - founded0, g_showTaken - taken0, g_showRenamed - renamed0, g_showRemoved - removed0, waiting,
                g_showFounded, g_showTaken, g_showRemoved, g_showLeft, g_showFoundFail, g_showNoFaction, g_townKeepStore);
    DebugLog(line);
}
/* the map hooks (owner 874 a; installed in InstallBuild): a town this game shows for a teammate - found by its handle words among the
   tracked bases, main thread only - gets the icon name the engine gives a player outpost of the row's size level (IconNameOfLevel),
   written into the call's answer as 0x48CEE0 writes it, and the player outposts' map layer. No town field is written. A marker is
   made when the map lists the town; a later change of level shows when the marker is made again. */
long long g_showIcons = 0;
int g_showMarkReading = 0;   /* T-811 log: 1 while BdShowMarkTick itself asks a town for its icon (the hook does not count that as the map's) */
int g_showMarkDue = 0;       /* T-811 log: 1 = the map made a teammate base's marker since the last look */
/* BdTownHandPod: defined with the town wake-up above (T-936) */
/* the index in g_shown of a base this game shows for a teammate; -1 = the town is not one (MAIN THREAD) */
int BdShowIndexOf(void* town)
{
    if (town == 0 || g_shown.empty()) return -1;
    int h[5] = { 0, 0, 0, 0, 0 };
    if (BdTownHandPod(town, h) == 0 || !townkeep::HandKnown(h)) return -1;
    for (size_t i = 0; i < g_shown.size(); ++i) if (townkeep::HandSame(g_shown[i].hand, h)) return (int)i;
    return -1;
}
/* the size level of a base this game shows for a teammate; -1 = the town is not one (MAIN THREAD) */
int BdShowLevelOf(void* town)
{
    const int i = BdShowIndexOf(town);
    return i < 0 ? -1 : g_shown[(size_t)i].level;
}
/* the answer of the icon call: the engine's short-string form, as 0x48CEE0 writes it into the caller's slot (15 bytes in place, the
   size at +0x10, the room 15 at +0x18); every icon name is 10 bytes at most */
void* BdShowIconPod(void* outName, const char* name)
{
    const size_t len = std::strlen(name);
    char* o = (char*)outName;
    std::memset(o, 0, 32);
    std::memcpy(o, name, len < 15 ? len : 15);
    *(unsigned long long*)(o + 0x10) = (unsigned long long)(len < 15 ? len : 15);
    *(unsigned long long*)(o + 0x18) = 15ull;
    return outName;
}
void* detour_townMapIcon(void* town, void* outName)
{
    const int idx = (BdOnMain() != 0) ? BdShowIndexOf(town) : -1;
    if (idx < 0) return orig_townMapIcon(town, outName);
    const int level = g_shown[(size_t)idx].level;
    if (g_showMarkReading == 0)
    {   /* T-811 log: the map made this base's marker - BdShowMarkTick reads and logs what it got */
        ++g_showIcons;
        g_shown[(size_t)idx].drawn = 1;
        g_showMarkDue = 1;
    }
    return BdShowIconPod(outName, baseshow::IconNameOfLevel(level));
}
unsigned detour_townMapLayer(void* town)
{
    if (BdOnMain() != 0 && BdShowLevelOf(town) >= 0) return baseshow::kOutpostMapLayer;
    return orig_townMapLayer(town);
}
/* ---- T-811 log (decisions 874 a / 875): what the map draws for each teammate base, read live from the game ----
   The icon: the town's own icon call (Town vt+0x378, the call the marker set-up 0x48D7A0 makes - for a teammate's base it runs through
   detour_townMapIcon) into an empty answer slot, its text read back. The colour: as the marker refresh 0x48F280 reads it - the town's
   name-shown call (vt+0x340, Town+0x181) and the engine's colour pick MapMarkerColour(town), a pointer into the five marker colours
   (baseshow::MarkerColourSlotOf); a town whose name is not shown is drawn in the neutral colour. The player's own outpost is read the
   same way for comparison. MAIN THREAD (BuildCopyDrain), fault-guarded; a line when a marker is first made or what it shows changes. */
DWORD g_showMarkAt = 0;
long long g_showMarkLines = 0;
/* the icon text the map gets for town t; 1 = read, 2 = read and the answer was a heap string (left: the engine's allocator), 0 = faulted */
int BdMarkIconPod(void* t, char* out, size_t cap)
{
    out[0] = 0;
    __try
    {
        unsigned char ans[40];   /* the game's string object is 0x28 bytes (decomp_48f280 locals 0x28 apart) */
        std::memset(ans, 0, sizeof(ans));
        void** vt = *(void***)t;
        ((BdTownMapIconFn)vt[0x378 / 8])(t, ans);
        const unsigned long long size = *(const unsigned long long*)(ans + 0x10);
        const unsigned long long room = *(const unsigned long long*)(ans + 0x18);
        const char* text = room > 15 ? *(const char* const*)ans : (const char*)ans;
        const size_t n = size < (unsigned long long)(cap - 1) ? (size_t)size : cap - 1;
        if (text != 0) std::memcpy(out, text, n);
        out[n] = 0;
        return room > 15 ? 2 : 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; return 0; }
}
/* the town's name-shown flag (vt+0x340) and the engine's colour pick for it with that colour's 4 floats; 1 = read, 0 = faulted */
int BdMarkColourPod(void* t, BdMapMarkerColourFn pick, int* nameShown, unsigned long long* colour, float* rgba)
{
    __try
    {
        typedef char (*BdTownBoolFn)(void* town);
        void** vt = *(void***)t;
        *nameShown = ((BdTownBoolFn)vt[0x340 / 8])(t) != 0 ? 1 : 0;
        const float* c = *nameShown != 0 ? pick(t) : 0;   /* as the map's refresh (0x48F280): the pick runs only for a shown name */
        *colour = (unsigned long long)(uintptr_t)c;
        for (int i = 0; i < 4; ++i) rgba[i] = c != 0 ? c[i] : 0.0f;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the 4 floats of marker colour slot k; 1 = read */
int BdMarkSlotPod(unsigned long long slots, int k, float* rgba)
{
    __try { for (int i = 0; i < 4; ++i) rgba[i] = ((const float*)(uintptr_t)(slots + (unsigned long long)k * baseshow::kMarkerColourBytes))[i]; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the colour field of the line: "<ally|own|neutral|enemy|own-selected>[<skin name> rgba=r/g/b/a nameShown=n pick=<name>]" or "?(<why>)" */
std::string BdMarkColourText(void* t)
{
    char b[260];
    if (kBdMapMarkerColourRva == 0 || kBdMapMarkerSlotsRva == 0) return "?(MapMarkerColour / MapMarkerColourSlots not in this game's address table)";
    const unsigned long long slots = (unsigned long long)AddrAbs(kBdMapMarkerSlotsRva);
    int shown = 0; unsigned long long c = 0; float rgba[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (BdMarkColourPod(t, (BdMapMarkerColourFn)(uintptr_t)AddrAbs(kBdMapMarkerColourRva), &shown, &c, rgba) == 0) return "?(the colour read FAULTED)";
    const int picked = shown != 0 ? baseshow::MarkerColourSlotOf(c, slots) : baseshow::kMarkerNeutral;
    if (picked < 0)
    {
        _snprintf_s(b, sizeof(b), _TRUNCATE, "?(the engine's pick 0x%llX is none of the five marker colours at 0x%llX; rgba=%.3f/%.3f/%.3f/%.3f nameShown=%d)", c, slots, rgba[0], rgba[1], rgba[2], rgba[3], shown);
        return b;
    }
    const int drawn = baseshow::MarkerDrawnSlot(shown, picked);
    float d[4] = { rgba[0], rgba[1], rgba[2], rgba[3] };
    if ((drawn != picked || shown == 0) && BdMarkSlotPod(slots, drawn, d) == 0) return "?(the neutral colour read FAULTED)";
    _snprintf_s(b, sizeof(b), _TRUNCATE, "%s[%s rgba=%.3f/%.3f/%.3f/%.3f nameShown=%d pick=%s]", baseshow::MarkerColourName(drawn), baseshow::MarkerSkinName(drawn),
                d[0], d[1], d[2], d[3], shown, baseshow::MarkerColourName(picked));
    return b;
}
/* the icon field: the name, or "?(<why>)" */
std::string BdMarkIconText(void* t)
{
    char icon[64];
    g_showMarkReading = 1;
    const int r = BdMarkIconPod(t, icon, sizeof(icon));
    g_showMarkReading = 0;
    if (r == 0) return "?(the town's icon call FAULTED)";
    if (icon[0] == 0) return "?(the town's icon call answered an empty name)";
    return std::string(icon) + (r == 2 ? "(long answer)" : "");
}
void BdShowMarkTick()
{
    if (g_shown.empty() || BdOnMain() == 0 || EngineWritesBlocked()) return;
    const DWORD now = ::GetTickCount();
    if (g_showMarkDue == 0 && now - g_showMarkAt < 2000) return;   /* a look every 2 s, and at once after the map made a marker */
    g_showMarkDue = 0;
    g_showMarkAt = now;
    bool any = false;
    for (size_t i = 0; i < g_shown.size(); ++i) if (g_shown[i].drawn != 0) any = true;
    if (!any) return;
    std::string own = "?(this player has no outpost)";
    ::Faction* mine = LocalPlayerFaction();
    void* towns[kTownWalkCap];
    const int n = BdPlayerTownsPod(towns, kTownWalkCap);
    for (int i = 0; i < n && mine != 0; ++i)
    {
        float lp[3]; int h[5]; void* f = 0;
        if (BdTownReadPod(towns[i], lp, h, &f) == 0 || f != (void*)mine) continue;
        own = BdMarkIconText(towns[i]) + "[own outpost '" + BdTownName(towns[i]).substr(0, 80) + "' colour=" + BdMarkColourText(towns[i]) + "]";
        break;
    }
    for (size_t i = 0; i < g_shown.size(); ++i)
    {
        baseshow::Shown& s = g_shown[i];
        if (s.drawn == 0) continue;
        void* t = BdShowTownByHand(s.hand);
        if (t == 0) continue;
        const std::string what = "icon=" + BdMarkIconText(t) + " colour=" + BdMarkColourText(t) + " ownOutpostIcon=" + own;
        if (what == s.markSaid) continue;
        s.markSaid = what;
        ++g_showMarkLines;
        char head[200];
        _snprintf_s(head, sizeof(head), _TRUNCATE, "[TEAMBASE] map marker '%.80s' owner=%d ", BdTownName(t).c_str(), s.owner);
        char tail[80];
        _snprintf_s(tail, sizeof(tail), _TRUNCATE, "; markLines=%lld showIcons=%lld", g_showMarkLines, g_showIcons);
        DebugLog(std::string(head) + what + tail);
    }
}
/* at the safe point (BuildCopyDrain, the AI worker paused - where the bases are founded): when the engine has been asked for a save
   (SaveManager+0xA0), every base founded here is taken out before the save runs; they are founded again at the first apply after it
   ran. One that cannot be taken out stays tracked. */
void BdShowSaveGuard()
{
    if (g_showSaveHeld != 0)
    {
        if (BdShowSavePending() != 0) return;
        g_showSaveHeld = 0;
        g_showDirty = 1;     /* the save ran: founded again */
        g_showLinkDue = 1;   /* review fold 1: the copies linked again (to a base founded again, or one left standing) */
        return;
    }
    if (g_shown.empty() || EngineWritesBlocked() || BdShowSavePending() == 0) return;
    BdSelfScope scope;
    int out = 0, left = 0;
    for (size_t i = 0; i < g_shown.size(); ++i)
    {
        if (g_shown[i].ours == 0 || !townkeep::HandKnown(g_shown[i].hand)) continue;
        /* review fold 1: the copies this game linked to it put back first, with no write cap (the save may run at the next frame), so
           the save holds none linked to it and it can be taken out; while the save is on its way BdShowLinkTick links none */
        BdShowUnlinkTown(g_shown[i], 0, "a save was asked for");
        if (BdShowTakeOut(&g_shown[i], "a save was asked for") != 0) ++out; else ++left;
    }
    g_showSaveHeld = 1;
    g_showSaveOut += out;
    if (out == 0 && left == 0) return;
    char line[300];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[SHOW] a save was asked for: %d teammate base(s) taken out of the world before it runs, %d left standing; saveOut=%lld", out, left, g_showSaveOut);
    DebugLog(line);
}
/* the saveGame pre-hook: counts only (no engine write here - the take-out is BdShowSaveGuard's, at the safe point). A base still
   standing now (a save with no safe point between its request and its run, e.g. from a paused game) is in this save; after a reload it
   is known by its mark and taken up (BdShowApply). */
void BuildShowBeforeSave(int onMainThread)
{
    if (onMainThread == 0) { ++g_showSaveOffThread; return; }
    int standing = 0;
    for (size_t i = 0; i < g_shown.size(); ++i) if (g_shown[i].ours != 0 && townkeep::HandKnown(g_shown[i].hand)) ++standing;
    if (standing == 0) return;
    g_showSavedIn += standing;
    char line[300];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[SHOW] a game save runs with %d base(s) founded here standing: the save holds them, and after a reload they are taken up by their mark; savedIn=%lld",
                standing, g_showSavedIn);
    DebugLog(line);
}
/* ---- outposttest (TEST-ONLY): found / link armed for the K2 safe point (BdOutpostDrain), list read now ---- */
volatile LONG g_otPending = 0;   /* 1 found, 2 link */
std::string g_otName, g_otKey;
float g_otDx = 0.0f, g_otDz = 0.0f;
std::string BdOutpostList()
{
    void* towns[kTownWalkCap];
    const int n = BdPlayerTownsPod(towns, kTownWalkCap);
    if (n < 0)
    {
        DebugLog("[BUILD] outposttest list: the town list is not readable here (TownList missing from the address table, or the read faulted)");
        return "error outposttest: the town list is not readable";
    }
    ::Faction* mine = LocalPlayerFaction();
    for (int i = 0; i < n; ++i)
    {
        int linked = 0;
        std::string keys;
        for (std::map<std::string, BdReg>::const_iterator r = g_reg.begin(); r != g_reg.end(); ++r)
        {
            void* b = ObjectByPositionKey(r->first.c_str());
            if (b == 0 || BdBuildingTownPod(b) != towns[i]) continue;
            ++linked;
            if (linked <= 8) keys += " " + r->first;
        }
        float p[3]; int h[5]; void* f = 0;
        BdTownReadPod(towns[i], p, h, &f);
        char line[900];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] outposttest list town %d of %d: %s own=%d registered pieces linked=%d%s%s", i + 1, n, BdTownText(towns[i]).c_str(),
                    (f != 0 && f == (void*)mine) ? 1 : 0, linked, keys.empty() ? "" : " keys:", keys.c_str());
        DebugLog(line);
    }
    char sum[400];
    _snprintf_s(sum, sizeof(sum), _TRUNCATE, "[BUILD] outposttest list: %d player town(s); kept outpost rows held %u; founded=%lld standing=%lld linked=%lld foundFail=%lld said=%lld teamWait=%lld notMate=%lld",
                n, (unsigned)g_townRows.size(), g_townFounded, g_townStanding, g_townLinked, g_townFoundFail, g_townSays, g_townTeamWait, g_townNotMate);
    DebugLog(sum);
    return "ok outposttest list: " + zonekeep::ZkNum((long long)n) + " player town(s)";
}
std::string OutpostTestCommand(const std::string& arg)
{
    std::istringstream is(arg);
    std::vector<std::string> tok;
    std::string s;
    while (is >> s) tok.push_back(s);
    const char* usage = "error outposttest: usage outposttest found <name> [dx dz] | link <key-substring> | list";
    if (tok.empty()) return usage;
    if (tok[0] == "list" && tok.size() == 1) return BdOutpostList();
    if (tok[0] == "link" && tok.size() == 2)
    {
        g_otKey = tok[1];
        ::InterlockedExchange(&g_otPending, 2);
        return "ok outposttest link armed: " + g_otKey;
    }
    if (tok[0] == "found" && tok.size() >= 2)
    {
        size_t end = tok.size();
        float dx = 0.0f, dz = 0.0f;
        if (tok.size() >= 4)
        {
            char* e1 = 0; char* e2 = 0;
            const double a = std::strtod(tok[tok.size() - 2].c_str(), &e1), b = std::strtod(tok[tok.size() - 1].c_str(), &e2);
            if (e1 != 0 && *e1 == 0 && e2 != 0 && *e2 == 0) { dx = (float)a; dz = (float)b; end -= 2; }
        }
        std::string name;
        for (size_t i = 1; i < end; ++i) name += (i > 1 ? " " : "") + tok[i];
        if (name.empty() || name.size() > keptwire::kStrMax) return usage;
        g_otName = name; g_otDx = dx; g_otDz = dz;
        ::InterlockedExchange(&g_otPending, 1);
        return "ok outposttest found armed: '" + name + "'";
    }
    return usage;
}
void BdOutpostDrain()
{
    const LONG kind = ::InterlockedExchange(&g_otPending, 0);
    if (kind == 0) return;
    if (EngineWritesBlocked()) { DebugLog("[BUILD] outposttest skipped: engine writes are blocked (world loading or tearing down)"); return; }
    ::Faction* mine = LocalPlayerFaction();
    char line[700];
    if (kind == 1)
    {
        float cp[3];
        if (mine == 0 || !BdFindMyCharPos(mine, cp)) { ErrorLog("[BUILD] outposttest found REFUSED: no own player-faction character with a readable position"); return; }
        const float pos[3] = { cp[0] + g_otDx, cp[1], cp[2] + g_otDz };
        void* t = BdTownFoundPod(pos);
        if (t == 0) { ErrorLog("[BUILD] outposttest found FAILED: TownFoundPlayer is missing from the address table, FAULTED or returned none"); return; }
        const std::string name(g_otName);
        const int named = BdTownSetNamePod(t, &name);
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] outposttest found CREATED %s%s (asked '%.80s' at (%.1f, %.1f, %.1f))", BdTownText(t).c_str(), named != 0 ? "" : " - the name write FAULTED",
                    name.c_str(), pos[0], pos[1], pos[2]);
        DebugLog(line);
        return;
    }
    std::string hit;
    int hits = 0;
    std::map<std::string, BdReg>::const_iterator ex = g_reg.find(g_otKey);
    if (ex != g_reg.end() && ex->second.own != 0) { hit = ex->first; hits = 1; }   /* the exact key */
    else
        for (std::map<std::string, BdReg>::const_iterator r = g_reg.begin(); r != g_reg.end(); ++r)
            if (r->second.own != 0 && r->first.find(g_otKey) != std::string::npos && ++hits == 1) hit = r->first;
    void* b = (hits == 1) ? ObjectByPositionKey(hit.c_str()) : 0;
    float bp[3];
    if (b == 0 || !BdCopyF((const char*)b + kObjPos, bp, 3))
    {
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] outposttest link REFUSED: '%.63s' names %d own registered piece(s)%s", g_otKey.c_str(), hits, hits == 1 ? " and it is not loaded here" : "");
        ErrorLog(line);
        return;
    }
    void* towns[kTownWalkCap];
    const int n = BdPlayerTownsPod(towns, kTownWalkCap);
    void* best = 0;
    float bestD = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float p[3]; int h[5]; void* f = 0;
        if (BdTownReadPod(towns[i], p, h, &f) == 0 || f != (void*)mine) continue;
        const float dx = p[0] - bp[0], dz = p[2] - bp[2], d = dx * dx + dz * dz;
        if (best == 0 || d < bestD) { best = towns[i]; bestD = d; }
    }
    if (best == 0) { ErrorLog("[BUILD] outposttest link REFUSED: this player has no outpost here"); return; }
    void* was = BdBuildingTownPod(b);
    const int ok = BdBuildingSetTownPod(b, best);   /* 1 written, -1 stands inside a building (nothing written), 0 a fault */
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] outposttest link %s key=%.63s -> %s (its town was '%.80s')%s", ok > 0 ? "LINKED" : (ok < 0 ? "NOT LINKED" : "FAULTED"), hit.c_str(),
                BdTownText(best).c_str(), was != 0 ? BdTownName(was).c_str() : "none", ok < 0 ? " - stands inside a building - nothing written" : "");
    DebugLog(line);
}
/* T-899 TEST-ONLY `buildtest town <sid|key-substring>` (read only, MAIN THREAD - BuildTestArm's thread; refused with the other verbs
   while engine writes are blocked): every registered piece the argument names (coopbuild::BuildTownArgMatch: its key contains it, or
   its sid is exactly it; at most kBdTownLeverCap shown) with its town as the engine's BuildingGetTown reads it (BdBuildingTownPod) - name,
   position, handle, faction - and whether that town is one of the player towns, plus the raw type words of its parent hand (+0x88,
   0xB = stands inside none) and its town hand (+0x1D8); then this player's own outposts with their handles, so a run compares the town
   a piece points at with the outpost it stands in. Every engine read is SEH-wrapped (BdFindPiece, BdBuildingTownPod, BdTownText). */
const int kBdTownLeverCap = 8;
std::string BdTestTown(const std::string& arg)
{
    void* towns[kTownWalkCap];
    const int n = BdPlayerTownsPod(towns, kTownWalkCap);
    ::Faction* mine = LocalPlayerFaction();
    int named = 0, shown = 0, notLoaded = 0, townFault = 0, inPlayerTown = 0;
    char line[900];
    for (std::map<std::string, BdReg>::const_iterator r = g_reg.begin(); r != g_reg.end(); ++r)
    {
        const int how = coopbuild::BuildTownArgMatch(arg, r->first, r->second.sid);
        if (how == 0) continue;
        if (++named > kBdTownLeverCap) continue;
        ++shown;
        int tv = 0;
        void* b = BdFindPiece(r->first, r->second, &tv);
        if (b == 0)
        {
            ++notLoaded;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] buildtest town piece key=%.63s sid=%.60s (named by its %s): not loaded here (verdict %d)",
                        r->first.c_str(), r->second.sid.c_str(), how == 1 ? "key" : "sid", tv);
            DebugLog(line);
            continue;
        }
        void* t = BdBuildingTownPod(b);
        int parentType = -1, townType = -1;
        BdBuildingHandTypesPod(b, &parentType, &townType);   /* left -1 on a fault */
        int player = 0;
        for (int i = 0; i < n; ++i) if (towns[i] == t) { player = i + 1; break; }
        if (t == 0) ++townFault;   /* BdBuildingTownPod gives 0 for a loaded piece only on a fault */
        else if (player != 0) ++inPlayerTown;
        char where[80];
        where[0] = 0;
        if (t != 0 && n < 0) _snprintf_s(where, sizeof(where), _TRUNCATE, " - the town list is not readable");
        else if (t != 0 && player != 0) _snprintf_s(where, sizeof(where), _TRUNCATE, " - player town %d of %d", player, n);
        else if (t != 0) _snprintf_s(where, sizeof(where), _TRUNCATE, " - NOT a player town");
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] buildtest town piece key=%.63s sid=%.60s (named by its %s) via=%d own=%d dismantled=%d: town=%s%s parentType=%d townType=%d",
                    r->first.c_str(), r->second.sid.c_str(), how == 1 ? "key" : "sid", r->second.via, r->second.own, r->second.dismantled,
                    t == 0 ? "FAULTED" : BdTownText(t).c_str(), where, parentType, townType);
        DebugLog(line);
    }
    int mineN = 0;
    for (int i = 0; i < n; ++i)
    {
        float p[3]; int h[5]; void* f = 0;
        if (BdTownReadPod(towns[i], p, h, &f) == 0 || f == 0 || f != (void*)mine) continue;
        ++mineN;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] buildtest town outpost (player town %d of %d, this player's): %s", i + 1, n, BdTownText(towns[i]).c_str());
        DebugLog(line);
    }
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] buildtest town '%.63s': %d registered piece(s) named, %d shown (not loaded %d, town read FAULTED %d, in a player town %d); player towns %d%s, this player's %d",
                arg.c_str(), named, shown, notLoaded, townFault, inPlayerTown, n, n < 0 ? " (the town list is not readable)" : "", mineN);
    DebugLog(line);
    return named == 0 ? "error buildtest town: no registered piece is named by '" + arg + "'" : "ok buildtest town: " + zonekeep::ZkNum((long long)named) + " piece(s) named";
}
void BdKeptPendDone(BdPending& p)
{
    std::map<unsigned, BdKeptLoc>::iterator it = g_kept.find((unsigned)p.keptRow);
    if (it == g_kept.end()) return;
    BdKeptLoc& L = it->second;
    L.pieceQueued = 0;
    const unsigned how = keptwire::HowOfRestore(p.rsOutcome);
    const keptwire::Row* w = KeptRowFind((unsigned)p.keptRow);
    if (w == 0) return;
    if (how == keptwire::kHowMade)
    {
        void* host = p.m.hostKey.empty() ? 0 : ObjectByPositionKey(p.m.hostKey.c_str());
        void* b = p.keptMade;   /* the piece the make produced - a search by key could find another piece standing within 0.1 */
        if (b != 0 && w->p.destroyed != 0) { DoorsForgetOwner(b); BdDestroyPod(b, true); }   /* the building's door rows go before its doors do */
        if (b != 0 && w->p.broken != BdBrokenRead(b)) BdSetBrokenPod(b, w->p.broken != 0);
        if (b != 0 && (w->p.destroyed != 0 || w->p.broken != 0)) BdKeptNoteBroken(p.m.key, b);   /* the registry row takes the live ruin and broken flags */
        if (b != 0) BdKeptRuinDoors(*w);
        if (b != 0) BdKeptExtrasNow(*w, L, b, "this player's own piece made again");
        if (b != 0) BdKeptOverlap(*w, L, b, host);
        if (b != 0 && p.m.hostKey.empty() && w->p.destroyed == 0) BdKeptTownPlace(*w, b, LocalPlayerFaction());   /* its outpost: taken or founded, then linked - furniture (a host) keeps its host's town and a ruin joins none, so no town is taken for either */
    }
    if (how == keptwire::kHowPresent) L.extrasDone = keptwire::ExtrasWanted(w->p);   /* a piece that already stood keeps its own state */
    if (how == keptwire::kHowMade) ++g_keptMade; else if (how == keptwire::kHowPresent) ++g_keptPresent; else ++g_keptRefused;
    BdKeptPieceLaid(*w, L, how, how == keptwire::kHowRefused ? std::string() : p.m.key,
               how == keptwire::kHowMade ? "this player's own piece made again (the restore road)" : (how == keptwire::kHowPresent ? "a piece already stands at its key - nothing made" : "the restore road could not make it - the claim is handed back"));
}
/* a kept own-slot restore taken off the restore road before it finished (a REMOVE dropped it, or another game's PLACE of the same key
   took its entry): its queued mark is cleared and its claim handed back (LAID refused), so the row is not left waiting for it */
void BdKeptPendDropped(BdPending& p)
{
    if (p.keptRow < 0) return;
    BdPending k = p;
    k.rsOutcome = 0;
    BdKeptPendDone(k);
    p.keptRow = -1;
}
/* MAIN THREAD: this player's own record (pp.build) of a piece key. 1 = a row read: *complete / *destroyed its finished and ruin flags;
   0 = no row, or one that does not read as this key's STATE; -1 = the record cannot be read now (asked again later) */
int BdOwnRecordFlags(const std::string& key, int* complete, int* destroyed)
{
    *complete = 0; *destroyed = 0;
    std::string st;
    const int r = OwnBuildRecordState(key, &st);
    if (r <= 0) return r;
    coopbuild::BuildMsg m;
    if (coopbuild::DecodeBuildSaved(st.data(), st.size(), &m) != coopbuild::kBuildDecodeOk || m.kind != coopbuild::kBuildState || m.key != key) return 0;
    *complete = m.complete != 0 ? 1 : 0;
    *destroyed = m.destroyed != 0 ? 1 : 0;
    return 1;
}
/* MAIN THREAD: the walls the engine's wall completion (0x561A20) completes along with `live` - every wall sharing its record - read
   for coopbuild::BuildKeptOwnCompleteVerdict. *grouped 0 = the record is live's alone. Otherwise the loaded buildings of live's key
   sector and the eight around it are walked: *found = the walls sharing the record (the lead among them), *leadCount = the lead's group
   count (-1 lead not found), *allOwnFinished = 1 when every one of them is this player's, standing whole, and held finished and not a
   ruin by this player's own record, and no sector's list was cut or unreadable. 1 = read, 0 = not readable (taken as not finished),
   -1 = a record cannot be read now or a zone went away during the walk (asked again later). */
int BdKeptWallGroupRead(void* live, const std::string& lk, int* grouped, int* allOwnFinished, int* found, int* leadCount)
{
    *grouped = 1; *allOwnFinished = 0; *found = 0; *leadCount = -1;
    const int alone = BdWallOwnRecord(live);
    if (alone < 0) return 0;
    if (alone == 1) { *grouped = 0; return 1; }
    const uintptr_t rec = BdWallRecordPod(live);
    int sx = 0, sy = 0;
    if (rec == 0 || BoxKeySector(lk.c_str(), &sx, &sy) == 0) return 0;
    void* mine = (void*)LocalPlayerFaction();
    if (mine == 0) return 0;
    static void* list[4096];
    const long long gen0 = BoxWalkGen();
    int all = 1;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
        {
            int trunc = 0;
            const int m = LoadedBuildingsInSector(sx + dx, sy + dy, list, 4096, &trunc);
            if (m < 0 || trunc != 0) all = 0;   /* a list that could not be read whole may miss a member */
            for (int i = 0; i < m; ++i)
            {
                void* b = list[i];
                if (BdIsWall(b) == 0 || BdWallRecordPod(b) != rec) continue;
                ++*found;
                if (rec == (uintptr_t)b + kWallOwnRecord) *leadCount = BdWallGroupCountPod(b);
                float p = 0, n = 0;
                int c = 0, d = 0, x = 0;
                if (BdOwnerPod(b) != mine || BdReadState(b, &p, &n, &c, &d, &x) == 0 || d != 0 || x != 0) { all = 0; continue; }
                char k[kKeyCap];
                k[0] = 0;
                if (ObjectPositionKey(b, k, kKeyCap, "", 3, 0) == 0) { all = 0; continue; }
                int rc = 0, rx = 0;
                const int fr = BdOwnRecordFlags(std::string(k), &rc, &rx);
                if (fr < 0) return -1;
                if (fr == 0 || rc == 0 || rx != 0) all = 0;
            }
        }
    if (BoxWalkGen() != gen0) return -1;
    *allOwnFinished = all;
    return 1;
}
/* MAIN THREAD (the kept piece lay): this player's own unfinished piece standing at a kept row's key whose own record (pp.build) holds it
   finished. coopbuild::BuildKeptOwnCompleteVerdict decides; when it says so the piece is completed through the engine's complete-now
   path (vt+0x230 FLT_MAX) and the flag read back, else the refusal is counted (keptOwnRefused). -1 = asked again later. */
int BdKeptOwnComplete(const keptwire::Row& w, void* live, const std::string& lk, int liveDestroyed, int recComplete, int recDestroyed)
{
    const int wall = BdIsWall(live);
    int grouped = 0, allOwn = 0, found = 0, leadCount = -1;
    if (wall != 0 && liveDestroyed == 0 && recComplete != 0 && recDestroyed == 0)
    {
        const int gr = BdKeptWallGroupRead(live, lk, &grouped, &allOwn, &found, &leadCount);
        if (gr < 0) return -1;
        if (gr == 0) { grouped = 1; allOwn = 0; }
    }
    if (wall != 0 && liveDestroyed == 0 && recComplete != 0 && recDestroyed == 0 && grouped != 0 && allOwn != 0 && (leadCount < 0 || found - 1 < leadCount))
        return -1;   /* every wall found is this player's and finished, but not all of its group is loaded or found yet: asked again later */
    char line[500];
    if (coopbuild::BuildKeptOwnCompleteVerdict(wall, liveDestroyed, recComplete, recDestroyed, grouped, allOwn, found, leadCount) == 0)
    {
        if (recComplete == 0) return 0;
        ++g_keptOwnRefused;
        char why[160];
        if (wall == 0) _snprintf_s(why, sizeof(why), _TRUNCATE, "not a wall (only walls are saved as blueprints)");
        else if (liveDestroyed != 0 || recDestroyed != 0) _snprintf_s(why, sizeof(why), _TRUNCATE, "a ruin (here=%d, in the record=%d)", liveDestroyed, recDestroyed);
        else _snprintf_s(why, sizeof(why), _TRUNCATE, "the engine would complete its whole wall group (%d sharing the record, lead's count %d, all this player's and recorded finished=%d)", found, leadCount, allOwn);
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u piece key=%.63s: this player's own record holds it finished, left unfinished - %s; keptOwnRefused=%lld",
                    w.row, lk.c_str(), why, g_keptOwnRefused);
        BdKeptLog(line);
        return 0;
    }
    const int ok = BdProgressPod(live, FLT_MAX);
    float cp = 0, cn = 0;
    int cc = 0, cd = 0, cx = 0;
    const int rd = BdReadState(live, &cp, &cn, &cc, &cd, &cx);
    if (ok != 0 && rd != 0 && cc != 0) ++g_keptOwnRecComplete;
    else if (ok != 0) ++g_wallCompleteNoop;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u piece key=%.63s: this player's own record holds it finished and it stood unfinished - vt+0x230(FLT_MAX) %s, now %.2f/%.2f complete=%d wall%s; keptOwnRecComplete=%lld wallCompleteNoop=%lld",
                w.row, lk.c_str(), ok != 0 ? "ran" : "FAULTED", cp, cn, cc, grouped != 0 ? " (its group with it)" : "", g_keptOwnRecComplete, g_wallCompleteNoop);
    BdKeptLog(line);
    return 0;
}
/* THE PIECE PART of one row whose claim is this game's: search, then lay */
void BdKeptPieceWork(const keptwire::Row& w, BdKeptLoc& L, int mySlot, int sx, int sy)
{
    char line[700];
    const int furn = w.p.hostKey.empty() ? 0 : 1;
    std::map<int, unsigned>::const_iterator al = g_keptAreaLay.find(sx * 64 + sy);
    if (al != g_keptAreaLay.end() && g_drainNo < al->second + kRetryGap) return;   /* a lay in this area just now: what it made settles first */
    void* host = 0;
    float hostPos[3], hostQ[4];
    for (int i = 0; i < 3; ++i) hostPos[i] = 0.0f;
    hostQ[0] = 1.0f; hostQ[1] = hostQ[2] = hostQ[3] = 0.0f;
    const char* why = 0;
    if (g_keptSearches >= kKeptSearchPerDrain) return;
    if (furn != 0) { ++g_keptSearches; if (BdKeptHost(w, &host, hostPos, hostQ, &why) == 0) host = 0; }
    else if (ZoneBuildingsInHereTri(w.p.pos[0], w.p.pos[1], w.p.pos[2]) <= 0) why = "its area is not loaded here";
    if (why != 0)
    {
        if (L.saidWait != 1) { L.saidWait = 1; _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u piece waits key=%.63s: %s%s - looked at again every %u safe points", w.row, w.p.key.c_str(), furn != 0 ? "its host: " : "", why, kRetryGap); BdKeptLog(line); }
        L.nextLook = g_drainNo + kRetryGap;
        return;
    }
    if (g_keptSearches >= kKeptSearchPerDrain) return;
    ++g_keptSearches;
    /* the drift-tolerant search every part uses (ObjectByPositionKey: free-standing pieces and indoor furniture, +/-0.1 on x and z);
       furniture it does not find is looked for again in its host's own list, read whole, with the same window */
    void* live = ObjectByPositionKey(w.p.key.c_str());
    int lv = ObjectByPositionKeyVerdict();
    if (furn != 0 && live == 0) live = BdFindInHostNear(host, w.p.key.c_str(), &lv);
    if (live == 0 && lv != kObjKeyNone)
    {
        if (L.saidWait != 2) { L.saidWait = 2; _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u piece match key=%.63s: %s - nothing made, searched again every %u safe points", w.row, w.p.key.c_str(), BdVerdictWord(lv), kRetryGap); BdKeptLog(line); }
        L.nextLook = g_drainNo + kRetryGap;
        return;
    }
    if (live != 0)
    {
        float lp = 0, ln = 0;
        int lc = 0, ld = 0, lx = 0;
        BdReadState(live, &lp, &ln, &lc, &ld, &lx);
        if (ld != 0) { L.nextLook = g_drainNo + kRetryGap; return; }   /* being dismantled: looked at again once it is gone */
        void* o = BdOwnerPod(live);
        const int recSlot = (o != 0) ? StandInRecordSlot((::Faction*)o) : -3;   /* a player's faction from the save (its record id names a slot, or the old two-player id) not yet in the stand-in table */
        const int ownerIsPlayer = (o != 0 && (o == (void*)LocalPlayerFaction() || IsStandInFaction((::Faction*)o) || recSlot >= 0 || recSlot == -2)) ? 1 : 0;   /* a player's piece is never handed to the row's owner */
        char k[kKeyCap];
        k[0] = 0;
        const std::string lk = ObjectPositionKey(live, k, kKeyCap, "", 3, 0) != 0 ? std::string(k) : w.p.key;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u piece match key=%.63s: found %.63s owned by '%.60s'", w.row, w.p.key.c_str(), lk.c_str(), BdFactionName(o).c_str());
        BdKeptLog(line);
        if (ownerIsPlayer == 0 && BdKeptHandBack(w, live, furn, host, hostPos, hostQ, mySlot) != 0)
        {
            BdKeptExtrasNow(w, L, live, "handed back");
            BdKeptPieceLaid(w, L, keptwire::kHowHandedBack, lk, "an unowned matching town piece given back to its player at no charge");
            return;
        }
        if (ownerIsPlayer == 0) { L.nextLook = g_drainNo + kRetryGap; return; }   /* its player's faction is not here yet: the hand-back waits */
        if (w.p.ownerSlot == mySlot && lc == 0 && o == (void*)LocalPlayerFaction())
        {   /* a save made by a game that held only copies keeps walls as blueprints (wall copies were never finished there); this
               player's own record is the writer's word for its own wall (coopbuild::BuildKeptOwnCompleteVerdict) */
            int rc = 0, rx = 0;
            const int oc = BdOwnRecordFlags(w.p.key, &rc, &rx);
            if (oc < 0) { L.nextLook = g_drainNo + kRetryGap; return; }   /* the record is being written: looked at again */
            if (oc > 0 && rc != 0 && BdKeptOwnComplete(w, live, lk, lx, rc, rx) < 0) { L.nextLook = g_drainNo + kRetryGap; return; }
        }
        ++g_keptPresent;
        L.extrasDone = keptwire::ExtrasWanted(w.p);   /* a piece that already stood keeps its own state */
        BdKeptTownPlace(w, live, BdKeptTownFaction(w, mySlot));   /* its outpost: taken or founded, then linked - a piece inside a building keeps its host's town */
        BdKeptPieceLaid(w, L, keptwire::kHowPresent, lk, "a matching piece already stands there - nothing made");
        return;
    }
    /* none: made */
    float pos[3], rot[4];
    if (BdKeptInputs(w, furn, hostPos, hostQ, pos, rot) == 0) { L.nextLook = g_drainNo + kRetryGap; return; }   /* no ground height there yet */
    if (w.p.ownerSlot == mySlot)   /* the overlap check runs once the piece stands (BdKeptPendDone for this player's own, below for another's) */
    {
        for (size_t j = 0; j < g_pend.size(); ++j) if (g_pend[j].m.key == w.p.key) { L.nextLook = g_drainNo + kRetryGap; return; }
        for (size_t j = 0; j < g_wait.size(); ++j) if (g_wait[j].m.key == w.p.key) { L.nextLook = g_drainNo + kRetryGap; return; }
        if ((int)(g_pend.size() + g_wait.size()) >= kPendCap) { L.nextLook = g_drainNo + kRetryGap; return; }
        int keptComplete = (w.p.complete != 0) ? 1 : 0;
        if (keptComplete == 0)
        {   /* a kept row from a save made by a game that held only copies records a wall as a blueprint (wall copies were never finished
               there); this player's own record is the writer's word for its own piece - made finished when it says so and neither it nor
               the kept row is a ruin. The piece is made here by the restore road (createBuilding, not a placement), so a wall made
               this way keeps its own record and its completion reaches no other wall. */
            int rc = 0, rx = 0;
            const int oc = BdOwnRecordFlags(w.p.key, &rc, &rx);
            if (oc < 0) { L.nextLook = g_drainNo + kRetryGap; return; }   /* the record is being written: looked at again */
            if (oc > 0 && rc != 0 && rx == 0 && w.p.destroyed == 0)
            {
                keptComplete = 1;
                ++g_keptOwnRecComplete;
                _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u piece key=%.63s: the kept row says unfinished, this player's own record says finished - made complete=1; keptOwnRecComplete=%lld",
                            w.row, w.p.key.c_str(), g_keptOwnRecComplete);
                BdKeptLog(line);
            }
        }
        /* its outpost is taken or founded once the restore road has made the piece (BdKeptPendDone): a refused piece leaves no empty town */
        BdPending p;
        p.m.kind = coopbuild::kBuildPlace; p.m.key = w.p.key; p.m.sid = w.p.tmpl;
        for (int i = 0; i < 3; ++i) p.m.pos[i] = pos[i];
        for (int i = 0; i < 4; ++i) p.m.rot[i] = rot[i];
        p.m.complete = (unsigned char)keptComplete; p.m.progress = w.p.progress; p.m.needed = 0.0f;
        p.m.ownerSlot = coopbuild::kBuildOwnerSender; p.m.nonce = BdNewNonce();
        if (furn != 0)
        {
            char hk[kKeyCap];
            hk[0] = 0;
            if (ObjectPositionKey(host, hk, kKeyCap, "", 3, 0) == 0) { L.nextLook = g_drainNo + kRetryGap; return; }
            p.m.hostKey = hk; p.m.hostForm = coopbuild::kBuildHostPlain; p.m.floor = w.p.floor; p.m.outside = 0;
        }
        p.tries = 0; p.waitLogged = 0; p.nextTry = 0; p.hostLogged = 0; p.slotWaitCounted = 0;
        p.restore = 1; p.rsOutcome = 0;
        p.rsHasState = 1;
        p.st.kind = coopbuild::kBuildState; p.st.key = w.p.key; p.st.progress = w.p.progress; p.st.needed = 0.0f; p.st.complete = (unsigned char)keptComplete;
        p.st.nMats = (unsigned char)keptwire::MatsArray(w.p.mats, p.st.mats, (int)coopbuild::kBuildMaxMats);
        p.keptRow = (int)w.row;
        g_pend.push_back(p);
        L.pieceQueued = 1; ++g_keptQueued;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u piece none at key=%.63s: this player's own piece goes to the restore road (made at (%.1f, %.1f, %.1f)%s%s); queued=%lld",
                    w.row, w.p.key.c_str(), pos[0], pos[1], pos[2], furn != 0 ? " in host " : "", furn != 0 ? p.m.hostKey.c_str() : "", g_keptQueued);
        BdKeptLog(line);
        return;
    }
    ::Faction* to = OwnerFactionForSlot(w.p.ownerSlot, true);
    if (to == 0)
    {
        if (L.saidWait != 3) { L.saidWait = 3; _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u piece key=%.63s waits: no faction for its player (slot %d) here yet", w.row, w.p.key.c_str(), w.p.ownerSlot); BdKeptLog(line); }
        L.nextLook = g_drainNo + kRetryGap;
        return;
    }
    void* const town = (furn != 0) ? 0 : BdKeptTownFor(w, to);   /* its outpost first: taken or founded before the piece is made; none for a furniture row (it stands inside a building and keeps its host's town; a town founded for it would have no member, and the engine's player-town refresh destroys such a town) */
    const char* cwhy = 0;
    void* b = BdKeptCreate(w, furn, host, pos, rot, (void*)to, &cwhy);
    if (b == 0)
    {
        ++g_keptRefused;
        BdKeptLaid(w, L, keptwire::kPartPiece, keptwire::kHowRefused, std::string(), cwhy != 0 ? cwhy : "not made");
        L.nextLook = g_drainNo + kRetryGap;
        return;
    }
    BdKeptApplyState(w, b);
    BdKeptRuinDoors(w);
    if (furn == 0) BdKeptTownLink(w, b, town);   /* a furniture row is linked to no town: its town is its host's */
    BdKeptExtrasNow(w, L, b, "made");
    BdKeptOverlap(w, L, b, furn != 0 ? host : 0);
    char k2[kKeyCap];
    k2[0] = 0;
    const int k2ok = ObjectPositionKey(b, k2, kKeyCap, "", 3, 0);
    const int keySame = (k2ok != 0 && poskey::KeysSamePiece(w.p.key.c_str(), k2) != 0) ? 1 : 0;   /* the row's key (wire form) and the made piece's 64-byte key: one piece when their wire forms match */
    if (keySame == 0) ++g_copyKeyMismatch;
    BdOnNewPiece(b, 2, pos, rot, furn != 0 ? 3 : 1, furn != 0 ? host : 0, coopbuild::kBuildHostPlain, w.p.floor, 0, w.p.ownerSlot, 0);
    ItFurnMarkStale(2);
    ++g_keptMade;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "made as the player's own (owner '%.60s'), handed to it (owed until its game confirms)%s%.63s",
                BdFactionName(BdOwnerPod(b)).c_str(), keySame != 0 ? "" : " KEY MISMATCH live=", keySame != 0 ? "" : (k2ok != 0 ? k2 : "?"));
    BdKeptPieceLaid(w, L, keptwire::kHowMade, k2ok != 0 ? std::string(k2) : w.p.key, line);
}
/* THE ITEMS PART: the box's writer lays them into the live piece, in full and once on this game; their LAID goes at once and the world
   server never offers laid box contents to another game. Items made here that no place took wait in no inventory and are tried
   again by this game (BdKeptWaitTick); the laid-here record names them, so a reload of a save that does not hold them makes them again
   from the row (BdKeptRemake). A lay the record does not list that the row names as this game's (the save it runs on does not hold
   it) is claimed again and laid once more, only while the record is read. */
void BdKeptRemake(const keptwire::Row& w, BdKeptLoc& L)
{
    if (g_keptSearches >= kKeptSearchPerDrain) return;
    ++g_keptSearches;
    void* lb = ObjectByPositionKey(w.p.key.c_str());
    float lpos[3];
    if (lb == 0 || !BdCopyF((const char*)lb + kObjPos, lpos, 3)) { L.nextLook = g_drainNo + kRetryGap; return; }
    std::string why;
    const int made = ItKeptRemake(lb, lpos, w.p.key.c_str(), w.p.items, L.waitsToMake, &why);
    const unsigned named = (unsigned)L.waitsToMake.size();
    L.waitsToMake.clear();
    L.boxKey = w.p.key; L.itemsWaiting = 1; L.waitNext = g_drainNo + kRetryGap;   /* any that no place takes waits again (BdKeptWaitTick) */
    BdKeptHereSave();
    char line[800];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u items: %d kept item(s) the save this game runs on does not hold (they waited for a place; %u name(s) in its record) made again from the row%s%.600s",
                w.row, made, named, why.empty() ? "" : " - ", why.c_str());
    BdKeptLog(line);
}
void BdKeptItems(const keptwire::Row& w, BdKeptLoc& L, int mySlot)
{
    char line[700];
    if (!L.waitsToMake.empty()) { BdKeptRemake(w, L); return; }
    const unsigned me = (unsigned)mySlot + 1u;
    const unsigned st = keptwire::PartState(w.flags, keptwire::kPartItems);
    const int again = (st == keptwire::kStLaid && w.claimItems == me && L.itemsHow == 0) ? 1 : 0;   /* laid by this game, not in its record */
    if (st != keptwire::kStNew && !(st == keptwire::kStClaimed && w.claimItems == me) && again == 0) return;
    if (st != keptwire::kStClaimed && L.itemsClaimAt != 0 && g_drainNo < L.itemsClaimAt + kKeptClaimWait) return;
    if (g_keptSearches >= kKeptSearchPerDrain) return;
    ++g_keptSearches;
    void* live = ObjectByPositionKey(w.p.key.c_str());
    if (live == 0) { L.nextLook = g_drainNo + kRetryGap; return; }
    float pos[3];
    if (!BdCopyF((const char*)live + kObjPos, pos, 3)) { L.nextLook = g_drainNo + kRetryGap; return; }
    const int mine = ItKeptBoxMine(live, pos, w.p.key.c_str());
    const int step = keptwire::PartStep(w.flags, keptwire::kPartItems, w.claimItems, mySlot, mine, L.itemsHow != 0 ? 1 : 0, g_keptHereLoaded == kHereGood ? 1 : 0);
    if (step == keptwire::kStepClaim)
    {
        if (L.itemsClaimAt != 0) { _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u items claim not granted (refused or unanswered) - asked again", w.row); BdKeptLog(line); }
        if (KeptSendPart(keptwire::kOpClaim, w.row, keptwire::kPartItems)) L.itemsClaimAt = g_drainNo;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u items claim sent%s (this game writes the box at %.63s; %u item(s) kept)", w.row,
                    again != 0 ? " AGAIN: this game laid them and the save it runs on does not hold them (its laid-here record does not list them) - laid once more when granted" : "",
                    w.p.key.c_str(), (unsigned)w.p.items.size());
        BdKeptLog(line);
        return;
    }
    if (step == keptwire::kStepRelease)
    {
        KeptSendPart(keptwire::kOpRelease, w.row, keptwire::kPartItems);
        L.itemsClaimAt = 0;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u items claim given back: this game no longer writes that box", w.row);
        BdKeptLog(line);
        return;
    }
    if (step == keptwire::kStepReport) { BdKeptLaid(w, L, keptwire::kPartItems, L.itemsHow, w.p.key, "laid here in this world - reported, nothing touched"); return; }
    if (step != keptwire::kStepWork) return;
    if (L.itemsClaimAt != 0) { L.itemsClaimAt = 0; _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u items claim granted", w.row); BdKeptLog(line); }
    int laid = 0;
    std::string why;
    const int r = ItKeptLayItems(live, pos, w.p.key.c_str(), w.p.items, &laid, &why);
    if (r == 0) { L.nextLook = g_drainNo + kRetryGap; return; }
    if (r < 0) { BdKeptLaid(w, L, keptwire::kPartItems, keptwire::kHowRefused, std::string(), why.c_str()); return; }
    ++g_keptItemsLaid;
    if (r == 3) { L.boxKey = w.p.key; L.itemsWaiting = 1; L.waitNext = g_drainNo + kRetryGap; }   /* named in the record with the lay (BdKeptHereOf) */
    _snprintf_s(line, sizeof(line), _TRUNCATE, "%d of %u kept item(s) laid into the box%s%.500s%s", laid, (unsigned)w.p.items.size(), why.empty() ? "" : " - ", why.c_str(),
                r == 3 ? " - the items still waiting are tried again here" : "");
    BdKeptLaid(w, L, keptwire::kPartItems, keptwire::kHowItemsLaid, w.p.key, line);
}
/* KEPT ITEMS WAITING FOR A PLACE, tried again by the game that laid them while their box is loaded here (items.cpp ItKeptRetryWaits),
   whatever the link and the row's state; a change goes into this game's record */
void BdKeptWaitTick()
{
    for (std::map<unsigned, BdKeptLoc>::iterator it = g_kept.begin(); it != g_kept.end(); ++it)
    {
        BdKeptLoc& L = it->second;
        if (L.itemsWaiting == 0 || L.boxKey.empty() || g_drainNo < L.waitNext) continue;
        if (g_keptSearches >= kKeptSearchPerDrain) return;
        ++g_keptSearches;
        L.waitNext = g_drainNo + kRetryGap;
        void* lb = ObjectByPositionKey(L.boxKey.c_str());
        float lpos[3];
        if (lb == 0 || !BdCopyF((const char*)lb + kObjPos, lpos, 3)) continue;   /* not loaded here: no pass counted */
        std::string why;
        const int left = ItKeptRetryWaits(lb, lpos, L.boxKey.c_str(), &why);
        if (left <= 0) L.itemsWaiting = 0;
        if (!why.empty())
        {
            char line[800];
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u items: %.700s", it->first, why.c_str());
            BdKeptLog(line);
        }
        BdKeptHereSave();
    }
}
/* THE OWNER PART, on the owner's own game: its piece found (never made here by this part), registered as its own, the row's ruin and
   broken flag put on it (BdKeptOwnerState), and LAID owner once pp.build holding it is committed */
void BdKeptOwner(const keptwire::Row& w, BdKeptLoc& L)
{
    char line[700];
    if (L.ownerStep == 3 || L.ownerStep == 1) return;
    if (L.ownerStep == 2)
    {
        unsigned long long cs = 0;
        if (!OwnStoreCommittedSeq(coopown::BuildRecKey(), &cs) || cs < L.ownerSeq) return;
        int sx = -1, sy = -1;
        keptwire::SectorOfPiece(w.p, &sx, &sy);
        if (!KeptSendLaid(w.row, keptwire::kPartOwner, keptwire::kHowOwnerRegistered, std::string(), sx, sy)) return;
        L.ownerStep = 3; ++g_keptOwnerReported;
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u owner LAID how=owner registered key=%.63s: this player's records (pp.build, seq %llu) hold the piece as its own", w.row, w.p.key.c_str(), cs);
        BdKeptLog(line);
        return;
    }
    if (g_keptSearches >= kKeptSearchPerDrain) return;
    ++g_keptSearches;
    void* live = ObjectByPositionKey(w.p.key.c_str());
    if (live == 0 && !w.p.hostKey.empty())
    {   /* furniture the search missed - a piece with no inventory inside a building (a bed) is on neither list it walks: looked for in
           its host's own list, read whole, with the same window (as the piece-lay step) */
        void* h0 = 0;   /* the owner part: furniture the search missed */
        float hp0[3], hq0[4];
        const char* hw = 0;
        int hv = 0;
        if (BdKeptHost(w, &h0, hp0, hq0, &hw) != 0) live = BdFindInHostNear(h0, w.p.key.c_str(), &hv);
    }
    if (live == 0) { L.nextLook = g_drainNo + kRetryGap; return; }
    if (BdOwnerPod(live) != (void*)LocalPlayerFaction()) { L.nextLook = g_drainNo + kRetryGap; return; }   /* not this player's on this game yet (a hand-off still on its way) */
    char k[kKeyCap];
    k[0] = 0;
    if (ObjectPositionKey(live, k, kKeyCap, "", 3, 0) == 0) { L.nextLook = g_drainNo + kRetryGap; return; }
    const std::string lk(k);
    std::map<std::string, BdReg>::iterator r = g_reg.find(lk);
    if (r != g_reg.end() && r->second.via == 3) { L.nextLook = g_drainNo + kRetryGap; return; }
    if (r == g_reg.end() || r->second.own == 0)
    {
        const int furn = w.p.hostKey.empty() ? 0 : 1;
        void* host = 0;
        float hostPos[3], hostQ[4], in[3], rq[4], lp[3], lq[4];
        const char* why = 0;
        if (furn != 0 && BdKeptHost(w, &host, hostPos, hostQ, &why) == 0) { L.nextLook = g_drainNo + kRetryGap; return; }
        if (!BdCopyF((const char*)live + kObjPos, lp, 3) || BdOrientPod(live, lq) == 0) { L.nextLook = g_drainNo + kRetryGap; return; }
        if (furn != 0) keptwire::LocalOfWorld(hostPos, hostQ, lp, lq, in, rq);
        else { for (int i = 0; i < 3; ++i) in[i] = lp[i]; for (int i = 0; i < 4; ++i) rq[i] = lq[i]; }
        float p = 0, n = 0;
        int c = 0, d = 0, x = 0;
        BdReadState(live, &p, &n, &c, &d, &x);
        BdReg& nr = BdRegisterOwnLive(live, lk, in, rq, p, n, c, x, furn != 0 ? 0 : 1);
        if (furn != 0) { char hk[kKeyCap]; hk[0] = 0; if (ObjectPositionKey(host, hk, kKeyCap, "", 3, 0) != 0) nr.hostKey = hk; nr.hostForm = coopbuild::kBuildHostPlain; nr.floor = w.p.floor; nr.outside = 0; }
        g_rsOwePlace.push_back(lk);
    }
    BdKeptOwnerState(w, live, lk);
    BdKeptTownPlace(w, live, LocalPlayerFaction());   /* this player's own outpost in this world: taken or founded, then linked - a piece inside a building keeps its host's town */
    BdOwnDirty(2);
    L.ownerStep = 1;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u owner: this player's piece %.63s found here and registered as its own - reported once its records hold it", w.row, lk.c_str());
    BdKeptLog(line);
}
/* store.cpp handed pp.build to the writer as seq `seq`: an owner part registered before it waits for that seq's commit */
void BdKeptOwnerHanded(unsigned long long seq)
{
    if (seq == 0) return;
    for (std::map<unsigned, BdKeptLoc>::iterator it = g_kept.begin(); it != g_kept.end(); ++it)
        if (it->second.ownerStep == 1) { it->second.ownerStep = 2; it->second.ownerSeq = seq; }
}
/* one row */
void BdKeptRow(const keptwire::Row& w, int mySlot)
{
    BdKeptLoc& L = g_kept[w.row];
    if (L.nextLook > g_drainNo) return;
    int sx = -1, sy = -1;
    keptwire::SectorOfPiece(w.p, &sx, &sy);
    L.sx = sx; L.sy = sy;
    Sector sec; sec.x = sx; sec.y = sy;
    const unsigned pst = keptwire::PartState(w.flags, keptwire::kPartPiece);
    char line[400];
    if (L.pieceQueued == 0 && L.refusals < kKeptRefuseCap)
    {
        const int held = (MineHeldTS(sec) == 1) ? 1 : 0;
        const int step = keptwire::PartStep(w.flags, keptwire::kPartPiece, w.claimPiece, mySlot, held, L.pieceHow != 0 ? 1 : 0, g_keptHereLoaded == kHereGood ? 1 : 0);
        if (step == keptwire::kStepClaim && (L.pieceClaimAt == 0 || g_drainNo >= L.pieceClaimAt + kKeptClaimWait))
        {
            if (L.pieceClaimAt != 0) { _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u piece claim not granted (refused or unanswered) - asked again", w.row); BdKeptLog(line); }
            if (KeptSendPart(keptwire::kOpClaim, w.row, keptwire::kPartPiece))
            {
                L.pieceClaimAt = g_drainNo; L.saidGrant = 0;
                _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u piece claim sent%s key=%.63s ('%.60s', owner slot %d, area %d,%d - this game holds the area)", w.row,
                            pst == keptwire::kStLaid ? " AGAIN: this game laid it and the save it runs on does not hold it (its laid-here record does not list it) - laid once more when granted" : "",
                            w.p.key.c_str(), w.p.tmpl.c_str(), w.p.ownerSlot, sx, sy);
                BdKeptLog(line);
            }
        }
        else if (step == keptwire::kStepRelease)
        {
            KeptSendPart(keptwire::kOpRelease, w.row, keptwire::kPartPiece);
            L.pieceClaimAt = 0;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u piece claim given back: this game no longer holds area %d,%d", w.row, sx, sy);
            BdKeptLog(line);
        }
        else if (step == keptwire::kStepReport)
            BdKeptLaid(w, L, keptwire::kPartPiece, L.pieceHow, L.pieceKey.empty() ? w.p.key : L.pieceKey, "laid here before in this world - reported again, nothing made");
        else if (step == keptwire::kStepWork)
        {
            if (L.saidGrant == 0) { L.saidGrant = 1; L.pieceClaimAt = 0; _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u piece claim granted", w.row); BdKeptLog(line); }
            BdKeptPieceWork(w, L, mySlot, sx, sy);
        }
        else if (L.pieceClaimAt != 0 && pst != keptwire::kStNew && w.claimPiece != (unsigned)mySlot + 1u)
        {
            L.pieceClaimAt = 0;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] row %u piece claim refused: the part is %s%s", w.row, keptwire::StateName(pst), w.claimPiece != 0 ? " by another game" : "");
            BdKeptLog(line);
        }
    }
    L.extrasWant = keptwire::ExtrasWanted(w.p);
    L.layHere = keptwire::ExtrasLayHere(w.flags, L.pieceHow);
    const int laidHere = (L.pieceHow != 0 && L.pieceHow != keptwire::kHowRefused) ? 1 : 0;   /* this game laid the piece: its parts are laid here while any remain */
    if ((pst == keptwire::kStLaid || laidHere != 0) && L.layHere != 0) BdKeptExtrasStep(w, L);
    if (!w.p.items.empty() && (pst >= keptwire::kStLaid || !L.waitsToMake.empty())) BdKeptItems(w, L, mySlot);
    if (w.p.ownerSlot == mySlot && (w.flags & keptwire::kFlagOwnerTook) == 0 && pst >= keptwire::kStLaid) BdKeptOwner(w, L);
}
/* this game's table of rows worked (and the kept marks on the restore road) dropped: a world teardown, or the store dropped its
   tables for another world */
long g_keptWorldSeen = 0;
void BdKeptDropTable(const char* why)
{
    if (!g_kept.empty())
    {
        char line[240];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] %s: this game's table of %u kept row(s) worked here is dropped", why, (unsigned int)g_kept.size());
        DebugLog(std::string(line));
    }
    ItKeptForget(why);
    g_kept.clear(); g_keptAreaLay.clear();
    BdKeptHereTeardown();   /* the laid-here record: written, its unsaved changes dropped or left to the save in flight, read again */
    for (size_t i = 0; i < g_pend.size(); ++i) g_pend[i].keptRow = -1;
    for (size_t i = 0; i < g_wait.size(); ++i) g_wait[i].keptRow = -1;
    if ((!g_townRows.empty() || !g_townKeyIdx.empty()) && g_townRowsGen != KeptWorldGen())
    {   /* the kept outposts came on another world's link: they go with its rows (a world teardown of the same world keeps them - the
           world server sends them once a link) */
        char tl[240];
        _snprintf_s(tl, sizeof(tl), _TRUNCATE, "[KEPT] %s: the outpost list of another world (%u outpost(s)) is dropped", why, (unsigned)g_townRows.size());
        DebugLog(std::string(tl));
        g_townRows.clear(); g_townKeyIdx.clear(); g_townLive.clear(); g_townFailed.clear(); g_townLoadedCursor = 0;
        g_townLoadedMiss.clear();   /* the looks counted against that list's keys */
    }
}
void BdKeptDrain()
{
    KeptStoreTick();
    g_keptSearches = 0;
    if (KeptWorldGen() != g_keptWorldSeen) { g_keptWorldSeen = KeptWorldGen(); BdKeptDropTable("the world server's link names another world"); }
    BdKeptWaitTick();   /* this game's waiting kept items, whatever the link and the rows */
    /* the outposts: the SAY and the loaded pieces' links run at the top of BuildCopyDrain, once a drain (BdTownsSayTick, BdTownsLoadedTick) */
    if (KeptRowsActive() == 0) return;
    const int mySlot = KeptMySlot();
    if (mySlot < 0) return;
    if (g_keptHereLoaded == kHereUnread || g_keptHereWorld != KeptWorldIdNow()) BdKeptHereLoad();   /* this game's record, before any row is worked */
    static std::vector<unsigned> order;   /* MAIN THREAD: the row numbers; each row is found again by its number (no row or item copied) */
    KeptRowNumbers(&order);
    std::set<std::string> hostKeys;
    for (size_t i = 0; i < order.size(); ++i)
    {
        const keptwire::Row* w = KeptRowFind(order[i]);
        if (w != 0 && !w->p.hostKey.empty()) hostKeys.insert(w->p.hostKey);
    }
    for (int pass = keptwire::kOrderHost; pass <= keptwire::kOrderFurniture; ++pass)
        for (size_t i = 0; i < order.size(); ++i)
        {
            const keptwire::Row* w = KeptRowFind(order[i]);
            if (w == 0 || (keptwire::RowDone(w->flags) != 0 && BdKeptRowWorkLeft(w->row) == 0)) continue;
            if (keptwire::LayOrder(w->p.hostKey.empty() ? 0 : 1, hostKeys.find(w->p.key) != hostKeys.end() ? 1 : 0) != pass) continue;
            BdKeptRow(*w, mySlot);
        }
}
void BdKeptForgetWorld()
{
    BdKeptDropTable("world teardown");
    g_townLive.clear();   /* the towns of the world torn down are gone; the kept outposts are looked for again in the next one */
    g_townFailed.clear();
    KeptForgetWorld();   /* store.cpp: this world's lays waiting to be settled go too */
}
/* keptlist: one line per kept item (at every depth) that carries a uniform, a crafter or an owner ticket - the values the area
   record held, as the kept lay writes them */
void BdKeptListMarks(const keptwire::Row& w, const std::vector<zonekeep::KeptItem>& items, int depth)
{
    for (size_t j = 0; j < items.size(); ++j)
    {
        const zonekeep::KeptItem& it = items[j];
        if (!it.uniform.empty() || !it.crafter.empty() || it.hasOwner != 0)
        {
            char l[700];
            _snprintf_s(l, sizeof(l), _TRUNCATE, "[KEPT] keptlist row=%u item d=%d %.60s x%d uniform=%.60s crafter='%.60s' owner=%s%d/%d/%d/%d/%d", w.row, depth, it.tmpl.c_str(), it.quantity,
                        it.uniform.empty() ? "-" : it.uniform.c_str(), it.crafter.c_str(), it.hasOwner != 0 ? "" : "(none kept) ", it.owner[0], it.owner[1], it.owner[2], it.owner[3], it.owner[4]);
            DebugLog(std::string(l));
        }
        BdKeptListMarks(w, it.inner, depth + 1);
    }
}
/* TEST-ONLY, read-only: keptlist - one line per kept row this game holds, then a summary. MAIN THREAD */
std::string BuildKeptListCommand()
{
    std::vector<keptwire::Row> rows;
    KeptRowsCopy(&rows);
    const int mySlot = KeptMySlot();
    long long found = 0, none = 0, ambiguous = 0, notLoaded = 0, pDone = 0, iDone = 0, oTook = 0;
    char line[900];
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const keptwire::Row& w = rows[i];
        void* live = ObjectByPositionKey(w.p.key.c_str());
        int v = ObjectByPositionKeyVerdict();
        if (live == 0 && !w.p.hostKey.empty())
        {   /* as the kept steps: furniture looked for in its host's own list (a bed inside a house is on neither list the search walks) */
            void* h0 = 0;   /* keptlist: furniture the search missed */
            float hp0[3], hq0[4];
            const char* hw = 0;
            int hv = v;
            if (BdKeptHost(w, &h0, hp0, hq0, &hw) != 0) { live = BdFindInHostNear(h0, w.p.key.c_str(), &hv); v = hv; }
        }
        const char* sr = live != 0 ? "found" : (v == kObjKeyNone ? "none" : (v == kObjKeyAmbiguous ? "ambiguous" : "not loaded"));
        if (live != 0) ++found; else if (v == kObjKeyNone) ++none; else if (v == kObjKeyAmbiguous) ++ambiguous; else ++notLoaded;
        const unsigned ps = keptwire::PartState(w.flags, keptwire::kPartPiece), is = keptwire::PartState(w.flags, keptwire::kPartItems);
        if (ps == keptwire::kStDone) ++pDone;
        if (is == keptwire::kStDone) ++iDone;
        if ((w.flags & keptwire::kFlagOwnerTook) != 0) ++oTook;
        const int box = (live != 0 && !w.p.items.empty()) ? ItKeptBoxCount(live) : -1;
        std::map<unsigned, BdKeptLoc>::const_iterator L = g_kept.find(w.row);
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] keptlist row=%u key=%.63s sid=%.60s ownerSlot=%d piece=%s items=%s owner=%s flags=0x%02x claimPiece=%u claimItems=%u search=%s liveOwner='%.60s' boxItems=%d keptItems=%u laidHere=%s/%s extrasWant=0x%x extrasDone=0x%x extrasEnded=0x%x doorsDone=0x%x present=%d itemsWaiting=%d",
                    w.row, w.p.key.c_str(), w.p.tmpl.c_str(), w.p.ownerSlot, keptwire::StateName(ps), keptwire::StateName(is), (w.flags & keptwire::kFlagOwnerTook) != 0 ? "took" : "not yet",
                    w.flags, w.claimPiece, w.claimItems, sr, live != 0 ? BdFactionName(BdOwnerPod(live)).c_str() : "-", box, (unsigned)w.p.items.size(),
                    (L != g_kept.end() && L->second.pieceHow != 0) ? keptwire::HowName(L->second.pieceHow) : "-", (L != g_kept.end() && L->second.itemsHow != 0) ? keptwire::HowName(L->second.itemsHow) : "-",
                    keptwire::ExtrasWanted(w.p), L != g_kept.end() ? L->second.extrasDone : 0u, L != g_kept.end() ? L->second.extrasEnded : 0u,
                    L != g_kept.end() ? L->second.doorsDone : 0u, (w.flags & keptwire::kFlagPiecePresent) != 0 ? 1 : 0, L != g_kept.end() ? L->second.itemsWaiting : 0);
        DebugLog(std::string(line));
        BdKeptListMarks(w, w.p.items, 1);
        unsigned packs = 0, packItems = 0;
        for (size_t j = 0; j < w.p.items.size(); ++j) if (!w.p.items[j].inner.empty()) { ++packs; packItems += (unsigned)w.p.items[j].inner.size(); }
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] keptlist row=%u kept/live destroyed=%d/%d broken=%d/%d keptPacks=%u keptPackItems=%u keptDoors=%u",
                    w.row, w.p.destroyed, live != 0 ? BdByteAtOr0(live, kBDestroyed) : -1, w.p.broken, live != 0 ? BdBrokenRead(live) : -1, packs, packItems,
                    (unsigned)w.p.doors.size());
        DebugLog(std::string(line));
        for (size_t j = 0; j < w.p.doors.size(); ++j)
        {
            const zonekeep::KeptDoor& d = w.p.doors[j];
            void* door = (live != 0) ? DoorsKeptDoorAt(live, d.index) : 0;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[KEPT] keptlist row=%u door %d kept state=%d locked=%d wants=%d lockLvl=%d gate=%d ownerSlot=%d - live %.60s owner='%.60s'",
                        w.row, d.index, d.state, d.locked, d.wantsLock, d.lockLevel, d.gateCode, d.ownerSlot, door != 0 ? DoorsKeptRead(door).c_str() : "(no door here)",
                        door != 0 ? BdFactionName(BdOwnerPod(door)).c_str() : "-");
            DebugLog(std::string(line));
        }
    }
    _snprintf_s(line, sizeof(line), _TRUNCATE, "rows=%u mySlot=%d found=%lld none=%lld ambiguous=%lld notLoaded=%lld pieceDone=%lld itemsDone=%lld ownerTook=%lld made=%lld present=%lld handedBack=%lld queued=%lld refused=%lld overlaps=%lld itemsLaid=%lld ownerReported=%lld hereWrites=%lld hereWriteFails=%lld ",
                (unsigned int)rows.size(), mySlot, found, none, ambiguous, notLoaded, pDone, iDone, oTook, g_keptMade, g_keptPresent, g_keptHandedBack, g_keptQueued, g_keptRefused, g_keptOverlaps,
                g_keptItemsLaid, g_keptOwnerReported, g_keptHereWrites, g_keptHereWriteFails);
    const std::string sum = std::string(line) + KeptStoreText();
    DebugLog("[KEPT] keptlist " + sum);
    return "ok keptlist " + sum;
}

/* T-805 (owner 797 a), MAIN THREAD (the K2 safe point, the AI worker paused): the piece owner's side of a DISMANTLE_ASK. The asker
   must share this player's team now (team.h, this game's copy of the table), the piece must be loaded here, this game's own and not
   dismantled, and not a wall. Then the engine's own dismantle (Building::confirmDismantle 0x54F410 with answer 2 - vt+0x248 FLT_MAX, and its mounted buildings destroyed with it) runs on it: the refund
   drops its materials at the piece (the ground road carries them to every game) and the dismantled flip sends the REMOVE to every game
   (BuildTick), as for this player's own dismantle. The verdict goes back to the asking game. */
/* T-805 fold, guarded reads only: the interior (+0x1F0) holds furniture - its count +0x108 >= 1, the test the engine's dismantle job
   makes before "I have to dismantle the furniture first." (decomp 351860). 1 = furniture, 0 = none or no interior, -1 unreadable. */
int BdInteriorFurnPod(void* b)
{
    __try
    {
        const char* in = *(const char* const*)((const char*)b + kBInterior);
        if (in == 0) return 0;
        return (*(const int*)(in + 0x108) >= 1) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* T-805 fold, MAIN THREAD: what the owner's game reads of a piece before a member's dismantle. *wall = a wall, or a class that cannot be
   read (refused as one); *blocked = furniture in its interior (unreadable counts), or a research bench (the panel will not delete one
   holding items, 54d0a0:68; its items are not read here, so every bench is refused). A box's contents do not refuse it (the job's own
   rule). Mounted buildings are not read here: the job counts them with 0x54AF40, which has no reader in this project. */
void BdDismantleReads(void* b, int* wall, int* blocked)
{
    *wall = 1; *blocked = 1;
    const unsigned int vt = PolicyVtableRvaPod(b);
    char cls[48];
    cls[0] = 0;
    if (vt == 0 || PolicyClassNamePod(vt, cls, (int)sizeof cls) == 0) return;
    *wall = (std::strcmp(cls, ".?AVWallBuilding@@") == 0) ? 1 : 0;
    *blocked = (BdInteriorFurnPod(b) != 0 || std::strcmp(cls, ".?AVResearchBuilding@@") == 0) ? 1 : 0;
}
void BdDismantleAskDrain()
{
    if (g_dmAskIn.empty() || EngineWritesBlocked()) return;
    std::vector<BdDmAsk> in;
    in.swap(g_dmAskIn);
    const int me = MySlotForWire();
    ::Faction* mine = LocalPlayerFaction();
    for (size_t i = 0; i < in.size(); ++i)
    {
        const std::string key = in[i].key;
        const int from = in[i].slot;
        const int member = (from >= 0 && me >= 0 && from != me && TeamSameAnyThread(me, from)) ? 1 : 0;
        std::map<std::string, BdReg>::iterator it = g_reg.find(key);
        if (it == g_reg.end()) it = BdRowByLiveKey(key);   /* T-805 fold: a live key the row carries as its alias */
        const std::string rowKey = (it != g_reg.end()) ? it->first : key;
        void* b = 0;
        int tv = 0;
        if (it != g_reg.end()) b = BdFindPiece(rowKey, it->second, &tv);
        char k2[kKeyCap];
        k2[0] = 0;
        if (b != 0 && (ObjectPositionKey(b, k2, kKeyCap, "", 3, 0) == 0 || (rowKey != std::string(k2) && it->second.liveKey != std::string(k2)))) b = 0;
        const int own = (b != 0 && it->second.own != 0 && it->second.via != 3 && it->second.removed == 0 && mine != 0 && BdOwnerPod(b) == (void*)mine) ? 1 : 0;
        float p0 = 0, n0 = 0;
        int c0 = 0, d0 = 0, x0 = 0;
        if (b != 0) BdReadState(b, &p0, &n0, &c0, &d0, &x0);
        int wall = 1, blocked = 1;
        if (b != 0) BdDismantleReads(b, &wall, &blocked);
        int v = coopbuild::BuildDismantleAskVerdict(member, b != 0 ? 1 : 0, own, d0, wall, blocked);
        int ret = -1, d1 = d0;
        if (v == coopbuild::kBuildDmDone)
        {
            if (BdConfirmDismantlePod(b) == 0) v = coopbuild::kBuildDmFaulted;   /* the panel's own dismantle: mounted buildings go with it */
            else { float p1 = 0, n1 = 0; int c1 = 0, x1 = 0; BdReadState(b, &p1, &n1, &c1, &d1, &x1); }
        }
        if (v == coopbuild::kBuildDmDone) ++g_dmAskDone;
        else if (v == coopbuild::kBuildDmFaulted) ++g_dmAskFaulted;
        else ++g_dmAskRefused;
        coopbuild::BuildMsg a;
        a.kind = coopbuild::kBuildDismantleAsk;
        a.key = key;
        a.reason = (unsigned char)v;
        const bool sent = from >= 0 && net::BuildSlotTarget(from) == from && coopbuild::BuildEncodable(a) && net::SendBuildToSlot(a, from);   /* the asker by its own slot only */
        char line[480];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[BUILD] <- DISMANTLE_ASK key=%.63s from slot%d (%s): %s%s; returned=%d isDismantled %d->%d; answer %s; dmAskRecv=%lld dmAskDone=%lld dmAskRefused=%lld dmAskFaulted=%lld",
                    key.c_str(), from, member != 0 ? "member" : "not a member", coopbuild::BuildDmWord(v),
                    v == coopbuild::kBuildDmDone ? " (its REMOVE follows from BuildTick)" : "", ret, d0, d1, sent ? "sent" : "NOT SENT (no road to the asker)",
                    g_dmAskRecv, g_dmAskDone, g_dmAskRefused, g_dmAskFaulted);
        BdLogCapped(line);
    }
}

void PowerDrain();   /* P47: below */
void BuildCopyDrain()
{
    ++g_drainNo;
    g_hostLookups = 0;
    g_rsSearches = 0;   /* mmo8a2 */
    FarmDrain();   /* par16: the writer's farm state onto copies, requests run by the writer, the writer's publish */
    BdDismantleAskDrain();   /* T-805: a teammate's DISMANTLE_ASKs on this game's own pieces */
    PowerDrain();   /* P47: the writers' POWER_STATEs onto copies here, the switches asked of this game, the switches this game asks */
    if (g_copyDiagOwed != 0 && !EngineWritesBlocked()) BdCopyListDiagTick();   /* P87 root (step 5): read-only, before any adopt / reconcile / REMOVE below */
    BdTownsSayTick();   /* this game's own outposts to the world server, every drain whatever is pending (its own checks: the 5 s look, a slot, the town list, engine writes, a faction) */
    if (!g_townRows.empty()) { BdSelfScope townScope; BdTownsLoadedTick(); }   /* every game: the kept outposts' pieces loaded here linked to their towns, whatever is pending (its own 5 s look and engine-write check) */
    if (!g_shown.empty() || g_showSaveHeld != 0) BdShowSaveGuard();   /* T-811: a save the engine was asked for - the bases founded here go before it runs (this safe point) */
    if (g_showDirty != 0 || StoreWorldGenNow() != g_showWorldGen || g_townRowsGen != g_showKeptSeen || (g_showRetryAt != 0 && (long)(::GetTickCount() - g_showRetryAt) >= 0))
    { BdSelfScope showScope; BdShowApply(); }   /* T-811: the teammates' bases on the map - after a SHOW, a world load, the kept list, a save, or a due retry */
    if (!g_shown.empty()) { BdSelfScope showLinkScope; BdShowLinkTick(); }   /* owner 936: the teammates' copies linked to their shown towns (its own 5 s look, due marks and engine-write check) */
    if (!g_shown.empty()) BdShowMarkTick();   /* T-811 log: what the map draws for each teammate base (read-only; a line when it first shows or changes) */
    BdTownWake();   /* T-936: the outposts founded since (here, by the lever, the relink or a teammate's copy just above) woken once their area is loaded (its own engine-write check) */
    if (g_pend.empty() && g_wait.empty() && g_pendState.empty() && g_pendRemove.empty() && g_helpIn.empty() && g_helpGoneIn.empty() && BdReconAnyDue() == 0
        && g_tombKill.empty() && KeptRowsActive() == 0 && g_bdHarvestDue.empty()) return;   /* P87: + a reconcile due; P14: + a removal queued; KEPT: + kept rows with a part this game may act on */
    if (EngineWritesBlocked()) { ++g_copyBlocked; return; }
    BdSelfScope selfScope;   /* help1: every engine call below is the mod's own - vt+0x230 inside it is never a helper's work */
    BdHelpDrain();   /* help1: the helpers' work onto this game's own pieces (no stand-in needed) */
    BdHarvestDrain();   /* the materials put into copies off the main thread: recorded as help for their owners, or refused and returned */
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
        if (r != 0 && r != 2 && g_pend[i].keptRow >= 0) BdKeptPendDone(g_pend[i]);   /* KEPT: that row's LAID */
        if (r == 2) { g_wait.push_back(g_pend[i]); g_pend.erase(g_pend.begin() + (std::ptrdiff_t)i); }
        else if (r != 0) g_pend.erase(g_pend.begin() + (std::ptrdiff_t)i);
        else ++i;
    }
    g_bdKeptDraining = 1;
    BdKeptDrain();   /* KEPT: the kept rows, after the PLACEs (an own kept piece queued now is made at the next safe point) */
    g_bdKeptDraining = 0;   /* a piece made while the kept rows were laid is not marked as placed here (BdOnNewPiece) */
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
void PowerForgetWorld();   /* P47 (H-P47a): below */
void BuildForgetWorld()
{
    FarmForgetWorld();   /* par16 */
    g_bdHarvestDue.clear();   /* the marked copies belong to the world going away */
    PowerForgetWorld();   /* P47 (H-P47a): the published writer verdicts belong to the world going away */
    g_ocQ.clear(); g_ocDepth = 0;   /* P18: the queued keys belong to the world going away */
    g_btPlaced = 0; g_btPlacedKey[0] = 0;   /* TEST-ONLY: the piece buildtest place made belongs to the world going away */
    g_townLoadedDone.clear();   /* the outposts' settled pieces are this world's */
    g_townNotMateSaid.clear();  /* owner 938: the not-a-teammate notices are this world's */
    g_townTeamWaitSaid.clear(); /* and the outposts counted waiting for the team table */
    g_showKeysSeen.clear();     /* T1342: the keys the shown rows said are this world's */
    g_townLoadedMiss.clear();   /* and the looks counted against its outpost keys */
    g_townKeysOf.clear(); g_townKeysWalked = false; g_townKeysAnyUnread = 1;   /* the town keys walked name the world going away: the next look walks again */
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
    g_madeHere.clear();   /* the pieces this game placed belong to the world that is going */
    g_deadSets.clear();   /* F1026: a dead-handle set and its first-seen safe point belong to the world that is going - a reload of the same save waits again */
    g_dmOweForgot += (long long)coopbuild::BuildOweRemoveForget(&g_dmOweRemove);   /* par13: a dismantle's owed REMOVE belongs to the world it was made in */
    g_clListPending = 1; g_clHoldT0 = 0; g_clFeedAt = 0;   /* P87 rf1: the next world's copy list is still to be read */
    g_copyList.clear(); g_copyRetire.clear(); g_copyDiagOwed = 0;   /* P87 root: the copy list is in pp.build (OwnBuildTeardownFlush wrote it first) and read back at the next load */
    g_tomb.clear(); g_tombRetire.clear(); g_tombSendOwed = 0; g_tombInfo.clear(); g_tombVerifyOwed = 0; g_tombVerifyZone = -1; g_tombVerifyCursor = 0; g_recon.clear(); g_tombKill.clear(); g_tombKillNo.clear(); g_refundSrc.clear(); g_ledgerPend.clear(); /* P14 (+ fold 1) */   /* P87: a world's tombstones are in its pp.build (written first); received REMOVEs belong to that world */
    g_ownRetireOwed.clear(); g_rsOwePlace.clear(); g_rsOweRemove.clear();   /* mmo8a2 (M2, M4), mmo8a3 (C): they go with their world (store.cpp handed pp.build's dirty rows to the writer first - mmo8a3 D1) */
    BdKeptForgetWorld();   /* KEPT: this world's table of rows worked here (the rows themselves are the link's, store.cpp) */
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
    if (m.kind == coopbuild::kBuildDismantleAsk)   /* T-805: an ask (owner's side, answered at the K2 safe point) or the owner's answer (asker's side) */
    {
        const int from = net::PlayerSlotOfKey(fromPeer);
        if (m.reason == coopbuild::kBuildDmAsk)
        {
            ++g_dmAskRecv;
            if ((int)g_dmAskIn.size() >= 16) { ++g_dmAskDropped; return; }   /* the member's click asks again */
            BdDmAsk q;
            q.key = m.key; q.slot = from;
            g_dmAskIn.push_back(q);
            return;
        }
        if (m.reason == coopbuild::kBuildDmDone) ++g_dmAnsDone; else ++g_dmAnsRefused;
        char dl[400];
        _snprintf_s(dl, sizeof(dl), _TRUNCATE, "[BUILD] <- DISMANTLE_ANSWER key=%.63s from slot%d: %s%s; dmAnsDone=%lld dmAnsRefused=%lld",
                    m.key.c_str(), from, coopbuild::BuildDmWord((int)m.reason),
                    m.reason == coopbuild::kBuildDmDone ? " (the owner's REMOVE takes the copy here)" : " (nothing was changed here)", g_dmAnsDone, g_dmAnsRefused);
        BdLogCapped(dl);
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
            if (g_pend[i].m.key == m.key && coopbuild::BuildRemoveNonceSkip(m.nonce, g_pend[i].m.nonce) == 0) { BdKeptPendDropped(g_pend[i]); g_pend.erase(g_pend.begin() + (std::ptrdiff_t)i); ++dropped; }   /* P87 fold 1 (H2); KEPT: a kept restore hands its claim back */
            else ++i;
        }
        for (size_t i = 0; i < g_wait.size(); )
        {
            if (g_wait[i].m.key == m.key && coopbuild::BuildRemoveNonceSkip(m.nonce, g_wait[i].m.nonce) == 0) { BdKeptPendDropped(g_wait[i]); g_wait.erase(g_wait.begin() + (std::ptrdiff_t)i); ++dropped; }   /* P87 fold 1 (H2); KEPT: a kept restore hands its claim back */
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
            it->second.deferTries = 0; it->second.completeTries = 0;   /* P15 fold 1 (LOW 4): a newer owner word starts its own bounded retries */
            return;
        }
        if ((int)g_pendState.size() >= kPendCap) { ++g_stateDropped; return; }
        BdStatePend& s = g_pendState[m.key];
        s.m = m; s.nextTry = 0; s.fromPlace = 0; s.waitLogged = 0; s.deferTries = 0; s.completeTries = 0;   /* P15 fold 1 */
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
        if (g_pend[i].m.key == m.key)
        {   /* KEPT: a kept restore overwritten hands its claim back; the entry is the other game's PLACE now (a new PLACE's restore state, below) - never made as this game's own */
            ++g_copyDup; BdKeptPendDropped(g_pend[i]); g_pend[i].m = m; g_pend[i].fromSlot = net::PlayerAddrOfKey(fromPeer);
            g_pend[i].restore = 0; g_pend[i].rsOutcome = 0; g_pend[i].rsHasState = 0; g_pend[i].st = coopbuild::BuildMsg(); g_pend[i].keptMade = 0;
            return;
        }
    for (size_t i = 0; i < g_wait.size(); ++i)
        if (g_wait[i].m.key == m.key)
        {   /* KEPT: as above, for an entry waiting */
            ++g_copyDup; BdKeptPendDropped(g_wait[i]); g_wait[i].m = m; g_wait[i].fromSlot = net::PlayerAddrOfKey(fromPeer);
            g_wait[i].restore = 0; g_wait[i].rsOutcome = 0; g_wait[i].rsHasState = 0; g_wait[i].st = coopbuild::BuildMsg(); g_wait[i].keptMade = 0;
            return;
        }
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
        st.broken = (unsigned char)(r.broken != 0 ? 1 : 0);
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
    BdKeptOwnerHanded(seq);   /* KEPT: an owner part registered before this write waits for its commit */
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
        const int brk = (b != 0) ? BdBrokenRead(b) : r.broken;
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
        std::sprintf(line, "[BUILD] list key=%s sid=%.95s owner='%.60s' live=%d complete=%d progress=%.2f/%.2f dismantled=%d destroyed=%d broken=%d removed=%d via=%s mats=[%s]"
                     " host=%.63s keySame=%d own=%d ownerSlot=%d handed=%d",
                     it->first.c_str(), r.sid.c_str(), owner.c_str(), ok, c, p, nd, d, x, brk, r.removed, BdViaName(r.via), ms,
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

/* MAIN THREAD: a piece this game placed is marked with the area's "loaded since" stamp of its own position at the placement; no
   stamp there (0) or a full table leaves it unmarked */
namespace {
void BdMadeHereMark(void* b, const char* key)
{
    if (key == 0 || key[0] == 0) return;
    const std::string k(key);
    float p[3]; p[0] = 0; p[1] = 0; p[2] = 0;
    const double since = (b != 0 && BdCopyF((const char*)b + kObjPos, p, 3) != 0) ? MyMapLoadedSinceTS(SectorOf(p[0], p[2])) : 0.0;
    if (since == 0.0 || (g_madeHere.size() >= kMadeHereCap && g_madeHere.count(k) == 0)) { g_madeHere.erase(k); return; }
    g_madeHere[k] = since;
}
} // namespace
/* MAIN THREAD: 1 = this game placed the piece at this key itself (the commit or the placing lever) in the world now loaded, and the
   row is still this game's own, not a copy, not removed; *placedSince = the area's "loaded since" stamp at that placement. While the
   area's stamp is still that one (coopown::HandbackMadeHereSkip compares it with the stamp now), this game has held the area without
   a break since the placement and written its own box itself, so no other game can have written the box. */
int BuildMadeHere(const char* key, double* placedSince)
{
    *placedSince = 0.0;
    if (key == 0 || key[0] == 0 || BdOnMain() == 0) return 0;
    const std::string k(key);
    std::map<std::string, double>::const_iterator mh = g_madeHere.find(k);
    if (mh == g_madeHere.end()) return 0;
    std::map<std::string, BdReg>::const_iterator it = g_reg.find(k);
    if (it == g_reg.end() || it->second.own == 0 || it->second.via == 3 || it->second.removed != 0) return 0;
    *placedSince = mh->second;
    return 1;
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

/* ==== P47: POWER, GENERATORS AND BATTERIES - ONE WRITER PER PIECE (src/common/powerwire.h) ====
   A power piece's writer is the production machines' writer (ProdWriterDecide / ProdStepWriterHere: the owner's game while it is
   online and holds the area, else the area holder). Every game runs its own town grids (0x92C980 -> 0x92BE90: each frame ONE town
   of the grid-update set TownList+0x180B8 runs its grid step through vt+0x2B8, the set's members taking turns - not every loaded town
   every frame) from the switches, the battery levels and the fuel stocks, so only those are carried:
   - the writer sends POWER_STATE for a piece on its first look, when its switch, gets-battery or number of input records changes
     or a stock rises (a fuel item taken), its battery level and falling stocks at most every kPowerLevelGapMs while they change, and every kPwRefreshMs regardless (a game that loads the
     area or joins later);
   - a game another game writes the piece for writes POWER_STATE onto its copy (BdPowerWriteRawPod: setPowerOn through vt+0x318 when
     the switch differs; BdProdWritePod for the stocks), and for that piece puts back the battery level its own charge 0x299460
     wrote and does not run the fuel step 0x29A330 of a generator (+0x3BC > 0);
   - a switch changed on such a game is asked of the writer (POWER_ASK, the writer's own slot) and, once sent, put back there; the
     writer switches its piece once when the asker is the owner's game or a teammate's - its next POWER_STATE carries the result.
     With no road to the writer the switch stays as the player set it until the writer's next POWER_STATE;
   - a POWER_STATE is written only when it came from the game this game names the writer; while either slot is unknown it is kept
     up to kPwWaitMs (powerwire::PowerStateFrom, the MINE_STATE rule).
   The writer's look and the asker's look (PwLook) run on the main thread only. A detour on the main thread looks at once; the
   engine also runs both steps OFF the main thread (T1323: offMain 5,695,914 against 1 row - the battery charge of the battery bank
   and the loom ran nowhere else), and there the detour gates, counts (offMain) and marks the piece (PwMark: the piece's P7n key
   and its address as a hint, into a fixed queue under g_pwMarkCs, a piece's own gap-table slot at most once per kPowerMarkGapMs,
   any slot at most once per half of it - powerwire::PowerMarkPick; the slots are read without the lock and the lock is taken
   only when a mark is due; while engine writes are blocked - a pause, a load, a teardown - the drain is skipped and nothing is queued,
   counted blocked). The K2 safe point (PowerDrain -> PwMarkDrain) re-proves each marked piece by key (ObjectByPositionKeyHint: the hint is used only once the resolver's own liveness walk finds it) and
   looks at it. The test lever bldset's power / battery / stock writes, a kept power lay and a switch done for an ask mark their
   piece too, forced past the gap, into a main-thread list the same drain takes (no lock, never dropped).
   Every one of the 13 vtables that holds setPowerOn
   0x2973B0 at +0x318 holds the battery charge 0x299460 at +0xD8 (their references sit 0x240 apart; 7 of them hold the fuel step
   0x29A330 at +0x5B8), so every power piece passes through the charge detour whenever the engine runs its vt+0xD8 step - on
   whichever thread - and a generator whose fuel step stopped (stock 0) is still looked at through its charge step. */
namespace {
typedef void (*PwChargeFn)(void* b);
typedef void (*PwInputsFn)(void* b, float amount);
PwChargeFn orig_pwCharge = 0;
PwInputsFn orig_pwInputs = 0;
int g_hkPwCharge = -2, g_hkPwInputs = -2;
const DWORD kPwDecideMs = 1000;     /* a row's key, class and writer asked again this often */
const DWORD kPwRefreshMs = 30000;   /* the writer sends each piece at least this often */
const DWORD kPwStaleMs = 2000;      /* a pointer not looked at this long (a main-thread detour or the look queue's drain) starts a new row (a world load, a new object) */
const DWORD kPwForgetMs = 10000;    /* a row not seen this long is dropped */
const DWORD kPwAskGapMs = 2000;     /* one ask per piece per this long */
const DWORD kPwFailGapMs = 1000;    /* a send with no road is tried again after this long */
const DWORD kPwWaitMs = kPwRefreshMs;   /* a POWER_STATE whose writer's or sender's slot is not known is kept this long (the writer resends within it) */
const size_t kPwRowCap = 2048, kPwInCap = 512, kPwAskCap = 32;
volatile LONGLONG g_pwGatedBurn = 0, g_pwGatedCharge = 0;
volatile LONGLONG g_pwOffMainCharge = 0, g_pwOffMainFuel = 0, g_pwOffMainPub = 0;   /* off-main charge steps, off-main fuel steps, off-main steps the published verdict decided (H-P47a) */
long long g_pwSent = 0, g_pwSendFail = 0, g_pwRecv = 0, g_pwApplied = 0, g_pwRefused = 0, g_pwNoPiece = 0, g_pwFaulted = 0, g_pwDropped = 0;
long long g_pwAsks = 0, g_pwAskFail = 0, g_pwUndone = 0, g_pwAskRecv = 0, g_pwAskDone = 0, g_pwAskRefused = 0, g_pwRowFull = 0;
long long g_pwWaitDropped = 0, g_pwAskReadFault = 0;   /* POWER_STATEs dropped after kPwWaitMs with a slot still unknown; asks refused on a faulted read */
int g_pwSaidFirst = 0, g_pwSaidEvent = 0;   /* [POWER] lines per budget (powerwire::PowerSayKind, kPowerSayCap each per process; the REPORT line is not counted) */
struct PwRow
{
    std::string key;
    DWORD seenAt, decidedAt, sentAt, failAt, askAt;
    int decided, power, prod, writer, slot;
    int ever, sOn, sGets, sN;            /* the writer's side: what was last sent */
    float sBat;
    float sStock[powerwire::kPowerMaxStocks];
    int held, hOn;                       /* the other side: the writer's switch last written here */
    PwRow() : seenAt(0), decidedAt(0), sentAt(0), failAt(0), askAt(0), decided(0), power(0), prod(0), writer(0), slot(-1),
              ever(0), sOn(0), sGets(0), sN(0), sBat(0.0f), held(0), hOn(0) { for (unsigned i = 0; i < powerwire::kPowerMaxStocks; ++i) sStock[i] = 0.0f; }
};
std::map<void*, PwRow> g_pwRows;          /* MAIN THREAD: by the pointer a detour was handed (or the drain resolved) */
/* H-P47a: each power piece's writer verdict as the main thread decided it (PwRowFor: ProdWriterDecide), published for an engine step
   OFF the main thread, where the owner check cannot answer for a player's piece (ProdStepWriterHere -> none) and the live owner read
   cannot be made. The map's shape (insert, erase) changes on the main thread only, under g_pwPubLock taken exclusive with a TRY,
   never waited for (a refused try is counted pubBusy and made again at the piece's next look, the next 5 s sweep or the next drain);
   a held entry's verdict and look time are written by the main thread alone with interlocked stores, under no lock. An off-main step
   takes the lock shared, finds the piece (no allocation) and reads both. A verdict is honoured only while its row is live (looked at
   within kPwStaleMs: powerwire::PowerStepSource), so an address a new object takes is never judged by the old piece's verdict. */
struct PwPub { LONG writer; LONG at; PwPub() : writer(0), at(0) {} };
std::map<void*, PwPub> g_pwPub;
SRWLOCK g_pwPubLock = SRWLOCK_INIT;
int g_pwPubClearOwed = 0;      /* MAIN THREAD: a world teardown's clear that found the lock busy, made by the next PowerDrain */
long long g_pwPubBusy = 0;     /* MAIN THREAD: tries of g_pwPubLock refused (each made again later) */
/* MAIN THREAD: piece b's verdict published with its look time now; a piece not yet held is inserted only when the verdict names a writer */
void PwPublish(void* b, int writer, DWORD now)
{
    std::map<void*, PwPub>::iterator it = g_pwPub.find(b);   /* only this thread changes the shape: found without the lock */
    if (it != g_pwPub.end())
    {
        ::InterlockedExchange(&it->second.writer, (LONG)writer);
        ::InterlockedExchange(&it->second.at, (LONG)now);
        return;
    }
    if (writer == coopfarm::kProdWriterNone) return;
    if (::TryAcquireSRWLockExclusive(&g_pwPubLock) == 0) { ++g_pwPubBusy; return; }   /* the piece's next look publishes it */
    PwPub& p = g_pwPub[b];
    p.writer = (LONG)writer; p.at = (LONG)now;
    ::ReleaseSRWLockExclusive(&g_pwPubLock);
}
/* ANY THREAD: piece b's published verdict; 1 = one is held (*writer, *age = ms since the main thread last looked at the piece) */
int PwPublished(void* b, int* writer, unsigned int* age)
{
    int have = 0;
    ::AcquireSRWLockShared(&g_pwPubLock);
    std::map<void*, PwPub>::iterator it = g_pwPub.find(b);
    if (it != g_pwPub.end())
    {
        *writer = (int)::InterlockedCompareExchange(&it->second.writer, 0, 0);
        const DWORD at = (DWORD)::InterlockedCompareExchange(&it->second.at, 0, 0);
        *age = (unsigned int)(::GetTickCount() - at);
        have = 1;
    }
    ::ReleaseSRWLockShared(&g_pwPubLock);
    return have;
}
/* ANY THREAD (an engine step off the main thread): the writer verdict the step uses - ProdStepWriterHere's when it knows, else the
   main thread's published one while its row is live, else none (powerwire::PowerStepSource); *fromPub = 1 when the published one decided */
int PwStepWriter(void* b, int* fromPub)
{
    const int live = ProdStepWriterHere(b);
    int pub = 0, have = 0;
    unsigned int age = 0;
    if (live == coopfarm::kProdWriterNone) have = PwPublished(b, &pub, &age);
    const int src = powerwire::PowerStepSource(live, have, pub, age, kPwStaleMs);
    *fromPub = (src == powerwire::kPowerFromPublished) ? 1 : 0;
    return powerwire::PowerStepWriter(src, live, pub);
}
/* MAIN THREAD (PowerDrain's 5 s sweep; all = a world teardown): published verdicts not looked at for kPwForgetMs dropped (all: every
   one). 0 = the lock was busy - the next round drops them (meanwhile a reader ignores a verdict older than kPwStaleMs) */
int PwPubSweep(DWORD now, int all)
{
    if (g_pwPub.empty()) return 1;
    if (::TryAcquireSRWLockExclusive(&g_pwPubLock) == 0) { ++g_pwPubBusy; return 0; }
    if (all != 0) g_pwPub.clear();
    else
        for (std::map<void*, PwPub>::iterator it = g_pwPub.begin(); it != g_pwPub.end(); )
        {
            if ((DWORD)(now - (DWORD)it->second.at) > kPwForgetMs) g_pwPub.erase(it++);
            else ++it;
        }
    ::ReleaseSRWLockExclusive(&g_pwPubLock);
    return 1;
}
struct PwIn { powerwire::PowerMsg m; int from; DWORD at; DWORD tried; PwIn() : from(-1), at(0), tried(0) {} };   /* at = when it arrived; tried = when a kept state was last decided (0 = never) */
std::map<std::string, PwIn> g_pwIn;       /* MAIN THREAD: the latest POWER_STATE per key, written at K2 */
struct PwAskIn { std::string key; int from; int on; };
std::vector<PwAskIn> g_pwAskIn;           /* MAIN THREAD: switches asked of this game, done at K2 */
struct PwAskOut { std::string key; int slot; };
std::vector<PwAskOut> g_pwAskOut;         /* MAIN THREAD: switches changed here on pieces another game writes, asked at K2 */
std::map<std::string, void*> g_pwHint;    /* MAIN THREAD: the last piece per key, re-proved on use */
DWORD g_pwSweepAt = 0;
/* the look queue: ANY THREAD writes it under g_pwMarkCs (PwMark), the K2 safe point takes it (PwMarkDrain). PODs only. */
const int kPwMarkCap = 256;         /* marked pieces waiting for one drain; an off-main mark past this is dropped (droppedFull) and comes again after the gap.
                                       While engine writes are blocked the drain is skipped and no off-main mark is queued (counted blocked) */
const int kPwMarkSlots = 512;       /* the gap table (a power of two); a piece probes powerwire::kPowerMarkProbe slots from its address's hash */
struct PwMarkRec { void* hint; char key[kKeyCap]; };
/* written only under g_pwMarkCs; read first without it (aligned, volatile): a torn or stale read only makes a wrong first guess,
   which the check under the lock corrects - a duplicate mark is one more look, a missed one comes again on a later step */
struct PwMarkSlot { void* volatile b; volatile DWORD at; };
PwMarkRec g_pwMarkQ[kPwMarkCap];
int g_pwMarkN = 0;
PwMarkSlot g_pwMarkSlot[kPwMarkSlots];
CRITICAL_SECTION g_pwMarkCs;
volatile LONG g_pwMarkInit = 0;
PwMarkRec g_pwMarkTake[kPwMarkCap];   /* MAIN THREAD: PwMarkDrain's copy */
std::vector<PwMarkRec> g_pwMarkMain;  /* MAIN THREAD: forced marks made on the main thread, taken by the drain; no lock, never dropped */
volatile LONGLONG g_pwMarked = 0, g_pwMarkFull = 0, g_pwMarkNoKey = 0;
volatile LONGLONG g_pwMarkBusy = 0, g_pwMarkBlocked = 0;
long long g_pwMarkDrained = 0, g_pwMarkLooked = 0, g_pwMarkStale = 0, g_pwMarkSame = 0;
/* the main thread only tries the lock: at the K2 safe point the engine holds its worker, which may hold it (busy counts a miss) */
int PwMarkLock()
{
    if (BdOnMain() == 0) { ::EnterCriticalSection(&g_pwMarkCs); return 1; }
    if (::TryEnterCriticalSection(&g_pwMarkCs) != 0) return 1;
    ::InterlockedIncrement64(&g_pwMarkBusy);
    return 0;
}
/* piece b's window of the gap table read into kind / age (powerwire::PowerMarkPick); returns the table index of window slot 0 */
int PwMarkWindow(void* b, DWORD now, int* kind, unsigned int* age)
{
    const uintptr_t p = (uintptr_t)b;
    const int base = (int)(((p >> 4) ^ (p >> 13)) & (uintptr_t)(kPwMarkSlots - 1));
    for (int i = 0; i < powerwire::kPowerMarkProbe; ++i)
    {
        const PwMarkSlot& s = g_pwMarkSlot[(base + i) & (kPwMarkSlots - 1)];
        void* const o = s.b;
        kind[i] = (o == b) ? 1 : (o == 0 ? 0 : 2);
        age[i] = (unsigned int)(now - s.at);
    }
    return base;
}
/* ANY THREAD: mark piece b for a look on the main thread. force = a write the mod made (passes the gap). Off the main thread no
   allocation, and the lock is taken only when the window says a mark is due */
void PwMark(void* b, int force)
{
    if (b == 0 || ::InterlockedCompareExchange(&g_pwMarkInit, 0, 0) == 0) return;
    PwMarkRec e;
    e.hint = b;
    if (force != 0 && BdOnMain() != 0)
    {   /* the main thread's own list: no lock, no slot stamped; an off-main mark of the same piece before the drain is one look */
        if (ObjectPositionKey(b, e.key, kKeyCap, "", 3, 0) == 0 || e.key[0] == 0) { ::InterlockedIncrement64(&g_pwMarkNoKey); return; }
        g_pwMarkMain.push_back(e);
        ::InterlockedIncrement64(&g_pwMarked);
        return;
    }
    int kind[powerwire::kPowerMarkProbe];
    unsigned int age[powerwire::kPowerMarkProbe];
    PwMarkWindow(b, ::GetTickCount(), kind, age);
    if (powerwire::PowerMarkPick(kind, age, powerwire::kPowerMarkProbe, force) < 0) return;   /* not due: no lock taken */
    /* EngineWritesBlocked is an any-thread read (store.cpp: interlocked load / teardown depths; GameplayRunning a main-written
       bool, a stale read is one frame late): while it holds, PowerDrain skips the drain, so nothing is queued and no key is read */
    const int blocked = EngineWritesBlocked() ? 1 : 0;
    const int hasKey = (blocked == 0 && ObjectPositionKey(b, e.key, kKeyCap, "", 3, 0) != 0 && e.key[0] != 0) ? 1 : 0;
    if (PwMarkLock() == 0) return;
    const DWORD now = ::GetTickCount();   /* under the lock: never earlier than a stamp another thread made before it */
    const int base = PwMarkWindow(b, now, kind, age);
    const int pick = powerwire::PowerMarkPick(kind, age, powerwire::kPowerMarkProbe, force);
    const int out = powerwire::PowerMarkOutcome(blocked, hasKey, g_pwMarkN < kPwMarkCap ? 1 : 0);
    if (pick >= 0 && out == powerwire::kPowerMarkQueued) g_pwMarkQ[g_pwMarkN++] = e;
    /* stamped when queued; an unforced mark that is blocked, has no key or finds no room is stamped too, so it is tried again after
       the gap, not every step */
    if (pick >= 0 && (out == powerwire::kPowerMarkQueued || force == 0))
    {
        PwMarkSlot& s = g_pwMarkSlot[(base + pick) & (kPwMarkSlots - 1)];
        s.at = now;
        s.b = b;
    }
    ::LeaveCriticalSection(&g_pwMarkCs);
    if (pick < 0) return;   /* another thread marked it first */
    ::InterlockedIncrement64(out == powerwire::kPowerMarkQueued ? &g_pwMarked : (out == powerwire::kPowerMarkBlocked ? &g_pwMarkBlocked
                             : (out == powerwire::kPowerMarkNoKey ? &g_pwMarkNoKey : &g_pwMarkFull)));
}
/* a [POWER] line on its budget (powerwire::PowerSayKind): first looks / first applies of a player's piece, and changes, each
   kPowerSayCap per process; kPowerSayNone prints nothing */
void PwSay(int kind, const char* line)
{
    int* n = (kind == powerwire::kPowerSayEvent) ? &g_pwSaidEvent : (kind == powerwire::kPowerSayFirst ? &g_pwSaidFirst : 0);
    if (n == 0 || *n >= powerwire::kPowerSayCap) return;
    ++*n;
    DebugLog(std::string(line));
}
/* MAIN THREAD: 1 = a player owns piece b (this game's player or another player: BuildingOwnerClass kOwnMe / kOwnPeer) */
int PwPlayerOwned(void* b)
{
    int os = -1;
    const int oc = BuildingOwnerClass(b, &os);
    return (oc == coopown::kOwnMe || oc == coopown::kOwnPeer) ? 1 : 0;
}
struct PwPod { int on, gets, n; float bat; float stock[powerwire::kPowerMaxStocks]; };
int PwFloatPod(const void* b, size_t off, float* v)
{
    __try { *v = *(const float*)((const char*)b + off); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int PwFloatSetPod(void* b, size_t off, float v)
{
    __try { *(float*)((char*)b + off) = v; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the switch, gets battery and battery (BdPowerReadPod), and for a production class its input stocks (n = 0 when there are more than
   kPowerMaxStocks: they are not carried) */
int PwRead(void* b, int prod, PwPod* o)
{
    BdPowerPod pw;
    std::memset(&pw, 0, sizeof pw);
    if (BdPowerReadPod(b, &pw) == 0) return 0;
    o->on = pw.on != 0 ? 1 : 0; o->gets = pw.gets != 0 ? 1 : 0; o->bat = pw.battery; o->n = 0;
    if (prod != 0)
    {
        float bar = 0.0f;
        int hasBar = 0, n = -1;
        if (BdProdReadPod(b, &bar, &hasBar, o->stock, (int)powerwire::kPowerMaxStocks, &n) != 0 && n > 0 && n <= (int)powerwire::kPowerMaxStocks) o->n = n;
    }
    if (!powerwire::PowerValueOk(o->bat)) return 0;
    for (int i = 0; i < o->n; ++i) if (!powerwire::PowerValueOk(o->stock[i])) { o->n = 0; break; }
    return 1;
}
/* MAIN THREAD: the row of a live piece, its key, class and writer asked again every kPwDecideMs; 0 when the table is full */
PwRow* PwRowFor(void* b)
{
    const DWORD now = ::GetTickCount();
    std::map<void*, PwRow>::iterator it = g_pwRows.find(b);
    if (it == g_pwRows.end())
    {
        if (g_pwRows.size() >= kPwRowCap) { ++g_pwRowFull; return 0; }
        it = g_pwRows.insert(std::make_pair(b, PwRow())).first;
    }
    PwRow& r = it->second;
    if ((DWORD)(now - r.seenAt) > kPwStaleMs) r = PwRow();
    r.seenAt = now;
    if (r.decided == 0 || (DWORD)(now - r.decidedAt) >= kPwDecideMs)
    {
        char k[kKeyCap];
        k[0] = 0;
        if (ObjectPositionKey(b, k, kKeyCap, "", 3, 0) == 0 || k[0] == 0) { r = PwRow(); r.seenAt = now; }
        else if (r.key != k)
        {
            r = PwRow(); r.seenAt = now; r.key = k;
            char cls[64];
            BdClassOf(b, cls, (int)sizeof cls);
            r.power = BdIsPowerClass(cls); r.prod = BdIsProdClass(cls);
        }
        r.writer = ProdWriterDecide(b, &r.slot);
        r.decided = 1; r.decidedAt = now;
    }
    PwPublish(b, r.writer, now);   /* H-P47a: the verdict and this look, for the engine's steps off the main thread */
    return &r;
}
/* MAIN THREAD (a detour, or the look queue's drain at K2): the writer sends its piece when due; another game's piece whose switch moved here is queued for an ask */
void PwLook(void* b, PwRow& r)
{
    if (r.power == 0 || r.key.empty()) return;
    PwPod p;
    if (PwRead(b, r.prod, &p) == 0) return;
    const DWORD now = ::GetTickCount();
    if (r.writer == coopfarm::kProdWriterOther)
    {
        if (powerwire::PowerAskDue(r.held, p.on, r.hOn) != 0 && (DWORD)(now - r.askAt) >= kPwAskGapMs && g_pwAskOut.size() < kPwAskCap)
        {
            r.askAt = now;
            PwAskOut a; a.key = r.key; a.slot = r.slot;
            g_pwAskOut.push_back(a);
        }
        return;
    }
    if (r.writer != coopfarm::kProdWriterHere) return;
    if (r.failAt != 0 && (DWORD)(now - r.failAt) < kPwFailGapMs) return;
    const int sw = (r.ever != 0 && (p.on != r.sOn || p.gets != r.sGets)) ? 1 : 0;
    int cnt = (r.ever != 0 && p.n != r.sN) ? 1 : 0;   /* the number of input records, a stock that rose (a fuel item taken) or reached 0: now */
    int lvl = powerwire::PowerLevelDiffers(p.bat, r.sBat), emptied = 0;
    for (int i = 0; i < p.n && i < r.sN; ++i)
    {
        lvl |= powerwire::PowerLevelDiffers(p.stock[i], r.sStock[i]);
        if (r.ever != 0) { cnt |= powerwire::PowerStockRose(p.stock[i], r.sStock[i]); emptied |= powerwire::PowerStockEmptied(p.stock[i], r.sStock[i]); }
    }
    cnt |= emptied;
    const unsigned int since = (unsigned int)(now - r.sentAt);
    if (powerwire::PowerSendDue(r.ever, sw, cnt, lvl, since) == 0 && !(r.ever != 0 && since >= kPwRefreshMs)) return;
    powerwire::PowerMsg m;
    m.kind = powerwire::kPowerState; m.key = r.key; m.on = (unsigned char)p.on; m.gets = (unsigned char)p.gets; m.battery = p.bat;
    for (int i = 0; i < p.n; ++i) m.stock.push_back(p.stock[i]);
    std::vector<char> bytes;
    if (!powerwire::EncodePower(&bytes, m) || !net::SendPower(bytes)) { ++g_pwSendFail; r.failAt = now; return; }
    const int first = (r.ever == 0) ? 1 : 0;
    r.ever = 1; r.sentAt = now; r.failAt = 0; r.sOn = p.on; r.sGets = p.gets; r.sBat = p.bat; r.sN = p.n;
    for (int i = 0; i < p.n; ++i) r.sStock[i] = p.stock[i];
    ++g_pwSent;
    const int changed = (sw != 0 || emptied != 0) ? 1 : 0;
    const int say = powerwire::PowerSayKind(first, changed, (first != 0 && changed == 0) ? PwPlayerOwned(b) : 0);   /* the owner read only for a first look */
    if (say != powerwire::kPowerSayNone)
    {
        char line[400];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[POWER] -> POWER_STATE key=%.63s (%s) power=%d getsBattery=%d battery=%.4f stocks=%d stock0=%.4f; powerSent=%lld",
                    r.key.c_str(), first != 0 ? "first look" : (sw != 0 ? "the switch or gets-battery changed" : "a stock reached 0"), p.on, p.gets, p.bat, p.n,
                    p.n > 0 ? p.stock[0] : -1.0f, g_pwSent);
        PwSay(say, line);
    }
}
/* the battery charge's own write put back: the writer's level arrives as POWER_STATE */
void PwChargeGated(void* b)
{
    float before = 0.0f, after = 0.0f;
    const int ok = PwFloatPod(b, kBdBattery, &before);
    orig_pwCharge(b);
    if (ok != 0 && PwFloatPod(b, kBdBattery, &after) != 0 && after != before)
    {
        PwFloatSetPod(b, kBdBattery, before);
        ::InterlockedIncrement64(&g_pwGatedCharge);
    }
}
/* UseableStuff vt+0xD8 (0x299460): the base step 0x54A680, then the battery charged from the received power +0x3C0 x the frame's dt.
   On a game another game writes the piece for, the base step runs and the battery write is put back. */
void detour_pwCharge(void* b)
{
    if (RoleIsSingle()) { orig_pwCharge(b); return; }
    if (BdOnMain() == 0)
    {   /* off the main thread: gated by this game's writer verdict (ProdStepWriterHere's, else the main thread's published one -
           H-P47a: PwStepWriter), the look queued for the main thread */
        ::InterlockedIncrement64(&g_pwOffMainCharge);
        int fromPub = 0;
        const int w = PwStepWriter(b, &fromPub);
        if (fromPub != 0) ::InterlockedIncrement64(&g_pwOffMainPub);
        if (powerwire::PowerGateRuns(0, w) != 0) orig_pwCharge(b); else PwChargeGated(b);
        PwMark(b, 0);
        return;
    }
    PwRow* r = PwRowFor(b);
    const int writer = (r != 0) ? r->writer : ProdStepWriterHere(b);
    if (powerwire::PowerGateRuns(0, writer) != 0) orig_pwCharge(b); else PwChargeGated(b);
    if (r != 0) PwLook(b, *r);
}
/* ProductionBuilding vt+0x5B8 (0x29A330): each input record's stock lowered by amount x its rate, a fuel item taken from the
   building's inventory each time the stock runs out. A generator (+0x3BC > 0) another game writes burns no fuel here; any other
   building keeps the production gate's own rules (items.cpp detour_prodOperate). */
void detour_pwInputs(void* b, float amount)
{
    float out = 0.0f;
    if (RoleIsSingle() || PwFloatPod(b, kPwOutput, &out) == 0 || !(out > 0.0f)) { orig_pwInputs(b, amount); return; }
    int writer = coopfarm::kProdWriterNone;
    PwRow* r = 0;
    if (BdOnMain() != 0) { r = PwRowFor(b); writer = (r != 0) ? r->writer : ProdStepWriterHere(b); }
    else
    {   /* off the main thread: ProdStepWriterHere's verdict, else the main thread's published one (H-P47a: PwStepWriter) */
        ::InterlockedIncrement64(&g_pwOffMainFuel);
        int fromPub = 0;
        writer = PwStepWriter(b, &fromPub);
        if (fromPub != 0) ::InterlockedIncrement64(&g_pwOffMainPub);
    }
    if (powerwire::PowerGateRuns(0, writer) == 0) { ::InterlockedIncrement64(&g_pwGatedBurn); if (BdOnMain() == 0) PwMark(b, 0); return; }
    orig_pwInputs(b, amount);
    if (r != 0) PwLook(b, *r);   /* the main thread: the step that empties a stock is looked at here (PowerStockEmptied sends it at once) */
    else if (BdOnMain() == 0) PwMark(b, 0);
}
void* PwResolve(const std::string& key)
{
    if (g_pwHint.size() > 1024) g_pwHint.clear();
    void*& h = g_pwHint[key];
    h = ObjectByPositionKeyHint(key.c_str(), h);
    if (h == 0) { g_pwHint.erase(key); return 0; }
    return h;
}
/* the row of a piece the drain resolved by key (alive now): the detour's next refresh keeps it while the key is the same */
PwRow& PwRowResolved(void* b, const std::string& key, int prod)
{
    PwRow& r = g_pwRows[b];
    if (r.key != key) { r = PwRow(); r.key = key; r.power = 1; r.prod = prod; PwPublish(b, coopfarm::kProdWriterNone, ::GetTickCount()); }   /* H-P47a: another piece at this address - the old verdict withdrawn until PwRowFor decides again */
    r.seenAt = ::GetTickCount();
    return r;
}
/* K2: each piece an off-main step or a mod write marked, re-proved by key (the hint only once the resolver's liveness walk finds it)
   and looked at once per drain; a key with no live piece here now is stale (it unloaded, or is gone) */
void PwMarkDrain()
{
    if (::InterlockedCompareExchange(&g_pwMarkInit, 0, 0) == 0) return;
    /* tried, never waited for: the K2 safe point holds the engine's worker, which may hold the lock - its marks wait one drain;
       the main thread's own list needs no lock and is taken either way */
    int n = 0;
    if (::TryEnterCriticalSection(&g_pwMarkCs) != 0)
    {
        n = g_pwMarkN;
        if (n > 0) std::memcpy(g_pwMarkTake, g_pwMarkQ, sizeof(PwMarkRec) * (size_t)n);
        g_pwMarkN = 0;
        ::LeaveCriticalSection(&g_pwMarkCs);
    }
    else ::InterlockedIncrement64(&g_pwMarkBusy);
    std::vector<PwMarkRec> mine;
    mine.swap(g_pwMarkMain);
    const int all = n + (int)mine.size();
    if (all == 0) return;
    std::set<std::string> done;
    for (int i = 0; i < all; ++i)
    {
        ++g_pwMarkDrained;
        const PwMarkRec& m = (i < n) ? g_pwMarkTake[i] : mine[(size_t)(i - n)];
        const std::string key(m.key);
        if (!done.insert(key).second) { ++g_pwMarkSame; continue; }   /* marked twice before one drain: one look */
        void* b = ObjectByPositionKeyHint(key.c_str(), m.hint);
        if (b == 0) { ++g_pwMarkStale; continue; }
        PwRow* r = PwRowFor(b);
        if (r == 0) continue;   /* the row table is full: counted as rowFull */
        PwLook(b, *r);
        ++g_pwMarkLooked;
    }
}
/* K2: the writers' POWER_STATEs written onto this game's pieces */
void PwApplyStates()
{
    std::map<std::string, PwIn> in;
    in.swap(g_pwIn);
    const DWORD now = ::GetTickCount();
    for (std::map<std::string, PwIn>::const_iterator i = in.begin(); i != in.end(); ++i)
    {
        const powerwire::PowerMsg& m = i->second.m;
        const int from = i->second.from;
        if (i->second.tried != 0 && (DWORD)(now - i->second.tried) < kPwDecideMs && (DWORD)(now - i->second.at) <= kPwWaitMs)
        {   g_pwIn[i->first] = i->second; continue; }   /* kept and decided less than kPwDecideMs ago: not asked again yet */
        void* b = PwResolve(i->first);
        char line[480];
        if (b == 0) { ++g_pwNoPiece; continue; }   /* not loaded here: the writer sends it again within kPwRefreshMs */
        char cls[64];
        BdClassOf(b, cls, (int)sizeof cls);
        int slot = -1;
        const int w = (BdIsPowerClass(cls) != 0) ? ProdWriterDecide(b, &slot) : coopfarm::kProdWriterNone;
        const int fromW = (BdIsPowerClass(cls) != 0) ? powerwire::PowerStateFrom(w, slot, from) : 0;
        if (fromW < 0)
        {   /* another game writes it but its slot or the sender's is not known now: kept (the latest per key) up to kPwWaitMs */
            if ((DWORD)(now - i->second.at) <= kPwWaitMs) { PwIn k = i->second; k.tried = now; g_pwIn[i->first] = k; continue; }
            ++g_pwWaitDropped;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[POWER] <- POWER_STATE key=%.63s from slot%d DROPPED: kept %u ms and the writer's slot (slot%d) or the sender's is still not known; powerWaitDropped=%lld",
                        i->first.c_str(), from, (unsigned int)(now - i->second.at), slot, g_pwWaitDropped);
            PwSay(powerwire::kPowerSayEvent, line);
            continue;
        }
        if (fromW == 0)
        {
            ++g_pwRefused;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[POWER] <- POWER_STATE key=%.63s from slot%d REFUSED: %s (class '%.48s', writer %s slot%d); powerRefused=%lld",
                        i->first.c_str(), from, BdIsPowerClass(cls) == 0 ? "not a power class" : "this game does not name that game the writer", cls,
                        w == coopfarm::kProdWriterHere ? "this-game" : (w == coopfarm::kProdWriterOther ? "other-game" : "none"), slot, g_pwRefused);
            PwSay(powerwire::kPowerSayEvent, line);
            continue;
        }
        BdPowerPod v0;
        std::memset(&v0, 0, sizeof v0);
        if (BdPowerReadPod(b, &v0) == 0) { ++g_pwFaulted; continue; }   /* a faulted read would write its zeroed pause */
        BdPowerPod v = v0;
        v.on = m.on; v.gets = m.gets; v.battery = m.battery;
        int called = 0, wrote = 0, barSet = 0;
        const int ok = BdPowerWriteRawPod(b, v, 1, 0, &called);
        const int prod = BdIsProdClass(cls);
        if (ok != 0 && prod != 0 && !m.stock.empty())
        {
            zonekeep::KeptStock st[powerwire::kPowerMaxStocks];
            const int n = (int)m.stock.size();
            for (int k = 0; k < n; ++k) { st[k].index = k; st[k].amount = m.stock[k]; }
            BdProdWritePod(b, 0, 0.0f, st, n, &wrote, &barSet);
        }
        if (ok == 0) { ++g_pwFaulted; continue; }
        ++g_pwApplied;
        PwRow& r = PwRowResolved(b, i->first, prod);
        const int firstHere = (r.held == 0) ? 1 : 0;
        r.held = 1; r.hOn = m.on != 0 ? 1 : 0;
        const int changed = ((v0.on != 0 ? 1 : 0) != r.hOn || (v0.gets != 0 ? 1 : 0) != (m.gets != 0 ? 1 : 0)) ? 1 : 0;
        const int say = powerwire::PowerSayKind(firstHere, changed, (firstHere != 0 && changed == 0) ? PwPlayerOwned(b) : 0);   /* the owner read only for a first apply */
        if (say != powerwire::kPowerSayNone)
        {
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[POWER] <- POWER_STATE key=%.63s from slot%d applied: power %d->%d getsBattery %d->%d battery %.4f->%.4f stocks %d written%s; powerApplied=%lld",
                        i->first.c_str(), from, v0.on, (int)m.on, v0.gets, (int)m.gets, v0.battery, m.battery, wrote, called != 0 ? " (setPowerOn called)" : "", g_pwApplied);
            PwSay(say, line);
        }
    }
}
/* K2, the writer's side: a switch another game asked for, done once when the asker is the owner's game or a teammate's */
void PwDoAsks()
{
    std::vector<PwAskIn> q;
    q.swap(g_pwAskIn);
    const int me = MySlotForWire();
    for (size_t i = 0; i < q.size(); ++i)
    {
        const PwAskIn& a = q[i];
        void* b = PwResolve(a.key);
        char cls[64];
        cls[0] = 0;
        if (b != 0) BdClassOf(b, cls, (int)sizeof cls);
        int slot = -1, os = -1;
        const int w = (b != 0 && BdIsPowerClass(cls) != 0) ? ProdWriterDecide(b, &slot) : coopfarm::kProdWriterNone;
        const int oc = (b != 0) ? BuildingOwnerClass(b, &os) : coopown::kOwnUnreadable;
        const int owner = (oc == coopown::kOwnMe) ? me : ((oc == coopown::kOwnPeer) ? os : -1);
        const int team = (owner >= 0 && a.from >= 0 && TeamSameAnyThread(owner, a.from)) ? 1 : 0;
        const char* why = 0;
        if (b == 0) why = "no such piece here";
        else if (BdIsPowerClass(cls) == 0) why = "not a power class";
        else if (powerwire::PowerAskAccept(w) == 0) why = "this game is not the piece's writer";
        else if (powerwire::PowerAskFromOk(a.from, owner, team) == 0) why = "the asker is not the owner's game or a teammate's";
        BdPowerPod v0;
        std::memset(&v0, 0, sizeof v0);
        int called = 0;
        if (why == 0 && BdPowerReadPod(b, &v0) == 0) { why = "the piece's power fields did not read"; ++g_pwAskReadFault; }
        if (why == 0)
        {
            BdPowerPod v = v0;
            v.on = a.on;
            if (BdPowerWriteRawPod(b, v, 1, 0, &called) == 0) why = "the write faulted";
        }
        if (why == 0) { ++g_pwAskDone; PwMark(b, 1); } else ++g_pwAskRefused;   /* marked: the next drain's look sends the switch */
        char line[420];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[POWER] <- POWER_ASK key=%.63s from slot%d switch %d: %s%s (owner slot%d); powerAskDone=%lld powerAskRefused=%lld",
                    a.key.c_str(), a.from, a.on, why == 0 ? "DONE" : "REFUSED - ", why == 0 ? (called != 0 ? " (setPowerOn called; the POWER_STATE carries it)" : " (already so)") : why,
                    owner, g_pwAskDone, g_pwAskRefused);
        PwSay(powerwire::kPowerSayEvent, line);
    }
}
/* K2, the asker's side: the switch moved here is asked of the writer and, once the ask is sent, put back to the writer's */
void PwSendAsks()
{
    std::vector<PwAskOut> q;
    q.swap(g_pwAskOut);
    for (size_t i = 0; i < q.size(); ++i)
    {
        void* b = PwResolve(q[i].key);
        if (b == 0) continue;
        std::map<void*, PwRow>::iterator it = g_pwRows.find(b);
        if (it == g_pwRows.end() || it->second.held == 0 || it->second.key != q[i].key) continue;
        BdPowerPod v;
        std::memset(&v, 0, sizeof v);
        if (BdPowerReadPod(b, &v) == 0 || (v.on != 0 ? 1 : 0) == it->second.hOn) continue;
        const int want = v.on != 0 ? 1 : 0;
        powerwire::PowerMsg m;
        m.kind = powerwire::kPowerAsk; m.key = q[i].key; m.what = powerwire::kAskSwitch; m.value = want != 0 ? 1.0f : 0.0f;
        std::vector<char> bytes;
        const int slot = q[i].slot;
        /* the writer by its own slot only - DISMANTLE_ASK's road (BdDismantleGate's reach, BdDismantleAskDrain's answer): never the
           raw session peer, which may be a third game */
        const bool sent = slot >= 0 && net::BuildSlotTarget(slot) == slot && powerwire::EncodePower(&bytes, m) && net::SendPowerToSlot(bytes, slot);
        if (sent) ++g_pwAsks; else ++g_pwAskFail;
        int called = 0;
        if (sent || slot < 0)
        {   /* put back once asked (the writer's POWER_STATE brings the switch it sets), or at once when the writer's slot is not known:
               there is no one to ask, and the switch must not stand on this game's grid */
            v.on = it->second.hOn;
            if (BdPowerWriteRawPod(b, v, 1, 0, &called) != 0 && called != 0) ++g_pwUndone;
        }
        else if (coopfarm::MineSayDue(g_pwAskFail) == 0) continue;   /* the writer's slot is known but its road is down: the switch stays as the player set it, asked again after kPwAskGapMs, until the writer's next POWER_STATE writes its own */
        char line[420];
        if (!sent && slot < 0)
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[POWER] -> POWER_ASK key=%.63s switch %d NOT SENT (the writer's slot is not known); the switch here put back to the writer's %d; powerAsks=%lld powerAskFail=%lld",
                        q[i].key.c_str(), want, it->second.hOn, g_pwAsks, g_pwAskFail);
        else if (sent)
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[POWER] -> POWER_ASK key=%.63s switch %d to slot%d sent; the switch here put back to the writer's %d%s; powerAsks=%lld powerAskFail=%lld powerUndone=%lld",
                        q[i].key.c_str(), want, slot, it->second.hOn, called != 0 ? "" : " (setPowerOn not called)", g_pwAsks, g_pwAskFail, g_pwUndone);
        else
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[POWER] -> POWER_ASK key=%.63s switch %d to slot%d NOT SENT (no road to the writer by its own slot); the switch here left as the player set it until the writer's next POWER_STATE; powerAsks=%lld powerAskFail=%lld",
                        q[i].key.c_str(), want, slot, g_pwAsks, g_pwAskFail);
        PwSay(powerwire::kPowerSayEvent, line);
    }
}
} // namespace

/* P47, MAIN THREAD (session dispatch): kept for the K2 safe point */
void PowerNoteRecv(const powerwire::PowerMsg& m, unsigned int fromPeer)
{
    const int from = net::PlayerSlotOfKey(fromPeer);
    if (m.kind == powerwire::kPowerAsk)
    {
        ++g_pwAskRecv;
        if (g_pwAskIn.size() >= kPwAskCap) { ++g_pwDropped; return; }   /* the asker asks again when its switch moves again */
        PwAskIn a;
        a.key = m.key; a.from = from; a.on = m.value > 0.5f ? 1 : 0;
        g_pwAskIn.push_back(a);
        return;
    }
    ++g_pwRecv;
    if (g_pwIn.find(m.key) == g_pwIn.end() && g_pwIn.size() >= kPwInCap) { ++g_pwDropped; return; }   /* the writer sends it again */
    PwIn& in = g_pwIn[m.key];
    in.m = m; in.from = from; in.at = ::GetTickCount();
}
/* P47, MAIN THREAD: a power piece the mod wrote (the bldset lever, a kept power lay) marked past the gap; any other class is not */
void PowerMarkPiece(void* b)
{
    if (b == 0) return;
    char cls[64];
    cls[0] = 0;
    BdClassOf(b, cls, (int)sizeof cls);
    if (BdIsPowerClass(cls) != 0) PwMark(b, 1);
}
/* P47 (H-P47a), MAIN THREAD (BuildForgetWorld): the world's published verdicts dropped; a busy lock leaves it to the next PowerDrain */
void PowerForgetWorld()
{
    if (PwPubSweep(::GetTickCount(), 1) == 0) g_pwPubClearOwed = 1;
}
/* P47 (H-P47b), MAIN THREAD: a power piece's switch now (1 / 0); -1 = not a power class, or unreadable */
int PowerSwitchNow(void* b)
{
    if (b == 0) return -1;
    char cls[64];
    cls[0] = 0;
    BdClassOf(b, cls, (int)sizeof cls);
    if (BdIsPowerClass(cls) == 0) return -1;
    BdPowerPod v;
    std::memset(&v, 0, sizeof v);
    if (BdPowerReadPod(b, &v) == 0) return -1;
    return v.on != 0 ? 1 : 0;
}
/* P47 (H-P47b), MAIN THREAD (BdApplyOne, right after the mod's own completion apply vt+0x230 FLT_MAX - the engine's complete-now path
   switches the piece on): on a game another game writes the piece for (ProdWriterDecide, live) that holds the writer's last switch
   (its row's held / hOn, the row's key the piece's key now), the copy's switch is put back to the writer's with the write the
   POWER_STATE apply uses (BdPowerWriteRawPod, setPowerOn vt+0x318 when it differs) - the copy then mirrors the writer and PwLook asks
   nothing. No writer state held: nothing written. before = PowerSwitchNow before the call (-1: not a power piece, nothing done).
   One [POWER] completion line per piece per completion, on the change budget. */
void PowerCompletionPutBack(void* b, int before)
{
    if (b == 0 || before < 0) return;
    BdPowerPod v0;
    std::memset(&v0, 0, sizeof v0);
    const int after = (BdPowerReadPod(b, &v0) != 0) ? (v0.on != 0 ? 1 : 0) : -1;
    int slot = -1;
    const int w = ProdWriterDecide(b, &slot);
    char k[kKeyCap];
    k[0] = 0;
    const int hasKey = (ObjectPositionKey(b, k, kKeyCap, "", 3, 0) != 0 && k[0] != 0) ? 1 : 0;
    std::map<void*, PwRow>::const_iterator it = g_pwRows.find(b);
    const int have = (hasKey != 0 && it != g_pwRows.end() && it->second.held != 0 && it->second.key == k
                      && (DWORD)(::GetTickCount() - it->second.seenAt) <= kPwStaleMs) ? 1 : 0;   /* a row the look would wipe as stale holds nothing */
    const int writerOn = (have != 0) ? it->second.hOn : 0;
    const int put = powerwire::PowerCompletionSwitch(w, have, writerOn, after);
    int last = after, called = 0;
    if (put >= 0)
    {
        BdPowerPod v = v0;
        v.on = put;
        BdPowerPod v1;
        std::memset(&v1, 0, sizeof v1);
        if (BdPowerWriteRawPod(b, v, 1, 0, &called) != 0 && BdPowerReadPod(b, &v1) != 0) last = v1.on != 0 ? 1 : 0;
        else last = -1;
    }
    char line[400];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[POWER] completion key=%.63s switch %d->%d -> %d (writer state %s)",
                hasKey != 0 ? k : "?", before, after, last, have == 0 ? "none" : (writerOn != 0 ? "1" : "0"));
    PwSay(powerwire::kPowerSayEvent, line);
}
/* P47, K2 safe point (BuildCopyDrain) */
void PowerDrain()
{
    const DWORD now = ::GetTickCount();
    if ((DWORD)(now - g_pwSweepAt) >= 5000)
    {
        g_pwSweepAt = now;
        for (std::map<void*, PwRow>::iterator it = g_pwRows.begin(); it != g_pwRows.end(); )
        {
            if ((DWORD)(now - it->second.seenAt) > kPwForgetMs) g_pwRows.erase(it++);
            else ++it;
        }
        PwPubSweep(now, 0);   /* H-P47a: the published verdicts of pieces not looked at for kPwForgetMs go with their rows */
    }
    if (g_pwPubClearOwed != 0 && PwPubSweep(now, 1) != 0) g_pwPubClearOwed = 0;   /* H-P47a: a world teardown's clear the busy lock refused */
    if (EngineWritesBlocked()) return;   /* the marks queued before wait; PwMark queues none meanwhile (counted blocked) */
    PwMarkDrain();   /* first: a look on another game's piece may queue an ask PwSendAsks sends below */
    if (g_pwIn.empty() && g_pwAskIn.empty() && g_pwAskOut.empty()) return;
    BdSelfScope scope;   /* the mod's own engine calls */
    PwApplyStates();
    PwDoAsks();
    PwSendAsks();
}
void InstallPower()
{
    ::InitializeCriticalSection(&g_pwMarkCs);   /* before the hooks: PwMark runs from them (InstallBuild runs this once) */
    ::InterlockedExchange(&g_pwMarkInit, 1);
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    g_hkPwCharge = BdHook(base, kPwChargeRva, (void*)&detour_pwCharge, (void**)&orig_pwCharge);
    g_hkPwInputs = BdHook(base, kPwInputsRva, (void*)&detour_pwInputs, (void**)&orig_pwInputs);
    char l[600];
    _snprintf_s(l, sizeof(l), _TRUNCATE, "[POWER] P47 hooks: battery charge PowerBatteryCharge 0x%llX %s, fuel step ProductionUpdateInputs 0x%llX %s"
                " (a power piece another game writes: its battery is not charged and its generator burns no fuel here - the writer's POWER_STATE carries both)",
                kPwChargeRva, HkWord(g_hkPwCharge), kPwInputsRva, HkWord(g_hkPwInputs));
    if (g_hkPwCharge == 1 && g_hkPwInputs == 1) DebugLog(std::string(l));
    else ErrorLog(std::string(l));
}
void ReportPower()
{
    char b[1500];
    _snprintf_s(b, sizeof(b), _TRUNCATE, "[POWER] REPORT power[sent,sendFail,recv,applied,refused,noPiece,faulted,dropped]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                " asks[sent,sendFail,undone,recv,done,refused]=%lld,%lld,%lld,%lld,%lld,%lld gate[gatedBurn,gatedCharge,offMain]=%lld,%lld,%lld"
                " relinked=%lld waitDropped=%lld askReadFault=%lld rows=%u rowFull=%lld hooks[charge,inputs]=%d,%d"
                " look[marked,droppedFull,noKey,drained,same,stale,looked,busy,blocked]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                " said[first,change]=%d,%d offMain[charge,fuel,published,pubBusy]=%lld,%lld,%lld,%lld",
                g_pwSent, g_pwSendFail, g_pwRecv, g_pwApplied, g_pwRefused, g_pwNoPiece, g_pwFaulted, g_pwDropped,
                g_pwAsks, g_pwAskFail, g_pwUndone, g_pwAskRecv, g_pwAskDone, g_pwAskRefused,
                (long long)g_pwGatedBurn, (long long)g_pwGatedCharge, (long long)g_pwOffMainCharge + (long long)g_pwOffMainFuel,
                g_pwRelinked, g_pwWaitDropped, g_pwAskReadFault, (unsigned int)g_pwRows.size(), g_pwRowFull, g_hkPwCharge, g_hkPwInputs,
                (long long)::InterlockedCompareExchange64(&g_pwMarked, 0, 0), (long long)::InterlockedCompareExchange64(&g_pwMarkFull, 0, 0),
                (long long)::InterlockedCompareExchange64(&g_pwMarkNoKey, 0, 0), g_pwMarkDrained, g_pwMarkSame, g_pwMarkStale, g_pwMarkLooked,
                (long long)::InterlockedCompareExchange64(&g_pwMarkBusy, 0, 0), (long long)::InterlockedCompareExchange64(&g_pwMarkBlocked, 0, 0),
                g_pwSaidFirst, g_pwSaidEvent,
                (long long)::InterlockedCompareExchange64(&g_pwOffMainCharge, 0, 0), (long long)::InterlockedCompareExchange64(&g_pwOffMainFuel, 0, 0),
                (long long)::InterlockedCompareExchange64(&g_pwOffMainPub, 0, 0), g_pwPubBusy);
    DebugLog(std::string(b));
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
        char pl[420];
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
    char p15f[800];   /* P15 fold 1; T-221 wall progress */
    _snprintf_s(p15f, sizeof(p15f), _TRUNCATE, "[BUILD] REPORT destroyedFold1[testAlreadyRuin,destroyDeferRetried,destroyDeferGaveUp,repairDeferred,copyWallRuinKept]=%lld,%lld,%lld,%lld,%lld"
                " (destroyPosted counts every deferred ruin try; destroyFaulted no longer counts a deferred repair)"
                " wallSync[groupMarked,groupResolves,groupLooks,groupCacheCleared,ownApplied,groupApplied,completeApplied,completeSlow,keptOwnRecComplete,keptOwnRefused]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                " wallCompleteNoop=%lld",
                g_testAlreadyRuin, g_destroyDeferRetried, g_destroyDeferGaveUp, g_repairDeferred, g_copyWallRuinKept,
                g_wallGroupMarked, g_wallGroupResolves, g_wallGroupLooks, g_wallGroupCacheCleared, g_wallOwnApplied, g_wallSharedApplied,
                g_wallCompleteApplied, g_wallCompleteSlow, g_keptOwnRecComplete, g_keptOwnRefused, g_wallCompleteNoop);
    DebugLog(std::string(p15f));
    char dg[400];   /* T-805 */
    _snprintf_s(dg, sizeof(dg), _TRUNCATE, "[BUILD] REPORT dismantleGate[refusedMember,refusedOther,testOrder,askSent,askOffline,askRepeat,ansDone,ansRefused,askRecv,askDone,askRefused,askFaulted,askDropped"
                ",offMainRefused,instantOnCopy]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%ld,%lld",
                g_dismantleRefusedMember, g_dismantleRefusedOther, g_testOrder, g_dmAskSent, g_dmAskOffline, g_dmAskRepeat, g_dmAnsDone, g_dmAnsRefused,
                g_dmAskRecv, g_dmAskDone, g_dmAskRefused, g_dmAskFaulted, g_dmAskDropped, (long)g_dmOffMainRefused, g_dmInstantOnCopy);
    DebugLog(std::string(dg));
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
    char h9[700];   /* T-903 */
    _snprintf_s(h9, sizeof(h9), _TRUNCATE, "[BUILD] REPORT help903[givenBuild,givenMaterials,givenRepair,refusedBuild,refusedMaterials,refusedRepair,refusedOff,repairDamagedHelp,repairWorkRec,repairApplied,repairWhole,repairFaulted,friendHelps,applyRefused,runRefused,applyHeld,deadHandlesIgnored]"
                "=%lld,%lld,%lld,%lld,%lld,%lld,%ld,%lld,%lld,%lld,%lld,%lld,%d,%lld,%lld,%lld,%lld harvest[done,refused,carried,lost,noBase]=%lld,%lld,%lld,%lld,%lld",
                g_helpGivenK[0], g_helpGivenK[1], g_helpGivenK[2], g_helpRefusedK[0], g_helpRefusedK[1], g_helpRefusedK[2],
                (long)::InterlockedCompareExchange(&g_helpRefusedOff, 0, 0), g_repairDamagedHelp, g_repairWorkRec, g_helpRepairApplied, g_repairWhole,
                g_repairFaulted, coopbuild::kBuildHelpFriendHelps, g_helpApplyRefused, g_helpRunRefused, g_helpApplyHeld, g_deadHandlesIgnored,
                g_helpHarvested, g_helpHarvestRefused, g_helpHarvestCarried, g_helpHarvestLost, g_helpHarvestNoBase);
    DebugLog(std::string(h9));
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
    char t805b[200];   /* T-805 b */
    _snprintf_s(t805b, sizeof(t805b), _TRUNCATE, "[BUILD] REPORT t805buy[hook,asked,ran,groups,notKeptSet]=%d,%lld,%lld,%lld,%lld",
                g_hkBuyMe, g_buyHeldAsked, g_buyHeldRan, g_buyHeldGroups, g_buyHeldNotKept);
    DebugLog(std::string(t805b));
    char t609u[560];   /* T-609 U */
    _snprintf_s(t609u, sizeof(t609u), _TRUNCATE, "[BUILD] REPORT t609upgrade[upgradeHook,downgradeHook,upgrades,downgrades,offMain,keyChanged,ownRow,lines,leverCalled,leverRefused,rekeyed,rekeyQueued,rekeyDropped]"
                "=%d,%d,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld", g_hkUpgrade, g_hkDowngrade,
                (long long)::InterlockedCompareExchange64(&g_upgSeen, 0, 0), (long long)::InterlockedCompareExchange64(&g_dngSeen, 0, 0),
                (long long)::InterlockedCompareExchange64(&g_upgOffMain, 0, 0), (long long)::InterlockedCompareExchange64(&g_upgKeyChanged, 0, 0),
                (long long)::InterlockedCompareExchange64(&g_upgOwnRow, 0, 0), (long long)::InterlockedCompareExchange64(&g_upgLines, 0, 0),
                g_testUpgrade, g_testUpgradeRefused, g_rekeyed,   /* T-609 U fix: at the END */
                (long long)::InterlockedCompareExchange64(&g_rekeyQueued, 0, 0), (long long)::InterlockedCompareExchange64(&g_rekeyDropped, 0, 0));
    DebugLog(std::string(t609u));
    char p18r[300];   /* P18 fold 2 */
    _snprintf_s(p18r, sizeof(p18r), _TRUNCATE, "[BUILD] REPORT p18f2[rebuildHook,calls,buildingStates,held,overflow,faults]=%d,%lld,%lld,%lld,%lld,%lld",
                g_hkRebuild, (long long)::InterlockedCompareExchange64(&g_rbCalls, 0, 0), (long long)::InterlockedCompareExchange64(&g_rbStates, 0, 0),
                (long long)::InterlockedCompareExchange64(&g_rbHeld, 0, 0), (long long)::InterlockedCompareExchange64(&g_rbOverflow, 0, 0),
                (long long)::InterlockedCompareExchange64(&g_rbFaults, 0, 0));
    DebugLog(std::string(p18r));
    char wn[1000];
    _snprintf_s(wn, sizeof(wn), _TRUNCATE, "[BUILD] REPORT worldNode[scanWorldProduction,scanPlayerProduction,restoreWorldNodeDropped,copyWorldNodeDropped,placeWorldNodeRefused]=%lld,%lld,%lld,%lld,%lld"
                " townWake[queued,woken,notLoaded,areaMismatch,faulted,dropped,already]=%lld,%lld,%lld,%lld,%lld,%lld,%lld townWakeHook=%d townWakeOffMain=%lld townWakeBusy=%lld townWakeSlotMismatch=%lld"
                " showLink[linked,putBack,putBackWild,already,noTown,insideBuilding,fault]=%lld,%lld,%lld,%lld,%lld,%lld,%lld showKeys[in,bad,rows]=%lld,%lld,%u townKeepStore=%lld showEngineLeft=%lld",
                g_scanWorldProduction, g_scanPlayerProduction, g_restoreWorldNodeDropped, g_copyWorldNodeDropped, g_placeWorldNodeRefused,
                (long long)::InterlockedCompareExchange64(&g_twQueued, 0, 0), g_twWoken, g_twNotLoaded, g_twAreaMismatch, g_twFaulted,   /* T-936 */
                (long long)::InterlockedCompareExchange64(&g_twDropped, 0, 0), g_twAlready, g_hkTownFound,
                (long long)::InterlockedCompareExchange64(&g_twOffMain, 0, 0), g_twBusy, g_twSlotMismatch,
                g_slLinked, g_slPutBack, g_slPutBackWild, g_slAlready, g_slNoTown, g_slInside, g_slFault,   /* owner 936: at the END (already..insideBuilding: the last whole look) */
                g_showKeysIn, g_showKeysBad, (unsigned)g_showKeyRows.size(), g_townKeepStore, g_slEngineLeft);   /* T1342: engineLeft as of the last whole look */
    DebugLog(std::string(wn));
    ReportFarm();   /* par16 */
    ReportPower();   /* P47 */
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
/* MAIN THREAD - the build records name a player as the owner of the piece at this P7n key: a live registry row of this world (an own
   piece, or a copy of another player's) not removed or dismantled, this game's own pp.build row waiting to be restored, or a copy-list
   entry (another player's piece, read back from pp.build) once this world's list is read. towngen.cpp asks it only for a building with
   no owner yet (another player's stand-in not arrived after a load) - never for one a real NPC faction owns (review MED 3). 1 yes, 0 no,
   -1 off the main thread or no key. */
/* review MED 2: a group's home that is a player's house is dropped - Ownerships::removeOwnedObject 0x7EB550 on the group's Ownerships with
   the house's handle, the engine purchase's own first step (it blanks the home when it names the house). 1 called, 0 no row or no
   building, -1 faulted. MAIN THREAD */
int BuildHomeDropPod(void* platoon, void* building)
{
    if (platoon == 0 || building == 0 || kOwnRemoveRva == 0) return 0;
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    __try
    {
        void* own = ((BdGetPtrFn)((*(void***)platoon)[0x98 / 8]))(platoon);
        if (own == 0) return 0;
        ((BdOwnListFn)(base + (uintptr_t)kOwnRemoveRva))(own, (char*)building + kRootHandle);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int BuildKeyPlayerPiece(const char* key)
{
    if (key == 0 || key[0] == 0 || BdOnMain() == 0) return -1;
    const std::string k(key);
    std::map<std::string, BdReg>::iterator it = BdRowByLiveKey(k);
    if (it != g_reg.end() && it->second.gen == g_bdGen && it->second.removed == 0 && it->second.dismantled == 0) return 1;
    if (BdCopyOwnRowAt(k) != 0) return 1;
    if (g_clListPending == 0)
    {
        std::map<std::string, BdCopyEnt>::const_iterator c = g_copyList.find(k);
        if (c != g_copyList.end() && c->second.rec.removed == 0) return 1;
    }
    return 0;
}

} // namespace coop
