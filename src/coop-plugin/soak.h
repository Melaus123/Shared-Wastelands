// soak.h - M5 step 1: instrumentation built for an HOUR, not for a moment.
//
// Every probe this project has written so far answers one question inside one short window.
// A soak asks a different kind of question - does any of this survive 45-60 minutes of
// ordinary play - and three of the things it must catch cannot be caught by the existing
// tools at all:
//
//   * SLOW DRIFT. `blood` diverged and froze at -6.93 (T039) and -3.76 (T040) and we still
//     cannot say why. Six spot samples make that an anecdote; a time series makes it a rate.
//
//   * TRANSIENT STATES. T041's knockout existed for <=2 s - SHORTER THAN THE COMMAND
//     CHANNEL'S OWN ACK LATENCY (0.72-2.89 s). No polled readout can sample that, and none
//     did: `4(KO)` appears zero times in either instance's log, and the only evidence the KO
//     happened at all is a correction line. So the prone/death watch here is EVENT-DRIVEN -
//     it rides the main pump, compares against the last value, and logs the TRANSITION.
//
//   * BOUNDS NOBODY WATCHES. The uid mirror is a fixed 64 slots and the hit ring is 64.
//     Both are bounded by design and neither has ever been near full, because nothing has
//     ever run long enough. A bound that is never reported is not a bound.
//
// Everything here is MAIN THREAD and read-only with respect to the game: it samples values
// the engine owns and writes nothing back. A measurement that perturbs the thing it measures
// would make the drift rate this exists to find into the drift rate of our own interference.
#pragma once

namespace coop {

// Rides the in-game pump, every frame. Cheap by construction: a handful of aligned reads per
// replicated character, and a log line only when something actually CHANGES.
void SoakTick();

// Seconds between rolling drift digests; 0 disables them. The transition watch is unaffected
// and always runs - it is the part that cannot be reconstructed after the fact.
void SetDriftPeriod(int seconds);

// Leak/bound counters and the current drift setting (extends the report command).
void ReportSoak();

// mirror1 fold (review-mirror1 #1): a transition-watch row goes with its uid's mirror row (returns the rows freed), and
// every row goes at a world teardown. MAIN THREAD.
int  SoakForgetUid(unsigned int uid);
void SoakWorldTeardown();

// F140: the transition watch OVER-COUNTS peer disagreements. Two of B's nine transitions in
// T043 were written by the authority, and the log line could not tell that apart from one the
// peer derived - the only way to see it was that a `[M4] STATE corrected` line happened to
// carry the same millisecond. A count that conflates "the peer disagreed" with "we overruled
// the peer" is exactly the kind of number this project has been bitten by six times.
//
// So the applier says so. Called from `ApplyState` immediately before it writes; the watch
// runs later in the same pump tick and attributes the transition it then sees.
void NoteAuthoritativeProne(unsigned int uid, int prone);

// F333 - in-game main-loop frames so far; 0 means gameplay has not started. Defined in coop.cpp
// beside the counter it returns. **This is the only honest "is there a world" test available**:
// F034 established that the GameWorld is a static inside the exe image, so `ou` is non-null from
// preload and no pointer check can tell the title screen from a loaded game.
long long MainLoopFrames();

// F337 - is gameplay running RIGHT NOW, which `MainLoopFrames() > 0` cannot answer: that counter is
// cumulative, so a gate built on it stays open forever once a save has been loaded and the title
// screen after a quit-to-menu looks identical to a live game. This is recomputed once per
// `SoakTick` from whether the in-game frame counter advanced since the previous tick - exact,
// event-driven, and still true during a freeze (a frozen engine keeps running its main loop).
bool GameplayRunning();
/* T-251 (H-OVL-COLD) - the engine's own STOPPED state (pause flag OR a zero speed multiplier - the P039 rule) as the pause
   watch sampled it THIS frame: 1 stopped, 0 running, -1 not sampled this frame (not primed yet, no gameplay this tick, a
   non-finite multiplier, no plausible world). SoakTick runs every frame in every game (CommandChannelTick, no switch) and
   before StoreTick, so a store-side reader in the same frame gets this frame's sample, never an earlier world's. MAIN THREAD. */
int SimulationStopped();
/* P7v (design-noworld-queue 3.2) - RECOMPUTE THE FLAG ABOVE ONCE, AT THE TOP OF THE FRAME.
   g_gameplayRunning has exactly one writer and it used to be SoakTick, which runs BELOW
   net::SessionTick in CommandChannelTick - so every reader inside the session pump was using LAST
   FRAME'S value. That is the same staleness review-p7f H-1 filed against the load gate, in a
   different place. This is called as the FIRST statement of CommandChannelTick and SoakTick reads
   the flag instead of computing it (lesson 11: when two things are kept in step by hand, remove the
   hand). Nothing about what the flag MEASURES has changed. MAIN THREAD. */
void SoakRefreshRunning();

} // namespace coop
