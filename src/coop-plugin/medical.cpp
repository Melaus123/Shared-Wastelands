// medical.cpp - M4 STATE, step 1: MEASUREMENT ONLY.
//
// This file deliberately changes NO behaviour. It exists to answer three questions that
// each change what M4 should build, before any of it is built (docs/m4-state-plan.md):
//
//   1. What THREAD does the medical tick run on? Damage resolves on the AI/locomotion
//      worker rather than the main loop (F095), and assuming otherwise already caused one
//      real defect - an inline network send from a hostile thread. So the detour below
//      obeys hostile-thread rules (no allocation, no locks, interlocked counters only) and
//      the answer is REPORTED from the main thread, never logged from inside.
//
//   2. How fast does an UNGATED puppet's medical state drift? F122 measured 0.6 units over
//      62 s from a single sample. A rate over a longer window sizes the whole problem, and
//      it is the control that a later gated run has to beat.
//
//   3. Does KO ALREADY follow on the puppet from the per-limb health we replicate today?
//      If the engine derives POSE_KNOCKED_OUT from health, M4's primary acceptance criterion may
//      already be met with no code at all. Nearly free to ask, expensive to skip - three
//      M3 runs went into building toward something the criterion never required (F113).
//
// Target, vetted in Ghidra (F123):
//   MedicalSystem::medicalUpdate(float)  RVA 0x651570
// It calls BOTH bloodlossUpdate (0x6456B0) and HealthPartStatus::update (0x64EB30), so it
// is the single per-frame driver - one function to gate later, not two systems to chase.
// Header-comment RVAs are SHIFTED in this build and land mid-function; never use them.
//
// Ownership: MedicalSystem is an inline member at Character+0x458, so the owning character
// is (Character*)((char*)medical - 0x458) - the same offset already used in both directions
// by the health readout.

#include "medical.h"
#include "combat.h"   // K2: NoteKnockEdgeAnyThread - the knock edge hand-off to the hit hook
#include "stats.h"   // S1: the stats counters ride the [P014] REPORT line
#include "worldsync.h"   // H030: AnnouncedToPeer
#include "spawn.h"
#include "net/session.h"
#include "addresses.h"   // C2-b: kApplyDamageRva / kApplyDamageHitRetRva come from the address table
#include "appearance.h"   // crash1c (R2c): CopyRebuildInFlightAny
#include "clothing.h"     // LIMBS: LimbsOfOwned (the limb block of every STATE), InstallCopyAmputateGate
#include "tags.h"         // tags1: the [TAGS] REPORT line rides the report
#include "store.h"        // par6 (parity P6): StoreDeclareDeadPod - Character::declareDead 0x7A5660 under SEH, MAIN THREAD
#include "../common/spawnage.h"     // T-303: kSpawnFlagDead / kSpawnFlagKo - the SPAWN's owner flags
#include "../common/prisonwire.h"   // par6 fold (review-par6 #1): MSG_PRISON kind 4 DEATH - the death request to the owner
#include "../common/uidslots.h"     // the per-copy word tables' slot bookkeeping (hunger, knocked out, dead)
#include <intrin.h>        // C2-b: _ReturnAddress - which caller is applying the damage

#include "coop_log.h"
#include "game/Character.h"
#include "game/MedicalSystem.h"
#include "game/GameWorld.h"   /* T-306 lever (destroybody): GameWorld::destroy 0x798F50 */
#include <stdint.h>   /* mig3: what <core/Functions.h> also brought in */
/* These hook targets are our own address-table rows (coop::AddrAbs gives image base + RVA, or 0 when the
   row is unbound, which each call site below guards). */
static unsigned long long kMig3StartKnockoutTimer = 0; static coop::AddrReg kMig3StartKnockoutTimer_reg("MedicalSystem_beginKnockout", &kMig3StartKnockoutTimer);   /* Steam_1.0.65 0x643D40 */
static unsigned long long kMig3Knockout = 0; static coop::AddrReg kMig3Knockout_reg("MedicalSystem_knockout", &kMig3Knockout);   /* Steam_1.0.65 0x643EF0 */
static unsigned long long kMig3MedicalUpdate = 0; static coop::AddrReg kMig3MedicalUpdate_reg("MedicalSystem_healthTick", &kMig3MedicalUpdate);   /* Steam_1.0.65 0x651570 */
#include "hooks.h"   /* mig1: coop::AddHook (own MinHook) */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <sstream>
#include <locale>
#include <map>   // T-303: the SPAWN deaths still to land
#include "playerfaction.h"   /* re-check par6 #1: IsPlayerFaction - an unequip kill only happens to a non-player character */

