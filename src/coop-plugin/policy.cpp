// policy.cpp - see policy.h. E36 / decision 40.
//
// ENGINE FACTS THIS FILE RESTS ON (build/read-locks.md; Ghidra decompiles, the game was not launched):
//
//  * `OwnedByAPlayerFaction(Building*)` 0x546340 is `?isThePlayer@Building@@QEBA_NXZ`, i.e.
//    `bool Building::isThePlayer() const` (review-p6h LOW-4), and THE RVA BELOW IS THAT SYMBOL'S. RE_Kenshi's
//    function-pointer table (RE_Kenshi\RVAs\Steam_1.0.65.br) holds 0x00546340 in SLOT 1256, which is the slot
//    `?isThePlayer@Building@@QEBA_NXZ` resolves to - tools/resolve_stub.py, re-run and
//    re-checked at P6r's patch time (2026-09-04). Re-check it in one command:
//        python tools/resolve_stub.py isThePlayer
//    The number is KEPT as a constant rather than resolved in the plugin, because resolve_stub.py is an offline
//    tool and nothing at runtime reads that table. So the tie is made where it can be made: build/patch_p6r.py
//    refuses to run at all unless the table still answers 0x546340 for slot 1256. If the game or that table
//    updates, the next patch STOPS instead of hooking whatever now lives at this address - which is what F020's
//    never-hardcode-an-RVA rule is protecting against, kept in the only place a build-time-only resolver can
//    keep it. Verified as well by decompile, prologue read and xref scan. The engine's own name is the narrower one -
//    `isThePlayer` - and it is worth keeping in mind when reading the caller list: it is
//        `getOwnerFaction() != 0 && getOwnerFaction()->isPlayer(Faction+0x250) != 0`
//    and it is the ONLY ownership term in the engine's container access gate,
//    `UseableStuff::getDefaultTask` 0x2991F0, where it SHORT-CIRCUITS the whole storage-box branch: a box a
//    player faction owns never has its lock consulted at all, and a box it does not own gives LOOT_TARGET when
//    unlocked and MOVE_CUS_ORDERED / PICK_LOCK when locked.
//
//  * XREFS, read out of the shipped binary rather than assumed (build/patch_p6h.py's own scan of .text for
//    E8/E9 rel32 targets, 2026-09-04): there is exactly ONE reference to 0x546340 in .text - a `jmp` thunk at
//    RVA 0x53913 - and SIXTEEN calls to that thunk, two of them inside getDefaultTask (0x2991F9 the container
//    branch, 0x2992AD the cage branch). Hooking 0x546340 therefore catches every out-of-line caller.
//
//  * ONLY TWO OF THOSE SIXTEEN CALLERS ARE THE CLICK GATE (review-p6h HIGH-3). The other fourteen are
//    ProductionBuilding::captureSaveState and restoreSaveState, NavMeshGenerator::generateTaskBT (a worker
//    thread), RootObjectFactory::createBuilding, PlayerInterface::buildingSelected x6, DoorStuff::getGUIData,
//    setupMiningResourceLevel and two unnamed - none of which decision 40 reasoned about, and two of which
//    branch on the answer during ordinary load and save. The detour therefore remaps ONLY when the return
//    address is one of getDefaultTask's own two call sites, and every other caller gets orig's answer untouched.
//
//  * `UseableStuff::getMouseCursor` 0x546C30 and `DoorStuff::getMouseCursor` 0x547010 do NOT appear in that
//    caller list: both INLINE the same two reads (`getOwnerFaction()`, then `+0x250`). Until P6m that made a real
//    UI-versus-behaviour mismatch - under `shared` the CLICK on a locked peer box looted it while the CURSOR
//    still showed the pick-lock icon. Neither function has a single direct caller: each is reached only through
//    a vtable slot that points at a `jmp` thunk into its body (0x3BCBE for 0x546C30, four vtable slots;
//    0x4EC7E for 0x547010, one), so hooking the BODY catches every dispatch, exactly as the 0x546340 hook does.
//    Both are hooked below as PURE RETURN-VALUE REMAPS - orig is called, and only its ANSWER changes.
//
//  * SIX CLICK-HANDLER CALL SITES ARE ANSWERED TOO, SINCE P6U - AND SINCE P7C THE DOOR PANEL'S IS NOT
//    (review-p6u HIGH-1 and HIGH-3). P6u put SEVEN GUI return addresses in the set and described all seven as
//    presentation. Six of them are `PlayerInterface::buildingSelected` 0x7FBB40, which is NOT panel
//    construction: it picks the mouse cursor and IT ISSUES THE ORDER (addOrderSelectedCharacters 0x7F9280,
//    newPlayerTaskSelectedCharacters 0x7F9AB0, addTaskNearestSelectedCharacter 0x7FA2D0, playerMove 0x7F9CB0;
//    there is no datapanel call anywhere in the function). Those six STAY - a peer building clicking like ours
//    is what decision 40 asks for, and 0x7FBF92 is where a storage box's click is gated (the bullet below).
//    THE SEVENTH, 0x3044BF, IS OUT. It is `DoorStuff::getGUIData` 0x3040E0, which keeps the answer in
//    `r14b` and tests it four times; two of those blocks bind `DoorStuff::openButton` 0x546520 and
//    `DoorStuff::lockButton` 0x5465D0 as datapanel delegates. `lockButton` toggles `wantsToLock` (+0x384),
//    writes `DoorLock+0x20` and recomputes the navmesh gate code (updateGateCodeState 0x297250), and
//    `DoorLock::serialise` 0x29FF90 round-trips that into the building's own zone record - so answering that
//    one site puts a LOCK BUTTON ON THE OTHER PLAYER'S DOOR, and a press of it is a cross-player world-state
//    write with nothing on the wire to carry it. Until door locks are synced (E37) the peer's door panel stays
//    the stranger's, and the site is counted as `ownedByPeerDoorPanelPassedThrough` rather than left silent.
//    Note the route: the lock button hangs off getGUIData, NOT off buildingSelected - I scanned
//    0x7FBB40..0x7FC579 for every rel32 and every rip-relative displacement that could reach 0x5465D0 or its
//    thunk 0x51F6E and there are none - which is why the containment is this one constant and not one of the
//    six. The nine non-GUI callers - serialise, loadFromSerialise, the navmesh, createBuilding,
//    setupMiningResourceLevel and the two unnamed - still get the engine's own answer and are still counted as
//    `ownedByPeerPassedThrough`.
//
//  * WHICH CLASS A PLAYER'S STORAGE BOX IS HAS NEVER BEEN ESTABLISHED, AND IT DECIDES WHETHER THE CLICK GATE
//    EVER MATTERED FOR ONE (review-p6m HIGH-1; PROBE P029 measures it). The cursor remap reaches five classes
//    through the two hooked bodies; the click gate reaches exactly one of them, because slot 131
//    (`getDefaultTask`) is a DIFFERENT function in each. Read out of the shipped binary by an RTTI walk
//    plus a resolve of each vtable's slot-131 thunk (2026-09-04, the game was not launched):
//        StorageBuilding  vtable 0x16AF7A8  getDefaultTask 0x2AB390 = `B8 1A 00 00 00 C3` -> constant 0x1A
//        UseableStuff     vtable 0x16B11C8  getDefaultTask 0x2991F0 = THE GATED ONE
//        ResearchBuilding vtable 0x16B18C8  getDefaultTask 0x2ADDA0 = constant 0x57
//        TurretBuilding   vtable 0x16D2F28  getDefaultTask 0x4401B0 = constant 0x92
//        DoorStuff        vtable 0x16B0AB8  getDefaultTask 0x2ADBC0 = 0x48/0x49 by [this+0x380], no faction term
//        LightBuilding    vtable 0x16E9B58  getDefaultTask 0x2991F0 = THE GATED ONE (but its cursor is the
//                                            constant 0x14 at 0x546D30, so its own half of the pair is absent)
//    THREE OF THOSE ARE CONSTANT RETURNS WITH NO OWNERSHIP TERM AT ALL. P6U SHIPPED THE WRONG CONCLUSION FROM
//    THAT, AND P7C CORRECTS IT (review-p6u HIGH-2). The sentence this file used to carry - "if a player's
//    storage box is a StorageBuilding, the shared click gate has never applied to one" - is FALSE, and it was
//    already false in the commit that installed it. `StorageBuilding::getDefaultTask` really is a constant
//    with no ownership term; the ownership decision for a CLICK on one simply never lived there. IT LIVES AT
//    `PlayerInterface::buildingSelected` 0x7FBF92, which is one of the six sites answered above:
//        0x7FBF86  ff 90 18 04 00 00   call [rax+0x418]   ; vtable slot 131 -> 0x1A for a StorageBuilding
//        0x7FBF89  44 8b e8            mov  r13d,eax      ; the task is taken FIRST
//        0x7FBF92  e8 7c 79 85 ff      call 0x53913       ; ...and THEN the ownership question is asked
//        0x7FC18E  41 83 fd 1a         cmp  r13d,0x1A     ; the "ours" dispatcher handles the box task by name
//    SO THE `shared` REMAP DOES REACH PLAYER BOXES - through the click handler, not through getDefaultTask -
//    and review-p6m HIGH-1's practical concern is closed by code rather than by a run. What a run still owes
//    is the CLASS itself: PROBE P029 logs the box's vtable RVA against the table above, which is the half only
//    a run can supply. Read `ownedByPeerAnswered` (getDefaultTask's own two sites) accordingly: 0 there is the
//    EXPECTED reading for a StorageBuilding box, and `ownedByPeerAnsweredClick` is where its gate shows up.
//    ONE CAVEAT, AND IT IS NOT COUNTED (review-p6u MEDIUM-2): the "ours" dispatcher then runs an INLINED
//    ownership test at 0x7FC241 - `getOwnerFaction()` followed by `cmp qword ptr [rax+0x250],0` - which this hook
//    cannot reach, because it is not a call. If the `coop-peer` stand-in does not carry Faction+0x250 (that is
//    PROBE P028's bit, still unmeasured) the click still ends in a message rather than an order. So
//    `ownedByPeerAnsweredClick` climbing means THE GATE ANSWERED, not THE ORDER WENT OUT.
//
//  * `Building::owner` is +0x10 and its faction's string id is `Faction+0x240 -> GameData+0x58`. That string
//    is what a zone record carries under "owner faction ID", and it is what store.cpp's TranslateZoneOwners
//    swaps: a zone the OTHER game wrote has <my player faction id> rewritten to "coop-peer" before any
//    Building exists. Both real player factions carry the SAME string id on disk - which is exactly why the
//    swap has to exist - so on this game "coop-peer" IS the peer's player faction, and my own id is mine.
#include "policy.h"
#include "addresses.h"   /* P8h: kXxxRva below is filled from the address table, not hard-coded */
#include "store.h"
#include "playerfaction.h"
#include "net/session.h"          /* PlayerSlotOfKey: the requester's slot from the key its request arrived with */
#include "../common/slotwire.h"   /* stand1: coop-p<n> ids */
#include "team.h"                  /* T-546 step 4: the team index - a teammate's building opens to this player */
#include "zones.h"                 /* NearestBuildingWhere: the `team access` lever's building */
#include "../common/teameffect.h"  /* T-546 step 4: MemberOpens */
#include "../common/crimewire.h"   /* T-546 step 4b: SightStubBuild - the same cell stub crime.cpp's sight1 patch uses */
#include "../common/teamorders.h" /* T-546 step 7: the orders a teammate's character is not offered */
#include "peace.h"   /* T-546 (owner 512): the team set the isAllyOf / isEnemyOf detours answer from */
#include "game/GameWorld.h"       /* T-546 step 7: the `team orders` lever walks activeCharacters() */
#include "coop_log.h"
#include "hooks.h"   /* coop::AddHook (own MinHook) / HookStatus - same include items.cpp uses for its four hooks */
#include <Windows.h>
#include <intrin.h>   /* _ReturnAddress: which of OwnedByAPlayerFaction's sixteen callers is asking */
#include <sstream>
#include <cstdio>
#include <cstring>   /* strcmp: the stand-in guard compares the faction's OWN string id, not only its address */

