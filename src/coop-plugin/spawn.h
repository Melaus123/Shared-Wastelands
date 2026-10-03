// spawn.h - M1: create a character from a template, locally and across the link.
#pragma once

#include <string>
#include "../common/statewire.h"   // POSE (read-poses): coopstate::PoseWire

// F316: SafeReadPosition returns a position by pointer, so the type must be complete here. It is a
// header-only value type, so this costs nothing that the .cpp files were not already paying.
#include <ogre/OgreVector3.h>

// Global scope: the engine's Character, not a coop:: one. Declaring it inside the
// namespace would mint a distinct coop::Character that shadows the real type.
class Character;

namespace coop {

// Spawn a character from a game-data template near the captured target.
// If a network session is UP, also replicates the spawn to the peer so both
// instances create the same character, at the same world position, with the same uid.
// uid == 0 means "allocate a new one" (the local/originating case).
bool SpawnTemplate(const std::string& templateName, float dx, float dz,
                   unsigned int uid, bool replicate);

// P11 (test-only lever capturetest): the `spawn` road (AllocateUid / CreateAt / SetLocalOwner / SendSpawn / WatchLocalRoll,
// as SpawnTemplate with replicate) anchored on a registered character (own or copy) instead of the captured reference, in
// `factionName` (empty = the reference's, as `spawn`), the new uid handed back. CreateAt still needs `watchplayer`.
bool SpawnTemplateNear(const std::string& templateName, unsigned int anchorUid, float dx, float dz,
                       const std::string& factionName, bool keepContainer, unsigned int* uidOut);
// P11: what `tasks <uid>` prints, handed back - the adopted goal and hasPendingOrders (-1 = unreadable). False = not readable here.
bool TaskGoalOf(unsigned int uid, std::string* goal, int* hasPendingOrders);

// Apply an inbound SPAWN message (payload decoded by the session layer).
// F115: faction and container mode arrive WITH the message. They used to be read from this
// instance's own `spawnfaction`/`spawncontainer` state, which the sender never set, so the
// two copies were different characters despite sharing a uid, template and position.
// P1: `age` (0..1, already clamped by the decoder) is the owner's CharacterAnimal +0x700 and goes into
// the factory's create() in place of the 0.0 every copy had, so a copied animal is its owner's size.
bool ApplyRemoteSpawn(unsigned int uid, const std::string& templateName,
                      float x, float y, float z,
                      const std::string& factionName, bool keepContainer, float age,
                      const unsigned int* stats44 = 0);   // S1: the owner's 44 stat values, 0 = none carried

// Look up a locally-created object by uid. Returns 0 if this instance has no counterpart
// for that uid - which is a legitimate state, not an error (F079: pre-session spawns are
// never backfilled, so the peer may hold uids we have never seen).
// P033 / M7 - register a character the ENGINE created (world population) as one we own.
// Returns a fresh uid, or 0 if unreadable or already registered. See the note at the definition
// for why "already registered" returns 0 rather than the existing uid.
// `tablesFull` (optional, out) distinguishes the two reasons this returns 0, which need OPPOSITE
// responses: "already ours / restored from retirement" means skip this character, while "the uid
// tables are full" means the registration was refused (mirror1 fold, review-mirror1 #3: skipped and
// retried on a later tick - it never stops the sweep, rows are released since mirror1). Conflating them let one region stream-out shut
// world replication off for a session with a false reason.
// `noUid` (optional, out): M4 (owner 203) - true when no uid could be minted (no notebook slot yet, or the counter is
// spent; counted in AllocateUid). Nothing was registered; the caller retries on a later tick.
unsigned int AdoptExisting(::Character* c, bool* tablesFull = 0, bool* noUid = 0);
bool UidTableHasRoom();   // mirror1 fold (review-mirror1 #3): a free uid mirror row exists right now
// T-354: a copy of another game's character this table refused is reported to its owner (MSG_NOT_SHOWN). MAIN THREAD.
long long MirrorPeerRefusals();          // spawnRefusedFull + twinRefusedFull - read around ApplyRemoteSpawn
int MirrorRefusalReasonForWire();        // the last refusal's reason, coopuid::kNs* (1..4)
void NotShownNoteSent(unsigned int uid, unsigned int ownerKey, bool sent);
void NotShownOnNet(int decoded, unsigned int uid, int reason, unsigned int theirRefusals, unsigned int fromKey, bool uidMine);
std::string MirrorTestCapLever(int cap);   // `mirrorcap <n>` - TEST-ONLY

::Character* FindSpawned(unsigned int uid);
// stale1 (review-stale1): the RAW object the registry holds for uid, 0 if none - no retired / plausibility test and never
// dereferenced by the caller; P034 compares it with a liveness row's address to tell a current row from an out-of-date one.
// MAIN THREAD.
const void* SpawnedRawObject(unsigned int uid);

// M-B / P064 (H025) - the authority's CONTEXT for a replicated character (squad template, SquadType, home town,
// home building handle, member type). SendContextFor reads it off the live character and sends it (once, after
// SPAWN); ApplyRemoteContext resolves each part on the receiving instance and emits a [VERDICT] context record.
// Nothing is APPLIED to the copy yet - this run measures whether the identities resolve across instances.
bool SendContextFor(unsigned int uid, ::Character* c);
void ApplyRemoteContext(unsigned int uid, const std::string& squadSid, int squadType, const std::string& townSid,
                        const unsigned char* handRaw32, const unsigned char* charRaw32, float bx, float by, float bz, int memberType,
                        const std::string& platoonId);   // P1: + the sender's platoon id
// M-B / H026 - `context on|off`, DEFAULT OFF: when on, a SPAWN whose CONTEXT arrived first creates the copy the way
// the engine creates a town NPC (npc-context.md section 6): a platoon from the same squad template, the same
// SquadType, home town and home building, the copy created INTO that platoon with that home, then the same
// squad-member type. Platoons are shared by (squad, building, town) so squad-mates land in one platoon.
void SetContextApply(bool on);

// F316 - the ONLY safe way to read a character's position. `Character::worldPosition`
// double-dereferences the AnimationClass at `Character+0x448` with no null check of its own, so a
// plausibility check on the character alone does NOT cover it - and a retired pointer must not be
// read at all. Doing this by hand crashed the host in T086; do not write the raw call again.
// Returns false and leaves `out` untouched when the position cannot be read safely.
bool SafeReadPosition(::Character* c, Ogre::Vector3* out);

// F316 - has this pointer been withdrawn by P034's stale-pointer detection?
bool IsRetiredObject(const void* obj);

// P038 / F319 - the engine is destroying this object RIGHT NOW. Withdraw the pointer permanently.
// Returns true if it was one of ours. **Called from a detour on an unknown thread**: it compares
// addresses only, never dereferences, and publishes with InterlockedExchange.
//
// This closes the window P034 cannot: P034 samples liveness two rows per tick, so a character
// stays "alive" in our tables for a fraction of a second after it dies, and three consecutive runs
// crashed inside exactly that window. An event has no window.
bool RetireOnDestroy(const void* obj);
// T-304 (2), fold 1: ANY thread, from the destroy detour before anything is retired. True ONLY for the engine's 'eaten' removal
// (debugInfo "eaten", MedicalSystem::gettingEaten) of a registered copy of ANOTHER game's character with the game link up: the
// detour then answers FALSE without destroying (the engine's own 'postponed' answer; counted copyDestroyRefused[eaten]) and the
// copy stays until the owner's DESPAWN. Every other reason goes through. Only the owner removes a character, a corpse included.
bool RefuseEngineDestroyOfCopy(const void* obj, unsigned int uid, const char* debugInfo);
// T-304 (2): MAIN THREAD, every frame (ReplicateTick). Runs NotifyDespawn for this game's own destroys that came off the main
// thread (they carry the uid resolved at the hook's entry), and caches the link state RefuseEngineDestroyOfCopy reads.
void DrainOffThreadDespawns();

// F322 - THE DESPAWN PAIR. Until P038 hooked GameWorld::destroy there was no moment at which a
// death could be announced, so the peer kept its copy forever and the worlds diverged by attrition.
//
// NotifyDespawn: called from the destroy detour. Refuses off the main thread and refuses a uid we
// do not author, counting both rather than doing it anyway.
// ApplyRemoteDespawn: MAIN THREAD. Drops the puppet, withdraws the pointer, then destroys the copy.
// F323: the UID, resolved BEFORE the pointer was retired.
// F334: plus the OBJECT (so our own destruction can be identified by identity rather than by a
// window of time, which would also swallow any nested destroy) and `justUnloaded` from the engine's
// own signature (an unload is not a death and must not be announced).
void NotifyDespawn(unsigned int uid, const void* obj, bool justUnloaded, const char* reason, int dead);   /* M7a3: reason = the engine's literal (any thread may pass it - the 85 sites pass .rdata literals); dead = Character::hasDied read at the hook for an AMBIGUOUS reason (1/0, -1 not read) */
void ApplyRemoteDespawn(unsigned int uid);
// M-A step 2: the owner says we no longer hold this character's sector. Same removal as DESPAWN (the copy is
// dropped, withdrawn and destroyed) but counted apart - it is not a death, and the owner re-announces later.
void ApplyRemoteUnload(unsigned int uid);

// review-session S6 - the peer's link went DOWN while we stayed in the game, so nothing will ever
// send a DESPAWN for the copies it owns here. Same removal as DESPAWN/UNLOAD, counted apart, and
// obeying H030: a twin or a player-faction character of OURS is withdrawn, never destroyed; the
// peer's own player characters are peer-faction copies here and are ordinary puppets.
// Returns 1 destroyed, 0 withdrawn, -1 we hold no copy for that uid. MAIN THREAD.
int DropPeerOwnedCopy(unsigned int uid);
// inv7a (e47-inv7-replan.md 3.1): once per world generation, at the first frame engine writes are allowed, every stand-in
// character (coop-p<n> / coop-peer by RECORD id) no mirror row knows is destroyed by DropPeerOwnedCopy's route - a caged one
// leaves its cage first (K2 safe point), a carried one is put down, a bedded one gets up; what cannot be handled safely is
// skipped, counted and logged. Returns 1 while the arrival queue must stay held (a cage-out still waiting, <= 5 s), else 0.
// Logs `[SPAWN] standin purge at load: chars= platoons= caged= skipped= faulted=` once per load. MAIN THREAD (InQueueDrain).
int StandinPurgeTick(long worldGen);
// inv7a2 (run T412): the purge's EVENT triggers, MAIN THREAD (InQueueDrain, before StandinPurgeTick). `count` stand-in
// platoons woke since the last drain (noted by the wake hook on any thread); the game link came up (a new generation).
void StandinPurgeOnWake(long count);
// inv7a3 (review-inv7a2 MEDIUM), MAIN THREAD: one woken stand-in platoon (its faction and id read at the drain). It is
// re-checked each tick until its members exist, then a pass runs. platoon 0 / no id = nothing to re-check by: a pass now.
void StandinPurgeNoteWoken(void* platoon, void* faction, const std::string& id, long worldGen);
void StandinPurgeOnLinkUp(long linkGen);
// inv7a-b, MAIN THREAD (the P033 sweep): the sweep met an unregistered stand-in copy (faction RECORD coop-p<n> / coop-peer) -
// one the fallbacks' pass ran before it existed. It is never adopted; a purge pass is asked for (kTrigSweep) ONCE per copy per
// world generation, 5 s after the last pass ended, never after a uid table FULL error (coopsp::SweepRequestDecide).
void StandinPurgeRequest(const void* character);
// orphan1 (T420/T426, decision 37): a non-player, unregistered character standing in an area ANOTHER game holds, seen by
// the P033 adoption pass after link-up (created here while unlinked; nobody announces it). Queued; OrphanPurgeTick removes
// it through the stand-in purge's destroy route, at most 4 per judging pass (one pass per 200 ms), and only once it has
// been noted continuously for 5 s with the game link up for 10 s (review-orphan1). MAIN THREAD, both.
void OrphanNote(void* c, float x, float z);
void OrphanPurgeTick(long worldGen);
void OrphanReportNumbers(long long* removed, long long* skipped);
std::string OrphanReportDetail();   /* " orphanSkip[...]=... orphan[faulted,noted,pending]=..." */
long long WorldGenGatedHeldUnlinked();   /* orphan1: defined in worldgen.cpp - the leaf gate's unlinked refusals */

// review-session S6 - retire every platoon the CONTEXT announcements of the player in `slot` created
// here (-1: the old link's peer with no announced slot - every platoon): the store's deactivate pair
// (StoreRetireLocalCopy) then Faction::removeSquad on the sleeping platoon (P1c). Returns how many
// were destroyed. Unlike ForgetContextPlatoons (world teardown) this runs while the world is ALIVE, so
// forgetting alone would strand them in the faction's list. Call AFTER that player's copies have been
// dropped, so its platoons are empty. MAIN THREAD.
int RetirePeerContextPlatoons(int slot);

// F334 - how many destroys were our own despawn call re-entering the hook. Exposed so the
// `[P032]` line can print it beside `destroyOurs`, which is where that number is actually read.
long long DespawnSelfReentryCount();

// F322 - record the main thread, called from the in-game pump, so the destroy detour can CHECK
// which thread it is on rather than trusting a one-run measurement.
void NoteMainThread();

// Reverse lookup: the uid we hold for this object, or 0 if it is not a replicated one.
// Called from a detour on an unmeasured thread, so it must not allocate or lock.
unsigned int FindSpawnedUid(const void* obj);

// E27 / review-p5q HIGH-4 - IS THIS UID ONE OF THIS SAVE'S OWN CHARACTERS, ADOPTED UNDER THE PEER'S UID?
// H027: both players run the same save, so the peer's named characters already exist here. AdoptExistingTwin
// registers OUR copy under the peer's uid instead of building a second body, and does NOT call SetLocalOwner -
// so `IsUidMine` is false for it and every ownership test reads it as somebody else's ghost. It is not: it is the
// SAME INDIVIDUAL, and rules that exist to stop us destroying a copy the peer built do not apply to it.
// MAIN THREAD (a std::set find over the twin registry).
bool IsTwinUid(unsigned int uid);

// F332 - the same lookup for the DESTROY detour, which needs an identity rather than permission to
// dereference. Answers for a RETIRED slot (withdrawn, still that character) and refuses a DESTROYED
// one (the address's identity is finished and may already belong to something else). Every other
// caller must keep using FindSpawnedUid. Same threading contract: address compares only.
unsigned int FindSpawnedUidForDestroy(const void* obj);
// M7a3f3 T-425 [m7a3f3-sh0]: 1 = another game's copy of squad `platoonId` under `faction` is live here (the platoon its CONTEXT built, awake). MAIN THREAD.
int CtxPlatoonLiveForId(const void* faction, const std::string& platoonId);

// Enumerate the uid mirror. M5 needs to walk every replicated character every frame (the
// prone/death watch) without going through the std::map, and it needs the OCCUPANCY, because
// the mirror is a fixed array (4096 rows since T-354; 2048 since mirror1 (crash T487); 512 filled in T487).
// (T-354 fold 1: comment updated.)
// A bound that is never reported is not a bound.
// P034/T083 - retire an entry so nothing dereferences its pointer again, WITHOUT removing it (a
// retired row keeps its uid so it can come back; mirror1 releases a row only once its character is
// finished - MirrorReclaimDestroyed, and a despawn). Reversible: a handle that fails to resolve
// may simply have streamed out with its region, and MirrorRestore un-retires it if it comes back.
void MirrorRetire(const void* obj);
void MirrorRestore(const void* obj);

// F334 - PERMANENT retirement: this address's IDENTITY is finished, not merely withdrawn. Use it
// wherever the address has been proven to hold a different object; `MirrorRetire` alone leaves the
// slot eligible for un-retirement and leaves its old uid answerable, and two separate mechanisms
// (F328's adoption guard and F332's destroy lookup) read `destroyed` to mean exactly this.
//
// F336 - **PASS THE UID YOU BELIEVE THIS ADDRESS HAS, and check the return.** The mark is refused
// when the slot has since been re-adopted under a different uid, because the caller's row can
// outlive a destroy-recycle-readopt cycle and would otherwise permanently kill a LIVE row. Returns
// false when refused; refusals are counted.
bool MirrorMarkDestroyed(const void* obj, unsigned int uid);
long long MarkDestroyedDeclinedCount();

// F337 - **CALL THIS WHEN THE MARK IS DECLINED.** A decline means the address was recycled and
// re-adopted under another uid, so YOUR uid's object is gone - and leaving it in the registry
// aliases it onto the new occupant, which `FindSpawned` will happily return. Drops the puppet,
// removes the uid, and announces the despawn if we author it. MAIN THREAD ONLY.
void RetireStaleUid(unsigned int uid);
long long StaleUidRetiredCount();

// F341 - did we once hold this uid and lose it? Distinguishes "we were never told about this
// character" from "we had it and the engine withdrew it", which `FindSpawned` cannot: it answers 0
// for a retired row by design. Retired rows included deliberately.
// **Contract, broader than its one caller (F342):** true means "we held this uid and lost it",
// which covers both a retired mirror row AND a uid `RetireStaleUid` permanently discarded (that one
// has no mirror row at all, so a mirror scan alone answers it backwards). A uid can occupy two
// mirror rows - `MirrorAdd` dedups on the OBJECT, not the uid - so a live uid could read as retired
// if this is called while another row for it resolves; the only caller consults it exactly when
// `FindSpawned` has already returned 0, where that cannot arise.
bool KnownUidRetired(unsigned int uid);
long long LostUidOverflowCount();   // F342: the lost-uid ring dropped this many, 0 = it did not

int  MirrorCapacity();
int  MirrorUsed();
// mirror1 (crash T487): release every row whose identity is finished (`destroyed`), forgetting its
// uid's map entry and puppet without a dereference. MAIN THREAD, never inside a walk of the puppet
// list. Returns how many rows were released.
int  MirrorReclaimDestroyed();
bool MirrorSlot(int i, unsigned int* uid, ::Character** out);

// F155: the BEHAVIOUR of a replicated character, as one short string.
//
// This is the layer every instrument in this project was missing. Health, prone, blood, the
// medical latches, placement error - all of them compare VALUES. None of them compares what a
// character is DOING. So the largest divergence in the system was structurally invisible to the
// entire suite, and a human looking at two screens for ten minutes found it instantly: one
// instance had characters running around chasing, the other had the same characters standing
// still (F154).
//
// Everything it reads was already available - the goal string has been read by P011 since M2 -
// it was simply never part of the routine per-character comparison.
//
// Returns e.g. "goal='Attacking target' orders=1 spd=3.4 pos=-50975.8,1532.6,2932.5"
// MAIN THREAD (it allocates).
std::string BehaviourString(::Character* c);

// P024 / F200 - IS IT FIGHTING, AND WOULD IT LOOK LIKE IT?
//
// The user reported that on the peer, characters move to roughly the right places but show no
// combat animations and no combat marker on their portraits, in fights this project's
// instruments scored as passing. Those numbers were right; nothing measured this layer.
//
// Plain reads of the engine's own combat fields - combat mode (the flag a portrait reads),
// swordStateEnum (which combat animation is chosen), whether a swing is in progress, and the
// animation layer's own idle flag. Nothing is called on the combat object; nothing is written.
//
// Returns e.g. "cc=ok mode=1 state=0:SWORD_SWING next=4:DECISION atk=0.6 tech=1 combo=2 animIdle=0"
// MAIN THREAD (it allocates).
std::string CombatString(::Character* c);

// Log what we have spawned/own, WITH live positions (extends the report command).
void ReportSpawns();

// P025 / F200 - count and describe EVERY character the engine is updating, not only ours.
//
// The user reported that the NPCs in the area differ between instances. Nothing here has ever
// looked beyond the handful of characters we spawn, so every "parity" score in this project was
// scoped to those. Prints a total, how many are ours, and race/faction histograms, all sorted
// so the two instances' lines can be diffed directly.
//
// READ ONLY. Command-triggered, never per-frame.
void WorldCensus();

// P031 / F261 - one line per character, keyed by the engine's `hand` and sorted by it, so the
// two instances' rosters diff line-for-line.
//
// P025 proves the two worlds hold different NUMBERS of people. It cannot prove they hold
// different PEOPLE: with no per-character identity logged, an equal count is equal only in
// aggregate. This answers that, and in doing so measures the thing P-16 Option A's cost turns
// on - whether the save-derived population already matches by handle across processes (in which
// case it needs no replication) or does not (in which case the bill is the whole world).
//
// READ ONLY. Command-triggered, never per-frame.
void WorldRoster();
/* P8a: `roster full [<page>]` - the probe build/read-roster-t236.md 4c asks for.  One line per loaded
   character: uid, mine, puppet, squad, squadType, town, building, sector, spawnCause and the adoption gate
   that would refuse it, sorted by uid so the two games' output diffs line for line, and PAGED at 64 rows
   like boxlist.  Read-only: it creates nothing, moves nothing, sends nothing and touches no counter. */
void WorldRosterFull(int page);

// Log the goal the engine actually adopted for a uid - settles "did it take the task we
// sent" by inspection rather than by inferring it from where the character walked.
void ReportTask(unsigned int uid);

// PROBE P009 (diagnostic only, H006): print a character's embedded `hand` - the engine's
// own toString/bool/explainState - for a spawned uid and for the captured player
// character, so the two can be compared. Answers whether factory-created characters are
// addressable by handle at all.
void ReportHandle(unsigned int uid);

// Choose the faction subsequent spawns join. Empty name = the reference character's own
// faction (the original behaviour). Returns false if the name does not resolve.
// Fixes F093/F101: spawning everything into the player's squad made spawned characters
// permanent allies, so no ordered attack could ever be planned.
bool SetSpawnFaction(const std::string& factionName);

// F127/F131: the WHOLE per-part injury record travels, in the engine's own field order:
//   [0] flesh · [1] stunDamage · [2] bandageLevel · [3] splintLevel · [4] limbWear
//   [5] maxHealthBase · [6] age · [7] healthScale
// `MedicalSystem::isCollapse` is literally `flesh - stunDamage + splintLevel < 0`, so sending
// `flesh` alone left both machines with identical flesh and opposite conclusions (F127).
// And the ceiling the engine clamps `flesh` against is `age * maxHealthBase * healthScale -
// limbWear` (F131, read out of clampHealth at 0x644B10), so sending `maxHealthBase`
// alone left two of the three factors behind. Buffers are `parts[maxParts * kPartFloats]`.
// All-or-nothing: a partial record puts a wound on the wrong limb, which is worse than none.
const int kPartFloats = 8;
int  SnapshotHealth(unsigned int uid, float* parts, int maxParts, float* blood);
bool ApplyHealth(unsigned int uid, const float* parts, int count, float blood);
int  SnapshotState(unsigned int uid, float* parts, int maxParts, float* blood,
                   int* prone, int* dead, unsigned int* latchBits, float* nextKnockoutAt, float* koTimer);
// True if PoseState had to be CHANGED. H009: with the full record replicated the peer should
// derive the same collapse itself, so this should stop returning true. Kept as a safety net so
// its SILENCE is the measurement. R1-a / R1-a-b: a write CharacterProneSafe says no to is PARKED
// (returns false) with `fromPeer` - the peer whose STATE this is - and applied later by
// ProneParkTick after it re-asks the ownership guard; health and the latch are applied now.
bool ApplyState(unsigned int uid, const float* parts, int count, float blood,
                int prone, int* outWasProne, unsigned int latchBits, float nextKnockoutAt, float koTimer,
                unsigned int fromPeer, unsigned int carryingUid,
                const float* rest);   // R3 (read-ragdoll): where the owner's ragdoll settled, 0 = none

// K1 (read-carry 2026-09-22). The uid of the registered character `uid` carries (Character +0x348 set and its
// carryingObject hand, +0x380, names that character's own hand), 0 = nothing. Reads only; no engine call.
unsigned int CarryingUidOf(unsigned int uid);
// MAIN THREAD, every in-game frame: an owned carrier's carry changed -> StatePush at once (carrySent).
void CarryWatchTick();
// MAIN THREAD, every in-game frame: make each copy carry what its owner's STATE says, through the engine's own
// pickupObject 0x5CF500 / dropCarriedObject 0x5CD750, once both characters pass CharacterBuilt (R1-a).
void CarryApplyTick();
// " carrySent=.. carryApplied=.. ..." for the [P014] REPORT line.
std::string CarryReportToken();
// arrest1 (docs/design-arrest.md 3): a decoded MSG_CARRY_BREAK - the game that drives `body` says its carry by `carrier`
// ended there. Acted on only when the sender drives the body and THIS game drives the carrier; the carrier puts its copy of
// the body down at the next CarryApplyTick. MAIN THREAD (the session drain).
void ApplyRemoteCarryBreak(unsigned int body, unsigned int carrier, unsigned int fromPeer);
void CarryBreakNoteDropped();   // a malformed one, or one arriving while the world is being rebuilt
// A conversation's carried-person hand-over (speech.cpp, the talker's game): this game's own `carrier` handed `body` to the other
// game's NPC `taker`. For cooptalk::kTalkHandOverMs, a wish from the NPC's owner that `taker`'s copy carry `body` makes `carrier`
// let go of it here (CarryApplyTick) - the one case where this game's own carrier gives a body up on another game's word.
// MAIN THREAD.
void CarryHandOverNote(unsigned int body, unsigned int carrier, unsigned int taker);
// M7b (T761) TEST-ONLY lever, reached only through the command file (items.cpp CaptureTestArm: `capturetest carry` /
// `capturetest carried`). MAIN THREAD. One [CAPT] capturetest carry / carried line; the returned status line.
std::string CarryTestCommand(const std::string& arg);
/* TEST-ONLY lever: `bedtest <carrierUid> <bodyUid | name <name>> [bedKeySubstring] [type=<n>]` - this game's own carrier is given
   the engine's put-in-bed order on the nearest free bed; `bedtest list [radius] [nearUid]` reads the beds nearby. MAIN THREAD. */
std::string BedTestCommand(const std::string& arg);
/* P43 TEST LEVER `cagetest <copyUid> nearest`: this game's engine cages the other game's character's copy in the nearest free cage
   (setPrisonMode at the K2 safe point, as a guard's task does); PrisonWatchCopies then tells the owner. [ARREST] cagetest lines. */
std::string CageTestCommand(const std::string& arg);
// P42 TEST-ONLY lever `locktest read <uid> | open|close <uid> shackles|cage | escape <uid>` (spawn.cpp): a prisoner's shackle and
// cage locks read, opened or closed as a pick's result at the K2 safe point, or our caged character ordered to escape. [LOCK] lines.
std::string LockTestCommand(const std::string& arg);
// TEST-ONLY lever crimetest bountyset (crime.cpp): the key a bounty on c is stored under for `faction`'s law - GetBountyFaction
// 0x851140 (c's BountyManager, faction), the resolve the prison sentence makes. 1 resolved (*key), 0 no address, -1 faulted.
// MAIN THREAD, K2 safe point.
int BountyKeyForPod(::Character* c, void* faction, void** key);

}   // namespace coop
namespace cooprison { struct PrisonMsg; }
namespace cooptreat { struct TreatMsg; }   /* heal1: src/common/treatwire.h */
namespace coop {
// arrest2 (docs/design-arrest.md 3): a decoded MSG_PRISON - the sender's guard caged its copy of `m.uid`. Acted on only for a
// character THIS game drives: it is caged in this game's copy of the same cage at the next PrisonApplyTick. MAIN THREAD.
void ApplyRemotePrison(const cooprison::PrisonMsg& m, unsigned int fromPeer);
// heal1 (user T305/T307: the guards bandaged the jailed character forever). MAIN THREAD. A medic here raised a copy's per-part
// bandageLevel / splint above what its owner's STATE last wrote -> MSG_TREAT to the owner (TreatTick, every 0.5 s); the owner raises
// its own character's values to them (ApplyRemoteTreat) and pushes a STATE. ApplyHealth keeps the copy's raised values for
// a few seconds so the owner's next STATE, sent before it heard, does not erase them.
void ApplyRemoteTreat(const cooptreat::TreatMsg& m, unsigned int fromPeer);
void TreatNoteDropped();   // malformed, or arrived while the world is being rebuilt
// names1 - a copy carries its owner's character name (MSG_NAME, protocol 64). spawn.cpp "names1" block.
void InstallNames();                                              // hook Character::setName 0x5CB840 (table "Character_rename")
void NameTick();                                                  // MAIN THREAD: send the renames the hook marked
void NameSendWithSpawn(unsigned int uid, const void* character);  // MAIN THREAD: net::SendSpawn, after each SPAWN it sent
void NameNoteRecv(unsigned int uid, const std::string& name, unsigned int fromPeer);   // MAIN THREAD (dispatch)
void NameNoteBad(bool tooLong);
void NameOnCopyReady(unsigned int uid);                           // ApplyRemoteSpawn: a held name is applied to the new copy
void NameForgetUid(unsigned int uid);                             // the uid is retired / despawned
void NameForgetPeer(int slot);                                    // the player in `slot` is gone: its held names are dropped (-1: every held name)
std::string NameReportLine();                                     // "[NAME] REPORT ..."
// slave1: a copy's slave state follows its owner (MSG_SLAVE, protocol 65). Hooks: StateBroadcastData::setSlaveState 0x5A3EB0
// (table "SlaveStateSet") and StateBroadcastData::periodicUpdate 0x5A44C0 ("SlaveStatePeriodic"), both on the AI worker.
void InstallSlaves();
void SlaveTick();                                                 // MAIN THREAD: send the owned changes the hooks marked; log refusals
void SlaveSendWithSpawn(unsigned int uid, const void* character); // MAIN THREAD: net::SendSpawn, after each SPAWN it sent
void SlaveNoteRecv(unsigned int uid, int state, unsigned int ownerUid, unsigned int fromPeer);   // MAIN THREAD (dispatch): held for the K2 safe point
void SlaveNoteBad();
void SlaveSafePointDrain();                                       // K2 safe point (combat.cpp): the owner's value set on its copy
void SlaveForgetUid(unsigned int uid);                            // the uid is retired / despawned
void SlaveForgetPeer(int slot);                                   // the player in `slot` is gone: its held states are dropped (-1: every held state)
void InstallCapture();                                            // P11 (items.cpp): task 181 / 182 bodies + strip / dress / owner / shave hooks
void CaptureTick();                                               // P11 MAIN THREAD: send what the slaver bodies recorded; timeouts
void CaptureSafePointDrain();                                     // P11 K2 safe point: apply MSG_CAPTURE (owner) / MSG_CAPTURE_DONE (captor)
void CaptureOnRequest(const char* p, size_t n, unsigned int fromPeer);   // P11 MAIN THREAD (dispatch): queued for the K2 safe point
void CaptureOnDone(const char* p, size_t n, unsigned int fromPeer);      // P11 MAIN THREAD (dispatch): queued for the K2 safe point
void CaptureOnPlaced(const char* p, size_t n, unsigned int fromPeer);    // P11 f3 MAIN THREAD (dispatch): queued for the K2 safe point
void CaptureWorldTeardown();                                      // P11 f3 (items.cpp teardown): held objects name the world that goes
std::string CaptureReportLine();                                  // P11: "[CAPTURE] REPORT ..."
std::string SlaveReportLine();                                    // "[SLAVE] REPORT ..."
void TreatTick();
std::string TreatReportToken();   // " heal1[...]" for the [P014] REPORT line
void PrisonNoteDropped();   // malformed, or arrived while the world is being rebuilt
// MAIN THREAD, every in-game frame: (a) a COPY this game's engine caged (a guard's task) is reported to the game that drives
// it; (b) a queued MSG_PRISON cages this game's own character.
void PrisonTick();
// arrest2 (engine read 1): MAIN THREAD, worker paused - the K2 safe point (combat.cpp detour_tsRagdollUpdates). The queued
// cage-ins / cage-outs: isFreeSlot, setPrisonMode, and for this game's own character the cage lock and the sentence.
void PrisonSafePointDrain();
// " arrest2[...]" for the [P014] REPORT line.
std::string PrisonReportToken();
// arrest3 TEST-ONLY lever (`crimetest sentence <hours>`): the sentence of every copy THIS game's guard jailed that is still in
// its cage becomes <hours>, so this game's jailer frees it within a run (a real 2500 bounty is ~50 game hours). Returns how many.
// MAIN THREAD.
int PrisonTestSentence(float hours);

// R3 (read-ragdoll 2026-09-22): a downed body's RESTING position is shared.
// Owner: the rest position last sent for an owned uid whose ragdoll is settled and not carried / held (false = none).
bool RestOfOwned(unsigned int uid, float* out3);
// MAIN THREAD, the STATE tick: an owned body's ragdoll settled (ragdoll +0x10 flipped to 1), or a settled body moved
// > 20 units since last sent -> StatePush at once (restSent). Reads only; no engine call.
void RestWatchTick();
// MAIN THREAD, every in-game frame: post a copy's move (owner rest - its AnimationClass +0x98) for the 0x7D38D0 detour,
// once it is ragdolled, settled, not carried / held and CharacterBuilt; verify the effect on the second frame after the
// last step (R3-b). One attempt per owner rest update; a move is forgotten only once the table confirms it is out.
void RestApplyTick();
// Hooks the below-ground rescue 0x7D38D0 (physics/update thread): a pending move is taken and made with the engine's
// translate 0x7D1580, the ragdoll tick 0x7D3060 (as 0x7D38D0 does), then notifyRagdollNeedsAnUpdate 0x7D07B0.
// Allocation-free, lock-free.
void InstallRagdollRest();
// R3-b (review-r3 item 3): did InstallRagdollRest hook 0x7D38D0? False = no copy's ragdoll is ever moved.
bool RagdollRestHookArmed();
// Is this character's ragdoll active (RagdollClass +0x59 or +0x5A)? Reads only, fault-guarded.
bool RagdollActive(::Character* c);
// " restSent=.. restRecorded=.. ..." for the [P014] REPORT line.
std::string RestReportToken();
// G1 (read-getup, T261), MAIN THREAD, every in-game frame: a copy (not owned here, not carried +0x3D4 / ragdoll +0x5A,
// not in a bed/cage +0x2F8) whose owner's last STATE says up (prone < 3, unconscious latch 0, no KO timer) for > 1.5 s
// but which is still a ragdoll (inRagdoll 0x7D08A0) gets PulseCharacterQuiet - one decision pass, so the queued
// 'Getting up' task (0x41) is adopted and the engine's own get-up runs. Once per 2 s, up to 5 per episode.
void GetupPulseTick();
// " getupPulsed=.. getupDone=.. getupGaveUp=.." for the [P014] REPORT line.
std::string GetupReportToken();
// MAIN THREAD, every in-game frame: a copy whose owner's latest STATE (bit 7, sneakwire.h) disagrees with the copy's own
// stealth mode is put in or out of it through the engine's Character::setStealthMode 0x5C9F10 - once per STATE, once the
// copy's body is built. The owner side pushes a STATE at once when an owned character's mode changes (RestWatchTick).
void SneakApplyTick();
// " sneak[pushed,setOn,setOff,matched,unreadable,fault,waited,reverted,held,faultSkip]=.. sneakWatched=.." for the [P014] REPORT line.
std::string SneakReportToken();
// sneaktest <ownUid> on|off - TEST-ONLY lever: this game's own character in or out of sneaking at the next SneakApplyTick.
std::string SneakTestCommand(const std::string& arg);

// POSE (read-poses 2026-09-23, user T262). The owner's IN-PLACE pose travels in STATE (session 50): bed mode (Character
// +0x2F8 == 1) by the bed's building key, or the action it replays every tick (AnimationClass +0x210 name). MAIN THREAD.
// Owner: the pose last computed for an owned uid (false = none, kind 0 is sent).
bool PoseOfOwned(unsigned int uid, coopstate::PoseWire* out);
// Every in-game frame: an owned character's pose changed -> StatePush at once (poseSent). POSE-b: only a LOOPED action
// (AnimationData +0x8A) of an owner standing still is sent; stances, carry lifts, get-ups, idle fidgets, turrets,
// stumbles and cages never are.
void PoseWatchTick();
// OnState: the owner's word on a copy's pose (latest ARRIVAL wins - STATE has no sequence field); PoseApplyTick acts on it.
void NotePoseWant(unsigned int uid, const coopstate::PoseWire& pose, unsigned int fromPeer);
// Every in-game frame: a copy (not carried, not ragdolled, not dead) is put in its bed with setBedMode 0x32E2B0, or has
// its action replayed with playAction 0x51F920; kind 0, a handoff, and 10 s without a confirming STATE undo what was
// applied. An action pose waits until the copy stands within 1.6 of the owner's spot. setBedMode DOES register the copy
// with the bed (vtable +0x4F8) - a bed shared by owner and copy is accepted (v1); no machine is operated.
void PoseApplyTick();
// True while the copy `uid` is posed - its movement drive holds it still and no G1 pulse is armed for it.
bool PoseHoldsCopy(unsigned int uid);
// replicate.cpp: one frame of a posed copy held still (poseHeldFrames).
void PoseNoteHeldFrame();
// " poseSent=.. poseAppliedAction=.. ..." for the [P014] REPORT line.
std::string PoseReportToken();

// Per-limb health readout - M3's real acceptance criterion is that a hit on one instance
// produces MATCHING per-limb health on the other, and until now nothing could read it.
// One line per character, all parts on it, so two instances diff at a glance.
void ReportHealth(unsigned int uid);

// R1-a / R1-a-b: apply the prone writes ApplyState parked, once CharacterProneSafe says yes and
// the ownership guard still accepts the sender; drop a uid that FindSpawned no longer returns.
// Does nothing while EngineWritesBlocked(). MAIN THREAD, every in-game frame beside AppearanceTick.
void ProneParkTick();

// PROBE P011 (DIAGNOSTIC ONLY, F111): sample a character's goal on the main pump and log
// every CHANGE of goal string / hasPendingOrders for `seconds`. Answers what an accepted order
// actually does in its first seconds - a window no run of this project has ever observed,
// because every previous reading was taken ~2 minutes after the order. Up to 2 at once.
void WatchTask(unsigned int uid, int seconds);
void WatchTaskTick();

// Whether an explicit-faction spawn ALSO keeps the reference character's platoon as its
// container. Only consulted when a faction is set explicitly. Default true.
// F106: the faction and the container were two decisions welded into one code path, so the
// T029 runaway could not be attributed to either. This separates them so one run can
// compare the two variants directly instead of guessing which mattered.
void SetSpawnContainer(bool keep);

// Enumerate factions present in this save, optionally substring-filtered. Enumerating
// beats guessing a name (F038's lesson, same as templates).
void ListFactions(const std::string& filter, int maxCount);

// List CHARACTER templates present in this install, optionally filtered by a
// case-insensitive substring. Enumerating beats guessing a name (F038's lesson).
void ListTemplates(const std::string& filter, int maxCount);

} // namespace coop
namespace coop { void ForgetContextPlatoons(); void ForgetContextPlatoon(void* platoon); }
namespace coop { void* ContextPlatoonForActive(void* active); int SlaveStateOfCharacter(const void* c); int IsContextPlatoon(void* platoon); }   /* M7a2 [m7a2-cx2]: IsContextPlatoon - the store's load, wake and sleep hooks (MAIN THREAD) */   /* recruit1 fold: hire.cpp */   // review-p3b: called from the store's world-teardown hook
// review-p3o H2: drop every engine pointer this module caches (g_spawned, the uid mirror and its
// hash index) before GameWorld::_clearAndDestroyGameWorldStuff frees the characters they name.
// MAIN THREAD, and only from the store's world-teardown hook.
namespace coop {   /* M4 fold (store protocol 58): the uid minter's blocks - store.cpp UidBlockTick / UidBlockOnNotebook / link-down, and the sweep. MAIN THREAD */
bool SpawnUidBlockWanted(unsigned int nowMs);
unsigned int SpawnUidSeat();   /* owner 205 A: the seat the notebook's last GRANT on this link named (0xFFFFFFFF = none) */
void SpawnUidBlockAsked(unsigned int nowMs);
void SpawnUidBlockReply(bool decoded, unsigned int kind, unsigned int slot, unsigned int lo, unsigned int hi, long linkGen);   /* M4 fold 2: linkGen = the notebook link generation the answer arrived on */
void SpawnUidBlocksForget();
bool SpawnUidMintReady();   /* L3: can a uid be minted now? M4 fold 2 (L-B): a refusal is counted once per stretch of the same reason; every refused call in uidSweepTicksRefused */
}
namespace coop { void SpawnWorldTeardown(); }
/* snap1 (user decision 2026-09-26): the `snapshot` verb's per-character engine facts. Every engine read is a guarded POD
   frame (spawn.cpp SnapCharFacts). MAIN THREAD. 1 = read (fields that would not read are flagged), 0 = not a live character. */
namespace coop {
struct SnapFacts
{
    int havePos; float x, z;
    int dead;              /* 1 dead, 0 alive, -1 unreadable */
    int haveBlood; float blood;
    int haveHunger; float hunger;   /* par5 (parity P5): MedicalSystem::hunger +0x60 */
    int playerFac;         /* 1 = this game's own player faction */
    char fac[128];         /* "@slot:<n>" for a player faction or a player's stand-in (the wire's slot), else the faction's name with ' ' as '_'; "?" unreadable */
    char town[96];         /* the town sid `roster full` reads (the platoon's Ownerships town); "" none */
};
int SnapCharFacts(void* character, SnapFacts* out);
}
