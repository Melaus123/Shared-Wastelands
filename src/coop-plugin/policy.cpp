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
    if (coop::BasePolicyValue() != coop::kPolicyShared) return a;
    if (!coop::IsStandInFaction((::Faction*)FactionOfBuildingPod(building))) return a;   /* stand1 fold (2b): a save's coop-peer too */   /* stand1: owned by ANY player's stand-in (coop-p<n>), not "the peer" */
    const uintptr_t base = g_base;
    const uintptr_t rva = (base != 0 && (uintptr_t)ret > base) ? ((uintptr_t)ret - base) : 0;
    const bool clickGate = (kGetDefaultTaskRet1 != 0 && rva == (uintptr_t)kGetDefaultTaskRet1)
                        || (kGetDefaultTaskRet2 != 0 && rva == (uintptr_t)kGetDefaultTaskRet2);   /* an empty row is no caller */
    const bool clickHandler = !clickGate && PolicyIsClickRet(rva);
    if (!clickGate && !clickHandler)
    {
        // P7c: the door datapanel's own site is counted APART from the nine engine callers, so a run can say
        // how many times a peer door's panel was built with the stranger's answer - i.e. how many times the
        // lock button was NOT offered. The two are disjoint: one event increments exactly one of them.
        if (kDoorPanelRetNotAnswered != 0 && rva == (uintptr_t)kDoorPanelRetNotAnswered) ::InterlockedIncrement64(&g_ownedByPeerDoorPanelPassedThrough);
        else
        {
            PolicySampleRet((LONGLONG)rva);
            ::InterlockedIncrement64(&g_ownedByPeerPassedThrough);
        }
        return a;
    }
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
    if (!CursorIsPeerObject(self)) return a;
    {
        const unsigned int vt = PolicyVtableRvaQuietPod(self);   /* doors1: boxes only */
        if (vt != kVtStorageBuilding) { ::InterlockedIncrement64(&g_cursorStranger); PolicySampleStrangerVt((LONGLONG)vt); return a; }
    }
    if (a == kCursorTypeEightNoContainer)
    {
        // A type-8 useable with no container and no lock. An own one answers 9, so this IS a mismatch - but
        // nothing has characterised what such an object is, and remapping an answer we cannot name is how a UI
        // stops matching behaviour. Counted, not changed; the register carries it as the residual.
        ::InterlockedIncrement64(&g_cursorOtherAnswer);
        return a;
    }
    ::InterlockedIncrement64(&g_cursorRemapped);
    return kCursorOwnUse;
}
// A door's own locked answer is 0xE and a stranger's is 0xF; both sides answer 0xC unlocked. UNTIL doors1 the 0xF was
// remapped to 0xE under `shared`; since the user's decision (2026-09-24: the owner's lock holds) it is only counted.
unsigned long long detour_DoorCursor(void* self)
{
    const unsigned long long a = orig_DoorCursor(self);
    if (a != (unsigned long long)kCursorPickLock) return a;
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

void InstallPolicy()
{
    g_base = (uintptr_t)::GetModuleHandleA(0);
    /* P7c / review-p6u MEDIUM-3: the P029 walk's bound, read from our own PE header rather than guessed at. */
    g_imageSize = PolicySizeOfImagePod(g_base);
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

bool BasePolicyAllows(void* building, const std::string& requesterFactionSid)
{
    ::InterlockedIncrement64(&g_checks);
    int pol = BasePolicyValue();
    if (pol == kPolicyUnknown)
    {
        ::InterlockedIncrement64(&g_unknownEvaluated);
        if (!g_saidUnknown)
        {
            g_saidUnknown = true;
            ErrorLog("[POLICY] no notebook process has said what this world's base access policy is (no relay link, or the WELCOME push has not landed yet). THIS REQUEST IS BEING JUDGED AS `shared`, which is the default - it is NOT the same as having been told `shared`, and it is reported as basepolicy=unknown. Logged once.");
        }
        pol = kPolicyShared;
    }
    if (pol == kPolicyShared) { ::InterlockedIncrement64(&g_allowedShared); return true; }

    char ownerSid[160]; ownerSid[0] = 0;
    void* f = FactionOfBuildingPod(building);
    const int why = FactionSidPod(f, ownerSid, 160);
    if (why != 1 || ownerSid[0] == 0)
    {
        // NOT "allow because we could not tell". Under `owner` and `locked` the whole purpose is to restrict,
        // and an unreadable owner is the one case where allowing would silently defeat the mode. It is refused
        // and counted apart from a real refusal, so a run can tell a policy decision from a read failure.
        ::InterlockedIncrement64(&g_ownerUnreadable);
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
        if (mineWhy == 3) ::InterlockedIncrement64(&g_mineNotYetKnown);
        else if (mineWhy == 2) ::InterlockedIncrement64(&g_mineWasStandIn);
        else ::InterlockedIncrement64(&g_mineUnreadable);
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
        ::InterlockedIncrement64(&g_notPlayerOwned);
        return true;
    }
    const bool isOwner = !requesterFactionSid.empty() && owner == requesterFactionSid;
    if (pol == kPolicyOwner)
    {
        if (isOwner) { ::InterlockedIncrement64(&g_allowedOwner); return true; }
        ::InterlockedIncrement64(&g_refusedOwner);
        return false;
    }
    // kPolicyLocked - the owner always; everybody else falls through to the ENGINE'S lock-and-pick, with no
    // plugin refusal at all. That is only true while the peer stand-in is not a player faction, which is what
    // P028 measures; if it is one, the engine would treat the peer's boxes as ours and `locked` would mean
    // nothing, so we refuse here instead. Doors are untouched either way - nothing in this plugin gates a door.
    ::InterlockedIncrement64(&g_locked);
    if (isOwner) return true;
    if (PolicyPeerIsPlayer() != 0)
    {
        ::InterlockedIncrement64(&g_lockedRefusedFallback);
        return false;
    }
    return true;
}

std::string PolicyReport()
{
    /* ` basepolicy=` itself is printed by store.cpp's ReportStore, beside the link state it belongs with. */
    return std::string(" basePolicy[checks,allowedShared,allowedOwner,refusedOwner,locked]=" + PN(g_checks) + "," + PN(g_allowedShared) + "," + PN(g_allowedOwner) + "," + PN(g_refusedOwner) + "," + PN(g_locked)
         + " basePolicy2[unknownEvaluated,notPlayerOwned,ownerUnreadable,lockedRefusedFallback,peerIsPlayer,hook]=" + PN(g_unknownEvaluated) + "," + PN(g_notPlayerOwned) + "," + PN(g_ownerUnreadable) + "," + PN(g_lockedRefusedFallback) + "," + PN((long long)PolicyPeerIsPlayer()) + "," + PN(g_hookInstalled)
         + " basePolicy3[mineUnreadable,mineWasStandIn,mineNotYetKnown,policyRequesterFromPeer]=" + PN(g_mineUnreadable) + "," + PN(g_mineWasStandIn) + "," + PN(g_mineNotYetKnown) + "," + PN(g_policyRequesterFromPeer)
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
