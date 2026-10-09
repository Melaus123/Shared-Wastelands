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

/* THE GAME SPEEDS A WORLD MAY RUN AT. 0 is pause; any finite multiplier above 0 up to kClockSpeedMax is a pace.
   The engine's own buttons set 0, 1, 2 and 5; a speed mod sets others (3, 10, 0.5 ...), and every one of them is
   a vote like a button's. What the range still protects: the value arrives as a raw f32 from the wire (the vote
   up, the CLOCK down) and from the engine's speed global, so NaN, infinities, negatives and absurd tempos are
   refused - a negative pace would run the shared clock backward and a huge one would leap it by days per second.
   ONE predicate, used by store.cpp's vote note, the speedvote lever and the world server's OnSpeedVote. */
const float kClockSpeedMax = 50.0f;
int ClockSpeedIsValid(float speed);
/* 1 for the four speeds the engine's own buttons set (0, 1, 2, 5) - for log text only; nothing is refused by it. */
int ClockSpeedIsBuiltIn(float speed);

/* A SPEED CHANGE NO GAME BUTTON MADE (a speed mod). The memory the main-thread tick keeps about the engine's speed
   global, and the transitions that change it - pure, so every path that moves it is an offline test. */
struct LiveSpeedMemo
{
    float        lastSeen;     /* the global after the last change this mod accounts for; < 0 = nothing recorded */
    float        appliedFor;   /* the world speed of this game's own apply, kept ONLY while that apply is the last
                                  recorded change (any later note clears it); < 0 = none */
    float        pending;      /* the latest speed-mod value that had to wait for the adoption gap; < 0 = none */
    unsigned int adoptAt;      /* tick ms (GetTickCount, wraps at 49.7 days - only differences are taken) of the last adoption */
    unsigned int holdAt;       /* tick ms the last hold began */
    int          adopted;      /* adoptAt is set */
    int          held;         /* holdAt is set */
    int          holding;      /* 1 = the last adoption is held until the world answers */
};
/* Nothing recorded: the next note starts the memory. At a link drop, a world teardown and process start. */
void LiveSpeedMemoReset(LiveSpeedMemo* m);
/* A change this mod accounts for (an engine setter call, an engine-held pause, a resync while no decision runs). */
void LiveSpeedNoteSeen(LiveSpeedMemo* m, float seen);
/* This game set the engine to the world's speed `srv`; `seenAfter` is the global read straight after. */
void LiveSpeedNoteApplied(LiveSpeedMemo* m, float seenAfter, float srv);
/* A speed-mod value arrived inside the adoption gap: it is kept as the value the next adoption sends. */
void LiveSpeedNoteWaited(LiveSpeedMemo* m, float live);
/* An adoption was made at nowMs; `seenNow` is the global at that moment. It is HELD (left on the engine until the world
   answers) only when this game's vote counts (`voteCounts`: consensus, or this game is the authority under fixed) and no
   other hold began within kClockSpeedRevoteMs. */
void LiveSpeedNoteAdopted(LiveSpeedMemo* m, float seenNow, unsigned int nowMs, int voteCounts);
/* The value an adoption sends: the global itself when it holds a speed-mod change nothing accounts for, else the kept one. */
float LiveSpeedAdoptValue(const LiveSpeedMemo& m, float live);

/* What the tick does with the global this frame, while linked, in a live world, with no engine-held pause:
     live        the speed global now
     srv         the world's speed from the notebook's CLOCK (0 = the world is paused)
     myVote      this player's standing vote
     nowMs       tick ms (wrapping; only differences are taken)
     answered    1 = a CLOCK arrived after the vote the last adoption sent went out
   kLiveSpeedAdopt   - the global moved to a valid non-zero speed nothing here accounts for and it is a NEW setting (or the
                       same setting restated after kClockSpeedRevoteMs), or a kept value is waiting, and the adoption gap
                       has passed: LiveSpeedAdoptValue becomes this player's vote, sent as a button press is.
   kLiveSpeedWait    - such a change came inside kClockSpeedAdoptGapMs of the last adoption: the caller keeps the value
                       (LiveSpeedNoteWaited) for the next adoption and, unless a hold is running, sets the world's speed -
                       a mod that keeps changing never keeps this game off the world's speed for longer than one hold.
   kLiveSpeedHold    - a held adoption is waiting for the world's answer (no CLOCK since its vote went out, and at most
                       kClockSpeedVoteHoldMs) and the global still holds it: nothing is done.
   kLiveSpeedApply   - the global differs from the world's speed and none of the above holds: the world's speed is set
                       again (the notebook's answer won, the vote does not count, a pause nobody voted for, a speed
                       outside 0..kClockSpeedMax, or an accounted change such as a button press while unlinked).
   kLiveSpeedNothing - the global holds the world's speed; or the last recorded change was this game's own apply of this
                       same world speed and the engine kept a different value - setting it again would change nothing. */
enum ClockLiveSpeedAct { kLiveSpeedNothing = 0, kLiveSpeedApply = 1, kLiveSpeedAdopt = 2, kLiveSpeedHold = 3, kLiveSpeedWait = 4 };
const unsigned int kClockSpeedVoteHoldMs = 2000;    /* a hold's ceiling when no CLOCK comes back (the notebook answers at once) */
const unsigned int kClockSpeedRevoteMs   = 10000;   /* a restated refused speed is re-sent, and a new hold may begin, at most this often */
const unsigned int kClockSpeedAdoptGapMs = 250;     /* adoptions are at least this far apart */
int ClockLiveSpeedDecide(const LiveSpeedMemo& m, float live, float srv, float myVote, unsigned int nowMs, int answered);
/* Does an engine setter call that passed through to the engine count as accounted (lastSeen follows it)? A call from
   inside the game's own image is the engine (a window, a load, the character editor); a call from this mod's own module is
   this mod's own write and never a vote; a pause, or a call that ends an engine-held pause, is the engine's or another
   mod's own window and never a pace. Only a non-zero speed set from ANOTHER module with no engine pause up - a speed mod's
   own code - is left for ClockLiveSpeedDecide to adopt. (This mod's own writes go through the trampoline and never reach
   the detour; they are recorded where they are made - store.cpp ClockOwnSpeedWrite.) */
int ClockSetterCallAccounted(int callerInGameImage, int callerThisMod, float speed, int engineHeldBefore);

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
