#pragma once
/* TEAMMAP: the team-bases PR's decisions (Shared Wastelands team-base-markers, 6e9db72..b789b73), ported onto T-811's teammates' bases
   (baseshow.h). Compiled into the game (build.cpp) and the offline suite (test_main.cpp). Pure: no disk, no clock, no engine.
   Header-only. No C++11. No player slot is special (a dedicated server has no host game).

   BUILDING AT A TEAMMATE'S OUTPOST is 0.3.4's T-923 (buildnear.h; policy.cpp's near-town and group-town stubs, build.cpp's town pick
   and hand-over): no "too close" refusal at a teammate's outpost, the piece joins it, no "Your Outpost" founded inside it. The PR's
   own CLAIM ZONE rule (ClaimVerdict, CopyTownPick and their hooks) was dropped for it in the 0.3.4 merge.
   RESEARCH AT A TEAMMATE'S BENCH (BenchRoute): the engine counts a research bench's work only for a bench of this player's own faction.
   Work at a bench whose town (else the bench itself) belongs to a teammate goes to THAT teammate's research (TEAM WORK, teamwire.h); the
   bench's "nothing to research" answer and a panel of that town follow that teammate's research (WORK STATE). A bench of this player's
   own feeds this player, as vanilla. */
#include "teamwire.h"   /* kMaxWork */

