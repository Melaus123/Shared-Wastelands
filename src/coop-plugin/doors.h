// doors.h - E45 (P8e / B8): A DOOR'S OPEN/CLOSED STATE IS THE AREA HOLDER'S, SHARED AS STATE.
//
// Engine facts of record: build/read-doors.md (F632).  DoorStuff::state is an int at +0x380
// (CLOSED 0, OPEN 1, OPENING 2, CLOSING 3 - only 0 and 1 are durable); the setter to call is
// DoorStuff::setDoorState 0x298FC0; every path that writes the state passes through the funnel
// DoorStuff::setDoorOpenAmount 0x298CD0; a town gate is a GatewayBuilding whose state lives on its
// child DoorStuff; and DoorStuff::setupPhysicalUT 0x29CC50 FORCES a finished gate to OPEN whenever
// its physics body is created, on every game independently (F633 - the explanation for a gate open
// on one screen and shut on the other).
//
// THE PATTERN IS THE WEATHER ONE (.modding/03-systems/world-states.md, build/read-e40.md): the
// behaviour that opens and closes doors - NPC tasks, physics setup, population - runs independently
// on each game and cannot be made to agree, so the STATE is shared and the cause is not.  The game
// that HOLDS the door's patch of map publishes (decision 40); every other game applies through the
// engine's own setter and lets its own writers through for instant feedback.
//
// P8n - WHAT "CORRECTS THEM ON THE NEXT TICK" BECAME.  It did not survive contact: build/read-door-churn.md
// measured a 12.7-16.6 Hz livelock in which one game's NPC opened a door, this game's applier snapped it
// shut 4 ms later, and - because DoorStuff::openDoor only acts on a door that is already CLOSED - the snap
// re-armed the NPC, 2,652 times on one door.  The rule is now LAST ACTOR WINS, THE AREA HOLDER TIE-BREAKS:
// an engine-originated change on either game is NOT REVERTED BY THE APPLIER - a later holder word or an
// adopted report may supersede it, which is convergent rather than frozen (P8n-b, review-p8n M-3);
// on a non-holder it is SENT to the holder,
// which ADOPTS it, applies it and republishes, so both games converge on the last actor.  Two actors inside
// one round trip resolve to the holder's most recent adopted state.  The decisions are pure and live in
// src/common/doorsync.h, swept offline by src/coop-test/test_main.cpp.
//
// HOW THE HOLDER'S WORD LANDS.  A holder's change to a door that is settled in the other state is PLAYED
// with the engine's own DoorStuff::openDoor / closeDoor - the calls a character's open uses - so the copy
// swings at the engine's speed with the engine's sounds, and the swing's frames are this game's own write
// until it lands (never reported back).  A door nobody watched move - a gate physics setup forced open, a
// first word about a door, a swing the engine refused or one that never landed - is SNAPPED with
// setDoorState.  Once the two games agree about OPEN / CLOSED, the holder's LOCK WORD (DoorLock::locked
// and DoorStuff::wantsToLock) is applied with the engine's own lockDoor / unlockDoor and, where no engine
// call produces the word, the direct write the engine's own code makes - silently, because lockButton's sound
// is heard everywhere - under the same last-actor rule.  A live lock word no detour saw (the NPC lock action
// on a shut door) is this game's own change: reported or adopted, never undone.  The decisions are coopdoor::DoorApplyHow, DoorPlayNote, DoorLockDecide and
// DoorLockStep.
#pragma once
#include <string>
namespace coop {

// PRELOAD, MAIN THREAD: the five detours (the funnel, openDoor, closeDoor, lockDoor, lockButton), each PROLOGUE-CHECKED
// against the bytes read out of this build's own exe before it is hooked (F038/F530).
void InstallDoors();
// MAIN THREAD, every frame, and the idle cost is the session-link read plus one interlocked read:
// publish the changes the detours recorded, apply the holder's states that arrived, and re-apply
// after a setupPhysicalUT forced open.  Every engine write is behind EngineWritesBlocked(), the
// same predicate ClockWorldLive() asks.
void DoorsTick();
// MAIN THREAD, from the store's teardown broadcast: the registry names doors in the world being
// destroyed and the holder table names keys that belong to it.
void DoorsWorldTeardown();
// MAIN THREAD: one MSG_DOOR_STATE off the session link.  `locked` is the lock word (bit 0 locked, bit 1
// wantsToLock - coopdoor::DoorLockWord); a value outside 0..3 is malformed.  P8n: `origin` is the message's optional
// trailing byte - coopdoor::kDoorOriginHolder (0, and what an absent byte means) is the area holder's
// answer; kDoorOriginActor (1) is a non-holder telling the holder what its own world just did.  An
// actor report is NOT stored as the holder's answer here: whether this game holds the door needs the
// door's position, which is a virtual call through a stored pointer, so it waits for DoorsTick.
void ApplyDoorState(const std::string& key, int state, int locked, unsigned int gen, unsigned int fromPeer,
                    int origin);
// The session link came up: a game that joins late has been told nothing, so the holder re-offers
// what it has (the weather pattern's "at WELCOME and on change").  Called from DoorsTick's own edge.
void DoorsOnLinkUp();
// MAIN THREAD. The world server asked this game to catch up another game (slot askerSlot) that has just come to have these n
// sectors (sx[i], sy[i]) in its delivery area - worldsync.cpp's catch-up answer: every registered door of those sectors in a resting
// state is re-offered once, and DoorsTick publishes it, routed by the door's sector, only where this game holds the door.  Queues only.
void DoorsServeCatchup(const int* sx, const int* sy, int n, int askerSlot, unsigned int askNo);
// ANY THREAD, inside the engine's own zone teardown: items.cpp's ZoneMapContent::deactivate detour
// fans out to this (one hook, two subscribers).  It makes two interlocked integer writes and nothing
// else - no walk, no log, no allocation - and the next main-thread DoorsTick drops every registry row
// the engine's active-zone walk no longer lists, BEFORE any virtual call is made through it.
void DoorsOnZoneDeactivate();
// MAIN THREAD, P15 fold 1 (review-p15 CRASH 1): a building's setDestroyed flip is about to run (either way - a ruin deletes its doors
// through GameWorld::destroy, a repair makes new ones under the same keys). Every registry row whose door hangs on it - the row's
// recorded owner, or a door its door array lists right now - is dropped BEFORE the flip, so no row outlives the door it names. The
// next funnel call on a new door re-registers it. Pointer compares and one POD read of the building; no virtual call.
void DoorsForgetOwner(void* building);
// MAIN THREAD, TEST-ONLY (T-160): `doortest open|close|lock|npclock|show <nearest|last|key>` / `doortest show held` - see doors.cpp.
std::string DoorTestCommand(const std::string& arg);
unsigned long long DoorsOpenDoorRva();   /* P18 fold 1: DoorStuff::openDoor (1.0.65 0x297000), 0 = no address row */
// MAIN THREAD, from OnDoorState: the message's trailing origin byte was neither 0 nor 1, so the
// message was IGNORED rather than taken as the area holder's word (P8n-b, review-p8n L-8).
void NoteDoorOriginUnknown();
void SetDoorsOn(bool on);
void ReportDoors();
int DoorOpenStatePod(void* door, int* state, int* openTenths);   /* PROBE P068 (B1): guarded read, 1 = read; state 0 closed 1 open */
// A built piece's doors on its record (build.cpp). ANY THREAD: 1 = `door` is listed by its parent (+0x368), at *indexOut.
int DoorsDoorOfParent(void* door, void** parentOut, int* indexOut);
// Guarded read of what the record carries: health +0x164, its max +0x188, door broken +0x3EC, has a lock, lock level, lock broken.
int DoorsRecReadPod(void* door, float* health, float* maxHealth, int* broken, int* hasLock, int* level, int* lockBroken);
// MAIN THREAD: the record's values onto a door (a break: setBroken(1), unlock, openDoor; a mend: setBroken(0)). -1 fault, 0 nothing to write, 1 written.
int DoorsRecApply(void* door, float health, int broken, int hasLock, int level, int lockBroken);
// MAIN THREAD: a repair's gained health added to the door (up to its max), broken cleared above 0 - as the engine's repair 0x558B10 leaves it. 1 = done.
int DoorsRecRepair(void* door, float gain);
void* DoorsKeptDoorAt(void* building, int index);
void* DoorsLockOf(void* door);
// The record's door applies and the door road around them, for the pieceLive REPORT line (DoorsRecCountsOut): out[0..9] = breaks,
// mends, lock levels, lock broken flags, health writes, broken keeps, broken doors reopened, mends handed back to the holder's word,
// door locks picked, other locks picked; n = the slots to fill.
void DoorsRecCounts(long long* out, int n);
// TEST-ONLY (bldstate, bldset): the door's state (0..3) and its lock's locked byte (0 with no lock); 1 = read.
int DoorsTestStatePod(void* door, int* state, int* locked);
// TEST-ONLY (bldset doorpick), MAIN THREAD: the lock pick's tick at its hooked entry, up to maxCalls times while the lock reads
// locked; the words say what was called and the locked byte before and after.
std::string DoorsTestPick(void* door, int maxCalls, int* lockedBefore, int* lockedAfter);

} // namespace coop
