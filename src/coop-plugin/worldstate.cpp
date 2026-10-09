// worldstate.cpp - E5 / decision 31(c). See worldstate.h for what this is for.
//
// THREADING - E19, and it is the whole shape of this file. `Character::uniqueStateUpdate` is called from
// `Character::threadedUpdatePeriodic` 0x5CF620, i.e. off the main thread, and the unload and respawn writers run
// on paths whose thread is not established. So: NO DETOUR IN THIS FILE READS THE SHARED STATE MAP AT ALL.
//
// The claim this file used to make - that `getState` 0x5E7D60 and `wasPlayerInvolved` 0x9AFE30 "only ever READ" -
// was wrong about the path that matters. `decomp_5e7d60:27-33`: on a HIT, getState re-enters `find_or_insert`
// 0x3498F0. Every unique whose entry exists (every dead or imprisoned one - precisely the ones this feature is
// about) therefore took the insert-shaped walk TWICE per periodic tick from a worker thread, while the main thread
// inserted into the same map on receipt. That is review-p5m's CRASH residual, and it is removed here by removing
// the reads, not by making them cheaper: a detour now does the `Character::isUnique` gate, the original, and a push
// of the character's GameData POINTER into a fixed 256-slot ring under an InterlockedIncrement ticket. No map, no
// string, no allocation, no lock, no logging.
//
// THE MAIN THREAD IS THE ONLY READER. `WorldStateTick` takes each rung pointer, reads the map ONCE, and diffs that
// against `g_wsShadow` - this plugin's own record of what it last saw for that record and last told the notebook. A
// rung entry whose state is unchanged costs one map read and stops there. Only a real change resolves the string id
// and sends. The map pointer is still read DIRECTLY out of the global at 0x212DAF0 and never through the accessor
// 0x354500, because that accessor CREATES the map on first use (operator new) and the drain must not be the one to
// do that either; an absent map simply means every unique is ALIVE, which is the map's own sparse rule.
//
// Everything that allocates, logs, sends or writes the map happens on the main thread: WorldStateTick (the drain)
// and ApplyRemoteUniqueState (the write-back).
//
// THE GATE, AND WHY IT IS STILL HERE (P5m / review-p5j CRASH-1, as it stands after E19 and E24). No detour takes a
// "before" reading any more - that pair of map walks per character per periodic tick is gone with the reads
// themselves, and nothing below this line reads the shared map at all. What the `Character::isUnique` 0x505ED0 gate
// still does is keep the RING, and therefore the drain's map reads, to named characters only: without it every
// character in the world would be pushed every periodic tick. E24 makes the gate cheaper rather than removing it -
// a plugin-side memo answers for a record the gate has already been asked about (see WsMemoGet below), so the engine
// call runs once per named record per world instead of once per character per tick.
//
// E20 - WHOSE TRUTH WINS, AND WHO IS ALLOWED TO SAY IT. Both games can have the same named character loaded at once:
// one owns it, the other holds a ghost of it (a copy made by the spawn factory, or this save's own twin adopted under
// the peer's uid - spawn.cpp AdoptExistingTwin). Nothing in the engine stops the ghost's periodic check running, and
// nothing in it gates on faction or ownership (read-e20.md 1b/1c), so the ghost's own simulation writes the same
// table for the same record and publishes a second, competing answer. E20 settles both directions on the MAIN THREAD,
// never in a detour, because the loaded-character walk and `net::IsUidMine` are neither of them safe from a worker:
//
//   SEND  - a changed record is published only when this game OWNS the character carrying it (FindSpawnedUid then
//           IsUidMine). A ghost therefore never publishes. When the change came from an unload or a respawn clear and
//           no character carries the record any more, the shadow's last-known owner flag decides, and "never
//           established" means do not publish.
//   APPLY - not loaded here: write the table. A GHOST: write the table for any state and NEVER call declareDead - a
//           received death must not kill a ghost, because the owner's own character stream does not carry death to it
//           (net/session.cpp:494-495 reads `dead` off the wire and discards it) and a kill here would make a corpse
//           the owner never made. OURS and DEAD: kill through the engine's own `Character::declareDead`, since a DEAD
//           in the notebook can only have come from a genuine owner death. OURS and alive/imprisoned: REFUSE it and
//           re-assert - send this game's own table reading straight back, because for an alive, free, non-player
//           faction unique the engine writes the table NEVER (decomp_5ce9e0:81-83), so nothing else would ever
//           correct the notebook.
//
// E27 - THE THREE THINGS E20 LEFT OPEN (build/review-p5q.md HIGH-2, HIGH-3, HIGH-4).
//
//   THE TWIN IS ITS OWN CLASS. "A ghost" above lumped together two things that are not alike. A ghost the spawn
//   factory built here is a body the PEER's game asked for; this save's own TWIN, registered under the peer's uid
//   because both players run the same save (spawn.cpp AdoptExistingTwin, spawn.h IsTwinUid), is THE SAME
//   INDIVIDUAL. The never-kill rule exists so this game does not put a corpse on screen that the owner never made;
//   it has nothing to say about a character the owner HAS just killed and which exists here in its own right. So a
//   received DEAD kills a twin (`appliedDeadKillTwin`) and still never kills a factory ghost, while every
//   non-death state is a plain map write for both. A twin still never publishes: its owner speaks for it.
//
//   A RE-ASSERT IS DAMPED. The hand-off overlaps ownership on purpose - the receiver takes the uid at XFER and the
//   sender releases it only when the ACK returns (handoff.cpp) - so for one round trip BOTH games answer "mine"
//   for one record, and E20 had given both of them a reflex that answers every disagreement immediately. The relay
//   forwards to everyone but the sender, so A says 1, B says 2, A says 1 ... at link speed, with a rewrite of the
//   relay's file per lap. Two refusals stop it: never re-send the state this game last sent for that id, and at
//   most one re-assert per id per five seconds. The first breaks the cycle after ONE lap; the second bounds
//   anything the first cannot see - and `reassertDamped` is also the DETECTOR, because a re-assert that has to
//   fire twice running for one id is by construction a second writer.
//
//   THE OWNED-HERE FLAG IS CLEARED WHEN THIS GAME GIVES OWNERSHIP AWAY. It was monotone upward: only a later
//   resolve that found a loaded ghost could put it back to 0, and after a hand-off the character is still loaded
//   here as a puppet, so that resolve happens only if a rung entry beats the engine's unload. The hand-off exists
//   because this game is walking away, so that is a race it loses - and losing it meant the one branch that cannot
//   re-ask published for a character the peer now owns. handoff.cpp's ACK path now says so directly
//   (WorldStateOnOwnershipReleased). The flag is also refreshed by EVERY apply, including the equal-state one,
//   which is the commonest apply once two games have converged and used to establish nothing at all.
//
// ON THE WIRE. The drain sends `kStoreMsgUniqueState` (store.cpp) on the notebook link, which the notebook process
// knows as `MSG_UNIQUE_STATE` = 36 (src/coop-store/store_main.cpp): a string id, then the state, then playerInvolved. What the
// world server sends also carries how many times that character was brought back.
//
// T-556 - A NAMED CHARACTER BROUGHT BACK (owner 493; the rule is src/common/fallenwire.h's). The engine's setter never lifts a
// stored DEAD, so the world server's bring-back mark (an ALIVE with back > 0) is written into the entry directly on every game
// whose map reads DEAD (WsBringBackPod) - except this game's own dead body, whose DEAD is newer. The game that brings one back
// writes it ALIVE at once (WorldStateBroughtBack) and holds off a DEAD from the server for it until the server confirms. A DEAD
// this game's map reads while a LIVING own character carries the record (the old body's squad unloading) is not published.
// The loaded-character walk prefers a living carrier over a dead one, so the brought-back character, not the old body, answers.
#include "worldstate.h"
#include "resurrect.h"       // T-556: ResurrectNoteDeath - the fallen list
#include "addresses.h"   /* P8h: kXxxRva below is filled from the address table, not hard-coded */
#include "store.h"          // StoreSendUniqueState - the notebook link; P5m: StoreMainThreadId + StoreDeclareDeadPod
#include "soak.h"           /* P7f (review-p6z C-1): GameplayRunning - F337's frame-counter test, the only honest "is there a world" (F034: the GameWorld pointer cannot tell) */
#include "spawn.h"          // E20: FindSpawnedUid - which uid, if any, this loaded character is registered under
#include "net/session.h"    // E20: net::IsUidMine - is that uid one this game owns (the authority map)
#include "medical.h"        // par6 (parity P6): CopyDeathAllowed - a copy dies only when its owner says so
#include <intrin.h>         // par6 fold (review-par6 #1): _ReturnAddress - which engine caller is killing a copy
#include "coop_log.h"
#include "game/GameWorld.h"   // P5m: activeCharacters - the engine's own set of the characters it is updating
#include "game/Character.h"   // T-556: hasDied - a living carrier is preferred, and a living carrier's map DEAD is not published
#include "game/GameData.h"    // WsTemplateIsUnique: a template's bool-field table, walked
#include "../common/fallenwire.h"   // T-556: BringBackWrite, DeadMayPublish
#include "../common/uniqslot.h"     // FirstSightAlive, ElsewhereMember, DeadSetMember, MemberVerdict, DupDecide
#include "../common/removalreason.h"   // kDuplicateNamedRemoved - the reason a second living body is destroyed with
#include "zones.h"          // SectorOf, AreaHolderSlotTS - which game runs a body's area
#include "handoff.h"        // HandoffPendingHas, ReleasePendingHas, SquadActivePlatoonOf
#include "playerfaction.h"  // MySlotForWire
#include "ai_spike.h"       // GetTarget - the watched player
#include "game/GameSaveState.h"      // SavedObjectState - the record a group member is made from
#include "hooks.h"   /* mig1: coop::AddHook (own MinHook) */
#include "worldgen.h"   // WorldGenDestroyHooked - the destroy detour that announces a removal is installed
#include <Windows.h>
namespace coop { bool AnyPlayersFaction(::Faction* f); }   // playerfaction.cpp: any player's faction - this game's own, a stand-in, a recorded stand-in
#include <map>          // E19: g_wsShadow, the main-thread picture of the state map
#include <vector>       // the dead set and the elsewhere set
#include <algorithm>    // std::binary_search over those sets
#include <set>
#include <string>
#include <cstring>
#include <cstdio>

