// appearance.h - PROBE P019 (read) and H010b (replicate the appearance RECORD).
//
// Written after F153/F155. T050 was the first time a human looked at two instances side by
// side, and the very first thing they saw was that the two copies of one uid were not the
// same person - a bearded man on A, a woman in a straw hat on B. Every number agreed;
// nothing here could see a face.
//
// T051 then measured it (F156): across four spawns from ONE template, sex differed 3/4,
// body mesh 3/4, hair 4/4, character height 4/4, clothing count 3/4 - while maxHealthBase,
// age, healthScale, hitWeight and placement were identical on both instances. The template
// resolves the same on both sides; what diverges is the per-instance appearance ROLL.
//
// T052 then refuted the obvious fix (F160). See appearance_record.h for what replaced it and
// why: the derived fields cannot be written, because the engine re-derives them.

#pragma once

#include <string>

class Character;

namespace coop {

struct RecordCopy;
struct GarmentSet;

// ---- P019: read -----------------------------------------------------------------------

// One-line description of what this character LOOKS like: sex, race, body mesh, hair,
// beard, physique, height, attachment count. Never throws, never faults - every field is
// guarded and prints "?" when unreadable. MAIN THREAD only (it allocates).
std::string AppearanceString(::Character* c);

// PROBE P021 - what the character is wearing and carrying, BY NAME, sorted.
// A count is not a diff: two characters can both wear four things and none of them the same.
// F165 left clothing matching 1/4 and this is the instrument that has to precede the fix.
std::string GearString(::Character* c);

// `look <uid>` - print P019 and P021 for one replicated character.
bool ReportAppearance(unsigned int uid);

// F157, and it is the reason the record is not sent inline with the SPAWN message: at the
// moment the spawn path returns, the appearance has NOT been applied. The values read as
// defaults (body='', hair='-', height=1.00) while `isCreatingBody` is already 0. The flags
// that actually mean "not yet applied" are updatedAttachments and updatedAppearanceData.
// True once BOTH have cleared and the roll is final.
bool AppearanceSettled(::Character* c);

// ---- T240: can the engine ORIENT this body? -------------------------------------------

// What AppearanceBodyChain found. 0..5 are the reasons a hit must NOT be played and are
// the index of the [M3] skip counters, so their order and their count are load-bearing;
// 6 and 7 are the two ways a body IS playable. AppearanceBodyChain itself never answers 5 -
// combat.cpp assigns it when the ATTACKER's chain fails (C1-b, review 2026-09-22 H4).
enum HitBodyReason
{
    kHitNoAnim                = 0,  // Character +0x448 is not a plausible AnimationClass
    kHitNoAppearance          = 1,  // AnimationClass +0x0E8 is not a plausible appearance
    kHitAppearancePending     = 2,  // updatedAttachments / updatedAppearanceData still set
    kHitNoEntity              = 3,  // appearance +0x0D8 is null or not an object in OgreMain_x64.dll
    kHitDetached              = 4,  // Ogre's getParentSceneNode(entity) returned 0 (or that
                                    //   export could not be resolved - logged once)
    kHitAttackerNotOrientable = 5,  // the victim's gate is SET, so addWound also orients the
                                    //   ATTACKER, ungated - and the attacker's chain is not attached
    kHitBodySkipCount         = 6,  // how many SKIP reasons there are - the counter array's size
    kHitBodyAttached          = 6,  // playable: the entity is attached to a scene node
    kHitBodyNoNodeRead        = 7   // playable: the engine's own gate is 0, so it reads no node
};

// Can the engine orient this character's body? 1 = yes, 0 = no. *reasonOut always receives a
// HitBodyReason (never kHitAttackerNotOrientable); animOut / appOut / entOut receive each hop
// that could be read and stay 0 otherwise, for the log; *gateOut receives the AnimationClass
// +0x88 byte as 0/1 (0 when the AnimationClass could not be read). Any out-pointer may be 0.
// honourGate = true is the VICTIM's question: a 0 gate means addWound reads no node, so the
// body is playable (kHitBodyNoNodeRead). honourGate = false is the ATTACKER's question: addWound
// reads the attacker's node whenever the VICTIM's gate is set, whatever the attacker's own byte
// says. Never throws, never faults. See appearance.cpp for the reads.
int AppearanceBodyChain(::Character* c, bool honourGate, void** animOut, void** appOut,
                        void** entOut, int* reasonOut, int* gateOut);

// The victim's question: AppearanceBodyChain with honourGate = true.
int AppearanceBodyAttached(::Character* c, void** animOut, void** appOut, void** entOut,
                           int* reasonOut, int* gateOut);

// ---- R1-a: has the engine FINISHED BUILDING this character? --------------------------

// Why CharacterBuilt answered as it did. kCharBuilt is the only "yes".
enum CharacterBuiltReason
{
    kCharBuilt              = 0,
    kNotBuiltNoChain        = 1,  // AppearanceBodyChain(honourGate = false) is not attached
    kNotBuiltCreating       = 2,  // appearance +0xE8 isCreatingBody != 0 (a rebuild is in flight)
    kNotBuiltGate           = 3,  // AnimationClass +0x88 != 1 (the engine's "body set" byte)
    kNotBuiltMismatch       = 4   // anim +0xA8 != appearance +0xD8, or appearance +0x138 != anim
};

// 1 = the engine has finished building this character's body, so an engine call that walks
// +0x448 -> +0xE8 -> +0xD8 -> node cannot find a hop missing; 0 = not yet. The bug class is
// "acting on a character before the game has finished building it" (T240). Live read at the
// moment of the call - no pure-decision header, because the whole predicate is engine memory.
// *reasonOut (optional) receives a CharacterBuiltReason. Never throws, never faults.
// MAIN THREAD. Offsets and reasoning: build/read-r1.md Q3/Q4 (Read, not Confirmed live).
int CharacterBuilt(::Character* c, int* reasonOut);

// deadlook1 (review-deadlook1 fold, fold 3). MAIN THREAD. An owner item move was APPLIED to this copy (items.cpp
// ApplyItemMove) in `section`: when that is a worn section and the copy is dead (or its dead flag is unreadable), the
// copy counts as LOOTED and is never re-dressed.
void DeadlookNoteItemTouch(unsigned int uid, const std::string& section);
// T-293 fold 1 (review F1). MAIN THREAD (items.cpp ApplyItemMove, before the move is tried). The owner added, removed or split an
// item in `section` of its character `uid` (op 0/1/3): when that is a worn section, a clothing list still waiting for this copy is
// older than the move and is dropped - applied or not, dead or alive.
void GarmentsNoteOwnerMove(unsigned int uid, const std::string& section, int op);
// MAIN THREAD. The copy is removed (spawn.cpp RemoveLocalCopy): forget its dead-copy marks (review D3a) and its createBody-guard
// marks (T-293 fold 2).
void DeadlookForgetCopy(unsigned int uid);
// MAIN THREAD (spawn.cpp RepeatSpawnAct): a stale copy is made again - the old body's 'owner's record applied' mark is forgotten.
void AppearanceForgetCopyBody(unsigned int uid);
// MAIN THREAD (off it: counted, never marked). Called right after a mod apply that may queue a body rebuild on
// `uid` (look, kit, limb): a set rebuild-pending byte marks the uid, and the createBody guard holds that rebuild in ragdoll
// even if the uid becomes this game's own before it runs.
void CopyBodyNoteModApply(unsigned int uid, ::Character* c);
// MAIN THREAD (spawn.cpp SpawnWorldTeardown): the world is going - every copy's marks DeadlookForgetCopy forgets one copy at a
// time are forgotten at once, so a copy met in the next world with the same uid starts with none (its first look is its own).
void DeadlookForgetAllCopies();

// R1-a / R1-a-b (review-r1a LOW-3): cumulative count of remote APPEARANCE / CLOTHING entries that
// ENTERED a wait because the character was settled but not yet built - once per uid per wait, not
// once per frame. For the [M1] REPORT line.
long long AppearanceWaitNotBuiltCount();

// R1-a-b (review-r1a M3): cumulative count of remote CLOTHING entries that entered a wait because
// the same uid still had a remote APPEARANCE entry pending (once per uid per wait). [M1] REPORT.
long long ClothingWaitAppearanceCount();

// ---- R1-a-b: may the PRONE write run on this character now? ---------------------------

// Why CharacterProneSafe answered as it did. kProneSafe is the only "yes".
enum CharacterProneSafeReason
{
    kProneSafe               = 0,
    kProneUnsafeChar         = 1,  // the Character itself is not an object
    kProneUnsafeAnim         = 2,  // Character +0x448 (AnimationClass) is not an object
    kProneUnsafeAppearance   = 3,  // AnimationClass +0xE8 (AppearanceBase) is not an object
    kProneUnsafeMovement     = 4,  // Character +0x640 (CharMovement) is not an object
    kProneUnsafeAi           = 5,  // Character +0x650 (AI) is not an object
    kProneUnsafeMovementObj  = 6,  // CharMovement +0x3B0 set, but +0x3A8 is not readable
    kProneUnsafeNameTag      = 7   // Character +0x638 non-null but not readable
};

// 1 = every pointer setProneState (0x5C7390) and its callees 0x6E8BA0 / 0x660FE0 dereference is
// there, so `setPoseState` cannot fault on this character; 0 = not yet (review-r1a M1). It
// deliberately does NOT ask for the body or its scene node - the prone path never reads them - so
// a character with no visible body gets its knockdown at once. CharacterBuilt (strict) stays the
// rule for APPEARANCE and CLOTHING. Live read; never throws, never faults. MAIN THREAD.
// Sources: build/decomp_5c7390.txt, decomp_6e8ba0.txt, decomp_660fe0.txt (Read, not Confirmed live).
int CharacterProneSafe(::Character* c, int* reasonOut);

// crash1 (T293 / F912): may this copy be knocked down (a prone write of 2..4, or K2's ragdoll) now? 0 = yes; 1 = no, a
// body rebuild is queued or its body is being created (the rebuild would run on the limp body - the T293 crash); 2 = not
// yet, the owner's appearance record or worn items for it still wait to be applied (they wait while it lies down, so it
// is better dressed first; callers bound this wait). The appearance and clothing applies wait while a copy lies down
// or is limp. MAIN THREAD.
int KnockdownWait(unsigned int uid, ::Character* c);
// The knockdown hold for this copy gave up waiting for its looks (spawn.cpp KnockdownMustWait). Counted lookKoArrival gaveUp
// when the owner says knocked out and the copy's first look is still pending. MAIN THREAD.
void NoteKnockLooksGaveUp(unsigned int uid);

// crash1: 1 = the copy lies down (PoseState 2..4), is limp, or is about to be (its `unconcious` byte or wake-up clock is
// set); 0 = standing; -1 = unreadable (treat as down). MAIN THREAD.
int CopyDowned(::Character* c);
int CopyLimpPod(::Character* c);   // crash1b: actually limp (PoseState 2..4 or ragdoll) - not merely flagged unconscious
// crash2: the animal copy body-rebuild guard - a detour on AppearanceAnimal::createBody 0x539C50. On the main thread it
// skips the rebuild of a replicated copy we do not own while it lies in ragdoll; the engine retries each update.
void InstallAnimalCreateBodyGuard();
// T-293 fold 1 (review F2): the same guard on AppearanceHuman::createBody (row AppearanceHumanCreateBody), counted separately.
void InstallHumanCreateBodyGuard();
// PROBE-START: P091 - copy createBody / knockdown gate state at the crash precondition (read and log only)
// MAIN THREAD, from KnockdownMustWait: the branch that decides (-1 limpEarly, 0 none, 1 wait1, 2 wait2).
void P091NoteKnock(unsigned int uid, ::Character* c, int branch);
// PROBE-END: P091
// crash1c (review-crash1b R2c): 1 = a body rebuild is queued on the copy or its body is being created (the updAttach /
// updApp / isCreating flags KnockdownWait 1 reads); 0 = none. SEH-guarded; a fault reads as 1. ANY THREAD.
// T-293 fold 2 (F2-A): 0 also for a limp copy whose createBody the guard holds now (+0x143 set, held) - the engine's update
// returns at +0x143, so no step of that rebuild runs on the limp body; counted copyBodyHeld[gateHeldPassed].
int CopyRebuildInFlightAny(::Character* c);

// ---- H010b: replicate -----------------------------------------------------------------

// Main-pump tick. Drives both halves without a timer (the project's standing rule: events
// over timers, live reads at the moment of commitment):
//   * a uid we OWN whose roll has settled -> capture its appearance record and send it once
//   * a uid we do NOT own with a record waiting -> apply it once ITS OWN appearance has
//     settled, so the engine's pending update cannot re-derive on top of us, AND (R1-a) once
//     CharacterBuilt says its body is fully built
void AppearanceTick();
// M7a (T-197 piece 7a): the catch-up's looks - an owned character's appearance record, then its clothing, captured now and sent
// (the caller has addressed the stream, net::CharStreamToSlot). false = not sent: unknown, or its roll has not settled (its
// settle sends it). MAIN THREAD.
bool AppearanceResendOwned(unsigned int uid);

// Queue an inbound record for a uid we do not own. Applied by the tick, not here.
void QueueRemoteRecord(unsigned int uid, const RecordCopy& rec);

// Queue the authority's worn items. Applied by the tick AFTER that uid's appearance record -
// applying the appearance rebuilds attachments, so clothing written first would be undone.
void QueueRemoteGarments(unsigned int uid, const GarmentSet& set);

// Register a locally-owned uid whose record should be captured and sent once it settles.
void WatchLocalRoll(unsigned int uid);

// Counters for the [P019] REPORT line: nothing here is allowed to read zero without an
// explanation (the `puppets=0` lesson, F155).
void ReportAppearanceCounters();

// P10 TEST-ONLY lever: `bodydown <anchorUid> [radiusM]` (appearance.cpp). MAIN THREAD. Returns the command's status line.
// P10 fold 1 (T633): only animals (AppearanceAnimal), radiusM in metres 1..5000 (the verb's default 300).
std::string BodyDownLever(unsigned int anchorUid, float radiusM);
// P11 fold 1 (T652) TEST-ONLY lever: `koself <uid> [seconds]` (appearance.cpp) - the game that drives <uid> knocks its OWN
// character out through KnockOutOwned; seconds > 0 holds the wake-up clock at least that long. MAIN THREAD. Returns the
// command's status line.
std::string KoSelfLever(unsigned int uid, float seconds);
// looktest <uid> / looktest restore <uid> - TEST-ONLY lever (appearance.cpp): the driven character's look alternates between its
// original + 0.1 and its original (restore: back to the original) and is sent to the other games at once; its own apply waits
// until it stands out of ragdoll and its get-up blend has ended. MAIN THREAD.
std::string LookTestLever(unsigned int uid, bool restore);
// looktest show <uid> - the lever's read half, any game: the character's look as this game shows it. MAIN THREAD.
std::string LookTestShow(unsigned int uid);
// bodytest pending <copyUid> - TEST-ONLY lever (appearance.cpp): forces a copy's queued body rebuild into the engine's ragdoll
// pass at the end of its get-up blend (the window the copy body-rebuild guard holds). MAIN THREAD.
std::string BodyTestLever(const std::string& sub, unsigned int uid);
// The K2 detour around the engine's ragdoll pass (ThreadSafeRagdollUpdates): RagdollPassEnter returns the depth that
// RagdollPassLeave restores (-1 off the main thread, counted). While the main thread is inside the pass no copy's body is rebuilt.
long RagdollPassEnter();
void RagdollPassLeave(long saved);
// The last `koself` target on this game (0 = none): capturetest's SUMMARY koSource. MAIN THREAD.
unsigned int KoSelfLastUid();

// ground5 fold 3 (T635): the animaldrop lever's animal test (appearance.cpp; items.cpp declares them too). MAIN THREAD.
// 1 = an animal (the appearance vtable holds AppearanceAnimal::createBody 0x539C50), 0 = another class, -1 = unreadable.
int GroundAnimalAppPod(::Character* c);
std::string GroundRaceName(::Character* c);   // the appearance's race record name, "?" = unreadable

} // namespace coop
