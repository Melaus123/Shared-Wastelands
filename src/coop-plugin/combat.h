// combat.h - M3 HIT: intercept damage application; suppress it on puppets, replicate it
// from the authority.
#pragma once

#include "identity.h"

namespace coop {

// Install the damage-chokepoint hook (Character::hitByMeleeAttack, F083).
void InstallCombatHook();

// P045 / T095 - install the counter on CombatClassAI::update (0x60CC10), the function that
// dispatches STARTUP. Separate from InstallCombatHook so its failure is reported on its own line
// and does not take the damage hook down with it.
void InstallCombatUpdateProbe();

// P058 / F399 - install the return-address recorder on CharBody::finishAction (0x5C5CE0).
// That function has NO direct callers, so the caller's return address names which code path removed
// a current action - the discriminator F399 leaves the workstream on. Its own entry point for the
// same reason as the one above: an instrument that fails to arm must say so on its own line rather
// than print a zero that reads like an answer.
// **Must be called AFTER InstallCombatUpdateProbe**, which caches the image base the recorder needs
// to turn a return address into an RVA (it re-reads the base defensively, but the ordering is the
// contract).
void InstallEndActionProbe();

// Apply an inbound HIT: the authority's already-resolved damage, applied verbatim to our
// copy so per-limb health matches exactly rather than being recomputed.
// F117: the attacker uid is REQUIRED, not decorative. The engine dereferences the attacker
// unconditionally, so a hit with no resolvable local attacker is dropped and counted rather
// than applied - applying it crashes the instance outright (T033).
// F119: the peer still PLAYS the hit through the engine (animation, stagger, reaction - the
// visible event must be indistinguishable from a natural one), then the authority's
// per-limb values are written over the engine's own roll in the same frame.
//
// PARITY (register P-2): when the attacker is NOT one of our replicated characters it has
// no uid, and until now that meant no visual at all on the peer. `attackerId` carries the
// engine's own cross-process name for it (see identity.h) so an ambient NPC that exists on
// both machines can be found here and the hit played for real. It is a second chance at the
// attacker, never a substitute for one: if it does not resolve, the old silent path stands.
void ApplyRemoteHit(unsigned int victimUid, unsigned int attackerUid, int cutDirection,
                    const char* damages24, int comboId,
                    const float* parts, int partCount, float blood,
                    const ObjId& attackerId, bool knock);

// P104 fix (protocol 104) - A CROSSBOW BOLT THAT STRIKES THIS GAME'S CHARACTER ON THE OTHER GAME IS PLAYED HERE.
// session.cpp OnShot hands every MSG_SHOT here (MAIN THREAD): refusal 0 = we drive the victim and the sender drives the shooter;
// 1 = the victim is not ours, 2 = the sender does not drive the shooter, 3 = it did not decode (uids 0, rec4 0). Counted and
// logged here; accepted ones are queued for ShotSafePointDrain.
void ShotOnNet(unsigned int victimUid, unsigned int shooterUid, const float* rec4, bool onPurpose, int refusal);
// M7a (T-197 piece 7a): the catch-up's combat mode - an owned character whose last sampled edge was ON against a replicated
// target says so again (the caller has addressed the stream, net::CharStreamToSlot). false = not fighting / not sent. MAIN THREAD.
bool CombatModeResendOwned(unsigned int uid);
// K2 safe point (combat.cpp detour_tsRagdollUpdates, MAIN THREAD, worker paused): each queued MSG_SHOT through the engine's
// MedicalSystem::addWound on our own character - wounds, damage, blood, knock-out and death are the engine's.
void ShotSafePointDrain();

// K2 (decision 61 follow-up) - a copy's fall starts on the SAME hit that knocked its owner down.
// Owner side: medical.cpp's applyDamage detour calls this, on whatever thread applies the hit (the AI
// worker), when a character this game drives went from conscious to unconscious (+0x161 0 -> 1) inside
// the call - applyDamage 0x64E870 queues the engine's own fall there. The hit hook armed a slot for its
// thread before running the original; this marks it. No lock, no allocation.
void NoteKnockEdgeAnyThread(const void* character);

// K2: hook GameWorld::threadSafeRagdollUpdates 0x7D17E0 - the ONE place the engine drains every
// character's ragdoll request queue (Character +0x3E0, no lock), called from the main loop at 0x7880B9
// while the worker thread is paused. A copy's knock is applied there, never from our per-frame code.
void InstallKnockSafePoint();

// T-211 / H054: one exclusive lock around GameWorld::getObjectsWithinBox 0x7857A0 (the AI worker's combat
// attack-zone scan reads PhysX shapes) and PhysicsActual::_destroy 0x7DB0E0 (the physics thread releases a
// knocked-down character's old hull actor), so a query never reads a shape whose actor is being released.
// Installed by InstallCombatHook, on by default; ReportPhysicsGuard prints the [H054] REPORT line.
void InstallPhysicsGuard();
void ReportPhysicsGuard();

// P-15 / F220 - COMBAT MODE replication.
//
// The user reported that on the peer, characters move to roughly the right places but show no
// combat animations and no combat marker on their portraits. T064 measured it: the peer's
// `combatModeActive` read 0 in all 115 samples while the authority cycled through 4-6 combat
// states. T066 then settled WHY it was stuck, by counting calls rather than reading state -
// the peer's `CombatClass::periodicUpdate` ran 22,625 times for our own characters. **The
// controller is running. It is simply never told to fight.**
//
// So this replicates the CAUSE and not the appearance: when the authority enters combat, the
// peer is told to enter combat against the same target, and the peer's own engine produces the
// stance, the animations and the portrait state. Nothing about animation crosses the wire.
//
// MAIN THREAD ONLY - it calls into the engine's combat object.
void CombatModeTick();
void ApplyRemoteCombatMode(unsigned int uid, bool on, unsigned int targetUid);
void ReportCombatMode();

// F233 - let a suppressed puppet have one decision pass on a cadence WHILE IT IS IN COMBAT
// MODE, so its combat state machine can advance. ON by default. Switchable so a run can
// measure the fix against its own absence without a rebuild.
void SetCombatPulse(bool on);

// F348 - SWING REPLICATION. The half of P-15 that combat mode never covered: the user reported
// characters standing in the right places and not swinging, and combat mode is a level that says
// a fight is on, not an event that says a blow was thrown.
//
// The authority sends the transition into the engine's attack state; the peer writes the same
// three fields the engine itself writes to begin a blow (`nextMove = SWORD_SWING`,
// `combatState = STARTUP`, `setTarget`) and its own engine picks the technique, plays
// the animation and resolves the impact. Nothing about animation crosses the wire.
//
// **The AI decision gate is not opened.** That is what makes this a different approach from the
// three recorded dead ends rather than a fourth variant of them: F348 read the engine and found
// `decisionState` cannot start a swing at all, so a swing needs no decision.
//
// MAIN THREAD ONLY.
void ApplyRemoteSwing(unsigned int uid, unsigned int targetUid);

// ON by default. `swingrepl off` restores the previous behaviour in one command, so the run can
// measure the fix against its own absence without a rebuild.
void SetSwingReplication(bool on);

// F384 / P055 - install a TASK_MELEE_FOCUSED Task directly as a puppet's current action, with no
// AI decision pass. Default OFF; see the switch in combat.cpp for why.
void SetTaskInstall(bool on);
// H015 / P059 - native combat window: on the authority's combat-ON edge the puppet's AI is released
// with the authority's attack order queued; on the OFF edge it is re-gated, its orders and goals
// cleared, and it is placed at the authority's position. Default ON since combat1 (user decision 2026-09-25); `nativecombat on|off`.
void SetNativeCombat(bool on);
// P059 - replicate.cpp's DropPuppet tells the watch row its window is gone (regated = it could
// re-gate the AI before forgetting the character).
void NoteNativeWindowDropped(unsigned int uid, bool regated);

void SetWeaponTask(bool on);

// F251 - order injection, OFF by default. Three variants were built and measured and all three
// left the peer permanently autonomous; a gated puppet swings zero times, and one decision pass
// is enough to change that for good. Kept switchable for whoever tests a different approach.
void SetCombatOrders(bool on);

// Drain the outbound hit queue the AI worker thread fills. MAIN THREAD ONLY (T024/F095).
void CombatTick();

// Counters (extends the report command).
void ReportCombat();

// review-p3o H2 - drop every engine address this module caches (the combat watch rows, the
// combat-object update slots and the end-of-action body ring) before
// GameWorld::_clearAndDestroyGameWorldStuff frees them. The two tables the combat worker reads are
// published with interlocked stores; the update slots' monotonic `calls` are deliberately left
// alone. MAIN THREAD, and only from the store's world-teardown hook.
void CombatWorldTeardown();

// P1 (read-parity3 GAP 1): an ANIMAL's age 0..1, the float at CharacterAnimal +0x700 (getAge0to1 0x5E1580
// returns it). The animal test is WeaponInHandsOf's getter offset (+0x708 = CharacterAnimal, F-verified
// against isAnimal 0x5E14C0). Returns false - and writes 0.0 - for a human, a null, or anything unreadable.
// The value is returned RAW; the caller clamps.
bool ReadAnimalAge01(const void* c, float* out);

} // namespace coop