namespace {

/* PS/PN rather than S/N: every module here keeps its own, and two identically named helpers reachable in one
   translation unit is the C2668 shape the project's build notes warn about. */
template <class T> std::string PS(const T& v) { std::ostringstream o; o << v; return o.str(); }
std::string PN(long long v) { char b[32]; sprintf(b, "%lld", v); return b; }
/* an RVA is unreadable in decimal: the sampled return addresses are printed the way every address in this
   project's notes is written, and -1 (never seen) is printed as such rather than as 0xFFFFFFFFFFFFFFFF. */
std::string PH(long long v) { if (v < 0) return std::string("-1"); char b[32]; sprintf(b, "0x%llX", (unsigned long long)v); return b; }

// the module's own copy, as every module here keeps one (identity.cpp's rule)
bool Plaus(const void* p) { return p != 0 && (uintptr_t)p > 0x10000 && (uintptr_t)p < 0x00007FFFFFFFFFFFull; }

const char* const kPeerSid = "coop-peer";   /* stand1: the protocol-67 id - a save may still carry it */
/* stand1 (docs/design-profiles1.md s2 Required 4): a stand-in BY ITS OWN RECORD ID - coop-p<n> (one per player slot), or a
   save's legacy coop-peer. The owner test asks this, not "is it the peer". */
bool IsStandInSid(const char* s) { return s != 0 && (coopslot::StandInSlotOfId(std::string(s)) >= 0 || std::strcmp(s, kPeerSid) == 0); }
/* `?isThePlayer@Building@@QEBA_NXZ` = RVA-table SLOT 1256 (RE_Kenshi\RVAs\Steam_1.0.65.br), asserted
   equal to this value by build/patch_p6r.py before it would edit a line of source; also verified by decompile,
   prologue read and an xref scan of .text. The header comment says why the number is kept here rather than
   resolved at runtime, and gives the one command that re-checks it. */
unsigned long long kOwnedByAPlayerFactionRva = 0; static coop::AddrReg kOwnedByAPlayerFactionRva_reg("OwnedByAPlayerFaction", &kOwnedByAPlayerFactionRva);   /* P8h: the address table fills this. Steam_1.0.65 0x546340 */

// THE TWO CALL SITES INSIDE UseableStuff::getDefaultTask 0x2991F0, AS RETURN ADDRESSES. Read out of the shipped
// kenshi_x64.exe on 2026-09-04 (PE section map + rel32 scan; the game was not launched):
//     0x2991F9: E8 15 A7 DB FF -> the thunk 0x53913 -> 0x546340   =>  returns to 0x2991FE   (container branch)
//     0x2992AD: E8 61 A6 DB FF -> the thunk 0x53913 -> 0x546340   =>  returns to 0x2992B2   (state-8 / lock branch)
// A five-byte E8 call returns to the byte after it, and the thunk is a bare tail `jmp` that adds no frame, so
// the address on the detour's stack is getDefaultTask's own. These are RVAs and are compared against
// _ReturnAddress() - g_base; a return address from anywhere else is a caller decision 40 never reasoned about.
static unsigned long long kGetDefaultTaskRet1 = 0; static coop::AddrReg kGetDefaultTaskRet1_reg("GetDefaultTaskRet1", &kGetDefaultTaskRet1);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x2991FE */
static unsigned long long kGetDefaultTaskRet2 = 0; static coop::AddrReg kGetDefaultTaskRet2_reg("GetDefaultTaskRet2", &kGetDefaultTaskRet2);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x2992B2 */

// THE SIX CLICK-HANDLER CALL SITES, AS RETURN ADDRESSES (P6u put seven here; P7c removed the door panel's -
// see kDoorPanelRetNotAnswered below and review-p6u HIGH-3). Same method and same binary as the two above -
// every one re-read out of the shipped kenshi_x64.exe, and every one cross-checked against the .pdata
// RUNTIME_FUNCTION table so "inside that function" is the unwind table's answer rather than a guess. All six
// lie inside `PlayerInterface::buildingSelected` 0x7FBB40..0x7FC579
// (?buildingSelected@PlayerInterface@@QEAA_NPEAVBuilding@@AEBVVector3@Ogre@@_N@Z, exports_all.txt:5445):
//     0x7FBB89: E8 85 7D 85 FF -> 0x53913  =>  returns to 0x7FBB8E   a `false` can report the click unconsumed
//     0x7FBC6F: E8 9F 7C 85 FF -> 0x53913  =>  returns to 0x7FBC74   after DoorStuff::isLocked - which cursor
//     0x7FBCE6: E8 28 7C 85 FF -> 0x53913  =>  returns to 0x7FBCEB   a `true` -> addOrderSelectedCharacters
//     0x7FBF92: E8 7C 79 85 FF -> 0x53913  =>  returns to 0x7FBF97   THE STORAGE-BOX CLICK GATE (header above)
//     0x7FC313: E8 FB 75 85 FF -> 0x53913  =>  returns to 0x7FC318   a `false` also needs Inventory::isEmpty
//     0x7FC345: E8 C9 75 85 FF -> 0x53913  =>  returns to 0x7FC34A   cursor 0x18 and a different order branch
// THIS FUNCTION IS NOT PANEL CONSTRUCTION. It picks the cursor and issues the order; it contains no datapanel
// call at all. Answering these six is what makes a peer building CLICK the way ours does under `shared`, which
// is decision 40's whole point - and it is a BEHAVIOUR change, which is how the register now describes it.
// THE NINE NON-GUI CALLERS STAY OUT, and that is the point of the gate: 0x2A2177 (captureSaveState), 0x2A24A1
// (restoreSaveState) - both run on ordinary load and save - 0x3CD74E (the navmesh, a worker thread),
// 0x57E565 (createBuilding), 0x29C792 (setupMiningResourceLevel) and the two unnamed. NEVER REMAP THOSE.
static unsigned long long kClickRets[6] = { 0, 0, 0, 0, 0, 0 };   /* stage 7/9: rows ClickRet1..6. Steam_1.0.65 0x7FBB8E 0x7FBC74 0x7FBCEB 0x7FBF97 0x7FC318 0x7FC34A */
static coop::AddrReg kClickRet1_reg("ClickRet1", &kClickRets[0]);
static coop::AddrReg kClickRet2_reg("ClickRet2", &kClickRets[1]);
static coop::AddrReg kClickRet3_reg("ClickRet3", &kClickRets[2]);
static coop::AddrReg kClickRet4_reg("ClickRet4", &kClickRets[3]);
static coop::AddrReg kClickRet5_reg("ClickRet5", &kClickRets[4]);
static coop::AddrReg kClickRet6_reg("ClickRet6", &kClickRets[5]);
/* review-p6u LOW-3: the count is DERIVED, never kept in step by hand - a constant added or removed above
   without editing a separate count would silently truncate the set or over-read the array. */
const unsigned int kClickRetCount = (unsigned int)(sizeof(kClickRets) / sizeof(kClickRets[0]));

// THE ONE GUI SITE THAT IS DELIBERATELY NOT ANSWERED (P7c; review-p6u HIGH-3). This is the return address
// inside `DoorStuff::getGUIData(DatapanelGUI*, int)` 0x3040E0..0x304E40:
//     0x3044BA: E8 54 F4 D4 FF -> 0x53913  =>  returns to 0x3044BF, then `44 0F B6 F0` movzx r14d,al
// The answer is KEPT in r14b and tested four times (0x3046EA / 0x30490F / 0x304B37 / 0x304D77). Two of those
// blocks bind datapanel delegates: 0x304914 `lea rbx,[rip-0x2EBCB7]` -> thunk 0x18C64 -> DoorStuff::openButton
// 0x546520, and 0x304D7C `lea rdi,[rip-0x2B2E15]` -> thunk 0x51F6E -> DoorStuff::lockButton 0x5465D0.
// ANSWERING IT WOULD PUT A LOCK BUTTON ON THE OTHER PLAYER'S DOOR. lockButton toggles wantsToLock (+0x384),
// writes DoorLock+0x20 and calls updateGateCodeState 0x297250 (which recomputes the navmesh gate code), and
// DoorLock::serialise 0x29FF90 writes lock level / locked / broken / hardness into the building's own
// GAMESTATE_BUILDING (0x23) record - the record store.cpp's TranslateZoneOwners carries between the games.
// There is no per-faction lock state in the engine's record and NOTHING on the wire syncs a door lock, so a
// press would be a cross-player world-state write with no arbitration. Until door locks are synced (E37) this
// site gets the engine's own answer and is counted as `ownedByPeerDoorPanelPassedThrough`.
static unsigned long long kDoorPanelRetNotAnswered = 0; static coop::AddrReg kDoorPanelRetNotAnswered_reg("DoorPanelRetNotAnswered", &kDoorPanelRetNotAnswered);   /* stage 7/9: Steam_1.0.65 0x3044BF.  NOT in kClickRets above, and must not be put back
    there without a door-lock sync on the wire (E37). It is named and counted rather than deleted so that the
    exclusion is a visible decision instead of an absence nobody can see. */

// The two cursor functions, and the cursor numbers they return. Every value below is read out of
// build/decomp_546c30.txt and build/decomp_547010.txt, not guessed - see the header comment.
unsigned long long kUseableCursorRva = 0; static coop::AddrReg kUseableCursorRva_reg("UseableCursor", &kUseableCursorRva);   /* P8h: the address table fills this. Steam_1.0.65 0x546C30 */   /* UseableStuff::getMouseCursor - body; vtable thunk 0x3BCBE */
unsigned long long kDoorCursorRva = 0; static coop::AddrReg kDoorCursorRva_reg("DoorCursor", &kDoorCursorRva);   /* P8h: the address table fills this. Steam_1.0.65 0x547010 */   /* DoorStuff::getMouseCursor    - body; vtable thunk 0x4EC7E */
const unsigned int kCursorOwnUse       = 0x9;      /* 0x546C30's answer for ANY player-faction-owned object, locked or not */
const unsigned int kCursorPickLock     = 0xF;      /* a STRANGER's locked object, in both functions */
const unsigned int kCursorLoot         = 0x18;     /* a stranger's lootable container (0x546C30) */
const unsigned int kCursorTypeEightNoContainer = 0xD;   /* 0x546C30, no container and type 8: nobody has characterised it */
const unsigned long long kCursorOwnLockedDoor = 0xE;    /* 0x547010's answer for MY OWN locked door */

uintptr_t g_base = 0;
void* g_peerFaction = 0;          // MAIN THREAD writes, the detour reads: a plain aligned pointer
int   g_peerIsPlayer = -1;        // P028's answer: -1 never observed
bool  g_p028Said = false, g_saidUnknown = false;

// the five the brief names, in this order: basePolicy[checks,allowedShared,allowedOwner,refusedOwner,locked]
volatile LONGLONG g_checks = 0, g_allowedShared = 0, g_allowedOwner = 0, g_refusedOwner = 0, g_locked = 0;
// and the ones that would otherwise be silent
volatile LONGLONG g_unknownEvaluated = 0, g_notPlayerOwned = 0, g_ownerUnreadable = 0, g_lockedRefusedFallback = 0,
                  g_ownedByPeerAnswered = 0, g_hookInstalled = 0;
// review-p6h HIGH-1: how many requests were judged as coming from a PEER because they arrived over the wire.
// It is the number that would have read 0 while the defect was live on a client game, since every request from
// the host took the local branch instead.
volatile LONGLONG g_policyRequesterFromPeer = 0;
// review-p6h HIGH-2: this game's own player faction sid could not be read (mineUnreadable), or the lookup
// resolved to the `coop-peer` stand-in itself (mineWasStandIn, review-p6h MEDIUM-3). Two causes, two counters,
// because one of them means "the read failed" and the other means "the read succeeded and returned the wrong
// faction", and they have different repairs.
// P6u / review-p6m MEDIUM-1: these three are now DISJOINT - one refusal increments exactly one of them, so
// they can be read straight off the report line without subtracting. `mineNotYetKnown` is the one that is not
// a failure at all: LocalPlayerFaction() resolved nothing, which is what a world still loading looks like.
volatile LONGLONG g_mineUnreadable = 0, g_mineWasStandIn = 0, g_mineNotYetKnown = 0;
// review-p6h HIGH-3, narrowed by P7c: an answer this hook WOULD have remapped, handed back unchanged because
// the caller is not in the answered set. Since P7c this is the NINE NON-GUI callers only - serialise,
// loadFromSerialise, the navmesh, createBuilding, setupMiningResourceLevel and the two unnamed. The door
// datapanel is also passed through but has its OWN counter (`ownedByPeerDoorPanelPassedThrough`), because a
// subset hidden inside a total is what review-p6m MEDIUM-1 had to be paid to unpick. The two RVAs below are
// the first two DISTINCT return addresses seen doing it, kept so a run can name the caller instead of
// guessing; -1 means never seen, and a synthetic 0 is never sampled (LOW-2). Written without allocation.
volatile LONGLONG g_ownedByPeerPassedThrough = 0;
volatile LONGLONG g_retSample1 = -1, g_retSample2 = -1;
// The cursor remap (the P6h mismatch, closed): answers changed, and the one answer deliberately NOT changed.
volatile LONGLONG g_cursorRemapped = 0, g_cursorOtherAnswer = 0, g_cursorHooks = 0;
// P7c / review-p6u HIGH-1 + MEDIUM-4: answers changed at the SIX `PlayerInterface::buildingSelected` sites.
// P6u called this `ownedByPeerAnsweredPanel` and split the seven GUI sites click-versus-panel, which was the
// WRONG SEAM: six of the seven are the click/cursor handler and exactly one is the door datapanel. The seam
// that matters is now the one the counters are on. THIS NUMBER MEANS "THE GATE ANSWERED", NOT "THE ORDER WENT
// OUT" - the ours-path for task 0x1A then runs an INLINED ownership test at 0x7FC241 that no hook can reach
// (review-p6u MEDIUM-2), so a click can still end in a message. Counting that would need a second hook on a
// mid-function address; it is owed, not silently implied.
volatile LONGLONG g_ownedByPeerAnsweredClick = 0;
/* doors1 (user decisions 2026-09-24, .modding/02-project-rules.md): under `shared` the "answer the other player's building
   as ours" remap now reaches STORAGE BOXES ONLY (StorageBuilding, the class P029 measured for a player's box) until the user
   decides boxes. Doors keep the owner's lock (no unlock button, no own-door task path for the other player: pick or break
   it), and beds, benches, research and turrets get the engine's stranger behaviour, as at an NPC faction's building. */
static unsigned long long kVtStorageBuilding = 0; static coop::AddrReg kVtStorageBuilding_reg("StorageBuildingVt", &kVtStorageBuilding);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x16AF7A8 - the class table above */
volatile LONGLONG g_ownedByPeerStranger = 0, g_cursorStranger = 0, g_doorCursorStranger = 0;
/* review-doors1 2: the first two DISTINCT classes (vtable RVAs) that lost the remap, so a run names them. */
volatile LONGLONG g_strangerVt1 = -1, g_strangerVt2 = -1;
void PolicySampleStrangerVt(LONGLONG rva)
{
    if (rva == 0) return;
    if (g_strangerVt1 == (LONGLONG)-1) ::InterlockedCompareExchange64(&g_strangerVt1, rva, (LONGLONG)-1);
    if (g_strangerVt1 == rva) return;
    if (g_strangerVt2 == (LONGLONG)-1) ::InterlockedCompareExchange64(&g_strangerVt2, rva, (LONGLONG)-1);
}
/* review-doors1 4: the class test without PolicyVtableRvaPod's rttiOutOfImage count (the cursor hook runs every hovered
   frame and would swamp P029's counter). 0 = unreadable / outside the module. NO std::string (C2712). */
unsigned int PolicyVtableRvaQuietPod(void* obj);
// P7c / review-p6u HIGH-3: the door datapanel's site 0x3044BF, passed through on purpose. DISJOINT from
// `ownedByPeerPassedThrough` (which is now the nine non-GUI callers only), because a subset counter hidden
// inside a total is exactly the defect review-p6m MEDIUM-1 charged for. Every increment is one peer door whose
// datapanel was built with the STRANGER's answer, i.e. one door that did not offer its lock button.
volatile LONGLONG g_ownedByPeerDoorPanelPassedThrough = 0;
/* T-546 step 4 (decision 481): a teammate's building opens to this game's player whatever the policy and whatever its class.
   allowedMember: requests the gate allowed because requester and owner share a team; click / doorPanel / cursor / doorCursor:
   engine answers made "this player's own" for a teammate's building at the click sites, the door panel and the two cursors. */
volatile LONGLONG g_allowedMember = 0, g_memberClick = 0, g_memberDoorPanel = 0, g_memberCursor = 0, g_memberDoorCursor = 0;
volatile LONGLONG g_memberCursorOther = 0;   /* a teammate's type-8 useable with no container: counted, not remapped (as for anyone) */
// P7c / review-p6u MEDIUM-3: a vtable pointer, a Complete Object Locator or a TypeDescriptor that fell OUTSIDE
// this module's image, rejected by the P029 walk. It also counts the case where the image size itself could
// not be read - the walk fails CLOSED rather than falling back to a looser bound - and `imageSize` on the
// report line is what tells those two apart.
volatile LONGLONG g_rttiOutOfImage = 0;
// The module's real SizeOfImage, read out of the PE header once at InstallPolicy. 0 means the header could not
// be read, and the P029 walk then answers `unreadable` for everything.
unsigned int g_imageSize = 0;
// `ownedByPeerAnswered` USED TO BE THE CURSOR GAP'S ONLY NUMBER, on the reasoning that nothing in this plugin
// is ever called on an inlined cursor path so no code here could see the mismatch happen. P6m makes that false:
// the two cursor functions are hooked through their own bodies and `cursorRemapped` counts the answers changed
// there, so the gap has a real number instead of an upper bound. `ownedByPeerAnswered` now means only what its
// name says - answers this hook changed at getDefaultTask's two call sites.

// ---- pure SEH-guarded reads. No std::string is constructed in any function carrying __try (C2712). --------
int CopyStdStringPod(const void* s, char* buf, int cap)
{
    __try
    {
        const size_t len = *(const size_t*)((const char*)s + 0x10);
        const size_t res = *(const size_t*)((const char*)s + 0x18);
        const char* p = (res >= 16) ? *(const char* const*)s : (const char*)s;
        size_t n = len; if (n > (size_t)(cap - 1)) n = (size_t)(cap - 1);
        for (size_t i = 0; i < n; ++i) buf[i] = p[i];
        buf[n] = 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// Faction+0x240 -> GameData, GameData+0x58 -> the std::string stringID. 1 ok, 0 unreadable, -1 faulted.
int FactionSidPod(void* faction, char* buf, int cap)
{
    if (!Plaus(faction)) return 0;
    __try
    {
        const void* gd = *(const void* const*)((const char*)faction + 0x240);
        if (!Plaus(gd)) return 0;
        return CopyStdStringPod((const char*)gd + 0x58, buf, cap) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int IsPlayerFieldPod(void* faction, void** out)
{
    if (!Plaus(faction)) return 0;
    __try { *out = *(void**)((char*)faction + 0x250); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// The building's own getOwnerFaction (virtual slot +0x58). A door OVERRIDES it to forward to its parent building,
// which is why the virtual is called rather than +0x10 being read flat. (An earlier version of this comment
// named that override "DoorStuff::getOwnerFactionDirect 0x2AD6F0"; review-p6h LOW-5 read the bytes there and they are
// mid-instruction, not a prologue. Nothing uses the address - the virtual slot is what is called - so the name
// is dropped rather than replaced by another unverified one.)
typedef void* (*GetFactionVFn)(void*);
void* FactionOfBuildingPod(void* building)
{
    if (!Plaus(building) || ((uintptr_t)building & 7)) return 0;
    __try
    {
        void** vt = *(void***)building;
        if (!Plaus(vt)) return 0;
        void* fn = vt[0x58 / 8];
        if (!Plaus(fn)) return 0;
        return ((GetFactionVFn)fn)(building);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// THE ONE READER OF THIS GAME'S OWN PLAYER FACTION SID. Both the requester's identity and the building's
// classification need it, and review-p6h HIGH-2 is what happens when the same fact is read twice and the two
// reads disagree about what a failure means: the building read failed CLOSED and the local read failed OPEN,
// twelve lines apart. One reader, three distinguishable answers, and every caller uses the distinction.
//
//   1  read, and it is this game's own player faction
//   0  not readable - no faction manager, no player faction in it, or the string could not be read
//   2  READ, but it resolved to the `coop-peer` stand-in itself (review-p6h MEDIUM-3): LocalPlayerFactionImpl
//      returns the FIRST faction in the manager array with Faction+0x250 set, and PROBE P028 exists precisely
//      because the stand-in may be one of those. Array order almost certainly favours the real faction (it is
//      created at world load, the stand-in later), so this is expected to read 0 forever - which is exactly why
//      it is counted rather than assumed.
int LocalPlayerSidPod(char* buf, int cap)
{
    void* mine = (void*)coop::LocalPlayerFaction();   /* qualified: this helper sits in the anonymous namespace, so unqualified lookup does not reach coop:: from here */
    if (mine == 0) return 3;   /* P6u: NOTHING resolved - a distinct state, not a read failure. See below. */
    if (coop::IsStandInFaction((::Faction*)mine)) return 2;   /* stand1 fold (2b): ANY player's stand-in or a save's coop-peer, by address */
    if (FactionSidPod(mine, buf, cap) != 1) return 0;
    if (buf[0] == 0) return 0;
    // P6u / review-p6m MEDIUM-2: THE POINTER TEST ABOVE IS BLIND IN THE ONE WINDOW THAT MATTERS. g_peerFaction
    // is not set until PolicyNotePeerFaction runs, from playerfaction.cpp's CreatePeer - and a stand-in REUSED
    // from the save's records is already in the faction manager array before that. LocalPlayerFactionImpl
    // returns the FIRST faction in that array with Faction+0x250 set, so if the stand-in precedes the real
    // player faction it is what comes back, the address test cannot fire, and this function used to answer 1
    // with the "coop-peer" sid in `buf`. BasePolicyAllows then computes ownerIsPlayerSide with BOTH of its
    // terms equal to the same string, so THIS GAME'S OWN buildings classify as NPC-owned - which the policy
    // deliberately allows. That is review-p6h HIGH-2's fail-open, restored for the length of that window.
    // The sid has just been read, so the guard costs one strcmp and does not depend on g_peerFaction at all.
    if (IsStandInSid(buf)) return 2;   /* stand1: coop-p<n> (or a save's coop-peer) - a stand-in, by its own id */
    return 1;
}

void Bump(volatile LONGLONG* c, bool count) { if (count) ::InterlockedIncrement64(c); }
// ---- T-546 step 4 (decision 481): A TEAMMATE'S BASE -----------------------------------------------------------
// The slot of the player whose stand-in `faction` is: the stand-in table (a pointer compare), else the faction's own record id
// coop-p<n>; -1 for anything else. ANY THREAD.
// The slot a faction's own record id coop-p<n> names, -1 for any other id or an unreadable one. No heap: the id is read into a
// stack buffer and parsed in place (the hook runs on the navmesh worker too). ANY THREAD.
int RecordSlotPod(void* faction)
{
    char id[64]; id[0] = 0;
    if (FactionSidPod(faction, id, 64) != 1) return -1;
    const size_t pl = std::strlen(coopslot::kStandInPrefix);
    if (std::strncmp(id, coopslot::kStandInPrefix, pl) != 0 || id[pl] == 0) return -1;
    if (id[pl] == '0' && id[pl + 1] != 0) return -1;   /* no leading zero, as coopslot::ParseSlotNum */
    int n = 0;
    for (const char* p = id + pl; *p != 0; ++p)
    {
        if (*p < '0' || *p > '9') return -1;
        n = n * 10 + (*p - '0');
        if (n > coopslot::kSlotMax) return -1;
    }
    return n;
}
int StandInSlotAnyPod(void* faction)
{
    if (!Plaus(faction)) return -1;
    int s = coop::StandInSlotOf((::Faction*)faction);   /* pointer compares */
    if (s < 0) s = RecordSlotPod(faction);
    return s;
}
// This game's player is in a team (one load of the team index). ANY THREAD.
bool MineInTeam() { return coop::TeamNoOfSlotAnyThread(coop::MySlotForWire()) != 0; }
// `faction` (a building's owner) is the faction of a player who shares this game's player's team: a stand-in met this session or
// the coop-p<n> faction the save carries (by its record id, as MemberRequest reads the id). ANY THREAD.
bool TeammateFaction(void* faction)
{
    return coop::TeamSameAnyThread(coop::MySlotForWire(), StandInSlotAnyPod(faction));
}
// The cursor hooks' test: the object belongs to a teammate (nothing is read while this game's player is in no team).
bool CursorTeammateObject(void* self) { return MineInTeam() && TeammateFaction(FactionOfBuildingPod(self)); }
// A REQUEST (the item road's gate): the requester and the building's owner are two players who share a team. The slot of a
// faction id: coop-p<n> names n, this game's own player faction's id names this game's slot. Nothing about the building is
// read unless the requester is in a team. ANY CALLER.
bool MemberRequest(void* building, const std::string& requesterSid)
{
    const int me = coop::MySlotForWire();
    if (me < 0 || requesterSid.empty()) return false;
    char mine[160]; mine[0] = 0;
    const bool mineRead = LocalPlayerSidPod(mine, 160) == 1;
    int asker = coopslot::StandInSlotOfId(requesterSid);
    if (asker < 0 && mineRead && requesterSid == std::string(mine)) asker = me;
    if (asker < 0 || coop::TeamNoOfSlotAnyThread(asker) == 0) return false;
    char ownerSid[160]; ownerSid[0] = 0;
    if (FactionSidPod(FactionOfBuildingPod(building), ownerSid, 160) != 1 || ownerSid[0] == 0) return false;
    int owner = coopslot::StandInSlotOfId(std::string(ownerSid));
    if (owner < 0 && mineRead && std::strcmp(ownerSid, mine) == 0) owner = me;
    return swteam::MemberOpens(owner, asker, coop::TeamNoOfSlotAnyThread(owner), coop::TeamNoOfSlotAnyThread(asker));
}

// ---- the OwnedByAPlayerFaction hook -----------------------------------------------------------------------
typedef char (*OwnedByAPlayerFactionFn)(void*);
OwnedByAPlayerFactionFn orig_OwnedByAPlayerFaction = 0;

// Allocation-free: two aligned reads, one guarded virtual call, one interlocked add. It is called from sixteen
// sites, one of them on the navmesh worker thread, and one of decision 40's modes is the only reason it exists.
//
// ONLY `shared` ARMS IT. Under kPolicyUnknown a REQUEST is judged as shared (and says so), but the engine's own
// answer is left alone: unknown means no notebook process has spoken, and quietly opening the other player's
// locked buildings on the strength of silence is the one outcome this whole file exists to prevent. Under
// `owner` and `locked` the engine's answer stands too, by design - `locked` IS the engine's answer.
//
// AND ONLY EIGHT OF THE SIXTEEN CALLERS ARE ANSWERED (review-p6h HIGH-3, review-p6u HIGH-1/HIGH-3). Decision
// 40's justification was one function - UseableStuff::getDefaultTask, where this test short-circuits the whole
// storage-box branch - and the remap is still gated on _ReturnAddress(). The answered set is now getDefaultTask's
// OWN TWO call-site returns plus the SIX inside PlayerInterface::buildingSelected, which is the click/cursor
// handler and is where a StorageBuilding box's click gate actually lives (0x7FBF92). EVERYONE ELSE GETS ORIG'S
// ANSWER, and that deliberately includes DoorStuff::getGUIData's 0x3044BF: answering it hangs a LOCK button
// off the other player's door, which writes state no message carries (E37, door lock sync). Two of the nine
// (ProductionBuilding::captureSaveState and restoreSaveState) branch on the answer during ordinary load and
// save, and one runs on the navmesh worker thread; those must never be remapped.
// The faction test is done FIRST so the pass-through counter means exactly "an answer this hook would have
// changed" rather than "a caller that reached here" - the extra virtual getOwnerFaction() that costs (review-p6h
// LOW-6) is the price of the counter meaning what its name says.
// P7c: is this return address one of the SIX click-handler sites? A linear scan of six aligned constants - no
// allocation, no lock, and it runs only after the faction test has already said this is a peer building.
bool PolicyIsClickRet(uintptr_t rva)
{
    for (unsigned int i = 0; i < kClickRetCount; ++i)
        if (kClickRets[i] != 0 && rva == (uintptr_t)kClickRets[i]) return true;   /* an empty row is no caller */
    return false;
}
// review-p6u LOW-2, both halves. (1) A SYNTHETIC 0 is not a caller: the detour computes rva=0 when g_base is
// unset or the return address is below it, and sampling that made `ownedByPeerOtherCallerRva=0x0` read like a
// real RVA instead of "could not compute". (2) The CAS LOSER used to return without ever trying slot 2, so two
// threads arriving together could leave the second slot empty forever; it now falls through and re-reads.
void PolicySampleRet(LONGLONG rva)
{
    if (rva == 0) return;
    if (g_retSample1 == (LONGLONG)-1) ::InterlockedCompareExchange64(&g_retSample1, rva, (LONGLONG)-1);
    if (g_retSample1 == rva) return;
    if (g_retSample2 == (LONGLONG)-1) ::InterlockedCompareExchange64(&g_retSample2, rva, (LONGLONG)-1);
}
char detour_OwnedByAPlayerFaction(void* building)
{
    const void* ret = _ReturnAddress();
    const char a = orig_OwnedByAPlayerFaction(building);
    if (a != 0) return a;
    /* T-546 step 4 (decision 481): a TEAMMATE's building is answered as this player's own at the answered sites below (the click
       gate, the click handler, the door panel) whatever the policy and whatever its class. While this game's player is in no team
       nothing more is read than before; the caller is worked out first, so a caller that is never answered (the navmesh worker,
       serialise, createBuilding ...) costs the stand-in pointer compare it always did and never reaches the teammate test. */
    const bool shared = coop::BasePolicyValue() == coop::kPolicyShared;
    const bool inTeam = MineInTeam();
    if (!inTeam && !shared) return a;
    const uintptr_t base = g_base;
    const uintptr_t rva = (base != 0 && (uintptr_t)ret > base) ? ((uintptr_t)ret - base) : 0;
    const bool clickGate = (kGetDefaultTaskRet1 != 0 && rva == (uintptr_t)kGetDefaultTaskRet1)
                        || (kGetDefaultTaskRet2 != 0 && rva == (uintptr_t)kGetDefaultTaskRet2);   /* an empty row is no caller */
    const bool clickHandler = !clickGate && PolicyIsClickRet(rva);
    const bool doorPanel = !clickGate && !clickHandler && kDoorPanelRetNotAnswered != 0 && rva == (uintptr_t)kDoorPanelRetNotAnswered;
    const bool answered = clickGate || clickHandler || doorPanel;
    if (!answered && !shared) return a;   /* a team never changes these callers' answer */
    void* const fac = FactionOfBuildingPod(building);
    const bool standIn = coop::IsStandInFaction((::Faction*)fac);   /* stand1 fold (2b): a save's coop-peer too - pointer compares */
    if (!answered)
    {
        if (standIn)
        {
            PolicySampleRet((LONGLONG)rva);
            ::InterlockedIncrement64(&g_ownedByPeerPassedThrough);
        }
        return a;
    }
    const bool member = inTeam && TeammateFaction(fac);   /* the answered sites only (main thread) */
    if (!member && (!standIn || !shared)) return a;
    if (doorPanel)
    {
        // P7c: the door datapanel's own site is counted APART from the nine engine callers, so a run can say
        // how many times a peer door's panel was built with the stranger's answer - i.e. how many times the
        // lock button was NOT offered. The two are disjoint: one event increments exactly one of them.
        /* T-546 step 4: a teammate's door panel offers this player the owner's buttons - open and lock. A lock press is this
           player's own lockButton, which doors.cpp sees and sends to the other games (T-160). */
        if (member) { ::InterlockedIncrement64(&g_memberDoorPanel); return 1; }
        ::InterlockedIncrement64(&g_ownedByPeerDoorPanelPassedThrough);
        return a;
    }
    /* T-546 step 4: a teammate's building of any class - doors, boxes, beds, benches, turrets - is this player's own */
    if (member) { ::InterlockedIncrement64(&g_memberClick); return 1; }
    /* doors1: only a storage box is answered as ours; every other class of the other player's building keeps the
       engine's stranger answer. The vtable is read here, where the building is known live (a click site, main thread). */
    {
        const unsigned int vt = PolicyVtableRvaQuietPod(building);
        if (vt != kVtStorageBuilding) { ::InterlockedIncrement64(&g_ownedByPeerStranger); PolicySampleStrangerVt((LONGLONG)vt); return a; }
    }
    // P7c: the click HANDLER's answers are counted apart from the click GATE's. If `ownedByPeerAnsweredClick`
    // reads 0 while `cursorRemapped` climbs, the handler is not being reached at all. And for a StorageBuilding
    // box it is `ownedByPeerAnsweredClick`, not `ownedByPeerAnswered`, that its click gate shows up in - the
    // box task is a constant read at 0x7FBF86 and the ownership question is asked afterwards, at 0x7FBF92.
    if (clickHandler) ::InterlockedIncrement64(&g_ownedByPeerAnsweredClick);
    else ::InterlockedIncrement64(&g_ownedByPeerAnswered);
    return 1;
}

// ---- the two getMouseCursor hooks: what the CURSOR says, made to match what the CLICK does ----------------
//
// P6h shipped the click side and disclosed the mismatch: under `shared` a locked peer box looted on click while
// the cursor still showed pick-lock, because both cursor functions inline the ownership test and the 0x546340
// hook cannot reach them. These two detours close it WITHOUT touching a lock or any other engine state: orig is
// called, and only the number it returns is changed, only under `shared`, and only when the object's faction is
// the coop-peer stand-in. If either hook fails to install the cursor simply behaves as it did in P6h.
typedef unsigned int (*UseableCursorFn)(void*);          /* 0x546C30 returns a 32-bit enum (decomp: undefined4) */
typedef unsigned long long (*DoorCursorFn)(void*);       /* 0x547010's return is 64-bit in the decompile; carried whole so an unremapped answer is returned byte-for-byte */
UseableCursorFn orig_UseableCursor = 0;
DoorCursorFn    orig_DoorCursor    = 0;

bool CursorIsPeerObject(void* self)
{
    if (coop::BasePolicyValue() != coop::kPolicyShared) return false;
    return coop::IsStandInFaction((::Faction*)FactionOfBuildingPod(self));   /* stand1 fold (2b) */   /* stand1: ANY player's stand-in */
}
// 0xF (a stranger's locked box) and 0x18 (a stranger's lootable container) both become 9, which is what the
// engine itself returns for a box a player faction owns - it returns it BEFORE consulting the lock at all, so
// the same answer covers the locked and the unlocked peer box, and it is exactly what the click now does.
unsigned int detour_UseableCursor(void* self)
{
    const unsigned int a = orig_UseableCursor(self);
    if (a != kCursorPickLock && a != kCursorLoot && a != kCursorTypeEightNoContainer) return a;
    const bool member = CursorTeammateObject(self);   /* T-546 step 4: a teammate's object of any class, whatever the policy */
    if (!member && !CursorIsPeerObject(self)) return a;
    if (!member)
    {
        const unsigned int vt = PolicyVtableRvaQuietPod(self);   /* doors1: boxes only */
        if (vt != kVtStorageBuilding) { ::InterlockedIncrement64(&g_cursorStranger); PolicySampleStrangerVt((LONGLONG)vt); return a; }
    }
    if (a == kCursorTypeEightNoContainer)
    {
        // A type-8 useable with no container and no lock. An own one answers 9, so this IS a mismatch - but
        // nothing has characterised what such an object is, and remapping an answer we cannot name is how a UI
        // stops matching behaviour. Counted, not changed; the register carries it as the residual.
        ::InterlockedIncrement64(member ? &g_memberCursorOther : &g_cursorOtherAnswer);
        return a;
    }
    ::InterlockedIncrement64(member ? &g_memberCursor : &g_cursorRemapped);
    return kCursorOwnUse;
}
// A door's own locked answer is 0xE and a stranger's is 0xF; both sides answer 0xC unlocked. UNTIL doors1 the 0xF was
// remapped to 0xE under `shared`; since the user's decision (2026-09-24: the owner's lock holds) it is only counted.
unsigned long long detour_DoorCursor(void* self)
{
    const unsigned long long a = orig_DoorCursor(self);
    if (a != (unsigned long long)kCursorPickLock) return a;
    /* T-546 step 4 (decision 481): a teammate's locked door shows this player's own locked-door cursor, whatever the policy */
    if (CursorTeammateObject(self)) { ::InterlockedIncrement64(&g_memberDoorCursor); return kCursorOwnLockedDoor; }
    if (!CursorIsPeerObject(self)) return a;
    /* doors1 (user decision 2026-09-24): the other player's locked door keeps the stranger's pick-lock cursor - the owner's
       lock holds; they pick or break it. Counted, never remapped. */
    ::InterlockedIncrement64(&g_doorCursorStranger);
    return a;
}

// P7c / review-p6u MEDIUM-3: THE REAL SizeOfImage, out of our own PE header, once. The P029 walk used to bound
// itself with a literal 0x08000000, which is 3.6x the module - it admitted ~93 MB of address space past the
// end of kenshi_x64.exe, so a vtable belonging to another loaded module could come back as a "kenshi_x64.exe
// RVA" and invite comparison against the six-vtable table in the log line. Measured on the shipped exe
// (2026-09-04, the game was not launched): e_lfanew = 0x130, PE00, optional-header magic 0x20B (PE32+),
// SizeOfImage = 0x232C000 at optional-header + 0x38. Every offset below is asserted before it is used, and the
// whole read is inside __try, so a header that is not what we expect yields 0 and the walk then refuses
// everything - fail CLOSED, and visibly, because `imageSize` is on the report line.
// NO std::string IS CONSTRUCTED HERE (C2712), and there is no C++11 in it.
unsigned int PolicySizeOfImagePod(uintptr_t base)
{
    if (base == 0) return 0;
    __try
    {
        if (*(const unsigned short*)base != 0x5A4Du) return 0;                    /* 'MZ' */
        const long lfanew = *(const long*)(base + 0x3C);
        if (lfanew <= 0 || lfanew > 0x10000) return 0;
        const uintptr_t nt = base + (uintptr_t)lfanew;
        if (*(const unsigned int*)nt != 0x00004550u) return 0;                    /* 'PE\0\0' */
        if (*(const unsigned short*)(nt + 0x18) != 0x020Bu) return 0;             /* PE32+ optional header */
        const unsigned int sz = *(const unsigned int*)(nt + 0x18 + 0x38);         /* SizeOfImage */
        if (sz < 0x1000u) return 0;
        return sz;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

}   // namespace

namespace coop {

// ---- PROBE P029: WHICH CONCRETE CLASS IS THIS BUILDING? -------------------------------------------------
//
// review-p6m HIGH-1's open leg. The click gate reaches ONE class and the cursor remap reaches five, and
// nothing in the repo says which of them a player-built storage container actually is - items.cpp identifies a
// box by `UseableStuff+0x430` and by `HandleManager::getBuilding_UseableStuff`, both of which accept every
// subclass. The answer decides what `ownedByPeerAnswered == 0` MEANS in a run's report.
//
// TWO READS, AND THE SPLIT BETWEEN THEM IS DELIBERATE.
//   PolicyVtableRvaPod    reads the object's vtable POINTER. It touches the building, so it is called where
//                         the building is known live - in items.cpp's box detour, beside the instance id and
//                         the position that are read there for exactly this reason (by the time the drain
//                         runs, the object may be gone).
//   PolicyClassNamePod    takes the RVA and reads NOTHING but this module's own image: a vtable lives in
//                         .rdata, which is never freed, so the name can be resolved at LOG time long after the
//                         building itself has been destroyed. That is what keeps the probe off the
//                         use-after-free path this file's neighbours have already paid for twice.
//                         SINCE P7C THAT SENTENCE IS ENFORCED RATHER THAN ASSERTED (review-p6u MEDIUM-3): the
//                         bound is the module's real SizeOfImage, and the Complete Object Locator pointer is
//                         bounded to the image too - it used to be checked only by `Plaus`, a canonical
//                         user-space test that would happily follow a garbage vtable to arbitrary heap.
//                         Everything rejected is counted as `rttiOutOfImage`.
//
// The RTTI shape, for anyone reading this without the MSVC layout to hand: vtable[-1] is a pointer to the
// Complete Object Locator; its first dword is the signature (1 on x64), and the dword at +12 is the RVA of the
// TypeDescriptor, whose name begins at +0x10 and reads `.?AV<ClassName>@@`. Every step is guarded and the whole
// walk sits inside __try, so a garbage vtable yields "unreadable" and never a fault. NO std::string IS
// CONSTRUCTED IN EITHER FUNCTION - both carry __try, and that is the C2712 rule this project's build notes name.
/* P7c / review-p6u MEDIUM-3. The bound is the module's REAL SizeOfImage (0x232C000 on the shipped exe), read
   from the PE header at InstallPolicy by PolicySizeOfImagePod. The literal it replaced, 0x08000000, was 3.6x
   the image. A 0 here means the header could not be read and the walk refuses everything - the safe direction,
   and `imageSize` on the report line says which of the two a zero answer was. */
unsigned int PolicyImageRvaCap() { return g_imageSize; }

unsigned int PolicyVtableRvaPod(void* obj)
{
    if (!Plaus(obj) || ((uintptr_t)obj & 7) || g_base == 0) return 0;
    const uintptr_t imgCap = (uintptr_t)PolicyImageRvaCap();
    __try
    {
        const uintptr_t vt = (uintptr_t)*(void**)obj;
        if (vt <= g_base) return 0;
        const uintptr_t rva = vt - g_base;
        if (imgCap == 0 || rva >= imgCap) { ::InterlockedIncrement64(&g_rttiOutOfImage); return 0; }
        return (unsigned int)rva;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

}   // namespace coop (briefly: the quiet reader lives beside the counting one but in the file's anonymous scope)
namespace {
unsigned int PolicyVtableRvaQuietPod(void* obj)
{
    if (!Plaus(obj) || ((uintptr_t)obj & 7) || g_base == 0 || g_imageSize == 0) return 0;
    __try
    {
        const uintptr_t vt = (uintptr_t)*(void**)obj;
        if (vt <= g_base || vt - g_base >= (uintptr_t)g_imageSize) return 0;
        return (unsigned int)(vt - g_base);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
}   // namespace
namespace coop {

int PolicyClassNamePod(unsigned int vtRva, char* buf, int cap)
{
    if (buf == 0 || cap < 8) return 0;
    buf[0] = 0;
    if (vtRva == 0 || g_base == 0) return 0;
    /* P7c / review-p6u MEDIUM-3: every RVA below is bounded by the module's REAL SizeOfImage. `imgCap` is
       deliberately NOT called `cap` - that name is this function's BUFFER capacity, and shadowing it would
       silently re-bound the copy loop below. imgCap == 0 means the PE header could not be read, and the walk
       then refuses everything rather than falling back to a looser bound. */
    const uintptr_t imgCap = (uintptr_t)PolicyImageRvaCap();
    if (imgCap == 0 || (uintptr_t)vtRva < 8 || (uintptr_t)vtRva >= imgCap) { ::InterlockedIncrement64(&g_rttiOutOfImage); return 0; }
    __try
    {
        const char* const vt = (const char*)(g_base + (uintptr_t)vtRva);
        const char* const col = *(const char* const*)(vt - 8);
        if (!Plaus(col)) return 0;
        /* The COL must live in THIS module too. `Plaus` only says "a canonical user-space address", which would
           follow a garbage vtable to arbitrary committed heap; the signature and `.?AV` tests make a false
           positive unlikely, not impossible. Bounded here, and the rejection is counted. */
        if ((uintptr_t)col < g_base || (uintptr_t)col + 16 > g_base + imgCap) { ::InterlockedIncrement64(&g_rttiOutOfImage); return 0; }
        if (*(const unsigned int*)col != 1u) return 0;   /* the x64 COL signature; anything else is not one */
        const unsigned int ptd = *(const unsigned int*)(col + 12);
        const uintptr_t nameRva = (uintptr_t)ptd + 0x10;
        if (ptd == 0 || nameRva + 4 > imgCap) { ::InterlockedIncrement64(&g_rttiOutOfImage); return 0; }
        const char* const n = (const char*)(g_base + nameRva);
        if (n[0] != '.' || n[1] != '?' || n[2] != 'A' || n[3] != 'V') return 0;
        int i = 0;
        /* the copy stops at the buffer, at the NUL, OR at the end of the image - whichever comes first. */
        while (i < cap - 1 && nameRva + (uintptr_t)i < imgCap && n[i] != 0) { buf[i] = n[i]; ++i; }
        buf[i] = 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { buf[0] = 0; return 0; }
}

/* ---- T-546 step 4b (decision 481): A TEAMMATE'S BOX OPENS ON A PLAIN CLICK --------------------------------------------------
   PlayerInterface::buildingSelected 0x7FBB40, after the click handler's ownership question at 0x7FBF92 (answered "own" for a
   teammate's building by the hook above) and with the box task 0x1A, tests the box's owner once more, inline, where no hook can
   reach (Confirmed by capstone on both Steam builds, 1.0.65 shown):
     0x7FC22D  call [rax+0x2F0] ; cmp eax, 9 ; jne LOOT          a type-9 object...
     0x7FC23E  call [rax+0x58]                                   ...its owner faction (rax)
     0x7FC241  48 83 B8 50 02 00 00 00   cmp qword [rax+0x250], 0   has a PlayerInterface?
     0x7FC249  75 5F                     jne 0x7FC2AA (LOOT: the loot order, task 0x1A, addTaskNearestSelectedCharacter)
     0x7FC24B  ...                       else "Hold down the ALT key to steal items"
   A stand-in has no PlayerInterface (it is not the player faction here), so a teammate's box answered "own" still ended in the
   steal line. THE FIX, the smallest sound road: the 8-byte compare is replaced (E9 rel32 + 3 NOPs) by a jump to a stub on a page of
   its own - crime.cpp's sight1 stub, built by the same pure coopcrime::SightStubBuild - which re-does the compare (ZF=0 for a
   player faction, exactly as before), else compares the owner against CELLS holding this game's teammates' factions (ZF=0 on a
   match: the jne takes the loot road), else re-does the compare (ZF=1: the steal line, as before), and jumps back to the jne,
   which stays where it is. It reads only rax and its own page, calls nothing and touches no other register. The cells are written
   by the main thread (PolicyTeammateFactions, from team.cpp) only when they change; empty while this game's player is in no team,
   so nothing changes for anyone else. Not giving the stand-in a PlayerInterface: that pointer makes the engine treat a faction
   as THE player's (LocalPlayerFaction, every isPlayer test) - far wider than this one test. Written once at InstallPolicy (the
   preload stage). The click handler only runs on the main thread. */
namespace {
unsigned long long kBoxOwnerTestRva = 0; static AddrReg kBoxOwnerTestRva_reg("BuildingSelectedBoxOwnerTest", &kBoxOwnerTestRva);   /* Steam_1.0.65 0x7FC241 - a patch site, not a function */
const unsigned char kBoxTestOrig[10] = { 0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00, 0x75, 0x5F };
const int kBoxCells = 16;              /* a team's other players (swteam::kMaxMembers + 1 players at most) */
const size_t kBoxCellsOff = 0x400;     /* off the stub's cache line, as sight1 */
struct BoxCells { volatile LONG64 hits; void* volatile cell[kBoxCells]; };   /* hits at +0, cell k at +8+8k (SightStubBuild's layout) */
typedef char kBoxCellsCoverTeam[(kBoxCells >= (int)swteam::kMaxMembers + 1) ? 1 : -1];
typedef char kBoxStubBeforeCells[(50 + 13 * kBoxCells <= (int)kBoxCellsOff) ? 1 : -1];
typedef char kBoxCellsInPage[((int)kBoxCellsOff + 8 + 8 * kBoxCells <= 0x1000) ? 1 : -1];
BoxCells* g_boxCells = 0;
const char* g_boxWhy = "not tried";
int g_boxHeld = 0;
int BoxReadPod(const void* p, unsigned char* out, size_t n) { __try { std::memcpy(out, p, n); return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
void* BoxAllocNear(uintptr_t site)   /* a page within a rel32 jump of the site, searched downward in 64 KB steps (crime.cpp's SightAllocNear) */
{
    const uintptr_t gran = 0x10000, start = site & ~(gran - 1);
    for (uintptr_t i = 1; i < 0x7000; ++i)
    {
        if (start <= i * gran) break;
        void* p = ::VirtualAlloc((void*)(start - i * gran), 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p != 0) return p;
    }
    return 0;
}
int BoxTestPatch(uintptr_t site)
{
    unsigned char now[10];
    if (!BoxReadPod((const void*)site, now, 10)) { g_boxWhy = "the site could not be read"; return 0; }
    if (std::memcmp(now, kBoxTestOrig, 10) != 0) { g_boxWhy = "the 10 bytes at the site are not the expected test (another mod?)"; return 0; }
    unsigned char* stub = (unsigned char*)BoxAllocNear(site);
    if (stub == 0) { g_boxWhy = "no page for the stub within a jump of the site"; return 0; }
    const long long rel = (long long)((intptr_t)stub - (intptr_t)(site + 5));
    if (rel > 0x7FFFFFF0LL || rel < -0x7FFFFFF0LL) { ::VirtualFree(stub, 0, MEM_RELEASE); g_boxWhy = "the stub page is out of jump range"; return 0; }
    BoxCells* cells = (BoxCells*)(stub + kBoxCellsOff);
    for (int k = 0; k < kBoxCells; ++k) cells->cell[k] = 0;
    cells->hits = 0;
    if (coopcrime::SightStubBuild(stub, (unsigned long long)site, kBoxCells, (int)kBoxCellsOff) != 50 + 13 * kBoxCells)
    { ::VirtualFree(stub, 0, MEM_RELEASE); g_boxWhy = "the stub came out at an unexpected length"; return 0; }
    ::FlushInstructionCache(::GetCurrentProcess(), stub, 0x1000);
    unsigned char patch[8];
    patch[0] = 0xE9;
    const int rel32 = (int)rel;
    std::memcpy(patch + 1, &rel32, 4);
    patch[5] = patch[6] = patch[7] = 0x90;
    DWORD oldProt = 0;
    if (!::VirtualProtect((void*)site, 8, PAGE_EXECUTE_READWRITE, &oldProt)) { ::VirtualFree(stub, 0, MEM_RELEASE); g_boxWhy = "VirtualProtect refused"; return 0; }
    std::memcpy((void*)site, patch, 8);
    DWORD ignored = 0;
    ::VirtualProtect((void*)site, 8, oldProt, &ignored);
    ::FlushInstructionCache(::GetCurrentProcess(), (void*)site, 8);
    g_boxCells = cells;
    g_boxWhy = "patched";
    return 1;
}
}   // namespace

/* ---- T-546 step 7 (owner 512 / 512-a): A TEAMMATE'S CHARACTERS ANSWER THIS PLAYER'S ORDERS AS ITS OWN CHARACTERS DO ------------
   "As if one person owned both sets": the engine's own decisions (src/common/teamorders.h holds the reads and the orders kept). Five
   hooks, each changing an answer only for a teammate's character and passing every other call through untouched:
     PlayerInterface::isEnemy 0x79C040 - "not an enemy", the answer it gives this player's own faction before anything else (so a
       right-click is never an attack, and the menu never takes its enemy branch); asked by the menu builder at its own call site
       (return 0x7A6C8E) inside a menu build, it also marks the build's list as one for a teammate's character;
     PlayerInterface::characterSelected 0x7FA870 (a right-click or a hover on a character) - marks the call as one on a teammate's
       character for the sneaking test;
     the sneaking test 0x5C8CC0 - "not sneaking" at the right-click's one call site on the non-enemy branch (return 0x7FB0E1) inside
       such a call, so a sneaking selected character is not offered the stealth knock-out (the cursor and the order both come after it);
     ContextMenu::showContextMenu 0x7A6020 - finds before the build the character the orders are built for (the clicked character, or
       the person in a clicked bed or cage, whose orders go to a nested menu), which decides the cage branch: it never asks isEnemy;
     the menu's fill 0x7A7440 - the list built for a teammate's character keeps only what the base game offers on one's own character
       (kidnap becomes carry) before the menu is made from it; the furniture's own list is never touched.
   The flags a hook sets are put back on every exit, a C++ exception from the engine's code included (the scope objects below).
   The cells hold this game's teammates' factions (written by the main thread through PolicyTeammateFactions, read by any thread); they
   are empty while this game's player is in no team, and every hook then answers as the engine. Shown nowhere: the engine simply does
   not offer the orders, as it does not for the player's own characters. Installed at InstallPolicy (the preload stage). */
namespace {
unsigned long long kPiIsEnemyRva = 0;    static AddrReg kPiIsEnemyRva_reg("PlayerInterface_isEnemy", &kPiIsEnemyRva);                   /* Steam_1.0.65 0x79C040 */
unsigned long long kCharSelectedRva = 0; static AddrReg kCharSelectedRva_reg("PlayerInterface_characterSelected", &kCharSelectedRva);   /* Steam_1.0.65 0x7FA870 */
unsigned long long kSneakTestRva = 0;    static AddrReg kSneakTestRva_reg("Character_sneakingTest", &kSneakTestRva);                    /* Steam_1.0.65 0x5C8CC0 */
unsigned long long kSneakClickRet = 0;   static AddrReg kSneakClickRet_reg("SneakTestClickRet", &kSneakClickRet);                      /* Steam_1.0.65 0x7FB0E1 (ret) */
unsigned long long kShowMenuRva = 0;     static AddrReg kShowMenuRva_reg("ContextMenu_showContextMenu", &kShowMenuRva);                 /* Steam_1.0.65 0x7A6020 */
unsigned long long kMenuFillRva = 0;     static AddrReg kMenuFillRva_reg("ContextMenu_fill", &kMenuFillRva);                            /* Steam_1.0.65 0x7A7440 */
unsigned long long kMenuEnemyRet = 0;    static AddrReg kMenuEnemyRet_reg("ShowMenuIsEnemyRet", &kMenuEnemyRet);                       /* Steam_1.0.65 0x7A6C8E (ret) */
unsigned long long kOrdHandCharRva = 0;  static AddrReg kOrdHandCharRva_reg("Hand_asCharacter", &kOrdHandCharRva);                     /* Steam_1.0.65 0x7974F0 hand::getCharacter */
typedef char (*PiIsEnemyFn)(void* pi, void* who);
typedef void (*CharSelectedFn)(void* pi, void* who);
typedef unsigned long long (*SneakTestFn)(void* ch);
typedef void (*ShowMenuFn)(void* menu, char show, void* target);
typedef void (*MenuFillFn)(void* menu, void* list, void* name, unsigned long long sub);
PiIsEnemyFn orig_piIsEnemy = 0;
CharSelectedFn orig_charSelected = 0;
SneakTestFn orig_sneakTest = 0;
ShowMenuFn orig_showMenu = 0;
MenuFillFn orig_menuFill = 0;
const int kOrdCells = 16;
void* volatile g_ordCells[kOrdCells];
volatile LONG g_ordHeld = 0;
const size_t kPiMenu = 0x48;                     /* the ContextMenu inside PlayerInterface (newPlayerTask 0x7F9AB0 closes it as showContextMenu(pi + 0x48, 0, 0)) */
const size_t kPiTargetMode = 0x2F1;              /* PlayerInterface: set, characterSelected hands the character to its target-picking call 0x7F8460 and returns */
const size_t kListCount = 8, kListData = 0x10;   /* the menu's order list (lektor<int>): count +8, array +0x10 */
const size_t kOrdInSomething = 0x2F8;            /* Character: int inSomething, 1 bed, 2 cage */
const size_t kOrdFurnHandType = 0x308;           /* Character: the furniture hand +0x300's type (+8); 0 = a building's */
const size_t kUseOccupied = 0x3A8, kUseHeldHead = 0x3D8, kUseHeldCount = 0x3E0, kHeldNodeHand = 0x18;   /* the furniture's useable part (Building vtable +0x300) */
volatile LONG64 g_ordEnemyAsked = 0, g_ordEnemyForced = 0, g_ordClicks = 0, g_ordSneakForced = 0, g_ordMenus = 0, g_ordRemoved = 0;
volatile LONG64 g_ordMenuMarks = 0, g_ordReplaced = 0, g_ordForFound = 0;
int g_ordHooks = 0;                              /* how many of the five are installed */
std::string g_ordWhy = "not tried";
volatile LONG g_clickMate = 0;                   /* inside characterSelected on a teammate's character */
volatile DWORD g_clickTid = 0;
/* inside showContextMenu (main thread): the build's depth and thread; the isEnemy mark; what was found before the build */
volatile LONG g_menuDepth = 0, g_menuOrdMark = 0, g_menuForTarget = 0, g_menuForOcc = 0, g_menuTargetCage = 0, g_menuOccCage = 0;
volatile DWORD g_menuTid = 0;
/* the `team orders` lever's capture of the last list the menu was handed (main thread) */
int g_capOn = 0, g_capBuilt = 0, g_capRawN = 0, g_capShownN = 0, g_capSub = 0, g_capCage = 0;
int g_capRaw[64], g_capShown[64];

unsigned long long RvaOfRet(const void* ret)
{
    return (g_base != 0 && (uintptr_t)ret > g_base) ? (unsigned long long)((uintptr_t)ret - g_base) : 0;
}
/* 1 = obj belongs to a teammate's faction (and is a character when wantCharacter), 0 = not, or unreadable. Never faults. ANY THREAD. */
int TeammateObjPod(void* obj, int wantCharacter)
{
    if (obj == 0 || g_ordHeld == 0) return 0;
    __try
    {
        void** vt = *(void***)obj;
        if (!Plaus(vt)) return 0;
        if (wantCharacter != 0)
        {
            typedef int (*TypeFn)(void*);
            if (((TypeFn)vt[0x20 / 8])(obj) != 1) return 0;   /* the object's data type: 1 = a character */
        }
        typedef void* (*FacFn)(void*);
        void* f = ((FacFn)vt[0x58 / 8])(obj);
        return swteamord::TeammateFaction(f, g_ordCells, kOrdCells) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* The character showContextMenu builds the orders for, read before the build as the builder reads it (decomp_7a6b76.txt :120-170):
   a clicked character in a bed or cage whose furniture hand is a building's becomes a click on that furniture, whose person is the
   character itself (*nested = 1); any other clicked character is the one (*nested = 0); for a clicked building, the person its useable
   part (vtable +0x300) holds - occupied +0x3A8, the first held hand at **(+0x3D8) + 0x18 while the count +0x3E0 is not 0, a character
   hand (type 1) resolved by hand::getCharacter (*nested = 1). *cage = that character is in a cage (inSomething 2). 0 = none or
   unreadable. Never faults. MAIN THREAD. */
void* OrdersForPod(void* target, int* nested, int* cage)
{
    *nested = 0; *cage = 0;
    if (target == 0) return 0;
    __try
    {
        void** vt = *(void***)target;
        if (!Plaus(vt)) return 0;
        typedef int (*TypeFn)(void*);
        const int type = ((TypeFn)vt[0x20 / 8])(target);
        void* who = 0;
        if (type == 1)
        {
            who = target;
            if (*(int*)((char*)target + kOrdInSomething) != 0 && *(int*)((char*)target + kOrdFurnHandType) == 0) *nested = 1;
        }
        else if (type == 0 && kOrdHandCharRva != 0)
        {
            typedef char* (*UseFn)(void*);
            char* u = ((UseFn)vt[0x300 / 8])(target);
            if (!Plaus(u) || *(unsigned char*)(u + kUseOccupied) == 0 || *(unsigned long long*)(u + kUseHeldCount) == 0) return 0;
            char* head = *(char**)(u + kUseHeldHead);
            if (!Plaus(head)) return 0;
            char* node = *(char**)head;
            if (!Plaus(node) || *(int*)(node + kHeldNodeHand + 8) != 1) return 0;
            typedef void* (*HandCharFn)(const void*);
            who = ((HandCharFn)(g_base + (uintptr_t)kOrdHandCharRva))(node + kHeldNodeHand);
            if (!Plaus(who)) return 0;
            *nested = 1;
        }
        if (who != 0) *cage = *(int*)((char*)who + kOrdInSomething) == 2 ? 1 : 0;
        return who;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *nested = 0; *cage = 0; return 0; }
}
char detour_piIsEnemy(void* pi, void* who)
{
    const void* const ret = _ReturnAddress();
    const char engine = orig_piIsEnemy(pi, who);
    if (g_ordHeld == 0 || TeammateObjPod(who, 0) == 0) return engine;
    ::InterlockedIncrement64(&g_ordEnemyAsked);
    if (engine != 0) ::InterlockedIncrement64(&g_ordEnemyForced);
    if (swteamord::MenuEnemySite(g_menuDepth > 0, ::GetCurrentThreadId() == g_menuTid, RvaOfRet(ret), kMenuEnemyRet))
    {
        g_menuOrdMark = 1;                        /* the character this build's orders are for is a teammate's */
        ::InterlockedIncrement64(&g_ordMenuMarks);
    }
    return 0;                                     /* as for this player's own faction */
}
struct ClickScope   /* characterSelected's flags, put back on every exit */
{
    LONG mate; DWORD tid;
    ClickScope() : mate(g_clickMate), tid(g_clickTid) {}
    ~ClickScope() { g_clickMate = mate; g_clickTid = tid; }
};
void detour_charSelected(void* pi, void* who)
{
    if (g_ordHeld == 0 || TeammateObjPod(who, 1) == 0) { orig_charSelected(pi, who); return; }
    ClickScope keep;
    g_clickTid = ::GetCurrentThreadId(); g_clickMate = 1;
    ::InterlockedIncrement64(&g_ordClicks);
    orig_charSelected(pi, who);
}
unsigned long long detour_sneakTest(void* ch)
{
    const void* const ret = _ReturnAddress();
    const unsigned long long r = orig_sneakTest(ch);
    if (g_clickMate == 0) return r;
    if (!swteamord::SneakForcedOff(true, ::GetCurrentThreadId() == g_clickTid, RvaOfRet(ret), kSneakClickRet)) return r;
    if ((r & 0xFFull) != 0) ::InterlockedIncrement64(&g_ordSneakForced);
    return r & ~0xFFull;                          /* "not sneaking": the right-click goes on as for a character that is not sneaking */
}
struct MenuScope    /* showContextMenu's build state, put back on every exit (a build inside a build included) */
{
    LONG depth, ordMark, forTarget, forOcc, targetCage, occCage; DWORD tid;
    MenuScope() : depth(g_menuDepth), ordMark(g_menuOrdMark), forTarget(g_menuForTarget), forOcc(g_menuForOcc), targetCage(g_menuTargetCage),
                  occCage(g_menuOccCage), tid(g_menuTid) {}
    ~MenuScope()
    {
        g_menuOrdMark = ordMark; g_menuForTarget = forTarget; g_menuForOcc = forOcc; g_menuTargetCage = targetCage; g_menuOccCage = occCage;
        g_menuTid = tid; g_menuDepth = depth;
    }
};
void detour_showMenu(void* menu, char show, void* target)
{
    MenuScope keep;
    g_menuOrdMark = 0; g_menuForTarget = 0; g_menuForOcc = 0; g_menuTargetCage = 0; g_menuOccCage = 0;
    if (show != 0 && target != 0 && g_ordHeld != 0)
    {
        int nested = 0, cage = 0;
        void* who = OrdersForPod(target, &nested, &cage);
        if (who != 0 && TeammateObjPod(who, 1) != 0)
        {
            ::InterlockedIncrement64(&g_ordForFound);
            if (nested != 0) { g_menuForOcc = 1; g_menuOccCage = cage; }
            else             { g_menuForTarget = 1; g_menuTargetCage = cage; }
        }
    }
    g_menuTid = ::GetCurrentThreadId();
    g_menuDepth = keep.depth + 1;
    orig_showMenu(menu, show, target);
}
/* The menu's list: captured for the lever, and for a teammate's character filtered in place. Never faults. */
void MenuListPod(void* list, int filter, int cage, unsigned long long sub)
{
    __try
    {
        unsigned* pn = (unsigned*)((char*)list + kListCount);
        int* d = *(int**)((char*)list + kListData);
        const int n = (int)*pn;
        if (n < 0 || n > 4096 || (n > 0 && !Plaus(d))) return;
        if (g_capOn != 0)
        {
            g_capBuilt = 1; g_capRawN = 0; g_capSub = sub != 0 ? 1 : 0; g_capCage = cage;
            for (int i = 0; i < n && i < 64; ++i) g_capRaw[g_capRawN++] = d[i];
        }
        int m = n;
        if (filter != 0)
        {
            int removed = 0, replaced = 0;
            m = swteamord::FilterForTeammate(d, n, cage != 0, &removed, &replaced);
            if (m != n) *pn = (unsigned)m;
            if (removed > 0) ::InterlockedExchangeAdd64(&g_ordRemoved, removed);
            if (replaced > 0) ::InterlockedExchangeAdd64(&g_ordReplaced, replaced);
            ::InterlockedIncrement64(&g_ordMenus);
        }
        if (g_capOn != 0)
        {
            g_capShownN = 0;
            for (int i = 0; i < m && i < 64; ++i) g_capShown[g_capShownN++] = d[i];
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
void detour_menuFill(void* menu, void* list, void* name, unsigned long long sub)
{
    const bool inBuild = g_menuDepth > 0 && ::GetCurrentThreadId() == g_menuTid;
    const bool filter = inBuild && swteamord::MenuFiltered(g_menuOrdMark != 0, sub, g_menuForTarget != 0, g_menuForOcc != 0);
    const bool cage = filter && g_menuOrdMark == 0 && (sub != 0 ? g_menuOccCage : g_menuTargetCage) != 0;
    if (list != 0 && (filter || g_capOn != 0)) MenuListPod(list, filter ? 1 : 0, cage ? 1 : 0, sub);
    orig_menuFill(menu, list, name, sub);
}
int OrdHook(unsigned long long rva, void* detour, void** orig, const char* what)
{
    if (rva == 0) { g_ordWhy += std::string(" ") + what + ":no-row"; return 0; }
    if (coop::AddHook((void*)(g_base + (uintptr_t)rva), detour, orig) != coop::SUCCESS) { g_ordWhy += std::string(" ") + what + ":hook-failed"; return 0; }
    ++g_ordHooks;
    return 1;
}
void InstallTeamOrders()
{
    g_ordWhy.clear();
    OrdHook(kPiIsEnemyRva, (void*)&detour_piIsEnemy, (void**)&orig_piIsEnemy, "isEnemy");
    OrdHook(kCharSelectedRva, (void*)&detour_charSelected, (void**)&orig_charSelected, "characterSelected");
    if (kSneakClickRet != 0) OrdHook(kSneakTestRva, (void*)&detour_sneakTest, (void**)&orig_sneakTest, "sneakingTest");
    else g_ordWhy += " sneakingTest:no-ret-row";
    OrdHook(kShowMenuRva, (void*)&detour_showMenu, (void**)&orig_showMenu, "showContextMenu");
    OrdHook(kMenuFillRva, (void*)&detour_menuFill, (void**)&orig_menuFill, "menuFill");
    if (kMenuEnemyRet == 0) g_ordWhy += " menuEnemySite:no-ret-row";
    if (kOrdHandCharRva == 0) g_ordWhy += " handGetCharacter:no-row";
    if (g_ordHooks == 5 && kMenuEnemyRet != 0 && kOrdHandCharRva != 0)
        DebugLog("[POLICY] T-546 step 7 team orders: 5 of 5 hooks installed (PlayerInterface::isEnemy, characterSelected, the sneaking test at the right-click's call site, showContextMenu, the menu's fill; the menu builder's isEnemy site and hand::getCharacter found) - a teammate's character is offered what one's own character is offered");
    else ErrorLog("[POLICY] T-546 step 7 team orders: " + PN((long long)g_ordHooks) + " of 5 hooks installed (" + g_ordWhy + ") - a teammate's character may still be offered an attack");
}
std::string TeamOrdersTokens()
{
    return " teamOrders[hooks,cells,enemyAsked,enemyForced,clicks,sneakForced,menus,removed,menuMarks,forFound,replaced]=" + PN((long long)g_ordHooks) + "," + PN((long long)g_ordHeld)
         + "," + PN(g_ordEnemyAsked) + "," + PN(g_ordEnemyForced) + "," + PN(g_ordClicks) + "," + PN(g_ordSneakForced) + "," + PN(g_ordMenus) + "," + PN(g_ordRemoved)
         + "," + PN(g_ordMenuMarks) + "," + PN(g_ordForFound) + "," + PN(g_ordReplaced);
}
}   // namespace

void PolicyTeammateFactions(void* const* facs, int n)
{
    /* T-546 (owner 512): the team set (this game's player faction and these factions) the isAllyOf / isEnemyOf detours answer
       from - kept whether or not the box stub is patched */
    coop::PeaceTeamFactions((n > 0 && facs != 0) ? (void*)coop::LocalPlayerFaction() : 0, facs, n);
    {   /* T-546 step 7: the team-orders cells, kept whether or not the box stub is patched */
        LONG held = 0;
        for (int k = 0; k < kOrdCells; ++k)
        {
            void* want = (k < n && facs != 0) ? facs[k] : 0;
            if (want != 0) ++held;
            if (g_ordCells[k] != want) g_ordCells[k] = want;
        }
        ::InterlockedExchange(&g_ordHeld, held);
    }
    if (g_boxCells == 0) return;
    int held = 0;
    for (int k = 0; k < kBoxCells; ++k)
    {
        void* want = (k < n && facs != 0) ? facs[k] : 0;
        if (want != 0) ++held;
        if (g_boxCells->cell[k] != want) g_boxCells->cell[k] = want;
    }
    g_boxHeld = held;
}

void InstallPolicy()
{
    g_base = (uintptr_t)::GetModuleHandleA(0);
    /* P7c / review-p6u MEDIUM-3: the P029 walk's bound, read from our own PE header rather than guessed at. */
    g_imageSize = PolicySizeOfImagePod(g_base);
    InstallTeamOrders();   /* T-546 step 7 */
    coop::HookStatus h = coop::AddHook((void*)(g_base + kOwnedByAPlayerFactionRva), (void*)&detour_OwnedByAPlayerFaction, (void**)&orig_OwnedByAPlayerFaction);
    if (h != coop::SUCCESS)
        ErrorLog("[POLICY] AddHook OwnedByAPlayerFaction 0x546340 FAILED - the `shared` base policy cannot make the other player's STORAGE BOXES behave as ours, so a LOCKED peer box will still refuse to open (an unlocked one is lootable either way, which is the engine's own rule). Doors, beds, benches and turrets are the engine's stranger behaviour either way (doors1)");
    else
    {
        g_hookInstalled = 1;
        DebugLog("[POLICY] hook installed: OwnedByAPlayerFaction 0x546340 (the engine's only ownership term in UseableStuff::getDefaultTask 0x2991F0 - one thunk at 0x53913, sixteen callers). It answers the CLICK GATE and the CLICK HANDLER, and nothing else: the remap runs when the return address is 0x2991FE or 0x2992B2 (UseableStuff::getDefaultTask's own two call sites, counted as ownedByPeerAnswered) or one of the SIX PlayerInterface::buildingSelected sites 0x7FBB8E / 0x7FBC74 / 0x7FBCEB / 0x7FBF97 / 0x7FC318 / 0x7FC34A (counted as ownedByPeerAnsweredClick - renamed in P7c from ownedByPeerAnsweredPanel, which was the wrong seam: buildingSelected issues the ORDER, it is not panel construction, and 0x7FBF97 is where a StorageBuilding box's click gate lives). P7C REMOVED DoorStuff::getGUIData's site 0x3044BF FROM THAT SET: answering it hangs DoorStuff::lockButton 0x5465D0 off the other player's door panel, and a press writes wantsToLock/DoorLock+0x20 into the zone record with nothing on the wire to carry it - so under `shared` a peer's door offers no lock button until door locks are synced (E37), and the site is counted as ownedByPeerDoorPanelPassedThrough. The remaining nine callers - serialise, loadFromSerialise, the navmesh, createBuilding, setupMiningResourceLevel and two unnamed - get the engine's own answer, counted as ownedByPeerPassedThrough. PROBE P029's RTTI walk is bounded by this module's real SizeOfImage = " + PH((long long)g_imageSize) + " (0 = the PE header could not be read, and the walk then refuses every vtable)");

        coop::HookStatus hu = coop::AddHook((void*)(g_base + kUseableCursorRva), (void*)&detour_UseableCursor, (void**)&orig_UseableCursor);
        if (hu != coop::SUCCESS)
            ErrorLog("[POLICY] AddHook UseableStuff::getMouseCursor 0x546C30 FAILED - under `shared` a locked box of the other player's will LOOT on click while the cursor still shows the pick-lock icon (the P6h mismatch, unclosed)");
        else ::InterlockedIncrement64(&g_cursorHooks);

        coop::HookStatus hd = coop::AddHook((void*)(g_base + kDoorCursorRva), (void*)&detour_DoorCursor, (void**)&orig_DoorCursor);
        if (hd != coop::SUCCESS)
            ErrorLog("[POLICY] AddHook DoorStuff::getMouseCursor 0x547010 FAILED - only the doors1 door-cursor counter is lost; the other player's locked door shows the stranger's pick-lock icon either way (user decision 2026-09-24)");
        else ::InterlockedIncrement64(&g_cursorHooks);

        if (kBoxOwnerTestRva == 0) g_boxWhy = "no address row";
        else BoxTestPatch(g_base + (uintptr_t)kBoxOwnerTestRva);
        DebugLog(std::string("[POLICY] T-546 step 4b box test (PlayerInterface::buildingSelected's inline owner test, 1.0.65 0x7FC241): ") + g_boxWhy
                 + (g_boxCells != 0 ? " - a teammate's box clicked as this player's own takes the loot road, not the steal line; every other owner as before" : " - a teammate's type-9 box still ends in the steal line"));
        DebugLog("[POLICY] cursor hooks installed: " + PN(g_cursorHooks) + " of 2 (UseableStuff::getMouseCursor 0x546C30 via vtable thunk 0x3BCBE, DoorStuff::getMouseCursor 0x547010 via vtable thunk 0x4EC7E; neither has a direct caller, so the vtable is the only route and hooking the body catches it). PURE RETURN-VALUE REMAP under `shared` only, and since doors1 only for a STORAGE BOX (StorageBuilding): a peer box's 0xF/0x18 answer becomes 9, the answer the engine gives for our own box. A peer door keeps the stranger's pick-lock cursor (user decision 2026-09-24: the owner's lock holds - pick or break it); beds, benches, research and turrets keep the engine's stranger answer. No lock and no other engine state is written");
    }
}

// PROBE P028 - is the `coop-peer` stand-in faction a player faction? Emitted ONCE, on the main thread, the
// moment the stand-in exists. `locked` rests on the answer being 0: if it is 1 the engine will treat the
// peer's boxes as ours and `locked` has to refuse non-owners itself (BasePolicyAllows does, and counts it).
void PolicyNotePeerFaction(void* faction)
{
    g_peerFaction = faction;
    void* ip = 0;
    const int why = IsPlayerFieldPod(faction, &ip);
    g_peerIsPlayer = (why == 1) ? (ip != 0 ? 1 : 0) : -1;
    char sid[160]; sid[0] = 0;
    const int sw = FactionSidPod(faction, sid, 160);
    if (g_p028Said) return;
    g_p028Said = true;
    DebugLog(std::string("[PROBE] P028 coop-peer faction=") + PS(faction)
             + " isPlayer=" + (why == 1 ? PS(ip) : std::string(why == 0 ? "unreadable" : "FAULTED"))
             + " sid='" + std::string(sw == 1 ? sid : "?") + "'"
             + " -> locked mode " + (g_peerIsPlayer == 0
                   ? std::string("can lean on the engine's own stranger behaviour (this faction is NOT a player faction, so a locked peer box shows PICK_LOCK exactly as in single-player)")
                   : std::string("CANNOT lean on the engine (the stand-in reads as a player faction, or could not be read) - the plugin refuses non-owners on boxes itself; doors are untouched")));
}

// MAIN THREAD, from playerfaction.cpp's ResetPlayerFactionState: the engine has freed the factions. The hook
// only ever COMPARES this pointer and never dereferences it, so a stale one cannot fault - but a faction
// allocated at the same address in the next world would match it, and every one of that world's buildings
// would then read as the peer's under `shared`. Cleared here, and P028 re-arms so the next world measures its
// own stand-in rather than inheriting the last world's answer.
void PolicyForgetPeerFaction()
{
    g_peerFaction = 0; g_peerIsPlayer = -1; g_p028Said = false;
}

int PolicyPeerIsPlayer() { return g_peerIsPlayer; }

// The requester's faction string id AS THIS GAME KNOWS IT.
//
// Both players' real player factions carry the SAME GameData string id, which is precisely why store.cpp's
// TranslateZoneOwners rewrites <my id> to "coop-peer" in every building record a zone the other game wrote
// carries. So on this machine there are exactly two player-side ids: mine, and "coop-peer" for the other
// player - and a remote requester is always the second one. There is no third player today; when there is,
// this is the function that has to learn a per-peer id, and the swap in store.cpp has to learn it with it.
// review-p6h HIGH-1: `local` is the only thing that says "me". The peer id cannot, because the transport gives
// THE HOST peer id 0 (net/transport.h:127; net/enet_transport.cpp:173 hands out 1,2,3... when hosting and a
// literal 0 when connecting), so on a client game every request from the host used to take the local branch.
std::string PolicyRequesterSid(unsigned int requesterPeer, bool local)
{
    if (!local)
    {
        /* The requester's stand-in BY SLOT - coop-p<slot of the player whose request this is>. `requesterPeer` is the key the request
           arrived with: on the world server's road it carries the acting player's slot, on the old game-to-game link it is that link's
           peer (its announced slot). With no slot known the answer is "" and `owner` mode refuses rather than guesses. */
        ::InterlockedIncrement64(&g_policyRequesterFromPeer);
        const int s = net::PlayerSlotOfKey(requesterPeer);
        return s >= 0 ? coopslot::StandInId(s) : std::string();
    }
    char sid[160]; sid[0] = 0;
    if (LocalPlayerSidPod(sid, 160) != 1) return std::string();
    return std::string(sid);
}

/* The request gate's judgement. count = false: nothing is counted or logged (the `team access` lever's own question, so the
   counters keep meaning real requests); *rule names the rule that decided. */
bool PolicyJudge(void* building, const std::string& requesterFactionSid, bool count, const char** rule)
{
    const char* dummy = 0;
    if (rule == 0) rule = &dummy;
    Bump(&g_checks, count);
    /* T-546 step 4 (decision 481): every member's base is open to every member - before the policy, whatever it is */
    if (MemberRequest(building, requesterFactionSid)) { Bump(&g_allowedMember, count); *rule = "member"; return true; }
    int pol = BasePolicyValue();
    if (pol == kPolicyUnknown)
    {
        Bump(&g_unknownEvaluated, count);
        if (count && !g_saidUnknown)
        {
            g_saidUnknown = true;
            ErrorLog("[POLICY] no notebook process has said what this world's base access policy is (no relay link, or the WELCOME push has not landed yet). THIS REQUEST IS BEING JUDGED AS `shared`, which is the default - it is NOT the same as having been told `shared`, and it is reported as basepolicy=unknown. Logged once.");
        }
        pol = kPolicyShared;
    }
    if (pol == kPolicyShared) { Bump(&g_allowedShared, count); *rule = "shared"; return true; }

    char ownerSid[160]; ownerSid[0] = 0;
    void* f = FactionOfBuildingPod(building);
    const int why = FactionSidPod(f, ownerSid, 160);
    if (why != 1 || ownerSid[0] == 0)
    {
        // NOT "allow because we could not tell". Under `owner` and `locked` the whole purpose is to restrict,
        // and an unreadable owner is the one case where allowing would silently defeat the mode. It is refused
        // and counted apart from a real refusal, so a run can tell a policy decision from a read failure.
        Bump(&g_ownerUnreadable, count);
        *rule = "owner-unreadable";
        return false;
    }
    const std::string owner(ownerSid);
    // review-p6h HIGH-2, AND THE ORDER IS THE FIX. This read used to happen inside the classification below, so
    // a failure did not refuse - it made every building of MY OWN faction unrecognisable, and an unrecognised
    // building was then classified as NPC-owned, which the policy deliberately allows. Under `owner`/`locked`
    // that opened this game's whole base for as long as the read kept failing, silently, counted as an ordinary
    // bandit chest. It is now checked BEFORE the classification and refuses, exactly as an unreadable BUILDING
    // owner does twelve lines above - the two failures now agree about what "we could not tell" means.
    char mine[160]; mine[0] = 0;
    const int mineWhy = LocalPlayerSidPod(mine, 160);
    if (mineWhy != 1)
    {
        // P6u / review-p6m MEDIUM-1: ONE REFUSAL, ONE COUNTER. `mineWasStandIn` used to be a strict SUBSET of
        // `mineUnreadable` - both were incremented for the same event - so `basePolicy3[...]=7,7` read as
        // "seven read failures, seven of them stand-ins" when it meant "zero read failures", and nothing on
        // the line said to subtract. The three causes are now disjoint and each says what it measures:
        //   3  nothing resolved at all - no faction manager yet, or no faction in it carries +0x250. That is
        //      a world that has not finished loading, not a fault, and it gets its own name.
        //   0  a faction WAS resolved and its string id could not be read, or read empty. A real read failure.
        //   2  it resolved to the `coop-peer` stand-in itself, by address or by its own string id.
        // All three still REFUSE - that half was already right and is unchanged.
        if (mineWhy == 3) Bump(&g_mineNotYetKnown, count);
        else if (mineWhy == 2) Bump(&g_mineWasStandIn, count);
        else Bump(&g_mineUnreadable, count);
        *rule = "mine-unreadable";
        return false;
    }
    // Reached only with BOTH sids read: the owner's (why == 1, non-empty) and this game's own. So `notPlayerOwned`
    // now counts one thing - a sid that was READ and matched no player faction on this machine - and can no
    // longer absorb "my own base, misread", which is the conflation review-p6h named.
    const bool ownerIsPlayerSide = IsStandInSid(owner.c_str()) || (owner == std::string(mine));   /* stand1: any stand-in (coop-p<n>) by id */
    if (!ownerIsPlayerSide)
    {
        // A building owned by a real NPC faction - a town shop counter, a bandit chest, a ruin. The base access
        // policy is about PLAYER bases (decision 40: "a hosting option attached to the building's owner
        // faction"), so it says nothing here and the engine's own rules stand. INTERPRETATION, stated rather
        // than measured: decision 40 does not name this case.
        Bump(&g_notPlayerOwned, count);
        *rule = "not-player-owned";
        return true;
    }
    const bool isOwner = !requesterFactionSid.empty() && owner == requesterFactionSid;
    if (pol == kPolicyOwner)
    {
        if (isOwner) { Bump(&g_allowedOwner, count); *rule = "owner"; return true; }
        Bump(&g_refusedOwner, count);
        *rule = "owner-only";
        return false;
    }
    // kPolicyLocked - the owner always; everybody else falls through to the ENGINE'S lock-and-pick, with no
    // plugin refusal at all. That is only true while the peer stand-in is not a player faction, which is what
    // P028 measures; if it is one, the engine would treat the peer's boxes as ours and `locked` would mean
    // nothing, so we refuse here instead. Doors are untouched either way - nothing in this plugin gates a door.
    Bump(&g_locked, count);
    if (isOwner) { *rule = "owner"; return true; }
    if (PolicyPeerIsPlayer() != 0)
    {
        Bump(&g_lockedRefusedFallback, count);
        *rule = "locked-refused";
        return false;
    }
    *rule = "locked-engine-lock";
    return true;
}
bool BasePolicyAllows(void* building, const std::string& requesterFactionSid) { return PolicyJudge(building, requesterFactionSid, true, 0); }

/* T-546 step 4: the `team access <slot> [class]` lever - ownerSlot's nearest loaded building (whose RTTI class name contains
   `cls`, when given), met by this game's player as a click and a hover meet it:
   - the building's OWN getDefaultTask (vtable +0x418, the call PlayerInterface::buildingSelected makes first at 0x7FBF86) and its
     OWN getMouseCursor (vtable +0x410, the slot every hooked class's cursor sits in - 0x16AFBB8 / 0x16B15D8 / 0x16B0EC8 ...), each
     through the engine's dispatch, so whatever hook that class reaches answers and counts as it does for a player: the click gate
     (UseableStuff::getDefaultTask's own isThePlayer sites - memberClick / ownedByPeerAnswered / ownedByPeerStranger) and the
     cursor hooks (memberCursor / memberDoorCursor / cursorRemapped / cursorStranger / doorCursorStranger). The line prints the
     two answers and every counter that moved; a class whose getter has no ownership term moves none.
   - the request gate's judgement (PolicyJudge, NOT counted: the request counters keep meaning real requests).
   The six PlayerInterface::buildingSelected sites and the door panel (DoorStuff::getGUIData) are not called: the first issues the
   order, the second builds a panel. Both getters only read. MAIN THREAD. */
namespace {
int g_accessSlot = -1;
std::string g_accessCls;
int g_accessOwned = 0;            /* buildings of that player's faction the walk met (class or not) */
std::string g_accessClsSeen;      /* their classes, each once, when none matched the asked class (the refusal line names them) */
int AccessWant(void* b)
{
    if (StandInSlotAnyPod(FactionOfBuildingPod(b)) != g_accessSlot) return 0;
    ++g_accessOwned;
    if (g_accessCls.empty()) return 1;
    char cls[128]; cls[0] = 0;
    const unsigned int vt = PolicyVtableRvaQuietPod(b);
    if (vt == 0 || coop::PolicyClassNamePod(vt, cls, 128) != 1) std::strcpy(cls, "?");
    if (std::strstr(cls, g_accessCls.c_str()) != 0) return 1;
    if (g_accessClsSeen.find(cls) == std::string::npos && g_accessClsSeen.size() < 400) g_accessClsSeen += std::string(g_accessClsSeen.empty() ? "" : ",") + cls;
    return 0;
}
/* the refusal's words: how many of that player's buildings were met, and their classes when a class was asked */
std::string AccessMissText()
{
    return PN((long long)g_accessOwned) + " of that player's buildings met" + (g_accessClsSeen.empty() ? std::string() : " (classes: " + g_accessClsSeen + ")");
}
typedef unsigned long long (*VtGetterFn)(void*);
/* the object's own virtual getter at `off`, faulting closed: 1 called (*out = the low 32 bits of the answer), 0 not */
int CallVtGetterPod(void* obj, size_t off, unsigned int* out)
{
    __try
    {
        void** vt = *(void***)obj;
        if (!Plaus(vt) || !Plaus(vt[off / 8])) return 0;
        *out = (unsigned int)(((VtGetterFn)vt[off / 8])(obj) & 0xFFFFFFFFull);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
const size_t kVtGetMouseCursor = 0x410, kVtGetDefaultTask = 0x418;
/* the counters the main thread's own click / hover path bumps (ownedByPeerPassedThrough is left out: the navmesh worker bumps it too) */
const int kAccessCounters = 12;
volatile LONGLONG* const kAccessCounter[kAccessCounters] = { &g_memberClick, &g_ownedByPeerAnswered, &g_ownedByPeerAnsweredClick, &g_ownedByPeerStranger,
    &g_memberCursor, &g_memberCursorOther, &g_cursorRemapped, &g_cursorOtherAnswer, &g_cursorStranger, &g_memberDoorCursor, &g_doorCursorStranger, &g_memberDoorPanel };
const char* const kAccessCounterName[kAccessCounters] = { "memberClick", "ownedByPeerAnswered", "ownedByPeerAnsweredClick", "ownedByPeerStranger",
    "memberCursor", "memberCursorOther", "cursorRemapped", "cursorOtherAnswer", "cursorStranger", "memberDoorCursor", "doorCursorStranger", "memberDoorPanel" };
/* the classes whose getDefaultTask (+0x418) and getMouseCursor (+0x410) were read (policy.cpp's class table; capstone on the
   1.0.65 vtables 0x16AF7A8 / 0x16B11C8 / 0x16B0AB8: 0x2AB390 / 0x2991F0 / 0x2ADBC0 and 0x546C30 / 0x547010) - only these are called */
bool GettersRead(const char* cls)
{
    static const char* const kRead[6] = { ".?AVStorageBuilding@@", ".?AVUseableStuff@@", ".?AVDoorStuff@@", ".?AVResearchBuilding@@", ".?AVTurretBuilding@@", ".?AVLightBuilding@@" };
    for (int i = 0; i < 6; ++i) if (std::strcmp(cls, kRead[i]) == 0) return true;
    return false;
}
std::string MovedSince(const LONGLONG* before)
{
    std::string moved;
    for (int i = 0; i < kAccessCounters; ++i)
    {
        const LONGLONG d = *kAccessCounter[i] - before[i];
        if (d != 0) moved += std::string(moved.empty() ? "" : ",") + kAccessCounterName[i] + "+" + PN((long long)d);
    }
    return moved.empty() ? std::string("none") : moved;
}
long long g_accessProbes = 0;
std::string HexU(unsigned int v) { char b[16]; std::sprintf(b, "0x%X", v); return b; }
}
std::string PolicyTeamAccessProbe(int ownerSlot, const std::string& cls)
{
    const std::string who = "s" + PN((long long)ownerSlot);
    if (EngineWritesBlocked()) return "error team access: no world loaded";
    const int me = MySlotForWire();
    if (ownerSlot == me) return "error team access: " + who + " is this game's own player";
    g_accessSlot = ownerSlot; g_accessCls = cls; g_accessOwned = 0; g_accessClsSeen.clear();
    double dist = 0.0; std::string err; unsigned seen = 0;
    void* b = NearestBuildingWhere(&AccessWant, &dist, &err, &seen);
    g_accessSlot = -1; g_accessCls.clear();
    if (b == 0)
    {
        DebugLog("[TEAM] access " + who + (cls.empty() ? std::string() : " class '" + cls + "'") + ": no loaded building of that player's faction" + (cls.empty() ? std::string() : " of that class") + " (" + err + "; " + PN((long long)seen) + " buildings looked at, " + AccessMissText() + ")");
        return "error team access: no loaded building of " + who + "'s faction";
    }
    ++g_accessProbes;
    const unsigned int vt = PolicyVtableRvaQuietPod(b);
    char name[128]; name[0] = 0;
    if (vt == 0 || PolicyClassNamePod(vt, name, 128) != 1) std::strcpy(name, "?");
    const bool member = TeamSameAnyThread(me, ownerSlot);
    LONGLONG before[kAccessCounters];
    for (int i = 0; i < kAccessCounters; ++i) before[i] = *kAccessCounter[i];
    unsigned int task = 0, cursor = 0;
    const bool read = GettersRead(name);
    const int taskCalled = read ? CallVtGetterPod(b, kVtGetDefaultTask, &task) : 0;
    const int cursorCalled = read ? CallVtGetterPod(b, kVtGetMouseCursor, &cursor) : 0;
    const std::string moved = MovedSince(before);
    const char* rule = "?";
    const bool allowed = PolicyJudge(b, PolicyRequesterSid(0, true), false, &rule);
    DebugLog("[TEAM] access " + who + "'s building " + std::string(name) + " " + PN((long long)dist) + " units away: owner " + who + " team "
             + PN((long long)TeamNoOfSlotAnyThread(ownerSlot)) + ", this player s" + PN((long long)me) + " team " + PN((long long)TeamNoOfSlotAnyThread(me))
             + " -> member=" + (member ? "1" : "0") + "; base policy " + BasePolicyName()
             + (read ? "; getDefaultTask " + (taskCalled ? HexU(task) : std::string("FAULTED")) + ", getMouseCursor " + (cursorCalled ? HexU(cursor) : std::string("FAULTED"))
                     : std::string("; getters not called (a class whose getters were not read)"))
             + "; hooks moved: " + moved
             + "; the request gate would answer " + (allowed ? "allowed" : "REFUSED") + " (" + rule + ", not counted)"
             + " (basePolicyMember allowedMember " + PN(g_allowedMember) + " click " + PN(g_memberClick) + " cursor " + PN(g_memberCursor) + " doorCursor " + PN(g_memberDoorCursor) + "; probes " + PN(g_accessProbes) + ")");
    return std::string("ok team access member=") + (member ? "1" : "0") + " cursor=" + (cursorCalled ? HexU(cursor) : std::string("fault"))
         + " task=" + (taskCalled ? HexU(task) : std::string("fault")) + " hooks=" + moved + " request=" + (allowed ? "allowed" : "refused");
}

/* T-546 step 4b: the `team click <slot> [class]` lever - a REAL click on ownerSlot's nearest loaded building (its RTTI class name
   containing `cls`, when given): the engine's own PlayerInterface::buildingSelected (1.0.65 0x7FBB40) is called for this game's
   player interface with the building and its position, with the interface's click byte (+0x2F3) set for the call and put back
   after it. +0x2F3 is the byte every order in that function is gated on - with it 0 the function only picks the cursor, with it
   set it issues the order (decomp_7fbb40.txt: 0x7F9AB0 / 0x7FA2D0 / 0x7F9CB0 calls each under `*(param_1+0x2f3) != 0`; Read) -
   so the call is a hover-then-click on that building as the mouse makes it: the hooks at the click sites answer and count, the box
   stub answers its owner test, and the order goes to whatever this player has selected. The line lists every main-thread hook
   counter that moved and the box stub's teammate answers. MAIN THREAD (the command channel tick). */
namespace {
unsigned long long kBuildingSelectedRva = 0; static AddrReg kBuildingSelectedRva_reg("PlayerInterfaceBuildingSelected", &kBuildingSelectedRva);   /* Steam_1.0.65 0x7FBB40 */
typedef char (*BuildingSelectedFn)(void* pi, void* building, const float* pos, char flag);
const size_t kFacPlayerInterface = 0x250, kPiClickByte = 0x2F3;
typedef void (*GetPosFn)(void*, float*);
int BuildingPosPod(void* b, float* pos)
{
    __try { void** vt = *(void***)b; float v[3] = { 0, 0, 0 }; ((GetPosFn)vt[8])(b, v); pos[0] = v[0]; pos[1] = v[1]; pos[2] = v[2]; return 1; }   /* vtbl+0x40 getPosition */
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* 1 called (*answer = its bool), 0 faulted; the click byte is put back either way */
int ClickPod(void* pi, void* b, const float* pos, int* answer, int* byteWas)
{
    unsigned char* clickByte = (unsigned char*)pi + kPiClickByte;
    __try
    {
        *byteWas = *clickByte;
        *clickByte = 1;
        const char r = ((BuildingSelectedFn)(g_base + (uintptr_t)kBuildingSelectedRva))(pi, b, pos, 0);
        *clickByte = (unsigned char)*byteWas;
        *answer = r != 0 ? 1 : 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        __try { *clickByte = (unsigned char)*byteWas; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return 0;
    }
}
void* PlayerInterfacePod(void* faction) { __try { return *(void**)((char*)faction + kFacPlayerInterface); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
long long g_clicks = 0;
}
std::string PolicyTeamClick(int ownerSlot, const std::string& cls)
{
    const std::string who = "s" + PN((long long)ownerSlot);
    if (EngineWritesBlocked()) return "error team click: no world loaded";
    if (kBuildingSelectedRva == 0 || g_base == 0) return "error team click: no address row for PlayerInterface::buildingSelected";
    const int me = MySlotForWire();
    if (ownerSlot == me) return "error team click: " + who + " is this game's own player";
    void* pi = PlayerInterfacePod((void*)LocalPlayerFaction());
    if (!Plaus(pi)) return "error team click: this game's player faction has no player interface";
    g_accessSlot = ownerSlot; g_accessCls = cls; g_accessOwned = 0; g_accessClsSeen.clear();
    double dist = 0.0; std::string err; unsigned seen = 0;
    void* b = NearestBuildingWhere(&AccessWant, &dist, &err, &seen);
    g_accessSlot = -1; g_accessCls.clear();
    if (b == 0)
    {
        DebugLog("[TEAM] click " + who + (cls.empty() ? std::string() : " class '" + cls + "'") + ": no loaded building of that player's faction" + (cls.empty() ? std::string() : " of that class") + " (" + err + "; " + PN((long long)seen) + " buildings looked at, " + AccessMissText() + ")");
        return "error team click: no loaded building of " + who + "'s faction";
    }
    float pos[3] = { 0, 0, 0 };
    if (BuildingPosPod(b, pos) != 1) return "error team click: the building's position could not be read";
    const unsigned int vt = PolicyVtableRvaQuietPod(b);
    char name[128]; name[0] = 0;
    if (vt == 0 || PolicyClassNamePod(vt, name, 128) != 1) std::strcpy(name, "?");
    const bool member = TeamSameAnyThread(me, ownerSlot);
    LONGLONG before[kAccessCounters];
    for (int i = 0; i < kAccessCounters; ++i) before[i] = *kAccessCounter[i];
    const LONGLONG hits0 = g_boxCells != 0 ? g_boxCells->hits : 0;
    int answer = 0, byteWas = 0;
    const int called = ClickPod(pi, b, pos, &answer, &byteWas);
    ++g_clicks;
    const std::string moved = MovedSince(before);
    const long long boxHits = g_boxCells != 0 ? (long long)(g_boxCells->hits - hits0) : -1;
    DebugLog("[TEAM] click " + who + "'s building " + std::string(name) + " " + PN((long long)dist) + " units away: member=" + (member ? "1" : "0") + "; base policy "
             + BasePolicyName() + "; PlayerInterface::buildingSelected " + (called ? (answer ? std::string("answered 1") : std::string("answered 0")) : std::string("FAULTED"))
             + " (click byte was " + PN((long long)byteWas) + ", put back); hooks moved: " + moved + "; box stub teammate answers +"
             + (boxHits < 0 ? std::string("n/a (not patched: ") + g_boxWhy + ")" : PN(boxHits)) + " (cells held " + PN((long long)g_boxHeld) + "; clicks " + PN(g_clicks) + ")");
    return std::string("ok team click member=") + (member ? "1" : "0") + " called=" + (called ? "1" : "0") + " hooks=" + moved + " boxHits=" + (boxHits < 0 ? std::string("n/a") : PN(boxHits));
}

/* T-546 step 7: the `team orders <slot>` lever (TEST-ONLY). For this game's own first character and for the first character of player
   `slot`'s faction: the engine's PlayerInterface::isEnemy through its hooked address; the engine's interaction menu built on the
   character (ContextMenu::showContextMenu(pi + 0x48, 1, it), then closed as newPlayerTask closes it) with the last list the engine made
   (sub 1 = the nested menu of a person in a bed or cage; cage 1 = filtered as for a caged character), the list it was shown, and the
   shown orders the base game does not offer on one's own character (withheldShown); and the engine's right-click handler
   characterSelected as a hover (the click byte held at 0 for the call, put back; not made while the player interface is picking a
   target), with the sneaking answers the hook changed. MAIN THREAD. */
namespace {
void* FactionOfPod(void* obj)
{
    __try { void** vt = *(void***)obj; if (!Plaus(vt)) return 0; typedef void* (*FacFn)(void*); return ((FacFn)vt[0x58 / 8])(obj); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void* FirstCharacterOf(void* faction)
{
    if (faction == 0 || coop::GameWorldPtr() == 0) return 0;
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) return 0;
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        void* ch = (void*)*it;
        if (Plaus(ch) && FactionOfPod(ch) == faction) return ch;
    }
    return 0;
}
int OrdIsEnemyPod(void* pi, void* ch, int* ans)
{
    __try { *ans = ((PiIsEnemyFn)(g_base + (uintptr_t)kPiIsEnemyRva))(pi, ch) != 0 ? 1 : 0; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int OrdMenuPod(void* pi, void* ch)
{
    __try
    {
        ((ShowMenuFn)(g_base + (uintptr_t)kShowMenuRva))((char*)pi + kPiMenu, 1, ch);
        ((ShowMenuFn)(g_base + (uintptr_t)kShowMenuRva))((char*)pi + kPiMenu, 0, 0);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* The hover: 1 made, 0 faulted, 2 not made - the player interface is picking a target (+0x2F1 set), where characterSelected hands the
   character to its target-picking call, which gives an order whatever the click byte says. */
int OrdHoverPod(void* pi, void* ch)
{
    unsigned char* clickByte = (unsigned char*)pi + kPiClickByte;
    unsigned char was = 0;
    __try
    {
        if (*((unsigned char*)pi + kPiTargetMode) != 0) return 2;
        was = *clickByte;
        *clickByte = 0;
        ((CharSelectedFn)(g_base + (uintptr_t)kCharSelectedRva))(pi, ch);
        *clickByte = was;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        __try { *clickByte = was; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return 0;
    }
}
std::string OrdersOn(void* pi, void* ch, std::string* shownOut, std::string* withheldOut)
{
    *shownOut = "none"; *withheldOut = "none";
    if (ch == 0) return "no loaded character";
    int enemy = 0;
    const int e = OrdIsEnemyPod(pi, ch, &enemy);
    g_capOn = 1; g_capBuilt = 0; g_capRawN = 0; g_capShownN = 0; g_capSub = 0; g_capCage = 0;
    const int m = OrdMenuPod(pi, ch);
    g_capOn = 0;
    const LONG64 sneak0 = g_ordSneakForced, clicks0 = g_ordClicks;
    const int h = OrdHoverPod(pi, ch);
    const std::string raw = swteamord::ListText(g_capRaw, g_capRawN), shown = swteamord::ListText(g_capShown, g_capShownN);
    const std::string notOwn = swteamord::NotOwnIn(g_capShown, g_capShownN, g_capCage != 0);
    if (g_capBuilt != 0) { *shownOut = shown; *withheldOut = notOwn; }
    return std::string("isEnemy=") + (e ? PN((long long)enemy) : std::string("FAULTED"))
         + " menu=" + (!m ? std::string("FAULTED") : g_capBuilt ? std::string("built sub=") + PN((long long)g_capSub) + " cage=" + PN((long long)g_capCage)
                                                               + " engineList=[" + raw + "] shown=[" + shown + "] withheldShown=[" + notOwn + "]"
                                                               : std::string("not built (no selected character?)"))
         + " hover=" + (h == 1 ? std::string("ok") : h == 2 ? std::string("skipped (picking a target)") : std::string("FAULTED"))
         + " teammateClicks+" + PN((long long)(g_ordClicks - clicks0)) + " sneakForced+" + PN((long long)(g_ordSneakForced - sneak0));
}
}
std::string PolicyTeamOrders(int slot)
{
    const std::string who = "s" + PN((long long)slot);
    if (EngineWritesBlocked()) return "error team orders: no world loaded";
    if (kPiIsEnemyRva == 0 || kShowMenuRva == 0 || kCharSelectedRva == 0 || g_base == 0) return "error team orders: an address row is missing";
    if (g_ordHooks != 5) return "error team orders: " + PN((long long)g_ordHooks) + " of 5 hooks installed";
    if (kMenuEnemyRet == 0 || kOrdHandCharRva == 0) return "error team orders: the menu builder's isEnemy site or hand::getCharacter has no row";
    const int me = MySlotForWire();
    if (slot == me) return "error team orders: " + who + " is this game's own player";
    void* pi = PlayerInterfacePod((void*)LocalPlayerFaction());
    if (!Plaus(pi)) return "error team orders: this game's player faction has no player interface";
    ::Faction* theirs = StandInForSlot(slot);
    if (theirs == 0) theirs = StandInRecordFaction(slot);
    const bool member = TeamSameAnyThread(me, slot);
    const LONG64 forced0 = g_ordEnemyForced, removed0 = g_ordRemoved;
    std::string ownShown, ownWithheld, mateShown, mateWithheld;
    const std::string own = OrdersOn(pi, FirstCharacterOf((void*)LocalPlayerFaction()), &ownShown, &ownWithheld);
    const std::string mate = theirs != 0 ? OrdersOn(pi, FirstCharacterOf((void*)theirs), &mateShown, &mateWithheld) : std::string("no faction for that player here");
    DebugLog("[TEAM] orders " + who + ": member=" + (member ? "1" : "0") + " cells=" + PN((long long)g_ordHeld) + " | own character: " + own + " | " + who
             + "'s character: " + mate + " | enemyForced+" + PN((long long)(g_ordEnemyForced - forced0)) + " removed+" + PN((long long)(g_ordRemoved - removed0)) + " |" + TeamOrdersTokens());
    return std::string("ok team orders member=") + (member ? "1" : "0") + " ownShown=" + ownShown + " ownWithheld=" + ownWithheld
         + " theirShown=" + mateShown + " theirWithheld=" + mateWithheld;
}

std::string PolicyReport()
{
    /* ` basepolicy=` itself is printed by store.cpp's ReportStore, beside the link state it belongs with. */
    return std::string(" basePolicy[checks,allowedShared,allowedOwner,refusedOwner,locked]=" + PN(g_checks) + "," + PN(g_allowedShared) + "," + PN(g_allowedOwner) + "," + PN(g_refusedOwner) + "," + PN(g_locked)
         + " basePolicy2[unknownEvaluated,notPlayerOwned,ownerUnreadable,lockedRefusedFallback,peerIsPlayer,hook]=" + PN(g_unknownEvaluated) + "," + PN(g_notPlayerOwned) + "," + PN(g_ownerUnreadable) + "," + PN(g_lockedRefusedFallback) + "," + PN((long long)PolicyPeerIsPlayer()) + "," + PN(g_hookInstalled)
         + " basePolicy3[mineUnreadable,mineWasStandIn,mineNotYetKnown,policyRequesterFromPeer]=" + PN(g_mineUnreadable) + "," + PN(g_mineWasStandIn) + "," + PN(g_mineNotYetKnown) + "," + PN(g_policyRequesterFromPeer)
         + " basePolicyMember[allowedMember,click,doorPanel,cursor,doorCursor,cursorOther]=" + PN(g_allowedMember) + "," + PN(g_memberClick) + "," + PN(g_memberDoorPanel) + "," + PN(g_memberCursor) + "," + PN(g_memberDoorCursor) + "," + PN(g_memberCursorOther)
         + TeamOrdersTokens()
         + " teamBox[patched,cellsHeld,hits]=" + PN(g_boxCells != 0 ? 1 : 0) + "," + PN((long long)g_boxHeld) + "," + PN(g_boxCells != 0 ? (long long)g_boxCells->hits : 0)
         + " ownedByPeerAnswered=" + PN(g_ownedByPeerAnswered)
         + " ownedByPeerAnsweredClick=" + PN(g_ownedByPeerAnsweredClick)
         + " doors1[ownedByPeerStranger,cursorStranger,doorCursorStranger]=" + PN(g_ownedByPeerStranger) + "," + PN(g_cursorStranger) + "," + PN(g_doorCursorStranger)
         + " strangerVt=" + PH(g_strangerVt1 < 0 ? 0 : g_strangerVt1) + "," + PH(g_strangerVt2 < 0 ? 0 : g_strangerVt2)
         + " (doors1: cursorOtherAnswer now counts storage boxes only; ownedByPeerAnswered - the getDefaultTask half - can no longer rise, only ownedByPeerAnsweredClick)"
         + " ownedByPeerPassedThrough=" + PN(g_ownedByPeerPassedThrough)
         + " ownedByPeerDoorPanelPassedThrough=" + PN(g_ownedByPeerDoorPanelPassedThrough)
         + " ownedByPeerOtherCallerRva=" + PH(g_retSample1) + "," + PH(g_retSample2)
         + " rttiOutOfImage=" + PN(g_rttiOutOfImage) + " imageSize=" + PH((long long)g_imageSize)
         + " cursorRemapped=" + PN(g_cursorRemapped) + " cursorOtherAnswer=" + PN(g_cursorOtherAnswer) + " cursorHooks=" + PN(g_cursorHooks));
}

}