namespace coop {

namespace {

// MedicalSystem sits at this offset inside Character (class layout, not an RVA, so it is
// not subject to the F020 shift).
const size_t kMedicalOffset = 0x458;
// H032 (T133 O4 = 2.53): StateTick used to push ONE owned character per second, round-robin - with ~130 owned
// characters each copy was corrected every 2-3 minutes, and the peer's own bleed-out (H012, ~1.5 pts / 45 s) had that
// long to drift. Every owned character is now refreshed within kStateRefreshSec (<= 1 KB per STATE; 130 characters =
// ~26 KB/s on the unreliable channel), capped per tick.
const int kStateRefreshSec = 5;
const int kStateMaxPerTick = 40;
volatile LONG64 g_statePerTick = 0;   // the last tick's batch size (report)

typedef void (*MedUpdateFn)(MedicalSystem*, float);
MedUpdateFn orig_medicalUpdate = 0;

volatile LONG64 g_medCalls      = 0;   // every character in the loaded world
volatile LONG64 g_medCallsOurs  = 0;   // characters we own
volatile LONG64 g_medCallsTheirs = 0;  // replicated characters owned by the peer
volatile LONG   g_medThreadId   = 0;
volatile LONG64 g_stateSent     = 0;   // M4 authoritative snapshots pushed, all causes
volatile LONG64 g_statePushed   = 0;   // of those, the EVENT-DRIVEN ones (F140)

// H012: the medical gate. OFF by default so the first run can measure the SAME fight with it
// off and then on - a controlled comparison in one session beats two sessions and a hope that
// the fights were comparable.
volatile LONG   g_medGate       = 0;
volatile LONG64 g_medGated      = 0;   // ticks actually suppressed

// Local guard: range + alignment + a readable first qword. spawn.cpp has its own copy in an
// anonymous namespace; duplicating six lines beats exporting a guard across modules.
bool PlausiblePtr(const void* p)
{
    uintptr_t v = (uintptr_t)p;
    if (v < 0x10000 || v >= 0x00007FFFFFFFFFFFull) return false;
    if (v & 0x7) return false;
    __try { volatile uintptr_t probe = *(uintptr_t*)v; (void)probe; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

std::string N(long long v)
{
    std::stringstream ss; ss.imbue(std::locale::classic()); ss << v; return ss.str();
}

std::string F2(float v)
{
    std::stringstream ss; ss.imbue(std::locale::classic());
    ss.setf(std::ios::fixed); ss.precision(2); ss << v; return ss.str();
}

// THE PER-COPY WORD TABLES - the owner's hunger, its "knocked out" and its "dead" per copy uid, read lock-free by the medical,
// knockout and death detours on the AI worker; the main thread is the one writer. The slot scheme is src/common/uidslots.h. A
// uid's slot is released when its copy is removed here (MedicalForgetCopy, from spawn.cpp's removal paths), when its ownership
// moves to or from this game (net/session.cpp), and at world teardown and session end (MedicalForgetAllCopies) - so each table
// holds only the copies present now, and a value from a character's earlier stay here is never read again. The fixed size is a
// safety net only: an insert that finds no slot is counted (*TableFull).
// Order: a release zeroes the slot's values first, then makes its key a tombstone; a claim writes the key first, then the
// values. So a reader that matched a key reads that uid's values or zeros ("not carried", "not knocked out", "not dead" - each
// table's conservative answer), and it re-reads the key after the values to drop a slot released or reused while it read.
volatile LONG64 g_copyWordsForgotten = 0;   // slots released (copy removed, ownership moved); teardown clears are not counted

template <class S> struct SlotKeys
{
    const S* t;
    unsigned int operator()(int i) const { return (unsigned int)t[i].uid; }
};

// ANY THREAD. The slot holding uid, or -1.
template <class S> int SlotOf(const S* t, int slots, int probe, unsigned int uid)
{
    SlotKeys<S> k; k.t = t;
    return uidslot::Find(k, slots - 1, probe, uid);
}

// MAIN THREAD. uid's slot, claimed when new (its values are 0: zeroed at release, or never written); -1 = no slot, which the
// caller counts. The claim is a compare-exchange on the key the walk saw, so a second writer racing for that slot walks again.
template <class S> int SlotClaim(S* t, int slots, int probe, unsigned int uid)
{
    SlotKeys<S> k; k.t = t;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        int existing = -1;
        const int at = uidslot::InsertAt(k, slots - 1, probe, uid, &existing);
        if (at < 0 || existing >= 0) return at;
        const LONG seen = (LONG)k(at);
        if (InterlockedCompareExchange(&t[at].uid, (LONG)uid, seen) == seen) return at;
    }
    return -1;
}

// MAIN THREAD. Release uid's slot: its values to 0, then the tombstone, then every tombstone that now ends a run goes back to
// empty (walking backwards). Returns 1 = released, 0 = the table held nothing for uid.
template <class S> int SlotRelease(S* t, int slots, int probe, unsigned int uid)
{
    const int s = SlotOf(t, slots, probe, uid);
    if (s < 0) return 0;
    t[s].ClearValues();
    InterlockedExchange(&t[s].uid, (LONG)uidslot::kTomb);
    SlotKeys<S> k; k.t = t;
    int i = s;
    for (int n = 0; n < slots && uidslot::Trimmable(k, slots - 1, i); ++n)
    {
        InterlockedExchange(&t[i].uid, (LONG)uidslot::kEmpty);
        i = (i - 1) & (slots - 1);
    }
    InterlockedIncrement64(&g_copyWordsForgotten);
    return 1;
}

// MAIN THREAD. Every slot back to never-used (world teardown, session end), per slot values first, then the key. A reader
// probing meanwhile can miss an entry not yet cleared - a conservative answer, and the copies are going.
template <class S> void SlotClearAll(S* t, int slots)
{
    for (int i = 0; i < slots; ++i)
    {
        if (t[i].uid == 0) continue;
        t[i].ClearValues();
        InterlockedExchange(&t[i].uid, (LONG)uidslot::kEmpty);
    }
}

// par5 (parity P5; parity-audit-characters row 28) - A COPY'S HUNGER IS ITS OWNER'S.
//
// Hunger (MedicalSystem::hunger +0x60) and `fed` (+0x64) never travelled: each copy ran its own hunger clock inside
// its own medicalUpdate (the H012 gate is off by default, so that tick runs) and never ate, so a long-lived copy could
// be knocked out by hunger (reassessCollapseMode -> isHungerKO, which the C2 clock gate does not see) or starve
// (hunger < 0 in medicalUpdate, F973) on one screen only. The owner now reads both into every STATE; the copy writes
// them on arrival (ApplyOwnerHunger) and, because its own tick keeps counting down between STATEs, puts them back
// BEFORE each of its own medicalUpdate calls (HoldOwnerHunger). The copy's hunger then moves only with the owner's.
//
// DETOUR-THREAD RULES (F124): HoldOwnerHunger runs on the AI worker - fixed array, interlocked counters, no lock, no
// allocation, no log. A slot lives as long as the copy (the per-copy word tables above).
const int kOwnerHungerSlots = 4096;   // a power of two
const int kOwnerHungerProbe = 64;     // a lookup gives up after this many slots, so a crowded table never costs a 4096-slot walk per medical tick
struct OwnerHungerSlot
{
    volatile LONG uid; volatile LONG has; volatile float hunger; volatile float fed;
    void ClearValues() { InterlockedExchange(&has, 0); hunger = 0.0f; fed = 0.0f; }
};
OwnerHungerSlot g_ownerHunger[kOwnerHungerSlots];
volatile LONG64 g_hungerCarried   = 0;   // owner: STATEs built with its hunger
volatile LONG64 g_hungerApplied   = 0;   // copy: owner hunger written on STATE arrival
volatile LONG64 g_hungerHeld      = 0;   // copy: its own tick had moved hunger / fed and the hold put the owner's back
volatile LONG64 g_hungerAbsent    = 0;   // copy: a STATE that carried no hunger
volatile LONG64 g_hungerRefused   = 0;   // copy: implausible values, or a STATE for a character this game drives
volatile LONG64 g_hungerTableFull = 0;

void NoteOwnerHunger(unsigned int uid, float hunger, float fed)
{
    if (uid == 0) return;
    const int at = SlotClaim(g_ownerHunger, kOwnerHungerSlots, kOwnerHungerProbe, uid);
    if (at < 0) { InterlockedIncrement64(&g_hungerTableFull); return; }
    OwnerHungerSlot& s = g_ownerHunger[at];
    s.hunger = hunger;
    s.fed = fed;
    InterlockedExchange(&s.has, 1);
}

bool OwnerHunger(unsigned int uid, float* hunger, float* fed)
{
    const int at = SlotOf(g_ownerHunger, kOwnerHungerSlots, kOwnerHungerProbe, uid);
    if (at < 0) return false;                     // the owner sent no hunger for this copy (or the copy was removed)
    const OwnerHungerSlot& s = g_ownerHunger[at];
    if (s.has == 0) return false;
    const float h = s.hunger, f = s.fed;
    if (s.has == 0 || (unsigned int)s.uid != uid) return false; // released (has goes to 0 before the values) or reused while it was read
    *hunger = h;
    *fed = f;
    return true;
}

// AI WORKER (the medical detour). A copy only; the caller has already decided that.
void HoldOwnerHunger(unsigned int uid, MedicalSystem* self)
{
    float h = 0.0f, f = 0.0f;
    if (!OwnerHunger(uid, &h, &f)) return;       // nothing from the owner yet: the engine's own value stands
    if (self->hunger != h || self->fed != f)
    {
        self->hunger = h;
        self->fed = f;
        InterlockedIncrement64(&g_hungerHeld);
    }
}

// Owner side, MAIN THREAD: {hunger, fed} of a character this game drives into hg[2]; 0 = not readable (not carried).
const float* HungerOfOwned(unsigned int uid, float* hg)
{
    ::Character* c = FindSpawned(uid);
    if (c == 0) return 0;
    MedicalSystem* med = (MedicalSystem*)((char*)c + kMedicalOffset);
    if (!PlausiblePtr(med)) return 0;
    hg[0] = med->hunger;
    hg[1] = med->fed;
    if (!coopstate::HungerPlausible(hg[0], hg[1])) return 0;
    InterlockedIncrement64(&g_hungerCarried);
    return hg;
}

// The detour. MEASURES AND PASSES THROUGH - it must not change a single value, or the
// drift rate this run exists to measure would be the drift rate of our own interference.
void detour_medicalUpdate(MedicalSystem* self, float frameTime)
{
    InterlockedIncrement64(&g_medCalls);
    if (g_medThreadId == 0)
        InterlockedExchange(&g_medThreadId, (LONG)::GetCurrentThreadId());

    // Attribute per entity. FindSpawnedUid is a flat-array scan - no allocation, no lock,
    // already proven safe from the combat detour on an unmeasured thread.
    const void* owner = (const void*)((const char*)self - kMedicalOffset);
    unsigned int uid = FindSpawnedUid(owner);
    if (uid != 0)
    {
        if (net::IsUidMineAnyThread(uid)) InterlockedIncrement64(&g_medCallsOurs);   /* O1: the medical detour's thread is not measured to be main */
        else
        {
            InterlockedIncrement64(&g_medCallsTheirs);

            // H012 / F150 - THE THIRD PUPPET GATE.
            //
            // A replicated character on this instance is a PUPPET. This project already
            // established that a puppet must stop DECIDING (AI::periodicUpdate, F059-F061) and
            // stop LOCOMOTING on its own (threadedUpdate, F047/F052/F058). Medicine is the third
            // system and it was never gated - so the peer has been running a full medical
            // simulation for a character it does not own, and COMPETING with the authority.
            //
            // T048 measured that competition directly: our `unconcious` write lands, and the
            // peer's engine reverts it within 17-128 ms (median 85). It even reached knockouts
            // the authority never had, while we were actively pushing the opposite value. Three
            // attempts to out-write a running simulation have now failed; this removes the
            // simulation instead.
            //
            // Detour-thread rules (F124: this is the AI worker): a pointer compare and an
            // ownership lookup, both of which this function already did as measurement.
            // par5 (parity P5): the owner's hunger, put back before the copy's own tick can move it.
            HoldOwnerHunger(uid, self);
            if (g_medGate)
            {
                InterlockedIncrement64(&g_medGated);
                return;      // the authority's STATE is the only thing that moves this character
            }
        }
    }

    orig_medicalUpdate(self, frameTime);
}

// C2 (read-KO 2026-09-22) - A COPY STARTS A KNOCKOUT CLOCK ONLY WHEN ITS OWNER SAYS IT IS KNOCKED OUT.
//
// The copy's wake-up clock is MedicalSystem::knockoutClock (+0xA0; Character+0x4F8). Only two engine functions
// write it - startKnockoutTimer 0x643D40 and knockout(float) 0x643EF0 (medical.md 'The knockout revert,
// localised'; both RVAs resolved through Steam_1.0.65.br, MedicalSystem slots 34 and 35) - and
// reassessCollapseMode keeps the character unconscious while 0 < knockoutClock. On a PUPPET (this game's copy of
// a character the other game drives) the engine's own rolls - a replayed hit among them (T250: the copy went
// down ~144 ms after a hit its owner only stumbled from) - would start that clock by themselves.
// So for a puppet these two run ONLY when:
//   - the owner's latest STATE says it is knocked out (the latch ApplyLatch applied: `unconcious` set, or a
//     wake-up clock still running), or
//   - this very thread is inside ApplyState applying the owner's STATE (ApplyingOwnerState).
// Otherwise the call returns without starting anything and is counted (puppetKoRefused). Characters this game
// drives, and every character in a lone game (no uid), take the engine's path untouched.
// ApplyLatch itself calls NEITHER function - it writes `unconcious` and `knockoutClock` directly - so the
// owner's own knockout still reaches the copy exactly as before. Nothing here writes a medical field or a prone
// state (H012 refuted, F147/F150): the gate only declines to START a clock.
//
// DETOUR-THREAD RULES (F124: the medical tick runs on the AI worker): no allocation, no lock, no log - fixed
// arrays and interlocked counters only; refusals are logged later from the main thread (DrainKoRefusals).
typedef void (*StartKoTimerFn)(MedicalSystem*);
typedef void (*KnockoutFn)(MedicalSystem*, float);
StartKoTimerFn orig_startKnockoutTimer = 0;
KnockoutFn     orig_knockout           = 0;

// The owner's latest word per puppet uid (a per-copy word table, above: a slot lives as long as the copy). Written from
// ApplyLatch, read from the detours. A uid not found = the owner has not said "knocked out" = refuse - the conservative
// answer, since the owner's real knockout arrives by STATE anyway.
const int kOwnerKoSlots = 4096;   // a power of two
const int kOwnerKoProbe = 64;     // as the hunger table: a lookup gives up after this many slots
struct OwnerKoSlot
{
    volatile LONG uid; volatile LONG ko;
    void ClearValues() { InterlockedExchange(&ko, 0); }
};
OwnerKoSlot g_ownerKo[kOwnerKoSlots];
volatile LONG64 g_ownerKoTableFull = 0;

volatile LONG   g_applyingOwnerThread = 0;   // the thread inside ApplyState right now (0 = none)

volatile LONG64 g_puppetKoRefused         = 0;
volatile LONG64 g_puppetKoAllowedOwner    = 0;   // the owner's latest STATE says knocked out
volatile LONG64 g_puppetKoAllowedApplying = 0;   // called from inside ApplyState itself
volatile LONG64 g_puppetKoRefusedRebuild  = 0;   // crash1c (R2c): refused because a body rebuild is in flight on the copy

// The first kKoRefusalLog refusals, for the main thread to log: uid, which function, its argument, and the clock
// as it stood. The engine computes the timer INSIDE these functions (floor 3.0 s) - startKnockoutTimer takes no
// argument and knockout takes a 0..1 skill - so the timer it would have set does not exist before the call;
// the argument and the current clock are what a refusal can honestly show.
const int kKoRefusalLog = 16;
struct KoRefusal { volatile LONG ready; unsigned int uid; int fn; float arg; float timerNow; };
KoRefusal g_koRefusal[kKoRefusalLog];
volatile LONG g_koRefusalTaken  = 0;
int           g_koRefusalLogged = 0;   // main thread only

void NoteOwnerKo(unsigned int uid, bool ko)
{
    if (uid == 0) return;
    const int at = SlotClaim(g_ownerKo, kOwnerKoSlots, kOwnerKoProbe, uid);
    if (at < 0) { InterlockedIncrement64(&g_ownerKoTableFull); return; }
    InterlockedExchange(&g_ownerKo[at].ko, ko ? 1 : 0);
}

bool OwnerSaysKo(unsigned int uid)
{
    const int at = SlotOf(g_ownerKo, kOwnerKoSlots, kOwnerKoProbe, uid);
    if (at < 0) return false;                     // the owner sent no STATE for this copy (or the copy was removed)
    const bool ko = g_ownerKo[at].ko != 0;
    return ko && (unsigned int)g_ownerKo[at].uid == uid;   // released (or reused) while it was read: not knocked out
}

// true = let the engine start its clock. fn: 1 = startKnockoutTimer(), 2 = knockout(float).
// The puppet test is the one detour_medicalUpdate uses: a replicated uid that this game does not own.
bool PuppetKoAllowed(MedicalSystem* self, int fn, float arg)
{
    const void* owner = (const void*)((const char*)self - kMedicalOffset);
    unsigned int uid = FindSpawnedUid(owner);
    if (uid == 0 || net::IsUidMineAnyThread(uid)) return true;   // ours, or not replicated at all (a lone game). O1-b: any thread
    // crash1c (review-crash1b R2c): never while a body rebuild is in flight on this copy - the rebuild would then run on the
    // limp body (T293 / T323). Checked before both allow branches; the owner's knockout still arrives by STATE, held by
    // KnockdownMustWait until the rebuild has run.
    if (coop::CopyRebuildInFlightAny((::Character*)owner) != 0)
    {
        InterlockedIncrement64(&g_puppetKoRefusedRebuild);
        return false;
    }
    if (g_applyingOwnerThread != 0 && g_applyingOwnerThread == (LONG)::GetCurrentThreadId())
    {
        InterlockedIncrement64(&g_puppetKoAllowedApplying);
        return true;
    }
    if (OwnerSaysKo(uid))
    {
        InterlockedIncrement64(&g_puppetKoAllowedOwner);
        return true;
    }

    InterlockedIncrement64(&g_puppetKoRefused);
    LONG slot = InterlockedIncrement(&g_koRefusalTaken) - 1;
    if (slot < kKoRefusalLog)
    {
        KoRefusal& r = g_koRefusal[slot];
        r.uid = uid; r.fn = fn; r.arg = arg; r.timerNow = self->knockoutClock;
        InterlockedExchange(&r.ready, 1);
    }
    return false;
}

void detour_startKnockoutTimer(MedicalSystem* self)
{
    if (!PuppetKoAllowed(self, 1, 0.0f)) return;   // refused: no clock started on this copy
    orig_startKnockoutTimer(self);
}

void detour_knockout(MedicalSystem* self, float skill01)
{
    if (!PuppetKoAllowed(self, 2, skill01)) return;   // refused: no clock started on this copy
    orig_knockout(self, skill01);
}

void InstallPuppetKoGate()
{
    intptr_t st = (intptr_t)coop::AddrAbs(kMig3StartKnockoutTimer);
    intptr_t ko = (intptr_t)coop::AddrAbs(kMig3Knockout);
    if (st == 0 || ko == 0)
    {
        ErrorLog("[C2] startKnockoutTimer/knockout resolved to 0 - puppet knockout gate NOT installed");
        return;
    }
    coop::HookStatus a =
        coop::AddHook((void*)st, (void*)&detour_startKnockoutTimer, (void**)&orig_startKnockoutTimer);
    coop::HookStatus b =
        coop::AddHook((void*)ko, (void*)&detour_knockout, (void**)&orig_knockout);
    DebugLog(std::string("[C2] puppet knockout gate: startKnockoutTimer AddHook ")
             + (a == coop::SUCCESS ? "SUCCESS" : "FAILED")
             + ", knockout AddHook " + (b == coop::SUCCESS ? "SUCCESS" : "FAILED"));
}

// C2-b (read-damage 2026-09-22) - A COPY TAKES NO ENGINE DAMAGE OF ITS OWN.
//
// Why (T250 1664, read-damage; Confirmed by Ghidra xrefs): the hit this game replays on a copy (combat.cpp
// orig_hitByMelee -> 0x439150; a ranged hit, 0x4398F0, takes the same road) calls addWound 0x64FE40, which
// applies armour and then calls MedicalSystem::applyDamage 0x64E870 - the call that returns to 0x65060C.
// applyDamage is what subtracts part health and blood and, below its thresholds, sets `unconcious` (+0x161) /
// bloodLossShock (+0x163), calls startKnockoutTimer and queues the ragdoll request (0x5CB2D0): the copy's fall
// ~144 ms after a hit its owner only stumbled from. The hit reaction animation, the sounds, the blood effect and
// the decal are decided OUTSIDE 0x64E870 on the same armour-reduced numbers, so skipping it removes the copy's
// own damage and nothing a player sees of the hit.
//
// So for a copy - a uid this game does not drive (FindSpawnedUid + !net::IsUidMineAnyThread, the knockout gate's lookup)
// - applyDamage is skipped when, and only when, it was called from the hit path (return RVA 0x65060C). Every
// other call runs the original unchanged: characters this game drives, every character in a lone game (uid 0),
// and applyDamage's only other caller, MedicalSystem::load 0x64F0B0 (return 0x64F616), which restores a saved
// character's wounds and must be left alone.
//
// A copy's health comes ONLY from its owner: ApplyHealth, the STATE snapshot and ApplyLatch write the medical
// fields directly and call none of this. The startKnockoutTimer / knockout gate above STAYS - the copy's own
// bleeding and hunger paths reach those two without passing through applyDamage.
//
// Both callers reach 0x64E870 through the jump thunk 0x290AA (a bare E9, no frame of its own), so
// _ReturnAddress() here is the caller's return address. DETOUR-THREAD RULES as above: no allocation, no lock, no
// log - interlocked counters only, reported on the [P014] REPORT line.
unsigned long long kApplyDamageRva = 0; static coop::AddrReg kApplyDamageRva_reg("ApplyDamage", &kApplyDamageRva);   /* P8h: the address table fills this. Steam_1.0.65 0x64E870 */   /* MedicalSystem::applyDamage */
unsigned long long kApplyDamageHitRetRva = 0; static coop::AddrReg kApplyDamageHitRetRva_reg("ApplyDamageHitRet", &kApplyDamageHitRetRva);   /* P8h: the address table fills this. Steam_1.0.65 0x65060C */   /* addWound's applyDamage call returns here */

typedef void (*ApplyDamageFn)(MedicalSystem*, void*, float*, char, char, unsigned long long);
ApplyDamageFn orig_applyDamage = 0;
uintptr_t     g_dmgBase        = 0;   /* image base, written once before the hook exists */

volatile LONG64 g_copyDamageSkipped           = 0;   /* hit path, a copy: the engine's own damage NOT applied */
volatile LONG64 g_copyDamagePassedMine        = 0;   /* hit path, ours or not replicated (uid 0): original ran */
volatile LONG64 g_copyDamagePassedOtherCaller = 0;   /* any other caller (MedicalSystem::load among them): original ran */

void detour_applyDamage(MedicalSystem* self, void* part, float* dmg, char a4, char a5, unsigned long long a6)
{
    const uintptr_t ret = (uintptr_t)_ReturnAddress();   /* FIRST, before anything can disturb the frame */
    if (g_dmgBase == 0 || ret < g_dmgBase || ret - g_dmgBase != (uintptr_t)kApplyDamageHitRetRva)
    {
        InterlockedIncrement64(&g_copyDamagePassedOtherCaller);
        orig_applyDamage(self, part, dmg, a4, a5, a6);
        return;
    }
    const void* who = (self != 0) ? *(void* const*)((const char*)self + 0xE0) : 0;   /* the owning Character */
    const unsigned int uid = (who != 0) ? FindSpawnedUid(who) : 0;
    if (uid != 0 && !net::IsUidMineAnyThread(uid))   /* O1-b: hits and ranged hits can run on an AI worker */
    {
        InterlockedIncrement64(&g_copyDamageSkipped);   /* its health arrives from the owner instead */
        return;
    }
    InterlockedIncrement64(&g_copyDamagePassedMine);
    // K2 (decision 61 follow-up): a character this game drives (uid != 0) that goes conscious ->
    // unconscious (+0x161 0 -> 1) inside this call was knocked down BY THIS HIT - applyDamage sets the byte
    // and queues the fall 0x5CB2D0(c,1,1) at 0x64EAB6 / 0x64EB0A. The hit hook armed a slot for this
    // thread; NoteKnockEdgeAnyThread marks it (no lock, no allocation).
    const bool k2Watch = (uid != 0 && self != 0 && who != 0);
    const unsigned char k2Before = k2Watch ? *((const unsigned char*)self + 0x161) : 1;
    orig_applyDamage(self, part, dmg, a4, a5, a6);
    if (k2Watch && k2Before == 0 && *((const unsigned char*)self + 0x161) != 0)
        NoteKnockEdgeAnyThread(who);
}

void InstallCopyDamageSkip()
{
    if (kApplyDamageRva == 0 || kApplyDamageHitRetRva == 0)
    {
        ErrorLog("[C2-b] ApplyDamage / ApplyDamageHitRet not in the address table - copy damage skip NOT installed");
        return;
    }
    g_dmgBase = (uintptr_t)::GetModuleHandleA(0);
    coop::HookStatus st = coop::AddHook((void*)(g_dmgBase + (uintptr_t)kApplyDamageRva),
                                                  (void*)&detour_applyDamage, (void**)&orig_applyDamage);
    DebugLog(std::string("[C2-b] copy damage skip: applyDamage 0x64E870 AddHook ")
             + (st == coop::SUCCESS ? "SUCCESS" : "FAILED")
             + " (skipped only for a copy, only on the hit path, return 0x65060C)");
}

// par6 (parity P6; parity-audit-characters row 16) - A COPY DIES ONLY WHEN ITS OWNER SAYS IT IS DEAD.
//
// `dead` always rode STATE (the owner's Character::hasDied, +0x5BC) and OnState only logged it, while each game ran the
// copy's own death test: medicalUpdate 0x651570 sets MedicalSystem::dead (+0x164) and calls Character::declareDead
// 0x7A5660 when blood falls below -max, hunger below 0, or a fatal part fails 0x64EB30 (build/decomp_651570.txt:136-142,
// 190-196). So a copy could die on one screen only, or stay alive after its owner died between two STATEs.
// Now: the owner's word per copy uid lives in this table (ApplyOwnerDeath, from every STATE). The one declareDead hook
// (worldstate.cpp detour_declareDead) asks CopyDeathAllowed first: a copy's own death is refused unless the owner's
// latest STATE says dead - and, like the C2 knockout gate, never while a body rebuild is in flight.
// par6b (attempt 2, T500 P6): MedicalSystem::dead (+0x164, medical at +0x458) IS Character::hasDied's byte (+0x5BC) - one
// byte, not two (hasDied 0x620B20 reads it; declareDead's first instruction 0x7A56A2 writes it). So whoever set the flag
// before the call (medicalUpdate, the kill lever, ApplyOwnerDeath) makes the character read dead INSIDE the gate, and the
// gate cannot tell that from a corpse by reading hasDied. The gate therefore recognises medicalUpdate's two call sites by
// return address (coopstate::MedicalUpdateDeathCall - medicalUpdate calls only while the flag was 0, so the character was
// alive) and un-sets the flag it refused; left set, the copy is half-dead: dead to hasDied, never through the death routine,
// its medical tick skipped for good (decomp_651570.txt:131). The lever and ApplyOwnerDeath un-set their own flag when the
// gate refused. The owner's death itself is applied by ApplyOwnerDeath through the engine's own two steps, in
// medicalUpdate's order: MedicalSystem::dead = 1, then declareDead.
// DETOUR-THREAD RULES (F124): CopyDeathAllowed runs on the AI worker - fixed arrays, interlocked counters, no lock, no
// allocation, no log; refusals are logged later from the main thread (DrainDeathRefusals).
const int kOwnerDeadSlots = 4096;   // a power of two
const int kOwnerDeadProbe = 64;     // as par5's hunger table: a lookup gives up after this many slots
struct OwnerDeadSlot
{
    volatile LONG uid; volatile LONG dead;
    void ClearValues() { InterlockedExchange(&dead, 0); }
};
OwnerDeadSlot g_ownerDead[kOwnerDeadSlots];
volatile LONG64 g_ownerDeadTableFull      = 0;
volatile LONG64 g_deathApplied            = 0;   // copy: the owner said dead and the copy died through the engine's path
volatile LONG64 g_deathAlready            = 0;   // copy: the owner said dead and the copy was already dead - a no-op
volatile LONG64 g_deathDeferred           = 0;   // copy: the owner said dead, a body rebuild is in flight or declareDead was refused/faulted - the next STATE retries
volatile LONG64 g_deathFault              = 0;   // copy: declareDead faulted (also counted deferred)
volatile LONG64 g_deathNoCopy             = 0;   // the owner said dead for a uid with no character loaded here
volatile LONG64 g_deathOwnerAliveCopyDead = 0;   // the owner says alive, the copy is dead (never resurrected - reported)
volatile LONG64 g_copyDeathRefused        = 0;   // a copy's own declareDead refused (the owner has not said dead)
volatile LONG64 g_copyDeathRefusedRebuild = 0;   // ... refused because a body rebuild is in flight
volatile LONG64 g_copyDeathAllowedOwner   = 0;   // a copy's declareDead allowed: the owner's latest STATE says dead
volatile LONG64 g_copyDeathMedCleared     = 0;   // refusals that put MedicalSystem::dead back to 0
volatile LONG64 g_killLever               = 0;   // TEST lever `kill <uid>` calls
volatile LONG64 g_copyDeathWorldRecord    = 0;   // review-par6 #2: a copy's declareDead let through - the notebook's (world record's) twin kill
volatile LONG64 g_copyDeathDeadNoClear    = 0;   // review-par6 #8: refusals on a copy that reads dead (not from medicalUpdate) - MedicalSystem::dead left alone
volatile LONG64 g_medicalDeathRefusedUndone = 0; // par6b: a copy's medicalUpdate death refused and its flag (= hasDied's byte) put back to 0
// review-par6 #1 - DEATH REQUESTS. A player's one-shot kill of a copy (butchering: Inventory::deathCheck 0x75DCA0 calls
// declareDead, prints "{1} has been butchered" and marks the job done without checking; Character::unequipItem 0x5DB880
// clears its trigger flag, then calls declareDead - build/decomp_75dca0.txt:38-42, decomp_5db880.txt:47-51) is refused by
// the gate above and nobody retries it, so the kill was lost on both screens. Such a refusal is now queued here (any
// thread: fixed slots, interlocked) and sent to the owner from the main thread (MSG_PRISON kind 4); the owner kills its own
// character if it is alive, and its STATE dead=1 then kills the copy through ApplyOwnerDeath. A health death (medicalUpdate
// 0x651570) sends nothing: the owner's own health decides.
volatile LONG64 g_deathReqQueued          = 0;   // copy side: a refused event kill queued for the owner
volatile LONG64 g_deathReqSent            = 0;   // copy side: sent to the owner (MSG_PRISON kind 4)
volatile LONG64 g_deathReqDropped         = 0;   // copy side: queue full, link down, or the uid became ours before the send
volatile LONG64 g_deathReqApplied         = 0;   // owner side: our character died through declareDead on the request
volatile LONG64 g_deathReqIgnored         = 0;   // owner side: not driven here, not loaded, already dead, or declareDead left it alive
long long       g_deathFaultLogged        = 0;   // review-par6 #6: the deathFault ErrorLog is rate-limited (first 5, every 100th)
// THE EVENT CALLERS, by return address. declareDead's hook is a jump at its entry, and both callers reach it through the
// thunk jump 0x7A5660 (decomp: thunk_FUN_1407a5660), so the hook's _ReturnAddress() is the instruction after the caller's
// CALL - inside the caller's own body. Bodies: [start, next function's start) from the exports (exports_all.txt:
// 0x75DCA0 Inventory::deathCheck, next 0x75DE40; 0x5DB880 Character::unequipItem, next 0x5DBAD0 - Steam 1.0.65). stage 7/9:
// all four, and medicalUpdate's two declareDead return addresses, are address-table rows (both tables; 1.0.68 by
// tools/gen_table_1068.py, each End row checked to be the next function in both builds). g_eventKillBase stays 0 - no
// death requests, gate unchanged - only if a row is unbound.
static unsigned long long kDeathCheckRva = 0; static coop::AddrReg kDeathCheckRva_reg("Inventory_onOwnerDeath", &kDeathCheckRva);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x75DCA0 - Inventory::deathCheck (butchering) */
static unsigned long long kDeathCheckEnd = 0; static coop::AddrReg kDeathCheckEnd_reg("Inventory_onOwnerDeathEnd", &kDeathCheckEnd);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x75DE40 - the next function */
static unsigned long long kUnequipRva = 0; static coop::AddrReg kUnequipRva_reg("Character_takeOffItem", &kUnequipRva);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x5DB880 - Character::unequipItem */
static unsigned long long kUnequipEnd = 0; static coop::AddrReg kUnequipEnd_reg("Character_takeOffItemEnd", &kUnequipEnd);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x5DBAD0 - the next function */
static unsigned long long kMedUpdateDeathRet1Rva = 0; static coop::AddrReg kMedUpdateDeathRet1Rva_reg("MedUpdateDeathRet1", &kMedUpdateDeathRet1Rva);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x651A15 */
static unsigned long long kMedUpdateDeathRet2Rva = 0; static coop::AddrReg kMedUpdateDeathRet2Rva_reg("MedUpdateDeathRet2", &kMedUpdateDeathRet2Rva);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x651C6E */
volatile unsigned long long g_eventKillBase = 0;   // the exe's base, set once at install; 0 = caller ranges off
int EventKillCause(unsigned long long ret)   // ANY THREAD. 0 none, 1 deathCheck, 2 unequip
{
    const unsigned long long base = g_eventKillBase;
    if (base == 0 || ret <= base) return 0;
    const unsigned long long rva = ret - base;
    if (rva > kDeathCheckRva && rva < kDeathCheckEnd) return 1;
    if (rva > kUnequipRva && rva < kUnequipEnd) return 2;
    return 0;
}
const int kDeathReqSlots = 64;
volatile LONG64 g_deathReq[kDeathReqSlots];   // 0 = free; else (cause << 32) | uid
void QueueDeathRequest(unsigned int uid, int cause)   // ANY THREAD - no allocation, no lock, no log
{
    const LONG64 v = ((LONG64)cause << 32) | (LONG64)uid;
    for (int i = 0; i < kDeathReqSlots; ++i)
        if (InterlockedCompareExchange64(&g_deathReq[i], v, 0) == 0) { InterlockedIncrement64(&g_deathReqQueued); return; }
    InterlockedIncrement64(&g_deathReqDropped);
}
// MAIN THREAD (StateTick). Sends every queued request once; a request that cannot be sent now is dropped and counted -
// the kill was one shot, and a late one could land after the owner's own game has moved on.
void DrainDeathRequests()
{
    for (int i = 0; i < kDeathReqSlots; ++i)
    {
        const LONG64 v = InterlockedExchange64(&g_deathReq[i], 0);
        if (v == 0) continue;
        const unsigned int uid = (unsigned int)(v & 0xFFFFFFFF);
        const int cause = (int)(v >> 32);
        cooprison::PrisonMsg m; m.uid = uid; m.kind = cooprison::kPrisonDeath;
        m.cageKey = cause == 1 ? "deathCheck" : "unequip";
        const bool sent = !net::IsUidMine(uid) && net::SendPrison(m);
        const LONG64 n = InterlockedIncrement64(sent ? &g_deathReqSent : &g_deathReqDropped);
        if (n <= 8 || n % 100 == 0)
            DebugLog("[DEATH] death request uid=" + N(uid) + " caller=" + m.cageKey + (sent ? " -> sent to the owner" : " -> NOT sent (link down or the uid is ours now)")
                     + " (#" + N(n) + ")");
    }
}
int DeathIsDeadPod(void* c)   // ANY THREAD: Character::hasDied under SEH - 1 dead, 0 alive, -1 fault
{
    __try { return ((::Character*)c)->hasDied() ? 1 : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

void NoteOwnerDead(unsigned int uid, bool dead)
{
    if (uid == 0) return;
    const int at = SlotClaim(g_ownerDead, kOwnerDeadSlots, kOwnerDeadProbe, uid);
    if (at < 0) { InterlockedIncrement64(&g_ownerDeadTableFull); return; }
    InterlockedExchange(&g_ownerDead[at].dead, dead ? 1 : 0);
}

bool OwnerSaysDead(unsigned int uid)
{
    const int at = SlotOf(g_ownerDead, kOwnerDeadSlots, kOwnerDeadProbe, uid);
    if (at < 0) return false;                     // the owner said nothing about this copy (or the copy was removed): alive
    const bool dead = g_ownerDead[at].dead != 0;
    return dead && (unsigned int)g_ownerDead[at].uid == uid;   // released (or reused) while it was read: alive
}

const int kDeathRefusalLog = 16;
struct DeathRefusal { volatile LONG ready; unsigned int uid; int why; int medDead; float blood; float hunger; unsigned long tid; int cause; };
DeathRefusal g_deathRefusal[kDeathRefusalLog];
volatile LONG g_deathRefusalTaken  = 0;
int           g_deathRefusalLogged = 0;   // main thread only

// MAIN THREAD. Logs each of the first kDeathRefusalLog copy-death refusals once, in the order they were taken.
void DrainDeathRefusals()
{
    while (g_deathRefusalLogged < kDeathRefusalLog && g_deathRefusal[g_deathRefusalLogged].ready)
    {
        const DeathRefusal& r = g_deathRefusal[g_deathRefusalLogged];
        ++g_deathRefusalLogged;
        DebugLog("[DEATH] copy declareDead REFUSED #" + N(g_deathRefusalLogged) + " uid=" + N(r.uid)
                 + (r.why == 2 ? std::string(" - a body rebuild is in flight on the copy")
                               : std::string(" - the owner's latest STATE does not say dead"))
                 + " medDead=" + N(r.medDead) + " blood=" + F2(r.blood) + " hunger=" + F2(r.hunger)
                 + (r.cause == 1 ? std::string(" caller=deathCheck (death request queued for the owner)")
                    : r.cause == 2 ? std::string(" caller=unequip (death request queued for the owner)") : std::string())
                 + " thread=" + N((long long)r.tid) + (r.tid == (unsigned long)::GetCurrentThreadId() ? " (main)" : " (off-main)"));
    }
}

// MAIN THREAD. Logs each of the first kKoRefusalLog refusals once, in the order they were taken.
void DrainKoRefusals()
{
    while (g_koRefusalLogged < kKoRefusalLog && g_koRefusal[g_koRefusalLogged].ready)
    {
        const KoRefusal& r = g_koRefusal[g_koRefusalLogged];
        ++g_koRefusalLogged;
        DebugLog("[C2] puppet knockout timer REFUSED #" + N(g_koRefusalLogged) + " uid=" + N(r.uid)
                 + (r.fn == 1 ? std::string(" via startKnockoutTimer()")
                              : std::string(" via knockout(skill01=") + F2(r.arg) + ")")
                 + " timerNow=" + F2(r.timerNow)
                 + " - the owner's latest STATE does not say knocked out");
    }
}

} // namespace

// par5 (parity P5): outside the anonymous namespace - medical.h declares these.
void ApplyOwnerHunger(unsigned int uid, int has, float hunger, float fed)
{
    if (has == 0) { InterlockedIncrement64(&g_hungerAbsent); return; }
    if (!coopstate::HungerPlausible(hunger, fed) || net::IsUidMine(uid)) { InterlockedIncrement64(&g_hungerRefused); return; }
    NoteOwnerHunger(uid, hunger, fed);          // the hold uses it from the next tick on
    ::Character* c = FindSpawned(uid);
    if (c == 0) return;
    MedicalSystem* med = (MedicalSystem*)((char*)c + kMedicalOffset);
    if (!PlausiblePtr(med)) return;
    med->hunger = hunger;
    med->fed = fed;
    InterlockedIncrement64(&g_hungerApplied);
}

// TEST-ONLY lever (par5): `hungerset <uid> <value>` - make a character this game drives hungry at once, so a run can
// watch its copy follow. Owner side only: on a copy the owner's next STATE would overwrite it anyway.
std::string HungerSetOwned(unsigned int uid, float value)
{
    if (!net::IsUidMine(uid)) return "error hungerset: uid " + N(uid) + " is not driven by this game";
    if (!coopstate::HungerPlausible(value, 0.0f)) return "error hungerset: implausible value";
    ::Character* c = FindSpawned(uid);
    if (c == 0) return "error hungerset: no character for uid " + N(uid);
    MedicalSystem* med = (MedicalSystem*)((char*)c + kMedicalOffset);
    if (!PlausiblePtr(med)) return "error hungerset: medical system unreadable";
    const float before = med->hunger;
    med->hunger = value;
    const bool pushed = StatePush(uid);
    DebugLog("[HUNGER] set uid=" + N(uid) + " hunger " + F2(before) + " -> " + F2(med->hunger)
             + " fed=" + F2(med->fed) + " pushed=" + N(pushed ? 1 : 0));
    return "ok hungerset uid=" + N(uid) + " hunger=" + F2(med->hunger) + " pushed=" + N(pushed ? 1 : 0);
}

// P10 TEST-ONLY lever (`bodydown`, appearance.cpp BodyDownLever): knock out a character this game drives with the
// engine's own knockout, through the C2 gate (it lets an owned character through), and push its STATE, so the copy's
// own engine on the other game follows it down (ApplyLatch writes unconcious / knockoutClock, F439) - the T424 road.
int KnockOutOwned(unsigned int uid)
{
    if (!net::IsUidMine(uid)) return -1;
    ::Character* c = FindSpawned(uid);
    if (c == 0) return -1;
    MedicalSystem* med = (MedicalSystem*)((char*)c + kMedicalOffset);
    if (!PlausiblePtr(med) || orig_knockout == 0) return -1;
    // P10 fold 1 (T633): knockout(float) 0x643EF0 multiplies the clock it computes by its argument and floors the result
    // at 3.0 s (build/decomp_643ef0.txt), so 0 always gave the 3 s floor; 1.0 gives the engine's full clock. It only sets
    // knockoutClock - `unconcious` follows at the medical update, which the lever checks one tick and 2 s later.
    detour_knockout(med, 1.0f);
    const int unc = med->unconcious ? 1 : 0;
    const bool pushed = StatePush(uid);
    DebugLog("[P10] lever bodydown knockout (TEST) owned uid=" + N(uid) + " unconcious=" + N(unc)
             + " koTimer=" + F2(med->knockoutClock) + " prone=" + N((int)c->poseState())
             + " pushed=" + N(pushed ? 1 : 0));
    return unc;
}

// P11 fold 1 (T652) TEST-ONLY lever (`koself`, appearance.cpp KoSelfLever): after KnockOutOwned, hold the wake-up clock
// at least `seconds` (it never shortens it) and push the STATE again, so a capture test has time to act. Returns the
// clock after, -1 = not ours / no character / unreadable. MAIN THREAD.
float KnockClockAtLeastOwned(unsigned int uid, float seconds)
{
    if (!net::IsUidMine(uid)) return -1.0f;
    ::Character* c = FindSpawned(uid);
    if (c == 0) return -1.0f;
    MedicalSystem* med = (MedicalSystem*)((char*)c + kMedicalOffset);
    if (!PlausiblePtr(med)) return -1.0f;
    const float before = med->knockoutClock;
    if (before < seconds) med->knockoutClock = seconds;
    const bool pushed = StatePush(uid);
    DebugLog("[P11] koself hold (TEST) owned uid=" + N(uid) + " koTimer " + F2(before) + " -> " + F2(med->knockoutClock)
             + " unconcious=" + N(med->unconcious ? 1 : 0) + " pushed=" + N(pushed ? 1 : 0));
    return med->knockoutClock;
}

// crash1b (T323): the owner's last STATE said this character is knocked out (the table OwnerSaysKo reads). ANY THREAD.
bool CopyOwnerSaysKo(unsigned int uid) { return OwnerSaysKo(uid); }

// crash1c (review-crash1b R3a): ApplyState writes the owner's word here even when it refuses the health apply, so a stale
// "knocked out" entry cannot hold the looks forever. ANY THREAD.
void CopyNoteOwnerKo(unsigned int uid, bool ko) { NoteOwnerKo(uid, ko); }

// The per-copy word tables (above): uid's hunger, knocked-out and dead words are forgotten. Called where its copy is removed
// here (spawn.cpp: RemoveLocalCopy, an inbound DESPAWN, the mirror reclaim) and where its ownership moves to or from this game
// (net/session.cpp: TakeLocalOwner, ReleaseLocalOwner, SetLocalOwner) - a word kept past either would be read as the owner's
// the next time a copy of that uid is held here. MAIN THREAD.
void MedicalForgetCopy(unsigned int uid)
{
    if (uid == 0) return;
    SlotRelease(g_ownerHunger, kOwnerHungerSlots, kOwnerHungerProbe, uid);
    SlotRelease(g_ownerKo, kOwnerKoSlots, kOwnerKoProbe, uid);
    SlotRelease(g_ownerDead, kOwnerDeadSlots, kOwnerDeadProbe, uid);
}

// Every copy's words are forgotten: world teardown (spawn.cpp SpawnWorldTeardown) and session end (net/session.cpp
// SessionLeave). MAIN THREAD.
void MedicalForgetAllCopies()
{
    SlotClearAll(g_ownerHunger, kOwnerHungerSlots);
    SlotClearAll(g_ownerKo, kOwnerKoSlots);
    SlotClearAll(g_ownerDead, kOwnerDeadSlots);
}

// par6 (parity P6) - see the table above. ANY THREAD (the medical tick's AI worker, or a lever on the main thread).
// true = let Character::declareDead run. Characters this game drives, and every character in a lone game (no uid), take
// the engine's path untouched; so does a character whose ownership has moved here (the ownership test is per call).
bool CopyDeathAllowed(void* character, unsigned long long callerRet, bool worldRecordKill)
{
    unsigned int uid = FindSpawnedUid(character);
    if (uid == 0 || net::IsUidMineAnyThread(uid)) return true;
    // review-par6 #2: the notebook's kill of a TWIN (worldstate.cpp, main thread, the flag set around that one call) is the
    // world record's decision, not a copy's own death - it passes.
    if (worldRecordKill) { InterlockedIncrement64(&g_copyDeathWorldRecord); return true; }
    int why = 0;
    if (!OwnerSaysDead(uid)) why = 1;
    else if (CopyRebuildInFlightAny((::Character*)character) != 0) why = 2;
    if (why == 0) { InterlockedIncrement64(&g_copyDeathAllowedOwner); return true; }
    InterlockedIncrement64(&g_copyDeathRefused);
    if (why == 2) InterlockedIncrement64(&g_copyDeathRefusedRebuild);
    // par6b: MedicalSystem::dead IS hasDied's byte, and medicalUpdate set it just before this call - a call from one of its two
    // call sites began on a LIVE copy (it calls only while the flag was 0), so it is treated as alive and the clear below
    // undoes the half-death. Every other caller keeps review-par6 #8: a copy that reads dead keeps its flag (a corpse).
    const bool fromMedicalUpdate = coopstate::MedicalUpdateDeathCall(callerRet, g_eventKillBase, kMedUpdateDeathRet1Rva, kMedUpdateDeathRet2Rva);
    const int copyDead = fromMedicalUpdate ? 0 : DeathIsDeadPod(character);
    MedicalSystem* med = (MedicalSystem*)((char*)character + kMedicalOffset);
    int medDead = 0; float blood = 0.0f, hunger = 0.0f;
    if (PlausiblePtr(med))
    {
        medDead = med->dead ? 1 : 0;
        blood = med->blood;
        hunger = med->hunger;
        if (med->dead && copyDead == 0)
        {
            med->dead = false;   // the caller set it before calling; left set, the copy is half-dead and its medical tick stops for good
            InterlockedIncrement64(&g_copyDeathMedCleared);
            if (fromMedicalUpdate) InterlockedIncrement64(&g_medicalDeathRefusedUndone);
        }
        else if (med->dead) InterlockedIncrement64(&g_copyDeathDeadNoClear);   // reads dead, not from medicalUpdate (or unreadable): leave the flag set
    }
    // review-par6 #1: a live copy's kill by a one-shot event caller goes to the owner as a death request.
    const int cause = copyDead == 0 ? EventKillCause(callerRet) : 0;
    if (cause != 0) QueueDeathRequest(uid, cause);
    LONG slot = InterlockedIncrement(&g_deathRefusalTaken) - 1;
    if (slot < kDeathRefusalLog)
    {
        DeathRefusal& r = g_deathRefusal[slot];
        r.uid = uid; r.why = why; r.medDead = medDead; r.blood = blood; r.hunger = hunger;
        r.tid = (unsigned long)::GetCurrentThreadId();
        r.cause = cause;
        InterlockedExchange(&r.ready, 1);
    }
    return false;
}

static volatile LONG64 g_deathHeldForLook = 0;   // T-303 fold 1: a STATE death held - the SPAWN-dead copy's first APPEARANCE comes first
// par6 (parity P6) - the owner's `dead` from its STATE. MAIN THREAD (OnState, after RemoteMayWrite).
void ApplyOwnerDeath(unsigned int uid, int dead)
{
    if (uid == 0 || net::IsUidMine(uid)) return;   // this game's engine decides the deaths of the characters it drives
    NoteOwnerDead(uid, dead != 0);
    ::Character* c = FindSpawned(uid);
    if (c == 0) { if (dead != 0) InterlockedIncrement64(&g_deathNoCopy); return; }
    if (dead == 0)
    {
        if (c->hasDied())
        {
            const LONG64 n = InterlockedIncrement64(&g_deathOwnerAliveCopyDead);
            if (n <= 8) DebugLog("[DEATH] uid=" + N(uid) + " the owner says ALIVE but the copy is dead - not resurrected (#" + N(n) + ")");
        }
        return;
    }
    if (c->hasDied()) { InterlockedIncrement64(&g_deathAlready); return; }   // a death that arrives for a dead character: no-op
    if (SpawnDeathLookWanted(uid))
    {
        /* T-303 fold 1 (owner decision 223): a copy its SPAWN said dead takes its owner's first APPEARANCE alive, on its built
           body, before any kill - the STATE route waits too (bounded: 3 s after the body is built, 30 s after the SPAWN). */
        const LONG64 n = InterlockedIncrement64(&g_deathHeldForLook);
        if (n <= 8) DebugLog("[DEATH] uid=" + N(uid) + " the owner says dead - held: its first APPEARANCE is applied alive first"
                             " (T-303 fold 1, decision 223; deathHeldForLook=" + N(n) + ")");
        return;
    }
    if (CopyRebuildInFlightAny(c) != 0)
    {
        const LONG64 n = InterlockedIncrement64(&g_deathDeferred);
        if (n <= 8) DebugLog("[DEATH] uid=" + N(uid) + " the owner says dead - deferred: a body rebuild is in flight on the copy (the next STATE retries, #" + N(n) + ")");
        return;
    }
    MedicalSystem* med = (MedicalSystem*)((char*)c + kMedicalOffset);
    const bool medOk = PlausiblePtr(med);
    // par6b: the flag IS hasDied's byte, so hasDied reads 1 after this call whatever declareDead did - whether the death went
    // through is told by the gate's refusal count (snapshot before the call) and the call's own fault result.
    const LONG64 refusedBefore = g_copyDeathRefused;
    if (medOk) med->dead = true;                    // medicalUpdate's own order: the flag, then declareDead
    const int called = StoreDeclareDeadPod(c);
    const long long refused = (long long)(g_copyDeathRefused - refusedBefore);
    if (called != 1 || refused != 0)
    {
        if (medOk) med->dead = false;   // the death did not go through: do not leave the copy half-dead (the next STATE retries)
        const LONG64 n = InterlockedIncrement64(&g_deathDeferred);
        if (called != 1)
        {
            InterlockedIncrement64(&g_deathFault);
            ++g_deathFaultLogged;   // review-par6 #6: every STATE retries a failing kill - log the first 5, then every 100th
            if (g_deathFaultLogged <= 5 || g_deathFaultLogged % 100 == 0)
                ErrorLog("[DEATH] uid=" + N(uid) + " the owner says dead - declareDead FAULTED; flag cleared, deferred to the next STATE"
                         " (fault #" + N(g_deathFaultLogged) + "; logged: first 5, then every 100th)");
        }
        else if (n <= 8)
            DebugLog("[DEATH] uid=" + N(uid) + " the owner says dead - declareDead refused by the copy gate; flag cleared, deferred to the next STATE (#" + N(n) + ")");
        return;
    }
    InterlockedIncrement64(&g_deathApplied);
    DebugLog("[DEATH] uid=" + N(uid) + " the owner says dead -> copy killed through the engine's path"
             " (MedicalSystem.dead + Character::declareDead 0x7A5660) isDead=1 applied=" + N(g_deathApplied));
}

// T-303 (protocol 102) - A CORPSE IS NEVER ANNOUNCED STANDING. T665: the owner re-announced its dead character after a
// rejoin; the SPAWN said nothing about death, so the copy was created ALIVE, dressed alive, and died only 7.5 s later at
// the owner's next round-robin STATE (StateTick: the whole owned set every kStateRefreshSec, one slice per ~1 s). Now the
// SPAWN carries the owner's dead / knocked-out bits and the copy dies in the same message.
static volatile LONG64 g_spawnFlagDeadSent  = 0;   // owner: SPAWNs sent saying dead
static volatile LONG64 g_spawnFlagKoSent    = 0;   // owner: SPAWNs sent saying knocked out
static volatile LONG64 g_spawnDeadAtArrival = 0;   // copy: the SPAWN said dead and the copy was dead when OnSpawn returned
static volatile LONG64 g_spawnDeadRetried   = 0;   // copy: ... died on a retry (no copy yet / a rebuild in flight / refused)
static volatile LONG64 g_spawnDeadGaveUp    = 0;   // copy: ... still alive after 30 s of retries (the STATE keeps trying)
static volatile LONG64 g_spawnDeadWasDead   = 0;   // copy: ... and the copy was already dead (a repeated SPAWN)
static volatile LONG64 g_spawnDeadDropped   = 0;   // copy: ... declareDead FAULTED on the kill - dropped, left to the STATE (T-303 fold 1)
static volatile LONG64 g_spawnFlagDeadRecv  = 0;   // copy: SPAWNs received saying dead (T-303 fold 1)
static volatile LONG64 g_spawnFlagKoRecv    = 0;   // copy: SPAWNs received saying knocked out (T-303 fold 1)
static volatile LONG64 g_spawnDeadNotYet    = 0;   // copy: arrivals that went pending - caps the 'not dead yet' line (T-303 fold 1)
static volatile LONG64 g_spawnDeadLookApplied = 0; // copy: ... took its owner's APPEARANCE alive before the kill (decision 223)
static volatile LONG64 g_spawnDeadNoLook    = 0;   // copy: ... killed without it - none applied within 3 s of a built body, or the apply failed
/* T-303 fold 1: one pending SPAWN death. look (owner decision 223): 0 = waiting for the owner's first APPEARANCE to be
   applied to the ALIVE, built copy; 1 = applied; 2 = given up (3 s after the body was first seen built, or the apply failed). */
struct SpawnDeadEntry
{
    DWORD at;        // GetTickCount of the SPAWN
    DWORD builtAt;   // GetTickCount when SpawnDeathTry first saw CharacterBuilt == 1 (valid when built)
    bool built;
    int look;
    SpawnDeadEntry() : at(0), builtAt(0), built(false), look(0) {}
};
static std::map<unsigned int, SpawnDeadEntry> g_spawnDeadPending;   // uid -> its entry. MAIN THREAD only.

bool CopyOwnerSaysDead(unsigned int uid)
{
    return uid != 0 && OwnerSaysDead(uid);
}

unsigned int SpawnOwnerFlags(const void* character)
{
    if (character == 0) return 0;
    unsigned int flags = 0;
    if (DeathIsDeadPod((void*)character) == 1) flags |= coopspawn::kSpawnFlagDead;
    float nextKO = 0.0f, koT = 0.0f;
    const unsigned int bits = SnapshotLatch((::Character*)character, &nextKO, &koT);
    if ((bits & kLatchUnconcious) != 0 || (koT > 0.0f && koT < 3600.0f)) flags |= coopspawn::kSpawnFlagKo;   // NoteOwnerKo's test
    return flags;   // T-303 fold 1: counted by SpawnNoteFlagsSent, only for a SPAWN the send accepted
}

// T-303 fold 1 OWNER: spawnFlagDeadSent / KoSent count a SPAWN the transport accepted (SendSpawn, after Send). MAIN THREAD.
void SpawnNoteFlagsSent(unsigned int flags)
{
    if ((flags & coopspawn::kSpawnFlagDead) != 0) InterlockedIncrement64(&g_spawnFlagDeadSent);
    if ((flags & coopspawn::kSpawnFlagKo) != 0) InterlockedIncrement64(&g_spawnFlagKoSent);
}

// T-303 fold 1 COPY: a SPAWN whose flags byte is present carries the owner's WHOLE word - dead or alive, knocked out or
// not - so an alive SPAWN clears a stale entry of an earlier life. A dead one needs no KO word (its death holds the looks).
// Noted before the copy is created (OnSpawn). MAIN THREAD.
void CopyNoteSpawnFlags(unsigned int uid, unsigned int flags)
{
    const bool dead = (flags & coopspawn::kSpawnFlagDead) != 0;
    const bool ko = (flags & coopspawn::kSpawnFlagKo) != 0;
    if (dead) InterlockedIncrement64(&g_spawnFlagDeadRecv);
    if (ko) InterlockedIncrement64(&g_spawnFlagKoRecv);
    if (uid == 0 || net::IsUidMine(uid)) return;   // this game's engine decides the characters it drives
    NoteOwnerDead(uid, dead);
    CopyNoteOwnerKo(uid, ko && !dead);
}

// T-303 fold 1: why a SPAWN death has not landed (SpawnDeathTry's *whyOut).
static const char* SpawnDeathWhy(int why)
{
    return why == 1 ? "no copy loaded" : why == 2 ? "its body is not built yet" : why == 4 ? "declareDead faulted"
         : why == 5 ? "its first appearance is applied alive first, up to 3 s after its body is built" : "its death was deferred";
}

// T-303 fold 1 (owner decision 223): true while a copy its SPAWN said dead still waits for its owner's first APPEARANCE - the
// look apply lets it through although the owner says dead (only an ALIVE copy: deadlook1 refuses a dead one first), and the
// STATE route's ApplyOwnerDeath holds its kill. MAIN THREAD.
bool SpawnDeathLookWanted(unsigned int uid)
{
    std::map<unsigned int, SpawnDeadEntry>::const_iterator it = g_spawnDeadPending.find(uid);
    return it != g_spawnDeadPending.end() && it->second.look == 0;
}

// T-303 fold 1 (owner decision 223): the look apply ran for this uid (ok = applied). No-op unless a SPAWN death waits on it.
// MAIN THREAD (AppearanceTick).
void SpawnDeathNoteLookApplied(unsigned int uid, bool ok)
{
    std::map<unsigned int, SpawnDeadEntry>::iterator it = g_spawnDeadPending.find(uid);
    if (it == g_spawnDeadPending.end() || it->second.look != 0) return;
    it->second.look = ok ? 1 : 2;
    const LONG64 n = InterlockedIncrement64(ok ? &g_spawnDeadLookApplied : &g_spawnDeadNoLook);
    if (n <= 8)
        DebugLog("[DEATH] uid=" + N(uid) + " the SPAWN said dead - its first APPEARANCE " + std::string(ok ? "applied" : "FAILED")
                 + " on the alive copy; the kill follows once its body is built again (T-303 fold 1, decision 223; "
                 + std::string(ok ? "spawnDeadLookApplied=" : "spawnDeadNoLook=") + N(n) + ")");
}

// T-303 fold 1: ONE kill attempt for a SPAWN death - the arrival and the retry both take it. MAIN THREAD.
// 1 = the copy is dead; 0 = not yet, stays pending (*whyOut 1 no copy, 2 body not built, 3 a body rebuild in flight or
// deferred by ApplyOwnerDeath, 5 waiting for its first appearance); -1 = declareDead FAULTED (*whyOut 4): the caller drops
// the entry and leaves it to the owner's STATE, so a faulting call is not repeated 4x a second for 30 s. The copy is not
// touched until CharacterBuilt(c, 0) == 1 - the test the look and clothing applies take (appearance.cpp):
// CopyRebuildInFlightAny reads a copy with NO appearance object as not busy, so declareDead could otherwise run on a copy
// with no built body / ragdoll. Owner decision 223: the built, standing copy first takes its owner's APPEARANCE once (the
// look apply sets e->look), waiting at most 3 s after its body is first seen built (then killed anyway, spawnDeadNoLook).
// Its clothing stays held meanwhile (the owner says dead) and goes through the dead-dress path after the kill.
static int SpawnDeathTry(unsigned int uid, ::Character* c, SpawnDeadEntry* e, int* whyOut)
{
    *whyOut = 0;
    if (c == 0) { *whyOut = 1; return 0; }
    if (DeathIsDeadPod(c) == 1) return 1;
    if (CharacterBuilt(c, 0) != 1) { *whyOut = 2; return 0; }
    const DWORD now = ::GetTickCount();
    if (!e->built) { e->built = true; e->builtAt = now; }
    if (e->look == 0)
    {
        if ((DWORD)(now - e->builtAt) < 3000) { *whyOut = 5; return 0; }
        e->look = 2;
        const LONG64 n = InterlockedIncrement64(&g_spawnDeadNoLook);
        if (n <= 8)
            DebugLog("[DEATH] uid=" + N(uid) + " the SPAWN said dead - no APPEARANCE applied within 3 s of its body being built;"
                     " killed with the look it has (T-303 fold 1, decision 223; spawnDeadNoLook=" + N(n) + ")");
    }
    if (CopyRebuildInFlightAny(c) != 0) { *whyOut = 3; return 0; }   // the look apply's rebuild: waited here, not counted deferred
    const LONG64 faultBefore = g_deathFault;
    ApplyOwnerDeath(uid, 1);   // notes the owner's word first (NoteOwnerDead), so the copy gate lets the engine's death through
    if (g_deathFault != faultBefore) { *whyOut = 4; return -1; }
    if (DeathIsDeadPod(c) == 1) return 1;
    *whyOut = 3;
    return 0;
}

void ApplyOwnerDeathAtSpawn(unsigned int uid)
{
    if (uid == 0 || net::IsUidMine(uid)) return;
    NoteOwnerDead(uid, true);   // T-303 fold 1: the owner's word first - the looks are held on it until the copy is dead
    ::Character* c = FindSpawned(uid);
    if (c != 0 && DeathIsDeadPod(c) == 1)
    {
        InterlockedIncrement64(&g_spawnDeadWasDead);
        g_spawnDeadPending.erase(uid);
        return;
    }
    if (g_spawnDeadPending.find(uid) == g_spawnDeadPending.end()) g_spawnDeadPending[uid].at = ::GetTickCount();
    int why = 0;
    const int r = SpawnDeathTry(uid, c, &g_spawnDeadPending[uid], &why);   // T-303 fold 1: built body, first look, no fault retry
    if (r == 1)
    {
        g_spawnDeadPending.erase(uid);
        InterlockedIncrement64(&g_spawnDeadAtArrival);
        DebugLog("[DEATH] uid=" + N(uid) + " the SPAWN says dead -> the copy is dead from its first frame, before any appearance"
                 " or clothing (T-303; spawnDeadAtArrival=" + N(g_spawnDeadAtArrival) + ")");
        return;
    }
    if (r < 0)
    {
        g_spawnDeadPending.erase(uid);
        const LONG64 d = InterlockedIncrement64(&g_spawnDeadDropped);
        if (d <= 8)
            DebugLog("[DEATH] uid=" + N(uid) + " the SPAWN says dead - declareDead FAULTED on the copy; not retried, left to the"
                     " owner's STATE (T-303 fold 1; spawnDeadDropped=" + N(d) + ")");
        return;
    }
    const LONG64 n = InterlockedIncrement64(&g_spawnDeadNotYet);
    if (n <= 8)
        DebugLog("[DEATH] uid=" + N(uid) + " the SPAWN says dead - the copy is not dead yet (" + std::string(SpawnDeathWhy(why))
                 + "); retried every ~0.25 s for up to 30 s, its appearance and clothing held meanwhile (T-303; logged: first 8, #"
                 + N(n) + ")");
}

void SpawnDeathRetryTick()
{
    if (g_spawnDeadPending.empty()) return;
    static DWORD s_last = 0;
    const DWORD now = ::GetTickCount();
    if ((DWORD)(now - s_last) < 250) return;
    s_last = now;
    std::map<unsigned int, SpawnDeadEntry>::iterator it = g_spawnDeadPending.begin();
    while (it != g_spawnDeadPending.end())
    {
        const unsigned int uid = it->first;
        if (net::IsUidMine(uid) || !OwnerSaysDead(uid)) { g_spawnDeadPending.erase(it++); continue; }   // driven here now, or the owner's later word
        int why = 0;
        const int r = SpawnDeathTry(uid, FindSpawned(uid), &it->second, &why);   // T-303 fold 1: waits for CharacterBuilt; a fault drops the entry
        if (r == 1)
        {
            InterlockedIncrement64(&g_spawnDeadRetried);
            DebugLog("[DEATH] uid=" + N(uid) + " the SPAWN said dead -> the copy died on a retry "
                     + N((long long)(DWORD)(now - it->second.at)) + " ms after its SPAWN (T-303; spawnDeadRetried=" + N(g_spawnDeadRetried) + ")");
            g_spawnDeadPending.erase(it++);
            continue;
        }
        if (r < 0)
        {
            const LONG64 d = InterlockedIncrement64(&g_spawnDeadDropped);
            if (d <= 8)
                DebugLog("[DEATH] uid=" + N(uid) + " the SPAWN said dead - declareDead FAULTED on a retry "
                         + N((long long)(DWORD)(now - it->second.at)) + " ms after its SPAWN; dropped from the retry, left to the owner's"
                         " STATE (T-303 fold 1; spawnDeadDropped=" + N(d) + ")");
            g_spawnDeadPending.erase(it++);
            continue;
        }
        if ((DWORD)(now - it->second.at) > 30000)
        {
            InterlockedIncrement64(&g_spawnDeadGaveUp);
            DebugLog("[DEATH] uid=" + N(uid) + " the SPAWN said dead - the copy is still not dead after 30 s ("
                     + std::string(SpawnDeathWhy(why))
                     + "); left to the owner's STATE (T-303; spawnDeadGaveUp=" + N(g_spawnDeadGaveUp) + ")");
            g_spawnDeadPending.erase(it++);
            continue;
        }
        ++it;
    }
}

// par6 fold (review-par6 #1): the copy's game says a player killed its copy of OUR character in one shot (butchering, an
// unequip kill) and its gate refused it. MAIN THREAD (OnPrison). The owner decides: the engine's own call the event caller
// made - Character::declareDead, nothing more - only when the character is driven here and alive; STATE is pushed at once so
// the copy dies through ApplyOwnerDeath. The owner re-checks the caller's own condition on its character (re-check par6 #1).
void ApplyOwnerDeathRequest(unsigned int uid, const std::string& caller, unsigned int fromPeer)
{
    const char* why = 0;
    ::Character* c = 0;
    if (uid == 0 || !net::IsUidMine(uid)) why = "not driven here";
    else if ((c = FindSpawned(uid)) == 0) why = "not loaded";
    else if (c->hasDied()) why = "already dead";
    /* re-check par6 #1/#2: the owner re-checks the condition the engine caller itself tests, on its OWN character, so a request
       never kills what this game's engine would spare: deathCheck kills only an unconscious character (a copy that looked
       knocked out while the owner's had woken is refused), and the unequip kill only a character outside a player faction (the
       other game sees our player characters in its peer faction, which that engine test does not treat as a player one). */
    else if (caller == "deathCheck" && !c->isUnconscious()) why = "our character is not unconscious (deathCheck kills only an unconscious one)";
    else if (caller == "unequip" && IsPlayerFaction(c->getOwnerFactionDirect())) why = "our character is in a player faction (the unequip kill spares player characters)";
    if (why == 0)
    {
        const int called = StoreDeclareDeadPod(c);
        if (called == 1 && c->hasDied())
        {
            const LONG64 n = InterlockedIncrement64(&g_deathReqApplied);
            const bool pushed = StatePush(uid);
            DebugLog("[DEATH] death request uid=" + N(uid) + " caller=" + caller + " from peer " + N(fromPeer)
                     + " -> our character killed through Character::declareDead 0x7A5660 pushed=" + N(pushed ? 1 : 0) + " (#" + N(n) + ")");
            return;
        }
        why = called == 1 ? "declareDead returned and it is alive" : "declareDead FAULTED";
    }
    const LONG64 n = InterlockedIncrement64(&g_deathReqIgnored);
    if (n <= 8 || n % 100 == 0)
        DebugLog("[DEATH] death request uid=" + N(uid) + " caller=" + caller + " from peer " + N(fromPeer) + " IGNORED - " + why + " (#" + N(n) + ")");
}

// par6 TEST-ONLY lever: `kill <uid>` - the engine's own death steps (MedicalSystem::dead = 1, Character::declareDead) on
// any loaded replicated character. On one this game drives it dies and its STATE is pushed at once; on a copy the call
// goes through the copy gate, which must refuse it (the owner has not said dead) - the lever proves both halves.
std::string KillLever(unsigned int uid)
{
    ::Character* c = FindSpawned(uid);
    if (c == 0) return "error kill: no character for uid " + N(uid);
    const bool mine = net::IsUidMine(uid);
    if (c->hasDied()) return "ok kill uid=" + N(uid) + " mine=" + N(mine ? 1 : 0) + " already dead (no-op)";
    InterlockedIncrement64(&g_killLever);
    MedicalSystem* med = (MedicalSystem*)((char*)c + kMedicalOffset);
    const bool medOk = PlausiblePtr(med);
    const LONG64 refusedBefore = g_copyDeathRefused;
    if (medOk) med->dead = true;
    const int called = StoreDeclareDeadPod(c);
    // par6b: the flag IS hasDied's byte, so hasDied cannot say whether the gate refused - its refusal count can. The copy was
    // alive (checked above), so a refused call has its flag put back; hasDied is read after, for the lever's line.
    const long long refused = (long long)(g_copyDeathRefused - refusedBefore);
    if (medOk && refused != 0) med->dead = false;
    const bool nowDead = c->hasDied();
    const bool pushed = (mine && nowDead) ? StatePush(uid) : false;
    const std::string line = "uid=" + N(uid) + " mine=" + N(mine ? 1 : 0) + " call=" + N(called) + " isDead=" + N(nowDead ? 1 : 0)
                           + " refusedByCopyGate=" + N(refused) + " pushed=" + N(pushed ? 1 : 0);
    DebugLog("[DEATH] kill lever (TEST) " + line);
    return "ok kill " + line;
}

// T-306 TEST-ONLY: the engine's destroy of an eaten body, under SEH (no std::string here - C2712). The arguments are the
// ones MedicalSystem 0x64F8E0 passes at 0x64FDE9: the GameWorld global, the character, justUnloaded=false, "eaten".
// 1 the engine destroyed it, 0 the engine refused (GameWorld::destroy returned false), -1 the call faulted.
static int DestroyBodyPod(::Character* c)
{
    __try { return (coop::GameWorldPtr() != 0 && coop::GameWorldPtr()->destroy(c, false, "eaten")) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// T-306 TEST-ONLY lever: `destroybody <uid>` - see medical.h. MAIN THREAD (the command channel's safe point, like `kill`).
// Nothing reads `c` after the destroy: the name is read first, and the hook retires the pointer inside the call.
std::string DestroyBodyLever(unsigned int uid)
{
    ::Character* c = FindSpawned(uid);
    const char* why = 0;
    if (c == 0) why = "no character for this uid is present on this game";
    else if (!net::IsUidMine(uid)) why = "not this game's own character (a copy of another game's)";
    else if (!c->hasDied()) why = "the character is not dead";
    else if (coop::GameWorldPtr() == 0) why = "the GameWorld global (row GameWorldGlobal) is not bound";
    const std::string name = (c != 0) ? c->getShownNameDirect() : std::string("?");
    const std::string head = "destroybody uid=" + N(uid) + " name='" + name + "'";
    if (why != 0)
    {
        DebugLog("[CMD] " + head + " -> REFUSED - " + why);
        return "error " + head + " refused: " + why;
    }
    const int rc = DestroyBodyPod(c);
    if (rc == 1)
    {
        DebugLog("[CMD] " + head + " -> destroyed through GameWorld::destroy 0x798F50 'eaten' (the same road as an eaten body)");
        return "ok " + head + " destroyed";
    }
    const std::string fail = (rc == 0) ? "GameWorld::destroy returned false (the engine refused)" : "GameWorld::destroy FAULTED";
    DebugLog("[CMD] " + head + " -> NOT destroyed - " + fail);
    return "error " + head + " " + fail;
}

void InstallMedicalProbe()
{
    // par6 fold (review-par6 #1), stage 7/9: the event-kill caller ranges are address-table rows - armed when every one is bound.
    if (kDeathCheckRva != 0 && kDeathCheckEnd > kDeathCheckRva && kUnequipRva != 0 && kUnequipEnd > kUnequipRva
        && kMedUpdateDeathRet1Rva != 0 && kMedUpdateDeathRet2Rva != 0)
        g_eventKillBase = (unsigned long long)(uintptr_t)::GetModuleHandleA(0);
    DebugLog(g_eventKillBase != 0 ? "[DEATH] death requests armed: a copy's refused kill from Inventory::deathCheck or Character::unequipItem goes to the owner;"
                                    " medical-death undo armed: a copy's refused medicalUpdate death (rows MedUpdateDeathRet1/2) has its flag put back"
                                  : "[DEATH] death requests OFF, medical-death undo OFF: a caller-range row is not bound in this address table");
    InstallPuppetKoGate();   // C2 (read-KO): independent of the probe below
    InstallCopyDamageSkip();   // C2-b (read-damage): a copy's hit-path applyDamage is skipped
    InstallCopyAmputateGate();   // LIMBS: a copy loses a limb only when its owner's STATE says so
    InstallRagdollRest();   // R3 (read-ragdoll): a copy's settled ragdoll is moved to its owner's rest, inside 0x7D38D0
    DebugLog("[P014] resolving MedicalSystem::medicalUpdate ...");
    intptr_t fn = (intptr_t)coop::AddrAbs(kMig3MedicalUpdate);
    if (fn == 0)
    {
        ErrorLog("[P014] medicalUpdate resolved to 0 - medical probe NOT installed");
        return;
    }

    coop::HookStatus st =
        coop::AddHook((void*)fn, (void*)&detour_medicalUpdate, (void**)&orig_medicalUpdate);
    DebugLog(st == coop::SUCCESS
             ? "[P014] medicalUpdate AddHook SUCCESS (MEASUREMENT ONLY - changes nothing)"
             : "[P014] medicalUpdate AddHook FAILED");
}

// M4 step 3 (F126): push the authority's full state for every character we own, ~1 Hz.
// MAIN THREAD (rides the command-channel pump), so it may allocate and log freely.
//
// Why periodic rather than on-change: it makes divergence SELF-HEALING. Anything that
// drifts - a hit we could not play, an unexplained max-health difference, a spurious KO
// entered on the peer, a medical tick running at its own rate - is corrected within a
// second without anyone having to work out why it happened. Unreliable delivery is correct
// here: a dropped snapshot is superseded by the next one.
void StateTick()
{
    DrainKoRefusals();   // C2 (read-KO): main-thread log of the first refusals
    DrainDeathRefusals();   // par6 (parity P6): main-thread log of the first copy-death refusals
    DrainDeathRequests();   // par6 fold (review-par6 #1): refused event kills of copies -> death requests to their owners
    const int owned = net::OwnedUidCount();
    if (owned <= 0) return;
    int k = (owned + kStateRefreshSec - 1) / kStateRefreshSec;   // H032: the whole owned set every kStateRefreshSec
    if (k < 1) k = 1; if (k > kStateMaxPerTick) k = kStateMaxPerTick;
    InterlockedExchange64(&g_statePerTick, k);
    for (int i = 0; i < k; ++i)
    {
        unsigned int uid = 0;
        if (!net::NextOwnedUid(&uid)) return;   // false when no road to another player or nothing owned      // round-robin

        float rec[32 * kPartFloats];
        float blood = 0.0f;
        int prone = 0, dead = 0;
        unsigned int latch = 0; float nextKO = 0.0f, koT = 0.0f;
        float rest[3] = { 0.0f, 0.0f, 0.0f };   // R3
        int nParts = SnapshotState(uid, rec, 32, &blood, &prone, &dead, &latch, &nextKO, &koT);
        if (nParts <= 0) continue;

        if (!AnnouncedToPeer(uid)) continue;   // H030
        float hg[2] = { 0.0f, 0.0f };   /* par5 */ coopstate::PoseWire pose;   // POSE (read-poses, 50): the owner's in-place pose rides every periodic STATE
        net::SendState(uid, rec, nParts, blood, prone, dead, latch, nextKO, koT, CarryingUidOf(uid),
                       RestOfOwned(uid, rest) ? rest : 0,   /* R3 (read-ragdoll): where its ragdoll settled */
                       PoseOfOwned(uid, &pose) ? &pose : 0, HungerOfOwned(uid, hg),
                       LimbsOfOwned(uid));   /* LIMBS: the owner's four limbs ride every periodic STATE */
        InterlockedIncrement64(&g_stateSent);
    }
}

// C2 (read-KO) - see medical.h. Restores the previous holder, so the guard nests.
ApplyingOwnerState::ApplyingOwnerState() : prev_(g_applyingOwnerThread)
{
    InterlockedExchange(&g_applyingOwnerThread, (LONG)::GetCurrentThreadId());
}

ApplyingOwnerState::~ApplyingOwnerState()
{
    InterlockedExchange(&g_applyingOwnerThread, prev_);
}

unsigned int SnapshotLatch(Character* c, float* nextKnockoutAt, float* koTimer)
{
    if (nextKnockoutAt) *nextKnockoutAt = 0.0f;
    if (koTimer) *koTimer = 0.0f;
    if (c == 0) return 0;
    MedicalSystem* med = (MedicalSystem*)((char*)c + kMedicalOffset);
    if (!PlausiblePtr(med)) return 0;

    unsigned int bits = 0;
    if (med->crippled)       bits |= kLatchCrippled;
    if (med->unconcious)     bits |= kLatchUnconcious;
    if (med->lowHealthKnockout)        bits |= kLatchSub50KO;
    if (med->bloodLossShock)bits |= kLatchBloodTrauma;
    if (med->rightArmUsable)     bits |= kLatchRightArmOk;
    if (med->leftArmUsable)      bits |= kLatchLeftArmOk;
    if (nextKnockoutAt) *nextKnockoutAt = med->nextKnockoutAt;
    if (koTimer) *koTimer = med->knockoutClock;   // H029
    return bits;
}

void ApplyLatch(Character* c, unsigned int bits, float nextKnockoutAt, float koTimer)
{
    if (c == 0) return;
    // C2 (read-KO): the owner's word for the puppet knockout gate - knocked out = `unconcious` set, or a
    // wake-up clock the owner still has running (the same range guard as the write at the end).
    NoteOwnerKo(FindSpawnedUid(c),
                (bits & kLatchUnconcious) != 0 || (koTimer > 0.0f && koTimer < 3600.0f));
    MedicalSystem* med = (MedicalSystem*)((char*)c + kMedicalOffset);
    if (!PlausiblePtr(med)) return;

    // Written BEFORE the prone comparison in ApplyState, deliberately: `unconcious` is what the
    // peer's own collapse decision reads, so setting prone first and the flag second would leave
    // the engine one tick in which to undo us - which is precisely the 9-125 ms undo T047 caught
    // happening eight times.
    med->crippled        = (bits & kLatchCrippled)    != 0;
    med->unconcious      = (bits & kLatchUnconcious)  != 0;
    med->lowHealthKnockout         = (bits & kLatchSub50KO)     != 0;
    med->bloodLossShock = (bits & kLatchBloodTrauma) != 0;
    med->rightArmUsable      = (bits & kLatchRightArmOk)  != 0;
    med->leftArmUsable       = (bits & kLatchLeftArmOk)   != 0;
    med->nextKnockoutAt      = nextKnockoutAt;
    // H029: the wake-up clock. With it positive, the peer's own reassessCollapseMode sets `unconcious`, requests the
    // ragdoll, and threadedUpdatePeriodic keeps prone at 4 - nothing below is out-written any more.
    if (koTimer >= 0.0f && koTimer < 3600.0f) med->knockoutClock = koTimer;
}

bool StatePush(unsigned int uid)
{
    // Only ever for characters WE own. Pushing state for someone else's character is the one
    // thing replication must never do, and the check is cheap enough to keep at the door.
    if (!net::IsUidMine(uid)) return false;

    float rec[32 * kPartFloats];
    float blood = 0.0f;
    int prone = 0, dead = 0;
    unsigned int latch = 0; float nextKO = 0.0f, koT = 0.0f;
    float rest[3] = { 0.0f, 0.0f, 0.0f };   // R3
    int nParts = SnapshotState(uid, rec, 32, &blood, &prone, &dead, &latch, &nextKO, &koT);
    if (nParts <= 0) return false;

    if (!AnnouncedToPeer(uid)) return false;   // H030
    float hg[2] = { 0.0f, 0.0f };   /* par5 */ coopstate::PoseWire pose;   // POSE (read-poses, 50)
    if (!net::SendState(uid, rec, nParts, blood, prone, dead, latch, nextKO, koT, CarryingUidOf(uid),
                       RestOfOwned(uid, rest) ? rest : 0,   /* R3 (read-ragdoll): where its ragdoll settled */
                       PoseOfOwned(uid, &pose) ? &pose : 0, HungerOfOwned(uid, hg),
                       LimbsOfOwned(uid))) return false;   /* LIMBS: also on a pushed / catch-up STATE */
    InterlockedIncrement64(&g_stateSent);
    InterlockedIncrement64(&g_statePushed);
    return true;
}

void ReportMedical()
{
    std::stringstream ss;
    ss << "[P014] REPORT medCalls=" << N(g_medCalls)
       << " ours=" << N(g_medCallsOurs)
       << " theirs=" << N(g_medCallsTheirs)
       << " medThread=" << N(g_medThreadId)
       << " statePerTick=" << N(g_statePerTick) << " stateRefreshSec=" << N((long long)kStateRefreshSec)
       << " medGate=" << N(g_medGate)
       << " medGated=" << N(g_medGated)
       << " stateSent=" << N(g_stateSent)
       << " (periodic=" << N(g_stateSent - g_statePushed)
       << " onEvent=" << N(g_statePushed) << ")"
       << StatsReportFields()   /* S1 (read-stats) */
       << " puppetKoRefused=" << N(g_puppetKoRefused)          // C2 (read-KO)
       << " puppetKoAllowedOwner=" << N(g_puppetKoAllowedOwner)
       << " puppetKoAllowedApplying=" << N(g_puppetKoAllowedApplying)
       << " koRefusedRebuild=" << N(g_puppetKoRefusedRebuild)          // crash1c (R2c)
       << " ownerKoTableFull=" << N(g_ownerKoTableFull)
       << " hungerCarried=" << N(g_hungerCarried)          /* par5 (parity P5) */
       << " hungerApplied=" << N(g_hungerApplied)
       << " hungerHeld=" << N(g_hungerHeld)
       << " hungerAbsent=" << N(g_hungerAbsent)
       << " hungerRefused=" << N(g_hungerRefused)
       << " hungerTableFull=" << N(g_hungerTableFull)
       << " copyDamageSkipped=" << N(g_copyDamageSkipped)          // C2-b (read-damage)
       << " copyDamagePassedMine=" << N(g_copyDamagePassedMine)
       << " copyDamagePassedOtherCaller=" << N(g_copyDamagePassedOtherCaller)
       << " deathApplied=" << N(g_deathApplied)          /* par6 (parity P6) */
       << " deathAlready=" << N(g_deathAlready)
       << " deathDeferred=" << N(g_deathDeferred)
       << " deathFault=" << N(g_deathFault)
       << " deathNoCopy=" << N(g_deathNoCopy)
       << " deathOwnerAliveCopyDead=" << N(g_deathOwnerAliveCopyDead)
       << " spawnFlagDeadSent=" << N(g_spawnFlagDeadSent)          /* T-303 */
       << " spawnFlagKoSent=" << N(g_spawnFlagKoSent)
       << " spawnDeadAtArrival=" << N(g_spawnDeadAtArrival)
       << " spawnDeadRetried=" << N(g_spawnDeadRetried)
       << " spawnDeadGaveUp=" << N(g_spawnDeadGaveUp)
       << " spawnDeadWasDead=" << N(g_spawnDeadWasDead)
       << " spawnDeadDropped=" << N(g_spawnDeadDropped)          /* T-303 fold 1 */
       << " spawnFlagDeadRecv=" << N(g_spawnFlagDeadRecv)
       << " spawnFlagKoRecv=" << N(g_spawnFlagKoRecv)
       << " spawnDeadLookApplied=" << N(g_spawnDeadLookApplied)
       << " spawnDeadNoLook=" << N(g_spawnDeadNoLook)
       << " deathHeldForLook=" << N(g_deathHeldForLook)
       << " spawnDeadPending=" << N((long long)g_spawnDeadPending.size())
       << " copyDeathRefused=" << N(g_copyDeathRefused)
       << " copyDeathRefusedRebuild=" << N(g_copyDeathRefusedRebuild)
       << " copyDeathAllowedOwner=" << N(g_copyDeathAllowedOwner)
       << " copyDeathMedCleared=" << N(g_copyDeathMedCleared)
       << " ownerDeadTableFull=" << N(g_ownerDeadTableFull)
       << " copyWordsForgotten=" << N(g_copyWordsForgotten)
       << " killLever=" << N(g_killLever)
       << " copyDeathWorldRecord=" << N(g_copyDeathWorldRecord)   /* par6 fold (review-par6 #2) */
       << " copyDeathDeadNoClear=" << N(g_copyDeathDeadNoClear)   /* #8 */
       << " medicalDeathRefusedUndone=" << N(g_medicalDeathRefusedUndone)   /* par6b */
       << " deathReqQueued=" << N(g_deathReqQueued)               /* #1 */
       << " deathReqSent=" << N(g_deathReqSent)
       << " deathReqDropped=" << N(g_deathReqDropped)
       << " deathReqApplied=" << N(g_deathReqApplied)
       << " deathReqIgnored=" << N(g_deathReqIgnored)
       << CarryReportToken()   /* K1 (read-carry) */
       << PrisonReportToken()   /* arrest2 */
       << TreatReportToken()    /* heal1 */
       << RestReportToken()   /* R3 (read-ragdoll) */
       << GetupReportToken()   /* G1 (read-getup) */
       << SneakReportToken()   /* sneaking: STATE bit 7 */
       << PoseReportToken();   /* POSE (read-poses) */
    DebugLog(ss.str());
    DebugLog(NameReportLine());   /* names1 */
    DebugLog(SlaveReportLine());   /* slave1 */
    DebugLog(CaptureReportLine());  /* P11 */
    DebugLog(TagsReportLine());   /* tags1 */
    DrainKoRefusals();
    DrainDeathRefusals();   /* par6 */
}

// F128: `updateDamageState` (0x645DD0) does NOT loop over parts - it reads exactly two,
// cached at MedicalSystem+0x80 and +0x88, and only those two drive the aggregate state.
// Three parts collapsed on both machines in T040 and nobody went down, because the
// collapsed ones were not these. Resolving which INDEX each vital pointer is turns that
// from an offline inference into something a log line states outright.
int VitalPartIndex(MedicalSystem* med, int which)   // which: 0 -> +0x80, 1 -> +0x88
{
    if (!PlausiblePtr(med)) return -1;
    void* vital = *(void**)((char*)med + (which == 0 ? 0x80 : 0x88));
    if (!PlausiblePtr(vital)) return -1;
    int n = med->countBodyParts();
    if (n <= 0 || n > 32) return -1;
    for (int i = 0; i < n; ++i)
        if ((void*)med->partAt((unsigned __int64)i) == vital) return i;
    return -1;   // a vital part that is not in the indexed list would itself be a finding
}

// TEST HARNESS ONLY - damage a specific part directly, on a character we own.
//
// Four consecutive runs staged three-attacker brawls and produced a knockout twice, in
// opposite directions, and not at all the third and fourth time. Waiting for the AI to
// deliver a decisive blow to the right limb is not a test, it is a lottery. This makes the
// input deterministic so the QUESTION (does the peer follow the authority into KO?) can
// finally be asked without depending on luck.
//
// It writes only the authority's own copy; the existing replication carries it from there,
// which is exactly the path under test.
bool WoundPart(unsigned int uid, int part, float stun, float cut)
{
    if (!net::IsUidMine(uid))
    {
        ErrorLog("[M4] wound refused: uid " + N(uid) + " is not ours - writing a character"
                 " we do not own is the one thing replication must never do");
        return false;
    }
    Character* c = FindSpawned(uid);
    if (c == 0) { ErrorLog("[M4] wound: no local object for uid " + N(uid)); return false; }

    MedicalSystem* med = (MedicalSystem*)((char*)c + kMedicalOffset);
    if (!PlausiblePtr(med)) return false;
    int n = med->countBodyParts();
    if (part < 0 || part >= n)
    {
        ErrorLog("[M4] wound refused: part " + N(part) + " out of range (0.." + N(n - 1) + ")");
        return false;
    }
    MedicalSystem::HealthPartStatus* p = med->partAt((unsigned __int64)part);
    if (!PlausiblePtr(p)) return false;

    p->stunDamage += stun;
    p->flesh     -= cut;
    p->recomputeHealth();
    med->clampHealth();

    DebugLog("[M4] wound uid=" + N(uid) + " part=" + N(part)
             + " +stun=" + F2(stun) + " -flesh=" + F2(cut)
             + " -> f=" + F2(p->flesh) + " stun=" + F2(p->stunDamage)
             + " coll=" + F2(p->flesh - p->stunDamage + p->splintLevel)
             + " prone=" + N((int)c->poseState()));
    return true;
}

// PROBE P017 - see medical.h. Named fields, not raw offsets: the header declares every one of
// these, and a line that reads `sub50=1` explains itself in a log a month from now in a way
// that `+0x162=1` never will.
std::string LatchString(Character* c)
{
    if (c == 0) return "latch=?";
    MedicalSystem* med = (MedicalSystem*)((char*)c + kMedicalOffset);
    if (!PlausiblePtr(med)) return "latch=unreadable";

    return std::string("crip=")  + N(med->crippled        ? 1 : 0)
         + " unc="              + N(med->unconcious       ? 1 : 0)
         + " sub50="            + N(med->lowHealthKnockout          ? 1 : 0)
         + " blt="              + N(med->bloodLossShock  ? 1 : 0)
         + " mdead="            + N(med->dead             ? 1 : 0)
         + " rok="              + N(med->rightArmUsable       ? 1 : 0)
         + " lok="              + N(med->leftArmUsable        ? 1 : 0)
         + " nextKO="           + F2(med->nextKnockoutAt)
         + " koT="              + F2(med->knockoutClock);   // H029: the clock that decides the KO
}

void SetMedicalGate(bool on)
{
    InterlockedExchange(&g_medGate, on ? 1 : 0);
    DebugLog(std::string("[H012] medical gate ") + (on ? "ON" : "OFF")
             + " - characters we do NOT own "
             + (on ? "no longer run their own medical simulation; the authority's STATE is the"
                     " only thing that moves them"
                   : "simulate medicine locally again (the original behaviour)")
             + ". gatedSoFar=" + N(g_medGated));
}

void ReportKO(unsigned int uid)
{
    Character* c = FindSpawned(uid);
    if (c == 0)
    {
        DebugLog("[M4] ko: no local object for uid " + N(uid));
        return;
    }

    MedicalSystem* med = (MedicalSystem*)((char*)c + kMedicalOffset);

    // PoseState: 0 NORMAL, 1 STAYING_LOW, 2 CRIPPLED, 3 PLAYING_DEAD, 4 KO.
    // Printed as a NUMBER AND a name - a bare integer in a log is the kind of value that
    // gets misread months later, and a bare name loses the ordering.
    int prone = (int)c->poseState();
    const char* proneName =
        prone == 0 ? "NORMAL" : prone == 1 ? "STAYING_LOW" : prone == 2 ? "CRIPPLED" :
        prone == 3 ? "PLAYING_DEAD" : prone == 4 ? "KO" : "?";

    DebugLog("[M4] ko uid=" + N(uid)
             + " prone=" + N(prone) + "(" + proneName + ")"
             + " dead=" + N(c->hasDied() ? 1 : 0)
             + " blood=" + F2(med->blood)
             + " bleedRatePerSec=" + F2(med->bleedRate)
             + " vital=" + N(VitalPartIndex(med, 0)) + "," + N(VitalPartIndex(med, 1))
             + " mine=" + N(net::IsUidMine(uid) ? 1 : 0)
             + " " + LatchString(c));
}

} // namespace coop