namespace teammap {

const int kMarkerOwn = 3;                /* baseshow.h's marker colour slots: 3 = MarkerPlayer, the colour of this player's own */
const unsigned long kLookMs = 1000;      /* the teammates and this player's research state are looked at this often */
const unsigned long kWorkSendMs = 1000;  /* the work gathered at teammates' benches is sent at most this often */
const unsigned long kWorkStateAgainMs = 10000;   /* an idle research says so again this often (a running one every look) */

/* where a research bench's work goes, after the engine's own tick: townSlot / benchSlot = the player slot that owns the bench's town /
   the bench (-1 = none or not a player's), mySlot = this player's, mate = that slot is in this player's team. The town's owner first. */
const int kBenchEngine = -1;   /* the engine's own answer: this player's own bench, or nobody's teammate */
inline int BenchRoute(int townSlot, int townIsMate, int benchSlot, int benchIsMate, int mySlot)
{
    if (benchSlot == mySlot && mySlot >= 0) return kBenchEngine;   /* this player's own bench: the engine counted it, as vanilla */
    if (townSlot >= 0 && townSlot != mySlot && townIsMate != 0) return townSlot;
    if (benchSlot >= 0 && benchIsMate != 0) return benchSlot;
    return kBenchEngine;
}
/* a bench's "nothing to research" answer (1 idle): a bench routed to a teammate follows that teammate's research as its game last said it
   (the engine's answer while nothing was said yet); every other bench the engine's */
inline int BenchIdleAnswer(int engine, int route, int known, int idle)
{
    if (route == kBenchEngine || known == 0) return engine;
    return idle != 0 ? 1 : 0;
}
/* RESEARCH WORK OFF THE MAIN THREAD (live 2026-10-25: a teammate's worker at the owner's bench, nothing counted, no WORK ever sent): a bench's
   work tick (vt+0x4B0, the bench's operate) and its idle test (vt+0x4E8) run inside the worker's task, on the AI worker thread (F062;
   farm.cpp T-853 #9), so a main-thread-only hook never saw one. The tick's work is gathered there into a cell per slot (OwedAdd) that
   the main thread takes and sends; the idle test reads a cell per slot the main thread writes from WORK STATE (MateCell). */
const int kCellSlots = 1024;   /* player slots 0..1023 (slotwire.h kSlotMax) */
inline int CellSlotOk(int slot) { return (slot >= 0 && slot < kCellSlots) ? 1 : 0; }
/* a teammate's research state as its cell holds it */
const int kMateUnknown = 0, kMateRunning = 1, kMateIdle = 2;
inline int MateCell(int idle) { return idle != 0 ? kMateIdle : kMateRunning; }
/* BenchIdleAnswer from a cell: a routed bench follows its teammate's research once said, else the engine's answer */
inline int BenchIdleFromCell(int engine, int route, int cell)
{
    const int known = (cell == kMateRunning || cell == kMateIdle) ? 1 : 0;
    return BenchIdleAnswer(engine, route, known, cell == kMateIdle ? 1 : 0);
}
/* the work a cell holds plus one tick's: a tick that is not a positive number (or past kMaxOwed) adds nothing; a cell that does not
   read as a number in range starts again at 0; the sum is held at kMaxOwed */
const float kMaxOwed = 1000000.0f;
inline float OwedAdd(float owed, float amount)
{
    if (!(owed >= 0.0f && owed <= kMaxOwed)) owed = 0.0f;
    if (!(amount > 0.0f && amount <= kMaxOwed)) return owed;
    const float s = owed + amount;
    return s > kMaxOwed ? kMaxOwed : s;
}
/* why BenchRoute answered as it did (the bench work summary's counts): this player's own bench, the town's owner, the bench's owner,
   or nobody's teammate */
const int kWhyOwn = 0, kWhyTown = 1, kWhyBench = 2, kWhyNobody = 3, kWhyN = 4;
inline int BenchRouteWhy(int townSlot, int townIsMate, int benchSlot, int benchIsMate, int mySlot)
{
    if (benchSlot == mySlot && mySlot >= 0) return kWhyOwn;
    if (townSlot >= 0 && townSlot != mySlot && townIsMate != 0) return kWhyTown;
    if (benchSlot >= 0 && benchIsMate != 0) return kWhyBench;
    return kWhyNobody;
}
inline const char* WhyName(int why)
{
    return why == kWhyOwn ? "own" : why == kWhyTown ? "town" : why == kWhyBench ? "bench" : why == kWhyNobody ? "nobody" : "?";
}
/* a send / arrive / apply line: the first 3, then every 300th */
inline int SayCount(long long n) { return (n >= 1 && n <= 3) || (n > 0 && n % 300 == 0) ? 1 : 0; }
/* WHOSE GAME COUNTS A BENCH TICK (the copy rule, as farmwire.h T-610 has it for production): every game runs its own copies of other
   games' characters, and a copy's work task runs the bench's work tick there too. Research is counted ONLY on the worker's own game -
   there the engine counts a bench of that player's own, and a tick at a teammate's bench is gathered and sent to that teammate (WORK).
   Counting it on any other game as well counted it twice: a teammate's copy at this player's own bench was counted by this engine AND sent
   by its own game; a copy at a teammate's bench was gathered here AND counted by the teammate's engine. So on every game but the worker's:
   a copy's tick gathers nothing (kTickEngineOnly), and a copy of a TEAMMATE's character at this player's OWN bench does not run the engine's
   tick at all (kTickSkip - its own game sends that work). A copy of anyone else (no team between them) keeps the engine's tick, as vanilla.
   workerCopy: farmwire.h WorkerIsCopy (an inexact owned mirror reads "own", the fail-safe); workerMate: the worker's faction is a
   teammate's; ownBench: BenchRoute answered kWhyOwn. Only while this player has teammates (the hook does nothing otherwise). */
const int kTickRun = 0, kTickEngineOnly = 1, kTickSkip = 2;
inline int BenchTickFor(int workerCopy, int workerMate, int ownBench)
{
    if (workerCopy == 0) return kTickRun;
    if (workerMate != 0 && ownBench != 0) return kTickSkip;
    return kTickEngineOnly;
}
/* the bench work summary line: only when a count moved, at most once a kWorkLogMs (the first at once) */
const unsigned long kWorkLogMs = 30000;
inline int SummaryDue(int moved, int saidOnce, unsigned long sinceLast) { return (moved != 0 && (saidOnce == 0 || sinceLast >= kWorkLogMs)) ? 1 : 0; }
/* the work sent now out of what was gathered: at most kMaxWork in one WORK (the rest is sent at the next) */
inline float WorkSendAmount(float owed) { return owed > swteam::kMaxWork ? swteam::kMaxWork : owed; }
/* a progress (0..1, NaN and out-of-range clamped) as the town panel's bar position 0..100 */
inline unsigned PanelPos(float frac)
{
    if (!(frac >= 0.0f)) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    return (unsigned)(frac * 100.0f + 0.5f);
}

}   // namespace teammap
