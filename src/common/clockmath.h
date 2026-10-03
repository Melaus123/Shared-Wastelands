/* src/common/clockmath.h - THE CLOCK ARITHMETIC, AND NOTHING ELSE (P7u, design-clock46 2a).
 *
 * THE ONE RULE THIS FILE EXISTS TO ENFORCE: nothing in here reads engine memory, reads a global, calls the
 * operating system or includes an Ogre, ENet or Windows header. Everything is a pure function of its
 * arguments. That is not tidiness - it is what makes review-p7j H-1's post-condition ("the throttle can never
 * write a lower time") a swept ASSERTION in an offline test program rather than a claim in a comment. The same
 * translation unit is compiled into the game plugin, into the notebook (SharedWastelandsServer.exe) and into the offline
 * test exe, so the three cannot hold different constants or different arithmetic.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no >> template closer.
 */
#ifndef COOP_COMMON_CLOCKMATH_H
#define COOP_COMMON_CLOCKMATH_H

#include <cstddef>
#include <map>
#include <vector>

namespace coopclock {

/* What one pass decided. `writeAbs` is meaningful only for kClockJump, kClockThrottle and kClockForwardSet. */
enum ClockActKind
{
    kClockNothing      = 0,   /* inside the dead band, or the engine advanced nothing since the last pass */
    kClockJump         = 1,   /* THE ONE JOIN JUMP - either direction; BACKWARD bounded by joinBoundMinutes, forward any size (decision 46 + 46b + 46c) */
    kClockThrottle     = 2,   /* forward, bounded by a fraction of the MEASURED engine advance */
    kClockForwardSet   = 3,   /* behind by more than the throttle band - one forward set */
    kClockAheadNoWrite = 4,   /* this game is ahead of the shared clock: nothing is written, ever */
    kClockOverBound    = 5,   /* a forward set past the 24-hour bound - refused and reported */
    kClockJumpOverBound = 6   /* P8a (F637) + clock1 (46c): the join jump is BACKWARD past the seven-day bound (this game AHEAD) - refused, nothing written */
};

struct ClockAct
{
    int    kind;               /* one of ClockActKind */
    double writeAbs;           /* the ABSOLUTE clock+0xA0 value to write */
    double gapMinutes;         /* SIGNED: POSITIVE = this game is BEHIND the shared clock */
    double engineAdvanceHours; /* MEASURED: localAbs - prevLocalAbs, or < 0 when there is no previous pass */
    double capHours;           /* the per-pass cap that bounded this write (0 when none applied) */
};

/* ONE struct so the test exe and the plugin cannot hold different constants. */
struct ClockTuning
{
    double deadBandMinutes;      /* 1.0   - below what the in-game clock face resolves */
    double throttleFraction;     /* 0.5   - "gentle": the sun moves at most 1.5x while a gap is open */
    double forwardSetMinutes;    /* 30.0  - above this a behind-gap is SET rather than throttled */
    double forwardBoundHours;    /* 24.0  - review-p7j H-3: a forward set beyond this is refused, not made */
    /* P8a, folding F637 / review-p7y HIGH-1.  SEVEN IN-GAME DAYS, THE SAME WIDTH THE NOTEBOOK REFUSES A SEED
       OFFER AT (amendment 46b), AND IT BINDS THE JOIN JUMP IN BOTH DIRECTIONS.  P7y made the notebook refuse an
       offer wider than this and wrote, in the relay's own line, "that game keeps <h>" - which was false: the
       join jump was headed "either direction, no bound" and, one second later, took the refused game to the
       world's clock anyway.  A 299-day BACKWARD jump parks every stamped deadline in that world until the clock
       climbs back past it (read-e40 2c), so the repair the amendment's own text describes is to refuse the jump
       at the same width: the two games then run on their own clocks and BOTH sides say so. */
    double joinBoundMinutes;     /* 10080.0 = 7 * 24 * 60.  clock1 (user 2026-09-26, amendment 46c): binds the BACKWARD join jump ONLY - a joiner behind the world takes its clock at any distance */
};
ClockTuning ClockDefaultTuning();

/* THE WHOLE DECISION, AND THE ONLY PLACE IT IS MADE.
     localAbs      - clock+0xA0 as read THIS pass
     sharedAbs     - the notebook's clock, interpolated to now
     prevLocalAbs  - clock+0xA0 as it stood when the PREVIOUS pass ENDED (after our own write);
                     < 0.0 means "no previous pass in this world epoch" -> no throttle this pass
     joinJumpOwed  - 1 = this game has not yet adopted this world's clock
   POST-CONDITION, asserted by the offline suite over a swept grid:
     kind == kClockThrottle || kind == kClockForwardSet  ==>  out.writeAbs >= localAbs
   i.e. the throttle can never produce a lower time. The JUMP can, and only the jump.
   P7w (F582 / review-p7u H-1) - AND THE POST-CONDITION IS NOW ACTUALLY WHAT THE CALLER RELIES ON.
   Until P7w the caller routed kClockForwardSet to ClockJoinSetPod, which re-read nothing and refused
   nothing, so this swept property held of the arithmetic and was DISCARDED at the write. Both writing
   kinds now go through ONE writer during play - store.cpp's ClockAdvanceToPod - which re-reads
   day + controller+0x1C at the moment of the write and refuses a target below it. This comment used to
   describe a guarantee only half the code kept. */
ClockAct ClockDecide(double localAbs, double sharedAbs, double prevLocalAbs,
                     int joinJumpOwed, const ClockTuning& t);

/* The notebook's clock, interpolated from the last CLOCK message to now. Pure - the clock reading is the
   caller's. Returns < 0 when there is no shared clock. */
double ClockSharedHoursAt(double srvHours, double srvAtMs, double nowMs,
                          float srvSpeed, float hourRealSeconds);

/* The four speeds the engine's own buttons set. THREE call sites used to write this set out by hand
   (store.cpp's vote note, the speedvote lever, the relay's OnSpeedVote) - one predicate removes the hand. */
int ClockSpeedIsValid(float speed);

/* MSG_CLOCK down the link, both ends. 14 bytes - THE SHAPE DOES NOT CHANGE (design-clock46 6).
   ClockParseDown returns 0 for n < 13 and writes NONE of its out-parameters in that case; 1 for n >= 13,
   reading seedState only when n >= 14 (a notebook one protocol behind still carries a usable clock). */
void ClockEncodeDown(char out14[14], double hours, float speed,
                     unsigned char modeByte, unsigned char seedState);
int  ClockParseDown(const char* p, std::size_t n, double* hours, float* speed,
                    unsigned char* modeByte, unsigned char* seedState);

/* THE NOTEBOOK'S SIDE. What one game's reported clock does to this world's clock - and it only ever moves it
   FORWARD (decision 46). */
enum ClockPullKind
{
    kPullIgnore        = 0,   /* the game is behind this world, or level with it: this world does not move */
    kPullSeed          = 1,   /* this world had no clock at all: take that game's */
    kPullWindowAdvance = 2,   /* inside the seed window and ahead: move forward to the furthest of the first linkers */
    kPullPlayForward   = 3,   /* during play and ahead by less than the bound: move forward by that much */
    kPullOverBound     = 4,   /* ahead by more than the 24-hour MISMATCH width - refused (review-p7j H-3) */
    kPullRateLimited   = 5,   /* P7w (F583): ahead by more than the per-pass bound - move forward BY the bound */
    kPullSeedOverBound = 6    /* P7y (amendment 46b): ahead by more than SEVEN in-game days - refused OUTSIDE the seed window (clock1/46c: inside it the furthest wins) */
};
/* P7w, folding F583 / review-p7u H-2. THE PER-PASS BOUND IS A RATE LIMIT, NOT A REFUSAL.
   Before this, a game more than pullBoundMinutes ahead was refused outright - and decision 46 also
   forbids that game from writing its own clock backward, so the gap had NO CLOSER IN EITHER DIRECTION
   for the life of the session. The notebook's own dt > 60 clamp on a suspended machine produces exactly
   that state with no operator error. Above the bound this world now advances BY the bound each pass, so
   each individual write stays bounded and the total is unbounded: the gap closes at pullBoundMinutes per
   pass and converges. An outright refusal is kept only at mismatchBoundMinutes (24 in-game hours), where
   the diagnosis is "two saves, one notebook folder" rather than drift, and the two are counted apart. */
/* P7y, folding amendment 46b (user, 2026-09-05) and F612 / review-p7w M-2. TWO THINGS CHANGE HERE.
   (a) THE SEED WINDOW NO LONGER SUSPENDS A REFUSAL. Until P7y the windowOpen arm was tested BEFORE any
       width test, so inside the window an offer of ANY size was adopted - two players, one notebook
       folder, one of them pointed at a day-300 save, and the shared world moved forward 299 days on the
       first three seconds with nothing refusing it (F612). The width test now comes FIRST.
   (b) THE WIDTH THAT REFUSES INSIDE THE WINDOW IS seedBoundMinutes - SEVEN in-game days. Outside the
       window the 24-in-game-hour mismatchBoundMinutes still refuses, and it is the narrower of the two,
       so the seed bound only ever decides a case the window would otherwise have waved through.
   A refused offer writes NOTHING: the offering game keeps its own clock, which is what the amendment
   says should happen, and the relay counts and logs it as a world mismatch. */
int ClockPullDecide(double shared, int seeded, int windowOpen, double fromGame,
                    double pullBoundMinutes, double mismatchBoundMinutes, double seedBoundMinutes,
                    double* newSharedOut);

/* THE NOTEBOOK'S PACE, decision 45's two modes. ENetPeer* becomes void* - the peers are identities in this
   function and nothing else, so this compiles with no ENet header in sight. `consensus` is 1 for timemode
   consensus, 0 for fixed. `lastRealSpeed` is what a RESTING notebook publishes: review-p7e H-3 - publishing 0
   told the next game to link that the world was paused. */
float EffectiveSpeedFrom(const std::vector<void*>& order,
                         const std::map<void*, float>& votes,
                         int consensus, float lastRealSpeed);

/* T-288 (owner rule 2026-09-29, industry standard): a conversation window must not pause a multiplayer session.
   DialogueWindow::show calls GameWorld::userPause(true) and DialogueWindow::hide calls userPause(false). `site` is
   1 for show's call, 2 for hide's, anything else = not a conversation call. `linked` is 1 while this game is in a
   session (a host playing alone included). `*suppressed` pairs the two halves: an opening pause skipped in a
   session raises it, so only that window's closing unpause is skipped; an opening pause outside a session is a
   REAL pause, lowers it, and its closing unpause passes through and lifts it. Returns 1 = let the call through to
   the engine (today's behaviour), 0 = skip it. */
int DialoguePauseLetThrough(int site, int linked, int* suppressed);

}   /* namespace coopclock */

#endif
