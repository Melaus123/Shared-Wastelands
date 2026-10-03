// soak.cpp - see soak.h for why this exists (M5 step 1).

#include "soak.h"
#include "spawn.h"
#include "medical.h"
#include "ai_spike.h"
#include "worldsync.h"   // P039 / F331: PauseSnapshot
#include "net/session.h"
#include "zones.h"   // M-A: sec= on every digest line

#include "coop_log.h"
#include "game/Character.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <sstream>
#include <locale>
#include <string>
#include <float.h>   // F333: _finite - a NaN speed multiplier would log every frame forever

namespace coop {

namespace {

std::string N(long long v)
{
    std::stringstream ss; ss.imbue(std::locale::classic()); ss << v; return ss.str();
}

std::string F2(float v)
{
    std::stringstream ss; ss.imbue(std::locale::classic());
    ss.setf(std::ios::fixed); ss.precision(2); ss << v; return ss.str();
}

// P039 / F331 - the terse pause fields, for lines that carry the state rather than announce it.
// `NO-GAMEPLAY` is printed as itself: a probe that prints 0 when it could not read is the F171
// defect. `stopped=` is the engine's own definition (flag OR zero multiplier - F333) and is the
// field to read; the two inputs are printed beside it so the verdict can always be checked.
std::string PauseFields()
{
    bool  paused = false;
    float mul    = 0.0f;
    if (!PauseSnapshot(&paused, &mul))
        return " stopped=NO-GAMEPLAY pausedFlag=NO-GAMEPLAY speedMul=NO-GAMEPLAY";
    // F336 - the NaN guard belongs in every CONSUMER, not only in the watcher. `NaN == 0.0f` is
    // false, so without this a non-finite multiplier would be reported as a running engine - the
    // confident-wrong answer the guard exists to prevent, printed on the line that matters most.
    if (!_finite(mul))
        return std::string(" stopped=UNKNOWN pausedFlag=") + N(paused ? 1 : 0)
             + " speedMul=NOT-FINITE";
    return std::string(" stopped=") + N((paused || mul == 0.0f) ? 1 : 0)
         + " pausedFlag=" + N(paused ? 1 : 0)
         + " speedMul=" + F2(mul);
}

// PoseState printed as number AND name, for the same reason ReportKO does it: a bare
// integer in an hour-long log is exactly the value that gets misread months later.
const char* ProneName(int p)
{
    return p == 0 ? "NORMAL" : p == 1 ? "STAYING_LOW" : p == 2 ? "CRIPPLED" :
           p == 3 ? "PLAYING_DEAD" : p == 4 ? "KO" : "?";
}

// --- transition watch ------------------------------------------------------------------
//
// One row per replicated character. Fixed size and indexed by uid rather than by mirror slot,
// because a mirror slot is an implementation detail that could be reused, and a watch that
// silently re-attaches to a different character is worse than no watch.
// Sized to kMaxMirror. See the note on kMaxCombatWatch in combat.cpp: when the mirror grew to 512
// this table did not, and a table that fails closed with no log is how "correct for the first 64,
// silently wrong after" gets rediscovered a second time.
const int kMaxWatch = 4096;   // T-354: kMaxMirror is 4096 (mirror1 (crash T487) made it 2048)
struct Watch
{
    unsigned int uid;
    int  lastProne;
    int  lastDead;
    bool primed;
};
Watch g_watch[kMaxWatch] = {0};

unsigned int g_baseTick   = 0;
long long    g_transitions = 0;
long long    g_transAuthority = 0;   // of those, how many WE wrote (F140)

// F140: notes left by `ApplyState` so a transition can be attributed rather than assumed.
// A note is only honoured for the tick it was left in - `SoakTick` runs later in the SAME pump
// call as `SessionTick`, so a stale note would mislabel a genuinely derived transition, which
// is the exact failure this exists to prevent.
const int kNoteSlots = 16;
struct ProneNote { unsigned int uid; int prone; long long frame; };
ProneNote g_notes[kNoteSlots] = {0};
long long g_frame = 0;

int g_driftPeriodSec = 15;          // 0 = off
unsigned int g_lastDriftTick = 0;
long long    g_driftLines = 0;

// High-water marks. These are the numbers that answer "was a bound ever close to being hit",
// which is a different question from "was it hit" - and the only one worth asking before it is.
int g_mirrorHigh = 0;

// --- F145 freeze watch -------------------------------------------------------------------
//
// T046: instance A's world simulation STOPPED for >=346 s while frames kept advancing at
// 9-10 ms, the mod pump kept sending state, and every counter we routinely print looked fine.
// It was noticed only because an executor took extra samples by chance - and their first draft
// nearly reported it as the two engines disagreeing about health, which would have been a major
// false finding. **A frozen engine derives nothing, so EVERY comparison against it is void.**
//
// An unattended 45-60 minute soak has no chance of catching this by eye, so the run has to say
// so itself. Edge-triggered: one line when it starts, one when it ends, and a periodic reminder
// while it lasts - never one line per frame.
long long    g_lastActivity   = -1;
unsigned int g_activityStamp  = 0;      // when activity last CHANGED
bool         g_frozen         = false;
long long    g_freezeEvents   = 0;
unsigned int g_freezeTotalMs  = 0;
unsigned int g_lastFreezeNag  = 0;

// How long the counters may stand still before we call it. Generous on purpose: a loading
// screen or a stall is not a freeze, and a false alarm in a soak log is its own kind of noise.
const unsigned int kFreezeMs = 20000;

// --- P039 / F331: pause watch --------------------------------------------------------------
//
// Edge-triggered, for the same reason the freeze watch is: a level printed every frame is not a
// measurement, it is noise, and what this question needs is the MOMENT the level changed. If a
// pause and a freeze share a timestamp, the freeze is explained; if the pause line never appears,
// the pause explanation is dead and the threads are the story. Either outcome is reportable, which
// is the point.
//
// `-1` is "never sampled" and is deliberately not `0`: "we have not looked" and "not paused" are
// different facts, and priming on the first sample would otherwise print a fake transition at t=0.
//
// F333 - **and the priming sample must be spent IN GAME.** The first version primed on the first
// pump tick, which is at the TITLE SCREEN, where the multiplier reads 0 from the static image and
// then changes during load - so every run would have manufactured an edge and `pauseEdges=0` could
// never occur. `PauseSnapshot` now refuses before gameplay starts, which moves priming to the
// first in-game tick on its own.
int   g_lastPaused    = -1;
float g_lastSpeedMul  = -1.0f;
// T-251 (H-OVL-COLD) - the g_frame of the last finite in-game sample of the stopped state. SimulationStopped() answers only
// for the frame it was sampled in: g_lastPaused is never reset, so without this a reader could take the title's or an
// earlier world's value.
long long g_stopSampleFrame = -1;

// F333 - TWO COUNTERS, NOT ONE. The first version incremented a single `pauseEdges` whenever
// EITHER field changed, so every 1x -> 5x speed change counted as a pause and printed a line headed
// PAUSE CHANGED. `paused=0 pauseEdges=7` would have been read as "paused seven times" - the exact
// label defect this project keeps hitting. `pauseEdges` now counts ONLY transitions of the stopped
// state; speed changes have their own counter and their own wording.
long long g_pauseEdges = 0;    // transitions of (paused || speed==0)
long long g_speedEdges = 0;    // multiplier changed while the stopped state did not

// F333 - a separate flag rather than `0` as a sentinel. `ElapsedMs()` returns 0 on its first call,
// so a pause detected at exactly ms==0 would have recorded a start time indistinguishable from
// "not timing", and that interval would have been silently dropped from the total.
bool         g_pauseTiming   = false;
unsigned int g_pausedSinceMs = 0;
unsigned int g_pausedTotalMs = 0;
bool         g_saidSpeedNaN  = false;   // F333: NaN multiplier reported once, not per frame
long long    g_speedNaNTicks = 0;       // F336: ...and how many ticks were actually lost to it

// F337 - "is gameplay running THIS tick", derived from whether the in-game frame counter advanced.
// `0` rather than `-1` deliberately: at the menu the counter reads 0 and must compare EQUAL, or the
// very first title-screen tick would report gameplay as running.
long long    g_lastFramesSeen  = 0;
bool         g_gameplayRunning = false;

Watch* WatchFor(unsigned int uid)
{
    int free = -1;
    for (int i = 0; i < kMaxWatch; ++i)
    {
        if (g_watch[i].uid == uid) return &g_watch[i];
        if (g_watch[i].uid == 0 && free < 0) free = i;
    }
    if (free < 0)
    {
        static bool s_said = false;
        if (!s_said)
        {
            s_said = true;
            ErrorLog("[M5] transition watch table FULL (" + N((long long)kMaxWatch)
                     + ") - uid " + N((long long)uid) + " is UNWATCHED, and so is every new uid until a row"
                     " frees (a row goes with its uid's mirror row since the mirror1 fold). Reported once.");
        }
        return 0;
    }
    g_watch[free].uid       = uid;
    g_watch[free].lastProne = -1;
    g_watch[free].lastDead  = -1;
    g_watch[free].primed    = false;
    return &g_watch[free];
}

unsigned int ElapsedMs()
{
    unsigned int now = ::GetTickCount();
    if (g_baseTick == 0) g_baseTick = now;
    return now - g_baseTick;
}

// PROBE P068 (C2, read-KO; REMOVE once the carried question is answered - 04-probes.md). Is an owner that
// reads 'standing + unconscious + timer' being CARRIED? `_isBeingCarried` Character+0x3D4 (byte) and
// `inSomething` +0x2F8 (int), both from medical.md (getCharacterDown) and replicate.cpp's CharDbg. No
// std::string in here (C2712). -1 = the read faulted.
void ReadCarriedProbe(Character* c, int* carried, int* inSomething)
{
    *carried = -1; *inSomething = -1;
    __try
    {
        *carried     = (int)*(const unsigned char*)((const char*)c + 0x3D4);
        *inSomething = *(const int*)((const char*)c + 0x2F8);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// PROBE P071 (T259; REMOVE once the copy speed and placement-height questions are answered - 04-probes.md).
// Run-speed inputs, pure field reads (Character: animation +0x448, stats +0x450, movement +0x640).
// v[0] CharStats+0x17C runSpeed, v[1] +0x94 _athletics, v[2] +0x18 athleticsMultiplier, v[3] +0x190
// encumbranceMult, v[4] AnimationClass+0x19C, v[5] CharMovement+0xB4 speedCap. -9999 = unreachable/faulted.
void ReadSpeedProbe(Character* c, float* v)
{
    for (int i = 0; i < 6; ++i) v[i] = -9999.0f;
    __try
    {
        const char* st = *(const char* const*)((const char*)c + 0x450);
        if (st)
        {
            v[0] = *(const float*)(st + 0x17C);
            v[1] = *(const float*)(st + 0x94);
            v[2] = *(const float*)(st + 0x18);
            v[3] = *(const float*)(st + 0x190);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    __try
    {
        const char* an = *(const char* const*)((const char*)c + 0x448);
        if (an) v[4] = *(const float*)(an + 0x19C);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    __try
    {
        const char* mv = *(const char* const*)((const char*)c + 0x640);
        if (mv) v[5] = *(const float*)(mv + 0xB4);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// The rolling digest. One line per replicated character, every g_driftPeriodSec seconds, in
// a format the two instances' logs can be diffed against each other line for line. The whole
// run becomes a time series instead of the handful of samples an executor had time to take.
void DriftDigest()
{
    for (int i = 0; i < MirrorCapacity(); ++i)
    {
        unsigned int uid = 0;
        Character* c = 0;
        if (!MirrorSlot(i, &uid, &c)) continue;

        float rec[32 * kPartFloats];
        float blood = 0.0f;
        int prone = 0, dead = 0;
        unsigned int latch = 0; float nextKO = 0.0f, koT = 0.0f;
        int n = SnapshotState(uid, rec, 32, &blood, &prone, &dead, &latch, &nextKO, &koT);
        if (n <= 0) continue;

        std::string fs;
        for (int p = 0; p < n; ++p)
        {
            if (p) fs += "|";
            fs += F2(rec[p * kPartFloats + 0]) + ":" + F2(rec[p * kPartFloats + 1]);
        }

        // PROBE P068: owner lines only (mine=1) - the question is about the OWNER's body.
        std::string carryProbe;
        if (net::IsUidMine(uid))
        {
            int carried = -1, inSomething = -1;
            ReadCarriedProbe(c, &carried, &inSomething);
            carryProbe = " carried=" + N(carried) + " inSomething=" + N(inSomething);
        }
        float sp[6]; ReadSpeedProbe(c, sp);   // PROBE P071
        const std::string speedProbe = " P071 run=" + F2(sp[0]) + " ath=" + F2(sp[1]) + " athMul=" + F2(sp[2])
            + " enc=" + F2(sp[3]) + " animMax=" + F2(sp[4]) + " mvMax=" + F2(sp[5]);
        ++g_driftLines;
        DebugLog("[M5] drift t=" + N(ElapsedMs() / 1000) + "s uid=" + N(uid)
                 + " mine=" + N(net::IsUidMine(uid) ? 1 : 0)
                 + " prone=" + N(prone) + " dead=" + N(dead)
                 + " blood=" + F2(blood)
                 + " " + LatchString(c)
                 + carryProbe   // PROBE P068
                 + speedProbe   // PROBE P071
                 // F155: WHAT IS IT DOING. The layer this project was blind to for 50 runs.
                 + " " + BehaviourString(c)
                 + " sec=" + SectorString(SectorOf(c->worldPosition().x, c->worldPosition().z))
                 // P024/F200: DOES IT LOOK LIKE IT IS FIGHTING. The layer this project was
                 // blind to for 60 runs, and the one the user could see from the sofa while
                 // every instrument here reported a pass.
                 + " " + CombatString(c)
                 + " parts=" + N(n) + " fs=" + fs);
    }
}

// Was this exact transition written by us this tick? Consumes the note either way, so one
// authoritative write can never label two transitions.
bool ClaimAuthorityNote(unsigned int uid, int prone)
{
    for (int i = 0; i < kNoteSlots; ++i)
    {
        if (g_notes[i].uid == uid && g_notes[i].prone == prone
            && g_notes[i].frame >= g_frame - 1)
        {
            g_notes[i].uid = 0;
            return true;
        }
    }
    return false;
}

} // namespace

void NoteAuthoritativeProne(unsigned int uid, int prone)
{
    for (int i = 0; i < kNoteSlots; ++i)
    {
        if (g_notes[i].uid == 0 || g_notes[i].frame < g_frame - 1)
        {
            g_notes[i].uid   = uid;
            g_notes[i].prone = prone;
            g_notes[i].frame = g_frame;
            return;
        }
    }
}

void SetDriftPeriod(int seconds)
{
    g_driftPeriodSec = seconds < 0 ? 0 : seconds;
    DebugLog("[M5] drift digest " + std::string(g_driftPeriodSec ? "every " : "OFF")
             + (g_driftPeriodSec ? N(g_driftPeriodSec) + "s" : std::string("")));
}

void SoakTick()
{
    unsigned int ms = ElapsedMs();
    ++g_frame;

    // F337 - **IS GAMEPLAY RUNNING RIGHT NOW**, which is not the same question as "has gameplay
    // ever started". `MainLoopFrames()` is cumulative, so a gate built on `> 0` stays open forever
    // once a save has been loaded - and after a quit to menu the title screen would fire
    // `WORLD FROZEN` all over again, which is the very thing F336 claimed to have fixed.
    //
    // This tick rides BOTH pumps, and only the in-game pump increments that counter. So "did it
    // advance since the previous tick" is an exact, event-driven answer: in game it advances every
    // call; at the menu it never does. It also stays correct during the failure this whole probe
    // exists for - a frozen engine still runs its main loop, so the gate stays open and the freeze
    // is still detected.
    /* P7v (design-noworld-queue 3.2): MOVED to SoakRefreshRunning(), which CommandChannelTick calls
       as its first statement. It was computed here, below net::SessionTick, so every predicate read
       taken during the session pump answered from the previous frame. */

    int used = MirrorUsed();
    if (used > g_mirrorHigh) g_mirrorHigh = used;

    // --- F145 freeze watch, before anything else, because everything else depends on it -----
    //
    // F336 - **AND NOT AT THE TITLE SCREEN.** This tick rides both pumps, and at the menu the
    // engine's activity counters never move because there is no world to simulate - so twenty
    // seconds at the title screen fired `WORLD FROZEN`, incremented `freezeEvents` and accumulated
    // `frozenTotal` on every run this project has ever done. The detector was right about its own
    // measurement and wrong about what it meant, which is why the fix belongs here rather than in
    // the threshold. F333 introduced exactly the gate this needed and did not apply it.
    // F337 - and the gate is "running now", not "has ever run" - see the note at the top.
    if (g_gameplayRunning)
    {
        long long act = WorldActivityTotal();
        if (act != g_lastActivity)
        {
            if (g_frozen)
            {
                unsigned int held = ms - g_activityStamp;
                g_freezeTotalMs += held;
                DebugLog("[M5] WORLD RESUMED after " + N(held / 1000) + "s frozen"
                         + " - every A/B comparison spanning that window is VOID");
                g_frozen = false;
            }
            g_lastActivity  = act;
            g_activityStamp = ms;
        }
        else if (!g_frozen && ms - g_activityStamp >= kFreezeMs)
        {
            g_frozen = true;
            ++g_freezeEvents;
            g_lastFreezeNag = ms;
            ErrorLog("[M5] WORLD FROZEN - engine activity counters have not moved for "
                     + N((ms - g_activityStamp) / 1000) + "s (aiTotal+tuTotal+upTotal="
                     + N(act) + "). Frames may still be advancing and the pump may still be"
                     + " sending; THIS INSTANCE IS NOT SIMULATING. Comparisons against it are"
                     + " meaningless until it resumes (F145)."
                     // F330 - AND SAY WHAT STOPPED. T091's host froze at ~593 s and never
                     // resumed, voiding an entire arm, and eight repetitions of the line above
                     // said nothing beyond "still frozen". The one question that splits the
                     // possibilities is whether the engine's worker THREAD is still alive: a
                     // thread that has EXITED is a different failure from one that is blocked,
                     // and they need different investigations.
                     + FreezeDiagnosis());
        }
        else if (g_frozen && ms - g_lastFreezeNag >= 60000)
        {
            g_lastFreezeNag = ms;
            // F331 - the repeat carries the pause state too. T091's eight repetitions said nothing
            // beyond elapsed time, and a state that CHANGES during a long freeze (paused, then
            // unpaused, still frozen) is a different fault from one that does not.
            ErrorLog("[M5] WORLD STILL FROZEN, " + N((ms - g_activityStamp) / 1000) + "s"
                     + PauseFields());
        }
    }

    // --- P039 / F331 pause watch, right beside the freeze watch because they answer one question -
    {
        bool  paused = false;
        float mul    = 0.0f;
        if (PauseSnapshot(&paused, &mul))
        {
            // F333 - the multiplier is a float straight out of the engine. A NaN compares unequal
            // to itself forever, which would put a log line on EVERY pump tick and destroy both
            // counters. It is also worth saying out loud if it ever happens.
            if (!_finite(mul))
            {
                ++g_speedNaNTicks;
                if (!g_saidSpeedNaN)
                {
                    g_saidSpeedNaN = true;
                    // F336 - say what actually happens. The first wording claimed the watch was
                    // "suspended for the rest of the run" and that every later figure was
                    // incomplete; it is not - only the non-finite TICKS are skipped and the watch
                    // resumes on the next finite sample. A disclaimer that overstates its own
                    // damage makes a reader throw away good data, which is the same defect as one
                    // that understates it.
                    ErrorLog("[P039] frameSpeedMultiplier is NOT FINITE. These ticks are SKIPPED by"
                             " the pause watch and the watch resumes on the next finite sample -"
                             " `speedNaNTicks=` says how many were lost. Reported once.");
                }
            }
            else
            {
            // F333 - STOPPED is the engine's own definition: the flag OR a zero multiplier.
            // `togglePause` computes the flag that way and `setFrameSpeedMultiplier` does not
            // touch the flag, so the flag alone is not the question.
            int stopped = (paused || mul == 0.0f) ? 1 : 0;
            g_stopSampleFrame = g_frame;   /* T-251: sampled THIS frame - g_lastPaused equals `stopped` after this block */

            if (g_lastPaused != stopped)
            {
                if (g_lastPaused >= 0)   // not the priming sample
                {
                    ++g_pauseEdges;
                    DebugLog("[P039] STOP STATE CHANGED t=" + N(ms / 1000) + "s stopped="
                             + N(g_lastPaused) + "->" + N(stopped)
                             + " (pausedFlag=" + N(paused ? 1 : 0)
                             + " speedMul=" + F2(g_lastSpeedMul) + "->" + F2(mul) + ")"
                             + (stopped ? " - simulation is held from here; every A/B comparison"
                                          " spanning this window is suspect for the same reason a"
                                          " freeze voids one"
                                        : " - simulation resumes here"));
                }
                if (stopped && !g_pauseTiming) { g_pauseTiming = true; g_pausedSinceMs = ms; }
                else if (!stopped && g_pauseTiming)
                {
                    g_pausedTotalMs += ms - g_pausedSinceMs;
                    g_pauseTiming = false;
                }
                g_lastPaused = stopped;
            }
            else if (g_lastPaused >= 0 && g_lastSpeedMul != mul)
            {
                // A speed change that did NOT start or end a stop. Its own counter and its own
                // wording, because calling this a pause is what made the old counter unreadable.
                ++g_speedEdges;
                DebugLog("[P039] SPEED CHANGED t=" + N(ms / 1000) + "s speedMul="
                         + F2(g_lastSpeedMul) + "->" + F2(mul) + " (stopped=" + N(stopped)
                         + " throughout - this is NOT a pause)");
            }
            g_lastSpeedMul = mul;
            }
        }
    }

    // --- the event-driven half. This is the part T041 needed and did not have. -----------
    for (int i = 0; i < MirrorCapacity(); ++i)
    {
        unsigned int uid = 0;
        Character* c = 0;
        if (!MirrorSlot(i, &uid, &c)) continue;

        int prone = (int)c->poseState();
        int dead  = c->hasDied() ? 1 : 0;

        Watch* w = WatchFor(uid);
        if (w == 0) continue;

        if (!w->primed)
        {
            // The first sample is a baseline, not a transition - reporting it as one would
            // put a fake event at t=0 in every run.
            w->primed = true;
            w->lastProne = prone;
            w->lastDead  = dead;
            DebugLog("[M5] watch uid=" + N(uid) + " baseline prone=" + N(prone)
                     + "(" + ProneName(prone) + ") dead=" + N(dead)
                     + " mine=" + N(net::IsUidMine(uid) ? 1 : 0));
            continue;
        }

        // F140: if OUR character's state just moved, tell the peer NOW rather than waiting for
        // the round-robin to come back round to it. The watch already runs every frame on every
        // replicated character, so the detection is free - only the send is new. A knockout that
        // lasts 9 ms is carried by this and by nothing else we have.
        if ((prone != w->lastProne || dead != w->lastDead) && net::IsUidMine(uid))
            StatePush(uid);

        if (prone != w->lastProne)
        {
            ++g_transitions;
            // F140: say WHO caused it.
            //
            // F206 - RENAMED, because the old names read backwards to their own author. They
            // were `authority` and `derived`, and in T062 I read "A: authority=0" on the
            // AUTHORITY instance as a possible label bug and flagged it for investigation. The
            // label was right and my reading was wrong: it never meant "this instance is the
            // authority", it meant "the owner wrote this state onto us". The owner therefore
            // correctly reports zero of them, because nobody writes to the owner.
            //
            // A name that misleads the person who wrote it will mislead everyone. So now:
            //   ownerWrite  = this state arrived as an authoritative write from the owner.
            //                 NOT evidence of disagreement.
            //   thisEngine  = the engine ON THIS MACHINE reached it by itself. On a peer, this
            //                 is the only kind that counts as divergence.
            bool byAuthority = ClaimAuthorityNote(uid, prone);
            if (byAuthority) ++g_transAuthority;
            // P017 rides the transition line rather than a poll, because that is the ONE
            // instant the latch block's value is decisive. T042's episodes were 9-127 ms
            // long - a periodic sample would land inside one by luck, not by design.
            DebugLog("[M5] TRANSITION t=" + N(ms) + "ms uid=" + N(uid) + " prone "
                     + N(w->lastProne) + "(" + ProneName(w->lastProne) + ") -> "
                     + N(prone) + "(" + ProneName(prone) + ")"
                     + " mine=" + N(net::IsUidMine(uid) ? 1 : 0)
                     + " src=" + std::string(byAuthority ? "ownerWrite" : "thisEngine")
                     + " " + LatchString(c));
            w->lastProne = prone;
        }
        if (dead != w->lastDead)
        {
            ++g_transitions;
            DebugLog("[M5] TRANSITION t=" + N(ms) + "ms uid=" + N(uid) + " dead "
                     + N(w->lastDead) + " -> " + N(dead)
                     + " mine=" + N(net::IsUidMine(uid) ? 1 : 0));
            w->lastDead = dead;
        }
    }

    // --- the periodic half ---------------------------------------------------------------
    if (g_driftPeriodSec > 0)
    {
        unsigned int periodMs = (unsigned int)g_driftPeriodSec * 1000u;
        if (g_lastDriftTick == 0 || ms - g_lastDriftTick >= periodMs)
        {
            g_lastDriftTick = ms;
            DriftDigest();
        }
    }
}

// mirror1 fold (review-mirror1 #1) - A WATCH ROW GOES WITH ITS UID. MAIN THREAD (MirrorRelease, SpawnWorldTeardown).
// Rows were keyed by uid and never freed, not even at a world teardown, so after kMaxWatch distinct uids in a session every
// later character's knockout / death transition went unwatched. WatchFor already reuses a free row (uid 0).
int SoakForgetUid(unsigned int uid)
{
    if (uid == 0) return 0;
    int n = 0;
    for (int i = 0; i < kMaxWatch; ++i)
    {
        if (g_watch[i].uid != uid) continue;
        g_watch[i].uid = 0; g_watch[i].lastProne = -1; g_watch[i].lastDead = -1; g_watch[i].primed = false;
        ++n;
    }
    return n;
}

void SoakWorldTeardown()
{
    for (int i = 0; i < kMaxWatch; ++i)
    {
        g_watch[i].uid = 0; g_watch[i].lastProne = -1; g_watch[i].lastDead = -1; g_watch[i].primed = false;
    }
}

void ReportSoak()
{
    DebugLog("[M5] REPORT mirrorUsed=" + N(MirrorUsed())
             + "/" + N(MirrorCapacity())
             + " mirrorHigh=" + N(g_mirrorHigh)
             + " transitions=" + N(g_transitions)
             + " (ownerWrite=" + N(g_transAuthority)
             + " thisEngine=" + N(g_transitions - g_transAuthority) + ")"
             + " driftLines=" + N(g_driftLines)
             + " driftPeriod=" + N(g_driftPeriodSec) + "s"
             + " frozenNow=" + N(g_frozen ? 1 : 0)
             + " freezeEvents=" + N(g_freezeEvents)
             + " frozenTotal=" + N(g_freezeTotalMs / 1000) + "s"
             // P039 / F331. On EVERY report, not only on a freeze edge - a run that ends without
             // ever freezing still has to be able to say whether it was ever stopped, or "the
             // engine was never paused" is an assumption rather than a reading. `pauseEdges=0`
             // beside `stopped=0` is an exercised zero; `NO-GAMEPLAY` is neither.
             + PauseFields()
             + " pauseEdges=" + N(g_pauseEdges)
             + " speedEdges=" + N(g_speedEdges)
             + " speedNaNTicks=" + N(g_speedNaNTicks)
             + " pausedTotal=" + N((g_pausedTotalMs
                                    + (g_pauseTiming ? ElapsedMs() - g_pausedSinceMs : 0)) / 1000)
             + "s"
             + " uptime=" + N(ElapsedMs() / 1000) + "s");
}

// F337 - exposed so `PauseSnapshot` can use the same "running now" test the freeze watch uses.
// Defined outside the anonymous namespace; the state it reads is updated once per `SoakTick`.
/* P7v (design-noworld-queue 3.2) - THE ONE WRITER, LIFTED TO THE TOP OF THE FRAME. See soak.h. */
void SoakRefreshRunning()
{
    long long f = MainLoopFrames();
    g_gameplayRunning = (f != g_lastFramesSeen);
    g_lastFramesSeen  = f;
}
bool GameplayRunning() { return g_gameplayRunning; }
/* T-251 (H-OVL-COLD) - see soak.h. */
int SimulationStopped() { return (g_lastPaused >= 0 && g_stopSampleFrame == g_frame) ? g_lastPaused : -1; }

} // namespace coop