namespace coop {
namespace {

// ---- the engine (all RVAs from build/read-world-states.md, which resolved them with tools/resolve_stub.py) ----
unsigned long long kMapPtrRva = 0; static coop::AddrReg kMapPtrRva_reg("MapPtr", &kMapPtrRva);   /* P8h: the address table fills this. Steam_1.0.65 0x212DAF0 */   // the global GameHashMap<GameData*, UniqueNPCManager::UniqueCharacterState>* (once-flag 0x212DAFC)
unsigned long long kGetMapRva = 0; static coop::AddrReg kGetMapRva_reg("GetMap", &kGetMapRva);   /* P8h: the address table fills this. Steam_1.0.65 0x354500 */    // UniqueNPCManager::getMap() - CREATES on first use; MAIN THREAD ONLY
unsigned long long kGetStateRva = 0; static coop::AddrReg kGetStateRva_reg("GetState", &kGetStateRva);   /* P8h: the address table fills this. Steam_1.0.65 0x5E7D60 */    // int getState(map, GameData*) - returns 1 (ALIVE) for an ABSENT key, but it is NOT a pure read: on a HIT it re-enters find_or_insert 0x3498F0 (decomp_5e7d60:27-33). MAIN THREAD ONLY (E19)
unsigned long long kWasPlayerInvolvedRva = 0; static coop::AddrReg kWasPlayerInvolvedRva_reg("WasPlayerInvolved", &kWasPlayerInvolvedRva);   /* P8h: the address table fills this. Steam_1.0.65 0x9AFE30 */    // bool wasPlayerInvolved(map, GameData*) - reads value+0x34; it walks the same bucket array, so MAIN THREAD ONLY too (E19)
unsigned long long kFindOrInsertRva = 0; static coop::AddrReg kFindOrInsertRva_reg("FindOrInsert", &kFindOrInsertRva);   /* P8h: the address table fills this. Steam_1.0.65 0x3498F0 */    // pair* find_or_insert(map, GameData** key) - INSERTS; MAIN THREAD ONLY
unsigned long long kSetImprisonedRva = 0; static coop::AddrReg kSetImprisonedRva_reg("SetImprisoned", &kSetImprisonedRva);   /* P8h: the address table fills this. Steam_1.0.65 0x34ADC0 */    // void setImprisoned(map, GameData*, char imprisoned, char playerInvolved) - the SHARED setter
unsigned long long kDeclareDeadRva = 0; static coop::AddrReg kDeclareDeadRva_reg("DeclareDead", &kDeclareDeadRva);   /* P8h: the address table fills this. Steam_1.0.65 0x7A5660 */    // void Character::declareDead(Character*)
unsigned long long kUniqueStateUpdateRva = 0; static coop::AddrReg kUniqueStateUpdateRva_reg("UniqueStateUpdate", &kUniqueStateUpdateRva);   /* P8h: the address table fills this. Steam_1.0.65 0x5CE9E0 */    // void Character::uniqueStateUpdate(Character*)
unsigned long long kCheckUniquesRva = 0; static coop::AddrReg kCheckUniquesRva_reg("CheckUniques", &kCheckUniquesRva);   /* P8h: the address table fills this. Steam_1.0.65 0x9A7250 */    // void ActivePlatoon::_checkForUniqueCharactersOnUnload(ActivePlatoon*)
unsigned long long kIsUniqueRva = 0; static coop::AddrReg kIsUniqueRva_reg("IsUnique", &kIsUniqueRva);   /* P8h: the address table fills this. Steam_1.0.65 0x505ED0 */    // bool Character::isUnique(Character*) - reads the character's OWN game-data bool property "unique" (decomp_505ed0: *(GameData**)(this+0x40) + 0xF8); it never touches the state map
unsigned long long kClearAllRva = 0; static coop::AddrReg kClearAllRva_reg("ClearAll", &kClearAllRva);   /* P8h: the address table fills this. Steam_1.0.65 0x4FECD0 */    // void ActivePlatoon::clearAllTheUniqueNPCStates(ActivePlatoon*) - respawn (decomp_4fecd0)
unsigned long long kGdContainerRva = 0; static coop::AddrReg kGdContainerRva_reg("GdContainer", &kGdContainerRva);   /* P8h: the address table fills this. Steam_1.0.65 0x21330D0 */   // the base GameDataContainer OBJECT (every engine call site passes &DAT_1421330d0)
unsigned long long kGdcGetDataRva = 0; static coop::AddrReg kGdcGetDataRva_reg("GdcGetData", &kGdcGetDataRva);   /* P8h: the address table fills this. Steam_1.0.65 0x6BD190 */    // GameData* GameDataContainer::getData(container, const std::string& sid) - find only
// ActivePlatoon vt+0x38 - makes ONE member of a group loaded from its records, through RootObjectFactory::create
// 0x582970, which never asks the engine's "may make" check 0x591720 (decomp_36f570). 0x36D6A0 (ActivePlatoon vt+0x30)
// calls it once per saved entry on each pass over the group's entries (it passes over them twice when it is given a
// filter); a group's load runs on the main thread or on a worker.
unsigned long long kPlatoonLoadInstanceRva = 0; static coop::AddrReg kPlatoonLoadInstanceRva_reg("PlatoonLoadInstance", &kPlatoonLoadInstanceRva);   /* the address table fills this. Steam_1.0.65 0x36F570, Steam_1.0.68 0x36F6B0 */
// The engine's "may make" check for a named character: bool fn(table, GameData* template) - 1 may make (no entry, or the
// entry's made slot +8 is empty), 0 may not (decomp_591720). Its only callers (build/xrefs_591720.txt, through the jump
// stub 0x50E20) are createRandomCharacter 0x582C50 (a 0 makes the template's "unique replacement spawn" instead, or
// nothing) and FactionWarMgr::getAllTheForces 0x9C4170 (a 0 skips that character's squad).
unsigned long long kMayMakeRva = 0; static coop::AddrReg kMayMakeRva_reg("UniqueMayMake", &kMayMakeRva);   /* the address table fills this. Steam_1.0.65 0x591720, Steam_1.0.68 0x5921B0 */
// UniqueCharacterState, from the pair* the map hands back: +0x30 int state (0 DEAD / 1 ALIVE / 2 IMPRISONED), +0x34 bool playerInvolved.
const int kStateOff = 0x30, kPlayerOff = 0x34;
const int kUsedOff = 0x8;   // the entry's template (GameData*): set when the unique is made (createCharacter 0x582C50:109) and for every used unique at load (0x9A93A0:86); 0x591720 refuses to make a unique whose entry holds it
long long g_wsStateGen = 0;   // MAIN THREAD: WorldStateGeneration
const int kCharDataOff = 0x40;        // RootObjectBase::data - the GameData the map is keyed by (Character::declareDead uses this[8])
const int kPlatoonCountOff = 0x58, kPlatoonArrayOff = 0x60;   // ActivePlatoon: active character count, Character* array (decomp_9a7250:33-36)

typedef void* (*GetMapFn)();
typedef int   (*GetStateFn)(void* map, void* gd);
typedef char  (*WasPlayerInvolvedFn)(void* map, void* gd);
typedef void* (*FindOrInsertFn)(void* map, void** key);
typedef void  (*SetImprisonedFn)(void* map, void* gd, char imprisoned, char playerInvolved);
typedef void  (*CharFn)(void* self);
typedef void  (*PlatoonFn)(void* self);
typedef void* (*GdcGetDataFn)(void* container, const std::string* sid);
typedef char  (*IsUniqueFn)(void* self);   // one argument, `this` in rcx: decomp_505ed0 takes param_1 and decomp_9a7250:35 passes the Character straight to it

uintptr_t g_base = 0;
CharFn    orig_declareDead = 0;
CharFn    orig_uniqueStateUpdate = 0;
PlatoonFn orig_checkUniques = 0;
SetImprisonedFn orig_setImprisoned = 0;
PlatoonFn orig_clearAllUniqueStates = 0;
// 0x36D6A0 passes seven arguments (its own third one last); the function reads six. All seven are passed through untouched.
typedef void (*LoadInstanceFn)(void* self, void* state, void* a3, void* a4, void* a5, void* a6, void* a7);
LoadInstanceFn orig_loadInstance = 0;
typedef unsigned char (*MayMakeFn)(void* table, void* gd);   // the answer is the low byte; both callers test only that
MayMakeFn orig_mayMake = 0;

// The re-entrancy flag the design calls for, carried as the APPLYING THREAD's id rather than a bool: the apply runs on
// the main thread while `uniqueStateUpdate` can be writing the same map on a worker, and a plain bool would swallow
// that worker's genuine change as if it were our own echo.
volatile LONG g_wsApplyingThread = 0;
bool WsApplying() { return (DWORD)::InterlockedCompareExchange(&g_wsApplyingThread, 0, 0) == ::GetCurrentThreadId(); }
// par6 fold (review-par6 #2): the thread id set around the ONE call that kills a loaded unique on the notebook's DEAD (the
// apply below, main thread). The copy death gate lets that call through: the world record decides a twin's death.
volatile LONG g_wsRecordKillThread = 0;
bool WsWorldRecordKill() { return (DWORD)::InterlockedCompareExchange(&g_wsRecordKillThread, 0, 0) == ::GetCurrentThreadId(); }
long long g_seenDeclareDeadRefused = 0;   // review-par6 #9: declareDead calls the copy death gate refused (not counted as calls)

long long g_sent = 0, g_recv = 0, g_applied = 0, g_unknownSid = 0, g_unchanged = 0;   // the five the report names
long long g_sendFailed = 0, g_noEffect = 0, g_queued = 0, g_ringDropped = 0, g_sidFault = 0, g_unloadOverflow = 0, g_applyFault = 0, g_stateFault = 0;
long long g_seenDeclareDead = 0, g_seenStateUpdate = 0, g_seenUnload = 0, g_seenSetter = 0, g_seenClearAll = 0;
// review-p5j CRASH-1: how much traffic the gate removed, and how often the gate itself could not be read.
long long g_nonUnique = 0, g_isUniqueFault = 0;
// E24.4 (review-p5o MEDIUM-2): the memo that answers the gate without calling the engine. `memoFull` is a record the
// table had no free slot for within its probe window - it costs a call, never a wrong answer.
long long g_memoHit = 0, g_memoMiss = 0, g_memoFull = 0;
// E24.2 (review-p5o HIGH-2): `ringDeduped` is a push refused because that record was already waiting in the ring -
// the duplicates the periodic writer produces, which are what filled the ring and made the unload writer's LAST
// ring for a record (the one no later ring recovers) the one that got dropped. `ringSlotShared` is a push whose
// pending slot was held by a DIFFERENT record: the dedupe is skipped and the push goes through, so a hash collision
// costs a duplicate rather than a lost change.
long long g_ringDeduped = 0, g_ringSlotShared = 0;
// E24.1 (review-p5o HIGH-1): a first sight of a non-ALIVE record that was NOT published, because the notebook's
// opening push had not finished (so "does the notebook know this id" was unanswerable) or because the notebook has
// already named that id itself.
long long g_firstSightSuppressed = 0;
// E32 (verify-p5t HIGH-2): a first sight seen BEFORE the notebook's opening push finished is DEFERRED, not
// suppressed - the two are different events and only one of them is an answer. `firstSightSuppressed` means "the
// notebook has already named this id, so its own answer is in the table and this is not news", which is a decision.
// `firstSightDeferred` means "the notebook has not been asked yet", which is not a decision at all, and writing the
// shadow for it made the record `known` so the question was never asked again - permanently, for any record whose
// state never changes (which for a DEAD record is all of them).
long long g_firstSightDeferred = 0;
// E24.5a (review-p5o MEDIUM-5a): a state the map handed back that is not 0, 1 or 2. The map's own value is
// trustworthy; the paths that can return something else are the lazy-init window and a bucket walk that outlived a
// rehash, and a wire message carrying a wild number is worse than a counter saying the read went wrong.
long long g_stateRange = 0, g_recvBadState = 0;
// review-p5j MEDIUM-2: which thread each detour actually ran on. Nothing in P5j recorded this, so every threading
// claim in the header above was unmeasurable. 0 from StoreMainThreadId() means "not yet known" and counts as OFF.
long long g_thrSetterMain = 0, g_thrSetterOff = 0, g_thrDeadMain = 0, g_thrDeadOff = 0;
long long g_thrUpdateMain = 0, g_thrUpdateOff = 0, g_thrUnloadMain = 0, g_thrUnloadOff = 0, g_thrClearMain = 0, g_thrClearOff = 0;
// review-p5j HIGH-1 / HIGH-2 + E20: what the apply decided to do, one counter per branch. `appliedDeadMap` and
// `appliedMap` are now the NOT-LOADED cases only; a loaded ghost's table write is `appliedMirrorMap` and a loaded
// character of ours refusing a non-death is `reassertedLocal`. P5m's `skippedLoadedHere` is RETIRED rather than left
// at a permanent zero: E20 leaves no branch that could increment it, and a counter no code can reach reads as
// "nothing was dropped" instead of as "this measurement is gone" (lesson 12's corollary).
long long g_appliedDeadKill = 0, g_appliedDeadMap = 0, g_appliedMap = 0, g_killNoEffect = 0;
long long g_appliedMirrorMap = 0, g_reassertedLocal = 0;
// E27 / review-p5q HIGH-4, HIGH-2, HIGH-3, MEDIUM-2. `appliedDeadKillTwin` is a received death applied to THIS
// SAVE'S OWN twin of a shared-save named character - the same individual the owner just killed - and it is counted
// apart from `deadKill` because it is the one kill this game makes for a character it does not own.
// `reassertDamped` is a re-assert REFUSED (it repeated the last state this game sent for that id, or it came under
// kReassertMinSec after the previous one for that id); it is the ping-pong detector as well as the brake.
// `reassertSent` / `reassertSendFailed` keep the apply's sends OUT of the drain's `sent` / `sendFailed`, so the
// drain's reconciliation identity means what it says (see the identity comment at g_shadowChanged).
// `ownedHereCleared` is a shadow row demoted because this game released the character's ownership.
// `lastSendCapped` is an id the bounded last-sent register had no room for; such an id is never damped.
long long g_appliedDeadKillTwin = 0, g_reassertDamped = 0, g_reassertSent = 0, g_reassertSendFailed = 0;
long long g_ownedHereCleared = 0, g_lastSendCapped = 0;
long long g_loadedWalkRefused = 0, g_reportedOverflow = 0;
long long g_broughtBackHere = 0, g_appliedBroughtBack = 0, g_backConfirmed = 0, g_deadKillHeldBack = 0, g_deadLivingCarrier = 0;   // T-556
std::set<std::string> g_wsBackPending;   // T-556, MAIN THREAD: named characters this game brought back that the world server has not confirmed yet
std::set<void*> g_wsBackGd;              // T-556, MAIN THREAD: the records of named characters brought back (by this game, or back > 0 from the world server)
// E20, the send gate. `sendSkippedNotOwned` is a change on a character loaded here that is NOT ours (the ghost case,
// which is the whole reason the gate exists). `sendSkippedUnknownOwner` is a change on a record no loaded character
// carries any more - an unload or a respawn clear - where the shadow never established that this game owned it.
// `loadedNoUid` sizes the one thing read-e20.md 2 said not to assume empty: a character loaded here that
// FindSpawnedUid answers 0 for (never announced, or a row P034 retired) is treated as NOT ours and is silenced by
// the gate. `ownRefreshSkipped` is a last-known-owner refresh the per-drain budget did not allow.
long long g_sendSkippedNotOwned = 0, g_sendSkippedUnknownOwner = 0, g_loadedNoUid = 0, g_ownRefreshSkipped = 0;
/* P6c (p5z MEDIUM-4): sends the gate refused for a body whose uid could not be read AND whose shadow's last-known
   flag said "ours" - i.e. the publications P5z's kOwnUnknown had started to admit. Counted apart from
   `sendSkippedUnknownOwner` because these are the ones that would OTHERWISE HAVE GONE OUT, which is the only number
   that sizes the widening. */
long long g_sendSkippedUnknownUid = 0;
// E19: the main-thread shadow diff. `unchanged` is a rung entry whose state the shadow already held (one map read,
// nothing else); `changed` is one that differed and was sent. `drainBadGd` is a published ring slot whose pointer
// was not plausible - it can only come from a slot published without one, so it is expected to stay 0.
long long g_shadowUnchanged = 0, g_shadowChanged = 0, g_drainBadGd = 0;
long long g_teardowns = 0, g_teardownRingDropped = 0;
// The drain's ALIVE first sights (uniqslot.h FirstSightAlive): published (sent / send failed), learned silently, deferred because the
// world server's opening data is not applied yet.
long long g_firstSightAliveSent = 0, g_firstSightAliveSendFailed = 0, g_firstSightAliveLearned = 0, g_firstSightAliveDeferred = 0;
long long g_firstSightAliveLogged = 0;
const long long kUniqLogMax = 20;
// The engine's "may make" check (uniqslot.h ElsewhereMember): calls on the main thread / off it, and the "may not" answers given here
// because the template is alive or imprisoned in another game. `mayMakeHook` 1 installed, 0 no address, -1 the hook failed.
long long g_mayMakeMain = 0, g_mayMakeOff = 0, g_mayMakeRefused = 0;
int g_mayMakeHook = 0;
// The record-member maker (uniqslot.h MemberVerdict): calls on the main thread / off it, members whose template is in the dead set,
// members skipped, those of them made fresh (the load's "first time" argument; no record read), members made because they
// are dead bodies, members made because a read faulted or the set could not be asked. `memberHook` as `mayMakeHook`.
long long g_memberMain = 0, g_memberOff = 0, g_memberDeadSet = 0, g_memberSkipped = 0, g_memberFirstTime = 0, g_memberBody = 0, g_memberFault = 0;
int g_memberHook = 0;
// The two sets: copies made, and copies refused because the loaded-character list was implausible (the sets
// were kept as they were).
long long g_setRebuilds = 0, g_setWalkRefused = 0;

std::string N(long long v) { char b[32]; _snprintf(b, 31, "%lld", v); b[31] = 0; return b; }
std::string Hex(unsigned long long v) { char b[32]; _snprintf(b, 31, "0x%llX", v); b[31] = 0; return b; }
bool WsPlaus(const void* p) { const uintptr_t v = (uintptr_t)p; return v > 0x10000 && v < 0x7FFFFFFFFFFFULL; }
// review-p5j MEDIUM-2. FAILS CLOSED, the same way store.cpp's OnMainThread does: an id of 0 is "I do not know", and
// the honest reading of that inside a detour is "assume a worker", so it is booked as an off-thread call.
int WsOnMainThread() { const unsigned long m = StoreMainThreadId(); return (m != 0 && ::GetCurrentThreadId() == (DWORD)m) ? 1 : 0; }
void WsNoteThread(long long* mainCount, long long* offCount) { ::InterlockedIncrement64(WsOnMainThread() ? mainCount : offCount); }

// ---- guarded POD reads (no std::string anywhere inside a __try function - C2712) ----
int WsReadU64Pod(uintptr_t at, unsigned long long* out)
{
    __try { *out = *(const unsigned long long*)at; return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void* WsMapPod()   // the map as it stands; 0 when the engine has not created it yet
{
    __try { void* m = *(void* const*)(g_base + kMapPtrRva); return WsPlaus(m) ? m : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int WsGetStateCall(void* map, void* gd)
{
    __try { return ((GetStateFn)(g_base + kGetStateRva))(map, gd); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int WsGetPlayerCall(void* map, void* gd)
{
    __try { return ((WasPlayerInvolvedFn)(g_base + kWasPlayerInvolvedRva))(map, gd) != 0 ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int WsStateOf(void* gd)   // 0/1/2; 1 when the map does not exist yet (its own sparse rule); -1 on a fault
{
    if (!WsPlaus(gd)) return -1;
    void* map = WsMapPod();
    if (map == 0) return 1;
    const int s = WsGetStateCall(map, gd);
    if (s < 0) ::InterlockedIncrement64(&g_stateFault);   /* a silent state read would make "nothing was queued" indistinguishable from "the read failed" (lesson 12) */
    return s;
}
int WsPlayerInvolvedOf(void* gd)
{
    void* map = WsMapPod();
    if (map == 0 || !WsPlaus(gd)) return 0;
    return WsGetPlayerCall(map, gd);
}
int WsSidPod(const void* gd, char* out, int cap)   // GameData::stringID at +0x58 (VS2010 std::string: buffer/pointer at +0, size +0x10, capacity +0x18)
{
    __try
    {
        const char* s = (const char*)gd + 0x58;
        const size_t size = *(const size_t*)(s + 0x10), res = *(const size_t*)(s + 0x18);
        if (size == 0 || size >= (size_t)cap) return 0;
        const char* p = (res >= 16) ? *(const char* const*)s : s;   // heap iff capacity >= 16 (review-p4a HIGH-3)
        if (p == 0) return 0;
        std::memcpy(out, p, size); out[size] = 0; return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void* WsCharGameData(const void* ch)
{
    __try { void* gd = *(void* const*)((const char*)ch + kCharDataOff); return WsPlaus(gd) ? gd : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int WsIsUniqueCall(void* ch)   // 1 named, 0 ordinary, -1 the call faulted
{
    __try { return ((IsUniqueFn)(g_base + kIsUniqueRva))(ch) != 0 ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// ---- E24.4 (review-p5o MEDIUM-2): THE isUnique MEMO. ANY THREAD. ----
// The gate is a lookup in the character's own game-data property map for the key "unique", and the detours ask it
// for EVERY character in the world on EVERY periodic tick - twice vanilla's own rate, since the original asks it
// again inside itself. The answer cannot change for a record, so it is worth remembering.
//
// A std::map on the main thread cannot serve this: the asker is a worker thread. So it is a fixed open-addressed
// table of {GameData*, answer}, claimed with a single pointer-width InterlockedCompareExchange and never erased
// except at world teardown. THE RACE, and why it is safe: a claimer publishes the KEY first and the ANSWER second,
// so a reader can see its own key with no answer yet - which reads as "not known", falls through to the engine call
// and is simply a miss. No reader can ever see one record's key paired with another's answer, because the answer
// slot is only ever written by the thread that won the key's CAS, always with the same value.
//
// A FAULTED gate is never memoised: "the call failed" is not "this is not a named character", and remembering the
// latter would silence that record's world events for the rest of the world's life.
//
// The keys are engine game-data records. They belong to the base container for as long as a world is loaded and are
// freed with it, so the table is cleared at world teardown - otherwise a record freed with one world and a
// different record allocated at the same address in the next would share an answer. The clear is per-slot and
// atomic, exactly as the ring's is, and carries the same residual: a detour running DURING teardown can re-enter a
// stale key. That window is the ring's window, and it is bounded by the same teardown.
const int kMemo = 4096;               // a power of two: the hash is masked
const int kMemoProbe = 8;             // how far a claim may walk before giving up and paying for the call
void* volatile g_wsMemoKey[kMemo];
volatile LONG  g_wsMemoAnswer[kMemo];   // 0 = not written yet, 1 = named, 2 = ordinary
unsigned WsMemoHash(const void* gd) { return (unsigned)(((uintptr_t)gd >> 4) & (uintptr_t)(kMemo - 1)); }
int WsMemoGet(void* gd)   // 1 named, 2 ordinary, 0 not known
{
    unsigned h = WsMemoHash(gd);
    for (int i = 0; i < kMemoProbe; ++i)
    {
        const unsigned at = (h + (unsigned)i) & (unsigned)(kMemo - 1);
        void* const k = (void*)::InterlockedCompareExchangePointer(&g_wsMemoKey[at], 0, 0);
        if (k == 0) return 0;          // an empty slot ends the probe: nothing beyond it can be ours
        if (k == gd) { const LONG a = ::InterlockedCompareExchange(&g_wsMemoAnswer[at], 0, 0); return (int)a; }
    }
    return 0;
}
void WsMemoPut(void* gd, int answer)
{
    unsigned h = WsMemoHash(gd);
    for (int i = 0; i < kMemoProbe; ++i)
    {
        const unsigned at = (h + (unsigned)i) & (unsigned)(kMemo - 1);
        void* const k = (void*)::InterlockedCompareExchangePointer(&g_wsMemoKey[at], gd, 0);
        if (k == 0 || k == gd) { ::InterlockedExchange(&g_wsMemoAnswer[at], (LONG)answer); return; }
    }
    ::InterlockedIncrement64(&g_memoFull);
}
// review-p5j CRASH-1, THE GATE. `Character::isUnique` 0x505ED0 is what the original itself calls as its very first
// line (decomp_5ce9e0:15-17), what the unload writer calls per member (decomp_9a7250:35), and the same "unique" bool
// property `declareDead` reads inline before it writes the map (decomp_7a5660:96-100). It is a plain non-virtual call
// taking `this` in rcx, and its whole body is a lookup in the character's OWN game-data property map at +0xF8 - it
// never reads the shared unique-state map at 0x212DAF0 and it allocates nothing (the "unique" key is 6 chars, inside
// VS2010's 15-char small-string buffer). A fault is booked separately and read as "not named", the safe side: the
// detour then does nothing but call the original, exactly as it would for any ordinary character.
// E24.4: the record is read FIRST (a POD read of Character+0x40, which every caller needed anyway) so the memo can
// be keyed by it, and it is handed back through `outGd` so no caller reads it twice. A record we cannot read simply
// misses the memo and pays for the call, as before.
int WsUniqueGate(void* ch, void** outGd)
{
    void* gd = WsCharGameData(ch);
    if (outGd != 0) *outGd = gd;
    if (gd != 0)
    {
        const int m = WsMemoGet(gd);
        if (m == 1) { ::InterlockedIncrement64(&g_memoHit); return 1; }
        if (m == 2) { ::InterlockedIncrement64(&g_memoHit); ::InterlockedIncrement64(&g_nonUnique); return 0; }
    }
    ::InterlockedIncrement64(&g_memoMiss);
    const int u = WsIsUniqueCall(ch);
    if (u < 0) { ::InterlockedIncrement64(&g_isUniqueFault); return 0; }   /* never memoised: a fault is not an answer */
    if (gd != 0) WsMemoPut(gd, u == 1 ? 1 : 2);
    if (u == 0) { ::InterlockedIncrement64(&g_nonUnique); return 0; }
    return 1;
}
int WsPlatoonArrayPod(const void* pl, unsigned* count, void*** arr)
{
    __try { *count = *(const unsigned*)((const char*)pl + kPlatoonCountOff); *arr = *(void** const*)((const char*)pl + kPlatoonArrayOff); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int WsMemberPod(void** arr, unsigned i, void** out)
{
    __try { *out = arr[i]; return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void* WsGetDataPod(const std::string* sid)   // the base container is the OBJECT at 0x21330D0: every engine call site passes &DAT_1421330d0
{
    __try { void* gd = ((GdcGetDataFn)(g_base + kGdcGetDataRva))((void*)(g_base + kGdContainerRva), sid); return WsPlaus(gd) ? gd : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// review-p5j HIGH-1 / HIGH-2, MAIN THREAD ONLY. Is this named character LOADED here - i.e. is this game simulating
// it right now? The engine's own answer is `GameWorld::activeCharacters` (GameWorld+0x750), the set of
// characters it is actively updating, which is what spawn.cpp's census walks. There is no handle->Character resolver
// in this plugin (the state map's `hand` field is not carried at all - persistence-service.md 24), so the match is
// made on the one key the map itself uses: the character's game-data record, read as a POD from Character+0x40 with
// the same guarded read every detour uses. An implausible list size is REFUSED and counted rather than walked.
int WsCharDeadPod(::Character* c)   // 1 dead, 0 alive, -1 unreadable
{
    __try { return c->hasDied() ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// T-556: for a named character that was brought back, a living carrier is answered before a dead one - the record then has two
// bodies here, the brought-back character and the old body, and the living one is the character the record is about. Every other
// record keeps the walk's own order.
::Character* WsFindLoadedCharacter(void* gd)
{
    if (!WsPlaus(gd) || !WsPlaus(coop::GameWorldPtr())) return 0;
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    const size_t n = all.size();
    if (n > 20000) { ++g_loadedWalkRefused; return 0; }
    const bool preferLiving = g_wsBackGd.count(gd) != 0;
    ::Character* firstDead = 0;
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        ::Character* c = *it;
        if (!WsPlaus(c)) continue;
        if (WsCharGameData(c) != gd) continue;
        if (!preferLiving) return c;
        if (WsCharDeadPod(c) == 1) { if (firstDead == 0) firstDead = c; continue; }
        return c;
    }
    return firstDead;
}

// ---- E20: WHO IS SIMULATING THIS RECORD HERE, AND IS IT OURS? MAIN THREAD ONLY. ----
// Both halves of this answer are main-thread work. The walk above reads the engine's live character list; and
// `net::IsUidMine` reads `g_localOwned`, a std::set that SetLocalOwner / TakeLocalOwner / ReleaseLocalOwner mutate,
// so asking it from a worker would be a red-black-tree find racing an insert (read-e20.md 2). Answering here is why
// E20's rules live in the drain and the apply and not in any detour - and it is also the only place that CAN answer
// for the two writers that are handed a GameData* and no Character* at all.
//
// FindSpawnedUid answering 0 means this game has no uid registered for the character - it was never announced, or
// its row was retired. That is NOT "it is ours": it is "ownership is not established", and the safe reading for a
// gate whose failure mode is two games publishing competing answers is to treat it as not ours. It is counted
// separately (`loadedNoUid`) because read-e20.md 2 says explicitly not to assume that bucket is empty.
// E27 / review-p5q HIGH-4: FOUR answers, not three. `kOwnTwin` is a character loaded here, registered under a uid
// this game does not own, that is THIS SAVE'S OWN character rather than a body the spawn factory built for the
// peer (spawn.h IsTwinUid; spawn.cpp AdoptExistingTwin registers it and deliberately does NOT SetLocalOwner, which
// is why every ownership test before E27 read it as somebody else's ghost). For SENDING the two are the same - a
// twin's owner speaks for it and this game stays quiet. For a received DEATH they are opposites, and that is the
// whole reason the class exists.
const int kOwnNone = 0;     // no loaded character here carries this record
const int kOwnMirror = 1;   // loaded here, but not ours - a body the spawn factory built for the peer's character
const int kOwnMine = 2;     // loaded here and ours
const int kOwnTwin = 3;     // loaded here, not ours, but it IS this save's own copy of that same individual
// E32 (verify-p5u MEDIUM-2) - A FIFTH ANSWER: a character IS loaded here and this game cannot say whose it is,
// because FindSpawnedUid answered 0 (never announced, or a row P034 retired). That is not the same statement as
// kOwnMirror ("loaded here and NOT ours"), and routing it to kOwnMirror wrote a positive falsehood into the shadow:
// kOwnMirror demotes ownedHere to 0 unconditionally, so a correctly-established "we own this record" was erased by
// a momentary re-adoption window - on the equal-state apply, which is the commonest apply once two games agree.
// It answers `prev` instead ("nothing was learned here"), exactly as kOwnNone does, and it is still never sendable
// on its own. `loadedNoUid` remains the number that sizes the window.
const int kOwnUnknown = 4;  // loaded here, and who owns it could not be established
// E32 (verify-p5u MEDIUM-1): the record a uid was last seen carrying, so a release can find the shadow row when the
// character is no longer resolvable. MAIN THREAD ONLY - written here (the drain and the apply are the only callers)
// and read by WorldStateOnOwnershipReleased, which the ACK path calls on the main thread. A GameData* is owned by
// the base container for the life of the process, which is why the shadow is keyed by one; this map is cleared at
// the world teardown with the shadow all the same, because the uids name that world's characters.
std::map<unsigned int, void*> g_wsUidGd;
int WsOwnershipOf(void* gd, ::Character** outLive)
{
    ::Character* c = WsFindLoadedCharacter(gd);
    if (outLive != 0) *outLive = c;
    if (c == 0) return kOwnNone;
    const unsigned int uid = FindSpawnedUid(c);
    if (uid == 0) { ++g_loadedNoUid; return kOwnUnknown; }
    g_wsUidGd[uid] = gd;   /* E32: the only writer - the uid -> record link a release needs when the body is gone */
    if (net::IsUidMine(uid)) return kOwnMine;
    return IsTwinUid(uid) ? kOwnTwin : kOwnMirror;
}
// E27: the classification reduced to the shadow's flag, in ONE place, so the drain, the apply and the equal-state
// apply cannot drift apart. `prev` (-1 from an apply, meaning "keep the row") is returned when nothing is loaded
// here, because "the character is gone" teaches nothing about who owned it and must not overwrite what we knew.
int WsOwnedFlagOf(int own, int prev)
{
    if (own == kOwnMine) return 1;
    if (own == kOwnMirror || own == kOwnTwin) return 0;   // a twin is not ours to publish, exactly like a ghost
    return prev;   // kOwnNone (nothing loaded) and kOwnUnknown (loaded, owner not establishable) both teach nothing
}
int WsOwnedFlag(void* gd, int prev)
{
    return WsOwnedFlagOf(WsOwnershipOf(gd, 0), prev);
}
// How many last-known-owner refreshes an UNCHANGED rung entry may cost per drain. Each one is a walk of the engine's
// live character list, so it is budgeted rather than unbounded; a change or an apply always resolves, budget or not.
const int kOwnRefreshPerDrain = 8;
// How many ALIVE first sights may walk the live character list per drain; the rest are asked again on a later pass
// (a first sight waits while its carrier is not registered, so the same record can be asked on many passes).
const int kFirstSightWalksPerDrain = 16;

// ---- E27 / review-p5q HIGH-2: WHAT THIS GAME LAST PUT ON THE WIRE, AND THE RE-ASSERT DAMPER ----
// Keyed by the string id, because that is what goes on the wire and what the relay keys by. Bounded: past
// kLastSendMax ids a new id is not tracked at all and is counted (`lastSendCapped`) rather than the register
// growing without limit - an untracked id is never damped, which is the safe direction for the feature and the
// unsafe one for the loop, so the cap is sized well above the few loaded named characters a town has ever shown
// (T225 measured shadow[entries]=3). MAIN THREAD ONLY: the drain and the apply are the only callers.
const double kReassertMinSec = 5.0;
const size_t kLastSendMax = 512;
struct WsLastSend { int state; double reassertAt; };
std::map<std::string, WsLastSend> g_lastSend;
double WsNowSec()
{
    LARGE_INTEGER f, c;
    ::QueryPerformanceFrequency(&f); ::QueryPerformanceCounter(&c);
    return f.QuadPart != 0 ? (double)c.QuadPart / (double)f.QuadPart : 0.0;
}
// Record what this game just published for `sid`. `wasReassert` stamps the per-id clock; a drain send does not,
// because the drain is rate-limited by the engine's own writers and is not the half that can loop.
void WsNoteSent(const std::string& sid, int state, bool wasReassert)
{
    std::map<std::string, WsLastSend>::iterator it = g_lastSend.find(sid);
    if (it == g_lastSend.end())
    {
        if (g_lastSend.size() >= kLastSendMax) { ++g_lastSendCapped; return; }
        WsLastSend ne; ne.state = state; ne.reassertAt = wasReassert ? WsNowSec() : -1.0e12;
        g_lastSend.insert(std::make_pair(sid, ne));
        return;
    }
    it->second.state = state;
    if (wasReassert) it->second.reassertAt = WsNowSec();
}
// Both refusals in one place, so `reassertDamped` counts every one of them and is therefore usable as the
// second-writer detector as well as the brake. THE TRADE, stated rather than hidden: a record whose value the peer
// keeps sending back different stays divergent until one side's table genuinely changes. That is deliberate - a
// value this game has already published, arriving straight back, means a SECOND WRITER, and answering it faster is
// exactly what produced the loop. `reassertDamped` climbing is the signal that this is happening.
bool WsReassertAllowed(const std::string& sid, int state)
{
    std::map<std::string, WsLastSend>::const_iterator it = g_lastSend.find(sid);
    if (it == g_lastSend.end()) return true;
    if (it->second.state == state) { ++g_reassertDamped; return false; }
    if (WsNowSec() - it->second.reassertAt < kReassertMinSec) { ++g_reassertDamped; return false; }
    return true;
}

int WsApplyPod(void* gd, int state, int playerInvolved)   // MAIN THREAD only: this one may insert
{
    __try
    {
        void* map = ((GetMapFn)(g_base + kGetMapRva))();   // the creating accessor - safe here, this is the main thread
        if (!WsPlaus(map)) return 0;
        if (state == 0)
        {
            // The shared setter CANNOT express DEAD: decomp_34adc0 only ever writes 1 or 2, and refuses an entry whose
            // state is already 0. The engine's own DEAD writer is find_or_insert followed by a store of 0 at +0x30
            // (Character::declareDead, decomp_7a5660:105-112), so that is the shape used here.
            void* key = gd;
            void* node = ((FindOrInsertFn)(g_base + kFindOrInsertRva))(map, &key);
            if (!WsPlaus(node)) return 0;
            *(int*)((char*)node + kStateOff) = 0;
            if (playerInvolved) *(char*)((char*)node + kPlayerOff) = 1;
            if (*(void**)((char*)node + kUsedOff) == 0) *(void**)((char*)node + kUsedOff) = gd;   /* a unique this game never made is now used here too, as a dead unique loaded from a save is: createCharacter 0x582C50 asks 0x591720, which then refuses it */
            return 1;
        }
        ((SetImprisonedFn)(g_base + kSetImprisonedRva))(map, gd, state == 2 ? (char)1 : (char)0, playerInvolved ? (char)1 : (char)0);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// T-556 - MAIN THREAD: the state written into the record's entry directly, a stored DEAD included - the shape the engine's own
// periodic update uses to write ALIVE into an existing entry (decomp_5ce9e0: find_or_insert, +0x30 = 1, +0x34 = 0). Used only for
// a bring-back (fallenwire.h BringBackWrite); every other apply keeps the engine's own writers above.
int WsBringBackPod(void* gd, int state, int playerInvolved)
{
    __try
    {
        void* map = ((GetMapFn)(g_base + kGetMapRva))();
        if (!WsPlaus(map)) return 0;
        void* key = gd;
        void* node = ((FindOrInsertFn)(g_base + kFindOrInsertRva))(map, &key);
        if (!WsPlaus(node)) return 0;
        *(int*)((char*)node + kStateOff) = state;
        *(char*)((char*)node + kPlayerOff) = playerInvolved ? (char)1 : (char)0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// ---- the ring the detours push into (fixed size, no allocation, no lock) ----
// E19: a rung entry is the character's game-data POINTER and nothing else. Reading that record's string id is a read
// of engine memory and reading its state is a walk of the shared map; both now happen in the drain, on the main
// thread. What a detour publishes is only "this record may have changed - go and look".
struct WsEntry { void* gd; long seq; volatile LONG ready; };
// E24.2 (review-p5o HIGH-2): FOUR TIMES LARGER. A dropped ticket is recovered for the periodic writer - the drain
// reads the map's CURRENT value, so a record rung again is caught - but `detour_checkUniques` rings a record for the
// LAST time, because the character is unloading, and a drop there is permanent. The ring is what stands between an
// unload's change and the notebook, so it is sized for the burst and not for the average.
const int kRing = 1024;   // a power of two: the ticket is masked, never divided
WsEntry g_ring[kRing];
volatile LONG g_ticket = 0;

// ---- E24.2: PER-RECORD DEDUPE, so the ring holds records rather than repetitions ----
// The periodic writer rings every named character every tick whether or not anything about it changed, and that is
// what fills the ring. A record already waiting in it does not need to be pushed again: the drain reads the map's
// value at drain time, so one waiting entry answers for every push until it is read.
//
// The slot is a hash of the record's address, and the record's IDENTITY is stored in it - so a second record that
// hashes to the same slot is not silently swallowed by the first one's pending flag. It simply skips the dedupe and
// is pushed, which is exactly the behaviour before this existed. A collision costs a duplicate, never a lost change,
// which is the only direction this optimisation is allowed to be wrong in.
struct WsPend { void* volatile gd; volatile LONG waiting; };
const int kPending = 4096;   // a power of two: the hash is masked
WsPend g_wsPending[kPending];
unsigned WsPendSlot(const void* gd) { return (unsigned)(((uintptr_t)gd >> 4) & (uintptr_t)(kPending - 1)); }
// ANY THREAD. 1 = this record was not already waiting and the caller should ring it; 0 = it was, and the caller
// should not. `*owned` says whether the slot's flag now belongs to this record, so the drain knows to release it.
int WsPendClaim(void* gd, int* owned)
{
    const unsigned at = WsPendSlot(gd);
    void* const k = (void*)::InterlockedCompareExchangePointer(&g_wsPending[at].gd, gd, 0);
    if (k != 0 && k != gd) { *owned = 0; ::InterlockedIncrement64(&g_ringSlotShared); return 1; }   // another record's slot: ring, do not dedupe
    *owned = 1;
    if (::InterlockedCompareExchange(&g_wsPending[at].waiting, 1, 0) != 0) { ::InterlockedIncrement64(&g_ringDeduped); return 0; }
    return 1;
}
void WsPendRelease(void* gd)
{
    const unsigned at = WsPendSlot(gd);
    if ((void*)::InterlockedCompareExchangePointer(&g_wsPending[at].gd, 0, 0) != gd) return;   // the slot belongs to another record
    ::InterlockedExchange(&g_wsPending[at].waiting, 0);
}

// ANY THREAD. Every caller has already validated `gd` (it comes from WsCharGameData, from the setter's own argument
// under WsPlaus, or from the register, all of which reject an implausible pointer), so there is no refusal here to
// go uncounted. Nothing in this function touches the shared map or the record `gd` points at.
void WsQueue(void* gd)
{
    // E24.2: the dedupe comes FIRST, before a ticket is spent - a repetition must not consume a ring slot, since
    // consuming ring slots is the whole of what it does wrong.
    int ownsPending = 0;
    if (WsPendClaim(gd, &ownsPending) == 0) return;
    const LONG t = ::InterlockedIncrement(&g_ticket) - 1;
    WsEntry& e = g_ring[(unsigned long)t & (unsigned long)(kRing - 1)];
    // A slot that is still READY holds a record the main thread has not looked at yet: the ring is full at this slot
    // and the new one is DROPPED and counted, never written over the old one. A drop costs at most a late reading -
    // the drain reads the map's CURRENT value, so a record that is rung again later is caught then.
    // E24.2: and the pending flag GOES BACK on a drop, or this record would be deduped against a push that was
    // never made and would go quiet until some other writer rang it.
    if (::InterlockedCompareExchange(&e.ready, 0, 0) != 0) { if (ownsPending) WsPendRelease(gd); ::InterlockedIncrement64(&g_ringDropped); return; }
    e.gd = gd; e.seq = (long)t;
    ::InterlockedExchange(&e.ready, 1);   // publish - the barrier is what makes the two fields above visible to the drain
    ::InterlockedIncrement64(&g_queued);
}
// review-p5j MEDIUM-1 / D. `clearAllTheUniqueNPCStates` 0x4FECD0 names no unique in its arguments: it ERASES, through
// 0x505D50, the entry of every named character among this platoon's leader and its "squad"/"squad2" members whose
// stored `hand` is this platoon's - and 0x505D50 REFUSES to erase a DEAD entry (.modding/03-systems/world-states.md:25
// and :42; decomp_4fecd0:66-73 the leader, :103-124 the members). It does NOT clear the whole map and it does NOT gate
// on the player faction. Enumerating its member lists ourselves would need std::string property lookups inside a
// detour, which this file does not do, so the detour rings a bounded register of exactly the uniques THIS game has
// itself reported as non-ALIVE - which is precisely "an entry it removed that this game had previously reported",
// and the drain then reads what each of them says now. Entries are only ever added, never removed (until a world
// teardown), and the register is a fixed array. E19 moved its ONLY feeder into the drain, so it is now written on
// the main thread alone and read (as plain pointers, no map) by the respawn detour.
// E32 (verify-p5u MEDIUM-1, register): 64 -> 512. The register had ONE feeder when it was sized; it now has three
// (a send, a suppressed first sight, and every apply through WsShadowNote), and the relay pushes EVERY row it holds
// at the WELCOME, so a save with more than 64 resolvable non-ALIVE named characters saturates it during the opening
// push and the respawn detour then re-rings a truncated list for the rest of the world's life. The consumer is a
// linear walk (detour_clearAllUniqueStates), not a bitmap, so the cost of 512 is 512 pointer comparisons on a rare
// event and a 4 KB stack snapshot inside that detour - both fine. `reportedOverflow` still says when even 512 is
// not enough, so this is a bigger number and not a silenced one.
const int kReported = 512;
void* g_reported[kReported];
volatile LONG g_reportedN = 0;
void WsNoteReported(void* gd, int state)
{
    if (state == 1) return;   // only a non-ALIVE entry exists to be erased; an ALIVE one is simply an absent key
    const LONG have = ::InterlockedCompareExchange(&g_reportedN, 0, 0);
    for (LONG i = 0; i < have && i < kReported; ++i) if (g_reported[i] == gd) return;
    const LONG slot = ::InterlockedIncrement(&g_reportedN) - 1;
    if (slot >= kReported) { ::InterlockedIncrement64(&g_reportedOverflow); ::InterlockedExchange(&g_reportedN, kReported); return; }
    g_reported[slot] = gd;
}
// ---- E19: the shadow, and it is what makes "no map reads on a worker" possible ----
// The detours name a record; the diff that decides whether anything is worth sending is taken HERE, on the one
// thread allowed to touch the map, against this plugin's own picture of it. Keyed by the GameData record - what the
// engine's map is itself keyed by, and what the base game-data container owns for the life of the process
// (`ApplyRemoteUniqueState` resolves the same records by string id out of that same container).
//
// `playerInvolved` is the value last PUBLISHED for the record, so the shadow is a full picture of what the notebook
// was told. It is deliberately NOT part of the change test: testing it would cost a second map read
// (`wasPlayerInvolved` 0x9AFE30) on every unchanged entry, which is exactly the cost E19 exists to remove. The flag
// is written by the same engine calls that write the state, so a flip of it without a state change is not a case
// this file can produce. `sid` is cached so a record's string id is read once rather than once per change.
// MAIN THREAD ONLY - the drain, the apply and the teardown, and nothing else.
// E20 adds `ownedHere`: 1 = the last time this record resolved to a loaded character, that character was ours;
// 0 = it was a ghost, or no ownership has ever been established for this record. It exists for exactly one case -
// an unload or a respawn clear writes the table for a character that is GONE by the time the drain looks, so nobody
// can be asked who owned it and the flag is the only answer left. It is refreshed on every change, on every apply,
// and on a bounded number of unchanged rung entries per drain (see kOwnRefreshPerDrain).
struct WsShadow { int state; int playerInvolved; int ownedHere; int remote; char sid[64]; };   // remote: 1 the row is a state received from another game and applied here, 0 this game's own
std::map<void*, WsShadow> g_wsShadow;

// ---- THE DEAD SET AND THE ELSEWHERE SET ----
// The dead set (uniqslot.h DeadSetMember) is what the record-member detour asks; the elsewhere set (ElsewhereMember) is what the "may make"
// detour asks. Both detours can run on a worker (a group's records can be loaded off the main thread; the character maker
// and the war-party builder run on paths whose thread is not established), and the engine's table and the loaded-character
// list are read on the main thread only (E19). So the main thread copies the sets out of the shadow into sorted arrays in
// WorldStateTick - whenever the shadow changed, and at least once a second, because what this game carries changes without
// the shadow changing - and the detours search those arrays under the lock. The detours allocate nothing: each searches,
// and queues a template for the main thread's log line in a fixed array. Nothing here is saved: the sets live in memory
// only and are made again from the world server's rows in every session.
CRITICAL_SECTION g_wsSetCs;
volatile LONG g_wsSetCsReady = 0;
std::vector<void*> g_wsDead;          // sorted; guarded by g_wsSetCs
std::vector<void*> g_wsElsewhere;     // sorted; guarded by g_wsSetCs
bool g_wsSetsDirty = true;            // MAIN THREAD: the shadow changed since the last copy
DWORD g_wsSetsAt = 0;                 // MAIN THREAD: GetTickCount at the last copy
const DWORD kSetsRefreshMs = 1000;
const int kLogQueue = 64;
struct WsLogQueue { void* item[kLogQueue]; int n; long long full; };   // guarded by g_wsSetCs
WsLogQueue g_wsSkipQueue;             // record members skipped
WsLogQueue g_wsRefuseQueue;           // makes refused
std::set<void*> g_wsSkipLogged;       // MAIN THREAD: templates whose skip has been logged
std::set<void*> g_wsRefuseLogged;     // MAIN THREAD: templates whose refused make has been logged
int WsSetHas(const std::vector<void*>& set, void* gd)   // ANY THREAD: 1 in the set, 0 not, -1 the sets are not made yet
{
    if (::InterlockedCompareExchange(&g_wsSetCsReady, 0, 0) == 0) return -1;
    ::EnterCriticalSection(&g_wsSetCs);
    const int has = std::binary_search(set.begin(), set.end(), gd) ? 1 : 0;
    ::LeaveCriticalSection(&g_wsSetCs);
    return has;
}
int WsDeadSetHas(void* gd) { return WsSetHas(g_wsDead, gd); }
int WsElsewhereHas(void* gd) { return WsSetHas(g_wsElsewhere, gd); }
void WsLogQueuePush(WsLogQueue& q, void* gd)   // ANY THREAD
{
    if (::InterlockedCompareExchange(&g_wsSetCsReady, 0, 0) == 0) return;
    ::EnterCriticalSection(&g_wsSetCs);
    bool have = false;
    for (int i = 0; i < q.n; ++i) if (q.item[i] == gd) { have = true; break; }
    if (!have)
    {
        if (q.n < kLogQueue) q.item[q.n++] = gd;
        else ++q.full;
    }
    ::LeaveCriticalSection(&g_wsSetCs);
}
long long WsSetCount(const std::vector<void*>* set, const WsLogQueue* q)   // MAIN THREAD (the report): a set's size or a queue's overflow
{
    if (::InterlockedCompareExchange(&g_wsSetCsReady, 0, 0) == 0) return 0;
    ::EnterCriticalSection(&g_wsSetCs);
    const long long n = (set != 0) ? (long long)set->size() : (q != 0 ? q->full : 0);
    ::LeaveCriticalSection(&g_wsSetCs);
    return n;
}
// ---- TWO LIVING BODIES OF ONE NAMED CHARACTER (uniqslot.h DupDecide). MAIN THREAD. ----
// WsSetsRebuild's walk hands over, for every named template (a shadow row, or the memo's "named"), its living bodies; a template
// with two or more is pending (at most kDupPendMax). It is decided once the same bodies - each one's uid, owner and its area's
// runner - have been seen unchanged on consecutive walks for kDupSettleMs, and only right after a walk made in the same tick (the
// body pointers are that walk's; g_wsDupNow is emptied by every rebuild that does not collect). At most one body is removed per
// kDupRemoveGapMs, through the engine's own GameWorld::destroy with the reason kDuplicateNamedRemoved: the destroy detour then
// sends DESPAWN (removalreason.h: a DEATH) and the other games remove their copies. No removal is made while the destroy detour
// that announces it is not installed (WorldGenDestroyHooked). The engine's table is not written: the character lives on in the kept
// body. When the body removed was the one whose squad the engine's table names, the template joins the no-make set while a
// living body carries it here, and the "may make" detour refuses it (a respawn that clears the slot must not make him again).
const DWORD kDupSettleMs = 5000, kDupRemoveGapMs = 1000;
const int kDupPendMax = 16, kDupBodiesMax = 8, kDupLogMax = 20;
struct WsDupKey { unsigned int uid; int owner; int runner; };
inline bool operator==(const WsDupKey& a, const WsDupKey& b) { return a.uid == b.uid && a.owner == b.owner && a.runner == b.runner; }
inline bool operator<(const WsDupKey& a, const WsDupKey& b) { return a.uid != b.uid ? a.uid < b.uid : (a.owner != b.owner ? a.owner < b.owner : a.runner < b.runner); }
struct WsDupPend { std::vector<WsDupKey> keys; DWORD firstMs; unsigned walk; };
std::map<void*, WsDupPend> g_wsDupPend;
std::vector<std::pair<void*, std::vector< ::Character*> > > g_wsDupNow;   // the latest walk's templates with two or more living bodies (kDupBodiesMax + 1 = more than kDupBodiesMax)
unsigned g_wsDupWalk = 0;
DWORD g_wsDupRemovedAt = 0;
bool g_wsDupRemovedOnce = false;
long long g_dupFound = 0, g_dupRemoved = 0, g_dupProtectedKept = 0, g_dupCrossGame = 0, g_dupWaiting = 0, g_dupHandover = 0, g_dupFault = 0;
long long g_dupNoHook = 0, g_dupTooMany = 0, g_dupNoMakeRefused = 0;
int g_dupLogged = 0;
std::set<std::pair<void*, int> > g_wsDupSaid;   // (template, result) refusals already logged
std::set<void*> g_wsDupTableGone;               // templates whose table-named body this game removed (until world teardown)
std::vector<void*> g_wsDupNoMake;               // sorted; guarded by g_wsSetCs: g_wsDupTableGone carried alive here
/* a refused walk cannot say who still carries a template: the no-make set is emptied rather than kept stale */
static void WsDupNoMakeClear()
{
    if (::InterlockedCompareExchange(&g_wsSetCsReady, 0, 0) == 0) return;
    ::EnterCriticalSection(&g_wsSetCs); g_wsDupNoMake.clear(); ::LeaveCriticalSection(&g_wsSetCs);
}
void WsDupKeyOf(::Character* c, WsDupKey* k);   // below, beside the duplicate check's reads
void WsDupCollect(const std::map<void*, std::vector< ::Character*> >& living, DWORD now)
{
    ++g_wsDupWalk;
    g_wsDupNow.clear();
    std::map<void*, WsDupPend> next;
    for (std::map<void*, std::vector< ::Character*> >::const_iterator it = living.begin(); it != living.end(); ++it)
    {
        if (it->second.size() < 2) continue;
        if ((int)next.size() >= kDupPendMax) { ++g_dupFault; continue; }
        WsDupPend p;
        for (size_t i = 0; i < it->second.size(); ++i) { WsDupKey k; WsDupKeyOf(it->second[i], &k); p.keys.push_back(k); }
        std::sort(p.keys.begin(), p.keys.end());
        p.walk = g_wsDupWalk; p.firstMs = now;
        const std::map<void*, WsDupPend>::const_iterator old = g_wsDupPend.find(it->first);
        if (old == g_wsDupPend.end()) ++g_dupFound;
        else if (old->second.walk + 1 == g_wsDupWalk && old->second.keys == p.keys) p.firstMs = old->second.firstMs;
        next[it->first] = p;
        g_wsDupNow.push_back(std::make_pair(it->first, it->second));
    }
    g_wsDupPend.swap(next);
    std::vector<void*> noMake;   /* filled in g_wsDupTableGone's order, which is pointer order: sorted */
    for (std::set<void*>::const_iterator g = g_wsDupTableGone.begin(); g != g_wsDupTableGone.end(); ++g)
    {
        const std::map<void*, std::vector< ::Character*> >::const_iterator lv = living.find(*g);
        if (lv != living.end() && !lv->second.empty()) noMake.push_back(*g);
    }
    ::EnterCriticalSection(&g_wsSetCs);
    g_wsDupNoMake.swap(noMake);
    ::LeaveCriticalSection(&g_wsSetCs);
}
// MAIN THREAD. One walk of the engine's live character list answers, for each record the shadow names, whether a character
// here carries it (1) and whether a living character this game owns does (2); each shadow row is then put in the sets by
// uniqslot.h ElsewhereMember and DeadSetMember. An unusable world or an implausible list keeps the sets as they were (counted) and is
// tried again on the next frame while a rebuild is owed, else a second later. The same walk collects every named template's
// living bodies for the duplicate check (WsDupCollect). Returns 1 when the walk ran.
int WsSetsRebuild()
{
    if (::InterlockedCompareExchange(&g_wsSetCsReady, 0, 0) == 0) return 0;
    const DWORD now = ::GetTickCount();   /* VS2010-era headers have no GetTickCount64; the unsigned difference is wrap-safe */
    g_wsDupNow.clear();   /* a walk's body pointers are used only in that walk's tick */
    if (!g_wsSetsDirty && (DWORD)(now - g_wsSetsAt) < kSetsRefreshMs) return 0;
    g_wsSetsAt = now;
    std::map<void*, int> carried;
    std::map<void*, std::vector< ::Character*> > living;   // named templates' living bodies
    const bool world = WsPlaus(coop::GameWorldPtr());
    if (!g_wsShadow.empty() && !world) { ++g_setWalkRefused; WsDupNoMakeClear(); return 0; }
    if (world)
    {
        const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
        if (all.size() > 20000) { if (!g_wsShadow.empty()) { ++g_setWalkRefused; WsDupNoMakeClear(); return 0; } }
        else for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
        {
            ::Character* c = *it;
            if (!WsPlaus(c)) continue;
            void* const gd = WsCharGameData(c);
            if (gd == 0) continue;
            const bool inShadow = g_wsShadow.find(gd) != g_wsShadow.end();
            if (!inShadow && WsMemoGet(gd) != 1) continue;
            const int dead = WsCharDeadPod(c);
            if (dead == 0) { std::vector< ::Character*>& lv = living[gd]; if (lv.size() <= (size_t)kDupBodiesMax) lv.push_back(c); }   /* one past the cap marks "too many" */
            if (!inShadow) continue;
            int& v = carried[gd];
            if (v < 1) v = 1;
            if (v == 2 || dead != 0) continue;
            const unsigned int uid = FindSpawnedUid(c);
            if (uid != 0 && net::IsUidMine(uid)) v = 2;
        }
        WsDupCollect(living, now);
    }
    const int snap = (StoreSnapshotApplied() != 0) ? 1 : 0;
    std::vector<void*> dead, elsewhere;   // filled in the shadow's key order, which is pointer order: sorted
    for (std::map<void*, WsShadow>::const_iterator it = g_wsShadow.begin(); it != g_wsShadow.end(); ++it)
    {
        const std::map<void*, int>::const_iterator c = carried.find(it->first);
        const int carry = (c != carried.end()) ? c->second : 0;
        int confirmed = it->second.remote != 0 ? 1 : 0;   /* received from another game, or this game published DEAD as the owner */
        if (confirmed == 0 && it->second.state == 0 && it->second.sid[0] != 0)
        {
            const std::map<std::string, WsLastSend>::const_iterator ls = g_lastSend.find(std::string(it->second.sid));
            if (ls != g_lastSend.end() && ls->second.state == 0) confirmed = 1;
        }
        if (swuniq::DeadSetMember(it->second.state, confirmed, g_wsBackGd.count(it->first) != 0 ? 1 : 0, carry == 2 ? 1 : 0) == 1) dead.push_back(it->first);
        if (swuniq::ElsewhereMember(snap, it->second.state, it->second.remote, carry != 0 ? 1 : 0) == 1) elsewhere.push_back(it->first);
    }
    ::EnterCriticalSection(&g_wsSetCs);
    g_wsDead.swap(dead);
    g_wsElsewhere.swap(elsewhere);
    ::LeaveCriticalSection(&g_wsSetCs);
    g_wsSetsDirty = false;
    ++g_setRebuilds;
    return 1;
}

// MAIN THREAD: what WE just made the map say is not news to send back. Recording it here is what stops a received
// apply from coming back out of the drain as a change this game discovered. Both refusals below are already counted
// elsewhere: `gd` is validated by whatever resolved it, and a negative state was booked in stateFault.
// E20: `ownedHere` is passed as 1 (ours), 0 (a ghost) or -1 meaning "this call established nothing about ownership",
// in which case the row keeps whatever it already knew rather than being told it is not ours.
// `remote` is 1 when the state noted is one received from another game and applied here (the elsewhere set is made only
// from such rows), 0 when it is this game's own (a refusal, a re-assert, a bring-back made here, a test write).
void WsShadowNote(void* gd, int state, int playerInvolved, const std::string& sid, int ownedHere, int remote)
{
    if (!WsPlaus(gd) || state < 0) return;
    // E24.5c (review-p5o MEDIUM-6): THE REGISTER IS FED FROM APPLIES AS WELL AS SENDS. The register is the list of
    // records this game knows to be non-ALIVE, and it is the ONLY thing the respawn detour has to ring - it names no
    // unique in its arguments. A state this game APPLIED from the notebook never entered it, so: A imprisons U; B
    // applies IMPRISONED for a U it has not got loaded; B's platoon respawns and the engine erases U's entry; B's
    // detour has nothing for U, rings nothing, and the IMPRISONED -> ALIVE transition is never published. Both games
    // keep IMPRISONED forever. One bounded scan of a 64-entry array on a rare path closes it.
    WsNoteReported(gd, state);
    g_wsSetsDirty = true;   // the two sets are copied from the shadow
    std::map<void*, WsShadow>::iterator it = g_wsShadow.find(gd);
    WsShadow ne; ne.state = state; ne.playerInvolved = playerInvolved; ne.remote = (remote != 0) ? 1 : 0;
    ne.ownedHere = (ownedHere >= 0) ? ownedHere : (it != g_wsShadow.end() ? it->second.ownedHere : 0);
    std::memset(ne.sid, 0, 64);
    const size_t cn = sid.size() < (size_t)63 ? sid.size() : (size_t)63;
    if (cn != 0) std::memcpy(ne.sid, sid.c_str(), cn);
    if (it != g_wsShadow.end()) it->second = ne; else g_wsShadow.insert(std::make_pair(gd, ne));
}

// ---- the five detours ----
// E19, THE RULE FOR ALL FIVE: the `Character::isUnique` gate, the original, then ring the record. Nothing here reads
// the shared state map, calls getState/wasPlayerInvolved, or touches the global at 0x212DAF0 - the drain does all of
// that on the main thread. The gate stays because it is what keeps the ring (and therefore the drain's map reads) to
// named characters only; it reads the character's OWN game-data property map, never the shared one.
void detour_declareDead(void* self)
{
    const unsigned long long ret = (unsigned long long)(uintptr_t)_ReturnAddress();   /* FIRST: the engine caller (review-par6 #1) */
    // par6 (parity P6): a copy's own death is refused unless its owner's latest STATE says dead (medical.cpp). Refused: the
    // character did not die, so there is no world-state change to ring either, and the call is counted apart (review-par6
    // #9). A refused one-shot event kill is sent to the owner as a death request (#1); the notebook's twin kill passes (#2).
    if (!coop::CopyDeathAllowed(self, ret, WsWorldRecordKill())) { ::InterlockedIncrement64(&g_seenDeclareDeadRefused); return; }
    ::InterlockedIncrement64(&g_seenDeclareDead);
    WsNoteThread(&g_thrDeadMain, &g_thrDeadOff);
    // The original's own gate is the same "unique" bool property, read inline before it writes the map
    // (decomp_7a5660:89-93 `getBool(this->data + 0xF8, "unique")`); isUnique 0x505ED0 is that exact test as a call.
    // The record is read BEFORE the original (the gate reads it and hands it back): the death path detaches the
    // character from its squad and ragdolls it, and this is a POD read of the character, not of the map.
    void* gd = 0;
    const int unique = WsPlaus(self) ? WsUniqueGate(self, &gd) : 0;
    orig_declareDead(self);
    coop::ResurrectNoteDeath(self, ret, unique != 0 ? 1 : 0);   /* T-556: this game's own dead are snapshotted on the main thread */
    if (unique == 0) return;
    if (gd != 0 && !WsApplying()) WsQueue(gd);
}
void detour_uniqueStateUpdate(void* self)
{
    ::InterlockedIncrement64(&g_seenStateUpdate);
    WsNoteThread(&g_thrUpdateMain, &g_thrUpdateOff);
    // decomp_5ce9e0:16-18 - the original's literal first two lines. THE hot path: this runs for every character in
    // the world on a worker thread, so below the gate it must cost a pointer read and a ring push, and it does.
    void* gd = 0;
    if (!WsPlaus(self) || WsUniqueGate(self, &gd) == 0) { orig_uniqueStateUpdate(self); return; }
    orig_uniqueStateUpdate(self);
    if (gd != 0 && !WsApplying()) WsQueue(gd);
}
void detour_setImprisoned(void* map, void* gd, char imprisoned, char playerInvolved)
{
    ::InterlockedIncrement64(&g_seenSetter);
    WsNoteThread(&g_thrSetterMain, &g_thrSetterOff);
    // No gate here and none is possible: this one is handed a GameData*, not a Character*, and isUnique needs the
    // character. It is also not the hot path - it fires on a prison toggle or a dialogue outcome, not on every tick.
    // `imprisoned` and `playerInvolved` are this call's INPUTS and could be rung as they stand, but they are not:
    // the drain reads what the map says AFTER the original ran, which is the authoritative answer for every writer
    // alike, and one shape for all five detours is one shape to get right.
    orig_setImprisoned(map, gd, imprisoned, playerInvolved);
    if (WsPlaus(gd) && !WsApplying()) WsQueue(gd);
}
void detour_checkUniques(void* platoon)
{
    ::InterlockedIncrement64(&g_seenUnload);
    WsNoteThread(&g_thrUnloadMain, &g_thrUnloadOff);
    // This writer names no unique in its arguments - it walks the platoon's own character array (decomp_9a7250:33-36:
    // count at +0x58, Character* array at +0x60) and writes one entry per unique it finds. So a bounded SNAPSHOT of
    // that array is taken on the stack, with no allocation, and every named member in it is rung. The snapshot is a
    // POD read of the PLATOON, not of the state map.
    void* snap[64]; int n = 0;
    unsigned count = 0; void** arr = 0;
    if (WsPlaus(platoon) && WsPlatoonArrayPod(platoon, &count, &arr) != 0 && arr != 0 && count <= 4096)
    {
        for (unsigned i = 0; i < count; ++i)
        {
            if (n >= 64) { ::InterlockedIncrement64(&g_unloadOverflow); break; }
            void* ch = 0;
            if (WsMemberPod(arr, i, &ch) == 0) break;
            if (!WsPlaus(ch)) continue;
            void* gd = 0;
            if (WsUniqueGate(ch, &gd) == 0) continue;   // the original gates each member the same way (decomp_9a7250:37)
            if (gd == 0) continue;
            snap[n] = gd; ++n;
        }
    }
    orig_checkUniques(platoon);
    if (!WsApplying()) for (int k = 0; k < n; ++k) WsQueue(snap[k]);
}
void detour_clearAllUniqueStates(void* platoon)
{
    ::InterlockedIncrement64(&g_seenClearAll);
    WsNoteThread(&g_thrClearMain, &g_thrClearOff);
    // See WsNoteReported above for why the records rung here come from the register rather than from this platoon's
    // own member lists. The register holds at most 64 game-data records, so this is a bounded walk of a plugin-side
    // array on a rare event (a respawn) - no map, and the erase this call performs is read by the drain afterwards.
    void* snap[kReported]; int n = 0;
    const LONG have = ::InterlockedCompareExchange(&g_reportedN, 0, 0);
    for (LONG i = 0; i < have && i < kReported; ++i)
    {
        void* gd = g_reported[i];
        if (!WsPlaus(gd)) continue;
        snap[n] = gd; ++n;
    }
    orig_clearAllUniqueStates(platoon);
    if (!WsApplying()) for (int k = 0; k < n; ++k) WsQueue(snap[k]);
}

// E24.1 (review-p5o HIGH-1) - EVERY ID THE NOTEBOOK HAS EVER NAMED. Filled from received UNIQUE_STATE messages,
// which is the only evidence this game gets that the notebook holds a row for an id; the relay pushes every row it
// has at the WELCOME, so after that push this set IS the notebook's key list. MAIN THREAD ONLY (the apply and the
// drain). It is NOT world state and is deliberately not cleared at a world teardown: it records what the notebook
// process knows, and that outlives any one world load on this machine.
std::set<std::string> g_wsNotebookSids;
std::set<std::string> g_unknownLogged;   // main thread only: one line per id we cannot resolve
std::set<std::string> g_reassertLogged;  // E20, main thread only: one line per id we re-assert (the counter carries the rate)
std::set<std::string> g_reassertDampLogged;  // E27, main thread only: one line per id whose re-assert was damped

// ---- the record-member maker and the "may make" check ----
// What it reads is under SEH and allocates nothing (C2712: no std::string is made here). The member's saved state is the
// SavedObjectState 0x36D6A0 built on its own stack for this one call, so nothing else writes it while it is read. Its
// record of type 0x39 is the medical record: Character::loadFromSerialise 0x6264C0 hands it to the medical load 0x64F0B0
// (rcx = Character+0x458, rdx = that record; 0x626578-0x626588 on 1.0.65), which reads its bool "dead" into
// MedicalSystem::dead (+0x164). No such record, or no such field, loads alive - the engine's lookup inserts false.
const int kMedicalRecordType = 0x39;
void* WsStateTemplatePod(void* st)    // the member's template (SavedObjectState::baseRecord), 0 when absent or unreadable
{
    if (!WsPlaus(st)) return 0;
    __try { void* gd = ((const SavedObjectState*)st)->baseRecord; return WsPlaus(gd) ? gd : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int WsMemberDeadPod(void* st)         // 1 a dead body, 0 alive, -1 the read faulted
{
    if (!WsPlaus(st)) return -1;
    __try
    {
        const GameHashMap<itemType, GameData*>::type& m = ((const SavedObjectState*)st)->states;
        for (GameHashMap<itemType, GameData*>::type::const_iterator it = m.begin(); it != m.end(); ++it)
        {
            if ((int)it->first != kMedicalRecordType) continue;
            const ::GameData* r = it->second;
            if (r == 0) return 0;
            if (!WsPlaus(r)) return -1;
            const GameHashMap<std::string, bool>::type& b = r->boolFields;
            for (GameHashMap<std::string, bool>::type::const_iterator f = b.begin(); f != b.end(); ++f)
                if (f->first.size() == 4 && std::memcmp(f->first.c_str(), "dead", 4) == 0) return f->second ? 1 : 0;
            return 0;
        }
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// uniqslot.h MemberVerdict. The template is asked first against the dead set (a search under the lock); only a template in that set
// has its "unique" flag read, and the member's own record only when the load is not the group's first time (with a non-zero
// third argument the engine makes the member fresh from its template and never reads the record - decomp_36f570:65-77).
// Skipping means the original is not called: that member is not made, and the group loads without it.
void detour_loadInstance(void* self, void* st, void* a3, void* a4, void* a5, void* a6, void* a7)
{
    WsNoteThread(&g_memberMain, &g_memberOff);
    void* const gd = WsStateTemplatePod(st);
    const int inDead = (gd != 0) ? WsDeadSetHas(gd) : 0;
    if (inDead != 0)
    {
        if (inDead == 1) ::InterlockedIncrement64(&g_memberDeadSet);
        const int firstTime = ((unsigned char)(uintptr_t)a3 != 0) ? 1 : 0;   /* a char argument: its low byte only */
        const int unique = (inDead == 1) ? WsTemplateIsUnique(gd) : -1;
        const int body = (unique == 1 && firstTime == 0) ? WsMemberDeadPod(st) : 0;
        if (swuniq::MemberVerdict(inDead, unique, firstTime, body) == swuniq::kMemberSkip)
        {
            ::InterlockedIncrement64(&g_memberSkipped);
            if (firstTime != 0) ::InterlockedIncrement64(&g_memberFirstTime);
            WsLogQueuePush(g_wsSkipQueue, gd);
            return;
        }
        if (body == 1) ::InterlockedIncrement64(&g_memberBody);
        else if (inDead < 0 || unique < 0 || body < 0) ::InterlockedIncrement64(&g_memberFault);
    }
    orig_loadInstance(self, st, a3, a4, a5, a6, a7);
}
// uniqslot.h ElsewhereMember. A template in the elsewhere set, or in the duplicate check's no-make set, is answered "may not
// make" (0) without asking the original; every other call gets the original's answer. Any thread: a search under the lock, nothing allocated, nothing written to the
// engine's table.
unsigned char detour_mayMake(void* table, void* gd)
{
    WsNoteThread(&g_mayMakeMain, &g_mayMakeOff);
    if (gd != 0 && WsSetHas(g_wsDupNoMake, gd) == 1)
    {
        ::InterlockedIncrement64(&g_dupNoMakeRefused);
        WsLogQueuePush(g_wsRefuseQueue, gd);
        return 0;
    }
    if (gd != 0 && WsElsewhereHas(gd) == 1)
    {
        ::InterlockedIncrement64(&g_mayMakeRefused);
        WsLogQueuePush(g_wsRefuseQueue, gd);
        return 0;
    }
    return orig_mayMake(table, gd);
}
// MAIN THREAD, from WorldStateTick: one line per template, the first 20 of each kind.
void WsLogQueueDrain(WsLogQueue& q, std::set<void*>& logged, int refused)
{
    if (::InterlockedCompareExchange(&g_wsSetCsReady, 0, 0) == 0) return;
    void* got[kLogQueue]; int n = 0;
    ::EnterCriticalSection(&g_wsSetCs);
    for (int i = 0; i < q.n; ++i) got[n++] = q.item[i];
    q.n = 0;
    ::LeaveCriticalSection(&g_wsSetCs);
    for (int i = 0; i < n; ++i)
    {
        if (logged.size() >= (size_t)kUniqLogMax || !logged.insert(got[i]).second) continue;
        char sid[64];
        if (WsSidPod(got[i], sid, 64) == 0) std::strcpy(sid, "?");
        if (refused != 0) DebugLog("[UNIQ] make refused sid=" + std::string(sid) + " (alive in another game)");
        else DebugLog("[UNIQ] record member skipped sid=" + std::string(sid) + " (dead in this world) skipped=" + N(g_memberSkipped));
    }
}

} // namespace

// The template's own bool field "unique", the one Character::isUnique 0x505ED0 reads (decomp_505ed0: *(GameData**)(this+0x40)
// + 0xF8, boolFields); towns2's recipe check (towngen.cpp T2RecipeUnique) and the record-member detour ask it. The table is
// WALKED and nothing is inserted here; a missing key is not unique, the engine's answer (items.cpp ItSquadIsTraderPod reads
// "is trader" the same way). ANY THREAD: the record-member detour calls it off the main thread too, and the engine's own bool
// lookup 0x6C780 inserts a missing key into this same table (createRandomCharacter 0x582C50 asks it for "unique"), so a walk
// here can meet the table while another thread changes it; the fault guard is the backstop for that. No std::string here
// (C2712).
int WsTemplateIsUnique(void* gd)
{
    if (!WsPlaus(gd)) return -1;
    __try
    {
        const GameHashMap<std::string, bool>::type& m = ((::GameData*)gd)->boolFields;
        for (GameHashMap<std::string, bool>::type::const_iterator it = m.begin(); it != m.end(); ++it)
            if (it->first.size() == 6 && std::memcmp(it->first.c_str(), "unique", 6) == 0) return it->second ? 1 : 0;
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// MAIN THREAD. The lookup is 0x591720's own (decomp_591720): the hash of the key pointer, the bucket walk (count +0x20,
// bucket count +0x18, buckets +0x38; node: next +0, hash +8, key +0x10), and on a hit find_or_insert 0x3498F0 - which then
// finds, never inserts - for the entry. 1 and *node when the table has an entry, 0 when it has none (or there is no table),
// -1 on a fault.
int WsEntryPod(void* gd, void** node)
{
    *node = 0;
    void* map = WsMapPod();
    if (map == 0) return 0;
    if (kFindOrInsertRva == 0) return -1;
    __try
    {
        const char* m = (const char*)map;
        const unsigned long long k = (unsigned long long)(uintptr_t)gd;
        unsigned long long h = (k >> 3) + k;
        h = h * 0x200000ULL + ~h;
        h = ((h >> 0x18) ^ h) * 0x109ULL;
        h = ((h >> 0xE) ^ h) * 0x15ULL;
        h = ((h >> 0x1C) ^ h) * 0x80000001ULL;
        const unsigned long long mask = *(const unsigned long long*)(m + 0x18) - 1;
        const unsigned long long at = mask & h;
        if (*(const long long*)(m + 0x20) == 0) return 0;
        const unsigned long long* nd = *(const unsigned long long* const*)(*(const char* const*)(m + 0x38) + at * 8);
        if (nd != 0) nd = (const unsigned long long*)nd[0];
        for (int guard = 0; nd != 0 && guard < 100000; ++guard, nd = (const unsigned long long*)nd[0])
        {
            if (h == nd[1])
            {
                if (nd[2] != k) continue;
                void* key = gd;
                void* e = ((FindOrInsertFn)(g_base + kFindOrInsertRva))(map, &key);
                if (!WsPlaus(e)) return -1;
                *node = e;
                return 1;
            }
            if ((mask & nd[1]) != at) break;
        }
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int WsUniqueSlotPod(void* gd)   // MAIN THREAD: 1 the entry's made slot holds a template, 0 empty or no entry, -1 a fault
{
    void* node = 0;
    const int r = WsEntryPod(gd, &node);
    if (r <= 0) return r;
    __try { return (*(void* const*)((const char*)node + kUsedOff) != 0) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int WsEntryPlayerInvolvedPod(void* gd)   // MAIN THREAD: the entry's +0x34 (1/0); 0 with no entry (the engine's default) or a fault
{
    void* node = 0;
    if (WsEntryPod(gd, &node) <= 0) return 0;
    __try { return (*((const unsigned char*)node + kPlayerOff) != 0) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// ---- the duplicate check's reads and its action (see WsDupCollect). MAIN THREAD. ----
namespace {
int WsDupPosPod(::Character* c, float* x, float* z)   // 1 read, 0 faulted
{
    __try { const Ogre::Vector3 p = c->worldPosition(); *x = p.x; *z = p.z; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// One body's settle key: its uid, its owner's slot (this game's own slot when it is this game's) and the slot of the game that
// runs its area (-1 none fresh, or its position unreadable).
void WsDupKeyOf(::Character* c, WsDupKey* k)
{
    k->uid = FindSpawnedUid(c);
    k->owner = (k->uid == 0) ? -1 : (net::IsUidMine(k->uid) ? coop::MySlotForWire() : net::OwnerSlotOf(k->uid));
    float x = 0.0f, z = 0.0f;
    k->runner = WsDupPosPod(c, &x, &z) != 0 ? coop::AreaHolderSlotTS(coop::SectorOf(x, z)) : -1;
}
// 1 protected by its own fields, 0 not, -1 a read faulted: any player's faction (playerfaction.cpp AnyPlayersFaction: this game's
// own, a stand-in, a recorded stand-in), carried (+0x3D4), in a bed or a cage (+0x2F8 non-zero)
int WsDupProtectedPod(::Character* c)
{
    __try
    {
        ::Faction* f = c->getOwnerFactionDirect();
        if (f != 0 && coop::AnyPlayersFaction(f)) return 1;
        if (*((const unsigned char*)c + 0x3D4) != 0) return 1;
        if (*(const int*)((const char*)c + 0x2F8) != 0) return 1;
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// Does the engine's table entry name this squad? The entry keeps its squad's handle at +0x1C/+0x20 and 0x505D50 compares them
// with the Platoon's handle (Platoon+0x58) fields +0xC/+0x10 (build/decomp_505d50.txt). 1 yes, 0 no or no entry, -1 a fault.
int WsDupTableHoldsPod(void* gd, void* platoon)
{
    if (platoon == 0) return 0;
    void* node = 0;
    const int r = WsEntryPod(gd, &node);
    if (r <= 0) return r;
    __try
    {
        const char* h = (const char*)platoon + 0x58;
        return (*(const int*)((const char*)node + 0x1C) == *(const int*)(h + 0xC) && *(const int*)((const char*)node + 0x20) == *(const int*)(h + 0x10)) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int WsClearSlotPod(void* gd)   // TEST-ONLY (uniqueforce clearslot): the entry's made slot emptied; 1 done, 0 no entry, -1 a fault
{
    void* node = 0;
    const int r = WsEntryPod(gd, &node);
    if (r <= 0) return r;
    __try { *(void**)((char*)node + kUsedOff) = 0; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int WsDupDestroyPod(::Character* c)   // the engine's own (hooked) destroy; 1 called, 0 no world, -1 faulted
{
    ::GameWorld* w = coop::GameWorldPtr();
    if (!WsPlaus(w)) return 0;
    __try { w->destroy((RootObject*)c, false, coopremoval::kDuplicateNamedRemoved); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// One body's inputs to DupDecide. A read that faults sets *faulted (the decision then waits).
void WsDupBodyRead(::Character* c, void* gd, int imprisoned, int mySlot, swuniq::DupBody* b, int* faulted)
{
    WsDupKey k; WsDupKeyOf(c, &k);
    b->uid = k.uid;
    b->mine = (k.uid != 0 && net::IsUidMine(k.uid)) ? 1 : 0;
    b->owner = b->mine != 0 ? mySlot : k.owner;
    b->runner = k.runner;
    float x = 0.0f, z = 0.0f;
    const int posOk = WsDupPosPod(c, &x, &z);
    const int p = WsDupProtectedPod(c);
    SquadView sv;
    const int sq = coop::ReadSquadOf(c, &sv);   /* 1 read, 0 no squad, -1 faulted */
    void* const platoon = (sq == 1) ? sv.platoon : 0;
    b->engineSquad = (platoon != 0 && coop::IsContextPlatoon(platoon) == 0) ? 1 : 0;
    b->tableHolds = WsDupTableHoldsPod(gd, platoon);
    if (posOk == 0 || p < 0 || sq < 0 || b->tableHolds < 0) *faulted = 1;
    b->prot = (p != 0 || imprisoned != 0 || c == coop::GetTarget()) ? 1 : 0;
}
// The living bodies of one template, read now (for `uniqueforce <sid> show`); kDupBodiesMax + 1 = more than kDupBodiesMax
void WsDupBodiesOf(void* gd, std::vector< ::Character*>& out)
{
    if (!WsPlaus(coop::GameWorldPtr())) return;
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) return;
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end() && out.size() <= (size_t)kDupBodiesMax; ++it)
        if (WsPlaus(*it) && WsCharGameData(*it) == gd && WsCharDeadPod(*it) == 0) out.push_back(*it);
}
std::string WsDupSid(void* gd) { char sid[64]; return WsSidPod(gd, sid, 64) > 0 ? std::string(sid) : std::string("?"); }
void WsDupSay(void* gd, int n, int result, const swuniq::DupBody* b, int keep, int keepWhy)
{
    if (g_dupLogged >= kDupLogMax || !g_wsDupSaid.insert(std::make_pair(gd, result)).second) return;
    ++g_dupLogged;
    DebugLog("[UNIQ] duplicate sid=" + WsDupSid(gd) + " bodies=" + N(n) + " keep uid=" + (keep >= 0 ? N((long long)b[keep].uid) : std::string("none"))
             + " (" + swuniq::DupKeepWhyText(keepWhy) + ") remove none (" + swuniq::DupResultText(result) + ")");
}
// MAIN THREAD, from WorldStateTick right after a walk: each settled template is decided; at most one body is removed.
void WsDupTick()
{
    if (g_wsDupNow.empty()) return;
    if (coop::EngineWritesBlocked()) { ++g_dupWaiting; return; }
    const DWORD now = ::GetTickCount();
    const int mySlot = coop::MySlotForWire();
    for (size_t k = 0; k < g_wsDupNow.size(); ++k)
    {
        void* const gd = g_wsDupNow[k].first;
        const std::vector< ::Character*>& bodies = g_wsDupNow[k].second;
        const std::map<void*, WsDupPend>::const_iterator pd = g_wsDupPend.find(gd);
        if (pd == g_wsDupPend.end() || (DWORD)(now - pd->second.firstMs) < kDupSettleMs) continue;
        const int n = (int)bodies.size();
        if (n > kDupBodiesMax) { ++g_dupTooMany; ++g_dupWaiting; continue; }   /* never decided on a cut list */
        swuniq::DupBody b[kDupBodiesMax];
        int rm[kDupBodiesMax];
        const int st = WsStateOf(gd);
        int faulted = (st < 0) ? 1 : 0, unregistered = 0, handover = 0;
        for (int i = 0; i < n; ++i)
        {
            WsDupBodyRead(bodies[i], gd, st == 2 ? 1 : 0, mySlot, &b[i], &faulted);
            if (b[i].uid == 0) unregistered = 1;
            else if (coop::HandoffPendingHas(b[i].uid) || coop::ReleasePendingHas(b[i].uid)) handover = 1;
        }
        if (faulted != 0) { ++g_dupFault; continue; }
        if (unregistered != 0) { ++g_dupWaiting; continue; }
        if (handover != 0) { ++g_dupHandover; continue; }
        int keep = -1, keepWhy = 0;
        const int r = swuniq::DupDecide(b, n, mySlot, swuniq::kProtectedIsKept, &keep, &keepWhy, rm);
        if (r == swuniq::kDupProtectedKept) { ++g_dupProtectedKept; WsDupSay(gd, n, r, b, keep, keepWhy); continue; }
        if (r == swuniq::kDupCrossGame) { ++g_dupCrossGame; WsDupSay(gd, n, r, b, keep, keepWhy); continue; }
        if (r != swuniq::kDupRemove) { ++g_dupWaiting; continue; }
        if (!coop::WorldGenDestroyHooked()) { ++g_dupNoHook; ++g_dupWaiting; continue; }
        if (g_wsDupRemovedOnce && (DWORD)(now - g_wsDupRemovedAt) < kDupRemoveGapMs) return;
        for (int i = 0; i < n; ++i)
        {
            if (rm[i] == 0) continue;
            ::Character* const c = bodies[i];
            if (WsCharDeadPod(c) != 0 || FindSpawnedUid(c) != b[i].uid || !net::IsUidMine(b[i].uid)) { ++g_dupWaiting; return; }
            g_wsDupRemovedAt = now; g_wsDupRemovedOnce = true;
            const std::string sid = WsDupSid(gd);
            const unsigned int keptUid = b[keep].uid, goneUid = b[i].uid;
            const int goneRunner = b[i].runner, goneTable = b[i].tableHolds;
            const int d = WsDupDestroyPod(c);
            g_wsDupNow.clear();   /* the walk's pointers are not used again after an engine destroy */
            if (d != 1) { ++g_dupFault; ErrorLog("[UNIQ] duplicate sid=" + sid + " remove uid=" + N((long long)goneUid) + " FAILED (the engine's destroy " + (d == 0 ? "had no world" : "faulted") + ")"); return; }
            ++g_dupRemoved;
            if (goneTable == 1) g_wsDupTableGone.insert(gd);   /* the no-make set takes it from the next walk */
            if (g_dupLogged < kDupLogMax)
            {
                ++g_dupLogged;
                DebugLog("[UNIQ] duplicate sid=" + sid + " bodies=" + N(n) + " keep uid=" + N((long long)keptUid) + " (" + swuniq::DupKeepWhyText(keepWhy)
                         + ") remove uid=" + N((long long)goneUid) + " (this game's own, not protected, in an area this game runs (slot " + N(goneRunner) + ")"
                         + (goneTable == 1 ? "; the engine's table named its squad - this game's makers now refuse him while he lives here" : "") + ")");
            }
            return;
        }
    }
}
// `uniqueforce <sid> show`: the living bodies with owner, runner and the decision as it would be taken now
std::string WsDupShow(void* gd)
{
    std::vector< ::Character*> bodies;
    WsDupBodiesOf(gd, bodies);
    const int n = (int)bodies.size();
    const int mySlot = coop::MySlotForWire();
    std::string out = " living=" + N(n) + " mySlot=" + N(mySlot) + " destroyHooked=" + N(coop::WorldGenDestroyHooked() ? 1 : 0) + " noMake=" + N(WsSetHas(g_wsDupNoMake, gd));
    if (n > kDupBodiesMax) return out + " dup=waiting (more than " + N(kDupBodiesMax) + " living bodies)";
    swuniq::DupBody b[kDupBodiesMax];
    int rm[kDupBodiesMax];
    const int st = WsStateOf(gd);
    int faulted = (st < 0) ? 1 : 0;
    for (int i = 0; i < n; ++i)
    {
        WsDupBodyRead(bodies[i], gd, st == 2 ? 1 : 0, mySlot, &b[i], &faulted);
        out += std::string(i == 0 ? " bodies=" : ";") + N((long long)b[i].uid) + ":owner=" + N(b[i].owner) + ",runner=" + N(b[i].runner) + ",protected=" + N(b[i].prot)
             + ",table=" + N(b[i].tableHolds) + ",engineSquad=" + N(b[i].engineSquad);
    }
    if (faulted != 0) return out + " dup=waiting (a read faulted)";
    int keep = -1, keepWhy = 0;
    const int r = swuniq::DupDecide(b, n, mySlot, swuniq::kProtectedIsKept, &keep, &keepWhy, rm);
    out += std::string(" dup=") + swuniq::DupResultText(r);
    if (keep >= 0) out += " keep=" + N((long long)b[keep].uid) + " (" + swuniq::DupKeepWhyText(keepWhy) + ")";
    for (int i = 0; i < n; ++i) if (rm[i] != 0) out += " remove=" + N((long long)b[i].uid);
    return out;
}
} // namespace
void* WsTemplateBySid(const char* sid)
{
    if (sid == 0 || sid[0] == 0) return 0;
    const std::string s(sid);
    return WsGetDataPod(&s);
}

void InstallWorldState()
{
    g_base = (uintptr_t)::GetModuleHandleA(0);
    std::memset(g_ring, 0, sizeof(g_ring));
    std::memset(g_reported, 0, sizeof(g_reported));
    std::memset((void*)g_wsPending, 0, sizeof(g_wsPending));   /* E24.2: preload, main thread, before any detour is installed */
    std::memset((void*)g_wsMemoKey, 0, sizeof(g_wsMemoKey));   /* E24.4 */
    std::memset((void*)g_wsMemoAnswer, 0, sizeof(g_wsMemoAnswer));
    // A probe that settles the one thing this file INFERS (ship the probe with the fix): the base game-data container
    // is passed by every engine call site as &DAT_1421330d0, i.e. the object itself lives at that address rather than a
    // pointer to it living there. An in-image first word is the container's vtable and confirms it; a heap-looking
    // value would mean the lookup below is handed the wrong `this` (it would then fault into unknownSid, not crash).
    {
        unsigned long long w0 = 0;
        const int okp = WsReadU64Pod(g_base + kGdContainerRva, &w0);
        const bool inImage = okp != 0 && w0 > g_base && w0 < g_base + 0x10000000ULL;
        DebugLog("[WS] base game-data container +0x21330D0 first word = " + (okp ? Hex(w0) : std::string("unreadable"))
                 + " image=" + Hex((unsigned long long)g_base) + " -> " + (inImage ? "in-image (the container OBJECT is there, as assumed)" : "NOT in-image (the sid lookup will find nothing - watch uniqueState[...unknownSid])"));
    }
    ::InitializeCriticalSection(&g_wsSetCs);   /* before the record-member and may-make detours below can run */
    ::InterlockedExchange(&g_wsSetCsReady, 1);
    coop::HookStatus h1 = coop::AddHook((void*)(g_base + kSetImprisonedRva), (void*)&detour_setImprisoned, (void**)&orig_setImprisoned);
    if (h1 != coop::SUCCESS) ErrorLog("[WS] AddHook the shared setter 0x34ADC0 FAILED - setPrisonMode and dialogue outcomes are NOT carried");
    else DebugLog("[WS] hook installed: UniqueNPCManager setImprisoned 0x34ADC0 (setPrisonMode + Dialogue::_doActions)");
    coop::HookStatus h2 = coop::AddHook((void*)(g_base + kDeclareDeadRva), (void*)&detour_declareDead, (void**)&orig_declareDead);
    if (h2 != coop::SUCCESS) ErrorLog("[WS] AddHook Character::declareDead 0x7A5660 FAILED - a named character's death is NOT carried");
    else DebugLog("[WS] hook installed: Character::declareDead 0x7A5660");
    coop::HookStatus h3 = coop::AddHook((void*)(g_base + kUniqueStateUpdateRva), (void*)&detour_uniqueStateUpdate, (void**)&orig_uniqueStateUpdate);
    if (h3 != coop::SUCCESS) ErrorLog("[WS] AddHook Character::uniqueStateUpdate 0x5CE9E0 FAILED - capture and release are NOT carried");
    else DebugLog("[WS] hook installed: Character::uniqueStateUpdate 0x5CE9E0 (OFF the main thread)");
    coop::HookStatus h4 = coop::AddHook((void*)(g_base + kCheckUniquesRva), (void*)&detour_checkUniques, (void**)&orig_checkUniques);
    if (h4 != coop::SUCCESS) ErrorLog("[WS] AddHook ActivePlatoon::_checkForUniqueCharactersOnUnload 0x9A7250 FAILED - a squad unload's state changes are NOT carried");
    else DebugLog("[WS] hook installed: ActivePlatoon::_checkForUniqueCharactersOnUnload 0x9A7250");
    // review-p5j MEDIUM-1 / persistence-service.md 24: the fifth writer, the one respawn uses. Its signature is
    // established - decomp_4fecd0 is `void FUN_1404fecd0(longlong param_1)`, one argument, `this` in rcx, void return.
    coop::HookStatus h5 = coop::AddHook((void*)(g_base + kClearAllRva), (void*)&detour_clearAllUniqueStates, (void**)&orig_clearAllUniqueStates);
    if (h5 != coop::SUCCESS) ErrorLog("[WS] AddHook ActivePlatoon::clearAllTheUniqueNPCStates 0x4FECD0 FAILED - a respawn's state clearing is NOT carried");
    else DebugLog("[WS] hook installed: ActivePlatoon::clearAllTheUniqueNPCStates 0x4FECD0 (respawn)");
    // The record-member maker. Without it a dead named character can come back through a group made from a record.
    if (kPlatoonLoadInstanceRva == 0) { g_memberHook = 0; ErrorLog("[UNIQ] no address for ActivePlatoon vt+0x38 - a group made from a record may bring back a dead named character"); }
    else
    {
        coop::HookStatus h6 = coop::AddHook((void*)(g_base + kPlatoonLoadInstanceRva), (void*)&detour_loadInstance, (void**)&orig_loadInstance);
        if (h6 != coop::SUCCESS) { g_memberHook = -1; ErrorLog("[UNIQ] AddHook ActivePlatoon vt+0x38 (record member) FAILED - a group made from a record may bring back a dead named character"); }
        else { g_memberHook = 1; DebugLog("[UNIQ] hook installed: ActivePlatoon vt+0x38 (record member) at +" + Hex(kPlatoonLoadInstanceRva)); }
    }
    // The "may make" check. Without it this game's own makers can make a second copy of a named character that lives in
    // another game.
    if (kMayMakeRva == 0) { g_mayMakeHook = 0; ErrorLog("[UNIQ] no address for the may-make check 0x591720 - this game may make a second copy of a named character alive in another game"); }
    else
    {
        coop::HookStatus h7 = coop::AddHook((void*)(g_base + kMayMakeRva), (void*)&detour_mayMake, (void**)&orig_mayMake);
        if (h7 != coop::SUCCESS) { g_mayMakeHook = -1; ErrorLog("[UNIQ] AddHook the may-make check 0x591720 FAILED - this game may make a second copy of a named character alive in another game"); }
        else { g_mayMakeHook = 1; DebugLog("[UNIQ] hook installed: the may-make check at +" + Hex(kMayMakeRva)); }
    }
}

// MAIN THREAD: take every published slot, oldest ticket first. A dropped ticket leaves a hole in the ring, so the
// order comes from each entry's stored ticket rather than from a read index.
//
// E19 - THIS IS THE ONLY PLACE THE SHARED STATE MAP IS READ ON THE SEND SIDE. Per rung record: ONE getState, diffed
// against the shadow. An entry whose state the shadow already holds stops there - one map read, no string read, no
// send. Only a change (or a first sight that is already non-ALIVE) reads playerInvolved, resolves the string id and
// sends. A record can be rung many times between drains and still costs one read per rung entry, because what is
// read is the map's CURRENT value rather than a before/after pair.
long long WorldStateGeneration() { return g_wsStateGen; }
void WorldStateTick()
{
    if (g_base == 0) return;
    if (WsSetsRebuild() == 1) WsDupTick();   // cheap unless the shadow changed or a second has passed; a walk is followed by the duplicate check
    WsLogQueueDrain(g_wsSkipQueue, g_wsSkipLogged, 0);
    WsLogQueueDrain(g_wsRefuseQueue, g_wsRefuseLogged, 1);
    int idx[kRing]; int n = 0;
    for (int i = 0; i < kRing; ++i) if (g_ring[i].ready != 0) idx[n++] = i;
    if (n == 0) return;
    for (int a = 1; a < n; ++a)
    {
        const int v = idx[a]; int b = a - 1;
        while (b >= 0 && (long)(g_ring[idx[b]].seq - g_ring[v].seq) > 0) { idx[b + 1] = idx[b]; --b; }
        idx[b + 1] = v;
    }
    int ownBudget = kOwnRefreshPerDrain;   // E20: the unchanged path's last-known-owner refreshes, bounded per drain
    int firstSightBudget = kFirstSightWalksPerDrain;
    for (int k = 0; k < n; ++k)
    {
        WsEntry& e = g_ring[idx[k]];
        void* const gd = e.gd;
        ::InterlockedExchange(&e.ready, 0);   // free the slot BEFORE the work: a slow link must not stall the writers
        // E24.2: and give the record's pending flag back BEFORE the map is read, not after. Releasing early can only
        // cost a duplicate push - a writer that runs during this drain rings the record again and the next drain
        // reads it again. Releasing late would lose that writer's change outright, because its push would be deduped
        // against an entry already being consumed. The cheap error and the expensive one are not symmetric.
        if (WsPlaus(gd)) WsPendRelease(gd);
        if (!WsPlaus(gd)) { ++g_drainBadGd; continue; }
        const int state = WsStateOf(gd);      // THE map read
        if (state < 0) continue;              // WsStateOf booked it in stateFault
        /* E24.5a (review-p5o MEDIUM-5a): the receive side has always range-checked and the send side never did, so
           the asymmetry was visible in this file. One comparison per rung entry turns "the map read went wrong"
           from an unattributable number on the wire into a counter. */
        if (state > 2) { ++g_stateRange; continue; }
        std::map<void*, WsShadow>::iterator it = g_wsShadow.find(gd);
        const bool known = (it != g_wsShadow.end());
        if (known && it->second.state == state)
        {
            // E20: the last-known-owner flag is caught up HERE, on the path that costs nothing else, because the
            // character it needs is only askable while it is still loaded. An alive, free, non-player-faction unique
            // never changes state at all (decomp_5ce9e0:81-83), so its record would otherwise reach its squad's
            // unload with nothing recorded about who owned it - and that unload's change is precisely the one this
            // game must still be able to publish. Budgeted, and the misses are counted rather than silent.
            if (ownBudget > 0) { --ownBudget; it->second.ownedHere = WsOwnedFlag(gd, it->second.ownedHere); }
            else ++g_ownRefreshSkipped;
            ++g_shadowUnchanged; continue;
        }
        if (!known && state == 1)
        {
            // uniqslot.h FirstSightAlive: an ALIVE first sight is published when the world server's opening data for this world has
            // been applied here and has named no state for this id, and the living character carrying it here is this
            // game's own - so a game that has never met this character learns that it lives here and does not make a
            // second one. Before the opening data is applied the question cannot be answered: no shadow row, asked again on
            // the next pass (one map read, as the unchanged path). Otherwise the baseline is learned silently.
            const int snap = (StoreSnapshotApplied() != 0) ? 1 : 0;
            if (snap != 0 && firstSightBudget <= 0) { ++g_firstSightAliveDeferred; continue; }   // asked again on a later pass
            if (snap != 0) --firstSightBudget;
            ::Character* carrier = 0;
            int own = kOwnNone, inFeed = 0, carrierAlive = -1;
            char fsid[64]; std::memset(fsid, 0, 64);
            if (snap != 0)
            {
                own = WsOwnershipOf(gd, &carrier);
                if (carrier != 0) { const int d = WsCharDeadPod(carrier); carrierAlive = (d == 0) ? 1 : (d == 1 ? 0 : -1); }
                if (own == kOwnMine)
                {
                    if (WsSidPod(gd, fsid, 64) == 0) { ::InterlockedIncrement64(&g_sidFault); continue; }   // no row: retried on the next pass
                    inFeed = (g_wsNotebookSids.find(std::string(fsid)) != g_wsNotebookSids.end()) ? 1 : 0;
                }
            }
            const int ownKind = own == kOwnMine ? swuniq::kFsOwnMine : (own == kOwnMirror || own == kOwnTwin) ? swuniq::kFsOwnOther
                              : own == kOwnUnknown ? swuniq::kFsOwnUnknown : swuniq::kFsOwnNone;
            const int fs = swuniq::FirstSightAlive(snap, inFeed, ownKind, carrierAlive);
            if (fs == swuniq::kFirstSightDefer) { ++g_firstSightAliveDeferred; continue; }
            const int fpi = (fs == swuniq::kFirstSightPublish) ? WsEntryPlayerInvolvedPod(gd) : 0;   /* what the engine's entry holds */
            WsShadow base; base.state = 1; base.playerInvolved = fpi; base.ownedHere = WsOwnedFlagOf(own, 0); base.remote = 0;
            std::memset(base.sid, 0, 64); std::memcpy(base.sid, fsid, 63);
            g_wsShadow.insert(std::make_pair(gd, base));
            if (fs != swuniq::kFirstSightPublish) { ++g_firstSightAliveLearned; ++g_shadowUnchanged; continue; }
            if (StoreSendUniqueState(std::string(fsid), 1, fpi))
            {
                ++g_firstSightAliveSent; WsNoteSent(std::string(fsid), 1, false);
                if (g_firstSightAliveLogged < kUniqLogMax) { ++g_firstSightAliveLogged; DebugLog("[UNIQ] published alive at first sight sid=" + std::string(fsid) + " (this game's own; the world server's feed holds nothing for it)"); }
            }
            else ++g_firstSightAliveSendFailed;
            continue;
        }
        if (known) ++g_wsStateGen;   /* a known record whose state changed (a first sight is this save's own history, not news) */
        const int pi = WsPlayerInvolvedOf(gd);
        char sid[64];
        if (known && it->second.sid[0] != 0) { std::memcpy(sid, it->second.sid, 64); sid[63] = 0; }
        // The shadow is NOT updated when the id cannot be read: the next rung entry for this record retries, rather
        // than the record going quiet with the shadow claiming it was published.
        else if (WsSidPod(gd, sid, 64) == 0) { ::InterlockedIncrement64(&g_sidFault); continue; }

        // ---- E24.1, FIRST SIGHT (review-p5o HIGH-1) ----
        // A record this plugin has never seen before, already non-ALIVE, is NOT news: it is this save's own stored
        // history, read for the first time because the character finally loaded. Publishing it republished this
        // game's whole local past at every world load, with no test of ownership or recency - and the notebook's
        // DEAD-is-terminal rule (store_main.cpp OnUniqueState) then makes a stale local DEAD permanent, killing a
        // character the other game has alive and walking around. That is a world event manufactured out of a file.
        //
        // A first sight is published only when the notebook has been asked and does not know: the opening push must
        // have finished, so "the notebook holds nothing for this id" is answerable at all, AND no UNIQUE_STATE for
        // this id can ever have arrived. Otherwise the notebook's own answer is already in the table - the push
        // applied it - and all this game has to do is learn the record silently.
        if (!known)
        {
            // E32 (verify-p5t HIGH-2) - "NOT YET ASKED" IS NOT "ASKED AND ANSWERED", and only the second one may be
            // written into the shadow. Inserting the row makes the record `known`, and the next drain then returns
            // at the unchanged test above; nothing else in this file re-opens the question (there is no handler on
            // the push's 1->2 edge, nothing rings a record at a zone or world load, and the 5-s re-assert lives
            // inside the apply, which cannot run for a record the notebook holds nothing for). So a record first
            // seen during the opening push was silenced FOR GOOD unless its state changed again.
            /* T-313 (store protocol 63): the unique states now arrive in the record feed's first page, after the WELCOME's push is
               over - so "asked and answered" is the feed's snapshot for THIS world being applied, not the WELCOME push's end. */
            if (StoreSnapshotApplied() == 0)
            {
                WsNoteReported(gd, state);   /* it WAS observed non-ALIVE, and a respawn's clear is diffed against that observation whether or not the notebook has answered yet */
                ++g_firstSightDeferred;
                continue;                     /* no shadow row: the record stays a first sight and is re-asked on the next pass */
            }
            const int inNotebook = (g_wsNotebookSids.find(std::string(sid)) != g_wsNotebookSids.end()) ? 1 : 0;
            if (inNotebook != 0)
            {
                WsShadow fs; fs.state = state; fs.playerInvolved = pi; fs.ownedHere = WsOwnedFlag(gd, 0); fs.remote = 0;
                std::memset(fs.sid, 0, 64); std::memcpy(fs.sid, sid, 63); fs.sid[63] = 0;
                g_wsShadow.insert(std::make_pair(gd, fs));
                g_wsSetsDirty = true;        /* the two sets are copied from the shadow */
                WsNoteReported(gd, state);   /* observed non-ALIVE, whether or not it was published - a respawn's clear is diffed against this */
                ++g_firstSightSuppressed;
                continue;
            }
        }

        // ---- E20, THE SEND GATE ----
        // A change is real (the shadow says so) but it is not necessarily OURS TO PUBLISH. A ghost of the other
        // game's character carries the same record, runs the same periodic check from its own simulation and writes
        // the same table, so both games would report - and the notebook would keep whichever arrived last. Only the
        // game that owns the character may speak for it.
        // E32 (verify-p5u HIGH-1) - THE FLAG COMES FROM THE SHARED CLASSIFIER, and there is no second copy of the
        // rule written out here. This gate used to test kOwnMine and kOwnMirror by hand and let everything else
        // fall into one `else`, so kOwnTwin - a class added later - landed in the branch whose own comment says
        // "nothing here carries this record any more", which is false of a twin by definition (WsOwnershipOf only
        // returns kOwnTwin after it has FOUND a body). Two consequences, both real: the twin's ownedHere was never
        // demoted, so a stale 1 left over from a period of real ownership let a twin PUBLISH; and the two skip
        // counters swapped populations, so `sendSkippedUnknownOwner` counted loaded twins. WsOwnedFlagOf is the
        // one place the classification lives (it is what the apply and the other three drain paths already use),
        // so a twin now lands with a ghost - flag 0, silenced, and counted as the not-ours skip that it is.
        ::Character* carrier = 0;
        const int own = WsOwnershipOf(gd, &carrier);   // the verdict, and (T-556) the character it was read from
        int ownedNow = WsOwnedFlagOf(own, (known ? it->second.ownedHere : 0));
        int maySend = 0;
        if (own == kOwnMine) maySend = 1;
        else if (own == kOwnMirror || own == kOwnTwin) ++g_sendSkippedNotOwned;
        else
        {
            // kOwnNone: nothing here carries this record any more - a squad unload or a respawn clear wrote the
            // table for a character that has since gone, so there is nobody left to ask. The shadow's last-known
            // owner flag is all there is, and "never established" means DO NOT PUBLISH.
            //
            // P6c (p5z MEDIUM-4) - kOwnUnknown NEVER PUBLISHES, AND THE WIDENING IS STATED HONESTLY.
            // P5z gave a uid-0 body its own class so the APPLY would stop demoting the shadow's ownedHere to 0 on
            // the commonest path - WsOwnedFlagOf returns `prev` for kOwnUnknown, which keeps the flag, and that
            // half is right and is kept exactly as it is. But the same change also moved uid 0 out of the
            // kOwnMirror branch, where it was unsendable BY CONSTRUCTION, into this else - where a last-known flag
            // of 1 made it sendable. That was not described anywhere in the commit, and it is a change to the one
            // gate whose whole job is "only the game that owns the character may speak for it".
            // A uid-0 body is a body this game cannot name. Keeping its record's flag is a statement about what we
            // used to know; publishing on it is a statement about the world, made on an identity we could not read
            // at the moment we made it. The first is worth keeping, the second is not, so the send is refused and
            // the ones that WOULD have gone out are counted rather than silently admitted. If that count stays 0
            // across runs, the widening never mattered; if it climbs, it is the population to argue about.
            if (own == kOwnUnknown) { if (ownedNow == 1) ++g_sendSkippedUnknownUid; else ++g_sendSkippedUnknownOwner; }
            else if (ownedNow == 1) maySend = 1;
            else ++g_sendSkippedUnknownOwner;
        }

        WsShadow ne; ne.state = state; ne.playerInvolved = pi; ne.ownedHere = ownedNow; ne.remote = 0;
        std::memcpy(ne.sid, sid, 64); ne.sid[63] = 0;
        if (known) it->second = ne; else g_wsShadow.insert(std::make_pair(gd, ne));
        g_wsSetsDirty = true;   // the two sets are copied from the shadow
        // E27 / review-p5q MEDIUM-2 - THE DRAIN'S RECONCILIATION IDENTITY, and it is true from here on because
        // the apply's re-assert now has its OWN counters (`reassertSent` / `reassertSendFailed`) instead of adding
        // into `sent` / `sendFailed`. Read straight off the increments rather than assumed: past this line a rung
        // entry books exactly one of `sent` or `sendFailed`, and the two gate refusals just above
        // (`sendSkippedNotOwned`, `sendSkippedUnknownOwner`, and since P6c `sendSkippedUnknownUid`) are the only
        // other ways to arrive at it, so
        //     shadowChanged == sent + sendFailed + sendSkippedNotOwned + sendSkippedUnknownOwner + sendSkippedUnknownUid
        // holds EXACTLY. `sendSkippedUnknownUid` is a TERM of it, not a sub-count of another term: the uid-0 branch
        // above books exactly one of the two skip counters and never falls through to a send. Two things about it are easy to state wrongly, and both were checked against the code.
        // `sendFailed` is a TERM of the identity, not an afterthought: a send the link refused leaves the change
        // unpublished just as surely as a refusal by the gate does. And `firstSightSuppressed` is NOT a term of it:
        // E24.1's first-sight branch `continue`s ABOVE this line, so a suppressed first sight never reaches
        // `shadowChanged` at all. The full account of one drained rung entry is the wider sum
        //     drained == shadowUnchanged + firstSightSuppressed + shadowChanged
        //                + firstSightAliveDeferred + firstSightAliveSent + firstSightAliveSendFailed
        //                + drainBadGd + stateFault + stateRange + sidFault
        // of which only the last four are faults.
        ++g_shadowChanged;
        // The register is what a respawn's clear is diffed against, so it records what this game OBSERVED non-ALIVE
        // whether or not the gate let it be published; the drain re-applies the gate when that entry is rung again.
        WsNoteReported(gd, state);   // E19: the register's only feeder, and it now runs on the main thread
        // T-556: a DEAD read while a LIVING character of this game carries the record is not that character's death (the old
        // body's squad unloading after a bring-back) and is not published; `deadLivingCarrier` is one more term of the identity.
        if (maySend != 0 && own == kOwnMine && !swfallen::DeadMayPublish(state, (carrier != 0 && WsCharDeadPod(carrier) == 0) ? 1 : 0))
        {
            maySend = 0; ++g_deadLivingCarrier;
            DebugLog("[WS] '" + std::string(sid) + "' reads DEAD here while a living character of this game carries it - not published (deadLivingCarrier=" + N(g_deadLivingCarrier) + ")");
        }
        if (maySend == 0) continue;
        if (StoreSendUniqueState(std::string(sid), state, pi)) { ++g_sent; WsNoteSent(std::string(sid), state, false); }
        else ++g_sendFailed;
    }
}

// MAIN THREAD, from the store's world-teardown broadcast. Every piece of state this file holds NAMES THE WORLD BEING
// destroyed - the shadow and the register are keyed by game-data records, and the ring holds records queued for a
// drain that will never run for this world. They go together (review-p5l MEDIUM-2's lesson: clearing half of a
// world's state leaves the other half asserting things about a world that is gone).
void WorldStateWorldTeardown()
{
    ++g_teardowns;
    g_wsDupPend.clear(); g_wsDupNow.clear(); g_wsDupSaid.clear(); g_wsDupTableGone.clear();   /* the bodies and templates of the world being destroyed */
    if (::InterlockedCompareExchange(&g_wsSetCsReady, 0, 0) != 0) { ::EnterCriticalSection(&g_wsSetCs); g_wsDupNoMake.clear(); ::LeaveCriticalSection(&g_wsSetCs); }
    for (int i = 0; i < kRing; ++i)
    {
        if (::InterlockedCompareExchange(&g_ring[i].ready, 0, 0) != 0) ++g_teardownRingDropped;
        ::InterlockedExchange(&g_ring[i].ready, 0);
        g_ring[i].gd = 0;
    }
    // E24.2: the pending flags name records of the world being destroyed, and a flag left set would dedupe the next
    // world's first push for whatever lands at that address. E24.4: the memo's keys are that world's game-data
    // records, freed with it, so an answer kept across the teardown could be handed to a different record allocated
    // at the same address. Both are released per slot with the same atomics the ring uses, and carry the same
    // residual it does: a detour running DURING the teardown can re-enter a stale entry.
    for (int i = 0; i < kPending; ++i)
    {
        ::InterlockedExchange(&g_wsPending[i].waiting, 0);
        ::InterlockedExchangePointer(&g_wsPending[i].gd, 0);
    }
    for (int i = 0; i < kMemo; ++i)
    {
        ::InterlockedExchange(&g_wsMemoAnswer[i], 0);
        ::InterlockedExchangePointer(&g_wsMemoKey[i], 0);
    }
    const size_t had = g_wsShadow.size();
    g_wsShadow.clear();
    g_wsSetsDirty = true;   /* the two sets name this world's records - emptied now, not at the next tick */
    if (::InterlockedCompareExchange(&g_wsSetCsReady, 0, 0) != 0)
    {
        std::vector<void*> none, none2;
        ::EnterCriticalSection(&g_wsSetCs);
        g_wsDead.swap(none);
        g_wsElsewhere.swap(none2);
        g_wsSkipQueue.n = 0;
        g_wsRefuseQueue.n = 0;
        ::LeaveCriticalSection(&g_wsSetCs);
    }
    g_wsSkipLogged.clear();
    g_wsRefuseLogged.clear();
    // E27: the last-sent register is keyed by a string id that names a character in THE WORLD BEING DESTROYED, so
    // it goes with the shadow (review-p5l MEDIUM-2's lesson: half a world's state left behind asserts things about
    // a world that is gone - here it would damp the first re-assert of the next world).
    g_lastSend.clear();
    g_reassertDampLogged.clear();
    ::InterlockedExchange(&g_reportedN, 0);
    std::memset(g_reported, 0, sizeof(g_reported));
    g_wsUidGd.clear();   /* E32: the uid -> record links name this world's characters, exactly as the shadow's keys do */
    g_wsBackPending.clear();   /* T-556: the bring-backs waiting for the server's word were made in this world */
    g_wsBackGd.clear();        /* the next world's feed names its brought-back characters again */
    DebugLog("[WS] world teardown: forgot " + N((long long)had) + " shadow rows, the non-ALIVE register and the ring");
}

// MAIN THREAD: a state from the notebook process.
void ApplyRemoteUniqueState(const std::string& sid, int state, int playerInvolved, unsigned int back)
{
    ++g_recv;
    /* P7f (review-p6z C-1): this apply reaches GameDataContainer::getData, a walk of ou->activeCharacters()
       and Character::declareDead - engine memory, from a link that is now up at the TITLE SCREEN. The notebook's
       push is queued in store.cpp until a world exists; this is the detector for any other route in. */
    if (coop::EngineWritesBlocked()) coop::StoreNoteRecordArrivedNoWorld("UNIQUE_STATE");   /* P7p (review-p7f H-1/M-1): the shared predicate, and this is the ARRIVAL - the malformed-state refusal below returns having touched nothing */
    /* E24.5a: a state outside 0..2 is a MALFORMED MESSAGE, not an unknown id, and folding the two together made
       both unreadable. */
    if (state < 0 || state > 2) { ++g_recvBadState; return; }
    if (g_base == 0 || sid.empty()) { ++g_unknownSid; return; }
    /* E24.1: the notebook has named this id - remember that, whatever the apply below decides to do about the
       state. This is the set the drain's first-sight rule asks. It is recorded BEFORE the id is resolved against
       this game's data, because "the notebook knows it" is true even for an id this save has never heard of. */
    g_wsNotebookSids.insert(sid);
    /* T-556: the world server's bring-back mark confirms a bring-back made here - a DEAD for this id may kill again from now on */
    if (state != 0 && back > 0 && g_wsBackPending.erase(sid) != 0)
    {
        ++g_backConfirmed;
        DebugLog("[WS] '" + sid + "' bring-back confirmed by the world server (state " + N(state) + ", brought back " + N((long long)back) + " time(s))");
    }
    /* P7p (review-p7f M-1): THE ACCEPTANCE TOKEN. WsGetDataPod is GameDataContainer::getData - the first engine
       call on this path, and everything above it (the malformed state, the empty sid, the null base) returns
       without touching the engine. */
    if (coop::EngineWritesBlocked()) coop::StoreNoteRecordEngineTouchNoWorld("UNIQUE_STATE");
    void* gd = WsGetDataPod(&sid);
    if (gd == 0)
    {
        ++g_unknownSid;
        if (g_unknownLogged.size() < 64 && g_unknownLogged.insert(sid).second) DebugLog("[WS] unique '" + sid + "' is not in this game's data - state " + N(state) + " ignored (first only per id)");
        return;
    }
    if (back > 0) g_wsBackGd.insert(gd);   /* T-556: a brought-back character - its living carrier answers for it */
    const int before = WsStateOf(gd);
    // E19: the shadow learns what the map says here too, whatever this apply decides below. Without it the first
    // rung entry for this record would be a "first sight" of a non-ALIVE state and would be SENT - the notebook's
    // own message coming back out of the drain as news.
    // E27 / review-p5q MEDIUM-3: this is the COMMONEST apply once two games have converged (every relay HELLO
    // replay of an already-agreed record lands here), and it used to pass -1 - "this call established nothing
    // about ownership" - at the one moment the character is most likely still loaded and the answer one walk away.
    // The file's own header claimed the flag was refreshed on every apply. Now it is.
    if (before == state)
    {   /* a replayed row whose last-known owner is this game (its own publish handed back by the world server) stays this game's own word */
        ++g_unchanged;
        const int own = WsOwnedFlag(gd, -1);
        WsShadowNote(gd, state, playerInvolved, sid, own, own == 1 ? 0 : 1);
        return;
    }

    // E20 - THE APPLY RULES. Everything below turns on ONE question, asked here on the main thread: is this named
    // character loaded here, and if it is, is it OURS or a ghost of the other game's character?
    ::Character* live = 0;
    const int own = WsOwnershipOf(gd, &live);
    const int ownFlag = WsOwnedFlagOf(own, -1);   // -1: nothing learned about ownership, keep the row
    const int isTwin = (own == kOwnTwin) ? 1 : 0;

    // E27 / review-p5q HIGH-4: a TWIN is killed by a received death and a factory GHOST is not. The twin is this
    // save's own copy of the very individual the owner has just killed, so the owner's death is the truth about
    // it; the never-kill rule protects a body the peer's game made, which is a different thing wearing the same
    // word. Counted apart (`appliedDeadKillTwin`) because it is the only kill this game makes for a uid it does
    // not own.
    if (state == 0 && (own == kOwnMine || own == kOwnTwin))
    {
        // HIGH-2. Marking the map DEAD behind a live character's back is the worst of both worlds: every world-state
        // query answers "dead" while the character stands there fightable and recruitable, and on the player's own
        // faction the engine ERASES the entry on its next periodic tick (decomp_5ce9e0:43-56) - which this game then
        // reports back as ALIVE and the notebook's permanent record is overwritten. So the death is applied the way
        // the engine applies a death: through Character::declareDead 0x7A5660, the same call the platoonkill lever
        // makes. The applying-thread guard is set across it so our own declareDead detour books it as our echo and
        // sends nothing back. par6 fold (review-par6 #2): so is the world-record flag - a TWIN is a registered copy, and
        // the copy death gate would refuse this kill until the owner's STATE; the notebook's DEAD is the decision here.
        // T-556: not a character this game brought back that the world server has not confirmed - its DEAD predates the TAKE.
        if (g_wsBackPending.find(sid) != g_wsBackPending.end())
        {
            ++g_deadKillHeldBack;
            WsShadowNote(gd, before, playerInvolved, sid, ownFlag, 0);
            DebugLog("[WS] '" + sid + "' is DEAD in the notebook but was brought back here and the world server has not confirmed it - NOT killed (deadKillHeld=" + N(g_deadKillHeldBack) + ")");
            return;
        }
        ::InterlockedExchange(&g_wsApplyingThread, (LONG)::GetCurrentThreadId());
        ::InterlockedExchange(&g_wsRecordKillThread, (LONG)::GetCurrentThreadId());
        const int killed = StoreDeclareDeadPod(live);
        ::InterlockedExchange(&g_wsRecordKillThread, 0);
        ::InterlockedExchange(&g_wsApplyingThread, 0);
        if (killed == 0)
        {
            /* E24.5b (review-p5o MEDIUM-5b): NOTE THE SHADOW EVEN ON A FAULT. declareDead stores 0 at +0x30 BEFORE
               the faction bookkeeping that can fault (decomp_7a5660:108 against :113-119), so a fault can leave the
               map changed with the shadow unaware - and the next rung entry would then publish the notebook's own
               message straight back out as a local discovery. Re-read and record whatever the map says now. */
            ++g_applyFault;
            { const int nowState = WsStateOf(gd); WsShadowNote(gd, nowState, playerInvolved, sid, ownFlag, nowState == state ? 1 : 0); }
            ErrorLog("[WS] killing loaded unique '" + sid + "' through the engine's declareDead faulted");
            return;
        }
        // A corrective measures the change it produced (project lesson 14).
        const int afterKill = WsStateOf(gd);
        WsShadowNote(gd, afterKill, playerInvolved, sid, ownFlag, afterKill == state ? 1 : 0);   // E19: what we just made the map say is not news
        if (afterKill == 0)
        {
            if (isTwin) ++g_appliedDeadKillTwin; else ++g_appliedDeadKill;
            ++g_applied;
            DebugLog("[WS] '" + sid + "' is DEAD in the notebook and LOADED here twin=" + N(isTwin) + " - killed through the engine's own Character::declareDead");
        }
        else { ++g_killNoEffect; DebugLog("[WS] '" + sid + "' DEAD twin=" + N(isTwin) + ": the engine's declareDead ran but the map still reads " + N(afterKill)); }
        return;
    }
    if (own == kOwnMine)
    {
        // E20 - OURS, LOADED, AND THE MESSAGE IS NOT A DEATH. Our engine is the truth for this character, so the
        // notebook's answer is refused. Refusing alone is not enough: for an alive, free, non-player-faction unique
        // the engine writes the table NEVER (decomp_5ce9e0:81-83), so no shadow diff will ever fire for it and the
        // notebook would stay wrong for the rest of the world's life, including for every future joiner. So the
        // refusal RE-ASSERTS: this game's own reading of the table goes back on the wire now. There is no ping-pong
        // to fear, because the send gate above means only the owner can answer at all.
        const int localNow = WsStateOf(gd);
        if (localNow < 0)
        {
            ++g_applyFault;
            WsShadowNote(gd, WsStateOf(gd), playerInvolved, sid, ownFlag, 0);   /* E24.5b */
            ErrorLog("[WS] re-asserting '" + sid + "' failed: this game's own state read faulted");
            return;
        }
        if (localNow == state) { ++g_unchanged; WsShadowNote(gd, localNow, playerInvolved, sid, ownFlag, 0); return; }
        // T-556: the world server says this character was brought back and the living character carrying it here is ours - the
        // map's DEAD is the old body's, so the server's state is written (an own DEAD body keeps the re-assert below).
        if (swfallen::BringBackWrite(localNow, state, back, (live != 0 && WsCharDeadPod(live) == 0) ? 0 : 1))
        {
            ::InterlockedExchange(&g_wsApplyingThread, (LONG)::GetCurrentThreadId());
            const int wsBeforeB = WsStateOf(gd);
            const int okb = WsBringBackPod(gd, state, playerInvolved);
            if (okb != 0 && wsBeforeB != state) ++g_wsStateGen;
            ::InterlockedExchange(&g_wsApplyingThread, 0);
            const int afterB = WsStateOf(gd);
            WsShadowNote(gd, afterB, playerInvolved, sid, ownFlag, 1);
            if (okb == 0) { ++g_applyFault; ErrorLog("[WS] writing brought-back '" + sid + "' -> " + N(state) + " faulted"); return; }
            ++g_appliedBroughtBack; ++g_applied;
            DebugLog("[WS] '" + sid + "' brought back (" + N((long long)back) + " time(s)) - written " + N(state) + " over this game's DEAD, carried here by a living character of ours (map now " + N(afterB) + ")");
            return;
        }
        const int localPi = WsPlayerInvolvedOf(gd);
        WsShadowNote(gd, localNow, localPi, sid, ownFlag, 0);   // what this game holds is not news for the drain to re-send
        // E27 / review-p5q HIGH-2 - THE DAMPER. During a hand-off both games own this uid for one round trip, and
        // two immediate re-assert reflexes answer each other at link speed with a rewrite of the relay's file per
        // lap. Refusing to re-send the state this game last sent for this id breaks that after ONE lap; the 5-s
        // floor bounds anything the first rule cannot see. The shadow above is written either way - what this
        // game's table says is true whether or not it was allowed on the wire.
        if (!WsReassertAllowed(sid, localNow))
        {
            if (g_reassertDampLogged.size() < 64 && g_reassertDampLogged.insert(sid).second)
                DebugLog("[WS] re-assert of '" + sid + "' -> " + N(localNow) + " DAMPED (the notebook said " + N(state)
                         + "; this game last sent that value, or re-asserted this id under " + N((long long)kReassertMinSec) + "s ago) - first only per id");
            return;
        }
        ++g_reassertedLocal;
        if (StoreSendUniqueState(sid, localNow, localPi)) { ++g_reassertSent; WsNoteSent(sid, localNow, true); }
        else ++g_reassertSendFailed;
        if (g_reassertLogged.size() < 64 && g_reassertLogged.insert(sid).second)
            DebugLog("[WS] re-asserted " + sid + " -> " + N(localNow) + " (owned here; the notebook said " + N(state) + ")");
        return;
    }
    // E20 - A GHOST, or nothing loaded here at all. Both write the table for ANY state, DEAD included, and neither
    // calls declareDead. Killing a ghost would be the ONLY thing that killed it: the owner's own character stream
    // carries a `dead` flag that the receiver reads and throws away (net/session.cpp:494-495), so a ghost dies only
    // when the owner's replicated wounds kill it or the owner's despawn removes it. A kill here would put a corpse on
    // this game for a character the other game has walking around, on the strength of a table entry.
    // E27: a TWIN reaches here only for a NON-death state, and is then treated exactly as a factory ghost - the
    // table is written and the body is left alone, because its owner is simulating that individual.
    // E32: kOwnUnknown is counted with the bodies, not with the empties. This line separates "a body is loaded here
    // and we wrote its table entry" from "nothing is loaded here at all", and a uid-0 answer is the first of those -
    // it reached this counter as kOwnMirror before kOwnUnknown existed, and moving it would change what the number
    // means between two builds for no reason anybody reading the report could see.
    if (own == kOwnMirror || own == kOwnTwin || own == kOwnUnknown) ++g_appliedMirrorMap;
    else if (state == 0) ++g_appliedDeadMap; else ++g_appliedMap;

    /* T-556: a character the world server says was brought back is written past a stored DEAD (the setter alone would refuse) */
    const bool backWrite = swfallen::BringBackWrite(before, state, back, 0);
    ::InterlockedExchange(&g_wsApplyingThread, (LONG)::GetCurrentThreadId());
    const int wsBefore = WsStateOf(gd);
    const int ok = backWrite ? WsBringBackPod(gd, state, playerInvolved) : WsApplyPod(gd, state, playerInvolved);
    if (ok != 0 && wsBefore != state) ++g_wsStateGen;
    ::InterlockedExchange(&g_wsApplyingThread, 0);
    if (ok == 0)
    {
        /* E24.5b: the setter can fault after it has written, so the same rule as the kill path above. */
        ++g_applyFault;
        { const int nowState = WsStateOf(gd); WsShadowNote(gd, nowState, playerInvolved, sid, ownFlag, nowState == state ? 1 : 0); }
        ErrorLog("[WS] applying '" + sid + "' -> " + N(state) + " faulted");
        return;
    }
    // A corrective measures the change it produced (project lesson 14): the map is re-read, and an apply the engine
    // refused (its setter never revives a DEAD entry) is booked as noEffect rather than as applied.
    const int after = WsStateOf(gd);
    WsShadowNote(gd, after, playerInvolved, sid, ownFlag, after == state ? 1 : 0);   // E19: what we just made the map say is not news
    if (after == state)
    {
        ++g_applied; if (backWrite) ++g_appliedBroughtBack;
        DebugLog("[WS] applied " + sid + " -> " + N(state) + " twin=" + N(isTwin) + " (from the notebook)" + (backWrite ? " - BROUGHT BACK " + N((long long)back) + " time(s): written over this game's DEAD" : std::string()));
    }
    else { ++g_noEffect; DebugLog("[WS] '" + sid + "' -> " + N(state) + " twin=" + N(isTwin) + " had NO EFFECT (state is " + N(after) + "; the engine's setter never revives a DEAD entry)"); }
}

// E27 / review-p5q HIGH-3. MAIN THREAD, from handoff.cpp's ACK path immediately after net::ReleaseLocalOwner.
// `ownedHere` is what the send gate falls back on when NO loaded character carries the record any more, and it was
// monotone upward - only a later resolve that found a loaded ghost could demote it, and after a hand-off the
// character is still loaded here as a puppet, so that resolve happens only if a rung entry beats the engine's
// ~6-s unload. The hand-off exists because this game is walking away, so that is the race it loses, and losing it
// published for a character the peer now owns. "The character is gone" teaches nothing about who owned it, but
// "this game released it" is a fact about ownership, so it is written straight in.
// E32 (verify-p5u MEDIUM-1) - BY UID FIRST, BECAUSE THE BODY IS USUALLY ALREADY GONE. The caller resolves the
// character with FindSpawned, which returns 0 for a RETIRED row (spawn.cpp:821) - and handoff.cpp only ever offers
// a squad the engine's unload countdown is already running on, so the retired case is the normal one rather than
// the exception. `WsPlaus(0)` is false, so the old first line returned immediately and the flag stayed set on
// exactly the hand-offs this function exists for; the symptom was `released` climbing while `ownedHereCleared`
// lagged. The uid is on the ACK, so it is passed in and the record is found through the uid -> GameData* link
// WsOwnershipOf records every time it resolves one. The Character* path is kept as the fallback for a uid nothing
// has been recorded for (a hand-off before this game ever classified that record).
// T-556 (owner 493) - MAIN THREAD, from resurrect.cpp right after a named character was brought back here.
std::string WorldStateBroughtBack(const std::string& sid)
{
    if (g_base == 0 || sid.empty()) return "not marked (no string id)";
    void* gd = WsGetDataPod(&sid);
    if (gd == 0) return "not marked ('" + sid + "' is not in this game's data)";
    g_wsBackPending.insert(sid);
    g_wsBackGd.insert(gd);
    const int before = WsStateOf(gd);
    int after = before, ok = 1;
    if (before == 0)
    {
        ::InterlockedExchange(&g_wsApplyingThread, (LONG)::GetCurrentThreadId());
        ok = WsBringBackPod(gd, 1, 0);
        ::InterlockedExchange(&g_wsApplyingThread, 0);
        after = WsStateOf(gd);
        if (ok == 0) ++g_applyFault;
    }
    WsShadowNote(gd, after, 0, sid, 1, 0);   /* what this game just made the map say is not news for the drain */
    ++g_broughtBackHere;
    const std::string line = "this game's map read " + N(before) + " -> " + N(after)
        + (before == 0 ? (ok ? " (written ALIVE)" : " (the write FAULTED)") : " (left as it was)")
        + "; a DEAD from the world server does not kill it here until the server confirms the bring-back";
    DebugLog("[WS] '" + sid + "' brought back here: " + line);
    return line;
}

void WorldStateOnOwnershipReleased(unsigned int uid, const void* character)
{
    void* gd = 0;
    if (uid != 0)
    {
        std::map<unsigned int, void*>::const_iterator ui = g_wsUidGd.find(uid);
        if (ui != g_wsUidGd.end()) gd = ui->second;
    }
    if (gd == 0)
    {
        if (!WsPlaus(character)) return;
        gd = WsCharGameData(character);
    }
    if (gd == 0) return;
    std::map<void*, WsShadow>::iterator it = g_wsShadow.find(gd);
    if (it == g_wsShadow.end() || it->second.ownedHere == 0) return;
    it->second.ownedHere = 0;
    ++g_ownedHereCleared;
}

// E32 (verify-p5t MEDIUM-4) - THE NOTEBOOK'S KEY LIST BELONGS TO THE NOTEBOOK, NOT TO THIS PROCESS. The set is
// deliberately not cleared at a world teardown (it records what the notebook process knows, which outlives any one
// world load), and that is still right - but it was cleared NOWHERE, so after a reconnect to a notebook whose store
// had been reset the first-sight rule answered "the notebook has already named this id" from the PREVIOUS
// notebook's list, and the record was suppressed permanently. It is cleared on the link-down edge and re-armed at
// each WELCOME, so it always describes the notebook this game is actually talking to; the opening push refills it.
void WorldStateNotebookReset()
{
    const size_t had = g_wsNotebookSids.size();
    g_wsNotebookSids.clear();
    if (had != 0) DebugLog("[WS] notebook id list cleared (" + N((long long)had) + " ids) - the next WELCOME's push says what THIS notebook knows");
}

// TEST-ONLY (`uniqueforce <sid> dead|deadsend|alive|clearslot|show`): this game's own table entry for a named character is written
// the way the apply writes it, and the shadow learns it so nothing is sent; `deadsend` also publishes DEAD to the world server as the
// owner's drain would (the other games receive a death); `clearslot` empties the entry's made slot so this game's makers may make
// the character again (a second living body, for the duplicate check); `show` reads the entry, the slot, the two sets and the
// living bodies with each one's owner, its area's runner and the duplicate decision.
std::string WorldStateForceLocal(const std::string& sid, const std::string& what)
{
    if (g_base == 0 || sid.empty()) return "error uniqueforce: no world or no id";
    void* gd = WsGetDataPod(&sid);
    if (gd == 0) return "error uniqueforce: '" + sid + "' is not in this game's data";
    bool sent = false;
    if (what == "dead" || what == "deadsend" || what == "alive")
    {
        const int want = (what == "alive") ? 1 : 0;
        ::InterlockedExchange(&g_wsApplyingThread, (LONG)::GetCurrentThreadId());
        const int ok = (want == 0) ? WsApplyPod(gd, 0, 0) : WsBringBackPod(gd, 1, 0);
        ::InterlockedExchange(&g_wsApplyingThread, 0);
        if (ok == 0) return "error uniqueforce: the write for '" + sid + "' faulted";
        WsShadowNote(gd, WsStateOf(gd), 0, sid, -1, 0);
        if (what == "deadsend")
        {
            if (!StoreSendUniqueState(sid, 0, 0)) return "error uniqueforce: '" + sid + "' written dead here but the send failed";
            WsNoteSent(sid, 0, false);
            sent = true;
        }
        g_wsSetsDirty = true;
        WsSetsRebuild();
    }
    else if (what == "clearslot")
    {
        const int r = WsClearSlotPod(gd);
        if (r <= 0) return "error uniqueforce: '" + sid + "' " + (r == 0 ? "has no entry in this game's table" : "- the write faulted");
    }
    else if (what != "show") return "error uniqueforce: dead|deadsend|alive|clearslot|show";
    const std::map<void*, WsShadow>::const_iterator it = g_wsShadow.find(gd);
    return "ok uniqueforce " + sid + " " + what + ": map=" + N(WsStateOf(gd)) + " slot=" + N(WsUniqueSlotPod(gd)) + " unique=" + N(WsTemplateIsUnique(gd))
           + " deadSet=" + N(WsDeadSetHas(gd)) + " elsewhere=" + N(WsElsewhereHas(gd)) + " shadow=" + (it != g_wsShadow.end() ? N(it->second.state) : std::string("none"))
           + WsDupShow(gd) + (sent ? " (DEAD sent to the world server)" : " (this game only, nothing sent)");
}

std::string WorldStateReport() { return N(g_sent) + "," + N(g_recv) + "," + N(g_applied) + "," + N(g_unknownSid) + "," + N(g_unchanged); }
std::string WorldStateDetail()
{
    return " uniqueStateWriters[declareDead,stateUpdate,squadUnload,setter,clearAll]=" + N(g_seenDeclareDead) + "," + N(g_seenStateUpdate) + "," + N(g_seenUnload) + "," + N(g_seenSetter) + "," + N(g_seenClearAll)
         + " uniqueStateThreads[setter:m/o,declareDead:m/o,stateUpdate:m/o,unload:m/o]=" + N(g_thrSetterMain) + "/" + N(g_thrSetterOff) + "," + N(g_thrDeadMain) + "/" + N(g_thrDeadOff) + "," + N(g_thrUpdateMain) + "/" + N(g_thrUpdateOff) + "," + N(g_thrUnloadMain) + "/" + N(g_thrUnloadOff)
         + " uniqueStateClearAllThread[m/o]=" + N(g_thrClearMain) + "/" + N(g_thrClearOff)
         + " declareDeadRefusedCopy=" + N(g_seenDeclareDeadRefused)   /* par6 fold (review-par6 #9): refused by the copy death gate, not in declareDead above */
         + " uniqueStateGate[nonUnique,isUniqueFault,memoHit,memoMiss,memoFull]=" + N(g_nonUnique) + "," + N(g_isUniqueFault) + "," + N(g_memoHit) + "," + N(g_memoMiss) + "," + N(g_memoFull)
         + " uniqueStateFirstSight[firstSightSuppressed,firstSightDeferred,notebookSids,welcomePushDone,snapshotApplied]=" + N(g_firstSightSuppressed) + "," + N(g_firstSightDeferred) + "," + N((long long)g_wsNotebookSids.size()) + "," + N((long long)StoreWelcomePushDone()) + "," + N((long long)StoreSnapshotApplied())
         + " uniqueStateRange[sendOutOfRange,recvBadState]=" + N(g_stateRange) + "," + N(g_recvBadState)
         + " shadow[entries,unchanged,changed]=" + N((long long)g_wsShadow.size()) + "," + N(g_shadowUnchanged) + "," + N(g_shadowChanged)
         + " uniqueStateTeardown[worlds,ringDropped]=" + N(g_teardowns) + "," + N(g_teardownRingDropped)
         + " uniqueStateApply[deadKill,deadKillTwin,killNoEffect,deadMap,map,appliedMirrorMap,reassertedLocal,reassertDamped,walkRefused,reportedOverflow]=" + N(g_appliedDeadKill) + "," + N(g_appliedDeadKillTwin) + "," + N(g_killNoEffect) + "," + N(g_appliedDeadMap) + "," + N(g_appliedMap) + "," + N(g_appliedMirrorMap) + "," + N(g_reassertedLocal) + "," + N(g_reassertDamped) + "," + N(g_loadedWalkRefused) + "," + N(g_reportedOverflow)
         + " uniqueStateSend[sendSkippedNotOwned,sendSkippedUnknownOwner,sendSkippedUnknownUid,loadedNoUid,ownRefreshSkipped,ownedHereCleared,reassertSent,reassertSendFailed,lastSendCapped]=" + N(g_sendSkippedNotOwned) + "," + N(g_sendSkippedUnknownOwner) + "," + N(g_sendSkippedUnknownUid) + "," + N(g_loadedNoUid) + "," + N(g_ownRefreshSkipped) + "," + N(g_ownedHereCleared) + "," + N(g_reassertSent) + "," + N(g_reassertSendFailed) + "," + N(g_lastSendCapped)
         + " uniqueStateRing[queued,dropped,ringDeduped,ringSlotShared,badGd,sidFault,stateFault,unloadOverflow,sendFailed,noEffect,applyFault]=" + N(g_queued) + "," + N(g_ringDropped) + "," + N(g_ringDeduped) + "," + N(g_ringSlotShared) + "," + N(g_drainBadGd) + "," + N(g_sidFault) + "," + N(g_stateFault) + "," + N(g_unloadOverflow) + "," + N(g_sendFailed) + "," + N(g_noEffect) + "," + N(g_applyFault)
         + " uniqueFirstSight[aliveSent,aliveSendFailed,aliveLearned,aliveDeferred]=" + N(g_firstSightAliveSent) + "," + N(g_firstSightAliveSendFailed) + "," + N(g_firstSightAliveLearned) + "," + N(g_firstSightAliveDeferred)
         + " uniqueMayMake[hook,main,off,refused,elsewhereSize,refuseQueueFull]=" + N((long long)g_mayMakeHook) + "," + N(g_mayMakeMain) + "," + N(g_mayMakeOff) + "," + N(g_mayMakeRefused) + "," + N(WsSetCount(&g_wsElsewhere, 0)) + "," + N(WsSetCount(0, &g_wsRefuseQueue))
         + " recordMember[hook,main,off,deadSet,skipped,firstTime,body,fault,deadSetSize,skipQueueFull]=" + N((long long)g_memberHook) + "," + N(g_memberMain) + "," + N(g_memberOff) + "," + N(g_memberDeadSet) + "," + N(g_memberSkipped) + "," + N(g_memberFirstTime) + "," + N(g_memberBody) + "," + N(g_memberFault) + "," + N(WsSetCount(&g_wsDead, 0)) + "," + N(WsSetCount(0, &g_wsSkipQueue))
         + " uniqueSets[rebuilds,walkRefused]=" + N(g_setRebuilds) + "," + N(g_setWalkRefused)
         + " uniqueDup[found,removed,protectedKept,crossGame,waiting,handover,fault]=" + N(g_dupFound) + "," + N(g_dupRemoved) + "," + N(g_dupProtectedKept) + "," + N(g_dupCrossGame) + "," + N(g_dupWaiting) + "," + N(g_dupHandover) + "," + N(g_dupFault)
         + " uniqueDupMore[noHookYet,tooMany,noMakeSize,noMakeRefused]=" + N(g_dupNoHook) + "," + N(g_dupTooMany) + "," + N(WsSetCount(&g_wsDupNoMake, 0)) + "," + N(g_dupNoMakeRefused)
         + " uniqueStateBack[broughtBackHere,appliedBroughtBack,confirmed,deadKillHeld,deadLivingCarrier,pending]=" + N(g_broughtBackHere) + "," + N(g_appliedBroughtBack) + "," + N(g_backConfirmed) + "," + N(g_deadKillHeldBack) + "," + N(g_deadLivingCarrier) + "," + N((long long)g_wsBackPending.size());
}

} // namespace coop
