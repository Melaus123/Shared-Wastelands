// replicate.h - M2a: stream an owned character's position; drive the remote copy to match.
#pragma once
#include <string>
#include <vector>   /* M7a A1 build 1: PuppetUidsSnapshot */

class Character;   // F239: InjectAttackOrder takes two of them; nothing here dereferences one
namespace Ogre { class Vector3; }

namespace coop {

// Begin/stop replicating the captured target's position to the peer.
// The OWNER streams; the peer's copy is a puppet (decisions suppressed, driven).
bool ReplicateStart(unsigned int uid);
void ReplicateStop();

// F152: make a spawn-replicated character an actual PUPPET - decisions suppressed, body
// driven from the authority's stream. Called by ApplyRemoteSpawn for every character we do
// NOT own. Until this existed, `ApplyRemoteSpawn` created a copy and then let it run its own
// AI, so the two players watched different events (F154) while `puppets=0` sat in the log
// being read as a known gap rather than a question (F155).
void AdoptRemotePuppet(unsigned int uid);

// F322 - stop holding a puppet. THE FIRST RELEASE PATH IN THIS PROJECT: an adversarial review found
// that nothing anywhere erased from the puppet map. Harmless while nothing could tell us a
// character had gone; not harmless once DESPAWN can, because the drive would keep pushing a
// character we had just removed. Restores the engine's own desiredSpeed if the lever wrote it.
// F355 - it also STOPS the character before forgetting it. Under F062 ceasing to push does not
// stop a suppressed character, and after the erase nobody is left to. Gated on the same flag as
// the speed restore, and for the same reason.
// F338 - `restoreSpeed` must be FALSE when the puppet's address may no longer belong to this uid.
// The restore writes through the stored pointer, and on the stale-uid path (F337) that pointer is
// another character entirely - a valid one, so no guard can catch it. Every ordinary caller passes
// true; only `RetireStaleUid` passes false.
void DropPuppet(unsigned int uid, bool restoreSpeed = true);
// mirror1 (crash T487): the puppet list asked directly, so removal does not depend on the uid table.
bool HasPuppet(unsigned int uid);
void PuppetUidsSnapshot(std::vector<unsigned int>* out);   // M7a A1 build 1 [a1b1-rh0]: every uid held as a puppet, MAIN THREAD
void DropPuppetIfObject(unsigned int uid, const void* obj);   // only if its puppet still names obj; no dereference
long long AdoptRefusedFullCount();

// Apply an inbound MOVE for a uid we hold a puppet for.
// F350/F353 - the last two are the authority's VELOCITY in world units per second, measured from
// two successive samples of this same stream. They were declared `dirX/dirZ` and every caller sent
// 0.0f, so the peer had never been told whether the authority was moving at all - which is the
// whole of P-20's cause.
void ApplyRemoteMove(unsigned int uid, float x, float y, float z, float velX, float velZ, float authDesiredSpeed, float faceX, float faceZ,
                     bool stamped, unsigned int stampMs);   // stamped: stampMs is the owner's clock (ms) when it took the sample
// The owner's stated stop for one of its characters, NPCs included (MSG_MOVESTOP): at (x, y, z), at the owner's stampMs.
void ApplyRemoteMoveStop(unsigned int uid, float x, float y, float z, unsigned int stampMs);
// One of this game's own characters was despawned or unloaded: it leaves the owner's stop watch and its sent stop point
// (NotifyDespawn). MAIN THREAD.
void FollowOwnForget(unsigned int uid);

// H019 / M-C - the authority's CURRENT GOAL for a puppet (stored per puppet; refused for uids we do not puppet).
void ApplyRemoteIntent(unsigned int uid, int taskType, unsigned int subjectUid, float x, float y, float z, int priority);
// P25 fold 2: the owner's word (MSG_INSIDE) on which building its character is in - stored on the puppet row, read by
// InteriorKeepTick. Refused (counted) unless we hold a puppet for uid. MAIN THREAD (the session pump).
void ApplyRemoteInside(unsigned int uid, int inside, const std::string& key);
// Hand a gated puppet an ORDER of any TaskType (subject = a character, or the puppet itself when none) and let
// exactly one decision pass adopt it - InjectAttackOrder generalised. MAIN THREAD ONLY.
bool InjectOrder(::Character* puppet, int taskType, ::Character* subject, const Ogre::Vector3& loc);
// `handoff auto <n>`: up to n eligible puppets (have an intent, not attacking, no open window), alternating
// inject / control; each gets a [VERDICT] handoff record ~30 ticks later. Returns how many were dispatched.
int  HandoffAuto(int maxCount);
// M-D - used by handoff.cpp
bool ReadAuthorityIntent(::Character* c, int* type, unsigned int* subj, float* x, float* y, float* z);
bool IntentResendOwned(unsigned int uid);   /* M7a2 item 9 [m7a2-rh1]: the catch-up's INTENT, MAIN THREAD */
bool ReadAuthorityFacing(::Character* c, float* fx, float* fz);
void UnpuppetForOwnership(unsigned int uid);

// E25 (review-p5p HIGH-1) - WHERE THE OTHER PLAYER WAS LAST SEEN, from the copies of their characters this game
// holds. There was no accessor for this at all: the peer's position lives only inside the puppet rows, as the last
// position their authority streamed. MAIN THREAD ONLY (it walks the puppet map).
//
// "The peer player" is taken to be the most recently streamed character in the PEER FACTION - the runtime faction
// (playerfaction.h, decision 23) that every one of the other player's own characters is resolved into on arrival.
// Their squad moves together, so any member of it answers "which sector is the other player in" to within the
// sector, which is the only precision the caller needs. `ageSec` is how old that sample is; a caller that must not
// act on a stale answer checks it. Returns false when no such character is replicated here at all.
bool PeerPlayerPosition(float* x, float* y, float* z, double* ageSec);
// E25: 1 = the character is in the peer player's own faction, 0 = not, -1 = the read faulted. tags1 labels only the 1s.
int PeerFactionPod(::Character* c);

// Per-frame: send our owned position on a cadence, and drive puppets toward theirs.
// MAIN THREAD only (called from the in-game pump).
void ReplicateTick();
// PROBE P071 (T259; REMOVE once the copy speed and placement-height questions are answered - 04-probes.md).
// site: 0 prone snap, 1 stuck snap, 2 far snap, 3 authority snap, 4 twin placement, 5 spawn create.
void P071NotePlacement(unsigned int uid, void* ch, int site, float x, float y, float z);
void P071Tick();   // once per main-thread frame, after ReplicateTick: logs last frame's placements

// Order one spawned character to attack another, LOCALLY (no session needed).
// Exists because the spawner assigns the reference character's faction (F093), so a
// hostile template spawns friendly and no fight ever starts on its own. This drives the
// damage path deterministically instead of waiting for the world to produce a fight.
bool AttackLocal(unsigned int attackerUid, unsigned int victimUid, int taskType);

// M2b: send a task order for an owned uid, and apply an inbound one.
// taskType is a raw TaskType enum value; location is absolute world coordinates.
bool SendTask(unsigned int uid, int taskType, float x, float y, float z);
void ApplyRemoteTask(unsigned int uid, int taskType, float x, float y, float z);

// F239 - hand a SUPPRESSED puppet an attack order and let exactly one decision pass through so
// it can adopt it. Unlike `AttackLocal` this does NOT un-suppress: T072 showed the peer gets
// combat mode with `goal='Aimless' orders=0` while the authority has `goal='Attacking target'
// orders=1`, and T071 showed that un-suppressing produces a free-running clone.
// MAIN THREAD ONLY.
bool InjectAttackOrder(::Character* puppet, ::Character* victim, int taskType);

// F242 - retract it. An order is a LEVEL, not an edge: T073 left one in place when the fight
// ended and the peer kept swinging for 87 seconds after the authority had stopped, drifting
// 200-410 units. Anything we set on a puppet we must be able to unset.
// MAIN THREAD ONLY.
bool ClearAttackOrder(::Character* puppet);

// F305 - the drive's MAGNITUDE lever. `steerDirectly` carries a unit DIRECTION only; the
// speed comes from `CharMovement::desiredSpeed`, which this drive has never written, and which
// `CharMovement::update` feeds to Havok every tick as min(desiredSpeed, speedCap).
//   0   = do not touch it (DEFAULT - so the untouched value can be measured)
//  -1   = each puppet uses its own speedCap
//  >0   = force that value on every puppet
void  SetCatchup2(bool on);   // walk1: the proportional, signed catch-up (default on); off = the H020 hysteresis
void  SetRunBoost(bool on);   // T-189 runboost (H051): a copy trailing a running owner may exceed run speed (default on; off = A/B)
void  SetDriveSpeed(float v);
float GetDriveSpeed();

// P037 / F315 - `catchup on|off`, DEFAULT OFF. A puppet that is pushed for two full windows and
// goes nowhere is PLACED at its target instead of pushed at it.
//
// This is the discriminating test for F313's hypothesis (STEER_BY_DIRECTION is a straight-line push
// with no pathfinding) before it is a fix, and it is a legitimate fix because the user decided
// teleports are acceptable. Off by default so the stall rate can be read with nothing corrected -
// `stalledWindows` counts either way.
void SetCatchup(bool on);
int  ForceCatchupGiveUp(unsigned int uid);   // T-573 TEST-ONLY: `giveup <uid>` (0 = every copy); returns copies marked

// H015 / P059 - NATIVE COMBAT WINDOW. While a puppet is fighting under its own AI (the gate
// released by combat.cpp), the drive must not push it: two locomotion writers on one body is the
// T070/T071 drift. The drive keeps MEASURING drift during the window; it stops CORRECTING it.
bool  SetPuppetNativeWindow(unsigned int uid, bool on);
// H016 - continuous reconciliation inside a native window: every frame the released copy is moved a
// small fraction of its error toward the authority's position, through the SAME two position writes
// the teleport makes (CharMovement's setter, then the animation node) but WITHOUT the halt, so the
// AI's own locomotion and animation keep running. Steps are capped so no single frame is a hop.
void  SetReconcile(bool on);
// Review 4 item 12: the ONE 'downed' predicate (prone OR ragdoll, fails closed) used by the peer's hop oracle
// AND the host's host_move record, so the two populations the verdict compares are classified alike.
bool  IsDownedCharacter(::Character* c);
void NoteProneWrite(unsigned int uid);   // PROBE P019: an authoritative prone write landed for this uid (vis_peak correlates)
double ProneWriteAgoSeconds(unsigned int uid, double now);   // PROBE P019: seconds since that write, -1 = none
struct ReconcileStats
{
    long long steps;      // -1 = no puppet row (NOT MEASURED), never a zero that reads as a pass
    float maxStep, sumStep, reqMax;
    float visMaxStep;     // largest 3D visible displacement per frame while windowed
    float visMaxRate;     // units/s, STANDING frames only - the judged number
    float visMaxRateDowned; // units/s on prone/ragdoll frames - the engine's throw, reported only
    float visMaxRateAny;  // units/s over all frames
    float layerGapMax;    // |CharMovement::pos - visible| XZ max, every windowed frame incl. prone/ragdoll
    float driftMean;      // mean drift over the window (steady state)
    long long satSteps, boostedSteps;   // H028: capped steps / boosted-rate steps this window
};
void  PuppetReconcileStats(unsigned int uid, ReconcileStats* out);


float PuppetWindowMaxDrift(unsigned int uid);        // horizontal, over the current/last window
float PuppetWindowDriftNow(unsigned int uid);        // live horizontal drift, -1 if unreadable
bool  PuppetAuthorityPos(unsigned int uid, float* x, float* y, float* z);   // the owner's last streamed spot; false if none
// Place the puppet at the authority's last streamed position (the catch-up teleport). Declines on
// ragdoll/prone - a placement that cannot work must decline, not fail (F324). Drift before/after
// are OUTPUTS so the caller can print what the snap actually did rather than that it was asked.
// Returns a REASON, not a bool (review finding 6): 0 = placed, 1 = no Puppet row, 2 = character
// unreadable, 3 = animation unreadable, 4 = movement unreadable, 5 = ragdoll, 6 = prone.
// `driftSameCall` is the readback in the SAME call - most likely the pre-teleport position, because
// the movement write reaches Character+0x48 on the next movement update (F314/F327). The number that
// means something is emitted on the NEXT DrivePuppet tick as a [VERDICT] snap_scored record.
int   SnapPuppetToAuthority(unsigned int uid, float* driftBefore, float* driftSameCall);
// SetPuppetNativeWindow returns false when there is no Puppet row for the uid (counted by caller).


// F350 / P-20 - `lead on|off`, DEFAULT ON. The authority's measured velocity now rides in the two
// MOVE floats that were always sent as zeros, and the peer aims at the sample carried forward by
// its age instead of at the sample itself.
//
// It fixes the jank the user reported by fixing its cause: the arrival deadband (1.5 units) is
// LARGER than the authority's advance between updates (1.455), so a correctly-tracking puppet was
// being explicitly stopped about five times a second. The stop is now caused by the authority
// being stopped.
//
// OFF reproduces the deployed behaviour exactly, so the run can measure the fix against its
// absence without a rebuild.
void SetLead(bool on);
// H021/H024 - `orderdrive on|off`, DEFAULT OFF: a STALLED copy (P037 progress window) is handed ONE Move order so the
// engine paths it around the obstacle, then the push resumes. The push is the primary drive (F431: orders are
// consumed at the AI's own cadence - seconds at 100+ puppets - so they cannot be the primary locomotion).
void SetOrderDrive(bool on);
// D1 (read-movement) - `pathdrive on|off`, DEFAULT ON (D1-c): a copy more than 6 units from its aim (or stalled one window)
// walks there by CharMovement::setDestination (0x6607E0, the engine's own pathfinder below the AI task layer); it stays on
// the path while its owner moves and pushes again only within 3 units once the owner has stopped 0.5 s (D1-c).
// D2 (read-stopgo): while the owner moves, the destination leads it ~1 s on its heading and is re-planned before the copy
// arrives (near end / turn > 30 deg / aim > 8 off line); once when the owner stops, the exact aim.
// OFF = the push plus the H024 unstick order, exactly as before.
void SetPathDrive(bool on);

// Drift + traffic report (commitment 5: divergence detection ships WITH the feature).
void ReportReplication();

// review-p3o H2 - drop every engine pointer this module caches (the puppet table's `Character*`s
// and the streamed character) before GameWorld::_clearAndDestroyGameWorldStuff frees them.
// It FORGETS rather than releases: no engine memory is touched, because the objects are about to
// be destroyed. MAIN THREAD, and only from the store's world-teardown hook.
// M8: the departed player's uids leave the moveForRetired store. MAIN THREAD.
void ReplicateForgetUids(const unsigned int* uids, int n);

void ReplicateWorldTeardown();

// P25 interior keep (owner 296 a / 315 a): for each copy of the other players' player-faction characters standing in a building,
// the engine's own load-if-needed-and-keep-alive call on that building's inside (BuildingInteriorKeep) - the copies are never put
// into the engine's player list, so nothing on this game's screen changes. MAIN THREAD, once per frame (GameWorld_gpuFrame).
void InteriorKeepTick();
std::string InteriorKeepToken();   // the p25Interior[...] counters (on the talk REPORT line)

} // namespace coop
