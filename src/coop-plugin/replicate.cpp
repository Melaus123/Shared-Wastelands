// replicate.cpp - M2a: MOVE replication.
//
// The owner streams {uid, position, facing} on a cadence; the peer holds a PUPPET for that
// uid - a character whose decisions are gated off (F061) and whose body is driven toward
// the streamed position using STEER_BY_DIRECTION (proven under suppression, T014).
//
// Design points that are consequences of measured findings, not preferences:
//
//  * MOVE rides the UNRELIABLE channel, latest-wins. A late position is worthless; resending
//    it costs latency and can arrive out of order. Every packet carries the full position,
//    so a dropped one self-heals on the next tick - no delta chain to desync.
//
//  * The puppet must be STOPPED EXPLICITLY when it arrives (F062): a decision-suppressed
//    character never cancels externally-set movement, so "stop pushing" is not "stop". We
//    zero the direction vector at the arrival threshold rather than simply ceasing to drive.
//
//  * Drift is measured continuously and reported, per architecture commitment 5 - the
//    divergence detector ships WITH the feature, not after the first desync mystery.
//
//  * F079 binds this milestone: pre-session objects exist on only one side. Every function
//    here refuses on an unknown uid rather than assuming a counterpart exists.

#include "replicate.h"
#include "addresses.h"   /* P8h: kXxxRva below is filled from the address table, not hard-coded */
#include "worldsync.h"   // H030: AnnouncedToPeer
#include "playerfaction.h"   // E25: PeerFaction - whose copies are the OTHER PLAYER's own characters
#include "spawn.h"
#include "stats.h"   // T-189 runboost: RunCeilingRead / RunCeilingWrite / RunCeilingRestore
#include "zones.h"   // review-r2 item 7: IsPositionLoadedHere - the far snap never places a copy into an area this game has not loaded
#include "ai_spike.h"
#include "net/session.h"

#include "coop_log.h"
#include "game/Character.h"
#include "game/CharMovement.h"
#include <ogre/OgreVector3.h>

// AITaskSystem.h is safe to include (unlike AI.h, which forward-declares CharacterMessage
// as a class while Character.h defines it as an enum - C2011 under v100).
#include "game/AITaskSystem.h"
#include "game/hand.h"
#include <cstring>

#include <stdint.h>   /* mig3: what <core/Functions.h> also brought in */
/* These hook targets are our own address-table rows (coop::AddrAbs gives image base + RVA, or 0 when the
   row is unbound, which each call site below guards). */
static unsigned long long kMig3SetPositionAndTeleport = 0; static coop::AddrReg kMig3SetPositionAndTeleport_reg("CharMovement_teleportTo", &kMig3SetPositionAndTeleport);   /* Steam_1.0.65 0x65DEB0 */
static unsigned long long kMig3AnimSetPosition = 0; static coop::AddrReg kMig3AnimSetPosition_reg("AnimationClass_placeAnimationRoot", &kMig3AnimSetPosition);   /* Steam_1.0.65 0x5B1070 */
static unsigned long long kMig3AiIsEnemy = 0; static coop::AddrReg kMig3AiIsEnemy_reg("AI_treatsAsEnemy", &kMig3AiIsEnemy);   /* Steam_1.0.65 0x5993A0 */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <map>
#include <vector>      // D1-b: PathBudgetFrame
#include <algorithm>   // D1-b: std::sort
#include <cmath>
#include <float.h>   // F317: _finite - VS2010 has no std::isfinite
#include "combat.h"   // P059: NoteNativeWindowDropped
#include "../common/pathlead.h"   // D3 (read-pathend): the path drive's lead / near-end / too-close / landed arithmetic
#include "../common/runboost.h"   // T-189 runboost (H051): the raise decision and the boost arithmetic
#include "../common/followplay.h"   // the follow drive: the owner's stamped route, the playback target, the stop rules
#include "../common/p124stop.h"   // PROBE P124: the stop reading and the overshoot measure, swept by the offline suite
#include "../common/getupcrawl.h"   // T-178 crawl1 (H050): MoveDrivesProne - a crawler (prone 2, not a ragdoll) is driven
#include "../common/uidtable.h"   // mirror1 (crash T487): AdoptDecision / DriveDecision
#include "../common/insidewire.h"   // P25 fold 2: the owner's INSIDE word - its staleness and send rules
#include <set>                    // mirror1: the once-per-uid refusal log
#include <sstream>
#include <locale>

// Minimal declaration at GLOBAL scope (the game's AI class, one method). We only ever take the
// address; no AI member is touched.
class RootObjectBase;
class AI
{
public:
    bool treatsAsEnemy(RootObjectBase* c);
};

// Same trick (F193 / P-14 attempt 3): all we need is one method, so a minimal global declaration of
// AnimationClass::setPosition(const Ogre::Vector3&) is made.
//
// Its decompile (real RVA 0x5B1070) is three lines and self-guarding: it does nothing unless
// `isActivated` (+0x88) is set AND the scene node (+0xB0) is non-null, and then it only calls
// `Ogre::Node::setPosition` on that node. It moves the NODE - the source the animation layer
// derives from - not the derived `rootBonePosition` value that `getPosition()` reads. Writing
// the derived value would be the fifth instance of out-writing a running derivation, which is
// a capped mistake in this project.
class AnimationClass
{
public:
    void setPosition(const Ogre::Vector3& v);

    // F308. The SECOND half of the guard that bypasses `CharMovement::update`'s whole movement-mode
    // dispatch - and its name is the answer to what it means, which is why it is worth calling
    // rather than reimplementing. Mangled: ?blocksWaypointMovement@AnimationClass@@QEAA_NXZ
    // (non-const, returns bool, no arguments), real RVA 0x51BDE0.
    //
    // When this returns true our STEER_BY_DIRECTION vector is never read: the engine forces
    // `currentlyMoving = false` and moves the body from the animation instead. It is true when the
    // character has something at animation+0x228, OR a ragdoll animation that
    // `RagdollAnimation::isAnimating()` says is playing, OR animation+0x210 with its +0x89 byte set.
    bool blocksWaypointMovement();
};

namespace coop {

namespace {

// Send cadence. The in-game pump is frame-locked at ~118/s (F071 - NOT the 900/s menu
// figure), so every 12th tick is ~10 Hz: often enough that a walking character never drifts
// far between updates, rare enough that the channel stays quiet.
const long long kSendEveryTicks = 12;

// Within this distance the puppet is "there" and is explicitly stopped (F062).
//
// F350 - **THIS NUMBER IS THE JANK.** The authority moves a measured median of 14.3 units/s
// (T086/F313) and MOVE goes out at 9.83 Hz, so its position advances **1.455 units per update** -
// less than this deadband. A puppet that is tracking correctly therefore receives a target it is
// already "arrived" at, issues a full explicit stop, stands still for the window, then finds
// itself 2.9 units behind and sprints to catch up. About five start-stop transitions a second, on
// exactly the puppets that are working.
//
// The threshold is NOT the thing to change: shrinking it would make the puppet chase a 100 ms-old
// sample with no deadband at all, which is a different jitter. What was missing is the authority's
// own motion, added below.
const float kArriveDist = 1.5f;

// F350 - **AIM WHERE THE AUTHORITY IS GOING, NOT WHERE IT WAS.**
//
// The peer knows the authority's velocity now (it rides in the two wire floats that used to be
// sent as zeros), so instead of driving at a stale sample it drives at that sample carried forward
// by however long ago it landed. While the authority walks, the aim point advances continuously at
// the authority's own speed and the puppet never sits inside the deadband; when the authority
// stops, the velocity is zero, the aim point is the sample, and the puppet arrives and stops for
// real. **The stop is now caused by the authority stopping** rather than by our distance to an old
// sample, which is the H010b rule applied to locomotion.
//
// The extrapolation EXPIRES rather than clamping - see `kVelocityMaxAgeSeconds` below.
//
// Below this the authority is treated as STOPPED and no lead is applied.
//
// F353 - **RAISED FROM 0.2 TO 2.0, because 0.2 was below the noise floor of the thing it filters.**
// The velocity is measured over a ~0.1 s window, so 0.2 units/s is a **0.02-unit** position change:
// idle-animation jitter, jostling or a body settling clears that easily, and the peer's test is a
// bare `!= 0.0f`. A standing-still authority whose sampled position wobbles by two centimetres
// would have meant its puppet was never explicitly stopped again.
//
// 2.0 units/s is 14% of the 14.3 the authority actually walks at, and a character genuinely
// creeping slower than that is not visibly walking - treating it as stopped restores exactly the
// pre-F350 behaviour for that case, which is the safe direction to be wrong in.
const float kMovingEpsilon = 2.0f;   // units per second

// F353 - **A WALL CLOCK, BECAUSE TICKS ARE NOT TIME AND THE TWO MACHINES DO NOT AGREE ON THEM.**
//
// The first version of this divided tick counts by a hardcoded 118 (F071's measured pump rate) on
// BOTH sides. The 118 cancels algebraically, and what survives is the RATIO OF THE TWO MACHINES'
// FRAME RATES:
//
//     lead = measuredVel x leadSec
//          = (dTarget / (12/118)) x (peerFrames / 118)
//          = trueVel x (peerFPS / authorityFPS) x correctLead
//
// So with the authority at 20 fps and the peer at 118 - the exact asymmetry F310's comment forty
// lines below anticipates, and the condition a 147-puppet world creates - the measured velocity
// reads 5.9x true, the aim point runs 42 units ahead, and every MOVE snaps it 34 units back. A
// 1.7 Hz, 30-unit sawtooth, in place of the 5 Hz, 1.5-unit stutter this exists to remove.
//
// `GetTickCount` is not usable here: its granularity is ~15 ms against a 100 ms window, i.e. 15%
// quantisation on the divisor. `QueryPerformanceCounter` is monotonic, cheap, and the same clock
// on both processes' terms - and neither side needs the other's clock, only its own elapsed time.
double NowSeconds()
{
    static LARGE_INTEGER s_freq = {0};
    if (s_freq.QuadPart == 0)
    {
        if (!::QueryPerformanceFrequency(&s_freq) || s_freq.QuadPart == 0) s_freq.QuadPart = 1;
    }
    LARGE_INTEGER now;
    if (!::QueryPerformanceCounter(&now)) return 0.0;
    return (double)now.QuadPart / (double)s_freq.QuadPart;
}

// F353 - anything faster than this is not a character walking, it is a TELEPORT (fast travel, a
// zone shift, `_setPositionAndTeleport`) or a corrupt sample. T086 measured puppet `speedCap`
// between 57.9 and 127.9, so 150 is above every character in the game and below any jump.
//
// A teleport must NOT become a velocity: it is finite, it clears every other gate, and it would aim
// the puppet thousands of units away with `lim = distA*distA` far too large for the per-frame cap to
// bite. Position correction and the catch-up placement already exist for exactly that case.
const float kMaxPlausibleSpeed = 150.0f;   // units per second

// F355 - STALENESS DOES TWO DIFFERENT THINGS, AND CONFLATING THEM PRODUCED A BACKSTEP.
//
// F353 replaced F350's age clamp with an outright expiry: past the horizon, `authMoving` went false
// AND the aim collapsed back to the last received sample. The first half was right and necessary -
// a region unload is deliberately not announced to the peer (F334), so without it a quiet stream
// would hold `authMoving` true forever and the puppet could never take the arrival stop again. The
// second half was wrong: the puppet is by then standing at `target + lead`, so collapsing the aim
// points it BACKWARDS and it walks back to a stale sample - a visible reversal every time five
// packets drop, which on an unreliable channel is routine.
//
// So the two are separated:
//   * the AIM is CLAMPED at the horizon (F350's behaviour) - the puppet holds where it got to;
//   * `authMoving` EXPIRES at the horizon (F353's behaviour) - so the arrival stop can fire again.
// Together: **stop where you are**, which is what silence should mean, rather than walk back.
//
// AND THE HORIZON IS ADAPTIVE, because a fixed 0.5 s was shorter than the send interval itself
// below 24 fps. `ReplicateTick` sends every 12th frame, so the interval is `12 / authorityFPS` -
// 0.6 s at the 20 fps this workstream's own load case names. A fixed horizon would then expire on
// EVERY cycle, letting the arrival stop fire 1.7 times a second and reinstating the exact jank
// P-20 is about. The horizon is three observed intervals instead, floored and capped.
const double kVelAgeFloorSeconds = 0.35;  // ~3 intervals at full rate; never tighter than this
const double kVelAgeCeilSeconds  = 3.0;   // never trust a velocity longer than this, whatever the fps

// F357 - **AND BOUND THE LEAD BY DISTANCE, NOT ONLY BY TIME.**
//
// F350 justified its fixed 0.5 s clamp BY DISTANCE - "the worst case is ~7 units at the measured
// median speed". F355 replaced it with an adaptive horizon justified entirely by the send cadence,
// and nothing then bounded the PRODUCT: at the 20 fps load case the horizon is 1.8 s, which at
// 14.3 units/s is a 25.7-unit lead, and at the 3.0 s ceiling 42.9 - **numerically the same figure
// that got F350 refused.** A fast character (T086 measured puppet speedCap to 127.9) would exceed
// `kLostDist` and trip the LOST detector while behaving exactly as designed.
//
// So the lead is clamped in units as well as in seconds. 8.0 is a shade above F350's own stated
// worst case, and roughly five times the arrival deadband - far enough ahead to keep a walking
// puppet out of the deadband, near enough that a held lead cannot be mistaken for drift.
const float kMaxLeadUnits = 8.0f;

// F358 - **AND THE HELD LEAD DECAYS TO ZERO, because a frozen one is permanent drift.**
//
// F355/F357 froze the aim at `target + vel x horizon` when the stream went quiet, to avoid the
// backstep F353 caused by collapsing it. Correct as far as it went - and it leaves the puppet
// **permanently 5 to 8 units** from the last known position, on the field literally named
// `driftXZ`, against T085's calibration of *25 of 25 undriven puppets within 2.0 units*. The
// entire quiet cohort would have failed the parity bar by construction, and T094's headline
// number would have been read as a regression caused by the fix.
//
// Decaying over a second gives both: no jerk back (the aim slides, the puppet follows at walking
// pace) and no permanent offset. The last known position is the only thing we actually know when
// the stream is silent, so ending up there is also the correct answer, not merely the tidy one.
const double kLeadDecaySeconds = 1.0;

// F350 - the authority's last STREAMED sample, so its velocity can be computed from two of them.
//
// Indexed by MIRROR SLOT, not by uid: the send loop already walks the mirror by index and a slot is
// stable for that character's whole life, so this makes the lookup free instead of a linear scan at
// 10 Hz per owned character.
//
// **F355 - the stability is NOT because the array is hashed.** `MirrorAdd` takes the first free slot
// by linear scan; it is the separate INDEX that is hashed on the object address. mirror1 (crash T487):
// and slots ARE reused now - a row is released once its character is despawned or destroyed, and the
// next registration may take it. A slot is stable for one character's life, not forever. The uid is
// stored beside it and checked, so a slot that is reused by a different character produces a fresh
// sample rather than a velocity computed across two different bodies.
//
// SIZED TO THE MIRROR. Same rule, and same reason, as `kMaxCombatWatch` in combat.cpp (F267):
// raising the mirror and not this table would silently stop velocity working for characters past
// the old bound, with nothing in any log to find it by.
struct SentSample
{
    unsigned int uid;      // 0 = the slot has never been sampled
    float x, y, z;
    double at;             // F353: WALL-CLOCK seconds, not ticks
    // F357 - HYSTERESIS. `kMovingEpsilon` was a bare threshold with none on either side, so a
    // character whose true speed sits near it (damaged legs, encumbered, sneaking) had measurement
    // noise decide PER SAMPLE whether the peer saw walking or stopped - which inside the arrival
    // deadband alternates explicit-stop / drive at the update rate. **That is P-20's exact
    // signature, restored for that cohort by the fix for P-20.** Once moving, a lower threshold
    // keeps it moving, and no run-wide counter could have revealed the flapping.
    bool wasMoving;
    int intentType; unsigned int intentSubject; double intentSentAt;   // H019: the last INTENT sent for this uid
    float intentX, intentZ;   // the movement destination that INTENT carried (a Move order's goal is re-sent when it moves)
    bool intentGoalOpen;      // the last INTENT changed the task (or a stop went out): its first goal correction skips the 0.5 s gap
    int sampleVerdict;        // this sample's own verdict (followplay::kSampleMoving / kSampleStill / kSampleNone), set by MeasureVelocity
};
const int kMaxSentSamples = 4096;   // T-354: = kMaxMirror (4096)
SentSample g_lastSent[kMaxSentSamples] = {0};
// The explicit `replicate <uid>` target is not in the mirror and so has no slot in the array above.
// It got `0.0f, 0.0f` for its velocity in the first version - the one caller the "every caller
// passed zero" story did not actually fix. One sample of its own costs nothing.
SentSample g_lastSentExplicit = {0};

// Beyond this the puppet is not catching up - it is lost. Logged loudly; a hard snap
// belongs to M2's acceptance work once we know how often this actually fires.
const float kLostDist = 250.0f;

// ---------------------------------------------------------------------------------------------
// P037 / F315 - THE CATCH-UP SNAP, and it is a TEST of F313's hypothesis before it is a fix.
//
// T086 killed the third explanation and left one measurement standing: over 394 seconds the
// authority's characters moved a median of **5,622 units** and the puppets moved **226**, while
// 45 units/s was authorised and reaching Havok the entire time and NOTHING in the drive path was
// refusing (`mvBlock=none` on all 54 drifters). The puppet is not slow. It is not travelling.
//
// The hypothesis that fits every measurement, including the three that killed the other
// explanations: **`STEER_BY_DIRECTION` is a straight-line push with no pathfinding.** The authority
// walks a navmesh route around terrain; the puppet is shoved in a straight line at whatever lies
// between it and a target thousands of units away.
//
// **This is the discriminating test, not a fourth guess** (lesson 16 - an attempt built on an
// untested diagnosis is the same attempt wearing a different name). Placing the puppet at its
// target separates the readings cleanly:
//   * placed, and it then holds parity     -> travelling was the whole problem. Confirmed.
//   * placed, and it diverges again at once -> something else is also wrong, and we know it in one
//                                              window rather than after another build.
//   * NOT placed (it does not move)         -> a different and important result: the write does not
//                                              propagate for a STANDING character either.
//
// It is legitimate as a fix as well as a test because **the user has already decided it**
// (2026-08-07): *"a small teleport shouldn't be an issue and a large teleport means there's another
// fundamental issue that needs to be solved causing such a disparity."* A large snap is a SIGNAL to
// investigate - and F313 is that investigation, done. Every snap is logged with its distance so the
// signal is never silent, and it goes in the parity register as a compensation.
//
// WHY NOT A DISTANCE THRESHOLD. A gap is not evidence about whether walking can close it - the
// authority moves too (F195, the same mistake, already paid for once). The trigger is the PUPPET'S
// OWN DISPLACEMENT over a fixed window: if it went essentially nowhere while we pushed it every
// frame, walking is not working for it, whatever the distance.
const long long kProgressWindowTicks = 240;   // ~2 s at the ~118/s in-game pump (F071)
const float kProgressMin      = 2.0f;   // moved less than this in a window = went nowhere. F313's
                                        // stalled puppets managed 0.57 units/s, i.e. ~1.1 per window
const int   kStalledWindows   = 2;      // consecutive, so one unlucky window does not trigger it
const float kCatchupMinEffect = 0.5f;   // a snap must displace the puppet by at least half the gap
const int   kCatchupMaxTries  = 3;      // then give up LOUDLY and stop (F189)
// review-r2 item 1: an EFFECTIVE snap resets catchupTries, so a copy re-lost after every snap was snapped every
// window forever. A snap (far or stuck) followed by the copy being lost or stalled again within
// kSnapStrikeWindows judged windows is a STRIKE even when the snap moved it; kSnapMaxStrikes -> catchupGaveUp.
const int    kSnapStrikeWindows = 5;
const int    kSnapMaxStrikes    = 3;
// ... and a hard cap: at most kSnapRateMax snaps per puppet in any kSnapRateWindowSec (a sliding window).
const int    kSnapRateMax       = 5;
const double kSnapRateWindowSec = 60.0;
// review-r2 item 8: no snap on an owner sample this old - the target is where the owner WAS.
const double kSnapMaxSampleAgeSec = 3.0;
// review-r2 item 3: a not-closing window is a stall only when the copy moved mostly SIDEWAYS - its displacement
// projected on the direction to the window-start target, over the distance moved, is below this. A copy sliding
// along an obstacle scores ~0; a copy chasing straight at its owner's speed scores ~1 and is not stalled.
const float  kNotClosingTowardMax = 0.3f;
const int    kSnapCauseFar   = 1;   // Puppet::snapCause: which snap placed the copy (review-r2 item 5)
const int    kSnapCauseStuck = 2;

// PROBE-START: P093 - T-196 true-runner lag localization: the per-copy accumulator (POD; memset to 0 at registration and on
// every reset). The probe itself is above DrivePuppet. Plan: .modding/investigations/t196-p093-probe.md.
struct P093Win   // one window: the frames since this copy's previous 1 s lag sample
{
    double startAt; long long startTick; int fr;
    double hvSum; int hvN; float hvMin; double curSum; int curN; float curMin; int stopFr, cmdLowFr;
    double runSum; int runN; double ownSum; int runAnimFr;
    float lagFmin, lagUmin; int hiFr, offHiFr, off[4];   // off = [notRun, noDir, lever, aimHid]
    int teleFr; float walked, fwdProg;
    int boostFr, capFr, lostFr, ovwFr, easeFr, starts, restores, skipFr;
    int pathFr, pendFr, deferFr, issues, endFr;
    int arrivals, lateArr; double gapMax; float arrJmpMax, arrJmpSum; int leadClampFr, expFr;
    // P093b: the engine's own update state (Character +0xE4 isVisibleUpdateMode, +0xC4 frameTIME, +0xC0 offscreenFrameTime)
    int rdFr, visFr, slowFr, rstN; float ftMax, rstFt, rstHv, rstMax, rstAnim;   // rst* = the last frame +0xC0 fell (bank spent)
};
struct P093Acc
{
    P093Win w;
    bool prim;                           // the previous-frame values below are set
    float px, pz, pLagU, pWant; double pLastMoveAt, pPathIssueAt, pLagSampleAt; bool pRbActive; int pStep;
    float lastDes, lastMax, lastAnim; int lastKind, lastMode, lastAgent;   // this frame's reads, for the line
    bool epOn; double epStartAt; float epLate, epArr, epCreep, epClose;   // the episode: lagU >= 10 since epStartAt
    bool due; float sLag;                // a true-runner sample was taken this frame (P093OnSample)
    bool pOftOk; float pOft;             // P093b: the previous frame's +0xC0 (offscreenFrameTime), to see the bank spent
};
// PROBE-END: P093

struct Puppet
{
    Character* ch;
    float tx, ty, tz;      // last streamed target position
    // F350 - THE AUTHORITY'S VELOCITY, in world units per second. These two floats used to be
    // called `dirX/dirZ`, were documented as "last streamed facing, unused until M2b, carried for
    // fidelity", and **every sender passed 0.0f into them**. So the peer has never been told
    // whether the authority is moving at all - only where it was 100 ms ago.
    //
    // They now carry a VELOCITY rather than a unit direction, which puts three facts in two floats
    // with no payload change: heading, speed, and (magnitude zero) stopped.
    float velX, velZ;
    double lastMoveAt;       // WALL-CLOCK seconds when that sample landed (F353: not ticks -
                             // ticks made the lead scale with the two machines' fps ratio)
    // F355 - the OBSERVED inter-arrival interval, smoothed. The staleness horizon is derived from
    // it rather than fixed, because the send interval is `12 / authorityFPS` and a fixed horizon
    // shorter than that expires on every single cycle.
    double moveIntervalSec;
    double lagSampleAt;      // walk1: when WalkLagSample last sampled this copy (wall-clock seconds, 0 = never)
    bool  driving;         // did we CALL steerDirectly this frame? (F184: NOT "is it
                           // moving" - a prone character is pushed and does not move)
    bool  everDriven;      // F342: have we EVER driven it, in its whole life? A different question
                           // from `driving`, and the one that decides whether a strange engine-side
                           // movement value can possibly be ours
    int   pronedAt;        // its PoseState at the last push, so the two are never conflated
    int   snapTries;       // consecutive corrections that did NOT MOVE THE PUPPET
    // F195: the strike test used to compare DRIFT before and after, and drift is the gap
    // between two bodies. In T062 an authority walked 0.8 units closer to a puppet that had
    // not moved at all, the gap shrank, and the guard credited the correction and RESET its
    // own strike count - so a corrective known to do nothing was cleared to keep firing.
    // The effect of moving something is how far THAT THING moved. Nothing else.
    float snapFromX, snapFromZ;   // where the puppet was when we last called the corrective
    // PER-PUPPET, not global. T083 exposed why: the LOST throttle was a single `static` shared by
    // every puppet, so at most ONE puppet could report every 5 s no matter how many were lost. With
    // 100 replicated characters and 60+ badly drifting, the run produced 64 LOST lines covering
    // only FIVE distinct uids - whichever happened to hit the window. The instrument was sized for
    // the 4-6 puppets this project used to have, and at world scale it HID the very collapse it
    // exists to report (position parity had fallen to 30 of 100 within 2.0 units, worst drift 4880).
    long long lastLostTick;
    // F311 - the UNDO for the `drivespeed` lever. `speedOriginal` is the engine's own
    // `desiredSpeed` as it was before our FIRST write to this puppet, so switching the lever off
    // puts back the engine's value and not one of ours. Lesson 21: anything we set on a puppet we
    // must be able to unset, and the undo ships in the same commit as the do.
    bool  speedSaved;
    float speedOriginal;
    // P037 / F315 - IS WALKING WORKING AT ALL? Measured per puppet over a fixed window, because
    // F313 measured that it is not: over 394 s the authority's characters moved a median of 5,622
    // units and the puppets moved 226, with 45 units/s authorised and reaching Havok the whole
    // time. These fields are the per-puppet version of that measurement, live.
    long long progressCheckTick;   // when the current window opened
    float progressFromX, progressFromZ;
    float progressFromDist;        // gap to the target when the window opened (item 5: is it closing?)
    float progressFromTX, progressFromTZ;   // review-r2 item 3: the TARGET when the window opened (which way is 'toward')
    int   stalledWindows;          // consecutive windows in which the puppet went nowhere
    // The corrective, and the guard that makes it self-limiting (F189/F195): what matters is how
    // far THE PUPPET moved, never whether the gap shrank - the authority moves too.
    bool  catchupPending;
    float catchupFromX, catchupFromZ, catchupDriftBefore;
    int   catchupTries;
    bool  catchupGaveUp;
    int   snapCause;          // review-r2 item 5: kSnapCauseFar / kSnapCauseStuck, set at placement; 0 = none yet
    int   snapWatchWindows;   // review-r2 item 1: judged windows since the last snap; -1 = no snap being watched
    int   snapStrikes;        // ... snaps followed by lost/stalled again within kSnapStrikeWindows
    double snapTimes[kSnapRateMax]; int snapTimesNext;   // ... the last kSnapRateMax snap times (ring; next = oldest)
    bool  unstickEnded;       // review-r2 item 2: an unstick order ended and no window has been judged since
    bool  snapPending;            // a correction is outstanding and has not been scored yet
    float snapLastDrift;   // drift at the previous correction - REPORTED, never the test
    bool  snapGaveUp;      // stop correcting this puppet; logged once when set
    float lastDrift;       // HORIZONTAL distance to target at the most recent tick
    float maxDrift;
    float lastDriftY;      // VERTICAL offset, signed. Measured, never corrected by driving.
    float maxDriftY;
    long long applied;     // MOVEs applied
    // H015 / P059 - native combat window (see replicate.h). `nativeMaxDrift` is WINDOW-scoped,
    // unlike `maxDrift` which is lifetime; a window's number must not inherit a pre-window spike.
    bool  nativeWindow;
    float nativeMaxDrift;
    bool  nativeSnapScorePending;   // a close-snap happened; score it on the next tick (F327)
    long long rcSteps;              // H016 - reconciliation steps applied in the current window
    float rcMaxStep;                // ... largest single-frame step (a hop would show here)
    float rcSumStep;                // ... total distance corrected
    // H016 review: the position READ lags the position WRITE (Character+0x48 is refreshed by the
    // AI thread's threadedUpdate, not by our write). So our own applied steps are dead-reckoned
    // against the last reading and reset the moment the reading changes.
    double rcLastTickAt;            // wall-clock of the previous windowed DrivePuppet TICK: the frame
                                    // interval (T104: dt-since-last-STEP banked skips into a 1.0 hop)
    float rcReqMax;                 // largest PRE-cap requested step (the post-cap one is a constant)
    float rcVisMaxStep;             // largest frame-to-frame 3D displacement of the VISIBLE position while
                                    // windowed - the hop metric (includes the AI's own motion)
    float rcVisMaxRate;             // ... the same as a RATE (units/s), STANDING frames only (T106: the
                                    // engine's ragdoll threw a body at 1117 u/s on the peer and 612 on the host)
    float rcVisMaxRateDowned;       // ... prone/ragdoll frames, reported separately, never judged
    float rcVisMaxRateAny;          // ... the same as a RATE (units/s), which is what a frame-rate-free
                                    // oracle needs (review 3, finding 1)
    float rcVisLastX, rcVisLastY, rcVisLastZ;
    bool  rcVisPrimed;
    float rcLayerGapMax;            // |CharMovement::pos - visible| max, measured on EVERY windowed frame
    float rcLayerGapLast;           // PROBE P019: the previous windowed frame's layer gap (the gap AT the peak frame)
                                    // including prone/ragdoll - the cohort where the layers diverge (F193)
    double rcDriftSum; long long rcDriftN;   // mean drift over the window (steady state, not the transient)
    long long rcSatSteps, rcBoostedSteps;    // H028: steps capped / steps with the boosted rate, this window
    float authDesired;              // F419: the authority's commanded desiredSpeed (gait intent). -1 = unknown; 0 is a real level
    float authDesiredApplied;       // review 4 item 10: the level last written to a STOPPED puppet (-2 = never)
    // H019 / M-C - the authority's current goal for this puppet, as last received. type 0 = the authority has
    // no goal ('Aimless'), which is a real state and is sent as such.
    int intentType; unsigned int intentSubject; float intentX, intentY, intentZ; int intentPriority; double intentAt; int intentSeq;
    // P25 fold 2 - the owner's INSIDE word, as last received: insideSaid -1 never heard / 0 outside / 1 inside; insideKey the
    // building's P7n key ("" = none); insideAt wall-clock seconds; insideHint the building it resolved to last (a KEY for
    // ObjectByPositionKeyHint only - re-checked there, never dereferenced as is; reset when the key changes).
    int insideSaid; char insideKey[p25inside::kInsideKeyMax + 1]; double insideAt; void* insideHint;
    bool catchingUp;                // H020: currently commanded above the authority's gait to close a lag
    bool orderActive; float orderX, orderZ; long long orderTick; int orderCount;   // H021: the live move order
    int orderSpeed;                 // H022: the SpeedOrder last written to speedOrders (-1 = never)
    bool unsticking; long long unstickTick;   // H024: one Move order in flight to get around an obstacle
    float faceX, faceZ; long long faceTick;    // H024: the authority's facing (unit XZ), last apply tick
    // D1 (read-movement) - pathdrive: walking by the engine's own pathfinder (CharMovement::setDestination 0x6607E0)
    bool pathMode;                  // this copy is currently walked by a path request, not pushed
    bool pathHaveDest; float pathDestX, pathDestZ; double pathIssueAt;   // the last-destination memo (cleared by any snap)
    bool pathAwait; long long pathAwaitTick;   // an issued path whose copy has not yet been seen moving (issue-to-moving timer)
    bool pathFailNoted;             // pathFailed() already counted for the current issue
    float pathLastX, pathLastZ; double pathLastAt;   // previous path-mode frame's position, for the measured speed
    double pathHoldUntil;           // D1-b item 1: a path that made no progress holds this copy on the push until then
    bool pathProgArmed; long long pathProgTick; float pathProgFromDistA;   // D1-b item 1: the path's own progress window (distance to the aim)
    bool pathWantIssue; double pathWantSince; bool pathGrant;   // D1-b item 2: waiting for the global per-frame budget; granted this frame
    double pathOwnerStillSince;     // D1-c: when the owner's speed last fell below kPathOwnerStillSpeed (0 = moving)
    bool pathProgOwnerMoved;        // D1-c: the owner moved at some point in the current path progress window
    float pathProgFromX, pathProgFromZ;   // D1-d (recheck-d1c Q2): where the COPY stood when the window opened
    bool pathDestLead; float pathDestDirX, pathDestDirZ, pathLineX, pathLineZ;   // D2: the last destination led a moving owner (its heading, the aim it was led from)
    bool pathOwnerWasMoving, pathStopDue, pathArrivedNoted;   // D2: owner-moving memo; exact-aim issue owed (owner stopped); arrival counted for this destination
    bool pathWantPriority; double pathTurnSince; int pathRetryAim;   // D2-b: the stop issue's budget priority; since when the owner has turned; failed-lead retry (0 none, 1 owed, 2 issued)
    bool ownPrimed; float ownSpd, ownHdgX, ownHdgZ;   // D2-b item 3: the owner's smoothed speed and unit heading (EMA per MOVE sample)
    long long pathIssueTick; bool pathFailSeenClear;   // D2-c: the frame of the last issue; pathFailed() has read 0 since it
    bool pathLeadWalking;   // D3 fold M2: an old LEAD path is still walked under the current exact-aim issue (until it LANDED)
    bool pathSeenPending, pathLanded, pathLandedFaultNoted, pathTooCloseNoted;   // D3: the agent's request read non-1 since the issue; it landed; its read faulted (counted once); a too-close refusal counted for this destination
    // THE FOLLOW DRIVE (followplay, src/common/followplay.h) - the copy of any character another game owns (a player's own
    // character, its squad or an NPC) replays its owner's reported route a fixed delay behind and never runs past where the
    // owner is known to be. All cleared by FollowReset.
    bool follow;                    // the copy is driven by the follow drive: its character is live and another game owns it (re-read on every report)
    bool followPlayer;              // its faction is a player stand-in's (PeerFactionPod == 1): what the probes split by, re-read on every report
    followplay::Track trk;          // the owner's reports, stamped with the owner's clock
    followplay::ClockOff clk;       // the owner's clock when no owner slot is known (else the owner game's, g_followClk)
    double fClkOff;                 // arrival time - owner time, the offset in use (s)
    double followD;                 // the playback delay (s), re-read only while the owner stands
    double fLastMovingAt;           // when the last moving report arrived (NowSeconds; 0 = never)
    int fOwnerSlot;                 // the owner slot (net::OwnerSlotOf) the recording, the stop and the offset belong to
    bool stopHeld; float stopX, stopY, stopZ; double stopT;   // the owner's stated stop (MSG_MOVESTOP), at owner time stopT
    bool stopIssueOwed;             // the path to the stop point goes out with the budget priority and past the floor
    bool goalHave; float goalX, goalZ;            // the owner's Move-order goal, kept after its INTENT ends until a stop
    bool goalSpent; float goalSpentX, goalSpentZ;  // the goal the last stop used up (its refresh does not bring it back)
    // this frame's replay, written by FollowFrame (DrivePuppet) and read by FollowDriveStep / ApplyDriveSpeed / P123
    double fTPlay; int fMode; float fTX, fTZ; float fE; bool fEOk; bool fMoving;
    bool fHaveN; float fNX, fNZ; bool fHaveH; float fHX, fHZ, fV; int fVum; int fDestKind; bool fHeld; float fPace;
    int fIssuedKind;                // the destination kind of the live path (followplay::kDest*)
    float fCopyLX, fCopyLZ; double fCopyLAt; float fCopyAlongV;   // the copy's previous-frame position and its speed along h
    followplay::StopProgress fStopProg;   // closing on the stated stop point (followplay::StopProgressStep), per drive frame
    bool fStopStalled; float fStopArrive; // it stopped closing on it; this frame's arrival radius on it (followplay::StopArriveRadius)
    bool fBehind;                   // behind schedule (followplay::BehindScheduleStep): it aims at the owner's newest reported point

    long long nativeSnapTick;
    bool rbActive, rbCeiling; float rbWrote; long long rbFrame;   // T-189 runboost: a raise is on; the ceiling is ours; our last
                                                                  // +0x19C write; the frame the drive last renewed it
    bool rbFailed; double rbEaseAt;   // runboost2 (T-195): a restore failed and is retried each frame (R2); the eased
                                      // restore's last step (NowSeconds, 0 = none yet) (R1)
    // PROBE-START: P093
    P093Acc p093;   // T-196 true-runner lag localization (measurement only)
    // PROBE-END: P093
};


std::map<unsigned int, Puppet> g_puppets;

// H016 - reconciliation inside a native window. F411: the two simulations diverge at ~4 units/s, so
// a per-frame pull of a few percent holds the gap under a unit with steps far below anything visible.
bool  g_reconcileOn        = true;        // combat1 (user decision 2026-09-25): ON by default with the native combat window it serves
const float kReconcileTau      = 0.15f;   // seconds: error decays as 1 - exp(-dt/tau), frame-rate free
const float kReconcileMaxRate  = 20.0f;   // units per SECOND: the cap is rate x dt (frame-rate free in
                                          // the saturated regime too - review 2, finding 5)
// H028 (F437): in a brawl the released copy's own locomotion (30+ u/s toward a target whose copy is itself
// displaced) outruns a 20 u/s pull, and the error compounds (T123: window mean drift 47-73 units, 47% of steps
// saturated). The pull RATE now grows with the error: 20 u/s up to kReconcileBoostErr, then +kReconcileBoostSlope
// per unit of error, capped at kReconcileMaxRateBoost - a smooth drag that beats locomotion when far, and the
// unchanged gentle pull when near (the 8-fighter runs sit at 1-2 units, below the boost).
const float kReconcileBoostErr      = 5.0f;
const float kReconcileBoostSlope    = 4.0f;    // u/s of extra pull per unit of error beyond kReconcileBoostErr
const float kReconcileMaxRateBoost  = 60.0f;   // a pull-RATE cap, not a speed ceiling: the engine's ceiling is each character's
                                               // own run speed (CharStats +0x17C; Garru 93.46, animals run at 93 - T-189)
long long g_rcBoostedSteps = 0;
const float kReconcileMaxStepAbs = 0.80f; // absolute per-frame ceiling, a DISTANCE per frame (was 0.45; 60 u/s at 75 fps needs 0.8). NOT the verdict's hop bound: the oracle judges a RATE (u/s), and 0.45/frame is 53 u/s at 118 fps, 9 u/s at 20 fps (review 4, item 16)
const double kReconcileMaxDt = 0.05;      // a frame longer than this (20 fps) is treated as 20 fps, never accumulated
const float kReconcileDeadband = 0.5f;    // inside this, leave the body alone
long long g_rcSteps = 0, g_rcSkippedDeadband = 0, g_rcSkippedProne = 0, g_rcSkippedRagdoll = 0;
long long g_crawlDriven = 0;   // T-178 crawl1: MOVE drive frames applied to a crawling copy (prone 2, not a ragdoll)
long long g_rcSkippedNoMovement = 0, g_rcSkippedBadSetter = 0, g_rcSkippedOff = 0;
long long g_rcSkippedNonFiniteVisible = 0, g_rcSkippedNonFiniteField = 0, g_rcSkippedRetired = 0, g_rcSkippedNoChar = 0;
// Review 4 (2026-09-02): item 2 - drive frames refused because the character is retired (checked FIRST now);
// item 3 - the reconcile setter faulted under SEH; item 10 - a stopped puppet's speed level re-synced to the authority's.
long long g_driveSkippedRetired = 0, g_rcSetterFaulted = 0, g_speedArriveSynced = 0;
// mirror1 (crash T487): drive frames refused because the uid table does not hold the puppet's object
// live under its uid; adoptions refused for the same reason (not held at all / held under another uid);
// puppets forgotten by the reclaim of a destroyed row.
long long g_driveSkippedUnregistered = 0, g_adoptRefusedFull = 0, g_adoptRefusedOtherUid = 0, g_puppetsDroppedReclaim = 0;
std::set<unsigned int> g_adoptRefusedUids;   // once-per-uid log; cleared at world teardown
// H019 / M-C - intent stream + handoff experiment.
long long g_intentSent = 0, g_intentRecv = 0, g_intentRefused = 0;
long long g_intentGoalResent = 0;   // INTENTs sent only because a Move order's goal moved (task and subject unchanged)
long long g_handoffInjected = 0, g_handoffControl = 0, g_handoffMatched = 0, g_handoffControlMatched = 0;
long long g_handoffInjectFailed = 0, g_handoffQuiesceTimedOut = 0, g_handoffSkippedNoIntent = 0, g_handoffSkippedAttack = 0, g_handoffSkippedWindow = 0;
struct PendingHandoff { unsigned int uid; int control; int wantType; long long tick; char goalBefore[48]; };
const int kMaxPendingHandoffs = 32;
PendingHandoff g_pendingHandoffs[kMaxPendingHandoffs]; int g_pendingHandoffN = 0;
long long g_rcSkippedBadTarget = 0, g_rcSkippedZeroStep = 0, g_rcSkippedVtImplausible = 0;
long long g_rcSkippedNoAnimEarly = 0, g_rcSaturated = 0, g_rcSlotRefused = 0, g_rcLayerGapFrames = 0;
float     g_rcReqMax = 0.0f, g_rcVisMaxStep = 0.0f, g_rcVisMaxRate = 0.0f, g_rcLayerGapMax = 0.0f;
long long g_visPeakLines = 0;   // PROBE P019: vis_peak lines emitted (peer side) p.rcLayerGapLast = 0.0f;
double    g_rcSumStep = 0.0; float g_rcMaxStep = 0.0f;
int       g_rcSlotVerified = -1;          // -1 unchecked, 1 the vtable is CharMovement's, 0 refused
uintptr_t g_rcImageBase = 0;
static std::string RcHex(uintptr_t v) { std::stringstream ss; ss << "0x" << std::hex << v; return ss.str(); }
// The address is our table row CharMovement_teleportTo. No C++
// objects in this frame, so __try is legal under /EHsc.
static uintptr_t SafeRealAddressOfTeleport()
{
    __try { return (uintptr_t)coop::AddrAbs(kMig3SetPositionAndTeleport); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

unsigned int g_ownedUid   = 0;    // the uid we stream (0 = not streaming)
Character*   g_ownedChar  = 0;
long long    g_tick       = 0;
long long    g_sent       = 0;
int          g_streamedLastTick = 0;   // uids streamed on the last send tick
long long    g_moveRecv  = 0;          // inbound MOVEs applied (T057: streaming had NO counter)
long long    g_proneSnaps = 0;         // corrections walking could not have made (F184)

std::string N2(long long v)
{
    std::stringstream ss; ss.imbue(std::locale::classic()); ss << v; return ss.str();
}

// F317 - A NON-FINITE FLOAT MUST SAY SO, because the alternative is a field that looks like a
// number and is not one.
//
// T086 printed `mvDirLen=1.$ mvLimit=1.$` for one uid across five separate reports. That is not a
// formatting nicety: MSVC's CRT renders NaN and infinity as `1.#QNAN` / `1.#INF` and then ROUNDS
// that text as if it were digits, so at `precision(1)` a special value collapses into a
// number-shaped token three characters long. Every other field on that line read normally, so
// nothing flagged it.
//
// It matters beyond the log. `desiredMotion` is the vector handed to the physics layer every tick;
// a NaN in it makes the drive a no-op for that character no matter how correct everything else is.
// A probe that renders that as `1.$` cannot report the one condition it would most need to.
bool Finite(float v) { return _finite((double)v) != 0; }

std::string F2(float v)
{
    if (!Finite(v)) return (v != v) ? "NaN" : "INF";
    std::stringstream ss; ss.imbue(std::locale::classic());
    ss.setf(std::ios::fixed); ss.precision(1); ss << v; return ss.str();
}

// F311. One decimal is right for distances and wrong for a SETTING: `drivespeed 0.04` printed
// `driveSpeed=0.0`, which is the exact rendering of OFF, in the field whose only job is to say
// which arm of a run is in effect.
std::string F3(float v)
{
    if (!Finite(v)) return (v != v) ? "NaN" : "INF";
    std::stringstream ss; ss.imbue(std::locale::classic());
    ss.setf(std::ios::fixed); ss.precision(3); ss << v; return ss.str();
}

bool PlausibleObj(const void* p)
{
    uintptr_t v = (uintptr_t)p;
    if (v < 0x10000 || v >= 0x00007FFFFFFFFFFFull) return false;
    if (v & 0x7) return false;
    uintptr_t vt = 0;
    __try { vt = *(uintptr_t*)v; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    return vt > base && vt < base + 0x4000000;
}

// Character -> PoseState. Read to LABEL the drive honestly, and to decide when walking
// cannot possibly close the gap.
const size_t kProneStateOff = 0xE0;

// P023 / F193. Character::getPosition (0x5CDBF0) does not read one field - it reads whichever
// of FOUR layers is currently authoritative, and the choice is made per call. These are the two
// that matter here: the logical position, and the animation node's own.
const size_t kAnimationClassOff = 0x448;   // Character -> AnimationClass*
const size_t kLogicalPosOff     = 0x48;    // Character  : Vector3, the base-case position
const size_t kAnimPosOff        = 0x98;    // AnimationClass : Vector3, used while down

// ---------------------------------------------------------------------------------------------
// P036 - WHY A DRIVEN PUPPET DOES NOT MOVE. Supersedes P035, which was built and deployed and
// COULD NOT HAVE ANSWERED THE QUESTION. That is worth stating plainly because it is the seventh
// instance of this project's oldest mistake (SS6a lesson 1): P035 read five fields, and I had
// written the interpretation key for them - "desiredSpeed == 0 means our drive never stuck" -
// without ever reading the function that consumes them. Four of the five are OUTPUTS the engine
// recomputes every tick, and `steerDirectly` does not write any of them.
//
// F303/F304/F305/F306 established what the drive path actually is, read out of
// `CharMovement::update` (0x65F510) instruction by instruction:
//
//   * `steerDirectly(dir, limit)` (0x3332C0) writes EXACTLY three things and has no guard
//     at all: movementMode (+0x378) = STEER_BY_DIRECTION via setSteeringMode, desiredMotion
//     (+0x38C) = dir, moveLimit (+0x398) = sqrtf(limit). It cannot fail and it cannot no-op.
//
//   * The per-tick consumer reads, in order: havokCharacter (+0x320) - NULL returns instantly and
//     silently; animationOverride (+0x37C) or an animation predicate - either bypasses the whole
//     mode dispatch; then movementMode, and only mode == 2 reaches our vector.
//
//   * *** AND THE SPEED COMES FROM A FIELD WE NEVER SET. *** Before the mode dispatch, every
//     tick, for every mode:  HavokCharacter::setDesiredSpeed(min(desiredSpeed, speedCap)).
//     The DECOMPILER DOES NOT SHOW THIS - it renders the call as `f(havok)` because it drops the
//     float argument passed in XMM1. Only the disassembly shows it (0x65F5CE..0x65F5ED:
//     `MOVSS XMM0,[RSI+0xbc]` / `COMISS XMM0,[RAX]` / `MOVSS XMM1,[RAX]` / `CALL` -> the symbol
//     resolver names that target `?setDesiredSpeed@HavokCharacter@@QEAAXM@Z` = 0x144070).
//     `desiredMotion` is a UNIT DIRECTION - vanilla normalises it too (0x349E10) - so the entire
//     magnitude of the motion is that one float. Vanilla's STEER_BY_DIRECTION caller sets it
//     explicitly on the very next line after storing the direction. WE NEVER HAVE.
//
// So this probe reports the INPUTS to that path (F171 - a zero that cannot be interrogated is not
// a measurement), and computes the verdict in code rather than leaving a human to derive it
// (F270 - I got 3 of 4 hex conversions wrong in the label whose job was to catch a wrong value).
//
// Offsets: +0x08/+0x24/+0xB4/+0xB8/+0xBC are AbstractMovementBase's; +0x320/+0x378/+0x37C/+0x38C/
// +0x398 are CharMovement's own. All were confirmed in the disassembly above. Plain aligned reads behind a
// pointer guard - no engine call, nothing written.
//
// Split from the caller because the caller builds std::strings and `__try` cannot live in a
// function that requires object unwinding.
struct MoveDbg
{
    int   moving, stopped;        // +0x24, +0x08   engine's own belief, both OUTPUTS
    float cur, want, maxs;        // +0xB8, +0xBC, +0xB4
    float limit;                  // +0x398         per-frame displacement cap
    float dirX, dirY, dirZ;       // +0x38C         desiredMotion, the unit direction we wrote
    int   mode;                   // +0x378         2 == STEER_BY_DIRECTION
    int   animOverride;           // +0x37C
    bool  havok;                  // +0x320 != 0
    bool  ok;
};

bool ReadMoveDbg(const void* mv, MoveDbg* o)
{
    o->moving = o->stopped = o->mode = o->animOverride = 0;
    o->cur = o->want = o->maxs = o->limit = 0.0f;
    o->dirX = o->dirY = o->dirZ = 0.0f;
    o->havok = false; o->ok = false;
    if (!PlausibleObj(mv)) return false;
    __try
    {
        o->stopped      = *(unsigned char*)((char*)mv + 0x08);    // officiallyStopped
        o->moving       = *(unsigned char*)((char*)mv + 0x24);    // currentlyMoving
        o->maxs         = *(float*)((char*)mv + 0xB4);            // speedCap
        o->cur          = *(float*)((char*)mv + 0xB8);            // speedNow
        o->want         = *(float*)((char*)mv + 0xBC);            // desiredSpeed
        o->havok        = *(void**)((char*)mv + 0x320) != 0;      // havokCharacter
        o->mode         = *(int*)((char*)mv + 0x378);             // movementMode
        o->animOverride = *(unsigned char*)((char*)mv + 0x37C);   // animationOverride
        o->dirX         = *(float*)((char*)mv + 0x38C);           // desiredMotion.x
        o->dirY         = *(float*)((char*)mv + 0x390);           // desiredMotion.y
        o->dirZ         = *(float*)((char*)mv + 0x394);           // desiredMotion.z
        o->limit        = *(float*)((char*)mv + 0x398);           // moveLimit
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    o->ok = true;
    return true;
}

// F305's lever. 0 = leave `desiredSpeed` alone (default), -1 = use the character's own speedCap,
// >0 = force that value. See the drive site for why the default must be "don't touch".
// P037 / F315. Was OFF by default so the first arm of a run could read the STALL RATE with nothing
// corrected. DECISION 60 (user 2026-09-22, docs/authority-model.md): ON by default - a copy that is
// pushed for two windows and goes nowhere is placed at its owner's position. The `catchup` verb
// still turns it off for a measurement arm - and, since review-r2 item 4, turns the far snap off too.
bool      g_catchupOn        = true;
long long g_stalledWindows   = 0;   // windows in which a pushed puppet went nowhere - the MEASUREMENT
long long g_catchupSnaps     = 0;
long long g_catchupEffective = 0;   // the puppet actually moved by roughly the gap
long long g_catchupIneffective = 0;
long long g_catchupGaveUp    = 0;
// F324: declined because the character is ragdolled, which makes placement structurally impossible
// (F314). Counted, not silent - "declined" and "failed" are different results and the run must be
// able to tell them apart.
long long g_catchupSkippedRagdoll = 0;
// DECISION 60 (user 2026-09-22): a copy more than kLostDist from its owner's position is TELEPORTED
// there (snapFar), judged by the same did-it-work check as the stuck snap (`catchupSnaps`, which is
// the stuck snap's count and reads as snapStuck). Declines are counted by cause, once per window.
long long g_snapFar                = 0;
std::map<unsigned int, long long> g_snapFarByUid;   // T-191: far snaps per uid (the log names the top 16 by count)
const size_t kSnapFarByUidMax = 256;   // runboost2 (T-195 R6): at most this many uids kept; a snap of any other is counted
long long g_snapFarByUidOther = 0;     // here. Both cleared at world teardown.
long long g_snapFarSkippedRagdoll  = 0;
long long g_snapFarSkippedGaveUp   = 0;
// Item 5 (T250 log-B): windows counted as stalled ONLY because the gap did not close - a copy
// sliding along an obstacle it cannot pass moves ~5 units a window and never met kProgressMin.
long long g_stalledNotClosing      = 0;
// review-r2. Per cause (index kSnapCauseFar / kSnapCauseStuck; 0 = a score with no cause recorded).
long long g_snapEffectiveBy[3]   = {0, 0, 0};
long long g_snapIneffectiveBy[3] = {0, 0, 0};
long long g_snapGaveUpBy[3]      = {0, 0, 0};
long long g_snapStrikes            = 0;   // item 1: a snapped copy lost/stalled again within kSnapStrikeWindows
long long g_snapRateCapped         = 0;   // item 1: a snap declined - kSnapRateMax already in the last 60 s
long long g_snapStuckAfterUnstick  = 0;   // item 2: stuck snaps fired by "still stalled after an unstick order"
long long g_snapSkippedAttached    = 0;   // item 6: declined - carried, or in a cage, bed or building slot
long long g_proneSnapSkippedCarried = 0;  // review-k1 item 7: frames a carried copy's prone snap was declined
long long g_proneSnapSkippedRagdoll = 0;  // R3 item 5: frames a RAGDOLLED copy's prone snap was declined (0x65DEB0 cannot move one)
long long g_snapFarSkippedUnloaded = 0;   // item 7: declined - the owner's target is in an area this game has not loaded
long long g_snapSkippedStale       = 0;   // item 8: declined - the owner's last sample is kSnapMaxSampleAgeSec old or more
long long g_snapFarCancelledUnstick = 0;  // item 9: a far snap cancelled an unstick order that was still running

float     g_driveSpeed    = -2.0f;  // AUD 2026-09-17: default -2 = drive each puppet at the authority's
                                    // STREAMED desiredSpeed (see SetDriveSpeed below). `drivespeed 0`
                                    // is the control - it leaves the engine's own value alone.
long long g_speedWrites   = 0;   // wrote a value > 0
long long g_speedAuthWrites = 0; // ... of which: from the authority's stream (drivespeed -2 = its desiredSpeed, -3 = its velocity)
long long g_speedAuthFloored = 0; // ... floor DECIDED (level unknown/0, or velocity stale); counted before the write, so includes writes then refused
// H020 (user 2026-09-02: bounded catch-up). Equal speeds preserve a gap (F426): a copy commanded at exactly the
// authority's gait can never close a lag a stall opened. While more than kCatchupEnter behind its aim point the
// copy is commanded kCatchupMul x the authority's gait, capped at kCatchupCap (T-189: not the engine's run level), until it is back
// within kCatchupLeave (hysteresis so the gait does not flicker at the boundary). No position write - no hop.
const float kCatchupEnter = 20.0f;  // H024: far above the push's steady lag (~1.5) - fires after a stall, never in steady state
const float kCatchupLeave = 4.0f;
const float kCatchupMul   = 2.0f;
const float kCatchupCap   = 45.0f;   // the RUN LEVEL test (review-walk1 3: jog 55 and run 999 are >= it) and the walking bonus's
                                     // cap - NOT the engine's ceiling, which is each character's own run speed (CharStats +0x17C,
                                     // AnimationClass +0x19C -> CharMovement +0xB4; Garru 93.46, animals 93 - T-189/H051)
long long g_speedCatchupFrames = 0, g_speedGaitFrames = 0, g_speedCatchupEnters = 0;
// walk1 (user decision 2026-09-25: fight drift option C - fix the walking lag; F922: copies started fights ~24 units from their
// owners). The catch-up is now PROPORTIONAL and SIGNED: requested = owner's speed + (lag along the owner's direction of travel,
// measured to the carried-forward aim) / kC2Tau, capped at kC2MaxMul x the owner's speed (or the old 2x / 45 cap past
// kCatchupEnter), never for a copy that is AHEAD (the old unsigned test sped those up too - F888). No dead band between 4-8
// and 20 units any more. `catchup2 off` restores the H020 hysteresis catch-up for an A/B.
bool g_catchup2 = true;
const float kC2Tau    = 2.0f;    // seconds: a 10-unit lag adds 5 u/s
const float kC2MaxMul = 1.4f;    // at most 1.4 x the owner's commanded speed below kC2RampFrom
const float kC2RampFrom = 15.0f; // review-walk1 2: the cap rises smoothly from kC2MaxMul to kCatchupMul between these lags
const float kC2RampTo   = 25.0f; //   (the old step at kCatchupEnter flapped the command between 21 and 25 for a walker)
const float kC2Dead   = 0.75f;   // under this lag: the owner's own speed (no flicker at the arrival radius)
long long g_c2Frames = 0;        // frames the proportional bonus raised the commanded speed
long long g_c2AheadFrames = 0;   // frames the copy was AHEAD of its owner's aim (no bonus - the old code boosted these)
// walk1: the walking lag, sampled once a second per moving copy (owner's smoothed speed above 2 u/s): the signed lag along the
// owner's direction to the aim point. Buckets <1, 1-5, 5-10, 10-20, 20-40, >=40 (behind), and ahead; walk vs run by the owner's
// commanded speed (review-walk1 3: jog 55 and run 999 both count as run, >= kCatchupCap). Sampled in DrivePuppet right after
// the aim is computed, BEFORE the arrival return (review-walk1: the best-tracking copies were never sampled). The fight-free
// companion of [NC] parityOrigin.
long long g_walkLagN[3] = {0, 0, 0};   // T-196: [2] = the run class whose owner truly RUNS (OwnerRuns), a subset of [1]
double    g_walkLagSum[3] = {0.0, 0.0, 0.0};
long long g_walkLagBucket[3][7];   // [walk/run/trueRun][<1, 1-5, 5-10, 10-20, 20-40, >=40, ahead]
bool      g_walkLagInit = false;
const float kAuthSpeedFloor = 4.0f;   // never command 0: WriteDesiredSpeed refuses it and a stopped
                                      // authority does not push anyway
long long g_speedRefused  = 0;   // F311: resolved to <= 0 and we DID NOT write it
long long g_speedRestored = 0;   // put back the engine's own value when the lever was switched off
long long g_speedRestoreSkipped = 0;   // F338: deliberately NOT put back - the address is not ours

// F350 - the velocity path, one counter per outcome so a run that shows no improvement can say
// which half failed. Authority side:
// F353 - one counter per REASON, because "sent as zero" had four different causes and three of
// them are defects.
long long g_velMoving      = 0;   // a nonzero velocity went on the wire
long long g_velStopped     = 0;   // genuinely below kMovingEpsilon
long long g_velFirstSample = 0;   // no previous sample for this uid yet - nothing to measure
long long g_velProne       = 0;   // prone: getPosition() is the ANIMATION node (T063/F203)
long long g_velRagdoll     = 0;   // F357: ragdolled - the movement write-back is skipped entirely
long long g_velInSomething = 0;   // F357: in a cage/bed/slot
long long g_velCarried     = 0;   // F357: the position follows the CARRIER, not the body
// F358 - **THE ONE THAT SAYS THE LIST ABOVE IS INCOMPLETE.** Non-zero while all four named
// conditions are false means `getPosition()` returned a layer we did not predict, and the
// enumeration needs another entry. The refusal is safe either way; the counter is what turns a
// silent hole into a reported one.
long long g_velLayerMismatch = 0;
long long g_velNonFinite   = 0;   // the arithmetic produced a non-number - a DEFECT, not "stopped"
long long g_velTeleport    = 0;   // above kMaxPlausibleSpeed: a jump, not a walk
long long g_velBadClock    = 0;   // non-positive elapsed time between samples
long long g_velNoSampleSlot = 0;  // mirror index beyond the sample array - velocity unavailable
// Peer side:
// FRAMES, not characters and not messages - these are per-frame per-puppet decisions, and F342
// was written because a counter whose name did not carry its unit was read as the wrong quantity.
long long g_leadAppliedFrames  = 0;   // the aim point was carried forward
long long g_velExpiredFrames   = 0;   // the last velocity was too old to trust - treated as STOPPED
long long g_leadDistClampedFrames = 0; // F357: the lead hit kMaxLeadUnits and was shortened
long long g_leadDecayedOutFrames  = 0; // F358: the held lead has decayed fully to zero
long long g_moveVelAbsurd      = 0;   // F357: a finite but impossible velocity arrived - lead dropped
long long g_arriveStoppedFrames = 0;  // arrival that issued the explicit stop (authority stationary)
long long g_arriveHeldFrames   = 0;   // arrival NOT stopped, because the authority is still moving
long long g_aimDegenerateFrames = 0;  // standing exactly on the aim point - stopped, not divided by
// F353 - DropPuppet now stops the engine before forgetting the puppet. Counted, because "we let go
// of a character that was still walking" is exactly the silence F062 makes possible.
long long g_dropStopped     = 0;
long long g_dropNoMovement  = 0;   // its CharMovement could not be read - could not be stopped
long long g_dropStopSkipped = 0;   // stale-uid path: the address may belong to a stranger (F338)
long long g_dropNoChar      = 0;   // F355: the character itself was unreadable - the T090 cohort
long long g_dropStopFaulted = 0;   // F355: the stop itself faulted and SEH caught it
long long g_dropNoPuppet    = 0;   // F357: called for a uid we hold no puppet for - counted nowhere before

// F350 - the switch. ON by default, because measuring it is the point of the next run; `lead off`
// restores the deployed behaviour exactly (no extrapolation, and the arrival branch stops on
// distance alone), so the fix can be compared against its own absence without a rebuild. The same
// rule as `catchup`, `drivespeed` and `combatpulse`.
bool g_leadOn = true;
// H021 (user-approved 2026-09-02): PATHFINDING DRIVE. The straight-line push (F313) leaves copies stuck
// against geometry for minutes (F427). ON BY DEFAULT since AUD 2026-09-17: a copy more than kOrderDoneDist
// from its aim point is handed a MOVE ORDER (TaskType 29) to that point through the order door M-C proved
// (F425), and the engine walks it there with its own pathfinding, doors and gait; the order is re-issued
// when the aim point moves more than kOrderReissueDist or every kOrderReissueTicks. No push while an order
// is live. `orderdrive off` is the control - it restores the straight-line push.
bool g_orderDrive = true;
const int   kMoveOrderType     = 29;     // 'Move order' (taskdata-table.md)
const float kOrderDoneDist     = 2.0f;   // within this of the aim: let the engine's own arrival finish it
const float kOrderReissueDist  = 16.0f;  // H023: aim moved this far from the ORDERED point -> new order (was 8)
const long long kOrderReissueTicks = 240; // H023: retry an UNCONSUMED order after ~2 s; a consumed one is left alone
const float kOrderArriveDist   = 2.0f;   // H023: copy within this of the ORDERED point = arrived -> may re-order
const float kOrderAheadRetract = -2.0f;  // H023: along-track error below this = the copy is AHEAD -> retract, wait
const float kOrderRunEnter     = 10.0f;  // H023: along-track lag to start RUNNING (bounded catch-up, user policy)
const float kOrderRunLeave     = 4.0f;   // H023: along-track lag to go back to WALK
long long g_orderConsumedFrames = 0, g_orderUnconsumedFrames = 0, g_orderRetries = 0, g_orderAheadRetracts = 0, g_orderAheadHeld = 0;
// H024 - the order drive's remaining role: UNSTICK a stalled copy once, then hand back to the push.
const long long kUnstickHoldTicks = 600;   // ~5 s: the engine gets this long to path around the obstacle
const float kUnstickDoneDist = 4.0f;       // within this of the aim = unstuck, back to the push
long long g_unstickOrders = 0, g_unstickResolved = 0, g_unstickTimedOut = 0, g_unstickHeldFrames = 0;
// H024 - facing: the authority's facing vector rides in MOVE; applied to a STATIONARY copy through the engine's own
// faceToward (Character vtbl +0x3B0), rate-limited, never inside a native window.
const float kFaceApplyCos = 0.966f;        // apply when the copy's facing is more than ~15 degrees off
const long long kFaceApplyEveryTicks = 60; // ~0.5 s
long long g_faceWrites = 0, g_faceWriteFailed = 0, g_faceSkippedNoVector = 0;
long long g_orderIssued = 0, g_orderIssueFailed = 0, g_orderHeldFrames = 0, g_orderDoneFrames = 0, g_orderFallbackPush = 0;
// H022 - the gait under the engine's task follows the authority's, and RUNS while behind (the user's bounded
// catch-up policy, H020, carried over from the push to the order drive).
const float kOrderRunAuthority = 50.0f;   // authority desiredSpeed above this = it is running (JOG 55 / RUN 999)
long long g_orderRunFrames = 0, g_orderWalkFrames = 0, g_orderSpeedWrites = 0, g_orderSpeedWriteFailed = 0;
// D1 (read-movement, 2026-09-23) - PATHDRIVE, DEFAULT ON since D1-c (user decision 2026-09-23, after the T260 churn was smoothed). F431 retired the Move ORDER (AI task 29) as the drive on an
// untimed 'decision cadence'. Below the AI task layer the engine has a direct path call: CharMovement::setDestination
// (0x6607E0) sets path mode (+0x378 = 0), clears stopped and queues a request the NavMeshMain thread computes. A copy
// that is more than kPathEnter from its aim (or stalled one progress window) is walked there by that call; within
// kPathLeave it goes back to the push (hysteresis). The call is re-issued only when the aim moved kPathReissueDist from
// the last destination (D1-b: no time-only re-issue; never sooner than kPathIssueFloorSec for one copy; at most kPathBudgetPerFrame a frame). desiredSpeed still caps the speed in every mode (F305), so the
// owner's level keeps being written. `pathdrive off` = the push plus the H024 unstick order, exactly as before.
bool g_pathDrive = true;    // D1-c: default ON (user decision 2026-09-23); `pathdrive off` = the push plus the H024 unstick, exactly
const float  kPathEnter        = 6.0f;   // aim farther than this -> path
const float  kPathLeave        = 3.0f;   // aim closer than this -> push again
const float  kPathReissueDist  = 4.0f;   // the aim moved this far from the last destination -> re-issue
const double kPathIssueFloorSec = 0.5;   // D1-b item 2: never two issues for one copy closer than this - D3: unless the last path LANDED (agent status), then cooppath::kMinIssueGapSec (0.2 s)
const int    kPathBudgetPerFrame = 8;   // D1-b item 2: setDestination calls per frame, all copies together (oldest waiting first)
const double kPathPendingSec   = 1.0;   // D1-b items 1/3: an issued (or budget-deferred) path counts as pending this long
const float  kPathProgressMin  = 2.0f;  // D1-b item 1: the aim distance must shrink this much per progress window
const double kPathHoldSec      = 5.0;   // D1-b item 1: after a path without progress, the push this long before a path again
// D1-c (T260 churn: pathToPush 24590 / pushToPath 24676 / pathAwaitAbandoned 19575 of 39613 in ~300 s) - the OWNER's motion
// decides the mode, the re-issue and the progress judgement. ownerSpeed = |p.velX, p.velZ| (the owner's streamed velocity,
// ApplyRemoteMove) while it is fresh (age <= the lead horizon), else 0.
const float  kPathOwnerStillSpeed = 1.0f;  // owner below this (u/s) = stationary
const double kPathOwnerStillSec   = 0.5;   // owner stationary this long (and the copy within kPathLeave) -> path mode may end
// D2 (read-stopgo, user T261: copies 'sprint, stop, sprint, stop') - PURSUIT while the owner moves. Re-issuing does not stop a
// walking copy (0x145CB0 only queues, 0x144C90 swaps waypoints); a copy stops on ARRIVAL at its own path end. So the destination
// leads the owner and is re-planned BEFORE the copy arrives. Replaces the D1-c lead-seconds threshold (aim moved >= max(4,
// speed x 0.5)) and the D1-c waited-moving hold.
// D3 (read-pathend, T266: 46% of new paths ARRIVED while the owner moved - the D2 near-end at 0.9 s of the OWNER's speed
// left less path than the 0.5 s floor plus the 0.08-0.51 s landing needs): the lead is
// clamp(max(ownerSpeed, copy speed) x 1.6 s + 2, 8, 90) and the re-plan fires within max(3, copy speed x 1.1 s) of the end,
// on the copy's OWN measured speed - src/common/pathlead.h holds the arithmetic (offline-tested); these are its values.
const float  kPathLeadSec       = cooppath::kLeadSec;      // D3: seconds of max(owner, copy) speed (+ cooppath::kLeadPad units)
const float  kPathLeadMin       = cooppath::kLeadMin;      // D2-b: ... at least this many units
const float  kPathLeadMax       = cooppath::kLeadMax;      // D2-b: ... at most this many units (runners 45-85 u/s)
const float  kPathNearEndMin    = cooppath::kNearEndMin;   // D3: re-plan within max(this, COPY speed x kPathNearEndSec) of the destination
const float  kPathNearEndSec    = cooppath::kNearEndSec;   // D3: 1.1 s of the copy's own speed (capped at 0.9 x the lead)
const double kPathOwnerStillConfirmSec = 0.3;   // D2-b item 3: the owner is still only after its smoothed speed < 1 this long
const double kPathTurnPersistSec = 0.25;  // D2-b item 5: a turn must persist this long before it re-plans
const float  kPathAheadStopDist = 2.0f;   // D2-b item 2: a copy this far ahead of its aim along the owner's heading stops
const float  kOwnSmoothAlpha    = 0.3f;   // D2-b item 3: EMA weight of a new owner speed/heading sample
const float  kOwnTeleportDist = 50.0f;     // D2-c (recheck-d2b): an owner sample this far (x/z) from the previous one is a TELEPORT - the smoothing re-primes
const long long kPathFailTrustTicks = 2;   // D2-c (recheck-d2b): pathFailed() is trusted this many frames after an issue even if it never read 0
const float  kOwnSmoothAlphaStill = 0.7f; // D2-b item 3: ... of a genuine standing sample (zero velocity, position static)
long long g_pathAheadStopped = 0, g_pathFailedRetryAim = 0, g_pathStopIssueBypassFloor = 0;   // D2-b
long long g_ownSmoothReset = 0;   // D2-c
const float  kPathTurnCos       = 0.8660254f;   // D2: cos 30 deg - the owner's heading turned more than this from the destination's
const float  kPathOffLineMax    = 8.0f;   // D2: the aim left the destination line by more than this
const float  kPathDestMinSep    = 2.0f;   // D2: a new destination at least this far from the last (the engine drops closer ones)
const float  kPathArrivedDist   = 1.0f;   // D2: the engine's arrival radius - a copy this close to its destination has stopped there
const float  kCatchupLeavePath  = 8.0f;   // D2 item 4: the H020 catch-up exit for path-driven copies (kCatchupLeave for pushed ones)
long long g_pathReplanNearEnd = 0, g_pathReplanTurn = 0, g_pathReplanOffLine = 0, g_pathStopIssue = 0;   // D2: issues by reason
long long g_pathArrivedWhileOwnerMoving = 0;   // D2: a copy reached its destination while its owner moved - D3's verdict counter (target ~0; T266 B: 16816)
long long g_pathReplanTooClose = 0;   // D3: EVENTS (once per destination) a due re-plan was refused < kPathDestMinSep from the last destination - owner still, or no extension possible
long long g_pathReplanExtended = 0;   // D3: issues whose too-close destination was pushed forward along the owner's heading instead of refused
long long g_pathReplanOnLanded = 0;   // D3: issues inside kPathIssueFloorSec, allowed because the previous path had LANDED (agent status 1)
long long g_pathLandedReadFaulted = 0;   // D3: destinations whose agent-status read faulted (or found no agent) - the floor applied
const float  kPathGrowMax         = 4.0f;  // owner moving: 'no progress' = the aim distance GREW by more than this in a window
long long g_pathLeaveOwnerStopped = 0;     // D1-c: path -> push because the owner had stopped >= kPathOwnerStillSec within kPathLeave
long long g_pathDeadFallback = 0;       // D1-d: path stints ended because the copy itself stood still a whole window (a dropped path)
long long g_pathNoProgressFallback = 0; // D1-b item 1: path stints ended because the aim distance did not shrink in a window
long long g_pathBudgetDeferred = 0;     // D1-b item 2: FRAMES a due issue waited for the per-frame budget
int g_pathBudgetLeft = 0;               // D1-b item 2: budget left this frame for copies not already waiting
const float  kPathMovingSpeed  = 1.0f;   // 'moving' for the delay timer: path mode AND measured speed above this (u/s)
const size_t kPathMoveModeOff  = 0x378;  // CharMovement movement mode: 0 = path, 2 = STEER_BY_DIRECTION (the push)
long long g_pathIssued = 0, g_pathIssueFailed = 0;
long long g_pathDeduped = 0;             // FRAMES in path mode on which no re-issue was needed
long long g_pathToPush = 0, g_pushToPath = 0, g_pathEnterByStall = 0;
long long g_pathIneligibleFrames = 0;    // FRAMES a copy wanted the path but was prone/ragdolled/carried/in something
long long g_pathFrames = 0;              // FRAMES driven by a path request
long long g_pathFailed = 0, g_pathFailedReadFaulted = 0, g_pathLeaveFaulted = 0;
long long g_pathMoveLe2 = 0, g_pathMoveLe10 = 0, g_pathMoveLe60 = 0, g_pathMoveGt60 = 0, g_pathMoveMaxFrames = 0;   // issue -> moving, FRAMES
long long g_pathAwaitAbandoned = 0;      // an issue whose copy left path mode (or was snapped) before it was seen moving
int g_pathLogLines = 0;                  // the first 16 path events are logged
// THE FOLLOW DRIVE's counters (REPORT follow[...] / followMore[...]): drive frames of a moving owner's copy by the target's
// mode; never-past halts at N / at the stop point; stops sent (owner) / received / already over on arrival / ended by a
// moving report; MOVEs dropped as older than the stop; delay re-reads; reports older than the newest; path issues by
// destination kind (none, stop, goal, keep, newest); re-plans on turning hidden; slowed frames; stop sends that failed (the
// next frame retries); stops for a uid with no follow copy here. The scale (REPORT followScale[...]): drive frames that ran the
// replay / skipped it as idle (followplay::ReplayIdle); the owner's stop watch now and at its largest; stops sent for this
// game's characters outside its player faction; stops received for copies outside every player stand-in's faction.
long long g_followInterp = 0, g_followExtrap = 0, g_followHeld = 0, g_followHeldAtNewest = 0, g_followHeldAtStop = 0;
long long g_followStopSent = 0, g_followStopRecv = 0, g_followStopOver = 0, g_followStopCleared = 0, g_followStaleMoveDropped = 0;
long long g_followDRecalc = 0, g_followOutOfOrder = 0, g_followIssue[5] = { 0, 0, 0, 0, 0 }, g_followHiddenReplan = 0;
long long g_followPaceFrames = 0, g_followStopSendFailed = 0, g_followStopUnknown = 0;
long long g_followReplayFrames = 0, g_followIdleFrames = 0, g_followStopSentNpc = 0, g_followStopRecvNpc = 0;
size_t g_followOwnPeak = 0;
// The path budget by copy (REPORT followPath[...]): path requests issued to player copies / NPC copies; frames an issue of
// each waited for the budget; player copies that took the wide stop radius after they stopped closing on the stop point;
// drive frames whose owner's newest report moves but has expired (the owner went quiet mid-walk); the owner's sent-stop
// table now, and its entries dropped because their character is gone.
long long g_followPathIssued[2] = { 0, 0 }, g_followPathDeferred[2] = { 0, 0 };   // [0] player copies, [1] NPC copies
long long g_followStopWide = 0, g_followOwnerExpired = 0, g_followOwnSentEvicted = 0;
// Behind schedule (REPORT followSchedule[...]): replay frames a copy spent behind schedule (followplay::BehindScheduleStep) and
// the times one fell behind; stuck / far snaps that placed a follow copy on its owner's point (followplay::OwnerPoint), and of
// those the ones taken while it was behind schedule; reports not put in the recording (a zero velocity from a position that
// moved further than a standing owner can, or a velocity over the plausible speed) - the point still moves the owner's
// newest reported point.
long long g_followBehindFrames = 0, g_followBehindEnters = 0, g_followSnapOwnerPt[2] = { 0, 0 }, g_followSnapBehind = 0;
long long g_followUnrecorded = 0;
std::map<int, followplay::ClockOff> g_followClk;   // the owner's clock per owner game (its slot)
long long g_handoffSkippedOrderDrive = 0;
                                       // any more, so the write would land on another character

// F340 - the two halves of "where does a non-finite motion vector come from". `moveNonFinite`
// counts targets rejected at ingest (it came over the wire); `driveNonFinite` counts writes refused
// at the engine boundary AFTER a finite target was stored (so the corruption is local). They cannot
// both be zero if T092's five stuck puppets recur, and which one moves is the answer.
long long g_moveNonFinite  = 0;
long long g_driveNonFinite = 0;

// F341 - the two causes behind "MOVE for unknown uid". `Retired` means we had it and this engine
// withdrew it while the authority kept it: a permanent divergence for that character. `Unheard`
// means we were never told about it, which is ordinary. One line used to cover both.
long long g_moveForRetired = 0;
long long g_moveForUnheard = 0;

// F342 - the DISTINCT characters behind `moveForRetiredMsgs`.
//
// F343 - **NOT a ring, and the comment here used to say it was.** It SATURATES: once full it keeps
// the first 64 and discards every later one. The lost-uid store in spawn.cpp is a true ring and
// evicts the OLDEST. Two structures with opposite eviction behaviour described by one word is how a
// reader ends up reasoning about the wrong one.
//
// F343 - and the overflow counter counts **messages**, because past saturation there is no store to
// deduplicate against - so it is NAMED for messages. Counting messages under a name ending `Uids`
// is the exact defect the split above was written to remove, and it was reintroduced by the fix
// for it: at ~10 Hz a single 65th character would contribute ~18,000 to a field a reader would
// take for a character count.
const int kDivergedSlots = 64;
unsigned int g_divergedUids[kDivergedSlots] = {0};
int          g_divergedCount = 0;
long long    g_divergedBeyondStore = 0;   // MESSAGES, not uids - see above

void NoteDivergedUid(unsigned int uid)
{
    if (uid == 0) return;   // F343: symmetry with NoteUidPermanentlyLost; 0 is not a character
    for (int i = 0; i < g_divergedCount && i < kDivergedSlots; ++i)
        if (g_divergedUids[i] == uid) return;
    if (g_divergedCount >= kDivergedSlots) { ++g_divergedBeyondStore; return; }
    g_divergedUids[g_divergedCount++] = uid;
}

// Split out because `__try` cannot live in a function that requires object unwinding, and the
// drive path builds std::strings. Third time this has come up; it is a compiler rule, not a
// judgement call.
//
// F311 - IT REFUSES TO WRITE ZERO, and it hands back what it found.
//
// `drivespeed -1` resolves to the character's own `speedCap`, and **a zero `speedCap` is a live
// hypothesis in this very investigation** (F310 established that field is refreshed from
// `animation->getMaximumSpeed()` every periodic tick and that what drives it low is still open).
// Writing that through would set `desiredSpeed = 0`, count it as a write, and produce a report
// reading `driveSpeed=-1.0 speedWrites=<large> mvBlock=...SPEED0...` - which any reader takes as
// "the lever is on and it still will not move" when what happened is that the lever wrote a zero.
// A lever that can silently apply the very fault it was built to rule out is worse than no lever.
//
// `*outOriginal` is only set on the first successful write for a character, so the value put back
// later is the ENGINE's, not one of ours (F242 / lesson 21: anything we set on a puppet we must be
// able to unset, and the undo ships with the do).
// F355 - the ENGINE STOP, behind SEH like every other engine write in this file.
//
// `DropPuppet` is reached from the `GameWorld::destroy` detour (F321/F334), i.e. while the engine
// is tearing an object down, and `PlausibleObj` validates only the vtable word. Its two immediate
// neighbours, `WriteDesiredSpeed` and `RestoreDesiredSpeed`, both wrap their writes; the stop
// added beside them did not, and the comment justifying that path claims 'PlausibleObj PLUS SEH'.
//
// A separate function because `__try` cannot live in one that requires object unwinding.
// F357 - `inRagdoll()` DOUBLE-DEREFERENCES the AnimationClass, so it needs both the pointer
// guard and SEH. Same treatment the P036 probe already gives it; factored out because
// `MeasureVelocity` needs it on the hot send path and `__try` cannot live in a function that
// requires object unwinding.
//
// **Fails CLOSED**: unreadable is treated as ragdolled, i.e. do not trust the position. The cost
// of a false positive is one skipped velocity sample; the cost of a false negative is a fabricated
// walking velocity that clears every downstream guard.
bool SafeIsRagdoll(Character* c)
{
    void* anim = 0;
    __try { anim = *(void**)((char*)c + kAnimationClassOff); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    if (!PlausibleObj(anim)) return true;
    __try { return c->inRagdoll(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
}

// Review 4, item 12: ONE predicate for 'downed' - the host's host_move record and the peer's hop oracle
// were classifying by different rules (prone-only vs prone-or-ragdoll), which put the host's ragdoll
// throws into the JUDGED standing baseline and loosened O7 in the peer's favour.
bool DownedImpl(Character* c)   // internal linkage; exported as coop::IsDownedCharacter below
{
    if (!PlausibleObj(c)) return true;   // fails closed, as SafeIsRagdoll does
    int prone = 0;
    __try { prone = *(int*)((char*)c + kProneStateOff); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    return prone != 0 || SafeIsRagdoll(c);
}

// Review 4, item 3: the vtable slot reads, the thunk-byte read and the setter call go through SEH
// like every other engine write in this file. POD in, POD out - nothing to unwind inside __try.
static bool SafeReadVtSlots(void* mv, uintptr_t* slotB8, uintptr_t* slotC8)
{
    __try { void** vt = *(void***)mv; *slotB8 = (uintptr_t)vt[0xB8 / 8]; *slotC8 = (uintptr_t)vt[0xC8 / 8]; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}
static bool SafeResolveJmpThunk(uintptr_t* addr, uintptr_t imageBase)
{
    __try
    {
        const unsigned char* b = (const unsigned char*)*addr;
        if (*addr >= imageBase && *addr - imageBase < coop::AddrTextEnd() && b[0] == 0xE9)
            *addr = *addr + 5 + (uintptr_t)(intptr_t)*(const int*)(b + 1);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}
typedef void (*MvSetPosFn)(CharMovement*, const Ogre::Vector3&);
static bool SafeCallSetPos(MvSetPosFn fn, CharMovement* mv, const Ogre::Vector3* np)
{
    __try { fn(mv, *np); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

bool SafeStopMovement(CharMovement* mv)
{

    if (!PlausibleObj(mv)) return false;
    __try { mv->steerDirectly(Ogre::Vector3(0.0f, 0.0f, 0.0f), 0.0f); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

bool WriteDesiredSpeed(void* mv, float requested, bool haveOriginal, float* outOriginal)
{
    if (!PlausibleObj(mv)) return false;
    __try
    {
        float want = requested;
        if (want < 0.0f) want = *(float*)((char*)mv + 0xB4);   // speedCap
        if (!(want > 0.0f)) return false;                      // refuse zero / negative / NaN
        if (!haveOriginal) *outOriginal = *(float*)((char*)mv + 0xBC);
        *(float*)((char*)mv + 0xBC) = want;                    // desiredSpeed
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

bool RestoreDesiredSpeed(void* mv, float original)
{
    if (!PlausibleObj(mv)) return false;
    __try { *(float*)((char*)mv + 0xBC) = original; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// Character-side conditions that stop LOCOMOTION ITSELF, one level above the movement object.
// `Character::threadedUpdate` (0x5C71A0) calls movement->update(dt) only when
// !inRagdoll() && inSomething == 0 && !hasDied; otherwise it calls movement->halt() - which
// forces movementMode back to MOVE_NORMAL, zeroes speedNow and sets officiallyStopped, i.e.
// it actively UNDOES our drive every frame. And when inRagdoll() is true it skips BOTH the
// movement update AND the write-back of the movement position into Character+0x48 - so the
// character's reported position freezes byte-identically, which is exactly the symptom.
//
// `inSomething` (+0x2F8) and `_isBeingCarried` (+0x3D4) are Character's fields at those offsets.
// `hasDied` has no known offset, so it is read through the engine's own `Character::hasDied()` rather
// than from a guessed offset. `inRagdoll()` likewise - it double-dereferences the AnimationClass, so it is
// only called when that pointer has already been validated.
struct CharDbg { int inSomething, carried, dead, ragdoll, animBlocked; bool ok; };

bool ReadCharDbgFields(const void* c, CharDbg* o)
{
    o->inSomething = o->carried = 0; o->dead = o->ragdoll = o->animBlocked = -1; o->ok = false;
    if (!PlausibleObj(c)) return false;
    __try
    {
        o->inSomething = *(int*)((char*)c + 0x2F8);
        o->carried     = *(unsigned char*)((char*)c + 0x3D4);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    o->ok = true;
    return true;
}

// F311. The three ENGINE CALLS, in their own SEH-guarded function.
//
// They were briefly in `MovementDebug`, which is the one place they could not be: that function
// concatenates std::strings, so it requires unwinding, so `__try` is illegal in it - which is
// exactly why `ReadMoveDbg` and `ReadCharDbgFields` were split out in the first place. I split out
// the plain field reads for that reason and then put three engine calls, which dereference far more
// than the fields do, straight into the unguarded caller beside them.
//
// And `PlausibleObj(anim)` was never the right guard for them anyway. It proves the first 8 bytes
// of `anim` read and look like a vtable. `inRagdoll()` double-dereferences `animation->getRagdoll()`;
// `blocksWaypointMovement()` walks `animation+0x228`, `+0x210` and a
// `RagdollAnimation` it calls a method on; `hasDied()` was being called with nothing validated at
// all, and was called even when the animation check had already failed. **None of the pointers
// those three actually follow is the one that was checked.**
//
// This runs once per puppet per report, and a report covers 150+ puppets, so "unlikely" is the
// wrong standard: the condition that makes it fault is a character being torn down, which is
// continuous in a streamed world.
//
// Each result is -1 = NOT READ, 0/1 = the engine's answer. -1 is propagated into the verdict as an
// explicit UNKNOWN rather than being allowed to look like a negative.
void ReadCharEngineFlags(Character* c, CharDbg* o)
{
    void* anim = 0;
    __try { anim = *(void**)((char*)c + kAnimationClassOff); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }

    if (PlausibleObj(anim))
    {
        __try { o->ragdoll = c->inRagdoll() ? 1 : 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { o->ragdoll = -1; }

        __try { o->animBlocked = ((::AnimationClass*)anim)->blocksWaypointMovement() ? 1 : 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { o->animBlocked = -1; }
    }

    __try { o->dead = c->hasDied() ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { o->dead = -1; }
}

std::string MovementDebug(Character* c)
{
    if (!PlausibleObj(c)) return " mv=NOCHAR";
    CharMovement* mv = c->movement;
    MoveDbg d;
    if (!ReadMoveDbg(mv, &d)) return " mv=UNREADABLE";

    CharDbg cd;
    if (ReadCharDbgFields(c, &cd)) ReadCharEngineFlags(c, &cd);

    float dirLen = sqrtf(d.dirX * d.dirX + d.dirY * d.dirY + d.dirZ * d.dirZ);
    float havokSpeed = (d.want < d.maxs) ? d.want : d.maxs;   // what update() hands Havok

    // F311 - THE VERDICT, and it lists EVERY blocking condition rather than the first one found.
    //
    // The first version stopped at the first hit, in what I labelled "the order the engine actually
    // tests them", and it was wrong on both counts. It was not the engine's order - the engine
    // pushes `min(desiredSpeed, speedCap)` into Havok BEFORE the animation guard and before the
    // mode dispatch, so SPEED0 belongs second, not eighth. And an ordered first-hit verdict MASKS:
    // a puppet that is animation-blocked AND has a zero speed reported only the animation, so
    // clearing that condition would have left it motionless for a reason the probe had seen and
    // not printed.
    //
    // A list cannot mask. `none` now means every condition was READ and none of them is blocking -
    // a real result, and the one that would send the investigation somewhere this probe does not
    // look. `UNKNOWN-*` means a condition could not be read and is neither confirmed nor excluded;
    // it is never silently folded into `none`.
    std::string block;
    #define COOP_BLOCK(tok) do { if (!block.empty()) block += "+"; block += (tok); } while (0)

    // Character level: `threadedUpdate` skips BOTH locomotion and the position write-back.
    if (cd.ragdoll == 1)     COOP_BLOCK("RAGDOLL-locomotion+posWriteback-both-skipped");
    if (cd.ragdoll == -1)    COOP_BLOCK("UNKNOWN-ragdoll-not-read");
    if (cd.ok && cd.carried) COOP_BLOCK("CARRIED-locomotion+posWriteback-both-skipped");
    // Character level: `halt()` every frame, which forces the mode back to MOVE_NORMAL.
    if (cd.dead == 1)        COOP_BLOCK("DEAD-halt-every-frame");
    if (cd.dead == -1)       COOP_BLOCK("UNKNOWN-isDead-not-read");
    if (cd.ok && cd.inSomething) COOP_BLOCK("INSOMETHING-halt-every-frame");
    if (!cd.ok)              COOP_BLOCK("UNKNOWN-character-fields-not-read");

    // Movement level, in `CharMovement::update`'s own order.
    if (!d.havok)            COOP_BLOCK("NOHAVOK-update-returns-instantly");
    if (havokSpeed <= 0.0f)  COOP_BLOCK("SPEED0-direction-accepted-magnitude-zero");
    if (d.animOverride)      COOP_BLOCK("ANIMOVERRIDE-mode-dispatch-bypassed");
    if (cd.animBlocked == 1) COOP_BLOCK("ANIMINCAPABLE-engine-says-cannot-take-waypoint-movement");
    if (cd.animBlocked == -1) COOP_BLOCK("UNKNOWN-animIncapable-not-read");
    if (d.mode != 2)         COOP_BLOCK("MODE-reverted-drive-ignored");
    if (dirLen <= 0.0f)      COOP_BLOCK("DIR0-no-direction-written");
    // F317. A NaN anywhere in the vector handed to the physics layer makes the drive a no-op for
    // that character regardless of everything else on the line, and T086 carried one for the whole
    // run behind a field that rendered it as `1.$`.
    if (!Finite(d.dirX) || !Finite(d.dirY) || !Finite(d.dirZ) || !Finite(dirLen))
                             COOP_BLOCK("DIRNAN-desiredMotion-is-not-a-number");
    if (!Finite(d.limit))    COOP_BLOCK("LIMITNAN-moveLimit-is-not-a-number");
    if (!Finite(d.want) || !Finite(d.maxs))
                             COOP_BLOCK("SPEEDNAN-desiredSpeed-or-maxSpeed-is-not-a-number");
    #undef COOP_BLOCK

    if (block.empty()) block = "none";

    return " mvBlock=" + block
         + " mvMode=" + N2(d.mode)
         + " mvHavok=" + std::string(d.havok ? "1" : "0")
         + " mvAnimOvr=" + N2(d.animOverride)
         + " mvDirLen=" + F2(dirLen)
         + " mvLimit=" + F2(d.limit)
         + " mvWant=" + F2(d.want)
         + " mvMax=" + F2(d.maxs)
         + " mvToHavok=" + F2(havokSpeed)
         + " mvCur=" + F2(d.cur)
         + " mvMoving=" + std::string(d.moving ? "1" : "0")
         + " mvStopped=" + std::string(d.stopped ? "1" : "0")
         + " chAnimIncapable=" + N2(cd.ok ? cd.animBlocked : -1)
         + " chRagdoll=" + N2(cd.ok ? cd.ragdoll : -1)
         + " chCarried=" + N2(cd.ok ? cd.carried : -1)
         + " chDead=" + N2(cd.ok ? cd.dead : -1)
         + " chInSomething=" + N2(cd.ok ? cd.inSomething : -1);
}

// (kProneStateOff / kAnimationClassOff / kLogicalPosOff / kAnimPosOff are declared above the
// P036 probe, which needs the animation-class offset to guard its inRagdoll() call.)

// F184/F181a: a prone character CANNOT WALK, so a driven puppet that goes down keeps whatever
// offset it had at that moment - T058 measured 8.0 units frozen across 64 s with the driver
// reporting itself active the whole time. Two players would see the same body lying in
// different places for as long as it is down.
//
// Walking cannot fix that; only a position snap can. `DrivePuppet` has always refused to snap
// ("the corrective is an M2 acceptance decision, not a guess made here") and that refusal was
// right when written - there was no evidence it was needed. T058 is the evidence. (The general
// question is now settled too: decision 60, user 2026-09-22 - a far copy is teleported.)
//
// The threshold is deliberately NOT zero. A small offset on a body lying on the ground is far
// less visible than a body teleporting, so snapping every downed puppet would trade a subtle
// defect for an obvious one. Above this distance the offset is the worse of the two.
//
// OPEN JUDGEMENT, recorded rather than settled: which is actually less noticeable to a player
// is a question about what the SCREEN shows, and no instrument here can score it. 5.0 units is
// a defensible default, not a measured optimum.
const float kProneSnapDist = 5.0f;

// F189 - A CORRECTIVE THAT IS NOT WORKING MUST STOP, NOT REPEAT.
//
// T061 ran the snap **22,375 times on one character over 168 seconds** - about 133 calls per
// second - while its position stayed BYTE-IDENTICAL, and it was still firing when the game was
// closed. The correction did nothing, so the condition that triggered it stayed true forever.
// It also wrote a 2.8 MB log.
//
// The specific cause is F189 (relocationTeleport is the wrong function). The STRUCTURAL cause
// is that the corrective never asked whether it had worked. A fix that cannot fail visibly
// will retry forever, and "it ran 22,000 times" is not evidence it did anything.
//
// So every correction now checks the drift it produced. Three in a row that fail to reduce it
// and this puppet is left alone, with ONE line saying so.
const int   kSnapMaxTries    = 3;
const float kSnapMinProgress = 0.5f;   // units of drift a correction must remove to count

// Which real game function is `AnimationClass::setPosition`?
//
// Its own function: __try cannot live in a function that needs object unwinding, and the caller
// builds std::strings. Kept separate rather than dropping the guard - F037 cost two in-game runs
// to a function chosen by NAME that resolved to a trivial float setter, and a resolution printed
// by the run itself is worth more than the same number in my notes.
// The guarded half, kept free of anything that unwinds so __try is legal here.
uintptr_t ResolveSetPositionAddress()
{
    __try { return (uintptr_t)coop::AddrAbs(kMig3AnimSetPosition); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void LogSetPositionAddress()
{
    uintptr_t real = ResolveSetPositionAddress();
    uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    unsigned long long rva = (real > base) ? (unsigned long long)(real - base) : 0ull;

    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "[P023] AnimationClass::setPosition resolves to rva 0x" << std::hex << rva
       << " - EXPECTED 0x5b1070 (resolve_stub slot 622). If these differ, the snap is calling"
          " something else and its result means nothing.";
    DebugLog(ss.str());
}

// P023 - WHICH LAYER is this character's position actually coming from?
//
// F193 localized, BY DECOMPILE ONLY, that `Character::getPosition` (0x5CDBF0) does not read a
// field - it picks one of four, and while a character is down the predicate at 0x7D08A0 selects
// the animation node at `AnimationClass+0x98` rather than the logical position at
// `Character+0x48`. If that is right, BOTH correctives spent so far wrote a field nobody was
// reading, which is why swapping the engine call changed nothing.
//
// THIS PROBE MOVES NOTHING. It exists so the mechanism is measured before a third attempt is
// built rather than after (lesson 16). It prints the two candidate layers beside what
// getPosition actually returned, so the answer is a comparison and not an inference.
//
// A STANDING sample is printed too, and it is the CONTROL: if `logical+0x48` never matches
// savedPosition even when the character is upright, then I am reading the wrong offset and the prone
// reading proves nothing. Without that line the probe could not tell "the layer switched" from
// "my offset is garbage".
void LayerProbe(unsigned int uid, Character* ch, const Ogre::Vector3& got, int prone,
                const char* why)
{
    if (!PlausibleObj(ch)) return;

    const float* logical = (const float*)((const char*)ch + kLogicalPosOff);
    std::string animPos  = "NOANIM";
    void* anim = *(void**)((char*)ch + kAnimationClassOff);
    if (PlausibleObj(anim))
    {
        const float* a = (const float*)((const char*)anim + kAnimPosOff);
        animPos = F2(a[0]) + "," + F2(a[1]) + "," + F2(a[2]);
    }

    // Name which layer AGREES with what we were handed, so the log answers the question
    // directly instead of leaving three coordinate triples to be diffed by eye later.
    float dl = fabsf(got.x - logical[0]) + fabsf(got.z - logical[2]);
    const char* agrees = (dl < 0.05f) ? "logical" : "NOT-logical";

    DebugLog(std::string("[P023] uid=") + N2(uid) + " why=" + why
             + " prone=" + N2(prone)
             + " savedPosition=" + F2(got.x) + "," + F2(got.y) + "," + F2(got.z)
             + " logical+0x48=" + F2(logical[0]) + "," + F2(logical[1]) + "," + F2(logical[2])
             + " anim+0x98=" + animPos
             + " agrees=" + agrees
             + " held+0x2F8=" + N2(*(int*)((char*)ch + 0x2F8))
             + " flag+0x3D4=" + N2((int)*(unsigned char*)((char*)ch + 0x3D4)));
}

// F353 - the DRIVE, extracted so the arrival-held path can use it WITHOUT falling through the
// machinery between the two.
//
// The first version of F350 simply stopped returning at the arrival branch, and an adversarial
// review found what that cost: under the deployed build `dist <= kArriveDist` returned
// unconditionally, so the P037 progress window, the catch-up placement and the LOST detector were
// UNREACHABLE on arrived frames. Letting them run changed the denominator of `movedThisWindow`,
// reset `stalledWindows` to zero whenever a window happened to expire on an arrived frame, and
// made `stalledWindows`/`catchupSnaps` incomparable between the `lead on` and `lead off` arms of
// the same run - which is exactly what the switch exists to allow.
//
// Extracting the drive keeps the reachability of every downstream measurement identical to the
// deployed build. `dist`/`dx`/`dz` are still the MEASUREMENT and are passed in for the diagnostic
// only; `aim*` is still only the command.
// D1 - the F305 desiredSpeed write, extracted verbatim from DriveTowardAim so the path drive writes the owner's
// level too (desiredSpeed caps the speed in every movement mode). `dist` is the catch-up measurement, as before.
// walk1: the signed lag along the owner's direction of travel from the copy to the aim point (positive = behind). With no
// usable owner direction (slower than 2 u/s, or not finite) it is the plain distance to the aim. *dirOk = a direction was used.
// review-walk1 4: the direction and the "moving" test are the owner's SMOOTHED heading and speed (p.ownHdgX/Z, p.ownSpd, kept
// across a rejected sample), not the raw velocity, which a rejected jump zeroes for a sample - that made an ahead copy
// read as behind again (F888).
static float SignedAimLag(const Puppet& p, const Ogre::Vector3& cur, float aimX, float aimZ, bool* dirOk)
{
    const float ax = aimX - cur.x, az = aimZ - cur.z;
    const float hn = sqrtf(p.ownHdgX * p.ownHdgX + p.ownHdgZ * p.ownHdgZ);
    *dirOk = p.ownPrimed && Finite(p.ownSpd) && p.ownSpd > 2.0f && Finite(hn) && hn > 0.001f;
    if (!*dirOk) { const float d = sqrtf(ax * ax + az * az); return Finite(d) ? d : 0.0f; }
    const float along = (ax * p.ownHdgX + az * p.ownHdgZ) / hn;
    return Finite(along) ? along : 0.0f;
}

static void WalkLagSample(const Puppet& p, float lag, bool dirOk)
{
    if (!g_walkLagInit) { std::memset(g_walkLagBucket, 0, sizeof(g_walkLagBucket)); g_walkLagInit = true; }
    if (!dirOk) return;   // only moving owners
    const int run = (Finite(p.authDesired) && p.authDesired >= kCatchupCap) ? 1 : 0;
    ++g_walkLagN[run];
    g_walkLagSum[run] += lag;
    int b = 6;
    if (lag >= 0.0f) b = lag < 1.0f ? 0 : lag < 5.0f ? 1 : lag < 10.0f ? 2 : lag < 20.0f ? 3 : lag < 40.0f ? 4 : 5;
    ++g_walkLagBucket[run][b];
    // T-196 (2026-09-29): runLag's class is desired >= kCatchupCap, which also takes JOGGING owners - and runboost2 does not
    // boost a jog (OwnerRuns, R4). [2] counts only the owners runboost treats as running, so the residual lag can be split.
    float rs = 0.0f, am = 0.0f;
    if (run == 1 && RunCeilingRead(p.ch, &rs, &am) && cooprunboost::OwnerRuns(p.authDesired, rs))
    {
        ++g_walkLagN[2];
        g_walkLagSum[2] += lag;
        ++g_walkLagBucket[2][b];
    }
}

// T-189 runboost (H051, 2026-09-28) - A COPY TRAILING A RUNNING OWNER MAY EXCEED ITS RUN SPEED, IN PROPORTION TO ITS LAG.
// Havok is given min(desiredSpeed +0xBC, speedCap +0xB4) (CharMovement::update 0x65F510, Read); +0xB4 is a plain copy of
// AnimationClass +0x19C each periodicUpdate (0x660330, Read), and +0x19C is what MedicalSystem (0x644840 / 0x645DD0, Read)
// sets through applySpeedCap 0x51BED0 to CharStats +0x17C, the run speed. A copy's run speed mirrors its owner's (F966), so a
// running copy keeps any gap it picked up until the owner stops. While the owner RUNS and the copy is more than 10 units
// BEHIND (signed, along the owner's travel) the copy's desiredSpeed is at least the boost and, when the boost exceeds its run
// speed, its ceiling is raised the engine's way (applySpeedCap(boost), then +0xB4) every drive frame:
//   boost = base x (1 + 0.5 x clamp(lag / 40, 0, 1)), base = min(the owner's command, the copy's run speed) (runboost2).
// The other readers of +0x19C - the vector clamp in CharMovement's vtable slot +0x70 (0x65D430) and update's own clamp at
// 0x65FAD6 - read the same field, so they rise with it; HavokCharacter::setDesiredSpeed 0x144070 has no cap (a finite test,
// then +0x7C = v x 0.1). Under 3 units, ahead, the owner stopped or not running, the lever off, a frame the drive did not renew
// it (RunBoostSettle), a drop, a handoff (UnpuppetForOwnership -> DropPuppet): applySpeedCap(the copy's own CharStats
// +0x17C) - exactly MedicalSystem's value - and +0xB4 likewise. Teardown forgets without writing (ReplicateWorldTeardown).
// runboost2 (T-195): a raise that ends in the drive (under 3, ahead, the owner no longer running or heading, a frame not
// renewed) EASES: RunBoostSettle steps the ceiling down each frame (cooprunboost::EaseCeiling, 200 u/s per s) - R1. At
// once: the lever off, a drop / handoff (DropPuppet), a copy the drive would not touch (unwritten, restoreSkipped), and a
// boost that already fits under the run speed (Havok's min(desired, ceiling) and the leg rate do not change). A failed
// restore keeps the ceiling ours and is retried each frame (R2). No boost for a jogging owner (OwnerRuns, R4).
bool g_runBoost = true;   // `runboost off` = the A/B arm (test-only lever); default ON
long long g_rbFrames = 0, g_rbEnters = 0, g_rbRestored = 0, g_rbRestoreFailed = 0, g_rbOverwritten = 0, g_rbRestoreSkipped = 0;
long long g_rbEaseFrames = 0, g_rbRetryOk = 0;   // runboost2: eased-ceiling frames written; restores that succeeded on a retry

// DrivePuppet's own "may anything be written through p.ch" questions, asked before a restore from outside the drive.
static bool RunBoostWritable(unsigned int uid, const Puppet& p)
{
    return PlausibleObj(p.ch) && !IsRetiredObject(p.ch) && coopuid::DriveDecision(FindSpawnedUid(p.ch), uid)
        && PlausibleObj(*(void**)((char*)p.ch + kAnimationClassOff));
}

// runboost2 (T-195 R2): put the engine's ceiling back NOW. A failed write keeps the ceiling ours (rbCeiling) with rbFailed
// set; RunBoostSettle retries it every frame until it succeeds (retryOk) or the copy can no longer be written.
static void RunBoostRestoreNow(Puppet& p)
{
    if (RunCeilingRestore(p.ch))
    {
        ++g_rbRestored;
        if (p.rbFailed) ++g_rbRetryOk;
        p.rbCeiling = false; p.rbFailed = false; p.rbWrote = 0.0f;
    }
    else { ++g_rbRestoreFailed; p.rbFailed = true; }
}

// End the raise AT ONCE (the lever off, a drop or handoff, a copy the drive would not touch). mayWrite false (the stale-uid
// drop, or a copy the drive itself would not touch): nothing is written, the ceiling is counted restoreSkipped and
// forgotten (MedicalSystem's next applySpeedCap puts the run speed back on a live one). A drop's failed restore is
// counted and not retried: the row is forgotten right after.
static void RunBoostEnd(Puppet& p, bool mayWrite)
{
    p.rbActive = false;
    if (!p.rbCeiling) return;
    if (mayWrite) { RunBoostRestoreNow(p); return; }
    ++g_rbRestoreSkipped;
    p.rbCeiling = false; p.rbFailed = false; p.rbWrote = 0.0f;
}

// runboost2 (T-195 R1): one frame of the EASED end - the ceiling steps down from our last write toward the copy's run speed
// at 200 u/s per s. applySpeedCap also clamps the leg rate +0x178 / +0x180 down to it (F310), so the legs slow over
// ~0.25 s, not in one frame. The last step, a +0x19C the engine rewrote since our write (its value stands: overwritten), an
// unreadable copy, a failed step, or a restore being retried: RunBoostRestoreNow.
static void RunBoostEaseStep(Puppet& p)
{
    const double now = NowSeconds();
    const float dt = p.rbEaseAt > 0.0 ? (float)(now - p.rbEaseAt) : (1.0f / 60.0f);
    p.rbEaseAt = now;
    float run = 0.0f, animMax = 0.0f, next = 0.0f;
    if (!p.rbFailed && RunCeilingRead(p.ch, &run, &animMax))
    {
        if (animMax != p.rbWrote) ++g_rbOverwritten;
        else next = cooprunboost::EaseCeiling(p.rbWrote, run, dt);
    }
    if (next > run && RunCeilingWrite(p.ch, next)) { p.rbWrote = next; ++g_rbEaseFrames; return; }
    RunBoostRestoreNow(p);
}

// From the drive loop, after DrivePuppet. A raise the drive did not renew THIS frame (arrival, a snap, prone, a ragdoll, the
// owner's level unknown, `drivespeed` other than -2, a skipped copy) ends; an ended raise whose ceiling is still ours takes
// one eased step (or its retried restore) - or, on a copy the drive would not touch, is forgotten unwritten (restoreSkipped).
static void RunBoostSettle(unsigned int uid, Puppet& p)
{
    if (p.rbActive && p.rbFrame != g_tick) p.rbActive = false;
    if (p.rbActive || !p.rbCeiling) return;
    if (!RunBoostWritable(uid, p)) { RunBoostEnd(p, false); return; }
    RunBoostEaseStep(p);
}

// ApplyDriveSpeed's step (the -2 arm, the owner's level known; DrivePuppet has validated p.ch this frame): the speed to
// command, 0 = no raise.
static float RunBoostFrame(Puppet& p, float lag, bool dirOk)
{
    float run = 0.0f, animMax = 0.0f;
    const bool readOk = RunCeilingRead(p.ch, &run, &animMax);
    const bool running = readOk && cooprunboost::OwnerRuns(p.authDesired, run);   // runboost2 R4: a jog is not a run
    // A follow copy AHEAD of its replayed target (lag = its error e < -1.5 u): the same ceiling is LOWERED to
    // run x followplay::PaceFactor(e) (down to 0.7x), renewed each frame like a raise; when it ends RunBoostSettle puts the
    // engine's ceiling back. The gait follows the current speed relative to the maximum (F419), so the run cycle is kept.
    if (p.follow && running && dirOk)
    {
        const float f = followplay::PaceFactor(lag);
        if (f < 1.0f)
        {
            const float pace = run * f;
            if (p.rbCeiling && animMax != p.rbWrote) ++g_rbOverwritten;
            if (RunCeilingWrite(p.ch, pace)) { p.rbCeiling = true; p.rbFailed = false; p.rbWrote = pace; }
            p.rbActive = true; p.rbFrame = g_tick; p.rbEaseAt = 0.0;
            p.fPace = f; ++g_followPaceFrames;
            return 0.0f;
        }
    }
    // A follow copy's lag is its error against the replayed target, which sits much closer than the old aim: the raise
    // starts 4 u behind and ends under 1 u (followplay::BoostDecide); every other copy keeps runboost.h's 10 / 3.
    const int step = p.follow ? followplay::BoostDecide(g_runBoost, p.rbActive, running, dirOk, lag, cooprunboost::kRbIdle,
                                                        cooprunboost::kRbEnter, cooprunboost::kRbKeep, cooprunboost::kRbRestore)
                              : cooprunboost::Decide(g_runBoost, p.rbActive, running, dirOk, lag);
    if (step == cooprunboost::kRbRestore) { p.rbActive = false; return 0.0f; }   // runboost2 R1: RunBoostSettle eases the ceiling
    if (step == cooprunboost::kRbIdle) return 0.0f;
    if (step == cooprunboost::kRbEnter) { p.rbActive = true; ++g_rbEnters; }
    p.rbFrame = g_tick;
    p.rbEaseAt = 0.0;
    ++g_rbFrames;
    const float boost = cooprunboost::BoostSpeed(cooprunboost::EffectiveSpeed(p.authDesired, run), lag);
    if (cooprunboost::NeedsCeiling(boost, run))
    {
        if (p.rbCeiling && animMax != p.rbWrote) ++g_rbOverwritten;   // +0x19C was rewritten (MedicalSystem) since our last write
        if (RunCeilingWrite(p.ch, boost)) { p.rbCeiling = true; p.rbFailed = false; p.rbWrote = boost; }
    }
    // runboost2: the command already fits under the run speed - Havok's min(desired, ceiling) and the leg rate do not change,
    // so the ceiling goes back at once (no hitch), retried by RunBoostSettle on failure.
    else if (p.rbCeiling) RunBoostRestoreNow(p);
    return boost;
}

static void ApplyDriveSpeed(Puppet& p, CharMovement* mv, float dist, const Ogre::Vector3& cur, float aimX, float aimZ)
{
    if (g_driveSpeed != 0.0f)
    {
        float requested = g_driveSpeed;
        if (g_driveSpeed == -2.0f)
        {
            // F419: the authority's commanded desiredSpeed IS the gait (WALK -> walkSpeed, RUN -> 999
            // clamped to max). Writing the same value gives the copy the same animation choice.
            if (Finite(p.authDesired) && p.authDesired > 0.0f)
            {
                requested = p.authDesired;
                bool dirOk = false;
                // A follow copy's lag is its error against the replayed target along the owner's route (FollowFrame), signed,
                // and usable only while the owner moves; every other copy keeps the lag to its guessed-ahead aim.
                float lag = 0.0f;
                if (p.follow) { lag = p.fE; dirOk = p.fEOk; p.fPace = 1.0f; }
                else lag = SignedAimLag(p, cur, aimX, aimZ, &dirOk);
                if (g_catchup2)
                {
                    if (p.authDesired >= kCatchupCap) ++g_speedGaitFrames;   // review-walk1 3: a jogger or runner gets no bonus (as H020's 45 cap gave none)
                    else if (lag > kC2Dead)
                    {
                        float ramp = (lag - kC2RampFrom) / (kC2RampTo - kC2RampFrom);
                        if (ramp < 0.0f) ramp = 0.0f; else if (ramp > 1.0f) ramp = 1.0f;
                        float cap = p.authDesired * (kC2MaxMul + (kCatchupMul - kC2MaxMul) * ramp);
                        if (cap > kCatchupCap) cap = kCatchupCap;
                        float boosted = p.authDesired + lag / kC2Tau;
                        if (boosted > cap) boosted = cap;
                        if (boosted > requested) { requested = boosted; ++g_c2Frames; }
                        else ++g_speedGaitFrames;
                    }
                    else { if (lag < -kC2Dead) ++g_c2AheadFrames; ++g_speedGaitFrames; }
                }
                else if (p.follow) ++g_speedGaitFrames;   // the unsigned H020 catch-up also sped up a copy that was AHEAD: not for a follow copy
                else {
                // H020 - bounded catch-up with hysteresis on `dist`: enter above kCatchupEnter, leave below kCatchupLeave.
                // `dist` is the caller's: the plain distance to the owner's LAST SAMPLE (not the aim point, as this
                // comment said until P074) - unsigned, so a copy AHEAD of its owner is sped up too (T269: 329,520 frames).
                if (!p.catchingUp && dist > kCatchupEnter) { p.catchingUp = true; ++g_speedCatchupEnters; }
                else if (p.catchingUp && dist < (p.pathMode ? kCatchupLeavePath : kCatchupLeave)) p.catchingUp = false;   // D2 item 4: 8 when path-driven
                if (p.catchingUp)
                {
                    float boosted = p.authDesired * kCatchupMul;
                    if (boosted > kCatchupCap) boosted = kCatchupCap;
                    if (boosted > requested) requested = boosted;
                    ++g_speedCatchupFrames;
                }
                else ++g_speedGaitFrames;
                }   // !g_catchup2
                // T-189 runboost (H051): a copy trailing a RUNNING owner may exceed its run speed (RunBoostFrame, above).
                const float rbWant = RunBoostFrame(p, lag, dirOk);
                if (rbWant > requested) requested = rbWant;
                // A follow copy ahead of its target that the run ceiling did not slow (a walker or a jogger): the same cut on
                // the desired speed.
                if (p.follow && dirOk && p.fPace >= 1.0f)
                {
                    const float f = followplay::PaceFactor(lag);
                    if (f < 1.0f) { requested *= f; p.fPace = f; ++g_followPaceFrames; }
                }
            }
            else { requested = kAuthSpeedFloor; ++g_speedAuthFloored; }
        }
        else if (g_driveSpeed == -3.0f)
        {
            // T107's mode: the 0.1-s finite-difference velocity. Noisy (30-52 for a 27 u/s walker) and
            // it crosses the run threshold; kept switchable for the record (F419).
            const float as = sqrtf(p.velX * p.velX + p.velZ * p.velZ);
            const bool stale = (NowSeconds() - p.lastMoveAt) > 1.0;
            if (!Finite(as) || stale || as < kAuthSpeedFloor) { requested = kAuthSpeedFloor; ++g_speedAuthFloored; }
            else requested = as;
        }
        if (WriteDesiredSpeed(mv, requested, p.speedSaved, &p.speedOriginal))
        {
            p.speedSaved = true;
            ++g_speedWrites;
            if (g_driveSpeed == -2.0f || g_driveSpeed == -3.0f) ++g_speedAuthWrites;
            p.authDesiredApplied = -2.0f;   // the stopped-level memory is stale once the drive has written
        }
        else ++g_speedRefused;
    }
}

void DriveTowardAim(unsigned int uid, Puppet& p, CharMovement* mv, const Ogre::Vector3& cur,
                    float dx, float dz, float dist, float aimX, float aimZ, float leadSec)
{
    // Unit direction toward the target, re-applied every frame (a direction is a
    // continuous command, not a one-shot order - the T011/T012 lesson).
    //
    // F350 - **TOWARD THE AIM POINT, NOT THE SAMPLE.** `dist` above is the measurement and stays
    // the measurement; these three are the command. They differ only while the authority is
    // moving, and when it is stationary `aimX/aimZ` ARE `p.tx/p.tz`, so a standing puppet behaves
    // exactly as it did before this change.
    //
    // It matters that `lim` is computed from the aim distance too: `steerDirectly`'s second
    // argument is a per-frame travel cap (F310), so computing it from a sub-unit gap to a stale
    // sample would cap the puppet at a fraction of a unit per frame in precisely the case this
    // change exists to make smooth.
    float dxA = aimX - cur.x;
    float dzA = aimZ - cur.z;
    float distA = sqrtf(dxA * dxA + dzA * dzA);

    // Degenerate only when we are standing exactly on the aim point. Under F062 that must still be
    // a real stop, not a fall-through into a division by zero.
    if (!(distA > 0.0001f))
    {
        if (p.driving)
        {
            mv->steerDirectly(Ogre::Vector3(0.0f, 0.0f, 0.0f), 0.0f);
            p.driving = false;
        }
        ++g_aimDegenerateFrames;
        return;
    }

    float inv = 1.0f / distA;

    // F342 - **THE FINITENESS CHECK RUNS BEFORE `setSteeringMode`, AND THAT ORDER IS THE WHOLE
    // POINT.** The first version of this gate sat below it, so a "refusal" armed direction-following
    // mode and then withheld only the vector - leaving the engine set to follow whatever
    // `desiredMotion` already held. Under F062 (ceasing to push does not stop a suppressed
    // character, it keeps walking on the last vector we gave it) that is the worst of the three
    // available behaviours, and the comment above it claimed a clean no-op. Refusing means
    // refusing: nothing is written, and `p.driving` is cleared so `pushing=` does not report a
    // drive that did not happen.
    float cx  = dxA * inv;
    float cz  = dzA * inv;
    float lim = distA * distA;
    if (!Finite(cx) || !Finite(cz) || !Finite(lim) || !Finite(inv))
    {
        ++g_driveNonFinite;

        // F343 - **AND STOP THE CHARACTER BEFORE CLEARING THE FLAG.** F342 cleared `p.driving` so
        // `pushing=` would not over-report, and thereby made it UNDER-report in the same cohort:
        // the arrival branch's explicit stop is gated on `if (p.driving)`, so a puppet refused for
        // one frame and arriving the next would never be stopped at all. Under F062 it keeps
        // walking on the vector we gave it last frame, coasts through its target, and oscillates
        // around it - while the report line asserts we are not driving it.
        //
        // Zeros are finite, so this does not violate the refusal it sits inside: what is being
        // refused is handing the engine a value that is not a number, not stopping the character.
        // This is the same stop the arrival path issues, for the same reason.
        if (p.driving)
        {
            mv->steerDirectly(Ogre::Vector3(0.0f, 0.0f, 0.0f), 0.0f);
            p.driving = false;
        }
        if (g_driveNonFinite <= 8)
            ErrorLog("[M2] DRIVE REFUSED uid=" + N2(uid) + " - would have written a NON-FINITE"
                     " vector into the engine (F340/F342)."
                     " target=" + F2(p.tx) + "," + F2(p.ty) + "," + F2(p.tz)
                     + " here=" + F2(cur.x) + "," + F2(cur.y) + "," + F2(cur.z)
                     + " dx=" + F2(dx) + " dz=" + F2(dz)
                     + " dist=" + F2(dist)
                     + " | aim=" + F2(aimX) + "," + F2(aimZ)
                     + " dxA=" + F2(dxA) + " dzA=" + F2(dzA) + " distA=" + F2(distA)
                     + " vel=" + F2(p.velX) + "," + F2(p.velZ) + " leadSec=" + F2(leadSec)
                     + " inv=" + F2(inv)
                     + " -> dir=" + F2(cx) + "," + F2(cz) + " limit=" + F2(lim)
                     + " | the target passed the ingest gate, so if this fires the corruption is"
                       " LOCAL - either the position read or the arithmetic here."
                       " (first 8 reported, then counted only)");
        return;
    }

    mv->setSteeringMode(STEER_BY_DIRECTION);

    // F310 - THE SECOND ARGUMENT IS A DISTANCE, AND IT IS SQUARE-ROOTED. It was `1.0f` here from
    // the day this was written, on the natural misreading that it is a speed scalar where 1.0
    // means "full". It is neither. `steerDirectly` stores `sqrtf(arg)` into `moveLimit`
    // (+0x398), and `CharMovement::update` uses it as a hard cap on how far the body may move
    // THIS FRAME:
    //
    //     if (moveLimit < dt * |velocity|)  timestep = moveLimit / |velocity|;
    //
    // So we were capping every puppet at **1.0 world unit per frame**. Both vanilla call sites
    // pass `Vector3::squaredDistance(target, here)`, which makes the cap the remaining distance -
    // its actual purpose is *do not overshoot the target*, not *go slowly*.
    //
    // At ~118 fps a walking character covers ~0.4 units per frame and the cap never bit, which is
    // why this was invisible. It bites exactly when the peer is under load - **at 20 fps a
    // character able to move 45 units/s was being held to 20** - and load is precisely the
    // condition a 147-puppet world creates. A cap that only engages when the machine is busy is
    // the kind of thing that reads as "sometimes it works".
    //
    // F340 - **AND DO NOT HAND THE ENGINE A NUMBER THAT IS NOT A NUMBER**, checked above so the
    // refusal is a true no-op. `steerDirectly` has no guard of any kind (F304, read instruction
    // by instruction): it stores whatever it is given into `desiredMotion` and `moveLimit`, which
    // are levels the engine re-reads every tick and never clears.
    //
    // `dist * dist` because the callee takes the square root. Vanilla's semantic exactly.
    mv->steerDirectly(Ogre::Vector3(cx, 0.0f, cz), lim);
    p.driving    = true;
    p.everDriven = true;   // F342 - latched for the puppet's whole life, never cleared

    // F305 - THE MAGNITUDE. `desiredMotion` is a UNIT direction (vanilla normalises it too), so
    // nothing above carries any speed at all. `CharMovement::update` supplies the magnitude from
    // a field this drive has never touched: every tick, before the mode dispatch, it calls
    // `HavokCharacter::setDesiredSpeed(min(desiredSpeed, speedCap))`. Vanilla's own
    // STEER_BY_DIRECTION caller (0x349E10) sets `desiredSpeed` on the line after storing the
    // direction. We inherited whatever the character's AI last left there.
    //
    // DEFAULT IS -2 since AUD 2026-09-17: a player gets the authority's streamed speed without
    // asking for it. The untouched-baseline arm is now an explicit one - `-DriveSpeed 0`
    // (`drivespeed 0`) - which restores the engine's own value in this field, so the measurement
    // that identifies the cause is still available, it just has to be asked for.
    //   drivespeed 0    - do not touch it (measure the engine's own value; the baseline arm)
    //   drivespeed -1   - use the character's OWN speedCap (+0xB4), its top speed, not a constant
    //   drivespeed -2   - the AUTHORITY's streamed speed (default; F418): the host's walkers move at <25 u/s,
    //                     their copies were commanded 45 and moved in bursts - a run animation at a
    //                     walking pace (F415). The intent is the host's speed; replicate the intent.
    //   drivespeed <n>  - force n
    ApplyDriveSpeed(p, mv, dist, cur, aimX, aimZ);   // D1: extracted so path mode writes the same level (walk1: + the aim)

    // F184: `driving` means WE PUSHED, not that the character moved. There is deliberately no
    // prone check above - we push regardless - and T058 caught the consequence: a puppet that
    // was prone and unconscious held a frozen 8.0-unit offset across 64 s while reporting
    // `driving=yes`. A downed character cannot walk, so the push goes nowhere and the gap
    // never closes. The report line now prints the character's PoseState beside it so the
    // two can never again be read as the same thing.
    p.pronedAt = *(int*)((char*)p.ch + kProneStateOff);
}

// InjectOrder is declared by replicate.h (coop scope) and defined below (M-C).

// H022 - the engine's own setter for the gait the Move task will use. SEH like every other engine write.
static bool SafeSetSpeedOrders(CharMovement* mv, int runSpeed)
{
    __try { mv->orderMoveSpeed((SpeedOrder)runSpeed); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// D1 (read-movement) - the path drive. CharMovement::setPathGoal(const Vector3&, PathPriority, bool) resolves to
// 0x6607E0 (resolve_stub.py, Steam_1.0.65.br slot 1989). PathPriority PATH_PRIORITY_MEDIUM (1): every engine call site that hands a
// Vector3 to this virtual (slot +0x90 on Character+0x640 - 0x3338D1, 0x33E3CB, 0x340640, 0x342C9C, 0x35433E) passes 1
// (the callee itself raises it to 2 on one internal condition). The bool is `false` at every
// one of those sites, and 0x6607E0 never reads r9 before overwriting it - the override ignores it.
static bool SafeSetDestination(CharMovement* mv, const Ogre::Vector3& dest)
{
    __try { mv->setPathGoal(dest, PATH_PRIORITY_MEDIUM, false); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// CharMovement::lastPathFailed (0x65DDA0), a const getter. 1 = failed, 0 = not, -1 = the read faulted.
static int SafePathFailed(CharMovement* mv)
{
    __try { return mv->lastPathFailed() ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static int SafeReadMoveMode(CharMovement* mv)
{
    __try { return *(int*)((char*)mv + kPathMoveModeOff); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// D3 (read-pathend) - the path agent's request status: CharMovement+0x320 -> the agent, agent+0x90 = 4/5 waiting for the
// NavMeshMain result, 1 landed (0x144C90) / arrived (0x147F90). -1 = the read faulted or there is no agent; crash-guarded
// like SafePathFailed. Read-only.
const size_t kPathAgentOff       = 0x320;   // CharMovement -> its path agent
const size_t kPathAgentStatusOff = 0x90;    // agent -> request status (int)
static int SafePathRequestStatus(CharMovement* mv)
{
    __try
    {
        char* agent = *(char**)((char*)mv + kPathAgentOff);
        if (agent == 0) return -1;
        return *(int*)(agent + kPathAgentStatusOff);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// Cancel a path request and stand still: STEER_BY_DIRECTION cancels path mode, a zero vector stops it (F062).
static bool SafeLeavePath(CharMovement* mv)
{
    __try { mv->setSteeringMode(STEER_BY_DIRECTION); mv->steerDirectly(Ogre::Vector3(0.0f, 0.0f, 0.0f), 0.0f); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void PathLog(const std::string& s)
{
    if (g_pathLogLines >= 16) return;
    ++g_pathLogLines;
    ErrorLog("[D1] " + s + (g_pathLogLines == 16 ? std::string(" (16th path line - counted only from here)") : std::string()));
}

// D1 item 3: forget the last destination so the next path frame re-issues (after any snap, and on leaving path mode).
static void PathForgetDest(Puppet& p)
{
    p.pathHaveDest = false;
    p.pathLastAt = 0.0;
    p.pathProgArmed = false;   // D1-b item 1: the progress window reopens at the next issue
    p.pathTurnSince = 0.0; p.pathRetryAim = 0;   // D2-b items 4/5
    if (p.pathAwait) { p.pathAwait = false; ++g_pathAwaitAbandoned; }
}

// D1-b items 1/3: a path is pending while it waits for the budget, or is issued and not yet seen moving, for at most
// kPathPendingSec - past that a copy that has not moved is judged like any other.
static bool PathPending(const Puppet& p, double now)
{
    if (p.pathWantIssue && now - p.pathWantSince < kPathPendingSec) return true;
    return p.pathAwait && p.pathHaveDest && now - p.pathIssueAt < kPathPendingSec;
}

static void PathLeave(unsigned int uid, Puppet& p, CharMovement* mv, float distA, const char* why, bool stop)
{
    p.pathMode = false;
    PathForgetDest(p);
    p.pathWantIssue = false; p.pathGrant = false;   // D1-b item 2: out of the budget queue
    p.pathWantPriority = false;
    ++g_pathToPush;
    if (stop && !SafeLeavePath(mv)) ++g_pathLeaveFaulted;
    PathLog("pathToPush uid=" + N2(uid) + " aimDist=" + F2(distA) + " (" + why + ")" + (stop ? " - stopped" : ""));
}

// Not prone, ragdolled, carried (+0x3D4) or in a bed/cage/slot (+0x2F8). An unreadable character is not eligible.
static bool PathEligible(Puppet& p, int prone)
{
    if (!coopgetup::MoveDrivesProne(prone, SafeIsRagdoll(p.ch))) return false;   // T-178 crawl1: prone 2, not ragdolled, paths
    if (SafeIsRagdoll(p.ch)) return false;
    CharDbg cd;
    if (!ReadCharDbgFields(p.ch, &cd)) return false;
    return cd.carried == 0 && cd.inSomething == 0;
}

// D1-b item 2 - once per frame, before the copies are driven: grant the budget (up to kPathBudgetPerFrame) to the waiting
// copies in followplay::PathServeRank order - a player's copy's stop issue, its other issues, an NPC copy's stop issue, its
// other issues - oldest waiting first within each; what is left serves copies whose issue fell due this frame, in map order
// (followplay::PathTakesLeftover: not an NPC follow copy, which queues). A crowd of walking NPC copies never holds back a
// player's copy.
static void PathBudgetFrame()
{
    static std::vector<std::pair<std::pair<int, double>, unsigned int> > waiting;
    waiting.clear();
    for (std::map<unsigned int, Puppet>::iterator it = g_puppets.begin(); it != g_puppets.end(); ++it)
    {
        Puppet& q = it->second;
        q.pathGrant = false;
        if (q.pathMode && q.pathWantIssue)
            waiting.push_back(std::make_pair(std::make_pair(followplay::PathServeRank(q.follow && q.followPlayer, q.pathWantPriority), q.pathWantSince), it->first));
    }
    std::sort(waiting.begin(), waiting.end());   // by rank, then oldest waiting first
    int grants = 0;
    for (size_t i = 0; i < waiting.size() && grants < kPathBudgetPerFrame; ++i)
    {
        std::map<unsigned int, Puppet>::iterator it = g_puppets.find(waiting[i].second);
        if (it != g_puppets.end()) { it->second.pathGrant = true; ++grants; }
    }
    g_pathBudgetLeft = kPathBudgetPerFrame - grants;
}

// D2-b item 3: the owner is 'still' only once its smoothed speed has stayed below kPathOwnerStillSpeed kPathOwnerStillConfirmSec
// (p.pathOwnerStillSince is kept by the caller) - no single-sample false stop.
static bool OwnerMovingConfirmed(const Puppet& p, float ownerSpeed, double now)
{
    if (ownerSpeed >= kPathOwnerStillSpeed) return true;
    return !(p.pathOwnerStillSince > 0.0 && now - p.pathOwnerStillSince >= kPathOwnerStillConfirmSec);
}

// Returns true when the path drive handled this frame; false = push as before.

static bool PathDriveStep(unsigned int uid, Puppet& p, CharMovement* mv, const Ogre::Vector3& cur,
                          float dist, float aimX, float aimZ, int prone, float ownerSpeed)
{
    const float adx = aimX - cur.x, adz = aimZ - cur.z;
    const float distA = sqrtf(adx * adx + adz * adz);
    if (!Finite(distA))
    {
        if (p.pathMode) PathLeave(uid, p, mv, distA, "non-finite aim distance", false);
        return false;   // the push's own F342 refusal handles it
    }
    const bool byDist = distA > kPathEnter;
    const double now = NowSeconds();
    // D1-c item 1 - while the owner moves, a copy in path mode STAYS in path mode however close it gets; it goes back to
    // the push only once the owner has been stationary kPathOwnerStillSec AND the copy is within kPathLeave. Entry as before.
    const bool ownerMoving = OwnerMovingConfirmed(p, ownerSpeed, now);   // D2-b item 3: smoothed, still after 0.3 s
    const bool ownerStopped = !ownerMoving && p.pathOwnerStillSince > 0.0 && now - p.pathOwnerStillSince >= kPathOwnerStillSec;
    bool want = p.pathMode ? (distA >= kPathLeave || !ownerStopped) : (byDist || (p.stalledWindows >= 1 && distA >= kPathLeave));
    if (want && !p.pathMode && now < p.pathHoldUntil) want = false;   // D1-b item 1: held on the push after a path without progress
    bool ineligible = false;
    if (want && !PathEligible(p, prone)) { want = false; ineligible = true; ++g_pathIneligibleFrames; }
    if (!want)
    {
        if (p.pathMode)
        {
            if (!ineligible) ++g_pathLeaveOwnerStopped;
            PathLeave(uid, p, mv, distA, ineligible ? "not eligible" : "within kPathLeave, owner stopped >= 0.5 s", false);
        }
        return false;
    }

    if (!p.pathMode)
    {
        p.pathMode = true;
        ++g_pushToPath;
        if (!byDist) ++g_pathEnterByStall;
        p.stalledWindows = 0;   // D1-b item 4: by distance too - the path gets full windows before the stuck snap
        p.driving = false;   // the push is cancelled by the path request (+0x378 = 0)
        PathForgetDest(p);
        PathLog("pushToPath uid=" + N2(uid) + " aimDist=" + F2(distA) + (byDist ? " (far)" : " (stalled one window)"));
    }
    ++g_pathFrames;

    // D1-b item 1 - a path that produces no progress must not freeze a copy. The window opens at the stint's first issue
    // and is judged only once no path is pending (PathPending). The aim distance shrunk by less than kPathProgressMin
    // -> back to the push (which cancels the path this same frame), held there kPathHoldSec before a path again.
    // D1-c item 3 - judged against the owner's motion: a copy keeping pace with a moving owner (distance roughly constant)
    // is progress. Fall back only if the distance GREW by more than kPathGrowMax, or did not shrink kPathProgressMin in a
    // window throughout which the owner was stationary.
    if (p.pathProgArmed && ownerMoving) p.pathProgOwnerMoved = true;
    if (p.pathProgArmed && g_tick - p.pathProgTick >= kProgressWindowTicks && !PathPending(p, now))
    {
        const float shrunk = p.pathProgFromDistA - distA;
        const bool grew = -shrunk > kPathGrowMax;
        // D1-d (recheck-d1c Q2): a path the engine silently dropped leaves the copy STANDING while its owner mills about
        // inside the re-issue circle - neither rule above fires then. A copy that itself moved < 0.5 in a whole window,
        // with no path pending and its aim beyond kPathLeave, is on a dead path: the same fallback.
        const float cmx = cur.x - p.pathProgFromX, cmz = cur.z - p.pathProgFromZ;
        const bool deadPath = (cmx * cmx + cmz * cmz) < 0.25f && distA > kPathLeave;
        if (deadPath) ++g_pathDeadFallback;
        if (grew || deadPath || (!p.pathProgOwnerMoved && shrunk < kPathProgressMin))
        {
            ++g_pathNoProgressFallback;
            p.pathHoldUntil = now + kPathHoldSec;
            PathLeave(uid, p, mv, distA, grew ? "aim distance grew > 4 in a window - the push for 5 s"
                                              : "no progress in a window, owner stationary - the push for 5 s", false);
            return false;
        }
        p.pathProgTick = g_tick; p.pathProgFromDistA = distA; p.pathProgOwnerMoved = ownerMoving; p.pathProgFromX = cur.x; p.pathProgFromZ = cur.z;
    }

    // D3 (read-pathend): the near-end trigger is in TIME on the copy's own speed, the lead covers the landing delay, a too-close
// destination extends forward while the owner moves, and the next re-plan waits for the last path to LAND, not a timer.
// D2 (read-stopgo, user T261) - ONE pursuit rule while the owner moves, replacing the D1-b/D1-c moving re-issue (aim moved
    // >= max(kPathReissueDist, ownerSpeed x 0.5)) and the D1-c waited-moving hold. The destination is ~kPathLeadSec ahead of
    // the owner on its heading, re-planned BEFORE the copy arrives (the engine stops a copy at its own path end): near its
    // end, the owner turned > 30 deg, or the aim left the destination line by > kPathOffLineMax. On the owner's moving ->
    // still edge: one issue to the exact aim, then the stationary D1-b rule (aim moved >= kPathReissueDist). Kept: the
    // kPathIssueFloorSec floor per copy and the kPathBudgetPerFrame budget (oldest waiting first).
    // D2-b (review-d2 items 1/3/5): the heading and the speed are the owner's SMOOTHED ones (p.ownHdgX/Z, ownerSpeed).
    float dirX = 0.0f, dirZ = 0.0f;
    const float hn = sqrtf(p.ownHdgX * p.ownHdgX + p.ownHdgZ * p.ownHdgZ);
    const bool haveHdg = Finite(hn) && hn > 0.001f;
    if (ownerMoving && haveHdg) { dirX = p.ownHdgX / hn; dirZ = p.ownHdgZ / hn; }
    const bool lead = dirX != 0.0f || dirZ != 0.0f;
    if (!lead && p.pathOwnerWasMoving && p.pathHaveDest) p.pathStopDue = true;   // D2 item 3: the owner's moving -> still edge
    if (lead) p.pathStopDue = false;
    p.pathOwnerWasMoving = lead;
    // D2-b item 2 (Q1 overshoot): a copy still walking to a LEAD destination while AHEAD of its aim along the owner's smoothed
    // heading - the owner still, or turning back (its latest sample against the smoothed heading) - leaves the path and stops.
    // D3 fold M2: the exact-aim (stop) issue clears pathDestLead, but the old lead path - up to 1.6 x copy speed + 2 ahead -
    // is still walked until the new one LANDS, so ahead-stop covers it until then (a faulted status read: kPathPendingSec).
    const bool leadWalked = p.pathLeadWalking && !(p.pathLandedFaultNoted && now - p.pathIssueAt >= kPathPendingSec);
    if (p.pathHaveDest && (p.pathDestLead || leadWalked) && haveHdg)
    {
        bool turningBack = false;
        if (ownerMoving && ownerSpeed > 0.0f)
        {
            const float rs = sqrtf(p.velX * p.velX + p.velZ * p.velZ);
            if (Finite(rs) && rs >= kPathOwnerStillSpeed && (p.velX * p.ownHdgX + p.velZ * p.ownHdgZ) < 0.0f) turningBack = true;
        }
        const float ahead = ((cur.x - aimX) * p.ownHdgX + (cur.z - aimZ) * p.ownHdgZ) / hn;
        if ((!ownerMoving || turningBack) && ahead > kPathAheadStopDist)
        {
            ++g_pathAheadStopped;
            PathLeave(uid, p, mv, distA, turningBack ? "ahead of its aim, owner turning back - stopped"
                                                     : "ahead of its aim, owner still - stopped", true);
            p.driving = false;
            return true;
        }
    }
    // D3: the copy's own measured speed, taken BEFORE the re-plan decision (D2 took it after) - the near-end trigger, the
    // lead and a too-close extension are judged on it. -1 = no sample this frame (first path frame, or a > 0.25 s gap).
    float spd = -1.0f;
    if (p.pathLastAt > 0.0)
    {
        const double dt = now - p.pathLastAt;
        if (dt > 0.0001 && dt < 0.25)
        {
            const float mx = cur.x - p.pathLastX, mz = cur.z - p.pathLastZ;
            spd = (float)(sqrtf(mx * mx + mz * mz) / dt);
        }
    }
    p.pathLastX = cur.x; p.pathLastZ = cur.z; p.pathLastAt = now;
    const float copySpeed = cooppath::CopySpeed(spd);   // fold LOW 1: at most cooppath::kCopySpeedMax (120 u/s)
    const bool retryAim = lead && p.pathRetryAim == 1;   // D2-b item 4: a failed lead path is retried once at the exact aim
    const float la = cooppath::LeadDist(ownerSpeed, copySpeed);   // D3: clamp(max(owner, copy) x 1.6 s + 2, 8, 90)
    float destX = aimX, destZ = aimZ;
    if (lead && !retryAim) { destX = aimX + dirX * la; destZ = aimZ + dirZ * la; }
    if (lead && p.pathHaveDest && !p.pathArrivedNoted)   // the stop-go this rule removes, once per destination
    {
        const float ax = p.pathDestX - cur.x, az = p.pathDestZ - cur.z;
        if (ax * ax + az * az < kPathArrivedDist * kPathArrivedDist) { p.pathArrivedNoted = true; ++g_pathArrivedWhileOwnerMoving; }
    }
    bool reissue = !p.pathHaveDest;
    int why = 0;   // 1 near end, 2 turn, 3 off line, 4 the owner stopped, 5 exact-aim retry of a failed lead path
    if (!lead) p.pathTurnSince = 0.0;
    if (!reissue && retryAim) { reissue = true; why = 5; }
    else if (!reissue && lead)
    {
        if (!p.pathDestLead && p.pathRetryAim != 2) reissue = true;   // the owner started moving: the last destination was an exact aim
        else
        {
            const float ex = p.pathDestX - cur.x, ez = p.pathDestZ - cur.z;
            const float nearEnd = cooppath::NearEndDist(copySpeed, la);   // D3: max(3, copy speed x 1.1 s), below 0.9 x the lead
            const float ox = aimX - p.pathLineX, oz = aimZ - p.pathLineZ;
            const float off = ox * p.pathDestDirZ - oz * p.pathDestDirX;   // the aim's signed distance from the destination line
            // D2-b item 5: the turn is judged on the smoothed heading and must persist kPathTurnPersistSec.
            const bool turned = dirX * p.pathDestDirX + dirZ * p.pathDestDirZ < kPathTurnCos;
            if (!turned) p.pathTurnSince = 0.0;
            else if (p.pathTurnSince <= 0.0) p.pathTurnSince = now;
            if (ex * ex + ez * ez < nearEnd * nearEnd) { reissue = true; why = 1; }
            else if (turned && now - p.pathTurnSince >= kPathTurnPersistSec) { reissue = true; why = 2; }
            else if (off > kPathOffLineMax || off < -kPathOffLineMax) { reissue = true; why = 3; }
        }
    }
    else if (!reissue && p.pathStopDue) { reissue = true; why = 4; }
    else if (!reissue)
    {
        const float ddx = aimX - p.pathDestX, ddz = aimZ - p.pathDestZ;   // owner still: the D1-b rule, unchanged
        reissue = (ddx * ddx + ddz * ddz) >= kPathReissueDist * kPathReissueDist;
    }
    // D3 (c): the engine drops a destination < 2 u from the last (0x6607E0). While the owner moves (a lead issue) a too-close
    // destination is pushed FORWARD along the owner's heading by max(2.5, copy speed x 0.5 s) instead of refused; the owner
    // still (and the exact-aim retry) keep the refusal. Refusals count once per destination, not per frame.
    bool extended = false;
    if (reissue && p.pathHaveDest)
    {
        const float sx = destX - p.pathDestX, sz = destZ - p.pathDestZ;
        if (sx * sx + sz * sz < kPathDestMinSep * kPathDestMinSep)
        {
            float nx = destX, nz = destZ;
            if (lead && !retryAim && cooppath::ExtendTooClose(destX, destZ, p.pathDestX, p.pathDestZ, dirX, dirZ, cur.x, cur.z,
                                                                copySpeed, la, kPathDestMinSep, kPathArrivedDist, &nx, &nz))
            {
                destX = nx; destZ = nz; extended = true;
            }
            else
            {
                reissue = false;
                if (!p.pathTooCloseNoted) { p.pathTooCloseNoted = true; ++g_pathReplanTooClose; }
                if (why == 4) p.pathStopDue = false;   // the path already ends at the exact aim
            }
        }
    }
    // D3 (d) - events over timers: the next re-plan may go out as soon as the previous path LANDED (the agent's request
    // status read non-1 since the issue, then 1). The kPathIssueFloorSec floor stays only as the fallback - the read faulted,
    // or never showed the request pending - and the kPathBudgetPerFrame budget still applies.
    // Fold M1: never two issues closer than cooppath::kMinIssueGapSec (0.2 s), landed or not - a copy ahead of its aim keeps
    // 'near end' true and would otherwise re-plan on every landing (0.08 s) against the 8/frame budget.
    if (p.pathHaveDest && !p.pathLanded && !p.pathLandedFaultNoted)
    {
        const int ls = cooppath::LandedStep(SafePathRequestStatus(mv), &p.pathSeenPending);
        if (ls > 0) { p.pathLanded = true; p.pathLeadWalking = false; }   // fold M2: the exact-aim path is now the one walked
        else if (ls < 0) { p.pathLandedFaultNoted = true; ++g_pathLandedReadFaulted; }
    }
    // D2-b item 2: the owner-stopped exact-aim issue bypasses the floor (once - it settles on issue) and takes budget priority.
    const bool inFloor = (now - p.pathIssueAt) < kPathIssueFloorSec;
    if (reissue && why != 4 && cooppath::IssueGateBlocks(now - p.pathIssueAt, p.pathHaveDest && p.pathLanded, kPathIssueFloorSec)) reissue = false;   // a first issue keeps the floor
    bool may = false;
    if (reissue)
    {
        may = p.pathGrant;
        if (!may && g_pathBudgetLeft > 0) { --g_pathBudgetLeft; may = true; }
        if (!may)
        {
            if (!p.pathWantIssue) { p.pathWantIssue = true; p.pathWantSince = now; }
            if (why == 4) p.pathWantPriority = true;
            ++g_pathBudgetDeferred;
        }
    }
    else { p.pathWantIssue = false; p.pathWantPriority = false; }
    p.pathGrant = false;
    if (may)
    {
        p.pathWantIssue = false;
        if (!SafeSetDestination(mv, Ogre::Vector3(destX, p.ty, destZ)))
        {
            ++g_pathIssueFailed;
            PathLeave(uid, p, mv, distA, "setDestination faulted", false);
            return false;
        }
        ++g_pathIssued;
        if (why == 1) ++g_pathReplanNearEnd; else if (why == 2) ++g_pathReplanTurn; else if (why == 3) ++g_pathReplanOffLine; else if (why == 4) ++g_pathStopIssue;
        if (why == 4 && inFloor) ++g_pathStopIssueBypassFloor;
        if (why == 5) ++g_pathFailedRetryAim;
        if (extended) ++g_pathReplanExtended;
        if (inFloor && why != 4) ++g_pathReplanOnLanded;   // only a landed path lets a non-stop issue through the floor
        p.pathWantPriority = false; p.pathTurnSince = 0.0;
        p.pathRetryAim = (why == 5) ? 2 : 0;
        if (!lead) p.pathStopDue = false;   // any exact-aim issue settles the one owed
        p.pathLeadWalking = cooppath::OldLeadStillWalked(p.pathHaveDest, p.pathDestLead, p.pathLeadWalking, p.pathLanded, lead && !retryAim);   // fold M2
        p.pathHaveDest = true; p.pathDestX = destX; p.pathDestZ = destZ; p.pathIssueAt = now; p.pathFailNoted = false; p.pathIssueTick = g_tick; p.pathFailSeenClear = false;
        p.pathDestLead = lead && !retryAim; p.pathDestDirX = dirX; p.pathDestDirZ = dirZ; p.pathLineX = aimX; p.pathLineZ = aimZ; p.pathArrivedNoted = false;
        p.pathSeenPending = false; p.pathLanded = false; p.pathLandedFaultNoted = false; p.pathTooCloseNoted = false;   // D3: per destination
        if (!p.pathAwait) { p.pathAwait = true; p.pathAwaitTick = g_tick; }   // a re-issue does not restart the timer
        if (!p.pathProgArmed) { p.pathProgArmed = true; p.pathProgTick = g_tick; p.pathProgFromDistA = distA; p.pathProgOwnerMoved = ownerMoving; p.pathProgFromX = cur.x; p.pathProgFromZ = cur.z; }
    }
    else if (!reissue) ++g_pathDeduped;

    if (p.pathHaveDest && !p.pathFailNoted)
    {
        const int f = SafePathFailed(mv);
        // D2-c (recheck-d2b): the flag can still be the PREVIOUS path's in the frame a path is issued - a stale 1 cancelled
        // the exact-aim retry at once. A 1 counts only once the flag has read 0 since the issue, or kPathFailTrustTicks later.
        const bool failTrusted = p.pathFailSeenClear || (g_tick - p.pathIssueTick) >= kPathFailTrustTicks;
        if (f == 1 && failTrusted)
        {
            p.pathFailNoted = true; ++g_pathFailed;
            PathLog("pathFailed uid=" + N2(uid) + " aimDist=" + F2(distA) + " dest=" + F2(p.pathDestX) + "," + F2(p.pathDestZ));
            // D2-b item 4: a failed LEAD path while the owner moves is retried once at the exact aim (after the floor);
            // the retry failing too falls back to the push, held kPathHoldSec (the D1-b hold).
            if (p.pathRetryAim == 2)
            {
                p.pathHoldUntil = now + kPathHoldSec;
                PathLeave(uid, p, mv, distA, "the exact-aim retry failed too - the push for 5 s", false);
                return false;
            }
            if (lead && p.pathDestLead) p.pathRetryAim = 1;
        }
        else if (f < 0) { p.pathFailNoted = true; ++g_pathFailedReadFaulted; }
        else if (f == 0) p.pathFailSeenClear = true;
    }

    // Issue -> moving: path mode (+0x378 == 0) and the copy's own measured speed above kPathMovingSpeed (spd, measured above).
    if (p.pathAwait && Finite(spd) && spd > kPathMovingSpeed && SafeReadMoveMode(mv) == 0)
    {
        const long long fr = g_tick - p.pathAwaitTick;
        p.pathAwait = false;
        if (fr <= 2) ++g_pathMoveLe2; else if (fr <= 10) ++g_pathMoveLe10; else if (fr <= 60) ++g_pathMoveLe60; else ++g_pathMoveGt60;
        if (fr > g_pathMoveMaxFrames) g_pathMoveMaxFrames = fr;
        PathLog("moving uid=" + N2(uid) + " " + N2(fr) + " frames after the issue, speed " + F2(spd) + " aimDist=" + F2(distA));
    }

    ApplyDriveSpeed(p, mv, dist, cur, aimX, aimZ);   // desiredSpeed caps the speed in path mode too (F305)
    return true;
}


// The engine's update mode for this character: Character +0xE4 isVisibleUpdateMode. Non-zero = moved every frame; 0 = moved
// only in GameWorld::charsUpdate's turns (0x7862F0 fully updates at most 6 such characters a frame), when the time saved up
// since its last turn moves it in one step. 1 / 0, -1 = unreadable.
static int SafeVisibleUpdateMode(::Character* c)
{
    __try { return *(const unsigned char*)((const char*)c + 0xE4) != 0 ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

const int kFollowNoSlot = -2;   // fOwnerSlot before the first report (-1 is OwnerSlotOf's "no owner slot known")

// Everything the follow drive keeps for one copy, back to nothing (registration, adoption, a change of faction or of owner).
static void FollowReset(Puppet& p)
{
    followplay::TrackClear(&p.trk); followplay::ClockReset(&p.clk);
    p.fClkOff = 0.0; p.followD = followplay::PlaybackDelay(0.0); p.fLastMovingAt = 0.0; p.fOwnerSlot = kFollowNoSlot;
    p.stopHeld = false; p.stopX = p.stopY = p.stopZ = 0.0f; p.stopT = 0.0; p.stopIssueOwed = false;
    p.goalHave = false; p.goalX = p.goalZ = 0.0f; p.goalSpent = false; p.goalSpentX = p.goalSpentZ = 0.0f;
    p.fTPlay = 0.0; p.fMode = followplay::kModeEmpty; p.fTX = p.fTZ = 0.0f; p.fE = 0.0f; p.fEOk = false; p.fMoving = false;
    p.fHaveN = false; p.fNX = p.fNZ = 0.0f; p.fHaveH = false; p.fHX = p.fHZ = p.fV = 0.0f; p.fVum = -1;
    p.fDestKind = followplay::kDestNone; p.fHeld = false; p.fPace = 1.0f; p.fIssuedKind = followplay::kDestNone;
    p.fCopyLX = p.fCopyLZ = 0.0f; p.fCopyLAt = 0.0; p.fCopyAlongV = 0.0f;
    followplay::StopProgressReset(&p.fStopProg); p.fStopStalled = false; p.fStopArrive = followplay::kStopArriveDist;
    p.fBehind = false;
}

// The owner's clock for uid: its owner game's (by slot), or this copy's own while no owner slot is known.
static followplay::ClockOff* FollowClockOf(unsigned int uid, Puppet& p)
{
    const int slot = net::OwnerSlotOf(uid);
    if (slot < 0) return &p.clk;
    std::map<int, followplay::ClockOff>::iterator c = g_followClk.find(slot);
    if (c == g_followClk.end())
    {
        followplay::ClockOff z; followplay::ClockReset(&z);
        c = g_followClk.insert(std::make_pair(slot, z)).first;
    }
    return &c->second;
}

// THE FOLLOW REPLAY, once per drive frame of a follow copy that is not idle (DrivePuppet, in place of the guessed-ahead
// aim; FollowIdleFrame for an idle one): the target T =
// the owner's recording at owner time (now - offset - D); N, h and the owner's reported speed; the error e = (T - copy) . h
// while the owner moves (ownerMovingNow: followplay::OwnerMovingNow, the newest report moving and not expired); the copy's
// update mode. The aim becomes T kept short of N along h (the push and the arrival use only points the owner reported), or
// the stated stop point. No recording yet: the aim is left as it was.
// BEHIND SCHEDULE (followplay::BehindScheduleStep: no stop stated, T further from the owner's newest reported point (p.tx)
// than the delay allows - reports that never reached the recording, or a recording that stopped moving): the aim is that
// point and the catch-up's lag e is the copy's distance to it, so the copy closes the gap instead of sitting on a target its
// owner left behind; T itself stays the recording's (the probes print it).
static void FollowFrame(Puppet& p, const Ogre::Vector3& cur, bool ownerMovingNow, float* aimX, float* aimZ)
{
    const double now = NowSeconds();
    ++g_followReplayFrames;
    p.fVum = SafeVisibleUpdateMode(p.ch);
    p.fHaveH = followplay::RouteHeading(p.trk, &p.fHX, &p.fHZ, &p.fV);
    if (!p.fHaveH) { p.fHX = p.fHZ = p.fV = 0.0f; }
    p.fHaveN = p.trk.n > 0;
    if (p.fHaveN) { const followplay::Sample& nw = followplay::TrackNewest(p.trk); p.fNX = nw.x; p.fNZ = nw.z; }
    p.fMoving = !p.stopHeld && ownerMovingNow;
    // the copy's own speed along h since the previous drive frame (0 when there is none, or the gap is over 0.25 s)
    p.fCopyAlongV = 0.0f;
    if (p.fHaveH && p.fCopyLAt > 0.0 && now - p.fCopyLAt > 0.0001 && now - p.fCopyLAt < 0.25)
    {
        const float v = ((cur.x - p.fCopyLX) * p.fHX + (cur.z - p.fCopyLZ) * p.fHZ) / (float)(now - p.fCopyLAt);
        if (Finite(v)) p.fCopyAlongV = v;
    }
    p.fCopyLX = cur.x; p.fCopyLZ = cur.z; p.fCopyLAt = now;
    p.fTPlay = now - p.fClkOff - p.followD;
    followplay::Sample t;
    p.fMode = followplay::TargetAt(p.trk, p.fTPlay, followplay::kExtrapMaxSec, &t);
    p.fE = 0.0f; p.fEOk = false;
    const bool wasBehind = p.fBehind;
    p.fBehind = false;
    if (p.stopHeld) { p.fTX = p.stopX; p.fTZ = p.stopZ; *aimX = p.stopX; *aimZ = p.stopZ; }
    else if (p.fMode != followplay::kModeEmpty)
    {
        p.fTX = t.x; p.fTZ = t.z;
        p.fBehind = followplay::BehindScheduleStep(wasBehind, t.x, t.z, p.tx, p.tz, p.fHaveH ? p.fV : 0.0f, p.followD);
        if (p.fBehind)
        {
            *aimX = p.tx; *aimZ = p.tz;
            const float bx = p.tx - cur.x, bz = p.tz - cur.z;
            const float bd = sqrtf(bx * bx + bz * bz);
            if (Finite(bd)) { p.fE = bd; p.fEOk = true; }
            ++g_followBehindFrames;
            if (!wasBehind) ++g_followBehindEnters;
            return;
        }
        float ax = t.x, az = t.z;
        if (p.fHaveH && p.fHaveN) followplay::ClampToLimit(t.x, t.z, p.fNX, p.fNZ, p.fHX, p.fHZ, &ax, &az);
        *aimX = ax; *aimZ = az;
    }
    if (p.fMoving && p.fHaveH && p.fMode != followplay::kModeEmpty)
    {
        const float e = followplay::AlongError(p.fTX, p.fTZ, cur.x, cur.z, p.fHX, p.fHZ);
        if (Finite(e)) { p.fE = e; p.fEOk = true; }
        if (p.fMode == followplay::kModeInterp) ++g_followInterp;
        else if (p.fMode == followplay::kModeExtrap) ++g_followExtrap;
        else ++g_followHeld;
    }
}

// AN IDLE FOLLOW COPY's frame (followplay::ReplayIdle: the owner stands, the copy stands on its point on no path, the replay
// has passed the last moving report): the replay is not run. The frame's values are what the replay gives by then - the
// owner standing at its newest report, or at its stated stop - so the probes and the arrival read no stale walk; the aim is
// that point. The copy's along-route speed restarts with the next replayed frame.
static void FollowIdleFrame(Puppet& p, double now, float* aimX, float* aimZ)
{
    ++g_followIdleFrames;
    p.fMoving = false; p.fE = 0.0f; p.fEOk = false; p.fCopyLAt = 0.0; p.fBehind = false;
    p.fTPlay = now - p.fClkOff - p.followD;
    p.fHaveN = p.trk.n > 0;
    p.fMode = p.fHaveN ? followplay::kModeHeld : followplay::kModeEmpty;
    if (p.fHaveN) { const followplay::Sample& nw = followplay::TrackNewest(p.trk); p.fNX = nw.x; p.fNZ = nw.z; p.fTX = nw.x; p.fTZ = nw.z; }
    if (p.stopHeld) { p.fTX = p.stopX; p.fTZ = p.stopZ; }
    if (p.fHaveN || p.stopHeld) { *aimX = p.fTX; *aimZ = p.fTZ; }
}

const float kFollowPastMovingSpeed = 0.5f;   // u/s along h: the copy is moving forward (the never-past check applies)

// PROBE-START: P123
static void P123Held(unsigned int uid, const Puppet& p, const Ogre::Vector3& cur, double now);
// PROBE-END: P123

// THE FOLLOW DRIVE for a copy of a character another game owns, in place of PathDriveStep (CopyPathStep). The engine's
// pathfinder walks the copy; FollowFrame has replayed the owner's route into the aim (aimX, aimZ) and found N, h and the
// owner's speed. Here: the never-past halt, the path or the push, the destination (followplay::FollowDest) and its
// re-plans. Entry and exit, eligibility, the progress window, the budget, the landing read, the too-close push and the
// failed-path retry follow PathDriveStep. A player's copy and an NPC copy differ in the path entry, the destination, a
// hidden copy's re-plan distance and the budget (followplay: FollowPathEnter, FollowDest, NewestReplanDist,
// PathTakesLeftover). Returns true when it handled the frame; false = the push toward the aim.
static bool FollowDriveStep(unsigned int uid, Puppet& p, CharMovement* mv, const Ogre::Vector3& cur,
                            float dist, float aimX, float aimZ, int prone)
{
    const double now = NowSeconds();
    const bool playerCopy = p.followPlayer;
    const int cls = playerCopy ? 0 : 1;   // g_followPath* index
    const float adx = aimX - cur.x, adz = aimZ - cur.z;
    const float distA = sqrtf(adx * adx + adz * adz);
    if (!Finite(distA))
    {
        if (p.pathMode) PathLeave(uid, p, mv, distA, "non-finite aim distance", false);
        return false;
    }
    const bool ownerMoving = p.fMoving;         // the owner's newest report moves and no stop is stated
    const bool hidden = p.fVum != 1;            // unreadable counts as hidden: its destination is then a reported point

    // NEVER PAST (a copy moved every frame): moving forward along h and level with N - or with the stated stop point - it
    // halts at once, whatever its path still says (a copy standing level with N, as at the start of a trip, is not passing
    // it). A halt at N holds until N is followplay::kRestartAhead ahead again; at the stop point the push finishes the last
    // stretch onto it (p.fStopArrive, the radius DrivePuppet's arrival uses). Not while behind schedule: N and h are then the
    // recording's, which the owner has left, and the copy's path and push end at the owner's newest reported point itself.
    if (!hidden && p.fHaveH && !p.fBehind && (p.stopHeld || p.fHaveN))
    {
        const float lx = p.stopHeld ? p.stopX : p.fNX, lz = p.stopHeld ? p.stopZ : p.fNZ;
        if (p.fHeld && !p.stopHeld && followplay::NeverPastRestart(cur.x, cur.z, lx, lz, p.fHX, p.fHZ)) p.fHeld = false;
        if (!p.fHeld && p.fCopyAlongV > kFollowPastMovingSpeed && followplay::NeverPastHalt(cur.x, cur.z, lx, lz, p.fHX, p.fHZ))
        {
            p.fHeld = true;
            if (p.stopHeld) ++g_followHeldAtStop; else ++g_followHeldAtNewest;
            P123Held(uid, p, cur, now);   // PROBE P123
            if (p.pathMode) PathLeave(uid, p, mv, distA, p.stopHeld ? "level with the stop point" : "level with the owner's newest point", true);
            else if (p.driving && !SafeLeavePath(mv)) ++g_pathLeaveFaulted;
            p.driving = false;
        }
        if (p.fHeld)
        {
            // At the stop point: a copy still short of it along h and off it by more than the stop's arrival radius is pushed
            // the last stretch onto it; level with it or past it, it stands (never walked back along the route).
            if (p.stopHeld)
            {
                const float sx = p.stopX - cur.x, sz = p.stopZ - cur.z;
                const bool shortOf = sx * p.fHX + sz * p.fHZ > 0.0f;
                if (shortOf && sx * sx + sz * sz > p.fStopArrive * p.fStopArrive) return false;
            }
            return true;   // stands (at N: until N is ahead again)
        }
    }
    else p.fHeld = false;

    // THE PATH OR THE PUSH. Entered (followplay::FollowPathEnter) beyond kPathEnter from the aim or after a stalled window,
    // and on a player's copy also from its owner's first moving report, so the planning overlaps the delay; an NPC copy keeps
    // the push near its replay target, as before the follow drive. Kept until within kPathLeave of the aim with the owner
    // stopped (its stop stated, or standing kPathOwnerStillSec).
    const bool ownerStopped = p.stopHeld || (!ownerMoving && p.pathOwnerStillSince > 0.0 && now - p.pathOwnerStillSince >= kPathOwnerStillSec);
    const bool byDist = distA > kPathEnter;
    bool want = p.pathMode ? (distA >= kPathLeave || !ownerStopped)
                           : followplay::FollowPathEnter(playerCopy, ownerMoving, p.fHaveH, distA, kPathEnter, kPathLeave, p.stalledWindows);
    if (want && !p.pathMode && now < p.pathHoldUntil) want = false;   // held on the push after a path without progress
    bool ineligible = false;
    if (want && !PathEligible(p, prone)) { want = false; ineligible = true; ++g_pathIneligibleFrames; }
    if (!want)
    {
        if (p.pathMode)
        {
            if (!ineligible) ++g_pathLeaveOwnerStopped;
            PathLeave(uid, p, mv, distA, ineligible ? "not eligible" : "within kPathLeave, owner stopped", false);
        }
        return false;
    }
    if (!p.pathMode)
    {
        p.pathMode = true;
        ++g_pushToPath;
        if (!byDist && p.stalledWindows >= 1) ++g_pathEnterByStall;
        p.stalledWindows = 0;
        p.driving = false;   // the push is cancelled by the path request (+0x378 = 0)
        PathForgetDest(p);
        PathLog("pushToPath uid=" + N2(uid) + " aimDist=" + F2(distA) + " (follow)");
    }
    ++g_pathFrames;

    // The progress window (PathDriveStep's): a path that does not bring the copy nearer its aim falls back to the push.
    if (p.pathProgArmed && ownerMoving) p.pathProgOwnerMoved = true;
    if (p.pathProgArmed && g_tick - p.pathProgTick >= kProgressWindowTicks && !PathPending(p, now))
    {
        const float shrunk = p.pathProgFromDistA - distA;
        const bool grew = -shrunk > kPathGrowMax;
        const float cmx = cur.x - p.pathProgFromX, cmz = cur.z - p.pathProgFromZ;
        const bool deadPath = (cmx * cmx + cmz * cmz) < 0.25f && distA > kPathLeave;
        if (deadPath) ++g_pathDeadFallback;
        if (grew || deadPath || (!p.pathProgOwnerMoved && shrunk < kPathProgressMin))
        {
            ++g_pathNoProgressFallback;
            p.pathHoldUntil = now + kPathHoldSec;
            PathLeave(uid, p, mv, distA, grew ? "aim distance grew > 4 in a window - the push for 5 s"
                                              : "no progress in a window - the push for 5 s", false);
            return false;
        }
        p.pathProgTick = g_tick; p.pathProgFromDistA = distA; p.pathProgOwnerMoved = ownerMoving; p.pathProgFromX = cur.x; p.pathProgFromZ = cur.z;
    }

    // The copy's own measured speed (the too-close push and the issue-to-moving timer read it). -1 = no sample this frame.
    float spd = -1.0f;
    if (p.pathLastAt > 0.0)
    {
        const double dt = now - p.pathLastAt;
        if (dt > 0.0001 && dt < 0.25)
        {
            const float mx = cur.x - p.pathLastX, mz = cur.z - p.pathLastZ;
            spd = (float)(sqrtf(mx * mx + mz * mz) / dt);
        }
    }
    p.pathLastX = cur.x; p.pathLastZ = cur.z; p.pathLastAt = now;
    const float copySpeed = cooppath::CopySpeed(spd);

    // THE DESTINATION: the stop point; N (hidden, an NPC copy, or the owner standing); else the nearer of the goal and the
    // keep-walking point. Behind schedule: the owner's newest reported point (FollowFrame's aim). A failed path beyond N is
    // retried once at N itself.
    float destX = aimX, destZ = aimZ;
    int kind = followplay::kDestNewest;
    if (!p.fBehind)
    {
        followplay::DestIn in;
        in.stopHeld = p.stopHeld; in.sx = p.stopX; in.sz = p.stopZ; in.hidden = hidden; in.ownerMoving = ownerMoving;
        in.haveN = p.fHaveN; in.nx = p.fNX; in.nz = p.fNZ; in.haveH = p.fHaveH; in.hx = p.fHX; in.hz = p.fHZ; in.v = p.fV;
        in.haveG = p.goalHave; in.gx = p.goalX; in.gz = p.goalZ; in.cx = cur.x; in.cz = cur.z; in.playerCopy = playerCopy;
        kind = followplay::FollowDest(in, &destX, &destZ);
        if (kind == followplay::kDestNone) { destX = aimX; destZ = aimZ; kind = followplay::kDestNewest; }
    }
    const bool beyondN = kind == followplay::kDestKeep || kind == followplay::kDestGoal;
    const bool retryAim = beyondN && p.pathRetryAim == 1 && p.fHaveN;
    if (retryAim) { destX = p.fNX; destZ = p.fNZ; kind = followplay::kDestNewest; }
    p.fDestKind = kind;
    const float kw = followplay::KeepWalkDist(p.fV);

    // THE RE-PLAN. why: 1 near end, 2 turn, 3 off line, 4 the stop point (priority), 5 the retry at N, 6 another kind of
    // destination, 7 turned hidden with a path beyond N (priority), 8 N moved (followplay::NewestReplanDist: farther for a
    // hidden NPC copy), 9 the goal moved.
    const float newestReplan = followplay::NewestReplanDist(playerCopy, hidden, kPathReissueDist);
    bool reissue = !p.pathHaveDest;
    int why = 0;
    if (!reissue)
    {
        const float mdx = destX - p.pathDestX, mdz = destZ - p.pathDestZ;
        const float moved2 = mdx * mdx + mdz * mdz;
        if (kind == followplay::kDestStop)
        {
            if (p.stopIssueOwed || p.fIssuedKind != followplay::kDestStop || moved2 > kPathArrivedDist * kPathArrivedDist) { reissue = true; why = 4; }
        }
        else if (retryAim) { reissue = true; why = 5; }
        else if (hidden && kind != p.fIssuedKind && (p.fIssuedKind == followplay::kDestKeep || p.fIssuedKind == followplay::kDestGoal)) { reissue = true; why = 7; }
        else if (kind != p.fIssuedKind) { reissue = true; why = 6; }
        else if (kind == followplay::kDestNewest) { if (moved2 >= newestReplan * newestReplan) { reissue = true; why = 8; } }
        else if (kind == followplay::kDestGoal) { if (moved2 > kPathArrivedDist * kPathArrivedDist) { reissue = true; why = 9; } }
        else
        {
            // the keep-walking point: re-planned before the copy reaches its end, on a turn of the owner's route, or when N
            // left the destination's line - the end distance from the OWNER's speed
            const float ex = p.pathDestX - cur.x, ez = p.pathDestZ - cur.z;
            const float nearEnd = cooppath::NearEndDist(p.fV, kw);
            const float ox = p.fNX - p.pathLineX, oz = p.fNZ - p.pathLineZ;
            const float off = ox * p.pathDestDirZ - oz * p.pathDestDirX;
            const bool turned = p.fHX * p.pathDestDirX + p.fHZ * p.pathDestDirZ < kPathTurnCos;
            if (!turned) p.pathTurnSince = 0.0;
            else if (p.pathTurnSince <= 0.0) p.pathTurnSince = now;
            if (ex * ex + ez * ez < nearEnd * nearEnd) { reissue = true; why = 1; }
            else if (turned && now - p.pathTurnSince >= kPathTurnPersistSec) { reissue = true; why = 2; }
            else if (off > kPathOffLineMax || off < -kPathOffLineMax) { reissue = true; why = 3; }
        }
    }
    if (kind != followplay::kDestKeep) p.pathTurnSince = 0.0;
    // The engine drops a destination < 2 u from the last (0x6607E0): a keep-walking point is pushed on along h instead;
    // every other kind is a limit and is never pushed past - the old path, which ends there too, goes on.
    bool extended = false;
    if (reissue && p.pathHaveDest)
    {
        const float sx = destX - p.pathDestX, sz = destZ - p.pathDestZ;
        if (sx * sx + sz * sz < kPathDestMinSep * kPathDestMinSep)
        {
            float nx = destX, nz = destZ;
            if (kind == followplay::kDestKeep && cooppath::ExtendTooClose(destX, destZ, p.pathDestX, p.pathDestZ, p.fHX, p.fHZ, cur.x, cur.z,
                                                                            copySpeed, kw, kPathDestMinSep, kPathArrivedDist, &nx, &nz))
            {
                destX = nx; destZ = nz; extended = true;
            }
            else
            {
                reissue = false;
                if (!p.pathTooCloseNoted) { p.pathTooCloseNoted = true; ++g_pathReplanTooClose; }
                if (why == 4) p.stopIssueOwed = false;
                p.fIssuedKind = kind;   // the live path already ends at this point
            }
        }
    }
    if (p.pathHaveDest && !p.pathLanded && !p.pathLandedFaultNoted)
    {
        const int ls = cooppath::LandedStep(SafePathRequestStatus(mv), &p.pathSeenPending);
        if (ls > 0) p.pathLanded = true;
        else if (ls < 0) { p.pathLandedFaultNoted = true; ++g_pathLandedReadFaulted; }
    }
    // The stop point and a turn to hidden go past the floor and first in the budget (the stop issue's rule); every other
    // re-plan keeps the gap and the floor (cooppath::IssueGateBlocks).
    const bool priority = why == 4 || why == 7;
    const bool inFloor = (now - p.pathIssueAt) < kPathIssueFloorSec;
    if (reissue && !priority && cooppath::IssueGateBlocks(now - p.pathIssueAt, p.pathHaveDest && p.pathLanded, kPathIssueFloorSec)) reissue = false;
    // The budget (PathBudgetFrame): the queue serves player copies first; an NPC copy's issue always waits for it.
    bool may = false;
    if (reissue)
    {
        may = p.pathGrant;
        if (!may && g_pathBudgetLeft > 0 && followplay::PathTakesLeftover(true, playerCopy)) { --g_pathBudgetLeft; may = true; }
        if (!may)
        {
            if (!p.pathWantIssue) { p.pathWantIssue = true; p.pathWantSince = now; }
            if (priority) p.pathWantPriority = true;
            ++g_pathBudgetDeferred; ++g_followPathDeferred[cls];
        }
    }
    else { p.pathWantIssue = false; p.pathWantPriority = false; }
    p.pathGrant = false;
    if (may)
    {
        p.pathWantIssue = false;
        if (!SafeSetDestination(mv, Ogre::Vector3(destX, p.ty, destZ)))
        {
            ++g_pathIssueFailed;
            PathLeave(uid, p, mv, distA, "setDestination faulted", false);
            return false;
        }
        ++g_pathIssued; ++g_followPathIssued[cls];
        if (kind >= 0 && kind < 5) ++g_followIssue[kind];
        if (why == 7) ++g_followHiddenReplan;
        if (why == 5) ++g_pathFailedRetryAim;
        if (extended) ++g_pathReplanExtended;
        if (inFloor && !priority) ++g_pathReplanOnLanded;
        if (why == 4) p.stopIssueOwed = false;
        p.pathWantPriority = false; p.pathTurnSince = 0.0;
        p.pathRetryAim = (why == 5) ? 2 : 0;
        p.pathLeadWalking = false;
        p.pathHaveDest = true; p.pathDestX = destX; p.pathDestZ = destZ; p.pathIssueAt = now; p.pathFailNoted = false; p.pathIssueTick = g_tick; p.pathFailSeenClear = false;
        p.pathDestLead = kind == followplay::kDestKeep; p.pathDestDirX = p.fHX; p.pathDestDirZ = p.fHZ;
        p.pathLineX = p.fHaveN ? p.fNX : aimX; p.pathLineZ = p.fHaveN ? p.fNZ : aimZ; p.pathArrivedNoted = false;
        p.pathSeenPending = false; p.pathLanded = false; p.pathLandedFaultNoted = false; p.pathTooCloseNoted = false;
        p.fIssuedKind = kind;
        if (!p.pathAwait) { p.pathAwait = true; p.pathAwaitTick = g_tick; }
        if (!p.pathProgArmed) { p.pathProgArmed = true; p.pathProgTick = g_tick; p.pathProgFromDistA = distA; p.pathProgOwnerMoved = ownerMoving; p.pathProgFromX = cur.x; p.pathProgFromZ = cur.z; }
    }
    else if (!reissue) ++g_pathDeduped;

    // A failed path beyond N is retried once at N; that failing too falls back to the push, held kPathHoldSec.
    if (p.pathHaveDest && !p.pathFailNoted)
    {
        const int f = SafePathFailed(mv);
        const bool failTrusted = p.pathFailSeenClear || (g_tick - p.pathIssueTick) >= kPathFailTrustTicks;
        if (f == 1 && failTrusted)
        {
            p.pathFailNoted = true; ++g_pathFailed;
            PathLog("pathFailed uid=" + N2(uid) + " aimDist=" + F2(distA) + " dest=" + F2(p.pathDestX) + "," + F2(p.pathDestZ) + " (follow)");
            if (p.pathRetryAim == 2)
            {
                p.pathHoldUntil = now + kPathHoldSec;
                PathLeave(uid, p, mv, distA, "the retry at the owner's newest point failed too - the push for 5 s", false);
                return false;
            }
            if (p.fIssuedKind == followplay::kDestKeep || p.fIssuedKind == followplay::kDestGoal) p.pathRetryAim = 1;
        }
        else if (f < 0) { p.pathFailNoted = true; ++g_pathFailedReadFaulted; }
        else if (f == 0) p.pathFailSeenClear = true;
    }

    if (p.pathAwait && Finite(spd) && spd > kPathMovingSpeed && SafeReadMoveMode(mv) == 0)
    {
        const long long fr = g_tick - p.pathAwaitTick;
        p.pathAwait = false;
        if (fr <= 2) ++g_pathMoveLe2; else if (fr <= 10) ++g_pathMoveLe10; else if (fr <= 60) ++g_pathMoveLe60; else ++g_pathMoveGt60;
        if (fr > g_pathMoveMaxFrames) g_pathMoveMaxFrames = fr;
    }

    ApplyDriveSpeed(p, mv, dist, cur, aimX, aimZ);   // desiredSpeed caps the speed in path mode too (F305)
    return true;
}

// The path drive for one copy: the follow drive for every follow copy (p.follow), PathDriveStep for any other row.
static bool CopyPathStep(unsigned int uid, Puppet& p, CharMovement* mv, const Ogre::Vector3& cur,
                         float dist, float aimX, float aimZ, int prone, float ownerSpeed)
{
    if (p.follow) return FollowDriveStep(uid, p, mv, cur, dist, aimX, aimZ, prone);
    return PathDriveStep(uid, p, mv, cur, dist, aimX, aimZ, prone, ownerSpeed);
}

// H024 - turn a stationary copy to face where its authority faces, through the engine's own faceToward.
static bool SafeLookAt(::Character* c, float x, float y, float z)
{
    __try { c->faceToward(Ogre::Vector3(x, y, z), true); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool CopyFacing(CharMovement* mv, float* fx, float* fz)
{
    float x = 0.0f, z = 0.0f;
    __try { const Ogre::Vector3& f = mv->headingVector(); x = f.x; z = f.z; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    const float n = sqrtf(x * x + z * z);
    if (!Finite(n) || n < 0.001f) return false;
    *fx = x / n; *fz = z / n; return true;
}
static void ApplyFacingIfStationary(Puppet& p, CharMovement* mv, const Ogre::Vector3& cur)
{
    if (p.faceX == 0.0f && p.faceZ == 0.0f) { ++g_faceSkippedNoVector; return; }
    if (g_tick - p.faceTick < kFaceApplyEveryTicks) return;
    float cx = 0.0f, cz = 0.0f;
    if (CopyFacing(mv, &cx, &cz) && (cx * p.faceX + cz * p.faceZ) > kFaceApplyCos) return;   // already facing that way
    p.faceTick = g_tick;
    if (SafeLookAt(p.ch, cur.x + p.faceX * 10.0f, cur.y, cur.z + p.faceZ * 10.0f)) ++g_faceWrites; else ++g_faceWriteFailed;
}

// H023 - has the copy's AI consumed its order? needGOAP 0 with a running action = yes (F424/F430).
static bool OrderConsumed(::Character* c)
{
    void* ai = GetCharacterAI(c);
    if (!PlausibleObj(ai)) return false;
    const char* ts = *(const char* const*)((const char*)ai + 0x20);
    if (!PlausibleObj(ts)) return false;
    __try
    {
        if (*(const unsigned char*)(ts + 0x1B0) != 0) return false;          // needGOAP still up: not consumed
        const char* body = *(const char* const*)(ts + 0x270);
        return body != 0 && *(const void* const*)(body + 0x68) != 0;        // CharBody::currentAction
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// H021 - one frame of the pathfinding drive. Returns true when the engine's own task owns locomotion
// this frame (the caller must NOT push); false when the caller should fall back to the straight push.
static bool OrderDriveStep(unsigned int uid, Puppet& p, CharMovement* mv, const Ogre::Vector3& cur, float dist, float aimX, float aimY, float aimZ)
{
    if (dist <= kOrderDoneDist)
    {
        // Close enough: the engine's arrival ends the task by itself; nothing to push, nothing to order.
        if (p.orderActive) p.orderActive = false;
        ++g_orderDoneFrames;
        return true;
    }
    // H023 - ALONG-TRACK lag: positive = the copy is behind the aim along the authority's direction of travel,
    // negative = ahead. A plain distance cannot tell the two apart, and that is what made copies run backwards.
    float e = dist;
    {
        const float vs = sqrtf(p.velX * p.velX + p.velZ * p.velZ);
        if (Finite(vs) && vs > 1.0f) e = ((aimX - cur.x) * p.velX + (aimZ - cur.z) * p.velZ) / vs;
    }
    if (e < kOrderAheadRetract)
    {
        // Ahead of the aim: never order it backwards. Retract what it is doing and let the authority catch up.
        if (p.orderActive) { ClearAttackOrder(p.ch); SafeStopMovement(mv); p.driving = false; p.orderActive = false; ++g_orderAheadRetracts; }
        ++g_orderAheadHeld;
        return true;
    }
    const float mdx = aimX - p.orderX, mdz = aimZ - p.orderZ;
    const float aimMoved = sqrtf(mdx * mdx + mdz * mdz);                       // aim vs the ORDERED point
    const float odx = p.orderX - cur.x, odz = p.orderZ - cur.z;
    const float toOrdered = sqrtf(odx * odx + odz * odz);                     // copy vs the ORDERED point
    const bool consumed = p.orderActive && OrderConsumed(p.ch);
    if (p.orderActive) { if (consumed) ++g_orderConsumedFrames; else ++g_orderUnconsumedFrames; }
    // H023 - re-issue ONLY when the last order was consumed and is spent (arrived, or the aim has moved far from
    // it), or when it was never consumed within the retry window. Never while a consumed order is still useful.
    bool reissue = false;
    if (!p.orderActive) reissue = true;
    else if (consumed) reissue = (toOrdered < kOrderArriveDist) || (aimMoved > kOrderReissueDist);
    else if ((g_tick - p.orderTick) > kOrderReissueTicks) { reissue = true; ++g_orderRetries; }
    // H022/H023 - gait: RUN while genuinely behind along the track (bounded catch-up, hysteresis 10/4) or while
    // the authority itself runs; else WALK.
    if (!p.catchingUp && e > kOrderRunEnter) { p.catchingUp = true; ++g_speedCatchupEnters; }
    else if (p.catchingUp && e < kOrderRunLeave) p.catchingUp = false;
    const bool authorityRuns = Finite(p.authDesired) && p.authDesired > kOrderRunAuthority;
    const int wantSpeed = (p.catchingUp || authorityRuns) ? 2 /*RUN*/ : 0 /*WALK*/;
    if (wantSpeed != p.orderSpeed)
    {
        if (SafeSetSpeedOrders(mv, wantSpeed)) { p.orderSpeed = wantSpeed; ++g_orderSpeedWrites; }
        else ++g_orderSpeedWriteFailed;
    }
    if (wantSpeed == 2) ++g_orderRunFrames; else ++g_orderWalkFrames;
    if (reissue)
    {
        if (p.driving) { SafeStopMovement(mv); p.driving = false; }   // hand locomotion to the engine's task
        if (InjectOrder(p.ch, kMoveOrderType, 0, Ogre::Vector3(aimX, aimY, aimZ)))
        {
                        p.orderActive = true; p.orderX = aimX; p.orderZ = aimZ; p.orderTick = g_tick; ++p.orderCount; ++g_orderIssued;
        }
        else { ++g_orderIssueFailed; ++g_orderFallbackPush; return false; }
    }
    ++g_orderHeldFrames;
        return true;
}

// Push the puppet one frame toward its streamed target, or stop it if it has arrived.
static int ReadProneStatePod(void* c) { __try { return *(int*)((char*)c + kProneStateOff); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; } }   // PROBE P019 (no C++ objects: C2712)
static int ReadCharFrameTimesPod(void* c, float* oft, float* ft, int* vum) { __try { *oft = *(float*)((char*)c + 0xC0); *ft = *(float*)((char*)c + 0xC4); *vum = (int)*(unsigned char*)((char*)c + 0xE4); return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }   // PROBE P019: Character::offscreenFrameTime / frameTIME / isVisibleUpdateMode (Read: visible-position-stepping-read.md)
const char* SnapCauseName(int cause)
{
    return cause == kSnapCauseFar ? "far" : (cause == kSnapCauseStuck ? "stuck" : "unknown");
}

// review-r2 items 1/6/8: the gates the far and the stuck snap share. 0 = may snap; otherwise why not, counted.
// Called only where a snap would otherwise be placed, which is at most once per puppet per window.
const char* SnapSharedGate(Puppet& p, double ageSec)
{
    if (ageSec >= kSnapMaxSampleAgeSec)
    {
        ++g_snapSkippedStale;
        return "stale - the owner's last sample is 3 s old or more, so its target is where the owner WAS";
    }
    CharDbg cd;   // the diagnostic read MovementDebug uses: _isBeingCarried (+0x3D4), inSomething (+0x2F8)
    if (ReadCharDbgFields(p.ch, &cd) && (cd.carried != 0 || cd.inSomething != 0))
    {
        ++g_snapSkippedAttached;
        return "attached - carried, or in a cage, bed or building slot";
    }
    if (NowSeconds() - p.snapTimes[p.snapTimesNext] < kSnapRateWindowSec)
    {
        ++g_snapRateCapped;
        return "rate cap - 5 snaps on this copy in the last 60 s";
    }
    return 0;
}

// review-r2 items 1/5: book a placement - its cause, the strike watch, the rate ring.
void NoteSnapPlaced(Puppet& p, int cause)
{
    p.snapCause = cause;
    p.snapWatchWindows = 0;
    p.snapTimes[p.snapTimesNext] = NowSeconds();
    p.snapTimesNext = (p.snapTimesNext + 1) % kSnapRateMax;
}

// PROBE-START: P093 - T-196 (H051) WHY A COPY OF A TRULY RUNNING OWNER STAYS >= 20 UNITS BEHIND (measurement only: every
// read below is __try-guarded or an existing guarded reader, and nothing here writes game memory or changes the drive).
// Plan: .modding/investigations/t196-p093-probe.md. P093OnSample fires beside WalkLagSample under ITS trueRunLag test (so
// trig = trueRunLag's 20-40 + 40+ buckets); P093Frame accumulates a window of per-frame reads between two lag samples and,
// on a sample, classifies it into one cause (first match wins): UNK, TRANSIENT, F_SKIP, E_CMD, B_OFF, B_LOST, A_CAP,
// CLOSING, C_HAVOK, C_DETOUR, C_POS; open = SNAP / NET / CREEP. Lines: [M2] RUNPROBE (lag >= 20; the first 400, then
// counted), [M2] RUNPROBE-CTL (|lag| < 5; every 5th, up to 40), [M2] RUNPROBE-SUM with every REPORT.
// P093b (T560's read): what a player sees is the lag of VISIBLE copies, and Kenshi steps a non-visible copy only when it wins
// one of GameWorld::charsUpdate's 6 per-frame update slots (0x7862F0; build/visible-position-stepping-read.md). So each window
// also reads the copy's Character +0xE4 isVisibleUpdateMode (non-zero = updated every frame, decomp_7862f0.txt:45/:90/:106),
// +0xC4 frameTIME and +0xC0 offscreenFrameTime (zeroed when update runs, decomp_5ce6a0.txt:26); a sample is VISIBLE when
// +0xE4 != 0 on >= 80% of its window's frames, else HIDDEN. The UNK floor is W < 0.1 s (T560: windows are 0.35-0.5 s).
enum { kP093Transient = 0, kP093FSkip, kP093ECmd, kP093BOff, kP093BLost, kP093ACap, kP093Closing, kP093CHavok, kP093CDetour,
       kP093CPos, kP093Unk, kP093Classes };
static const char* const kP093ClassName[kP093Classes] = { "TRANSIENT", "F_SKIP", "E_CMD", "B_OFF", "B_LOST", "A_CAP", "CLOSING",
                                                          "C_HAVOK", "C_DETOUR", "C_POS", "UNK" };
static const char* const kP093OpenName[3] = { "SNAP", "NET", "CREEP" };
// Process-wide, never reset (like g_walkLag*), so RUNPROBE-SUM's trig is comparable with the same REPORT's trueRunLag.
static long long g_p093Trig = 0, g_p093Lines = 0, g_p093Capped = 0, g_p093NoRead = 0, g_p093TransientNet = 0;
static long long g_p093Hold20[kP093Classes], g_p093Hold40[kP093Classes], g_p093Open20[3], g_p093BOff[4], g_p093CHavok[3], g_p093ACap[2];
static long long g_p093CtlSeen = 0, g_p093CtlLines = 0, g_p093CtlHvIn = 0, g_p093CtlCpIn = 0;
struct P093UidN { unsigned int uid; long long n; };
static P093UidN g_p093Uids[64]; static int g_p093UidCount = 0; static long long g_p093UidOverflow = 0;
// P093b: hold20/hold40 split by the copy's cohort (V = VISIBLE, H = HIDDEN); trigLost = trig samples whose window was dropped
// (P093Frame's not-running/not-primed reset), so trigVis + trigHid + trigLost = trig. visLag = every true-runner sample on a
// VISIBLE copy, bucketed like trueRunLag [<1, 1-5, 5-10, 10-20, 20-40, >=40, ahead]; samp/sampLost = all true-runner samples.
static long long g_p093Hold20V[kP093Classes], g_p093Hold40V[kP093Classes], g_p093Hold20H[kP093Classes], g_p093Hold40H[kP093Classes];
static long long g_p093TrigVis = 0, g_p093TrigHid = 0, g_p093TrigLost = 0, g_p093TrigNoVisRd = 0, g_p093Samp = 0, g_p093SampLost = 0;
static long long g_p093VisLagN = 0, g_p093VisLagB[7]; static double g_p093VisLagSum = 0.0;
// P093b (owner guidance 2026-09-29: lag off screen does not matter; far from every player's characters it may be larger; near
// them it must be as accurate as reasonable): near = the XZ distance from the copy to the nearest PLAYER-OWNED character,
// taken from two existing registries - this game's own characters (net::OwnedUidsSnapshot = g_localOwned, FindSpawned, kept
// only when their faction is this game's player faction, IsPlayerFaction) and the copies of other players' characters
// (g_puppets rows whose faction is a player stand-in, PeerFactionPod == 1 - PeerPlayerPosition's test). Never a world sweep:
// the list is rebuilt at most every 0.25 s, and only when a sample asks. The copy itself is excluded (by uid). visLag is then
// split into bands by near: [0] near < 300, [1] mid 300-1500, [2] far > 1500 or no player-owned character found (nearNone).
struct P093Pc { unsigned int uid; float x, z; };
static std::vector<P093Pc> g_p093Pcs; static std::vector<unsigned int> g_p093OwnScan; static double g_p093PcAt = -1.0;
static long long g_p093PcOwn = 0, g_p093PcPeer = 0, g_p093NearNone = 0;
static long long g_p093BandN[3], g_p093BandB[3][7]; static double g_p093BandSum[3];
// K: the engine's "is running" factor (speedNow +0xB8 > speedCap +0xB4 x K, 0x65E3B0), a float at image+0x1695C84 in
// Steam 1.0.65 ONLY. Not in the address table (its rows are generated and this is probe-only), so it is read relative to the
// module base, once, and only when the loaded table is Steam_1.0.65 (1.0.68's image is laid out differently - its
// GameWorldGlobal is 0x1060 higher - so the same RVA would be some other value there). Accepted only in (0, 2].
// state: 0 not tried, 1 read, 2 read but outside (0, 2], 3 the read faulted, -1 not Steam_1.0.65.
static int g_p093KState = 0; static float g_p093K = 0.0f, g_p093KRaw = 0.0f;
static unsigned long long kP093KRva = 0; static coop::AddrReg kP093KRva_reg("RunningSpeedFactor", &kP093KRva);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x1695C84 - PROBE-ONLY (P093); 1.0.68 by tools/gen_table_1068.py */

struct P093Mv { float max, cur, des, hv; int kind, mode, agentSt, hvOk; };
// CharMovement +0xB4 max / +0xB8 cur / +0xBC desired / +0x20 kind / +0x378 mode; +0x320 -> the HavokCharacter (the path agent,
// SafePathRequestStatus's pointer): +0x7C x 10 = the speed Havok was given (0x144070), +0x90 = the agent's request status.
static int P093ReadMvPod(CharMovement* mv, P093Mv* o)
{
    __try
    {
        char* m = (char*)mv;
        o->max = *(float*)(m + 0xB4); o->cur = *(float*)(m + 0xB8); o->des = *(float*)(m + 0xBC);
        o->kind = *(int*)(m + 0x20); o->mode = *(int*)(m + kPathMoveModeOff);
        o->hvOk = 0; o->agentSt = -1; o->hv = 0.0f;
        char* hk = *(char**)(m + kPathAgentOff);
        if (hk != 0) { o->hv = *(float*)(hk + 0x7C) * 10.0f; o->agentSt = *(int*)(hk + kPathAgentStatusOff); o->hvOk = 1; }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static int P093ReadFloatPod(uintptr_t at, float* out)
{
    __try { *out = *(const float*)at; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static void P093KOnce()
{
    if (g_p093KState != 0) return;
    if (!coop::AddrOk() || kP093KRva == 0) { g_p093KState = -1; return; }
    float k = 0.0f;
    if (!P093ReadFloatPod((uintptr_t)coop::AddrAbs(kP093KRva), &k)) { g_p093KState = 3; return; }
    g_p093KRaw = k;
    if (Finite(k) && k > 0.0f && k <= 2.0f) { g_p093K = k; g_p093KState = 1; } else g_p093KState = 2;
}

static int P093OwnPlayerPod(::Character* c)   // 1 = in this game's own player faction, 0 = not, -1 = the read faulted
{
    __try { ::Faction* f = c->getOwnerFactionDirect(); return (f != 0 && IsPlayerFaction(f)) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static void P093PcRebuild(double now)
{
    if (g_p093PcAt >= 0.0 && now - g_p093PcAt < 0.25) return;
    g_p093PcAt = now;
    g_p093Pcs.clear();
    g_p093PcOwn = g_p093PcPeer = 0;
    net::OwnedUidsSnapshot(&g_p093OwnScan);
    for (size_t i = 0; i < g_p093OwnScan.size(); ++i)
    {
        ::Character* c = FindSpawned(g_p093OwnScan[i]);
        if (!PlausibleObj(c) || IsRetiredObject(c) || P093OwnPlayerPod(c) != 1) continue;
        Ogre::Vector3 v;
        if (!SafeReadPosition(c, &v)) continue;
        P093Pc e; e.uid = g_p093OwnScan[i]; e.x = v.x; e.z = v.z; g_p093Pcs.push_back(e); ++g_p093PcOwn;
    }
    for (std::map<unsigned int, Puppet>::const_iterator it = g_puppets.begin(); it != g_puppets.end(); ++it)
    {
        const Puppet& q = it->second;
        if (!PlausibleObj(q.ch) || IsRetiredObject(q.ch) || PeerFactionPod(q.ch) != 1) continue;
        Ogre::Vector3 v;
        if (!SafeReadPosition(q.ch, &v)) continue;
        P093Pc e; e.uid = it->first; e.x = v.x; e.z = v.z; g_p093Pcs.push_back(e); ++g_p093PcPeer;
    }
}
// -1 when no player-owned character (other than the copy itself) is known.
static float P093NearPlayer(unsigned int uid, const Ogre::Vector3& cur, double now)
{
    P093PcRebuild(now);
    float best = -1.0f;
    for (size_t i = 0; i < g_p093Pcs.size(); ++i)
    {
        if (g_p093Pcs[i].uid == uid) continue;
        const float dx = g_p093Pcs[i].x - cur.x, dz = g_p093Pcs[i].z - cur.z;
        const float d = sqrtf(dx * dx + dz * dz);
        if (Finite(d) && (best < 0.0f || d < best)) best = d;
    }
    return best;
}

static void P093WinReset(P093Acc& a, double now, long long startTick)
{
    std::memset(&a.w, 0, sizeof(a.w));
    a.w.startAt = now; a.w.startTick = startTick;
    a.w.rstFt = a.w.rstHv = a.w.rstMax = a.w.rstAnim = -1.0f;   // P093b: -1 = no bank spent in this window
}

// Beside WalkLagSample, with the same arguments; the SAME true-runner test as its g_walkLagN[2] branch.
static void P093OnSample(Puppet& p, float lag, bool dirOk)
{
    if (!dirOk) return;
    if (!(Finite(p.authDesired) && p.authDesired >= kCatchupCap)) return;
    float rs = 0.0f, am = 0.0f;
    if (!(RunCeilingRead(p.ch, &rs, &am) && cooprunboost::OwnerRuns(p.authDesired, rs))) return;
    p.p093.due = true; p.p093.sLag = lag;
    if (lag >= 20.0f) ++g_p093Trig;
}

static std::string P093N4(const long long* v, int n)
{
    std::string s;
    for (int i = 0; i < n; ++i) { if (i) s += ","; s += N2(v[i]); }
    return s;
}

// The sample taken this frame: classify the window, count it, maybe log it. `lagU` is this frame's uncapped-aim lag.
static void P093Due(unsigned int uid, Puppet& p, P093Acc& a, double now, float lagU, double ageSec, const Ogre::Vector3& cur)
{
    const P093Win& w = a.w;
    const float lag = a.sLag;
    const bool trig = lag >= 20.0f, ctl = lag > -5.0f && lag < 5.0f;
    const float Wf = (float)(now - w.startAt);
    const long long ticks = g_tick - w.startTick;
    const float hvMean = w.hvN > 0 ? (float)(w.hvSum / w.hvN) : -1.0f;
    const float curMean = w.curN > 0 ? (float)(w.curSum / w.curN) : -1.0f;
    const float runMean = w.runN > 0 ? (float)(w.runSum / w.runN) : -1.0f;
    const float ownMean = w.fr > 0 ? (float)(w.ownSum / w.fr) : 0.0f;
    const float expv = hvMean - ownMean;                              // the closing rate the copy's Havok speed allows
    const float cpAdv = Wf > 0.0f ? w.fwdProg / Wf : 0.0f;            // the copy's advance along the owner's heading, u/s
    const float cpPath = Wf > 0.0f ? w.walked / Wf : 0.0f;            // ... and the distance it walked, u/s
    const float eff = w.walked > 0.01f ? w.fwdProg / w.walked : -1.0f;
    const float got = cpAdv - ownMean;                                // the closing rate it actually made
    const float R = runMean > 0.0f ? ownMean / runMean : -1.0f;
    const float eqLag = R > 0.0f ? 80.0f * (R - 1.0f) : -1.0f;       // where the proportional boost settles (Inferred)
    const bool noRead = w.runN == 0 || w.hvN == 0 || !(hvMean > 0.0f);
    // P093b: the cohort, and every true-runner sample on a VISIBLE copy into visLag (before the trig/ctl filter)
    const bool vis = w.fr > 0 && (double)w.visFr >= 0.8 * (double)w.fr;
    const bool visRd = 2 * w.rdFr >= w.fr;
    ++g_p093Samp;
    const float nearD = P093NearPlayer(uid, cur, now);
    const int band = nearD < 0.0f ? 2 : nearD < 300.0f ? 0 : nearD <= 1500.0f ? 1 : 2;
    if (nearD < 0.0f) ++g_p093NearNone;
    if (vis)
    {
        int b = 6;
        if (lag >= 0.0f) b = lag < 1.0f ? 0 : lag < 5.0f ? 1 : lag < 10.0f ? 2 : lag < 20.0f ? 3 : lag < 40.0f ? 4 : 5;
        ++g_p093VisLagN; g_p093VisLagSum += lag; ++g_p093VisLagB[b];
        ++g_p093BandN[band]; g_p093BandSum[band] += lag; ++g_p093BandB[band][b];
    }
    if (!trig && !ctl) return;
    int cls;
    if (Wf < 0.1f || Wf > 3.0f || noRead) cls = kP093Unk;
    else if (w.lagUmin < 10.0f) cls = kP093Transient;
    else if ((double)(ticks - w.fr + w.skipFr) >= 0.2 * (double)ticks) cls = kP093FSkip;
    else if (w.cmdLowFr >= 0.2 * w.fr) cls = kP093ECmd;
    else if (w.hiFr > 0 && w.offHiFr >= 0.2 * w.hiFr) cls = kP093BOff;
    else if (w.boostFr > 0 && w.lostFr >= 0.2 * w.boostFr) cls = kP093BLost;
    else if (expv <= 5.0f) cls = kP093ACap;
    else if (got >= 0.5f * expv) cls = kP093Closing;
    else if (curMean < 0.85f * hvMean) cls = kP093CHavok;
    else if (eff >= 0.0f && eff < 0.85f) cls = kP093CDetour;
    else cls = kP093CPos;
    const double lastSnap = p.snapTimes[(p.snapTimesNext + kSnapRateMax - 1) % kSnapRateMax];
    const double since = (a.epOn && a.epStartAt < w.startAt) ? a.epStartAt : w.startAt;
    const bool snap = w.teleFr > 0 || lastSnap >= since;
    const float lateNeed = 0.5f * ((lagU - 10.0f) > 1.0f ? (lagU - 10.0f) : 1.0f);
    int open = 2;
    if (snap) open = 0;
    else if (cls == kP093Transient && w.arrJmpMax >= 10.0f) open = 1;
    else if (a.epOn && a.epLate >= lateNeed) open = 1;
    if (trig)
    {
        ++g_p093Hold20[cls];
        if (lag >= 40.0f) ++g_p093Hold40[cls];
        if (vis) { ++g_p093TrigVis; ++g_p093Hold20V[cls]; if (lag >= 40.0f) ++g_p093Hold40V[cls]; }
        else { ++g_p093TrigHid; ++g_p093Hold20H[cls]; if (lag >= 40.0f) ++g_p093Hold40H[cls]; }
        if (!visRd) ++g_p093TrigNoVisRd;
        ++g_p093Open20[open];
        if (noRead) ++g_p093NoRead;
        if (cls == kP093Transient && w.arrJmpMax >= 10.0f) ++g_p093TransientNet;
        if (cls == kP093BOff)
        {
            int bi = 0;
            for (int i = 1; i < 4; ++i) if (w.off[i] > w.off[bi]) bi = i;
            ++g_p093BOff[bi];
        }
        if (cls == kP093CHavok) { if (w.stopFr > 0) ++g_p093CHavok[0]; if (w.endFr > 0) ++g_p093CHavok[1]; if (w.pendFr > 0) ++g_p093CHavok[2]; }
        if (cls == kP093ACap) { if (R > 1.05f) ++g_p093ACap[0]; if (w.boostFr > 0 && 2 * w.capFr >= w.boostFr) ++g_p093ACap[1]; }
        int ui = -1;
        for (int i = 0; i < g_p093UidCount; ++i) if (g_p093Uids[i].uid == uid) { ui = i; break; }
        if (ui < 0 && g_p093UidCount < 64) { ui = g_p093UidCount++; g_p093Uids[ui].uid = uid; g_p093Uids[ui].n = 0; }
        if (ui >= 0) ++g_p093Uids[ui].n; else ++g_p093UidOverflow;
        if (g_p093Lines >= 400) { ++g_p093Capped; return; }
        ++g_p093Lines;
    }
    else
    {
        const long long seen = g_p093CtlSeen++;
        if (seen % 5 != 0 || g_p093CtlLines >= 40) return;
        ++g_p093CtlLines;
        if (ownMean > 0.0f && hvMean >= 0.8f * ownMean && hvMean <= 1.2f * ownMean) ++g_p093CtlHvIn;
        if (ownMean > 0.0f && cpAdv >= 0.8f * ownMean && cpAdv <= 1.2f * ownMean) ++g_p093CtlCpIn;
    }
    float oft = -1.0f, ft = -1.0f; int vum = -1;
    ReadCharFrameTimesPod(p.ch, &oft, &ft, &vum);
    const int prone = ReadProneStatePod(p.ch);
    const long long offv[4] = { w.off[0], w.off[1], w.off[2], w.off[3] };
    DebugLog(std::string(trig ? "[M2] RUNPROBE P093" : "[M2] RUNPROBE-CTL P093")
             + " uid=" + N2(uid) + " t=" + F2((float)now) + " lag=" + F2(lag) + " lagU=" + F2(lagU) + " lagUminW=" + F2(w.lagUmin)
             + " W=" + F3(Wf) + " hold=" + kP093ClassName[cls] + " open=" + kP093OpenName[open]
             + " | run=" + F2(runMean) + " own=" + F2(ownMean) + " R=" + F3(R) + " eqLag=" + F2(eqLag)
             + " | hv=" + F2(hvMean) + "/" + F2(w.hvMin) + " cur=" + F2(curMean) + "/" + F2(w.curMin) + " stopFr=" + N2(w.stopFr)
             + " des=" + F2(a.lastDes) + " max=" + F2(a.lastMax) + " anim=" + F2(a.lastAnim) + " exp=" + F2(expv) + " got=" + F2(got)
             + " cpAdv=" + F2(cpAdv) + " cpPath=" + F2(cpPath) + " eff=" + F3(eff)
             + " | fr=" + N2(w.fr) + "/" + N2(ticks) + " boostFr=" + N2(w.boostFr) + " capFr=" + N2(w.capFr)
             + " hi=" + N2(w.hiFr) + " offHi=" + N2(w.offHiFr) + " off=" + P093N4(offv, 4) + " skipFr=" + N2(w.skipFr)
             + " lostFr=" + N2(w.lostFr) + " ovwFr=" + N2(w.ovwFr) + " easeFr=" + N2(w.easeFr) + " ent=" + N2(w.starts)
             + " rst=" + N2(w.restores) + " cmdLowFr=" + N2(w.cmdLowFr)
             + " | path=" + N2(w.pathFr) + " pend=" + N2(w.pendFr) + " defer=" + N2(w.deferFr) + " iss=" + N2(w.issues)
             + " endFr=" + N2(w.endFr) + " mode=" + N2(a.lastMode) + " agentSt=" + N2(a.lastAgent)
             + " | arr=" + N2(w.arrivals) + " late=" + N2(w.lateArr) + " gapMax=" + F3((float)w.gapMax) + " ivl=" + F3((float)p.moveIntervalSec)
             + " since=" + F3((float)ageSec) + " arrJmpMax=" + F2(w.arrJmpMax) + " arrJmpSum=" + F2(w.arrJmpSum)
             + " leadClampFr=" + N2(w.leadClampFr) + " expFr=" + N2(w.expFr)
             + " | ep=" + F2(a.epOn ? (float)(now - a.epStartAt) : 0.0f) + " epLate=" + F2(a.epLate) + " epArr=" + F2(a.epArr)
             + " epCreep=" + F2(a.epCreep) + " epClose=" + F2(a.epClose) + " snap=" + (snap ? "1" : "0")
             + " | kind=" + N2(a.lastKind) + " runAnimFr=" + N2(g_p093KState == 1 ? w.runAnimFr : -1) + " vum=" + N2(vum)
             + " ft=" + F3(ft) + " prone=" + N2(prone)
             + " | cohort=" + (vis ? "VISIBLE" : "HIDDEN") + " vis=" + N2(w.visFr) + "/" + N2(w.fr) + " rd=" + N2(w.rdFr)
             + " slowFr=" + N2(w.slowFr) + " ftMax=" + F3(w.ftMax) + " rst=" + N2(w.rstN) + " rstFt=" + F3(w.rstFt)
             + " rstHv=" + F2(w.rstHv) + " rstMax=" + F2(w.rstMax) + " rstAnim=" + F2(w.rstAnim)
             + " near=" + F2(nearD) + " pc=" + N2(PeerFactionPod(p.ch)));
}

// Every DrivePuppet frame that reaches the lag sample (before `CharMovement* mv`). Reads only.
static void P093Frame(unsigned int uid, Puppet& p, const Ogre::Vector3& cur, float aimX, float aimZ, double ageSec, double horizon,
                      bool expired, bool haveVel, float leadSec, float leadScale, float ownerSpeed)
{
    P093Acc& a = p.p093;
    const double now = NowSeconds();
    P093KOnce();
    if (!(Finite(p.authDesired) && p.authDesired >= kCatchupCap) || !p.ownPrimed)
    {
        if (a.due) { ++g_p093SampLost; if (a.sLag >= 20.0f) ++g_p093TrigLost; }   // P093b: a sample with no window
        std::memset(&a, 0, sizeof(a));   // not a running owner (or no smoothed speed/heading): nothing to measure
        return;
    }
    P093Mv m; std::memset(&m, 0, sizeof(m));
    int mvOk = 0;
    CharMovement* mv0 = p.ch->movement;
    if (PlausibleObj(mv0)) mvOk = P093ReadMvPod(mv0, &m);
    const bool hvOk = mvOk && m.hvOk && Finite(m.hv);
    const bool curOk = mvOk && Finite(m.cur);
    float run = 0.0f, animMax = 0.0f;
    const bool runOk = RunCeilingRead(p.ch, &run, &animMax) && Finite(run) && run > 0.0f;
    bool dirOk = false;
    const float lagF = SignedAimLag(p, cur, aimX, aimZ, &dirOk);
    float ux = p.tx, uz = p.tz;   // the UNCAPPED aim: no 8-unit clamp, no decay
    if (haveVel) { const float t = (float)(ageSec < horizon ? ageSec : horizon); ux += p.velX * t; uz += p.velZ * t; }
    bool dirU = false;
    const float lagU = SignedAimLag(p, cur, ux, uz, &dirU);
    const float hn = sqrtf(p.ownHdgX * p.ownHdgX + p.ownHdgZ * p.ownHdgZ);
    const bool first = !a.prim;
    if (first)
    {
        a.prim = true;
        P093WinReset(a, now, g_tick - 1);
        a.px = cur.x; a.pz = cur.z; a.pLagU = lagU; a.pWant = 0.0f; a.pStep = 0;
        a.pLastMoveAt = p.lastMoveAt; a.pPathIssueAt = p.pathIssueAt; a.pRbActive = p.rbActive; a.pLagSampleAt = 0.0;
    }
    P093Win& w = a.w;
    ++w.fr;
    // P093b: the engine's update state for this copy (the existing guarded reader; reads only)
    float oftE = -1.0f, ftE = -1.0f; int vumE = -1;
    if (ReadCharFrameTimesPod(p.ch, &oftE, &ftE, &vumE) && Finite(oftE) && Finite(ftE))
    {
        ++w.rdFr;
        if (vumE != 0) ++w.visFr;
        if (ftE > 0.25f) ++w.slowFr;
        if (ftE > w.ftMax) w.ftMax = ftE;
        if (a.pOftOk && oftE < a.pOft)   // the bank was spent: Character::update ran (it zeroes +0xC0, decomp_5ce6a0.txt:26)
        {
            ++w.rstN; w.rstFt = ftE;
            w.rstHv = hvOk ? m.hv : -1.0f; w.rstMax = mvOk ? m.max : -1.0f; w.rstAnim = runOk ? animMax : -1.0f;
        }
        a.pOftOk = true; a.pOft = oftE;
    }
    else a.pOftOk = false;
    if (hvOk) { w.hvSum += m.hv; ++w.hvN; if (w.hvN == 1 || m.hv < w.hvMin) w.hvMin = m.hv; }
    if (curOk) { w.curSum += m.cur; ++w.curN; if (w.curN == 1 || m.cur < w.curMin) w.curMin = m.cur; if (m.cur < 1.0f) ++w.stopFr; }
    if (hvOk && runOk && m.hv < 0.95f * run) ++w.cmdLowFr;
    if (runOk) { w.runSum += run; ++w.runN; }
    w.ownSum += ownerSpeed;
    if (g_p093KState == 1 && curOk && Finite(m.max) && m.cur > m.max * g_p093K) ++w.runAnimFr;
    if (w.fr == 1 || lagF < w.lagFmin) w.lagFmin = lagF;
    if (w.fr == 1 || lagU < w.lagUmin) w.lagUmin = lagU;
    if (!first)
    {
        const float dx = cur.x - a.px, dz = cur.z - a.pz;
        const float st = sqrtf(dx * dx + dz * dz);
        if (Finite(st))
        {
            if (st > 50.0f) ++w.teleFr;
            else { w.walked += st; if (Finite(hn) && hn > 0.001f) w.fwdProg += (dx * p.ownHdgX + dz * p.ownHdgZ) / hn; }
        }
    }
    // the boost: renewed by the previous frame's drive?
    const bool boost = p.rbFrame == g_tick - 1;
    if (boost)
    {
        ++w.boostFr;
        if (runOk && a.pWant >= 1.49f * run) ++w.capFr;
        if (hvOk && a.pWant > 0.0f && m.hv < 0.95f * a.pWant) ++w.lostFr;
    }
    if (p.rbCeiling && runOk && animMax != p.rbWrote) ++w.ovwFr;
    if (p.rbCeiling && !p.rbActive) ++w.easeFr;
    if (!first && !a.pRbActive && p.rbActive) ++w.starts;
    if (!first && a.pRbActive && !p.rbActive) ++w.restores;
    if (!first && (a.pStep == cooprunboost::kRbEnter || a.pStep == cooprunboost::kRbKeep) && !boost) ++w.skipFr;
    // RunBoostFrame's decision re-run on this frame's inputs (the drive's own, when it reaches ApplyDriveSpeed's -2 arm)
    const bool running = runOk && cooprunboost::OwnerRuns(p.authDesired, run);
    const bool lever = g_runBoost && g_driveSpeed == -2.0f;
    const int step = lever ? cooprunboost::Decide(g_runBoost, p.rbActive, running, dirOk, lagF) : cooprunboost::kRbIdle;
    const bool says = step == cooprunboost::kRbEnter || step == cooprunboost::kRbKeep;
    if (lagU >= 10.0f)
    {
        ++w.hiFr;
        if (!boost)
        {
            ++w.offHiFr;
            if (!lever) ++w.off[2];
            else if (!running) ++w.off[0];
            else if (!dirOk) ++w.off[1];
            else if (!says) ++w.off[3];   // Decide says no only because lagF (the capped aim's lag) is small
        }
    }
    const float want = says ? cooprunboost::BoostSpeed(cooprunboost::EffectiveSpeed(p.authDesired, run), lagF) : 0.0f;
    // path
    if (p.pathMode) ++w.pathFr;
    if (PathPending(p, now)) ++w.pendFr;
    if (p.pathWantIssue) ++w.deferFr;
    if (!first && p.pathIssueAt != a.pPathIssueAt) ++w.issues;
    if (p.pathMode && p.pathHaveDest && ownerSpeed > 2.0f)
    {
        const float ex = cur.x - p.pathDestX, ez = cur.z - p.pathDestZ;
        const float ed = sqrtf(ex * ex + ez * ez);
        if (Finite(ed) && ed <= kPathArrivedDist) ++w.endFr;
    }
    // network
    bool arrived = false, late = false;
    const float dL = first ? 0.0f : lagU - a.pLagU;
    if (!first && p.lastMoveAt != a.pLastMoveAt)
    {
        arrived = true;
        ++w.arrivals;
        const double gap = p.lastMoveAt - a.pLastMoveAt;
        if (gap > w.gapMax) w.gapMax = gap;
        const double lateAt = 2.0 * p.moveIntervalSec > 0.25 ? 2.0 * p.moveIntervalSec : 0.25;
        if (gap > lateAt) { late = true; ++w.lateArr; }
        if (w.arrivals == 1 || dL > w.arrJmpMax) w.arrJmpMax = dL;
        w.arrJmpSum += dL;
    }
    if (haveVel)
    {
        const float lead = sqrtf(p.velX * p.velX + p.velZ * p.velZ) * leadSec * leadScale;
        if (Finite(lead) && lead > kMaxLeadUnits) ++w.leadClampFr;
    }
    if (expired) ++w.expFr;
    // the episode: from lagU rising to >= 10 until it falls below 10
    if (lagU >= 10.0f)
    {
        if (!a.epOn) { a.epOn = true; a.epStartAt = now; a.epLate = a.epArr = a.epCreep = a.epClose = 0.0f; }
        if (arrived) { a.epArr += dL; if (late && dL > 0.0f) a.epLate += dL; }
        else if (dL > 0.0f) a.epCreep += dL;
        else a.epClose += -dL;
    }
    else { a.epOn = false; a.epLate = a.epArr = a.epCreep = a.epClose = 0.0f; }
    // this frame's reads for the line, then the previous-frame values
    a.lastDes = m.des; a.lastMax = m.max; a.lastAnim = animMax; a.lastKind = mvOk ? m.kind : -1; a.lastMode = mvOk ? m.mode : -1;
    a.lastAgent = mvOk ? m.agentSt : -1;
    a.px = cur.x; a.pz = cur.z; a.pLagU = lagU; a.pWant = want; a.pStep = step; a.pRbActive = p.rbActive;
    a.pLastMoveAt = p.lastMoveAt; a.pPathIssueAt = p.pathIssueAt;
    const bool sampled = p.lagSampleAt != a.pLagSampleAt;   // WalkLagSample ran this frame (true runner or not)
    a.pLagSampleAt = p.lagSampleAt;
    if (a.due) P093Due(uid, p, a, now, lagU, ageSec, cur);
    if (sampled || a.due) { a.due = false; P093WinReset(a, now, g_tick); }
}

static std::string P093Band(const char* label, int i)
{
    return label + N2(g_p093BandN[i]) + "," + F2(g_p093BandN[i] > 0 ? (float)(g_p093BandSum[i] / (double)g_p093BandN[i]) : 0.0f)
         + "," + P093N4(g_p093BandB[i], 7);
}
std::string P093SummaryLine()
{
    unsigned int topU = 0; long long topN = 0;
    for (int i = 0; i < g_p093UidCount; ++i) if (g_p093Uids[i].n > topN) { topN = g_p093Uids[i].n; topU = g_p093Uids[i].uid; }
    const std::string k = g_p093KState == 1 ? "1:" + F3(g_p093K)
                        : g_p093KState == 2 ? "0:outOfRange:" + F3(g_p093KRaw)
                        : g_p093KState == 3 ? std::string("0:fault")
                        : g_p093KState == -1 ? std::string("0:noRow") : std::string("0:notTried");
    const long long ctl[3] = { g_p093CtlLines, g_p093CtlHvIn, g_p093CtlCpIn };
    return "[M2] RUNPROBE-SUM P093 trig=" + N2(g_p093Trig) + " lines=" + N2(g_p093Lines) + " capped=" + N2(g_p093Capped)
         + " noRead=" + N2(g_p093NoRead)
         + " hold20[TRANSIENT,F_SKIP,E_CMD,B_OFF,B_LOST,A_CAP,CLOSING,C_HAVOK,C_DETOUR,C_POS,UNK]=" + P093N4(g_p093Hold20, kP093Classes)
         + " hold40[same]=" + P093N4(g_p093Hold40, kP093Classes)
         + " open20[SNAP,NET,CREEP]=" + P093N4(g_p093Open20, 3) + " transientNet=" + N2(g_p093TransientNet)
         + " bOff[notRun,noDir,lever,aimHid]=" + P093N4(g_p093BOff, 4) + " cHavok[stopFr,endFr,pendFr]=" + P093N4(g_p093CHavok, 3)
         + " aCap[R>1.05,capped]=" + P093N4(g_p093ACap, 2) + " uids=" + N2(g_p093UidCount) + (g_p093UidOverflow > 0 ? "+" : "")
         + " topUid=" + N2(topU) + ":" + N2(topN) + " ctl[lines,hvIn,cpIn]=" + P093N4(ctl, 3) + " ctlSeen=" + N2(g_p093CtlSeen)
         + " kRead=" + k
         + " trigVis=" + N2(g_p093TrigVis) + " trigHid=" + N2(g_p093TrigHid) + " trigLost=" + N2(g_p093TrigLost)
         + " trigNoVisRd=" + N2(g_p093TrigNoVisRd)
         + " hold20Vis[same]=" + P093N4(g_p093Hold20V, kP093Classes) + " hold40Vis[same]=" + P093N4(g_p093Hold40V, kP093Classes)
         + " hold20Hid[same]=" + P093N4(g_p093Hold20H, kP093Classes) + " hold40Hid[same]=" + P093N4(g_p093Hold40H, kP093Classes)
         + " samp=" + N2(g_p093Samp) + " sampLost=" + N2(g_p093SampLost)
         + " visLag[n,mean,<1,1-5,5-10,10-20,20-40,40+,ahead]=" + N2(g_p093VisLagN) + ","
         + F2(g_p093VisLagN > 0 ? (float)(g_p093VisLagSum / (double)g_p093VisLagN) : 0.0f) + "," + P093N4(g_p093VisLagB, 7)
         + P093Band(" visLagNear[same]=", 0) + P093Band(" visLagMid[same]=", 1) + P093Band(" visLagFar[same]=", 2)
         + " nearNone=" + N2(g_p093NearNone) + " pcs[own,peer]=" + N2(g_p093PcOwn) + "," + N2(g_p093PcPeer);
}
// PROBE-END: P093

void DrivePuppet(unsigned int uid, Puppet& p)
{
    if (!PlausibleObj(p.ch)) { if (p.nativeWindow) { ++g_rcSkippedNoChar; p.rcVisPrimed = false; } return; }
    // Review 4, item 2: SafeReadPosition's own rule is PlausibleObj + IsRetiredObject + anim; this loop
    // read the position (and prone/ragdoll, and the layer gap) BEFORE asking whether the character had
    // been retired, and never asked at all outside a window. F337's recycled address is a wrong-object
    // write, so the question is asked first.
    if (IsRetiredObject(p.ch)) { ++g_driveSkippedRetired; if (p.nativeWindow) { ++g_rcSkippedRetired; p.rcVisPrimed = false; } return; }
    // mirror1 (crash T487) A - AND THE TABLE MUST HOLD THIS OBJECT LIVE UNDER THIS UID. IsRetiredObject
    // answers "not retired" for an object the table never held; T487's puppet was one, its memory had
    // become a GameData (PlausibleObj passes a GameData), and the prone snap below crashed in
    // setPositionAndTeleport. Asked before any read through p.ch beyond the vtable probe above.
    if (!coopuid::DriveDecision(FindSpawnedUid(p.ch), uid)) { ++g_driveSkippedUnregistered; if (p.nativeWindow) p.rcVisPrimed = false; return; }

    // `Character::getPosition()` itself double-dereferences `Character+0x448` (the
    // AnimationClass) with no null check of its own - the predicate at 0x7D08A0 does
    // `(**(code**)(**(longlong**)(this+0x448) + 0x48))()` on the ordinary path. Every live
    // character has one, which is why 60-odd runs have never hit it, but this call happens
    // EVERY TICK FOR EVERY PUPPET and a character being torn down is exactly when that member
    // would be null. Validated here rather than trusted.
    if (!PlausibleObj(*(void**)((char*)p.ch + kAnimationClassOff))) { if (p.nativeWindow) { ++g_rcSkippedNoAnimEarly; p.rcVisPrimed = false; } return; }

    Ogre::Vector3 cur = p.ch->worldPosition();
    float dx = p.tx - cur.x;
    float dz = p.tz - cur.z;
    float dist = sqrtf(dx * dx + dz * dz);

    // Horizontal drift drives the correction; VERTICAL drift is measured separately and
    // never folded into it. T021 flagged the trap: the drive is a ground vector, so y is
    // terrain-following and cannot be corrected by pushing - but if the probe only
    // measured x/z, a puppet on the wrong floor or ledge would report drift 0.0 and look
    // perfect. A number that reads as "distance apart" must not silently exclude an axis.
    p.lastDrift  = dist;
    p.lastDriftY = p.ty - cur.y;
    if (dist > p.maxDrift) p.maxDrift = dist;
    if (fabsf(p.lastDriftY) > fabsf(p.maxDriftY)) p.maxDriftY = p.lastDriftY;

    // POSE (read-poses): a copy in its owner's bed, seat or work pose is not moved - the owner's position is streamed
    // and the seat snap is its own. Out of path mode and stopped once. POSE-b: held only once posed - an action pose first
    // lets this drive walk the copy to within 1.6 of the owner's spot. No machine is operated; a bed pose does register
    // the copy with the bed (setBedMode's vtable +0x4F8), accepted.
    if (PoseHoldsCopy(uid))
    {
        if (p.pathMode)
        {
            p.pathMode = false; PathForgetDest(p); p.pathWantIssue = false; p.pathGrant = false;
            if (PlausibleObj(p.ch->movement) && !SafeLeavePath(p.ch->movement)) ++g_pathLeaveFaulted;
        }
        if (p.driving) { if (PlausibleObj(p.ch->movement)) SafeStopMovement(p.ch->movement); p.driving = false; }
        PoseNoteHeldFrame();
        return;
    }

    // H015 / P059 - INSIDE A NATIVE COMBAT WINDOW THE AI OWNS THE LEGS. Measured, not driven:
    // the AI is closing on the same target the authority's copy is closing on, so the two bodies
    // should track each other without a push, and any gap is the window's own drift number.
    // Correction happens ONCE, at window close (SnapPuppetToAuthority), on the authority's edge.
    if (p.nativeWindow)
    {
        if (dist > p.nativeMaxDrift) p.nativeMaxDrift = dist;
        if (p.pathMode)   // D1: the AI owns the legs in a native window. D1-b item 5: stopped, so it does not walk the old path
        {
            p.pathMode = false; PathForgetDest(p); p.pathWantIssue = false; p.pathGrant = false;
            if (PlausibleObj(p.ch->movement) && !SafeLeavePath(p.ch->movement)) ++g_pathLeaveFaulted;
        }
        if (Finite(dist)) { p.rcDriftSum += dist; ++p.rcDriftN; }
        p.driving = false;
        const double tickNow = NowSeconds();
        double frameDt = (p.rcLastTickAt > 0.0) ? (tickNow - p.rcLastTickAt) : (1.0 / 118.0);
        if (frameDt < 0.0) frameDt = 0.0;

        // The VISIBLE per-frame displacement - the hop metric - in 3D (a puppet dropped through the
        // floor is a hop; T021's XZ-only trap), and as a RATE so the oracle is frame-rate free.
        if (p.rcVisPrimed && Finite(cur.x) && Finite(cur.y) && Finite(cur.z))
        {
            const float vx = cur.x - p.rcVisLastX, vy = cur.y - p.rcVisLastY, vz = cur.z - p.rcVisLastZ;
            const float vis = sqrtf(vx * vx + vy * vy + vz * vz);
            if (Finite(vis))
            {
                if (vis > p.rcVisMaxStep) p.rcVisMaxStep = vis;
                if (vis > g_rcVisMaxStep) g_rcVisMaxStep = vis;
                if (frameDt > 0.0005)
                {
                    // decision 27 (F502): the engine hands a character its BANKED time (Character+0xC4) and integrates it in one step;
                    // a step is judged against that interval, not the render frame - the engine's own scheduler step reads as its true speed
                    float oftB = -1.0f, ftB = -1.0f; int vumB = -1; ReadCharFrameTimesPod(p.ch, &oftB, &ftB, &vumB);
                    const double bankedFt = (ftB > (float)frameDt && ftB < 5.0f) ? (double)ftB : frameDt;
                    const float rate = vis / (float)bankedFt;
                    // A downed body (prone / ragdoll) is being thrown by the engine's physics, which is
                    // not deterministic between the two instances; it is recorded, not judged.
                    const bool downed = DownedImpl(p.ch);   // review 4 item 12: the SAME predicate as the host's host_move record
                    if (rate > p.rcVisMaxRateAny) p.rcVisMaxRateAny = rate;
                    if (downed) { if (rate > p.rcVisMaxRateDowned) p.rcVisMaxRateDowned = rate; }
                    else
                    {
                        // PROBE-START: P019 - vis_peak: the frame that sets a new standing visible-rate maximum (hop investigation H041)
                        if (rate > p.rcVisMaxRate && rate > 90.0f)
                        {
                            const int proneNow = ReadProneStatePod(p.ch);
                            const double wroteAgo = ProneWriteAgoSeconds(uid, tickNow);
                            float oft = -1.0f, ft = -1.0f; int vum = -1; ReadCharFrameTimesPod(p.ch, &oft, &ft, &vum);
                            char tqpc[32]; _snprintf(tqpc, 31, "%.3f", tickNow); tqpc[31] = 0;   // review-p019b HIGH-1: QPC seconds as a double - the cross-instance join on one box (the log prefixes are per-process)
                            ++g_visPeakLines; DebugLog("[PROBE] vis_peak side=peer uid=" + N2(uid) + " tqpc=" + std::string(tqpc) + " frameDt=" + F3((float)(frameDt * 1000.0)) + "ms from=" + F2(p.rcVisLastX) + "," + F2(p.rcVisLastY) + "," + F2(p.rcVisLastZ)
                                     + " to=" + F2(cur.x) + "," + F2(cur.y) + "," + F2(cur.z) + " step=" + F3(vis) + " rate=" + F2(rate) + " prone=" + N2(proneNow) + " ragdoll=" + N2(SafeIsRagdoll(p.ch) ? 1 : 0)
                                     + " layerGapPrev=" + F3(p.rcLayerGapLast) + " proneWriteAgoMs=" + (wroteAgo < 0.0 ? std::string("none") : F2((float)(wroteAgo * 1000.0))) + " ft=" + F3(ft * 1000.0f) + "ms oft=" + F3(oft * 1000.0f) + "ms vum=" + N2(vum));
                        }
                        // PROBE-END: P019
                        if (rate > p.rcVisMaxRate) p.rcVisMaxRate = rate; if (rate > g_rcVisMaxRate) g_rcVisMaxRate = rate;
                    }
                }
            }
        }
        p.rcVisLastX = cur.x; p.rcVisLastY = cur.y; p.rcVisLastZ = cur.z; p.rcVisPrimed = true;

        // The layer gap, measured on EVERY windowed frame - prone and ragdoll included, because that
        // is where the layers are documented to diverge (F193/F314). Review 3: the first placement
        // of this counter, below the prone skip, was blind to the only cohort that motivated it.
        CharMovement* mvForGap = p.ch->movement;
        if (PlausibleObj(mvForGap) && Finite(cur.x) && Finite(cur.z))
        {
            const Ogre::Vector3 mpg = *(const Ogre::Vector3*)((const char*)mvForGap + 0xC4);
            const float gx = mpg.x - cur.x, gz = mpg.z - cur.z;
            const float layerGap = sqrtf(gx * gx + gz * gz);
            p.rcLayerGapLast = layerGap;   // PROBE P019
            if (Finite(layerGap)) { ++g_rcLayerGapFrames; if (layerGap > g_rcLayerGapMax) g_rcLayerGapMax = layerGap; if (layerGap > p.rcLayerGapMax) p.rcLayerGapMax = layerGap; }
        }

        // H016 - RECONCILE, DO NOT DRIVE. The engine teleport (0x65DEB0) is: halt -> CharMovement
        // vtbl+0xC8 = CharMovement::_setPositionSimple (0x65DDE0: writes pos and pushes it into the
        // Havok controller) -> clickHull teleport -> floor. Only the setter is made here: halt would
        // stop the AI every frame, the hull is refreshed by CharMovement::update anyway, and the
        // animation is fed from pos by that same update. The loop READS the field it WRITES
        // (CharMovement::pos, +0xC4), so the read and the write are one quantity.
        //
        // KNOWN, NOT YET ADDRESSED (review 3, finding 2): the aim point here is the last STREAMED
        // sample, not the lead-extrapolated point the ordinary drive uses below, so for a fast-moving
        // character the error floor is speed x send interval. Fighters move slowly; walkers do not.
        if (!g_reconcileOn) ++g_rcSkippedOff;
        else if (!Finite(dist) || !Finite(cur.x) || !Finite(cur.y) || !Finite(cur.z)) ++g_rcSkippedNonFiniteVisible;
        else if (IsRetiredObject(p.ch)) ++g_rcSkippedRetired;
        // T-178 crawl1: a crawler (prone 2, not a ragdoll) is reconciled like prone 0; a ragdolled prone 0 falls to the next test
        else if (!coopgetup::MoveDrivesProne(*(int*)((char*)p.ch + kProneStateOff), SafeIsRagdoll(p.ch))) ++g_rcSkippedProne;
        else if (SafeIsRagdoll(p.ch)) ++g_rcSkippedRagdoll;
        else
        {
            CharMovement* mv = p.ch->movement;
            if (!PlausibleObj(mv)) ++g_rcSkippedNoMovement;
            else
            {
                const Ogre::Vector3 mp = *(const Ogre::Vector3*)((const char*)mv + 0xC4);   // CharMovement::pos
                const float ex = p.tx - mp.x, ez = p.tz - mp.z;
                const float err = sqrtf(ex * ex + ez * ez);
                if (!Finite(mp.x) || !Finite(mp.y) || !Finite(mp.z) || !Finite(err)) ++g_rcSkippedNonFiniteField;
                else if (err <= kReconcileDeadband) ++g_rcSkippedDeadband;
                else
                {
                    if (g_rcImageBase == 0) g_rcImageBase = (uintptr_t)::GetModuleHandleA(0);
                    uintptr_t slotB8 = 0, slotC8 = 0;
                    if (!PlausibleObj(*(void***)mv) || !SafeReadVtSlots(mv, &slotB8, &slotC8)) ++g_rcSkippedVtImplausible;
                    else
                    {
                        // Verify ONCE that this vtable is CharMovement's: slot +0xB8 must resolve (through
                        // its incremental-link thunk) to the address the table gives for
                        // teleportTo. One verified object licenses every later frame -
                        // an assumption about object identity (CharMovement is the only subclass of
                        // AbstractMovementBase in the headers), stated as such.
                        if (g_rcSlotVerified == -1)
                        {
                            uintptr_t want = SafeRealAddressOfTeleport();
                            uintptr_t got  = slotB8;
                            if (!SafeResolveJmpThunk(&got, g_rcImageBase)) got = 0;   // a faulting thunk read is a mismatch
                            g_rcSlotVerified = (want != 0 && got == want) ? 1 : 0;
                            DebugLog(std::string("[H016] CharMovement vtable slot check: ") + (g_rcSlotVerified ? "OK" : "MISMATCH - reconcile refused")
                                     + " slot+0xB8=" + RcHex(got) + " expected=" + RcHex(want));
                        }
                        MvSetPosFn setter = (MvSetPosFn)slotC8;
                        const uintptr_t fa = (uintptr_t)setter;
                        if (g_rcSlotVerified != 1) ++g_rcSlotRefused;
                        else if (setter == 0 || fa < g_rcImageBase || fa - g_rcImageBase >= coop::AddrTextEnd()) ++g_rcSkippedBadSetter;   // stage 7/9: the running exe's own .text end
                        else
                        {
                            // Frame-rate free: the gain is 1 - exp(-dt/tau) and the cap is a RATE times dt,
                            // both on the FRAME interval (stamped every windowed tick, stepping or not).
                            // Elapsed skip time is never banked into one step: a correction owed from a
                            // prone stretch is paid over the following frames at tau, never at once.
                            double dt = frameDt; if (dt > kReconcileMaxDt) dt = kReconcileMaxDt;
                            const float requested = err * (1.0f - expf((float)(-dt / kReconcileTau)));
                            float rate = kReconcileMaxRate;
                            if (err > kReconcileBoostErr) { rate = kReconcileMaxRate + (err - kReconcileBoostErr) * kReconcileBoostSlope; if (rate > kReconcileMaxRateBoost) rate = kReconcileMaxRateBoost; ++g_rcBoostedSteps; ++p.rcBoostedSteps; }
                            float cap = rate * (float)dt; if (cap > kReconcileMaxStepAbs) cap = kReconcileMaxStepAbs;
                            float step = requested;
                            bool sat = false;
                            if (step > cap) { step = cap; sat = true; }
                            if (step <= 0.0f) ++g_rcSkippedZeroStep;   // a zero-length frame; normal, not a defect
                            else
                            {
                                const Ogre::Vector3 np(mp.x + (ex / err) * step, mp.y, mp.z + (ez / err) * step);
                                if (!Finite(np.x) || !Finite(np.z)) ++g_rcSkippedBadTarget;
                                else
                                {
                                    if (!SafeCallSetPos(setter, mv, &np)) ++g_rcSetterFaulted;   // review 4 item 3
                                    else
                                    {
                                        if (sat) { ++g_rcSaturated; ++p.rcSatSteps; }
                                        if (requested > p.rcReqMax) p.rcReqMax = requested;
                                        if (requested > g_rcReqMax) g_rcReqMax = requested;
                                        ++g_rcSteps; ++p.rcSteps; g_rcSumStep += step; p.rcSumStep += step;
                                        if (step > g_rcMaxStep) g_rcMaxStep = step;
                                        if (step > p.rcMaxStep) p.rcMaxStep = step;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        p.rcLastTickAt = tickNow;
        return;
    }
    if (p.nativeSnapScorePending && g_tick >= p.nativeSnapTick + 2)
    {
        // P059 / F327 - the snap is scored where the position can actually be seen: one movement
        // update later. The close runs in SessionTick and this in ReplicateTick of the SAME pump,
        // so `+2` guarantees at least one engine movement update in between (review round 2).
        // `dist` here IS the drift after the snap.
        p.nativeSnapScorePending = false;
        const std::string q(1, (char)34);
        DebugLog("[VERDICT] {" + q + "ev" + q + ":" + q + "snap_scored" + q
                 + "," + q + "uid" + q + ":" + N2(uid)
                 + "," + q + "driftAfterSnap" + q + ":" + (Finite(dist) ? F2(dist) : std::string("-2.0"))
                 + "," + q + "ticksAfter" + q + ":" + N2(g_tick - p.nativeSnapTick)
                 + "," + q + "tick" + q + ":" + N2(g_tick) + "}");
    }

    // F350 - THE AIM POINT, AND IT IS DELIBERATELY NOT THE DRIFT MEASUREMENT.
    //
    // `dist` above stays the true gap to the authority's last reported position, because that is
    // what every parity table in this project reports and what LOST, the stall window and the
    // catch-up placement are all defined against. Changing what those numbers measure while
    // changing the behaviour they score is exactly the failure `verify-what-numbers-measure`
    // exists for. The lead point below is used for ONE thing: which way, and how hard, to push
    // this frame.
    // F353 - **THE VELOCITY EXPIRES INTO "STOPPED", IT DOES NOT CLAMP INTO "STILL WALKING".**
    //
    // The first version clamped the extrapolation age at half a second and kept driving. A region
    // unload is deliberately not announced to the peer (F334), so a puppet whose stream goes quiet
    // simply stops receiving MOVEs - and with a clamp it would hold `authMoving` true forever,
    // never take the arrival stop again, and be driven every frame for the rest of the session.
    // For a prone or blocked puppet, which cannot walk out of it, that is permanent.
    //
    // Silence means stop. That is also the direction that reverts to the deployed behaviour rather
    // than inventing a new one.
    double ageSec = NowSeconds() - p.lastMoveAt;
    if (ageSec < 0.0) ageSec = 0.0;   // a clock that went backwards is not a licence to lead

    // F355 - the horizon, from the observed interval rather than a constant.
    double horizon = p.moveIntervalSec * 3.0;
    if (horizon < kVelAgeFloorSeconds) horizon = kVelAgeFloorSeconds;
    if (horizon > kVelAgeCeilSeconds)  horizon = kVelAgeCeilSeconds;

    // **THE TWO ANSWERS ARE SEPARATE, AND THAT SEPARATION IS THE WHOLE OF F355.**
    //   `haveVel`    - do we have a velocity to aim with at all?  -> positions the AIM
    //   `authMoving` - is the authority still walking RIGHT NOW?  -> suppresses the ARRIVAL STOP
    // An expired velocity answers the first yes and the second no: hold the aim where the puppet
    // already got to, and let the stop fire. F353 answered both no, which collapsed the aim back
    // to a sample the puppet had walked away from and marched it backwards.
    const bool haveVel = g_leadOn && (p.velX != 0.0f || p.velZ != 0.0f);
    const bool expired = haveVel && ageSec > horizon;
    if (expired) ++g_velExpiredFrames;
    const bool authMoving = haveVel && !expired;
    // D1-c - the OWNER's speed for the path drive: its streamed velocity (p.velX/p.velZ) while fresh - not gated on the
    // lead lever - else 0; and since when it has been stationary (p.pathOwnerStillSince, 0 = moving).
    // D2-b item 3: the SMOOTHED speed (p.ownSpd, ApplyRemoteMove) - two raw samples read 30-52 for a 27 u/s walker.
    float ownerSpeed = (p.ownPrimed && ageSec <= horizon) ? p.ownSpd : 0.0f;
    if (!Finite(ownerSpeed)) ownerSpeed = 0.0f;
    if (ownerSpeed >= kPathOwnerStillSpeed) p.pathOwnerStillSince = 0.0;
    else if (p.pathOwnerStillSince <= 0.0) p.pathOwnerStillSince = NowSeconds();

    // The lead is CLAMPED at the horizon, never dropped.
    const float leadSec = (float)(ageSec > horizon ? horizon : ageSec);
    // F358 - once expired, the held lead DECAYS to zero over `kLeadDecaySeconds` rather than being
    // frozen there. Frame-rate independent because it is computed from the age, not accumulated.
    float leadScale = 1.0f;
    if (expired)
    {
        const double over = ageSec - horizon;
        leadScale = (float)(1.0 - over / kLeadDecaySeconds);
        if (leadScale < 0.0f) leadScale = 0.0f;
        if (leadScale == 0.0f) ++g_leadDecayedOutFrames;
    }

    float aimX = p.tx, aimZ = p.tz;
    if (haveVel)
    {
        float lx = p.velX * leadSec * leadScale;
        float lz = p.velZ * leadSec * leadScale;

        // F357 - AND CLAMPED IN UNITS. The horizon is a timing quantity and was being used as a
        // distance budget: 1.8 s at the 20 fps load case is a 25.7-unit lead, and the 3.0 s ceiling
        // is 42.9 - the same number that got F350 refused. A held lead that large inflates
        // `driftXZ`, and on a fast character it trips the LOST detector on a puppet doing exactly
        // what it was told.
        const float lead2 = lx * lx + lz * lz;
        if (lead2 > kMaxLeadUnits * kMaxLeadUnits)
        {
            const float k = kMaxLeadUnits / sqrtf(lead2);
            lx *= k; lz *= k;
            ++g_leadDistClampedFrames;
        }

        aimX += lx;
        aimZ += lz;
        ++g_leadAppliedFrames;
    }

    // A follow copy aims at its owner's replayed route (FollowFrame) instead of the guessed-ahead point above. An idle one
    // (followplay::ReplayIdle - a standing NPC on its owner's standing point) takes only the arrival stop below, so its replay
    // is skipped: the per-frame cost of the follow drive grows with the copies that move, not with every copy in the area.
    // The owner moves only while its newest report moves AND has not expired (followplay::OwnerMovingNow, the horizon above,
    // not gated on the lead lever): an owner game does not announce every character it stops streaming, so a copy whose
    // owner went quiet mid-walk walks to the owner's newest point and goes idle there.
    // The stop's arrival radius (followplay::StopArriveRadius): the narrow one for a player's copy still closing on the stop
    // point, the wide one for an NPC copy or a copy that has not closed on it for followplay::kStopStallSec (bodies round the
    // spot), so a copy that cannot reach the exact spot is not driven against the crowd for good.
    if (p.follow)
    {
        const double fnow = NowSeconds();
        const bool ownerMovingNowF = followplay::OwnerMovingNow(p.trk, ageSec, horizon);
        if (!ownerMovingNowF && p.trk.n > 0 && !followplay::TrackNewest(p.trk).still) ++g_followOwnerExpired;
        bool stopStalled = false;
        if (p.stopHeld)
        {
            const float sdx = p.stopX - cur.x, sdz = p.stopZ - cur.z;
            stopStalled = followplay::StopProgressStep(&p.fStopProg, sqrtf(sdx * sdx + sdz * sdz), fnow);
        }
        else followplay::StopProgressReset(&p.fStopProg);
        if (stopStalled && !p.fStopStalled && p.followPlayer) ++g_followStopWide;
        p.fStopStalled = stopStalled;
        p.fStopArrive = followplay::StopArriveRadius(p.followPlayer, stopStalled);
        const bool ownerMovingF = authMoving || ownerMovingNowF;
        const float arriveF = p.stopHeld ? p.fStopArrive : kArriveDist;
        const double sinceMovingF = p.fLastMovingAt > 0.0 ? fnow - p.fLastMovingAt : 1.0e9;
        if (followplay::ReplayIdle(ownerMovingF, p.pathMode, dist, arriveF, sinceMovingF, p.followD)) FollowIdleFrame(p, fnow, &aimX, &aimZ);
        else FollowFrame(p, cur, ownerMovingNowF, &aimX, &aimZ);
    }

    // walk1 (review-walk1): the walking-lag histogram, sampled here - before the arrival return and every drive branch - so
    // the copies that track best are counted too. Only while the owner is moving (smoothed speed, fresh).
    if (p.lagSampleAt == 0.0 || NowSeconds() - p.lagSampleAt >= 1.0)
    {
        p.lagSampleAt = NowSeconds();
        bool lagDirOk = false;
        const float lagNow = SignedAimLag(p, cur, aimX, aimZ, &lagDirOk);
        WalkLagSample(p, lagNow, lagDirOk && ownerSpeed > 2.0f);
        // PROBE-START: P093
        P093OnSample(p, lagNow, lagDirOk && ownerSpeed > 2.0f);
        // PROBE-END: P093
    }
    // PROBE-START: P093
    P093Frame(uid, p, cur, aimX, aimZ, ageSec, horizon, expired, haveVel, leadSec, leadScale, ownerSpeed);
    // PROBE-END: P093

    CharMovement* mv = p.ch->movement;
    if (!PlausibleObj(mv)) return;
    // T-178 crawl1: every frame from here drives (or stops) the copy - count those on a crawler
    if (*(int*)((char*)p.ch + kProneStateOff) == coopgetup::kProneCrawling && !SafeIsRagdoll(p.ch)) ++g_crawlDriven;

    // P023 CONTROL, and it must be read BEFORE the arrival return.
    //
    // T063 found it: the control was placed after the `dist <= kArriveDist` early return, so a
    // puppet sitting exactly on its target - which is the NORMAL state - never reached it. The
    // control only began printing ~150 s in, once the characters started moving, and came within
    // seconds of being absent from the run that needed it. A control that only fires when things
    // are already going wrong is not a control.
    {
        static long long s_lastControl = -100000;
        int proneNow = *(int*)((char*)p.ch + kProneStateOff);
        if (proneNow == 0 && g_tick - s_lastControl > 2400)   // ~20 s at 118 fps
        {
            s_lastControl = g_tick;
            LayerProbe(uid, p.ch, cur, proneNow, "standing-control");
        }
    }

    // F327 - **SCORE AN OUTSTANDING PLACEMENT BEFORE THE ARRIVAL RETURN, NOT AFTER IT.**
    //
    // The scoring used to live with the rest of the P037 window bookkeeping, below the
    // `dist <= kArriveDist` early return. **A placement that worked perfectly puts the puppet ON its
    // target**, so the very next window took that return and the pending score was never settled -
    // `catchupEffective` could only ever be incremented when the placement had FAILED to close the
    // gap. T091 made it visible: 3 placements, all of which demonstrably closed their gap, scored
    // `catchupEffective=0` AND `catchupIneffective=0`.
    //
    // T089 hid it because the authority kept moving, so drift re-opened before the next window and
    // the scoring was reached. **The counter's meaning depended on whether the other machine
    // happened to be walking.**
    //
    // **THIRD TIME IN THIS PROJECT.** F195/T063 put the P023 control after this same return and it
    // only ever fired once things were already going wrong; F125 -> F177 is the same shape again.
    // A check placed after an early return does not measure the case the return represents - and
    // here the case the return represents IS SUCCESS.
    if (p.catchupPending && g_tick - p.progressCheckTick >= kProgressWindowTicks)
    {
        float sdx = cur.x - p.catchupFromX;
        float sdz = cur.z - p.catchupFromZ;
        float movedSinceSnap = sqrtf(sdx * sdx + sdz * sdz);

        p.catchupPending = false;
        const int cause = (p.snapCause == kSnapCauseFar || p.snapCause == kSnapCauseStuck) ? p.snapCause : 0;
        if (movedSinceSnap >= p.catchupDriftBefore * kCatchupMinEffect)
        {
            p.catchupTries = 0;   // review-r2 item 1: an effective snap still leaves the strike watch running
            ++g_catchupEffective;
            ++g_snapEffectiveBy[cause];
        }
        else if (++p.catchupTries >= kCatchupMaxTries)
        {
            p.catchupGaveUp = true;
            ++g_catchupGaveUp;
            ++g_snapGaveUpBy[cause];
            // review-r2 item 5: the old text said "this one reported standing" - nothing re-reads that here. What
            // is known is that the snap gates declined prone and ragdolled copies AT PLACEMENT.
            ErrorLog("[M2] catch-up GIVING UP on uid=" + N2(uid) + " after "
                     + N2(kCatchupMaxTries) + " placements (the last a " + SnapCauseName(cause)
                     + " snap) - THE PUPPET ITSELF moved "
                     + F2(movedSinceSnap) + " units against a requested "
                     + F2(p.catchupDriftBefore) + ". The position write is not propagating for"
                     " this character, which is a DIFFERENT result from 'it cannot walk there'"
                     " and must not be read as one (F314: for a ragdolled character the"
                     " write-back is skipped entirely - this one was neither prone nor ragdolled"
                     " when it was placed; its state now is not re-read).");
        }
        else { ++g_catchupIneffective; ++g_snapIneffectiveBy[cause]; }
    }

    // A follow copy whose owner stated its stop arrives within that stop's radius (p.fStopArrive, set above): a player's copy
    // is landed on the spot rather than within the general radius, unless it stopped closing on it.
    const float arriveDist = (p.follow && p.stopHeld) ? p.fStopArrive : kArriveDist;
    if (dist <= arriveDist)
    {
        /* recheck-r2b item 1: an arrived copy restarts its progress window here, so the first window judged after it
           moves on measures from NOW - not from a stale edge that could read a healthy copy as stalled - and an ended
           unstick order or a snap's strike watch cannot outlive the arrival. */
        p.unstickEnded = false; p.snapWatchWindows = -1;
        p.progressCheckTick = g_tick; p.progressFromX = cur.x; p.progressFromZ = cur.z;
        // D1-c item 1: a copy in path mode whose owner is still moving stays on its path however close it gets.
        // D2-b item 3: the owner's smoothed motion, still only after 0.3 s - one zero (rejected) sample no longer ends the path.
        // A follow copy: while its owner's newest report moves and no stop is stated (FollowFrame).
        const bool keepPath = g_pathDrive && p.pathMode && (p.follow ? p.fMoving : OwnerMovingConfirmed(p, ownerSpeed, NowSeconds()));
        if (p.pathMode && !keepPath) PathLeave(uid, p, mv, dist, "arrived", !authMoving);   // D1: within kArriveDist the push or the stop owns it
        p.progressFromDist = dist; p.progressFromTX = p.tx; p.progressFromTZ = p.tz;
        // F350 - **ARRIVED AT WHAT?** Until now this branch answered "within 1.5 units of a
        // sample that is up to 100 ms old", and stopped the character dead. With the authority
        // moving 1.455 units between updates, that condition is true most of the time for a
        // puppet that is tracking correctly, so the correct-looking puppets were the stuttering
        // ones (P-20).
        //
        // Now the stop is caused by the AUTHORITY BEING STOPPED. If it is still walking we fall
        // through and keep driving - at the lead point, which is ahead of us - and the character
        // walks continuously the way the engine expects to animate it.
        if (!authMoving)
        {
            // Stop explicitly - F062: ceasing to push does not stop a suppressed character, it
            // keeps walking on the last vector we gave it.
            ++g_arriveStoppedFrames;
            if (p.driving)
            {
                mv->steerDirectly(Ogre::Vector3(0.0f, 0.0f, 0.0f), 0.0f);
                p.driving = false;
            }
            // Review 4 item 10: desiredSpeed is a LEVEL, and the drive only writes it while pushing, so a
            // stopped puppet kept whatever the last push wrote. While stopped, the level follows the
            // authority's own (which is what the host's copy carries) - written once per change, not per frame.
            if (g_driveSpeed == -2.0f && Finite(p.authDesired) && p.authDesired >= 0.0f && p.authDesired != p.authDesiredApplied)
            {
                if (RestoreDesiredSpeed(mv, p.authDesired)) { p.authDesiredApplied = p.authDesired; ++g_speedArriveSynced; }
            }
            ApplyFacingIfStationary(p, mv, cur);   // H024: a stopped copy faces where its authority faces
            return;
        }
        // F353 - AND RETURN, exactly as the deployed build does from this branch. Falling through
        // would run the progress window, the catch-up placement and the LOST detector on frames
        // where they have never run, changing what `stalledWindows` and `catchupSnaps` count and
        // making the two arms of the run incomparable.
        // A follow copy near its owner's newest report still takes the follow drive: its never-past check runs every frame.
        if (keepPath || (p.follow && g_pathDrive))
        {
            const int proneA = *(int*)((char*)p.ch + kProneStateOff);
            if (CopyPathStep(uid, p, mv, cur, dist, aimX, aimZ, proneA, ownerSpeed)) return;
        }
        ++g_arriveHeldFrames;
        DriveTowardAim(uid, p, mv, cur, dx, dz, dist, aimX, aimZ, leadSec);
        return;
    }

    // A prone puppet is pushed and does not move (F184). Correct it by position instead, but
    // only when the gap is worse than the teleport would be.
    int prone = *(int*)((char*)p.ch + kProneStateOff);
    // T-178 crawl1 (H050): a crawler (prone 2, not a ragdoll) moves by the same CharMovement update as a walker, so MOVE
    // drives it exactly like prone 0 - no prone snap; the stuck / far placements apply to it as to prone 0 (decision 60).
    const bool moveDrives = coopgetup::MoveDrivesProne(prone, prone == coopgetup::kProneCrawling && SafeIsRagdoll(p.ch));

    if (!moveDrives && dist > kProneSnapDist && !p.snapGaveUp)
    {
        // review-k1 item 7: a carried copy (+0x3D4) follows its carrier - no prone snap, as the far/stuck snaps.
        CharDbg cdc;
        if (ReadCharDbgFields(p.ch, &cdc) && cdc.carried != 0) { ++g_proneSnapSkippedCarried; return; }
        // R3 (read-ragdoll) item 5: a RAGDOLLED copy's position lives only in its physics bodies, and the teleport
        // (0x65DEB0) cannot move one - its rest position comes from the owner (RestApplyTick). The snap stays for a
        // prone copy that is not ragdolled. The give-up flags are unchanged.
        // R3-b (review-r3 item 3): only while the R3 hook is installed - without it nothing else would move the body,
        // so the snap stays as before R3, said once in the log.
        if (RagdollActive(p.ch))
        {
            if (RagdollRestHookArmed()) { ++g_proneSnapSkippedRagdoll; return; }
            static bool s_notedNoRestHook = false;
            if (!s_notedNoRestHook)
            {
                s_notedNoRestHook = true;
                ErrorLog("[R3] rest hook NOT installed - a ragdolled copy's prone snap is KEPT, as before R3 (first uid="
                         + N2(uid) + ")");
            }
        }
        // Did the LAST correction MOVE THE PUPPET? Not "did the gap shrink" - F195. The
        // authority moves too, so the gap is not evidence about anything we did.
        if (p.snapPending)
        {
            float mdx = cur.x - p.snapFromX;
            float mdz = cur.z - p.snapFromZ;
            float moved = sqrtf(mdx * mdx + mdz * mdz);
            if (moved < kSnapMinProgress)
            {
                if (++p.snapTries > kSnapMaxTries)
                {
                    p.snapGaveUp = true;
                    ErrorLog("[M2] prone snap GIVING UP on uid=" + N2(uid)
                             + " after " + N2(kSnapMaxTries)
                             + " corrections - THE PUPPET ITSELF moved " + F2(moved)
                             + " units (drift " + F2(p.snapLastDrift) + " -> " + F2(dist)
                             + ", which the authority can change on its own and is NOT the"
                             + " test). The correction is not working; not retrying (F195).");
                    return;
                }
            }
            else p.snapTries = 1;   // the puppet really moved - reset the strike count
        }
        else p.snapTries = 1;
        p.snapLastDrift = dist;
        p.snapFromX = cur.x; p.snapFromZ = cur.z; p.snapPending = true;

        LayerProbe(uid, p.ch, cur, prone, "prone");

        // F189, ATTEMPT 2. Attempt 1 used `Character::relocationTeleport` and it moved nothing
        // - T061 ran it 22,375 times on one character over 168 s while its position stayed
        // BYTE-IDENTICAL. That function shifts a relocation ACCUMULATOR at Character+0x48 and
        // nudges a render node; it is for shifting a zone, not for placing a character, and
        // `getPosition()` never changed.
        //
        // `CharMovement::_setPositionAndTeleport` is the real one, and its decompile is much
        // safer than the function it replaces: it VALIDATES the position and does nothing if it
        // is bad, it null-checks both pointers it touches, and it only writes the floor index
        // when the argument is >= 0 - so passing -1 for "leave the floor alone" is an explicitly
        // supported case, not a value we are hoping is ignored.
        //
        // `mv` is already validated above by the drive path.
        Ogre::Vector3 target(p.tx, p.ty, p.tz);

        // *** ALL THREE ATTEMPTS HAVE NOW FAILED. DO NOT WRITE A FOURTH. ***
        //
        // T065: the address guard passed (`resolves to rva 0x5b1070`), so the right function was
        // called, and `anim+0x98` moved 0.1 units in response to three requests of 5.8-5.9 units.
        // Three different layers - a zone accumulator (F189), the logical position (F193) and
        // the animation scene node (F203) - have each been written and measured, and none of
        // them moves a downed character.
        //
        // What remains here is NOT a fix. It is a self-limiting probe: it tries three times per
        // puppet, measures the puppet's own displacement, says so once, and stops. Left in place
        // because it costs 3 calls and would report immediately if the engine ever behaved
        // differently. **P-14 is open and belongs to localization, not to another attempt.**
        //
        // The one candidate explanation nobody has tested: `AnimationClass::setPosition` does
        // nothing unless `isActivated` (+0x88) is set and `node` (+0xB0) is non-null, and this
        // probe prints the RESULT of the write but never those two PRECONDITIONS - so a
        // silently-skipped call and an overridden write look identical here.
        //
        // ATTEMPT 3, and the FIRST one aimed at a layer that was measured rather than assumed.
        //
        // T063 settled it. The standing control read `agrees=logical` 8 times out of 8, so the
        // probe and the offsets are sound. While prone, `getPosition()` returned `anim+0x98`
        // instead - and the decisive detail is visible inside ONE character's three samples
        // 7 ms apart: between try 1 and try 2 the logical field at `Character+0x48` CHANGED,
        // which is this corrective's write landing, and `getPosition()` did not follow it.
        // The write was never failing. The read was somewhere else.
        //
        // So both layers are written now:
        //   * the logical position, so the character is in the right place when it STANDS UP;
        //   * the animation scene node, which is what the position reported while it is DOWN
        //     is derived from.
        //
        // The attempt cap was set at two "until a probe CONFIRMS the mechanism in game". That
        // was the release condition and T063 met it. This is not a third guess - it is the
        // first attempt with a measurement behind it.
        P071NotePlacement(uid, p.ch, 0, target.x, target.y, target.z);   // PROBE P071
        mv->teleportTo(target, -1);
        PathForgetDest(p);   // D1 item 3

        // F037 cost two runs to a function picked by NAME that resolved to a trivial float
        // setter. `resolve_stub.py` says this symbol is slot 622 -> real RVA 0x5B1070, and that
        // address decompiles to the node setter, so the mapping is verified offline. Logged ONCE
        // anyway, so the run itself carries the evidence instead of relying on my notes.
        AnimationClass* anim = *(AnimationClass**)((char*)p.ch + kAnimationClassOff);
        if (PlausibleObj(anim))
        {
            static bool s_loggedAddr = false;
            if (!s_loggedAddr)
            {
                s_loggedAddr = true;
                LogSetPositionAddress();
            }
            anim->setPosition(target);
        }

        ++g_proneSnaps;
        DebugLog("[M2] prone SNAP uid=" + N2(uid) + " prone=" + N2(prone)
                 + " by " + F2(dist) + " units, try " + N2(p.snapTries)
                 + " (walking cannot correct a downed puppet, F184)");
        p.driving = false;
        p.pronedAt = prone;
        return;
    }

    // P037 / F315. Everything here is measurement; the corrective at the end is gated on `catchup`,
    // which defaults ON since decision 60 (user 2026-09-22); `catchup off` restores measurement only.
    //
    // **The SCORING half of this now runs above the arrival return (F327).** Leaving it here made
    // the counter mean the opposite of its name.
    bool windowRolled = false;   // decision 60: the far snap acts at most once per window, at its edge
    bool afterUnstickStalled = false;   // review-r2 item 2: stalled in the first window judged after an unstick ended
    if (p.progressCheckTick == 0)
    {
        p.progressCheckTick = g_tick;
        p.progressFromX = cur.x; p.progressFromZ = cur.z;
        p.progressFromDist = dist;
        p.progressFromTX = p.tx; p.progressFromTZ = p.tz;
    }
    else if (g_tick - p.progressCheckTick >= kProgressWindowTicks)
    {
        float mdx = cur.x - p.progressFromX;
        float mdz = cur.z - p.progressFromZ;
        float movedThisWindow = sqrtf(mdx * mdx + mdz * mdz);

        // Item 5 (T250 log-B, Confirmed): 75% of LOST copies were SLIDING along an obstacle only
        // this game has, at ~2.5 units/s - ~5 units a window, so "moved < kProgressMin" never fired
        // and neither the unstick nor the stuck snap ran. A window also counts as stalled when the
        // gap is above kCatchupEnter (20, far above the ~1.5 steady lag) and did not shrink by
        // kProgressMin - so a normally following copy is never stalled by this.
        // review-r2 item 3: ... and only when the copy moved mostly SIDEWAYS. A copy chasing its owner at the
        // owner's own speed also fails to close the gap, but it moves straight at the target (ratio ~1); a copy
        // sliding along an obstacle moves across it (ratio ~0). Unjudgeable geometry counts as chasing.
        windowRolled = true;
        float closedThisWindow = p.progressFromDist - dist;
        bool notMoving  = movedThisWindow < kProgressMin;
        float towardRatio = 1.0f;
        {
            float tdx = p.progressFromTX - p.progressFromX;
            float tdz = p.progressFromTZ - p.progressFromZ;
            float tlen = sqrtf(tdx * tdx + tdz * tdz);
            if (tlen > 0.001f && movedThisWindow > 0.001f)
                towardRatio = (mdx * tdx + mdz * tdz) / (tlen * movedThisWindow);
        }
        bool notClosing = !notMoving && dist > kCatchupEnter && closedThisWindow < kProgressMin
                          && towardRatio < kNotClosingTowardMax;
        // D1: a path-driven copy is judged too. D1-b item 3: but NOT by the sideways (notClosing) rule - a detour round a
        // building is exactly that; it is stalled only when it moved < kProgressMin AND no path is pending.
        bool stalled = p.pathMode ? (notMoving && !PathPending(p, NowSeconds()) && dist > kArriveDist)
                                  : ((notMoving || notClosing) && p.driving && dist > kArriveDist);
        if (stalled) { ++p.stalledWindows; ++g_stalledWindows; if (notClosing) ++g_stalledNotClosing; }
        else p.stalledWindows = 0;

        // review-r2 item 2: with orderdrive on, the first stalled window issues an unstick order, which zeroes
        // stalledWindows and leaves p.driving false while it runs - so stalledWindows never reached
        // kStalledWindows and the stuck snap almost never fired. Still stalled in the first window judged after
        // the order ended = the unstick did not work; that fires the stuck snap below. The unstick step stays.
        if (p.unstickEnded) { afterUnstickStalled = stalled; p.unstickEnded = false; }

        // review-r2 item 1: THE STRIKE WATCH. A snapped copy that is lost or stalled again within
        // kSnapStrikeWindows judged windows earns one strike per snap, even if the snap itself moved it.
        if (p.snapWatchWindows >= 0)
        {
            ++p.snapWatchWindows;
            if (stalled || dist > kLostDist)
            {
                p.snapWatchWindows = -1;
                ++p.snapStrikes; ++g_snapStrikes;
                if (p.snapStrikes >= kSnapMaxStrikes && !p.catchupGaveUp)
                {
                    const int sc = (p.snapCause == kSnapCauseFar || p.snapCause == kSnapCauseStuck) ? p.snapCause : 0;
                    p.catchupGaveUp = true;
                    ++g_catchupGaveUp;
                    ++g_snapGaveUpBy[sc];
                    ErrorLog("[M2] catch-up GIVING UP on uid=" + N2(uid) + ": " + N2(kSnapMaxStrikes)
                             + " strikes, the last after a " + SnapCauseName(sc) + " snap - each time the copy was "
                             + (stalled ? "stalled" : "lost") + " again within " + N2(kSnapStrikeWindows)
                             + " windows of being placed, so placing it does not keep it with its owner (review-r2 item 1)");
                }
            }
            else if (p.snapWatchWindows >= kSnapStrikeWindows) p.snapWatchWindows = -1;
        }

        p.progressCheckTick = g_tick;
        p.progressFromX = cur.x; p.progressFromZ = cur.z;
        p.progressFromDist = dist;
        p.progressFromTX = p.tx; p.progressFromTZ = p.tz;

        // THE CORRECTIVE. Not "the gap is big" - "we pushed this puppet for two full windows and it
        // went nowhere", which is the thing a walk command cannot fix.
        // F324 - **NEVER PLACE A RAGDOLLED PUPPET.** T090 produced the project's first
        // `catchupGaveUp` (3), and all five puppets placement never fixed report
        // `chRagdoll=1 mvBlock=RAGDOLL-locomotion+posWriteback-both-skipped` - the probe's own F314
        // branch. A ragdolled character skips BOTH `movement->update` AND the write-back of the
        // movement position into `Character+0x48`, so a position written here cannot propagate. It
        // is not that placement failed; it is that placement CANNOT WORK on that character, and the
        // three capped prone-snap attempts already paid for learning it.
        //
        // Two of the three give-ups were at requested distances of 3.6 and 2.3 units, so the budget
        // was being spent on characters we already knew were unreachable - and, worse, the run then
        // reported "gave up" for a case that is fully explained. **A corrective that cannot work
        // must decline, not fail.**
        bool ragdolled = false;
        {
            void* anim = *(void**)((char*)p.ch + kAnimationClassOff);
            if (PlausibleObj(anim)) ragdolled = p.ch->inRagdoll();
        }
        if (ragdolled) ++g_catchupSkippedRagdoll;

        // review-r2: also fired by item 2 (still stalled after an unstick order ended); declined by the shared
        // gates - stale owner sample (item 8), carried / caged / in a bed or building slot (item 6), rate cap (item 1).
        const bool wantStuck = g_catchupOn && (p.stalledWindows >= kStalledWindows || afterUnstickStalled)
                               && !p.catchupGaveUp && moveDrives && !ragdolled;   // T-178 crawl1: prone 0 or a crawler
        if (wantStuck && SnapSharedGate(p, ageSec) == 0)
        {
            const bool byUnstick = afterUnstickStalled && p.stalledWindows < kStalledWindows;
            if (byUnstick) ++g_snapStuckAfterUnstick;
            p.stalledWindows = 0;
            NoteSnapPlaced(p, kSnapCauseStuck);
            p.catchupPending = true;
            p.catchupDriftBefore = dist;
            p.catchupFromX = cur.x; p.catchupFromZ = cur.z;
            ++g_catchupSnaps;

            // `CharMovement::teleportTo` (0x65DEB0, F309 - NOT 0x5B1070, which is
            // `AnimationClass::setPosition`). Its decompile is self-guarding: it calls `halt()`,
            // VALIDATES the vector and does nothing if it is bad, null-checks both pointers it
            // touches, and writes the floor index only when the argument is >= 0 - so -1 is an
            // explicitly supported "leave the floor alone".
            //
            // It halting first is not a side effect to work around: it clears our STEER_BY_DIRECTION
            // mode, and the next frame's drive re-applies it. That is the correct order - place,
            // then resume pushing from the new position.
            // A follow copy is placed on its owner's point (followplay::OwnerPoint: the stated stop point, else the owner's
            // newest reported point) - the point this snap's distance is measured to - never on a replay target its owner
            // has left behind. Placed there, the watch below judges it against that same point, so the gap this snap closed
            // is not charged again: only a new one is.
            float plX = p.tx, plZ = p.tz;
            if (p.follow)
            {
                followplay::OwnerPoint(p.stopHeld, p.stopX, p.stopZ, p.tx, p.tz, &plX, &plZ);
                ++g_followSnapOwnerPt[0];
                if (p.fBehind) ++g_followSnapBehind;
            }
            P071NotePlacement(uid, p.ch, 1, plX, p.ty, plZ);   // PROBE P071
            mv->teleportTo(Ogre::Vector3(plX, p.ty, plZ), -1);
            PathForgetDest(p);   // D1 item 3 (snap 1)
            p.fHeld = false;

            DebugLog("[M2] catch-up SNAP uid=" + N2(uid) + " by " + F2(dist)
                     + (byUnstick ? std::string(" units (still stalled in the first window after an unstick order ended,")
                                  : " units (pushed for " + N2(kStalledWindows * kProgressWindowTicks) + " ticks,")
                     + " moved " + F2(movedThisWindow) + " in the last window). A large distance here is the"
                     " SIGNAL the user asked for, not a cost being hidden - F313 is the"
                     " investigation behind it.");
            p.driving = false;
            return;
        }
    }

    if (dist > kLostDist)
    {
        // Too far to walk back plausibly. DECISION 60 (user 2026-09-22, docs/authority-model.md):
        // the copy is TELEPORTED to its owner's position, with the stuck snap's call and safety -
        //   * never when prone: the prone snap above owns downed puppets (F184);
        //   * never when ragdolled: the write cannot propagate on that character (F314/F324), so
        //     placement must DECLINE, not fail;
        //   * never once `catchupGaveUp` is set: F189 - a corrective that does not work must stop.
        //   * never when `catchup` is off: the same lever as the stuck snap - off = measurement only (review-r2 4);
        //   * never into an area this game has not loaded - the withdraw path's own live test (review-r2 7);
        //   * never on a stale owner sample, an attached copy, or past the rate cap (review-r2 8/6/1).
        // It sets catchupPending/DriftBefore/From, so the did-it-work check above scores it a full
        // window later and gives up after kCatchupMaxTries placements that do not move the copy.
        // At most one far snap per puppet per progress window (it acts only at the window edge).
        bool farRagdolled = false;
        {
            void* anim = *(void**)((char*)p.ch + kAnimationClassOff);
            if (PlausibleObj(anim)) farRagdolled = p.ch->inRagdoll();
        }
        const char* whyNot = 0;
        if (!g_catchupOn)           whyNot = "catchup is off - measurement only, nothing is corrected";
        else if (!moveDrives)       whyNot = "prone - the prone snap owns downed copies (F184)";   // T-178 crawl1: a crawler is placed
        else if (farRagdolled)      whyNot = "ragdolled - a placement cannot propagate (F314/F324)";
        else if (p.catchupGaveUp)   whyNot = "gave up - earlier placements did not move or keep this copy (F189, review-r2 1)";

        if (windowRolled && moveDrives && g_catchupOn)   // T-178 crawl1
        {
            if (farRagdolled) ++g_snapFarSkippedRagdoll;
            else if (p.catchupGaveUp) ++g_snapFarSkippedGaveUp;
        }

        if (whyNot == 0 && windowRolled && !p.catchupPending)
        {
            // Asked only here, at a window edge, so the engine is not queried every frame a copy is lost.
            if (!IsPositionLoadedHere(p.tx, p.ty, p.tz))
            {
                ++g_snapFarSkippedUnloaded;
                whyNot = "the owner's target is in an area this game has not loaded (review-r2 7)";
            }
            else whyNot = SnapSharedGate(p, ageSec);
        }

        if (whyNot == 0 && windowRolled && !p.catchupPending)
        {
            // review-r2 item 9: the copy is being placed - an unstick order still holding the push is cancelled.
            if (p.unsticking)
            {
                p.unsticking = false; p.orderActive = false; ClearAttackOrder(p.ch);
                ++g_snapFarCancelledUnstick;
            }
            p.unstickEnded = false;
            p.stalledWindows = 0;
            NoteSnapPlaced(p, kSnapCauseFar);
            p.catchupPending = true;
            p.catchupDriftBefore = dist;
            p.catchupFromX = cur.x; p.catchupFromZ = cur.z;
            ++g_snapFar;
            if (g_snapFarByUid.size() < kSnapFarByUidMax || g_snapFarByUid.find(uid) != g_snapFarByUid.end())
                ++g_snapFarByUid[uid];   // T-191
            else ++g_snapFarByUidOther;   // runboost2 (T-195 R6): the map is full

            // Same call as the stuck snap - see there for why it is safe (self-guarding, -1 = floor
            // left alone) and why its halt() is the right order.
            // A follow copy: on its owner's point (followplay::OwnerPoint), as the stuck snap.
            float plX = p.tx, plZ = p.tz;
            if (p.follow)
            {
                followplay::OwnerPoint(p.stopHeld, p.stopX, p.stopZ, p.tx, p.tz, &plX, &plZ);
                ++g_followSnapOwnerPt[1];
                if (p.fBehind) ++g_followSnapBehind;
            }
            P071NotePlacement(uid, p.ch, 2, plX, p.ty, plZ);   // PROBE P071
            mv->teleportTo(Ogre::Vector3(plX, p.ty, plZ), -1);
            PathForgetDest(p);   // D1 item 3 (snap 2)
            p.fHeld = false;

            if (g_snapFar <= 16)
                ErrorLog("[M2] far SNAP #" + N2(g_snapFar) + " uid=" + N2(uid) + " by " + F2(dist)
                         + " units (> " + F2(kLostDist) + ") to the owner's target ("
                         + F2(p.tx) + ", " + F2(p.ty) + ", " + F2(p.tz) + ") - decision 60");
            p.driving = false;
            return;
        }

        if (whyNot != 0 && g_tick - p.lastLostTick > 600)   // ~5 s at 118 fps, PER PUPPET
        {
            p.lastLostTick = g_tick;
            ErrorLog("[M2] puppet uid=" + N2(uid) + " LOST: drift " + F2(dist)
                     + " exceeds " + F2(kLostDist) + " - NOT teleported: " + whyNot);
        }
    }

    // H024 - UNSTICK: the push is the primary drive; when the P037 progress window says the copy is going
    // nowhere, hand it ONE Move order so the engine paths it around the obstacle, hold the push while that
    // order runs (up to ~5 s or until within reach of the aim), then push again.
    // D1 - pathdrive (default ON since D1-c) replaces the H024 unstick: a copy far from its aim, or stalled, walks there by the
    // engine's own pathfinder; the push only within kPathLeave..kPathEnter. The H024 order is the fallback when off.
    if (g_pathDrive)
    {
        if (p.unsticking) { p.unsticking = false; p.orderActive = false; p.unstickEnded = false; ClearAttackOrder(p.ch); }   // lever switched on mid-order
        if (CopyPathStep(uid, p, mv, cur, dist, aimX, aimZ, prone, ownerSpeed)) return;
    }
    else if (g_orderDrive)
    {
        if (p.unsticking)
        {
            // review-r2 item 2: either way the order has ENDED - the next judged window decides whether it worked.
            if (dist < kUnstickDoneDist) { p.unsticking = false; p.orderActive = false; p.unstickEnded = true; ++g_unstickResolved; }
            else if (g_tick - p.unstickTick > kUnstickHoldTicks) { p.unsticking = false; p.orderActive = false; p.unstickEnded = true; ++g_unstickTimedOut; ClearAttackOrder(p.ch); }
            else { ++g_unstickHeldFrames; return; }
        }
        else if (p.stalledWindows >= 1 && moveDrives)   // T-178 crawl1
        {
            if (p.driving) { SafeStopMovement(mv); p.driving = false; }
            if (OrderDriveStep(uid, p, mv, cur, dist, aimX, p.ty, aimZ)) { p.unsticking = true; p.unstickTick = g_tick; p.stalledWindows = 0; ++g_unstickOrders; return; }
        }
    }
    DriveTowardAim(uid, p, mv, cur, dx, dz, dist, aimX, aimZ, leadSec);
}

} // namespace

bool ReplicateStart(unsigned int uid)
{
    Character* c = GetTarget();
    if (c == 0)
    {
        ErrorLog("[M2] replicate refused: no captured character - run 'watchplayer' first");
        return false;
    }
    g_ownedUid  = uid;
    g_ownedChar = c;
    DebugLog("[M2] replicating uid=" + N2(uid) + " at ~10 Hz (unreliable, latest-wins)");
    return true;
}

void ReplicateStop()
{
    if (g_ownedUid)
        DebugLog("[M2] stopped replicating uid=" + N2(g_ownedUid));
    g_ownedUid  = 0;
    g_ownedChar = 0;
}

static void P115OnMove(unsigned int uid, float x, float z, float vx, float vz);   // PROBE P115: defined in the P115 block above ReplicateTick
// PROBE-START: P124
static void P124OnMove(unsigned int uid, const Puppet& p, float x, float z, float vx, float vz);
// PROBE-END: P124
void ApplyRemoteMove(unsigned int uid, float x, float y, float z, float velX, float velZ, float authDesiredSpeed, float faceX, float faceZ,
                     bool stamped, unsigned int stampMs)
{
    // F340 - **THE INGEST GATE.** T092 ended with five puppets permanently carrying
    // `mvDirLen=INF` and `mvBlock=DIRNAN-desiredMotion-is-not-a-number`: the engine's own motion
    // vector was non-finite, the drive refused them for the rest of the run, and nothing could say
    // where the value came from.
    //
    // There are only two sources - the target we were sent, or the position we read locally - and
    // this gate splits them. A non-finite target is rejected HERE, before it can be stored, so if
    // the drive still meets a non-finite value afterwards, the corruption is provably local. Both
    // halves are counted; neither is inferred.
    //
    // Rejecting is also the correct behaviour on its own merits: a stored non-finite target
    // poisons every later frame for that puppet, because the target is a LEVEL, not an event.
    // F357 - the MAGNITUDE gate runs on the peer too, not only where the value is measured.
    // `kMaxPlausibleSpeed` was enforced in `MeasureVelocity` alone, so a finite-but-absurd
    // velocity arriving over the wire would be stored as a LEVEL and drive every later frame -
    // `aim`, `distA` and `lim` all stay finite, so F340's gate passes it. Inert while both
    // instances run the same build, which is exactly what F340's rationale says not to rely on:
    // a value that survives ingest is supposed to PROVE later corruption is local.
    if (!Finite(x) || !Finite(y) || !Finite(z) || !Finite(velX) || !Finite(velZ))
    {
        ++g_moveNonFinite;
        if (g_moveNonFinite <= 8)
            ErrorLog("[M2] MOVE REJECTED uid=" + N2(uid) + " - NON-FINITE over the wire:"
                     " x=" + F2(x) + " y=" + F2(y) + " z=" + F2(z)
                     + " velX=" + F2(velX) + " velZ=" + F2(velZ)
                     + " (F340; first 8 reported, then counted only)");
        return;
    }

    // F357 - and the MAGNITUDE gate, on this side too.
    bool velRejected = false;   // D2-b item 3: a rejected sample keeps the owner's smoothed speed/heading
    {
        const float sp2 = velX * velX + velZ * velZ;
        if (sp2 > kMaxPlausibleSpeed * kMaxPlausibleSpeed)
        {
            ++g_moveVelAbsurd; velRejected = true;
            velX = 0.0f; velZ = 0.0f;   // keep the POSITION, which is still usable; drop the lead
        }
    }

    std::map<unsigned int, Puppet>::iterator it = g_puppets.find(uid);
    if (it == g_puppets.end())
    {
        // F079: the counterpart may genuinely not exist here (pre-session spawn, or a uid
        // we were never told about). Refuse quietly-but-visibly rather than assume.
        Character* c = FindSpawned(uid);
        if (c == 0)
        {
            // F341 - SPLIT THE TWO CAUSES. "No local puppet" has meant both "we were never told
            // about this character" and "we had it, drove it, and this machine's engine withdrew
            // it" - and T092 showed the second is the one that actually happens: 172 of 173 were a
            // SINGLE uid that had drifted 609 units behind before `[P034] RETIRED` fired on it.
            // It was alive on the authority the whole time. Until now both read as one line.
            // F342 - **COUNT CHARACTERS, NOT MESSAGES.** T092's shape was 172 of 173 for a SINGLE
            // uid, against `moveRecv=1,209,911`: one diverged character streaming at ~10 Hz for ten
            // minutes contributes thousands. A reader seeing `moveForRetired=6000` reads six
            // thousand diverged characters. The message count is kept because it shows how long the
            // divergence persisted, but the number that answers "how many characters" is beside it
            // and named for what it is.
            bool wasOurs = KnownUidRetired(uid);
            if (wasOurs) { ++g_moveForRetired; NoteDivergedUid(uid); }
            else         { ++g_moveForUnheard; }
            static long long s_lastWarn = -10000;
            if (g_tick - s_lastWarn > 600)
            {
                s_lastWarn = g_tick;
                // F342 - the same value the counter used, not a second call: `RetireOnDestroy` can
                // fire between two evaluations and produce a line contradicting the counter above it.
                DebugLog("[M2] MOVE for unknown uid=" + N2(uid) + " - no local puppet (F079),"
                         + std::string(wasOurs
                             ? " and WE ONCE HELD IT: this engine withdrew the character (P034"
                               " retired it) while the authority still has it. The two worlds have"
                               " diverged for this character and nothing here can restore it -"
                               " F341."
                             : " and we have never held it."));
            }
            return;
        }
        Puppet p;
        p.ch = c; p.tx = x; p.ty = y; p.tz = z; p.velX = velX; p.velZ = velZ;
        p.lastMoveAt = NowSeconds(); p.moveIntervalSec = 0.0; p.lagSampleAt = 0.0;
        p.driving = false; p.everDriven = false; p.lastDrift = 0.0f; p.maxDrift = 0.0f;
        p.lastDriftY = 0.0f; p.maxDriftY = 0.0f; p.applied = 0; p.pronedAt = 0;
        p.snapTries = 0; p.snapLastDrift = 0.0f; p.snapGaveUp = false;
        p.snapPending = false; p.snapFromX = 0.0f; p.snapFromZ = 0.0f;
        // Far in the past, so the FIRST time a puppet is lost it reports immediately rather than
        // waiting out a throttle window it never entered.
        p.lastLostTick = -10000;
        p.speedSaved = false; p.speedOriginal = 0.0f;
        p.rbActive = false; p.rbCeiling = false; p.rbWrote = 0.0f; p.rbFrame = -1;   // T-189 runboost
        p.rbFailed = false; p.rbEaseAt = 0.0;   // runboost2 (T-195)
        p.progressCheckTick = 0; p.progressFromX = 0.0f; p.progressFromZ = 0.0f; p.progressFromDist = 0.0f;
        p.stalledWindows = 0; p.catchupPending = false; p.catchupDriftBefore = 0.0f;
        p.catchupFromX = 0.0f; p.catchupFromZ = 0.0f; p.catchupTries = 0; p.catchupGaveUp = false;
        p.progressFromTX = 0.0f; p.progressFromTZ = 0.0f; p.snapCause = 0; p.snapWatchWindows = -1; p.snapStrikes = 0;
        for (int si = 0; si < kSnapRateMax; ++si) p.snapTimes[si] = -1.0e9;
        p.snapTimesNext = 0; p.unstickEnded = false;
        p.nativeWindow = false; p.nativeMaxDrift = 0.0f; p.nativeSnapScorePending = false; p.nativeSnapTick = 0; p.rcSteps = 0; p.rcMaxStep = 0.0f; p.rcSumStep = 0.0f; p.rcLastTickAt = 0.0; p.rcReqMax = 0.0f; p.rcVisMaxStep = 0.0f; p.rcVisMaxRate = 0.0f; p.rcVisMaxRateDowned = 0.0f; p.rcVisMaxRateAny = 0.0f; p.rcVisLastX = p.rcVisLastY = p.rcVisLastZ = 0.0f; p.rcVisPrimed = false; p.rcLayerGapMax = 0.0f; p.rcLayerGapLast = 0.0f; p.rcDriftSum = 0.0; p.rcDriftN = 0; p.rcSatSteps = 0; p.rcBoostedSteps = 0; p.authDesired = -1.0f; p.authDesiredApplied = -2.0f; p.intentType = 0; p.intentSubject = 0; p.intentX = p.intentY = p.intentZ = 0.0f; p.intentPriority = 0; p.intentAt = 0.0; p.intentSeq = 0; p.insideSaid = -1; p.insideKey[0] = 0; p.insideAt = 0.0; p.insideHint = 0; p.catchingUp = false; p.orderActive = false; p.orderX = p.orderZ = 0.0f; p.orderTick = 0; p.orderCount = 0; p.orderSpeed = -1; p.unsticking = false; p.unstickTick = 0; p.faceX = p.faceZ = 0.0f; p.faceTick = 0;
        p.pathMode = false; p.pathHaveDest = false; p.pathDestX = p.pathDestZ = 0.0f; p.pathIssueAt = 0.0; p.pathAwait = false; p.pathAwaitTick = 0; p.pathFailNoted = false; p.pathLastX = p.pathLastZ = 0.0f; p.pathLastAt = 0.0; p.pathHoldUntil = 0.0; p.pathProgArmed = false; p.pathProgTick = 0; p.pathProgFromDistA = 0.0f; p.pathWantIssue = false; p.pathWantSince = 0.0; p.pathGrant = false; p.pathOwnerStillSince = 0.0; p.pathProgOwnerMoved = false; p.pathProgFromX = p.pathProgFromZ = 0.0f; p.pathDestLead = false; p.pathDestDirX = p.pathDestDirZ = p.pathLineX = p.pathLineZ = 0.0f; p.pathOwnerWasMoving = false; p.pathStopDue = false; p.pathArrivedNoted = false; p.pathWantPriority = false; p.pathTurnSince = 0.0; p.pathRetryAim = 0; p.ownPrimed = false; p.ownSpd = p.ownHdgX = p.ownHdgZ = 0.0f; p.pathIssueTick = 0; p.pathFailSeenClear = false; p.pathSeenPending = p.pathLanded = p.pathLandedFaultNoted = p.pathTooCloseNoted = p.pathLeadWalking = false;   // D1 / D1-b / D1-c / D1-d / D2 / D2-b / D2-c / D3
        p.follow = false; p.followPlayer = false; FollowReset(p);   // the follow drive starts empty; the first report decides whether it applies
        // PROBE-START: P093
        std::memset(&p.p093, 0, sizeof(p.p093));
        // PROBE-END: P093
        g_puppets[uid] = p;
        it = g_puppets.find(uid);
        DebugLog("[M2] puppet registered uid=" + N2(uid) + " (first MOVE received)");
    }

    Puppet& p = it->second;
    // THE FOLLOW DRIVE's recording, for the copy of any live character another game owns - a player's own character, its
    // squad and every NPC (re-read on every report). Owner time: the stamp, unwrapped on the owner game's clock, or the
    // arrival time for an unstamped report. A report stamped before the owner's stated stop arrived late and is dropped whole; a moving one
    // stamped after it ends the stop once it is off the stop point (on it, it is the stop's own trailing report: dropped);
    // any report after it far from the stop point, or an owner teleport, ends it too. Everything is on the owner game's
    // clock, so an owner of another slot (a roster re-key, a hand-over) starts the follow state again.
    bool followStill = false;
    {
        const bool follow = PlausibleObj(p.ch) && !IsRetiredObject(p.ch) && !net::IsUidMine(uid);
        if (follow != p.follow) { FollowReset(p); p.follow = follow; }
        p.followPlayer = follow && PeerFactionPod(p.ch) == 1;   // a recruit keeps its follow state: only the probes split by it
        if (p.follow)
        {
            const int ownerSlot = net::OwnerSlotOf(uid);
            if (ownerSlot != p.fOwnerSlot) { FollowReset(p); p.fOwnerSlot = ownerSlot; }
            const double now = NowSeconds();
            double ownerT = now;
            if (stamped)
            {
                followplay::ClockOff* ck = FollowClockOf(uid, p);
                ownerT = followplay::StampUnwrapMs(ck, stampMs) / 1000.0;
                p.fClkOff = followplay::ClockOffsetUpdate(ck, now, ownerT);
            }
            else p.fClkOff = 0.0;
            const bool moving = !velRejected && (velX != 0.0f || velZ != 0.0f);
            const int order = followplay::MoveVsStop(p.stopHeld, p.stopT, ownerT, moving, x, z, p.stopX, p.stopZ);
            if (order == followplay::kMoveStale) { ++g_followStaleMoveDropped; return; }
            if (order == followplay::kMoveClearsStop) { p.stopHeld = false; p.stopIssueOwed = false; p.fHeld = false; ++g_followStopCleared; }
            const float jx = x - p.tx, jz = z - p.tz;
            const float jd2 = jx * jx + jz * jz;
            const bool jumped = Finite(jd2) && jd2 > kOwnTeleportDist * kOwnTeleportDist;
            if (jumped) followplay::TrackClear(&p.trk);   // an owner teleport: the route before it is not walked
            // An owner that left the stop point without a moving report (prone, crawling, ragdolled or carried bodies
            // report no velocity) or teleported no longer holds the copy there.
            if (followplay::StopLeftBy(p.stopHeld, p.stopT, ownerT, x, z, p.stopX, p.stopZ, jumped))
            {
                p.stopHeld = false; p.stopIssueOwed = false; p.fHeld = false; ++g_followStopCleared;
            }
            // A zero velocity from a position that moved further than a standing owner can is a sample the owner could not
            // measure (MeasureVelocity sends 0 for it): not a standing point, so not recorded.
            bool untrusted = velRejected;
            if (!moving && !untrusted && !jumped && p.trk.n > 0)
            {
                const followplay::Sample& nw = followplay::TrackNewest(p.trk);
                const double odt = ownerT - nw.t;
                const float ux = x - nw.x, uz = z - nw.z;
                const float ud = sqrtf(ux * ux + uz * uz);
                if (odt > 0.0 && odt < 5.0 && Finite(ud) && ud > kMovingEpsilon * (float)odt + 0.5f) untrusted = true;
            }
            if (!untrusted)
            {
                followplay::Sample sm;
                sm.x = x; sm.y = y; sm.z = z; sm.vx = moving ? velX : 0.0f; sm.vz = moving ? velZ : 0.0f; sm.t = ownerT; sm.still = !moving;
                if (followplay::Push(&p.trk, sm) == 0) ++g_followOutOfOrder;
            }
            else ++g_followUnrecorded;
            if (moving) p.fLastMovingAt = now;
            followStill = !moving;
        }
    }
    if (!velRejected) P115OnMove(uid, x, z, velX, velZ);   // PROBE P115: the owner's moving / stop point, for the copy's settle line
    if (!velRejected) P124OnMove(uid, p, x, z, velX, velZ);   // PROBE P124: the same for a non-player character's copy
    {
        // D2-b (review-d2 item 3): the owner's speed and heading, smoothed per sample (EMA). A REJECTED sample - the magnitude
        // gate above, or a zero velocity while the position moved (the authority's MeasureVelocity sends 0 for a sample it
        // could not trust) - keeps the last value. A genuine standing sample (slow, position static) weighs kOwnSmoothAlphaStill.
        const float ss = sqrtf(velX * velX + velZ * velZ);
        // D2-c (recheck-d2b): an owner TELEPORT (a jump > kOwnTeleportDist from the previous sample) resets the smoothing;
        // the jump sample itself is not fed in - the next samples re-prime it.
        const float tjx = x - p.tx, tjz = z - p.tz;
        const float tjd2 = tjx * tjx + tjz * tjz;
        const bool teleported = Finite(tjd2) && tjd2 > kOwnTeleportDist * kOwnTeleportDist;
        if (teleported && p.ownPrimed) { p.ownPrimed = false; p.ownSpd = p.ownHdgX = p.ownHdgZ = 0.0f; ++g_ownSmoothReset; }
        bool rejected = velRejected || !Finite(ss) || teleported;
        bool standing = false;
        if (!rejected && p.ownPrimed && ss < kPathOwnerStillSpeed)
        {
            const double sdt = NowSeconds() - p.lastMoveAt;
            const float smx = x - p.tx, smz = z - p.tz;
            const float smd = sqrtf(smx * smx + smz * smz);
            if (sdt > 0.005 && sdt < 5.0 && Finite(smd))
            {
                if (ss == 0.0f && smd > kPathOwnerStillSpeed * (float)sdt) rejected = true;
                else standing = true;
            }
        }
        if (!rejected)
        {
            if (!p.ownPrimed) { p.ownPrimed = true; p.ownSpd = ss; }
            else { const float a = standing ? kOwnSmoothAlphaStill : kOwnSmoothAlpha; p.ownSpd = p.ownSpd * (1.0f - a) + ss * a; }
            if (ss >= kPathOwnerStillSpeed)
            {
                const float ux = velX / ss, uz = velZ / ss;
                const float hn0 = sqrtf(p.ownHdgX * p.ownHdgX + p.ownHdgZ * p.ownHdgZ);
                if (!(hn0 > 0.001f)) { p.ownHdgX = ux; p.ownHdgZ = uz; }
                else
                {
                    const float hx = p.ownHdgX * (1.0f - kOwnSmoothAlpha) + ux * kOwnSmoothAlpha;
                    const float hz = p.ownHdgZ * (1.0f - kOwnSmoothAlpha) + uz * kOwnSmoothAlpha;
                    const float hn1 = sqrtf(hx * hx + hz * hz);
                    if (Finite(hn1) && hn1 > 0.001f) { p.ownHdgX = hx / hn1; p.ownHdgZ = hz / hn1; }
                    else { p.ownHdgX = ux; p.ownHdgZ = uz; }   // an exact reversal: take the new heading
                }
            }
        }
    }
    p.tx = x; p.ty = y; p.tz = z;
    p.velX = velX; p.velZ = velZ;
    if (Finite(authDesiredSpeed) && authDesiredSpeed >= 0.0f) p.authDesired = authDesiredSpeed;   // 0 is a real level; -1 = the authority could not read it
    if (Finite(faceX) && Finite(faceZ) && (faceX != 0.0f || faceZ != 0.0f)) { p.faceX = faceX; p.faceZ = faceZ; }   // H024
    {
        // F355 - the OBSERVED send interval, smoothed, so the staleness horizon tracks the
        // authority's actual frame rate instead of assuming ours. The interval is
        // `12 / authorityFPS`, so a FIXED horizon shorter than it expires on every single cycle -
        // 0.6 s at the 20 fps this workstream's own load case names, against a 0.5 s horizon.
        const double now = NowSeconds();
        const double d   = now - p.lastMoveAt;
        // F357 - a 5 ms FLOOR, not just `> 0`. The new-puppet branch sets `lastMoveAt` and then
        // falls through to here, where `d` is a few microseconds - which passed the old guard and
        // seeded the EMA at ~1e-6, pinning the horizon at its floor for the first couple of
        // intervals. A real send interval is 12 frames: 50 ms even at 240 fps.
        if (d > 0.005 && d < 5.0)
            p.moveIntervalSec = (p.moveIntervalSec <= 0.0)
                                    ? d : (p.moveIntervalSec * 0.8 + d * 0.2);
        p.lastMoveAt = now;
    }
    // The follow drive's delay is re-read only while the owner stands, so a copy never speeds up or slows down mid-walk
    // because the delay moved.
    if (p.follow && (followStill || p.stopHeld))
    {
        const double d = followplay::PlaybackDelay(p.moveIntervalSec);
        if (d != p.followD) { p.followD = d; ++g_followDRecalc; }
    }
    ++p.applied;
    ++g_moveRecv;
}

// F353 - measure the authority's own velocity, with every way it can lie about one handled.
//
// Factored out because there are two send sites and the first version fixed only one of them.
// `out*` are set to zero unless a velocity is both measurable and BELIEVABLE.
// F419 - the authority's own commanded speed, the gait intent. Read from CharMovement+0xBC (desiredSpeed).
// H024 - the authority's facing direction, unit XZ. CharMovement::headingVector (0x2ADB90) is a const getter.
static bool AuthorityFacing(Character* c, float* fx, float* fz)
{
    *fx = 0.0f; *fz = 0.0f;
    if (!PlausibleObj(c)) return false;
    CharMovement* mv = c->movement;
    if (!PlausibleObj(mv)) return false;
    float x = 0.0f, z = 0.0f;
    __try { const Ogre::Vector3& f = mv->headingVector(); x = f.x; z = f.z; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    const float n = sqrtf(x * x + z * z);
    if (!Finite(n) || n < 0.001f) return false;
    *fx = x / n; *fz = z / n;
    return true;
}

// The movement object's currentlyMoving byte (+0x24), which the engine recomputes every tick from the body's own measured
// displacement and clears when it stops moving it (.modding/05-findings.md F306 / F308): on a character this game runs, the
// engine's own word on whether its movement goes on. 1 / 0, -1 = unreadable.
static int AuthorityEngineMoving(Character* c)
{
    if (!PlausibleObj(c)) return -1;
    CharMovement* mv = c->movement;
    if (!PlausibleObj(mv)) return -1;
    unsigned char b = 0;
    __try { b = *(const unsigned char*)((const char*)mv + 0x24); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    return b != 0 ? 1 : 0;
}

// Is this character in THIS game's own player faction (the stops sent are counted by it; P123 lines only these)? 1 / 0,
// -1 faulted.
static int FollowOwnPlayerPod(::Character* c)
{
    __try
    {
        ::Faction* f = c->getOwnerFactionDirect();
        if (f == 0) return 0;
        return IsPlayerFaction(f) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// The owner's clock on the wire: NowSeconds in whole milliseconds, kept to 32 bits (the copy unwraps it, followplay).
static unsigned int FollowStampMs(double sec)
{
    if (!(sec > 0.0)) return 0u;
    return (unsigned int)((unsigned long long)(sec * 1000.0) & 0xFFFFFFFFull);
}

// Review 4 item 10: 0 is a real level (a stopped authority may well carry it), so 'unreadable' is -1.
static float AuthorityDesiredSpeed(Character* c)
{
    if (!PlausibleObj(c)) return -1.0f;
    CharMovement* mv = c->movement;
    if (!PlausibleObj(mv)) return -1.0f;
    float d = -1.0f;
    __try { d = *(const float*)((const char*)mv + 0xBC); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1.0f; }
    return (Finite(d) && d >= 0.0f) ? d : -1.0f;
}

void MeasureVelocity(Character* c, unsigned int uid, const Ogre::Vector3& here,

                     SentSample& s, double nowSec, float* outX, float* outZ)
{
    *outX = 0.0f; *outZ = 0.0f;
    s.sampleVerdict = followplay::kSampleNone;   // until a trusted sample says moving or still

    // F357 - **ONE TRUSTWORTHINESS TEST, AND IT RUNS BEFORE ANY STORE.**
    //
    // `Character::getPosition()` reads whichever of FOUR layers is currently authoritative and
    // chooses per call. Sampling the wrong one does not produce a wrong-looking number - it
    // produces a difference of COORDINATE LAYERS that reads as a perfectly plausible walking
    // speed. T063/F201 measured that offset at 12.5 units; over a 0.102 s interval that is
    // ~122 units/s, which is finite, above `kMovingEpsilon` and BELOW `kMaxPlausibleSpeed`, so it
    // sails through every downstream guard.
    //
    // **F353 put this guard at the point of USE. F355 moved it to the point of STORE and left an
    // earlier store above it** - the `first` branch re-based and stamped a VALID time without ever
    // consulting prone, so on alternate ticks the poison was written anyway and the defect survived
    // at half rate. That is the same shape four times running. It is now one predicate, evaluated
    // before anything is written, covering every condition this file knows switches the layer:
    //
    //   prone       (+0xE0)  - getPosition() returns the animation scene node (T063/F203)
    //   ragdoll              - `threadedUpdate` skips the write-back of the movement position
    //                          into Character+0x48 entirely (see the P036 notes above)
    //   inSomething (+0x2F8) - in a cage, a bed, a building slot
    //   carried     (+0x3D4) - the reported position follows the CARRIER, so a body that cannot
    //                          walk emits a real, sustained velocity for as long as the carry lasts
    //
    // On any of them the sample is INVALIDATED and nothing is stored, so the next call starts a
    // fresh baseline from a trustworthy layer.
    {
        bool untrusted = false;
        int reason = 0;
        if (*(const int*)((const char*)c + kProneStateOff) != 0)      { untrusted = true; reason = 1; }
        else if (*(const int*)((const char*)c + 0x2F8) != 0)          { untrusted = true; reason = 2; }
        else if (*(const unsigned char*)((const char*)c + 0x3D4) != 0){ untrusted = true; reason = 3; }
        else if (SafeIsRagdoll(c))                                    { untrusted = true; reason = 4; }

        // F358 - **AND THEN TEST THE THING ITSELF, NOT ONLY ITS PROXIES.**
        //
        // The four conditions above are the ones this project has NAMED, and F357 claimed that was
        // completeness. It is not: F193 decompiled the actual layer-3 selector and it is two bytes
        // on an animation-side object reached through `AnimationClass` vtable+0x48 - **neither
        // `PoseState` nor `inRagdoll()` is that test.** F201 measured the proxies failing outright:
        // `+0x2F8` and `+0x3D4` read 0 on all 13 probe lines, standing and prone alike, and one line
        // read `agrees=logical` while `prone=4`.
        //
        // So the guard also asks the question directly. `Character+0x48` is the LOGICAL position;
        // when `getPosition()` returns it, the layer is the one a displacement may be measured
        // across. When it does not, we are on some other layer - whichever, and for whatever reason
        // - and the sample is refused. **A direct test of the property beats an enumeration of the
        // causes**, and it fails safe rather than fabricating a ~122 units/s walk.
        //
        // The counter is the point as much as the refusal: `velLayerMismatch` non-zero with all
        // four named conditions false means the enumeration above is INCOMPLETE, and the run says
        // so instead of the next reviewer having to.
        if (!untrusted)
        {
            const float* lg = (const float*)((const char*)c + kLogicalPosOff);
            float dl = 0.0f;
            __try { dl = fabsf(here.x - lg[0]) + fabsf(here.z - lg[2]); }
            __except (EXCEPTION_EXECUTE_HANDLER) { dl = 1e30f; }
            // Generous against ordinary animation lag, tight against the 12.5-unit layer offset
            // T063 measured.
            if (!(dl < 1.0f)) { untrusted = true; reason = 5; }
        }

        if (untrusted)
        {
            s.uid = uid;
            s.at  = 0.0;          // invalidate; deliberately do NOT store a position
            s.wasMoving = false;
            if      (reason == 1) ++g_velProne;
            else if (reason == 2) ++g_velInSomething;
            else if (reason == 3) ++g_velCarried;
            else if (reason == 4) ++g_velRagdoll;
            else                  ++g_velLayerMismatch;
            return;
        }
    }

    const bool first = (s.uid != uid || s.at == 0.0);
    const float px = s.x, pz = s.z;
    const double prevAt = s.at;

    // Re-base only now, when the layer is known good.
    s.uid = uid; s.x = here.x; s.y = here.y; s.z = here.z; s.at = nowSec;

    if (first) { ++g_velFirstSample; return; }

    const double dtSec = nowSec - prevAt;
    if (!(dtSec > 0.0)) { ++g_velBadClock; return; }

    const float sx = (float)((here.x - px) / dtSec);
    const float sz = (float)((here.z - pz) / dtSec);
    const float sp = sqrtf(sx * sx + sz * sz);

    // Finite BEFORE it goes on the wire. The peer rejects non-finite values at ingest (F340) and
    // attributes them to the authority; making that attribution meaningful means never being the
    // source of one. Counted separately from "stopped" - a non-finite sample filed as "standing
    // still" is a defect wearing a normal reading's clothes.
    if (!Finite(sx) || !Finite(sz) || !Finite(sp)) { ++g_velNonFinite; return; }

    // A TELEPORT IS NOT A VELOCITY (F353). Finite, above the epsilon, and it would aim the puppet
    // thousands of units away with `lim` far too large for the per-frame cap to bite.
    if (sp > kMaxPlausibleSpeed) { ++g_velTeleport; return; }

    // F357 - hysteresis: `kMovingEpsilon` to START being treated as moving, half of it to KEEP
    // being treated as moving. Without the second threshold a character whose true speed sits on
    // the first one flaps between walking and stopped at the update rate — which inside the arrival
    // deadband is the very stutter this whole feature exists to remove, reinstated for that cohort
    // by its own fix. No run-wide counter could have revealed it: `velMoving`/`velStopped` are
    // aggregates and one straddling character is invisible in both.
    const float thresh = s.wasMoving ? (kMovingEpsilon * 0.5f) : kMovingEpsilon;
    if (sp < thresh) { s.wasMoving = false; s.sampleVerdict = followplay::kSampleStill; ++g_velStopped; return; }

    s.wasMoving = true; s.sampleVerdict = followplay::kSampleMoving;
    *outX = sx; *outZ = sz;
    ++g_velMoving;
}

// ============================================================================================
// H019 / M-C - THE FIRST HANDOFF EXPERIMENT: does a copy adopt its authority's CURRENT GOAL on its
// first free decision pass when that goal is handed to it as an ORDER, the way the native combat
// window hands over an attack (F239/F409)? The engine's own path is checkOrders -> runGoals ->
// setCurrentGoal + runGOAP -> setCurrentTask, which leaves needGOAP == 0 with the task running -
// the steady state that holds a goal in place (F424). A control puppet gets the pass without the
// order and is expected to re-decide for itself.
// ============================================================================================

typedef void* (*TaskDataByTypeFn)(int);
unsigned long long kTaskDataByTypeRva = 0; static coop::AddrReg kTaskDataByTypeRva_reg("TaskDataByType", &kTaskDataByTypeRva);   /* P8h: the address table fills this. Steam_1.0.65 0x283F40 */   // TaskType -> TaskData* (the static table DAT_141CE80F0)

static void* SafeTaskData(int type)
{
    TaskDataByTypeFn fn = (TaskDataByTypeFn)((uintptr_t)::GetModuleHandleA(0) + kTaskDataByTypeRva);
    __try { return fn(type); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static bool ReadTaskName(const void* td, char* out, int cap)
{
    __try
    {
        const char* s = (const char*)td + 0x138;                 // MSVC std::string: buf[16] | ptr, size at +0x10
        const size_t size = *(const size_t*)(s + 0x10);
        if (size >= (size_t)cap) return false;
        const size_t res = *(const size_t*)((const char*)s + 0x18);   // review-p4f HIGH-1: heap iff capacity >= 16
        const char* p = (res >= 16) ? *(const char* const*)s : s;
        if (p == 0) return false;
        memcpy(out, p, size); out[size] = 0;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// The engine's own name for a TaskType; "Aimless" is describeCurrentGoal's word for no goal.
static std::string TaskTypeName(int type)
{
    if (type <= 0) return "Aimless";
    void* td = SafeTaskData(type);
    char buf[64];
    if (td != 0 && ReadTaskName(td, buf, 64)) return std::string(buf);
    return "type" + N2(type);
}
static bool IsAttackTaskType(int t)
{
    return t == 4 || t == 5 || t == 13 || t == 16 || t == 21 || t == 34 || t == 61 || t == 227 || t == 248;
}
// hand -> Character*, under SEH (hand has a vtable; getCharacter is the engine's own resolver).
static ::Character* SafeHandCharacter(const void* hp)
{
    __try
    {
        const hand& h = *(const hand*)hp;
        if (!h) return 0;
        return h.getCharacter();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static bool ReadTaskSystemFlags(const void* ts, int* needGoap, int* taskInstalled)
{
    __try
    {
        *needGoap = (int)*(const unsigned char*)((const char*)ts + 0x1B0);
        const char* body = *(const char* const*)((const char*)ts + 0x270);   // CharBody*
        *taskInstalled = (body != 0 && *(const void* const*)(body + 0x68) != 0) ? 1 : 0;   // CharBody::currentAction
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *needGoap = -1; *taskInstalled = -1; return false; }
}
static bool ReadGoalFields(const char* ts, void** td, int* prio)
{
    __try { *td = *(void**)(ts + 0x1C0); *prio = *(int*)(ts + 0x20C); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool ReadTaskType(const void* td, int* type)
{
    __try { *type = *(const int*)((const char*)td + 0x44); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool ReadDestination(const void* mv, float* x, float* y, float* z)
{
    __try { const float* d = (const float*)((const char*)mv + 0xDC); *x = d[0]; *y = d[1]; *z = d[2]; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The authority's current goal: TaskType (0 = none), subject uid (0 = not a mirrored character),
// movement destination, priority. Plain reads on the main thread; the AI worker may be writing
// these at the same instant, so a torn read is possible and shows as a one-sample glitch that the
// 5-second refresh corrects.
static bool AuthorityIntent(::Character* c, int* type, unsigned int* subj, float* x, float* y, float* z, int* prio)
{
    *type = 0; *subj = 0; *x = *y = *z = 0.0f; *prio = 0;
    void* ai = GetCharacterAI(c);
    if (!PlausibleObj(ai)) return false;
    char* ts = *(char**)((char*)ai + 0x20);
    if (!PlausibleObj(ts)) return false;
    void* td = 0; int prioV = 0;
    if (!ReadGoalFields(ts, &td, &prioV)) return false;
    if (td == 0) return true;                       // no goal - a real state, sent as type 0
    if (!ReadTaskType(td, type)) return false;
    *prio = prioV;
    ::Character* s = SafeHandCharacter(ts + 0x1C8);  // TaskMatch::subject
    if (PlausibleObj(s)) *subj = FindSpawnedUid(s);
    CharMovement* mv = c->movement;
    if (PlausibleObj(mv)) ReadDestination(mv, x, y, z);
    return true;
}

// M-D - the handoff reads the authority's intent and facing through these (the statics above).
bool ReadAuthorityIntent(::Character* c, int* type, unsigned int* subj, float* x, float* y, float* z) { int prio = 0; return AuthorityIntent(c, type, subj, x, y, z, &prio); }
/* M7a2 item 9 [m7a2-rp1]: the catch-up's INTENT - this game's character's current goal, sent now (the stream's own 5 s refresh unchanged). */
bool IntentResendOwned(unsigned int uid)
{
    ::Character* c = FindSpawned(uid);
    if (c == 0 || !PlausibleObj(c)) return false;
    int t = 0, pr = 0; unsigned int s = 0; float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!AuthorityIntent(c, &t, &s, &x, &y, &z, &pr)) return false;
    return net::SendIntent(uid, t, s, x, y, z, pr);
}
bool ReadAuthorityFacing(::Character* c, float* fx, float* fz) { return AuthorityFacing(c, fx, fz); }
// M-D - the new owner un-puppets a copy: drop the drive row (restores the engine's own speed, closes a window),
// then release the AI gate. The caller has already taken ownership so the streams flip with it.
void UnpuppetForOwnership(unsigned int uid)
{
    ::Character* c = FindSpawned(uid);
    DropPuppet(uid, true);
    if (PlausibleObj(c)) SuppressCharacter(c, false);
}

/* P25 fold 2: items.cpp (declared in items.h, namespace coop) - the P7n position key of a building, and the building a key names */
int  ObjectPositionKey(void* obj, char* out, int cap, const char* suffix, int countIt, int podPos);
void* ObjectByPositionKeyHint(const char* key, void* hint);
/* P25 fold 2: defined with the interior keep below - the owner says which building its own character is in (MSG_INSIDE) */
static void IkOwnerSay(unsigned int uid, ::Character* c, double now);

void ApplyRemoteIntent(unsigned int uid, int taskType, unsigned int subjectUid, float x, float y, float z, int priority)
{
    std::map<unsigned int, Puppet>::iterator it = g_puppets.find(uid);
    if (it == g_puppets.end()) { ++g_intentRefused; return; }   // not our puppet: refused, counted
    Puppet& p = it->second;
    if (p.intentType != taskType || p.intentSubject != subjectUid) ++p.intentSeq;
    p.intentType = taskType; p.intentSubject = subjectUid; p.intentX = x; p.intentY = y; p.intentZ = z;
    p.intentPriority = priority; p.intentAt = NowSeconds();
    ++g_intentRecv;
    // The owner's Move-order goal ends a follow copy's path (FollowDriveStep). It stays after the INTENT that ends the order,
    // until the owner's stop uses it up; the refresh of a goal a stop used up does not bring it back.
    if (taskType == kMoveOrderType && Finite(x) && Finite(z))
    {
        const float sx = x - p.goalSpentX, sz = z - p.goalSpentZ;
        if (!(p.goalSpent && sx * sx + sz * sz <= followplay::kGoalSameDist * followplay::kGoalSameDist))
        {
            p.goalHave = true; p.goalX = x; p.goalZ = z; p.goalSpent = false;
        }
    }
}

// PROBE-START: P123
namespace { static void P123CopyStopRecv(unsigned int uid, const Puppet& p, double now); }   // the P123 block's namespace
// PROBE-END: P123

// THE OWNER'S STATED STOP (MSG_MOVESTOP) for a follow copy (any character, NPCs included): its owner's body stopped at
// (x, y, z) at owner time stampMs.
// It goes into the recording as a standing report and holds until a moving report stamped after it: the copy walks on to
// exactly that point (its path to it goes out at once, FollowDriveStep) and never past it. A stop older than a moving report
// already received off the stop point is over. The goal it reached is used up. A stop from an owner of another slot than
// the follow state was built under starts that state again (its clock is another game's).
void ApplyRemoteMoveStop(unsigned int uid, float x, float y, float z, unsigned int stampMs)
{
    std::map<unsigned int, Puppet>::iterator it = g_puppets.find(uid);
    if (it == g_puppets.end() || !it->second.follow) { ++g_followStopUnknown; return; }
    if (!Finite(x) || !Finite(y) || !Finite(z)) { ++g_moveNonFinite; return; }
    Puppet& p = it->second;
    const double now = NowSeconds();
    ++g_followStopRecv;
    if (!p.followPlayer) ++g_followStopRecvNpc;
    const int ownerSlot = net::OwnerSlotOf(uid);
    if (ownerSlot != p.fOwnerSlot) { FollowReset(p); p.fOwnerSlot = ownerSlot; }
    const double stopT = followplay::StampUnwrapMs(FollowClockOf(uid, p), stampMs) / 1000.0;
    const bool haveNewest = p.trk.n > 0;
    const followplay::Sample* nw = haveNewest ? &followplay::TrackNewest(p.trk) : 0;
    if (followplay::StopAlreadyOver(haveNewest, nw ? nw->t : 0.0, nw ? nw->still : true, nw ? nw->x : x, nw ? nw->z : z,
                                    stopT, x, z))
    {
        ++g_followStopOver;
        return;
    }
    p.stopHeld = true; p.stopX = x; p.stopY = y; p.stopZ = z; p.stopT = stopT; p.stopIssueOwed = true; p.fHeld = false;
    followplay::StopProgressReset(&p.fStopProg); p.fStopStalled = false;   // the closing watch starts on the new point
    followplay::Sample sm;
    sm.x = x; sm.y = y; sm.z = z; sm.vx = 0.0f; sm.vz = 0.0f; sm.t = stopT; sm.still = true;
    if (followplay::Push(&p.trk, sm) == 0) ++g_followOutOfOrder;
    p.tx = x; p.ty = y; p.tz = z; p.velX = 0.0f; p.velZ = 0.0f;   // what a standing report at the stop point would set
    p.ownSpd = 0.0f;   // the smoothed speed restarts from the owner's real one when it moves again
    if (p.goalHave) { p.goalSpent = true; p.goalSpentX = p.goalX; p.goalSpentZ = p.goalZ; }
    p.goalHave = false;
    const double d = followplay::PlaybackDelay(p.moveIntervalSec);   // the owner stands: the delay may be re-read
    if (d != p.followD) { p.followD = d; ++g_followDRecalc; }
    P123CopyStopRecv(uid, p, now);   // PROBE P123
}

bool InjectOrder(::Character* puppet, int taskType, ::Character* subject, const Ogre::Vector3& loc)
{
    if (!PlausibleObj(puppet)) return false;
    void* ai = GetCharacterAI(puppet);
    if (!PlausibleObj(ai)) return false;
    AITaskSytem* orders = *(AITaskSytem**)((char*)ai + 0x20);
    if (!PlausibleObj(orders)) return false;
    // Handles are per-process: the subject's LOCAL handle. With no subject the puppet's own handle
    // stands in (a Move order's subject is the mover).
    hand none;   // an EMPTY handle for subject-less orders (Move order) - the M2b TASK path's own convention
    const hand& target = PlausibleObj(subject) ? subject->getHandle() : none;
    orders->issueOrder((TaskType)taskType, target, loc, true, false);
    PulseCharacterQuiet(puppet);   // exactly one decision pass, then gated again
    return true;
}

int HandoffAuto(int maxCount)
{
    int done = 0, k = 0;
    for (std::map<unsigned int, Puppet>::iterator it = g_puppets.begin(); it != g_puppets.end(); ++it)
    {
        if (done >= maxCount || g_pendingHandoffN >= kMaxPendingHandoffs) break;
        const unsigned int uid = it->first;
        Puppet& p = it->second;
        if (p.intentType == 0) { ++g_handoffSkippedNoIntent; continue; }
        if (p.nativeWindow)    { ++g_handoffSkippedWindow; continue; }
        if (p.orderActive)     { ++g_handoffSkippedOrderDrive; continue; }   // H021 owns it: its goal is our Move order
        if (IsAttackTaskType(p.intentType)) { ++g_handoffSkippedAttack; continue; }   // the native window owns fights
        ::Character* c = p.ch;
        if (!PlausibleObj(c) || IsRetiredObject(c)) continue;
        void* ai = GetCharacterAI(c);
        if (!PlausibleObj(ai)) continue;
        AITaskSytem* ts = *(AITaskSytem**)((char*)ai + 0x20);
        if (!PlausibleObj(ts)) continue;

        const std::string before = ts->describeCurrentGoal();
        const bool control = ((k++) % 2) == 1;
        SuppressCharacter(c, true);                              // already gated as a puppet; stated anyway
        if (WaitForNoAiPassOn(c, 20) == 2) ++g_handoffQuiesceTimedOut;
        ts->dropAllOrders();
        bool ok = true;
        if (!control)
        {
            ::Character* subj = p.intentSubject ? FindSpawned(p.intentSubject) : 0;
            ok = InjectOrder(c, p.intentType, subj, Ogre::Vector3(p.intentX, p.intentY, p.intentZ));
            if (ok) ++g_handoffInjected; else ++g_handoffInjectFailed;
        }
        else { PulseCharacterQuiet(c); ++g_handoffControl; }

        PendingHandoff& hnd = g_pendingHandoffs[g_pendingHandoffN++];
        hnd.uid = uid; hnd.control = control ? 1 : 0; hnd.wantType = p.intentType; hnd.tick = g_tick;
        strncpy(hnd.goalBefore, before.c_str(), 47); hnd.goalBefore[47] = 0;
        DebugLog("[HANDOFF] uid=" + N2(uid) + (control ? " CONTROL" : " INJECT") + " want=" + TaskTypeName(p.intentType)
                 + " (type " + N2(p.intentType) + ", subject uid " + N2(p.intentSubject) + ") goalBefore='" + before + "'"
                 + (ok ? "" : " INJECT FAILED"));
        ++done;
    }
    DebugLog("[HANDOFF] auto: dispatched=" + N2(done) + " skippedNoIntent=" + N2(g_handoffSkippedNoIntent)
             + " skippedAttack=" + N2(g_handoffSkippedAttack) + " skippedWindow=" + N2(g_handoffSkippedWindow) + " skippedOrderDrive=" + N2(g_handoffSkippedOrderDrive));
    return done;
}

// ~30 ticks after the pulse the pass has run on the worker; read what the copy decided.
static void ProcessPendingHandoffs()
{
    const std::string q(1, (char)34);
    for (int i = 0; i < g_pendingHandoffN; )
    {
        PendingHandoff& hnd = g_pendingHandoffs[i];
        if (g_tick < hnd.tick + 30) { ++i; continue; }
        std::string after = "UNREADABLE"; int ordersAfter = -1, needGoap = -1, taskInstalled = -1;
        std::map<unsigned int, Puppet>::iterator it = g_puppets.find(hnd.uid);
        ::Character* c = (it != g_puppets.end()) ? it->second.ch : 0;
        if (PlausibleObj(c) && !IsRetiredObject(c))
        {
            void* ai = GetCharacterAI(c);
            AITaskSytem* ts = PlausibleObj(ai) ? *(AITaskSytem**)((char*)ai + 0x20) : 0;
            if (PlausibleObj(ts))
            {
                WaitForNoAiPassOn(c, 5);
                after = ts->describeCurrentGoal();
                ordersAfter = ts->hasPendingOrders() ? 1 : 0;
                ReadTaskSystemFlags(ts, &needGoap, &taskInstalled);
            }
        }
        const std::string want = TaskTypeName(hnd.wantType);
        const bool match = (after == want);
        if (match) { if (hnd.control) ++g_handoffControlMatched; else ++g_handoffMatched; }
        DebugLog("[VERDICT] {" + q + "ev" + q + ":" + q + "handoff" + q
                 + "," + q + "uid" + q + ":" + N2(hnd.uid)
                 + "," + q + "control" + q + ":" + N2(hnd.control)
                 + "," + q + "wantType" + q + ":" + N2(hnd.wantType)
                 + "," + q + "wantName" + q + ":" + q + want + q
                 + "," + q + "goalBefore" + q + ":" + q + std::string(hnd.goalBefore) + q
                 + "," + q + "goalAfter" + q + ":" + q + after + q
                 + "," + q + "match" + q + ":" + (match ? "1" : "0")
                 + "," + q + "needGOAP" + q + ":" + N2(needGoap)
                 + "," + q + "taskInstalled" + q + ":" + N2(taskInstalled)
                 + "," + q + "ordersAfter" + q + ":" + N2(ordersAfter)
                 + "," + q + "tick" + q + ":" + N2(g_tick) + "}");
        g_pendingHandoffs[i] = g_pendingHandoffs[--g_pendingHandoffN];
    }
}

// PROBE-START: P071 (T259; REMOVE once the copy speed and placement-height questions are answered -
// 04-probes.md). Around every placement: the requested point, CharMovement+0x334 (floorGroup) as it
// stood, and on the NEXT main-thread frame the character's own position fields (logical +0x48, and
// AnimationClass+0x98 - the two getPosition layers F193 names) and +0x334 again. Pure field reads
// under fault guards; no engine call; no state change. 64 lines per run.
namespace {
struct P071Rec { unsigned int uid; void* ch; int site; float x, y, z; int floorBefore; int armed; int used; };
const int kP071Slots = 16;
const int kP071Cap   = 64;
P071Rec g_p071[kP071Slots];
int g_p071Lines   = 0;   // reserved at note time, so the cap is exact
int g_p071Dropped = 0;   // table full at note time

const int kP071Fault = (int)0x80000000;
int P071ReadFloor(void* ch)
{
    int v = kP071Fault;
    __try
    {
        const char* mv = *(const char* const*)((const char*)ch + 0x640);   // Character::movement
        if (mv) v = *(const int*)(mv + 0x334);                               // CharMovement::floorGroup
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { v = kP071Fault; }
    return v;
}
int P071ReadPos(void* ch, float* lg, float* an)   // 0 fault, 1 logical only, 2 both
{
    int r = 0;
    __try
    {
        const float* l = (const float*)((const char*)ch + 0x48);            // kLogicalPosOff
        lg[0] = l[0]; lg[1] = l[1]; lg[2] = l[2];
        r = 1;
        const char* anim = *(const char* const*)((const char*)ch + 0x448);  // kAnimationClassOff
        if (anim)
        {
            const float* a = (const float*)(anim + 0x98);                   // kAnimPosOff
            an[0] = a[0]; an[1] = a[1]; an[2] = a[2];
            r = 2;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}
bool P071StillMapped(unsigned int uid, void* ch)
{
    for (int i = 0; i < MirrorCapacity(); ++i)
    {
        unsigned int u = 0; Character* c = 0;
        if (MirrorSlot(i, &u, &c) && (void*)c == ch) return u == uid;
    }
    return false;
}
const char* P071SiteName(int site)
{
    static const char* k[] = { "prone", "stuck", "far", "authsnap", "twin", "spawn" };
    return (site >= 0 && site < 6) ? k[site] : "?";
}
} // namespace

void P071NotePlacement(unsigned int uid, void* ch, int site, float x, float y, float z)
{
    if (ch == 0 || g_p071Lines >= kP071Cap) return;
    for (int i = 0; i < kP071Slots; ++i)
    {
        if (g_p071[i].used) continue;
        P071Rec& r = g_p071[i];
        r.uid = uid; r.ch = ch; r.site = site; r.x = x; r.y = y; r.z = z;
        r.floorBefore = P071ReadFloor(ch);
        r.armed = 0; r.used = 1;
        ++g_p071Lines;
        return;
    }
    ++g_p071Dropped;
}

void P071Tick()
{
    for (int i = 0; i < kP071Slots; ++i)
    {
        P071Rec& r = g_p071[i];
        if (!r.used) continue;
        if (!r.armed) { r.armed = 1; continue; }   // noted this frame: report on the next one
        char buf[400];
        if (!P071StillMapped(r.uid, r.ch))
        {
            _snprintf_s(buf, sizeof(buf), _TRUNCATE, "[P071] place site=%s uid=%u req=(%.1f,%.1f,%.1f) floor0=%d -> GONE (no longer mapped) dropped=%d",
                      P071SiteName(r.site), r.uid, r.x, r.y, r.z, r.floorBefore, g_p071Dropped);
        }
        else
        {
            float lg[3] = { 0, 0, 0 }, an[3] = { 0, 0, 0 };
            const int got = P071ReadPos(r.ch, lg, an);
            const int floorAfter = P071ReadFloor(r.ch);
            _snprintf_s(buf, sizeof(buf), _TRUNCATE, "[P071] place site=%s uid=%u req=(%.1f,%.1f,%.1f) floor0=%d -> pos=(%.1f,%.1f,%.1f) anim=(%.1f,%.1f,%.1f)%s dY=%.1f animdY=%.1f floor1=%d dropped=%d",
                      P071SiteName(r.site), r.uid, r.x, r.y, r.z, r.floorBefore,
                      lg[0], lg[1], lg[2], an[0], an[1], an[2],
                      got == 0 ? " READFAULT" : (got == 1 ? " NOANIM" : ""),
                      lg[1] - r.y, got == 2 ? an[1] - r.y : 0.0f, floorAfter, g_p071Dropped);
        }
        r.used = 0;
        DebugLog(std::string(buf));
    }
}
// PROBE-END: P071

// THE OWNER'S STOP (followplay): any character this game owns and streams (its player faction and every NPC it runs) that
// produced a moving report is ARMED; every frame (not only on the send pass) an armed one whose engine moving flag reads 0
// with its body still
// followplay::kStopStillFrames frames running sends ONE stop with its exact position, stamped with this game's clock
// (net::SendMoveStop). An unreadable flag falls back to followplay::kStopStillPasses still send passes. Disarmed on the send;
// the next moving report off the stop point sent arms it again (the first report after the stop still reads moving while it
// sits on the point). A failed send (no road) is tried again the next frame.
// SCALE: the watch holds only characters between a moving report SENT and their stop (each leaves on its stop, its disarm,
// or when it is gone or no longer this game's), so it is bounded by the send set - the mirror's rows, kMaxSentSamples - and
// sized to it: no stop is dropped for room. The per-frame check costs one position read and one flag read per character
// that is walking right now; a standing character is not in it.
namespace {
struct FollowOwnWatch { bool armed; int stillFrames, stillPasses; bool havePos; float lx, lz; long long stillTick; double stillAt; };
const size_t kFollowOwnMax = (size_t)kMaxSentSamples;   // the send set: every character this game streams can be watched at once
std::map<unsigned int, FollowOwnWatch> g_followOwn;
struct FollowOwnSent { float x, z; };
std::map<unsigned int, FollowOwnSent> g_followOwnSent;   // the stop point last sent per character, until it moves off it
// PROBE-START: P123
static void P123OwnerStop(unsigned int uid, double bodyStopAt, long long frames, double now, int eng, const Ogre::Vector3& at);
// PROBE-END: P123
}   // namespace

// The stop point just sent for uid, kept until the character moves off it, is despawned or unloaded (FollowOwnForget), or
// found gone. At most kFollowOwnMax (the send set): when full, characters no longer this game's, and characters gone (no live
// object: FindSpawned answers 0 for a retired one - killed or put away), leave first; still full, the point is not kept.
static void FollowOwnNoteSent(unsigned int uid, float x, float z)
{
    if (g_followOwnSent.size() >= kFollowOwnMax && g_followOwnSent.find(uid) == g_followOwnSent.end())
    {
        for (std::map<unsigned int, FollowOwnSent>::iterator s = g_followOwnSent.begin(); s != g_followOwnSent.end(); )
        {
            ::Character* c = net::IsUidMine(s->first) ? FindSpawned(s->first) : 0;
            if (!PlausibleObj(c) || IsRetiredObject(c)) { g_followOwnSent.erase(s++); ++g_followOwnSentEvicted; }
            else ++s;
        }
        if (g_followOwnSent.size() >= kFollowOwnMax) return;
    }
    FollowOwnSent v; v.x = x; v.z = z;
    g_followOwnSent[uid] = v;
}

// A character of this game's that the engine despawned or unloaded (NotifyDespawn, main thread) leaves the owner's stop watch
// and its sent stop point: neither is needed again for that object.
void FollowOwnForget(unsigned int uid)
{
    g_followOwn.erase(uid);
    g_followOwnSent.erase(uid);
}

// The owner's send pass, per sample at (x, z): a moving report arms the stop for the character (whatever its faction or
// task), except while it still sits on the stop point just sent; a still one counts toward the unreadable-flag fallback.
static void FollowOwnerNoteSample(unsigned int uid, int verdict, float x, float z)
{
    std::map<unsigned int, FollowOwnWatch>::iterator w = g_followOwn.find(uid);
    if (verdict == followplay::kSampleMoving)
    {
        std::map<unsigned int, FollowOwnSent>::iterator sent = g_followOwnSent.find(uid);
        if (sent != g_followOwnSent.end())
        {
            if (!followplay::MovedOffSentStop(x, z, sent->second.x, sent->second.z)) return;   // the stop's trailing report
            g_followOwnSent.erase(sent);
        }
        if (w == g_followOwn.end())
        {
            if (g_followOwn.size() >= kFollowOwnMax) return;
            FollowOwnWatch z; std::memset(&z, 0, sizeof(z));
            w = g_followOwn.insert(std::make_pair(uid, z)).first;
            if (g_followOwn.size() > g_followOwnPeak) g_followOwnPeak = g_followOwn.size();
        }
        w->second.armed = followplay::OwnerStopArm(w->second.armed, verdict);
        w->second.stillPasses = 0;
        return;
    }
    if (w != g_followOwn.end()) w->second.stillPasses = followplay::StillPassesStep(w->second.stillPasses, verdict);
}

// Every frame: the stop check of the armed characters (the ones walking now). A character no longer this game's, gone or
// disarmed leaves the watch.
static void FollowOwnerStopFrame()
{
    if (g_followOwn.empty()) return;
    const double now = NowSeconds();
    for (std::map<unsigned int, FollowOwnWatch>::iterator it = g_followOwn.begin(); it != g_followOwn.end(); )
    {
        FollowOwnWatch& w = it->second;
        ::Character* c = FindSpawned(it->first);
        if (!w.armed || !PlausibleObj(c) || IsRetiredObject(c) || !net::IsUidMine(it->first)) { g_followOwn.erase(it++); continue; }
        Ogre::Vector3 pos;
        if (!SafeReadPosition(c, &pos)) { ++it; continue; }
        if (w.havePos)
        {
            const float mx = pos.x - w.lx, mz = pos.z - w.lz;
            w.stillFrames = followplay::StillFramesStep(w.stillFrames, sqrtf(mx * mx + mz * mz));
            if (w.stillFrames == 1) { w.stillTick = g_tick; w.stillAt = now; }   // the first frame the body did not move
        }
        w.havePos = true; w.lx = pos.x; w.lz = pos.z;
        const int eng = AuthorityEngineMoving(c);
        if (followplay::OwnerStopDue(w.armed, eng, w.stillFrames, w.stillPasses))
        {
            if (net::SendMoveStop(it->first, pos.x, pos.y, pos.z, FollowStampMs(now)))
            {
                ++g_followStopSent;
                const bool ownPlayer = FollowOwnPlayerPod(c) == 1;
                if (!ownPlayer) ++g_followStopSentNpc;
                FollowOwnNoteSent(it->first, pos.x, pos.z);
                if (ownPlayer) P123OwnerStop(it->first, w.stillFrames > 0 ? w.stillAt : now, w.stillFrames > 0 ? g_tick - w.stillTick : -1, now, eng, pos);   // PROBE P123: player-faction characters only
                g_followOwn.erase(it++);
                continue;
            }
            ++g_followStopSendFailed;
        }
        ++it;
    }
}

// PROBE-START: P115
// T-416 (report-only, the owner's report: on the OTHER player's screen a character runs PAST the clicked spot after each
// move order, turns and runs back). Two halves, each game runs both:
// OWNER (this game owns the character; only this game's player-faction characters, at most kP115MaxUids): the movement
// destination CharMovement +0xDC (ReadDestination, the field the INTENT stream already sends) changing by >= 2 units is a move
// order -> one `move order` line; the position sampled every 0.1 s falling under 0.5 u/s for 0.5 s after moving >= 2 u/s is the
// arrival -> one `arrive` line. COPY (g_puppets rows whose faction is the other player's, PeerFactionPod == 1, at most
// kP115MaxUids): an episode opens on the first MOVE whose velocity is >= 2 u/s; the first later MOVE under 0.5 u/s is the
// owner's stop point; the copy's position is sampled every 0.1 s; when the copy has been under 0.5 u/s for 1 s (or 30 s passed,
// or the owner moved again) one `copy settle` line: the overshoot = the copy's farthest point past the owner's stop along the
// trip's line (episode start -> owner's stop), back = how far it then came back. Reads only (positions via SafeReadPosition,
// +0xDC, +0xBC, RunCeilingRead, Puppet::rbActive, the faction); writes no game memory; changes no drive decision. Main thread.
namespace {
const int    kP115MaxUids      = 10;     // per side
const double kP115SampleSec    = 0.1;
const float  kP115MovingSpd    = 2.0f;   // u/s
const float  kP115StillSpd     = 0.5f;   // u/s
const double kP115CopyHoldSec  = 1.0;    // copy settled: still this long
const double kP115OwnHoldSec   = 0.5;    // owner arrived: still this long
const double kP115RateSec      = 3.0;    // per uid per line kind
const double kP115TimeoutSec   = 30.0;   // owner stopped, copy never settled
const float  kP115OrderMinMove = 2.0f;   // a destination moving less than this is not a new order
const int    kP115LineCap      = 400;
const int    kP115MaxTrail     = 900;    // 90 s of 0.1 s samples per episode

struct P115Own
{
    bool haveDest; float dx, dz;                         // last destination seen (+0xDC)
    bool haveOrder; double orderAt; float ox, oz, tx, tz; // the last order: when, from where, to where
    bool haveSample; double sampAt; float sx, sz;        // the last 0.1 s sample
    bool moving; double stillSince; float stopX, stopZ, maxSpd;
    double lastOrderLine, lastArriveLine;
};
struct P115Pt { float x, z; };
struct P115Cp
{
    bool epActive; double epStartAt; float startX, startZ, lastVx, lastVz;   // from the owner's MOVE stream
    bool ownStopped; double stopAt; float stopX, stopZ;
    std::vector<P115Pt> trail;                                              // the copy, every 0.1 s
    bool haveSample; double sampAt; float sx, sz, maxSpd; double stillSince;
    bool boost; double lastLine;
};
std::map<unsigned int, P115Own> g_p115Own;
std::map<unsigned int, P115Cp>  g_p115Cp;
std::map<unsigned int, double>  g_p115CpRejectAt;   // uids checked and not the other player's - re-checked after 5 s
std::vector<unsigned int> g_p115Scan; double g_p115OwnScanAt = -1.0;
long long g_p115Orders = 0, g_p115Arrivals = 0, g_p115Settles = 0, g_p115Over2 = 0, g_p115Backtracks = 0;
float g_p115MaxOver = 0.0f;
long long g_p115Timeouts = 0, g_p115Resumed = 0, g_p115RateLimited = 0, g_p115Lines = 0, g_p115Capped = 0;
} // namespace

static float P115Dist(float ax, float az, float bx, float bz) { const float x = ax - bx, z = az - bz; return sqrtf(x * x + z * z); }
static void P115Line(const char* buf)
{
    if (g_p115Lines >= kP115LineCap) { ++g_p115Capped; return; }
    ++g_p115Lines;
    DebugLog(std::string(buf));
}
static int P115OwnPlayerPod(::Character* c)   // 1 = this game's player faction, 0 = not, -1 = the read faulted
{
    __try { ::Faction* f = c->getOwnerFactionDirect(); return (f != 0 && IsPlayerFaction(f)) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static void P115OwnReset(P115Own& o)
{
    std::memset(&o, 0, sizeof(o));
    o.stillSince = -1.0; o.lastOrderLine = -1.0e9; o.lastArriveLine = -1.0e9;
}
static void P115CpOpen(P115Cp& s, float x, float z, double now)
{
    s.epActive = true; s.epStartAt = now; s.startX = x; s.startZ = z; s.lastVx = 0.0f; s.lastVz = 0.0f;
    s.ownStopped = false; s.stopAt = 0.0; s.stopX = 0.0f; s.stopZ = 0.0f;
    s.trail.clear(); s.haveSample = false; s.sampAt = 0.0; s.sx = 0.0f; s.sz = 0.0f; s.maxSpd = 0.0f; s.stillSince = -1.0;
    s.boost = false;
}

// OWNER: one character this game owns, every frame (the work is gated to 0.1 s samples, the destination read is one float x3).
static void P115OwnStep(unsigned int uid, ::Character* c, P115Own& o, double now)
{
    Ogre::Vector3 p;
    if (!SafeReadPosition(c, &p)) return;
    CharMovement* mv = c->movement;
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
    if (PlausibleObj(mv) && ReadDestination(mv, &dx, &dy, &dz) && Finite(dx) && Finite(dz))
    {
        const bool isNew = !o.haveDest || P115Dist(dx, dz, o.dx, o.dz) >= kP115OrderMinMove;
        if (isNew && o.haveDest)   // the first read is a baseline, never an order
        {
            ++g_p115Orders;
            o.haveOrder = true; o.orderAt = now; o.ox = p.x; o.oz = p.z; o.tx = dx; o.tz = dz; o.maxSpd = 0.0f;
            if (now - o.lastOrderLine >= kP115RateSec)
            {
                o.lastOrderLine = now;
                const float des = AuthorityDesiredSpeed(c);
                float run = 0.0f, animMax = 0.0f;
                const bool runOk = RunCeilingRead(c, &run, &animMax);
                char buf[320];
                _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                            "[PROBE] P115 move order uid=%u target=%.1f,%.1f from=%.1f,%.1f dist=%.1f run=%s des=%.1f runSpd=%.1f t=%.2f",
                            uid, dx, dz, p.x, p.z, P115Dist(dx, dz, p.x, p.z),
                            runOk ? (cooprunboost::OwnerRuns(des, run) ? "1" : "0") : "?", des, runOk ? run : -1.0f, now);
                P115Line(buf);
            }
            else ++g_p115RateLimited;
        }
        if (isNew) { o.haveDest = true; o.dx = dx; o.dz = dz; }
    }
    if (!o.haveSample) { o.haveSample = true; o.sampAt = now; o.sx = p.x; o.sz = p.z; return; }
    const double dt = now - o.sampAt;
    if (dt < kP115SampleSec) return;
    const double prevAt = o.sampAt;
    const float spd = P115Dist(p.x, p.z, o.sx, o.sz) / (float)dt;
    o.sampAt = now; o.sx = p.x; o.sz = p.z;
    if (Finite(spd) && spd > o.maxSpd) o.maxSpd = spd;
    if (spd >= kP115MovingSpd) { o.moving = true; o.stillSince = -1.0; return; }
    if (!o.moving) return;
    if (spd >= kP115StillSpd) { o.stillSince = -1.0; return; }
    if (o.stillSince < 0.0) { o.stillSince = prevAt; o.stopX = p.x; o.stopZ = p.z; }
    if (now - o.stillSince < kP115OwnHoldSec) return;
    o.moving = false;
    ++g_p115Arrivals;
    if (now - o.lastArriveLine >= kP115RateSec)
    {
        o.lastArriveLine = now;
        char buf[320];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                    "[PROBE] P115 arrive uid=%u at=%.1f,%.1f after=%.2f target=%.1f,%.1f miss=%.1f maxSpd=%.1f t=%.2f",
                    uid, o.stopX, o.stopZ, o.haveOrder ? o.stillSince - o.orderAt : -1.0,
                    o.haveOrder ? o.tx : 0.0f, o.haveOrder ? o.tz : 0.0f,
                    o.haveOrder ? P115Dist(o.stopX, o.stopZ, o.tx, o.tz) : -1.0f, o.maxSpd, o.stillSince);
        P115Line(buf);
    }
    else ++g_p115RateLimited;
    o.haveOrder = false; o.maxSpd = 0.0f;
}

// COPY: the episode ends - settled, timeout (owner stopped 30 s ago, copy still moving) or resumed (the owner moved again first).
static void P115CpFinish(unsigned int uid, P115Cp& s, double now, const char* why)
{
    s.epActive = false;
    if (s.trail.empty()) return;
    float ux = s.stopX - s.startX, uz = s.stopZ - s.startZ;
    float n = sqrtf(ux * ux + uz * uz);
    if (n < 2.0f) { ux = s.lastVx; uz = s.lastVz; n = sqrtf(ux * ux + uz * uz); }   // a short trip: the owner's last heading
    if (!(n > 0.001f)) { ux = 0.0f; uz = 0.0f; } else { ux /= n; uz /= n; }
    float maxAlong = -1.0e9f; size_t farI = 0;
    for (size_t i = 0; i < s.trail.size(); ++i)
    {
        const float a = (s.trail[i].x - s.stopX) * ux + (s.trail[i].z - s.stopZ) * uz;
        if (a > maxAlong) { maxAlong = a; farI = i; }
    }
    const P115Pt& e = s.trail.back();
    const float endAlong = (e.x - s.stopX) * ux + (e.z - s.stopZ) * uz;
    const float over = maxAlong > 0.0f ? maxAlong : 0.0f;
    const float back = maxAlong - endAlong;
    const int backtrack = (maxAlong > 0.5f && back >= 1.0f) ? 1 : 0;
    const bool settled = std::strcmp(why, "settled") == 0;
    if (settled)
    {
        ++g_p115Settles;
        if (over > 2.0f) ++g_p115Over2;
        if (over > g_p115MaxOver) g_p115MaxOver = over;
        if (backtrack) ++g_p115Backtracks;
    }
    else if (std::strcmp(why, "timeout") == 0) ++g_p115Timeouts;
    else ++g_p115Resumed;
    if (now - s.lastLine < kP115RateSec) { ++g_p115RateLimited; return; }
    s.lastLine = now;
    char buf[480];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "[PROBE] P115 copy settle uid=%u end=%s ownerStop=%.1f,%.1f copyFarthest=%.1f,%.1f overshoot=%.1f backtrack=%d back=%.1f"
                " settleDelay=%.2f maxCopySpeed=%.1f boostActive=%d copyEnd=%.1f,%.1f endDist=%.1f farAlong=%.1f trip=%.1f dirOk=%d samples=%d t=%.2f",
                uid, why, s.stopX, s.stopZ, s.trail[farI].x, s.trail[farI].z, over, backtrack, back,
                settled ? s.stillSince - s.stopAt : -1.0, s.maxSpd, s.boost ? 1 : 0, e.x, e.z, P115Dist(e.x, e.z, s.stopX, s.stopZ),
                maxAlong, P115Dist(s.stopX, s.stopZ, s.startX, s.startZ), (ux != 0.0f || uz != 0.0f) ? 1 : 0, (int)s.trail.size(), now);
    P115Line(buf);
}

// COPY: every MOVE that passed ApplyRemoteMove's gates (main thread, the drain).
static void P115OnMove(unsigned int uid, float x, float z, float vx, float vz)
{
    const double now = NowSeconds();
    std::map<unsigned int, P115Cp>::iterator it = g_p115Cp.find(uid);
    if (it == g_p115Cp.end())
    {
        if ((int)g_p115Cp.size() >= kP115MaxUids) return;
        std::map<unsigned int, double>::iterator r = g_p115CpRejectAt.find(uid);
        if (r != g_p115CpRejectAt.end() && now - r->second < 5.0) return;
        std::map<unsigned int, Puppet>::iterator pi = g_puppets.find(uid);
        if (pi == g_puppets.end() || !PlausibleObj(pi->second.ch) || IsRetiredObject(pi->second.ch) || PeerFactionPod(pi->second.ch) != 1)
        { g_p115CpRejectAt[uid] = now; return; }
        g_p115CpRejectAt.erase(uid);
        P115Cp fresh; fresh.lastLine = -1.0e9; fresh.epActive = false;
        it = g_p115Cp.insert(std::make_pair(uid, fresh)).first;
        P115CpOpen(it->second, x, z, now); it->second.epActive = false;
    }
    P115Cp& s = it->second;
    const float spd = sqrtf(vx * vx + vz * vz);
    if (spd >= kP115MovingSpd)
    {
        if (s.epActive && s.ownStopped) P115CpFinish(uid, s, now, "resumed");
        if (!s.epActive) P115CpOpen(s, x, z, now);
        s.lastVx = vx; s.lastVz = vz;
        return;
    }
    if (s.epActive && !s.ownStopped && spd < kP115StillSpd) { s.ownStopped = true; s.stopX = x; s.stopZ = z; s.stopAt = now; }
}

static void P115CpStep(P115Cp& s, const Puppet& p, unsigned int uid, double now)
{
    if (!s.epActive) return;
    if (p.rbActive) s.boost = true;
    Ogre::Vector3 v;
    if (!SafeReadPosition(p.ch, &v)) return;
    if (!s.haveSample)
    {
        s.haveSample = true; s.sampAt = now; s.sx = v.x; s.sz = v.z;
        P115Pt q; q.x = v.x; q.z = v.z; s.trail.push_back(q);
        return;
    }
    const double dt = now - s.sampAt;
    if (dt < kP115SampleSec) return;
    const double prevAt = s.sampAt;
    const float spd = P115Dist(v.x, v.z, s.sx, s.sz) / (float)dt;
    s.sampAt = now; s.sx = v.x; s.sz = v.z;
    if (Finite(spd) && spd > s.maxSpd) s.maxSpd = spd;
    if ((int)s.trail.size() < kP115MaxTrail) { P115Pt q; q.x = v.x; q.z = v.z; s.trail.push_back(q); }
    else s.trail.back().x = v.x, s.trail.back().z = v.z;   // full: keep the latest as the end point
    if (spd >= kP115StillSpd) s.stillSince = -1.0;
    else if (s.stillSince < 0.0) s.stillSince = prevAt;
    if (!s.ownStopped) return;
    if (s.stillSince >= 0.0 && now - s.stillSince >= kP115CopyHoldSec) { P115CpFinish(uid, s, now, "settled"); return; }
    if (now - s.stopAt >= kP115TimeoutSec) P115CpFinish(uid, s, now, "timeout");
}

static void P115Frame()
{
    const double now = NowSeconds();
    if (g_p115OwnScanAt < 0.0 || now - g_p115OwnScanAt >= 1.0)   // refill the owner list once a second (cap kP115MaxUids)
    {
        g_p115OwnScanAt = now;
        if ((int)g_p115Own.size() < kP115MaxUids)
        {
            net::OwnedUidsSnapshot(&g_p115Scan);
            for (size_t i = 0; i < g_p115Scan.size() && (int)g_p115Own.size() < kP115MaxUids; ++i)
            {
                const unsigned int u = g_p115Scan[i];
                if (g_p115Own.count(u) != 0) continue;
                ::Character* c = FindSpawned(u);
                if (!PlausibleObj(c) || IsRetiredObject(c) || P115OwnPlayerPod(c) != 1) continue;
                P115Own o; P115OwnReset(o); g_p115Own[u] = o;
            }
        }
    }
    for (std::map<unsigned int, P115Own>::iterator it = g_p115Own.begin(); it != g_p115Own.end(); )
    {
        ::Character* c = FindSpawned(it->first);
        if (!PlausibleObj(c) || IsRetiredObject(c) || !net::IsUidMine(it->first)) { g_p115Own.erase(it++); continue; }
        P115OwnStep(it->first, c, it->second, now);
        ++it;
    }
    for (std::map<unsigned int, P115Cp>::iterator it = g_p115Cp.begin(); it != g_p115Cp.end(); )
    {
        std::map<unsigned int, Puppet>::const_iterator pi = g_puppets.find(it->first);
        if (pi == g_puppets.end() || !PlausibleObj(pi->second.ch) || IsRetiredObject(pi->second.ch)) { g_p115Cp.erase(it++); continue; }
        P115CpStep(it->second, pi->second, it->first, now);
        ++it;
    }
}

static std::string P115ReportFields()
{
    char b[400];
    _snprintf_s(b, sizeof(b), _TRUNCATE,
                " p115[orders,arrivals,settles,overshoots>2u,maxOvershoot,backtracks]=%lld,%lld,%lld,%lld,%.1f,%lld"
                " p115more[timeouts,resumed,rateLimited,lines,capped,ownTracked,copyTracked]=%lld,%lld,%lld,%lld,%lld,%d,%d",
                g_p115Orders, g_p115Arrivals, g_p115Settles, g_p115Over2, g_p115MaxOver, g_p115Backtracks,
                g_p115Timeouts, g_p115Resumed, g_p115RateLimited, g_p115Lines, g_p115Capped, (int)g_p115Own.size(), (int)g_p115Cp.size());
    return std::string(b);
}
// PROBE-END: P115

// PROBE-START: P124
// T-416 (report-only): does a copy of a NON-PLAYER character run past the point where its owner's character stopped. P115
// and P123 cover the other players' own characters; this measures the copies of every other character (g_puppets rows
// whose faction is not a player stand-in, PeerFactionPod == 0, at most kP124MaxUids at once) by the same rule whatever
// drives them, so its counts compare across builds. The owner's reports are
// read by p124::OnReport (src/common/p124stop.h): a moving report (>= 2 u/s) restarts the copy's 0.1 s samples and is kept
// as the last moving report; the first still report after it (< 0.5 u/s) is the stop; 3 s later one `npc stop` line:
// the last moving and still positions, the copy's farthest point along the owner's last heading past the still position
// (overshoot), whether it then came back (backtrack, back) and its distance to the still position (endDist). A moving report
// inside the 3 s drops the stop (counted resumed, no line). Reads only (SafeReadPosition, the faction); writes no game memory;
// changes no drive decision. Main thread. At most kP124LineCap lines per game, then counted.
namespace {
const int    kP124MaxUids    = 64;
const int    kP124LineCap    = 400;
const double kP124SampleSec  = 0.1;
const double kP124MeasureSec = 3.0;    // the copy's end distance is read this long after the owner's stop
const int    kP124MaxTrail   = 600;    // 60 s of 0.1 s samples
struct P124Cp
{
    int phase;
    float lastX, lastZ, lastVx, lastVz;          // the owner's last moving report
    double stopAt; float stillX, stillZ;         // the owner's stop
    std::vector<p124::Pt> trail;                 // the copy, every 0.1 s since the last moving report
    bool haveSample; double sampAt;
};
std::map<unsigned int, P124Cp> g_p124Cp;
std::map<unsigned int, double>  g_p124RejectAt;   // uids checked and not a non-player copy - re-checked after 5 s
long long g_p124Stops = 0, g_p124Over2 = 0, g_p124Backtracks = 0, g_p124Resumed = 0, g_p124Lines = 0, g_p124Capped = 0;
float g_p124MaxOver = 0.0f;
} // namespace

static void P124Line(const char* buf)
{
    if (g_p124Lines >= kP124LineCap) { ++g_p124Capped; return; }
    ++g_p124Lines;
    DebugLog(std::string(buf));
}

// COPY: every MOVE that passed ApplyRemoteMove's gates (main thread, the drain); `p` is the report's copy row.
static void P124OnMove(unsigned int uid, const Puppet& p, float x, float z, float vx, float vz)
{
    const double now = NowSeconds();
    std::map<unsigned int, P124Cp>::iterator it = g_p124Cp.find(uid);
    if (it != g_p124Cp.end() && p.followPlayer) { g_p124Cp.erase(it); return; }   // became a player's character (recruited)
    if (it == g_p124Cp.end())
    {
        if (p.followPlayer || (int)g_p124Cp.size() >= kP124MaxUids) return;
        std::map<unsigned int, double>::iterator r = g_p124RejectAt.find(uid);
        if (r != g_p124RejectAt.end() && now - r->second < 5.0) return;
        if (!PlausibleObj(p.ch) || IsRetiredObject(p.ch) || PeerFactionPod(p.ch) != 0) { g_p124RejectAt[uid] = now; return; }
        g_p124RejectAt.erase(uid);
        P124Cp fresh;
        fresh.phase = p124::kPhaseIdle; fresh.lastX = fresh.lastZ = fresh.lastVx = fresh.lastVz = 0.0f;
        fresh.stopAt = 0.0; fresh.stillX = fresh.stillZ = 0.0f; fresh.haveSample = false; fresh.sampAt = 0.0;
        it = g_p124Cp.insert(std::make_pair(uid, fresh)).first;
    }
    P124Cp& s = it->second;
    const int rep = p124::OnReport(&s.phase, sqrtf(vx * vx + vz * vz));
    if (rep == p124::kRepResumed) ++g_p124Resumed;
    if (rep == p124::kRepMoving || rep == p124::kRepResumed)
    {
        s.lastX = x; s.lastZ = z; s.lastVx = vx; s.lastVz = vz;
        s.trail.clear(); s.haveSample = false;
    }
    else if (rep == p124::kRepStop) { s.stopAt = now; s.stillX = x; s.stillZ = z; }
}

static void P124Finish(unsigned int uid, P124Cp& s, double now)
{
    s.phase = p124::kPhaseIdle;
    const int n = (int)s.trail.size();
    if (n == 0) return;
    const p124::Measure m = p124::MeasureStop(&s.trail[0], n, s.lastX, s.lastZ, s.lastVx, s.lastVz, s.stillX, s.stillZ);
    ++g_p124Stops;
    if (p124::CountsAsOvershoot(m.overshoot)) ++g_p124Over2;
    if (m.overshoot > g_p124MaxOver) g_p124MaxOver = m.overshoot;
    if (m.backtrack) ++g_p124Backtracks;
    const p124::Pt& f = s.trail[m.farIndex >= 0 ? m.farIndex : n - 1];
    char buf[480];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "[PROBE] P124 npc stop uid=%u lastMoving=%.1f,%.1f ownerSpd=%.1f still=%.1f,%.1f copyFarthest=%.1f,%.1f overshoot=%.1f"
                " backtrack=%d back=%.1f endDist=%.1f copyEnd=%.1f,%.1f heading=%d samples=%d t=%.2f",
                uid, s.lastX, s.lastZ, sqrtf(s.lastVx * s.lastVx + s.lastVz * s.lastVz), s.stillX, s.stillZ, f.x, f.z, m.overshoot,
                m.backtrack, m.back, m.endDist, s.trail[n - 1].x, s.trail[n - 1].z, m.headingOk, n, now);
    P124Line(buf);
}

static void P124Frame()
{
    const double now = NowSeconds();
    for (std::map<unsigned int, P124Cp>::iterator it = g_p124Cp.begin(); it != g_p124Cp.end(); )
    {
        std::map<unsigned int, Puppet>::const_iterator pi = g_puppets.find(it->first);
        if (pi == g_puppets.end() || !PlausibleObj(pi->second.ch) || IsRetiredObject(pi->second.ch)) { g_p124Cp.erase(it++); continue; }
        P124Cp& s = it->second;
        if (s.phase != p124::kPhaseIdle && (!s.haveSample || now - s.sampAt >= kP124SampleSec))
        {
            Ogre::Vector3 v;
            if (SafeReadPosition(pi->second.ch, &v))
            {
                s.haveSample = true; s.sampAt = now;
                p124::Pt q; q.x = v.x; q.z = v.z;
                if ((int)s.trail.size() < kP124MaxTrail) s.trail.push_back(q);
                else s.trail.back() = q;   // full: keep the latest as the end point
            }
        }
        if (s.phase == p124::kPhaseStopped && now - s.stopAt >= kP124MeasureSec) P124Finish(it->first, s, now);
        ++it;
    }
}

static std::string P124ReportFields()
{
    char b[300];
    _snprintf_s(b, sizeof(b), _TRUNCATE,
                " p124[npcStops,overshoot2u,backtracks,lines,capped]=%lld,%lld,%lld,%lld,%lld p124more[resumed,maxOvershoot,tracked]=%lld,%.1f,%d",
                g_p124Stops, g_p124Over2, g_p124Backtracks, g_p124Lines, g_p124Capped, g_p124Resumed, g_p124MaxOver, (int)g_p124Cp.size());
    return std::string(b);
}
// PROBE-END: P124

// PROBE-START: P123
// T-416 follow drive (report-only): what the follow copies replay and how they stop. COPY (follow copies of player stand-ins'
// characters, p.followPlayer, at most kP123MaxCopies - NPC copies are P124's): every 0.1 s while the owner moved within 3 s, one `f` line - owner time now and replayed (tPlay), the delay,
// the target's mode, T, N, e, the copy's position and speed, the owner's reported speed, lagT (owner time now minus the owner
// time at which the recorded route passes nearest the copy), lagU (the copy's distance to N along h), vum (the copy's +0xE4
// update mode: 1 every frame, 0 in turns), the live path destination and its kind, the pace factor, held, stop, path; a
// `jump` line for a step over 10 u in one 0.1 s sample (with vum); a `stop side=copy recv` line at a stop's receipt (the copy's
// distance short of the stop point along h) and a `stop side=copy settle` line when the copy then stands; a `held` line per
// never-past halt. OWNER: a `stop side=owner` line per stop sent (the first still frame, the send, the frames between, the
// engine flag; this game's player-faction characters only). Reads only; changes no decision; main thread. At most
// kP123LineCap lines per game, then counted.
namespace {
const int    kP123LineCap      = 1500;
const int    kP123MaxCopies    = 10;
const double kP123SampleSec    = 0.1;
const double kP123ActiveSec    = 3.0;
const float  kP123JumpDist     = 10.0f;
const float  kP123SettleSpd    = 0.5f;
const double kP123SettleMaxSec = 10.0;
struct P123Cp { bool have; double sampAt; float sx, sz; bool stopOpen; double stopRecvAt; };
std::map<unsigned int, P123Cp> g_p123;
long long g_p123Lines = 0, g_p123Capped = 0;

static void P123Line(const char* buf)
{
    if (g_p123Lines >= kP123LineCap) { ++g_p123Capped; return; }
    ++g_p123Lines;
    DebugLog(std::string(buf));
}
static const char* P123ModeName(int m)
{
    return m == followplay::kModeInterp ? "interp" : m == followplay::kModeExtrap ? "extrap" : m == followplay::kModeHeld ? "held" : "empty";
}
static const char* P123KindName(int k)
{
    return k == followplay::kDestStop ? "stop" : k == followplay::kDestGoal ? "goal" : k == followplay::kDestKeep ? "keep"
         : k == followplay::kDestNewest ? "newest" : "none";
}
static P123Cp* P123Slot(unsigned int uid)
{
    std::map<unsigned int, P123Cp>::iterator it = g_p123.find(uid);
    if (it != g_p123.end()) return &it->second;
    if ((int)g_p123.size() >= kP123MaxCopies) return 0;
    P123Cp z; std::memset(&z, 0, sizeof(z));
    return &g_p123.insert(std::make_pair(uid, z)).first->second;
}
static void P123OwnerStop(unsigned int uid, double bodyStopAt, long long frames, double now, int eng, const Ogre::Vector3& at)
{
    char buf[256];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "[PROBE] P123 stop side=owner uid=%u at=%.1f,%.1f bodyStopAt=%.3f sentAt=%.3f frames=%lld eng=%d t=%.3f",
                uid, at.x, at.z, bodyStopAt, now, frames, eng, now);
    P123Line(buf);
}
static void P123CopyStopRecv(unsigned int uid, const Puppet& p, double now)
{
    if (!p.followPlayer) return;
    P123Cp* st = P123Slot(uid);
    if (st == 0) return;
    st->stopOpen = true; st->stopRecvAt = now;
    Ogre::Vector3 v(0.0f, 0.0f, 0.0f);
    const bool ok = SafeReadPosition(p.ch, &v);
    const float dx = p.stopX - v.x, dz = p.stopZ - v.z;
    const float shortAlong = (ok && p.fHaveH) ? dx * p.fHX + dz * p.fHZ : -999.0f;
    char buf[320];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "[PROBE] P123 stop side=copy recv uid=%u at=%.1f,%.1f copy=%.1f,%.1f shortAlong=%.2f dist=%.2f vum=%d path=%d t=%.3f",
                uid, p.stopX, p.stopZ, v.x, v.z, shortAlong, ok ? sqrtf(dx * dx + dz * dz) : -1.0f, p.fVum, p.pathMode ? 1 : 0, now);
    P123Line(buf);
}
static void P123Held(unsigned int uid, const Puppet& p, const Ogre::Vector3& cur, double now)
{
    if (!p.followPlayer || P123Slot(uid) == 0) return;
    const float lx = p.stopHeld ? p.stopX : p.fNX, lz = p.stopHeld ? p.stopZ : p.fNZ;
    char buf[288];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "[PROBE] P123 held uid=%u at=%s limit=%.1f,%.1f copy=%.1f,%.1f along=%.2f ownerMoving=%d vum=%d t=%.3f",
                uid, p.stopHeld ? "stop" : "newest", lx, lz, cur.x, cur.z, (cur.x - lx) * p.fHX + (cur.z - lz) * p.fHZ,
                p.fMoving ? 1 : 0, p.fVum, now);
    P123Line(buf);
}
static void P123Frame()
{
    const double now = NowSeconds();
    for (std::map<unsigned int, Puppet>::const_iterator it = g_puppets.begin(); it != g_puppets.end(); ++it)
    {
        const Puppet& p = it->second;
        if (!p.follow || !p.followPlayer || !PlausibleObj(p.ch) || IsRetiredObject(p.ch)) continue;
        P123Cp* st = P123Slot(it->first);
        if (st == 0) continue;
        Ogre::Vector3 v;
        if (!SafeReadPosition(p.ch, &v)) continue;
        if (!st->have) { st->have = true; st->sampAt = now; st->sx = v.x; st->sz = v.z; continue; }
        const double dt = now - st->sampAt;
        if (dt < kP123SampleSec) continue;
        const float mx = v.x - st->sx, mz = v.z - st->sz;
        const float step = sqrtf(mx * mx + mz * mz);
        const float spd = step / (float)dt;
        const float fromX = st->sx, fromZ = st->sz;
        st->sampAt = now; st->sx = v.x; st->sz = v.z;
        const bool active = p.fLastMovingAt > 0.0 && now - p.fLastMovingAt <= kP123ActiveSec;
        if (active && step > kP123JumpDist)
        {
            char jb[288];
            _snprintf_s(jb, sizeof(jb), _TRUNCATE,
                        "[PROBE] P123 jump uid=%u step=%.1f dt=%.3f from=%.1f,%.1f to=%.1f,%.1f vum=%d path=%d mode=%s t=%.3f",
                        it->first, step, dt, fromX, fromZ, v.x, v.z, p.fVum, p.pathMode ? 1 : 0, P123ModeName(p.fMode), now);
            P123Line(jb);
        }
        if (active)
        {
            double tc = 0.0;
            const bool haveTc = followplay::TrackTimeNear(p.trk, v.x, v.z, &tc);
            const double ownNow = now - p.fClkOff;
            float lagU = -1.0f;
            if (p.fHaveN && p.fHaveH) lagU = (p.fNX - v.x) * p.fHX + (p.fNZ - v.z) * p.fHZ;
            else if (p.fHaveN) { const float nx = p.fNX - v.x, nz = p.fNZ - v.z; lagU = sqrtf(nx * nx + nz * nz); }
            char fb[640];
            _snprintf_s(fb, sizeof(fb), _TRUNCATE,
                        "[PROBE] P123 f uid=%u own=%.3f tPlay=%.3f D=%.3f mode=%s T=%.1f,%.1f N=%.1f,%.1f e=%.2f copy=%.1f,%.1f copyV=%.1f"
                        " ownV=%.1f lagT=%.3f lagU=%.2f vum=%d dest=%.1f,%.1f kind=%s pace=%.2f held=%d stop=%d path=%d t=%.3f",
                        it->first, ownNow, p.fTPlay, p.followD, P123ModeName(p.fMode), p.fTX, p.fTZ, p.fNX, p.fNZ, p.fE, v.x, v.z, spd,
                        p.fV, haveTc ? ownNow - tc : -1.0, lagU, p.fVum, p.pathHaveDest ? p.pathDestX : 0.0f, p.pathHaveDest ? p.pathDestZ : 0.0f,
                        P123KindName(p.pathHaveDest ? p.fIssuedKind : followplay::kDestNone), p.fPace, p.fHeld ? 1 : 0, p.stopHeld ? 1 : 0,
                        p.pathMode ? 1 : 0, now);
            P123Line(fb);
        }
        if (st->stopOpen && (spd < kP123SettleSpd || !p.stopHeld || now - st->stopRecvAt > kP123SettleMaxSec))
        {
            const float sx = p.stopX - v.x, sz = p.stopZ - v.z;
            char sb[288];
            _snprintf_s(sb, sizeof(sb), _TRUNCATE,
                        "[PROBE] P123 stop side=copy settle uid=%u after=%.3f endDist=%.2f shortAlong=%.2f end=%s t=%.3f",
                        it->first, now - st->stopRecvAt, sqrtf(sx * sx + sz * sz), p.fHaveH ? sx * p.fHX + sz * p.fHZ : -999.0f,
                        spd < kP123SettleSpd ? "still" : (!p.stopHeld ? "cleared" : "timeout"), now);
            P123Line(sb);
            st->stopOpen = false;
        }
    }
    for (std::map<unsigned int, P123Cp>::iterator it = g_p123.begin(); it != g_p123.end(); )
    {
        if (g_puppets.find(it->first) == g_puppets.end()) g_p123.erase(it++);
        else ++it;
    }
}
static std::string P123ReportFields()
{
    char b[96];
    _snprintf_s(b, sizeof(b), _TRUNCATE, " p123[lines,capped]=%lld,%lld", g_p123Lines, g_p123Capped);
    return std::string(b);
}
}  // namespace
// PROBE-END: P123
void ReplicateTick()
{
    ++g_tick;
    PathBudgetFrame();   // D1-b item 2: the per-frame setDestination budget, oldest waiting first
    // mirror1 (crash T487) C - release the rows of finished characters, BEFORE the drive walk below:
    // the reclaim drops puppets, so it must never run inside it. Every 30 frames (a 4096-row scan since T-354; T-354 fold 1).
    DrainOffThreadDespawns();   // T-304 (2): this game's off-thread destroys are announced here, before the reclaim frees their rows
    if ((g_tick % 30) == 0) MirrorReclaimDestroyed();

    // Drive every puppet EVERY frame, not on the send cadence: the target updates at
    // ~10 Hz but the push must be continuous or the character stutters between updates.
    for (std::map<unsigned int, Puppet>::iterator it = g_puppets.begin();
         it != g_puppets.end(); ++it)
    {
        DrivePuppet(it->first, it->second);
        RunBoostSettle(it->first, it->second);   // T-189 runboost: a raise the drive did not renew this frame ends
    }
    ProcessPendingHandoffs();   // H019: score handoffs whose pass has had time to run
    FollowOwnerStopFrame();   // every frame: the stated stop of this game's armed (walking) characters
    P115Frame();   // PROBE P115: owner move order / arrival lines, copy settle / overshoot lines (reads only)
    P124Frame();   // PROBE P124: non-player copies' stop / overshoot lines (reads only)
    P123Frame();   // PROBE P123: the follow copies' replay lines (reads only)

    if (g_tick % kSendEveryTicks != 0) return;

    // F152: STREAM EVERY UID WE OWN, not one chosen by hand.
    //
    // This used to send a single `g_ownedUid` picked by the `replicate` command, which was
    // right for the M2a spike that proved the mechanism (0.0 drift, T021) and wrong for
    // everything after it. `[M2] REPORT streaming=none puppets=0` then sat in every log for
    // weeks and was read as "P-3, a known gap" - a LABEL where a question belonged (F155).
    // What it actually meant: every spawn-replicated character was running its own AI,
    // locomotion and medicine on the peer, so the two players watched different events
    // (F154 - one instance had characters chasing, the other had them standing still).
    //
    // The cost is small and worth stating rather than assuming: 4 characters at 10 Hz on the
    // unreliable channel is ~1.1 KB/s. The mirror is walked instead of the std::map because
    // it is a fixed array of plain values (no rebalancing under a concurrent reader).
    int streamed = 0;
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int uid = 0;
        Character* c = 0;
        if (!MirrorSlot(i, &uid, &c)) continue;
        if (!net::IsUidMine(uid)) continue;      // commitment 3: never stream what we do not own
        if (!AnnouncedToPeer(uid)) continue;    // H030: never announced into the peer's world (M-A step 2) - nothing there to drive

        // F316. `MirrorSlot` already hides retired entries, which is why THIS loop survived 891
        // seconds at 98 uids in T086 while the report path died - but the animation-pointer guard
        // was missing here too, and this runs ~10 times a second for every owned character.
        Ogre::Vector3 p;
        if (!SafeReadPosition(c, &p)) continue;

        // F350 - MEASURE THE AUTHORITY'S OWN VELOCITY FROM TWO SAMPLES OF THE THING WE ALREADY
        // SEND. Deliberately not read out of the engine: `desiredMotion` is a unit direction and
        // the movement object's `moving`/`officiallyStopped` bytes are the engine's own BELIEFS
        // (they are labelled as outputs in the drive diagnostic above), whereas what the peer needs
        // is the thing it is being asked to reproduce - how far this body actually travelled per
        // second. Two positions and the ticks between them answer exactly that and depend on no
        // offset that could shift under us.
        // F353 - the modulo was a silent aliaser. `kMaxSentSamples` equals `kMaxMirror` today so it
        // is a no-op, but if the mirror were raised, slots 0 and 512 would alias, the uid guard
        // would make both permanently "first sample", and velocity would stop working for two
        // characters with **nothing in any log** - verbatim the outcome the comment above claimed
        // to prevent. Bounds-checked and counted instead.
        float fx0 = 0.0f, fz0 = 0.0f; AuthorityFacing(c, &fx0, &fz0);
        if (i >= kMaxSentSamples) { ++g_velNoSampleSlot; if (net::SendMove(uid, p.x, p.y, p.z, 0.0f, 0.0f, AuthorityDesiredSpeed(c), fx0, fz0, FollowStampMs(NowSeconds()))) { ++g_sent; ++streamed; } continue; }

        float vx = 0.0f, vz = 0.0f;
        SentSample& s = g_lastSent[i];
        const double nowSec = NowSeconds();
        MeasureVelocity(c, uid, p, s, nowSec, &vx, &vz);

        if (net::SendMove(uid, p.x, p.y, p.z, vx, vz, AuthorityDesiredSpeed(c), fx0, fz0, FollowStampMs(nowSec))) { ++g_sent; ++streamed; }
        FollowOwnerNoteSample(uid, s.sampleVerdict, p.x, p.z);   // a moving report arms this character's stated stop
        // H019 - the authority's CURRENT GOAL: sent when it changes, refreshed every 5 s. A Move order's goal that moved more
        // than 2 u is sent too (cooppath::GoalResendDue): the other game ends its copy's path at that goal, and a second
        // click under the same order would otherwise reach it only with the 5 s refresh.
        {
            int it2 = 0; unsigned int isubj = 0; float ix = 0, iy = 0, iz = 0; int iprio = 0;
            if (AuthorityIntent(c, &it2, &isubj, &ix, &iy, &iz, &iprio))
            {
                const bool changed = it2 != s.intentType || isubj != s.intentSubject;
                const bool refresh = nowSec - s.intentSentAt > 5.0;
                const bool goalDue = cooppath::GoalResendDue(it2 == kMoveOrderType, ix, iz, s.intentX, s.intentZ,
                                                             s.intentGoalOpen ? 1.0e9 : nowSec - s.intentSentAt);
                if ((changed || refresh || goalDue) && net::SendIntent(uid, it2, isubj, ix, iy, iz, iprio))
                {
                    const bool goalOnly = !changed && !refresh;
                    if (goalOnly) ++g_intentGoalResent;
                    s.intentGoalOpen = !goalOnly;   // a task change: the game may write the new goal a pass later
                    ++g_intentSent; s.intentType = it2; s.intentSubject = isubj; s.intentSentAt = nowSec; s.intentX = ix; s.intentZ = iz;
                }
            }
        }
        IkOwnerSay(uid, c, nowSec);   /* P25 fold 2: the owner says which building its own player-faction character is in */
    }
    g_streamedLastTick = streamed;

    // The explicit `replicate <uid>` target still streams, but ONLY if it is not already
    // covered by the mirror walk above - otherwise an owned spawn would be sent twice per
    // tick and the traffic counters would read double.
    if (g_ownedUid != 0 && FindSpawned(g_ownedUid) == 0)
    {
        Ogre::Vector3 pos;
        if (SafeReadPosition(g_ownedChar, &pos))            // F316
        {
            // F353 - and this one gets a velocity too. It was the caller left on zeros.
            float vx = 0.0f, vz = 0.0f;
            MeasureVelocity(g_ownedChar, g_ownedUid, pos, g_lastSentExplicit, NowSeconds(),
                            &vx, &vz);
            if (net::SendMove(g_ownedUid, pos.x, pos.y, pos.z, vx, vz, AuthorityDesiredSpeed(g_ownedChar), 0.0f, 0.0f, FollowStampMs(NowSeconds())))
            { ++g_sent; ++g_streamedLastTick; }
        }
    }
}

void AdoptRemotePuppet(unsigned int uid)
{
    // F152's actual fix. `ApplyRemoteSpawn` never called this - only `ApplyRemoteTask` and
    // the explicit `replicate` path did - so a SPAWN-replicated character was never a puppet
    // and never stopped deciding for itself. The gates have been proven since T014 (F059-F061
    // decisions, F047/F052/F058 locomotion, F056/F061 drive); they were simply never engaged
    // on this path.
    // mirror1 (crash T487) A - ONE RULE: a character the uid table does not hold LIVE under this uid is
    // never adopted. T487 adopted 69 characters the full table had refused; a puppet with no row is
    // invisible to DESPAWN and to the retired guard, and one was driven into freed memory. The refused
    // character is left to this engine's own AI - not suppressed, not driven. Logged once per uid.
    {
        const void* raw = SpawnedRawObject(uid);
        const unsigned int reg = raw != 0 ? FindSpawnedUid(raw) : 0;
        const int verdict = raw != 0 ? coopuid::AdoptDecision(reg, uid) : coopuid::kAdoptYes;
        if (verdict != coopuid::kAdoptYes)
        {
            if (verdict == coopuid::kAdoptRefusedUnregistered) ++g_adoptRefusedFull; else ++g_adoptRefusedOtherUid;
            if (g_adoptRefusedUids.insert(uid).second)
                ErrorLog("[M2] puppet NOT adopted uid=" + N2(uid) + " - the uid table does not hold its"
                         + std::string(verdict == coopuid::kAdoptRefusedUnregistered
                             ? " character (not registered - table full - or retired/released)"
                             : " character under this uid (the row belongs to another uid)")
                         + ". Left to this engine's AI and never driven (adoptRefusedFull /"
                           " adoptRefusedOtherUid; logged once per uid).");
            return;
        }
    }
    Character* c = FindSpawned(uid);
    if (c == 0 || !PlausibleObj(c))
    {
        ErrorLog("[M2] adopt refused: no local character for uid " + N2(uid));
        return;
    }

    if (!IsSuppressed(c))
        SuppressCharacter(c, true);

    // Register the puppet NOW rather than on the first MOVE. Waiting left a window in which
    // the character was suppressed but undriven - which is worse than either state alone,
    // because a suppressed character does not cancel its own movement (F062) and would coast.
    if (g_puppets.find(uid) == g_puppets.end())
    {
        // F316. If the position cannot be read safely the character is mid-teardown or retired,
        // and registering a puppet around it would give the drive a target of (0,0,0) - a puppet
        // ordered to walk to the origin from wherever it is.
        Ogre::Vector3 here;
        if (!SafeReadPosition(c, &here))
        {
            ErrorLog("[M2] puppet NOT adopted uid=" + N2(uid) + " - its position cannot be read"
                     " safely (retired, or the animation object is gone). Registering it would"
                     " have given the drive a target of 0,0,0 (F316).");
            return;
        }
        Puppet p;
        p.ch = c; p.tx = here.x; p.ty = here.y; p.tz = here.z;
        // F350 - a puppet adopted from a SPAWN has never received a MOVE, so it has no velocity
        // and no sample time. Both are set explicitly: `lastMoveAt` left uninitialised would
        // make the first frames compute a nonsense lead age against stack garbage, and although
        // a zero velocity means the lead is not applied, the age is still measured and counted.
        p.velX = 0.0f; p.velZ = 0.0f; p.lastMoveAt = NowSeconds(); p.moveIntervalSec = 0.0; p.lagSampleAt = 0.0;
        p.driving = false; p.everDriven = false; p.lastDrift = 0.0f; p.maxDrift = 0.0f;
        p.lastDriftY = 0.0f; p.maxDriftY = 0.0f; p.applied = 0; p.pronedAt = 0;
        p.snapTries = 0; p.snapLastDrift = 0.0f; p.snapGaveUp = false;
        p.snapPending = false; p.snapFromX = 0.0f; p.snapFromZ = 0.0f;
        // Far in the past, so the FIRST time a puppet is lost it reports immediately rather than
        // waiting out a throttle window it never entered.
        p.lastLostTick = -10000;
        p.speedSaved = false; p.speedOriginal = 0.0f;
        p.rbActive = false; p.rbCeiling = false; p.rbWrote = 0.0f; p.rbFrame = -1;   // T-189 runboost
        p.rbFailed = false; p.rbEaseAt = 0.0;   // runboost2 (T-195)
        p.progressCheckTick = 0; p.progressFromX = 0.0f; p.progressFromZ = 0.0f; p.progressFromDist = 0.0f;
        p.stalledWindows = 0; p.catchupPending = false; p.catchupDriftBefore = 0.0f;
        p.catchupFromX = 0.0f; p.catchupFromZ = 0.0f; p.catchupTries = 0; p.catchupGaveUp = false;
        p.progressFromTX = 0.0f; p.progressFromTZ = 0.0f; p.snapCause = 0; p.snapWatchWindows = -1; p.snapStrikes = 0;
        for (int si = 0; si < kSnapRateMax; ++si) p.snapTimes[si] = -1.0e9;
        p.snapTimesNext = 0; p.unstickEnded = false;
        p.nativeWindow = false; p.nativeMaxDrift = 0.0f; p.nativeSnapScorePending = false; p.nativeSnapTick = 0; p.rcSteps = 0; p.rcMaxStep = 0.0f; p.rcSumStep = 0.0f; p.rcLastTickAt = 0.0; p.rcReqMax = 0.0f; p.rcVisMaxStep = 0.0f; p.rcVisMaxRate = 0.0f; p.rcVisMaxRateDowned = 0.0f; p.rcVisMaxRateAny = 0.0f; p.rcVisLastX = p.rcVisLastY = p.rcVisLastZ = 0.0f; p.rcVisPrimed = false; p.rcLayerGapMax = 0.0f; p.rcLayerGapLast = 0.0f; p.rcDriftSum = 0.0; p.rcDriftN = 0; p.rcSatSteps = 0; p.rcBoostedSteps = 0; p.authDesired = -1.0f; p.authDesiredApplied = -2.0f; p.intentType = 0; p.intentSubject = 0; p.intentX = p.intentY = p.intentZ = 0.0f; p.intentPriority = 0; p.intentAt = 0.0; p.intentSeq = 0; p.insideSaid = -1; p.insideKey[0] = 0; p.insideAt = 0.0; p.insideHint = 0; p.catchingUp = false; p.orderActive = false; p.orderX = p.orderZ = 0.0f; p.orderTick = 0; p.orderCount = 0; p.orderSpeed = -1; p.unsticking = false; p.unstickTick = 0; p.faceX = p.faceZ = 0.0f; p.faceTick = 0;
        p.pathMode = false; p.pathHaveDest = false; p.pathDestX = p.pathDestZ = 0.0f; p.pathIssueAt = 0.0; p.pathAwait = false; p.pathAwaitTick = 0; p.pathFailNoted = false; p.pathLastX = p.pathLastZ = 0.0f; p.pathLastAt = 0.0; p.pathHoldUntil = 0.0; p.pathProgArmed = false; p.pathProgTick = 0; p.pathProgFromDistA = 0.0f; p.pathWantIssue = false; p.pathWantSince = 0.0; p.pathGrant = false; p.pathOwnerStillSince = 0.0; p.pathProgOwnerMoved = false; p.pathProgFromX = p.pathProgFromZ = 0.0f; p.pathDestLead = false; p.pathDestDirX = p.pathDestDirZ = p.pathLineX = p.pathLineZ = 0.0f; p.pathOwnerWasMoving = false; p.pathStopDue = false; p.pathArrivedNoted = false; p.pathWantPriority = false; p.pathTurnSince = 0.0; p.pathRetryAim = 0; p.ownPrimed = false; p.ownSpd = p.ownHdgX = p.ownHdgZ = 0.0f; p.pathIssueTick = 0; p.pathFailSeenClear = false; p.pathSeenPending = p.pathLanded = p.pathLandedFaultNoted = p.pathTooCloseNoted = p.pathLeadWalking = false;   // D1 / D1-b / D1-c / D1-d / D2 / D2-b / D2-c / D3
        p.follow = false; p.followPlayer = false; FollowReset(p);   // the follow drive starts empty; the first report decides whether it applies
        // PROBE-START: P093
        std::memset(&p.p093, 0, sizeof(p.p093));
        // PROBE-END: P093
        g_puppets[uid] = p;
        DebugLog("[M2] puppet ADOPTED uid=" + N2(uid)
                 + " (spawn-replicated; suppressed and driven from here)");
    }
}

bool AttackLocal(unsigned int attackerUid, unsigned int victimUid, int taskType)
{
    Character* attacker = FindSpawned(attackerUid);
    Character* victim   = FindSpawned(victimUid);
    if (attacker == 0 || victim == 0)
    {
        ErrorLog("[M3] attack refused: attacker=" + N2(attackerUid)
                 + " victim=" + N2(victimUid) + " - one of them is not a local spawn");
        return false;
    }
    if (!PlausibleObj(attacker) || !PlausibleObj(victim)) return false;

    void* ai = GetCharacterAI(attacker);
    if (!PlausibleObj(ai)) { ErrorLog("[M3] attack refused: attacker AI unreadable"); return false; }
    AITaskSytem* orders = *(AITaskSytem**)((char*)ai + 0x20);
    if (!PlausibleObj(orders)) { ErrorLog("[M3] attack refused: task system unreadable"); return false; }

    // Unlike a move order, an attack needs a SUBJECT: the engine identifies targets by
    // `hand`, not by pointer, so the order carries the victim's handle.
    const hand& target = victim->getHandle();
    Ogre::Vector3 loc(0.0f, 0.0f, 0.0f);

    // DO NOT suppress the attacker. T024 found the mistake: this is a LOCAL attacker we
    // own, and combat needs continuous re-evaluation - closing distance, choosing swings,
    // reacting. Suppressing it and allowing one pulse let it adopt the goal 'Attacking
    // target' and then freeze its decision-making, so it never closed (separation grew
    // from 8 to 18 units). Suppression belongs on a PUPPET whose decisions come from the
    // authority, not on the authority's own fighter.
    if (IsSuppressed(attacker))
        SuppressCharacter(attacker, false);

    // PROBE P010 (DIAGNOSTIC, H007) - logs the relationship, changes nothing. The
    // two-attempt cap on this symptom is still in force (F098). `isTargetEnemy` has the
    // signature of a GOAP requirement predicate, and every spawned character inherits the
    // reference character's faction (F093) - so attacker and victim are squadmates and an
    // attack order may be accepted yet unplannable. This prints the answer instead of
    // guessing it. AI::treatsAsEnemy(RootObjectBase*) is at the AI object, directly callable.
    {
        typedef bool (*IsEnemyFn)(void*, void*);
        IsEnemyFn treatsAsEnemy = (IsEnemyFn)coop::AddrAbs(kMig3AiIsEnemy);
        if (treatsAsEnemy != 0)
        {
            bool enemy = treatsAsEnemy(ai, victim);
            DebugLog(std::string("[P010] attacker uid=") + N2(attackerUid)
                     + " considers victim uid=" + N2(victimUid)
                     + (enemy ? " an ENEMY (attack is plannable)"
                              : " NOT an enemy (H007: GOAP requirement would fail)"));
        }
        else
        {
            DebugLog("[P010] treatsAsEnemy unresolved - relationship not measured");
        }
    }

    orders->issueOrder((TaskType)taskType, target, loc, true, false);

    DebugLog("[M3] attack ordered: uid " + N2(attackerUid) + " -> uid " + N2(victimUid)
             + " taskType=" + N2(taskType) + " (attacker left UNSUPPRESSED - it must keep"
             + " deciding to actually fight)");
    return true;
}

// F239 - GIVE THE PUPPET THE ORDER, not just the combat mode.
//
// T072 measured the difference and it is one line of the digest: with `mode=1` on both sides,
// the authority reads `goal='Attacking target' orders=1` and the peer reads
// `goal='Aimless' orders=0`. **Combat mode arrives; no goal and no orders come with it.**
//
// This injects the same order the authority's own fighter gets, and then allows exactly ONE
// decision pass so the suppressed puppet can adopt it - which is precisely what
// `PulseCharacter` was built for (F081/F082: consume the needGOAP flag an injected order
// raised, adopt THAT order, and go back to being suppressed).
//
// It is deliberately NOT `AttackLocal`: that function UN-suppresses the attacker, which is
// right for a character we own and fight with, and is exactly the free-running clone we do not
// want on the peer (F236: 491 units of drift and a self-chosen `Self preservation` goal).
//
// The distinction that makes this different from T071's failure: there, repeated pulses were
// given with NO order in place, so the AI chose its own goals. Here an order is injected first,
// so the decision it makes is about the order.
bool InjectAttackOrder(::Character* puppet, ::Character* victim, int taskType)
{
    if (!PlausibleObj(puppet) || !PlausibleObj(victim)) return false;

    void* ai = GetCharacterAI(puppet);
    if (!PlausibleObj(ai)) return false;
    AITaskSytem* orders = *(AITaskSytem**)((char*)ai + 0x20);
    if (!PlausibleObj(orders)) return false;

    // The engine identifies targets by `hand`, taken LOCALLY - handles are per-process.
    const hand& target = victim->getHandle();
    Ogre::Vector3 loc(0.0f, 0.0f, 0.0f);
    orders->issueOrder((TaskType)taskType, target, loc, true, false);

    // ONE decision pass, so a suppressed puppet can adopt the order it was just handed. The
    // gate closes again immediately afterwards - PulseCharacter allows a single pass, not a
    // window.
    PulseCharacterQuiet(puppet);
    return true;
}

// F242 - AND TAKE THE ORDER BACK WHEN THE FIGHT ENDS.
//
// T073 measured what happens without this, and the number is stark: **756 of the peer's 947
// swings happened AFTER the authority had stopped fighting.** At t=255 the authority read
// `orders=0 mode=0 goal='Aimless'` on all four characters; the peer still read `orders=1`, still
// held `goal='Attacking target'`, and was still swinging **87 seconds later**. Drift reached
// 200-410 units.
//
// The order was injected and nothing ever retracted it. An order is a LEVEL, not an edge - the
// same shape as F062, where a driven puppet keeps walking because ceasing to push is not the
// same as stopping. **Anything we set on a puppet, we have to be able to unset.**
//
// `dropAllOrders()` is the engine's own, and a pulse follows so the suppressed puppet actually
// re-plans without the order rather than continuing to execute the goal it already adopted.
bool ClearAttackOrder(::Character* puppet)
{
    if (!PlausibleObj(puppet)) return false;

    void* ai = GetCharacterAI(puppet);
    if (!PlausibleObj(ai)) return false;
    AITaskSytem* orders = *(AITaskSytem**)((char*)ai + 0x20);
    if (!PlausibleObj(orders)) return false;

    orders->dropAllOrders();

    // F247 - AND CLEAR THE GOAL, WITHOUT A DECISION PASS.
    //
    // This used to call `PulseCharacterQuiet` here, reasoning that the puppet needed a pass to
    // notice the order was gone. T075 measured what that pass actually bought: **73 milliseconds
    // after the flag went down, the peer's combat mode was back UP on its own**, it adopted
    // `goal='Self preservation'` - its own, not one we injected - and swung 28 more times while
    // the authority's copy had stopped.
    //
    // The pass was not "noticing the order was gone". It was **a decision opportunity**, and the
    // AI used it to choose for itself. That is the T071 failure arriving through a much smaller
    // door: one pulse is enough, and it does not matter that the order was deleted first.
    //
    // So the goal is removed directly instead. `dropGoals` is the engine's own, and clearing
    // the goal is what the pulse was trying to achieve indirectly - **with no window in which
    // the AI gets to pick a replacement.**
    orders->dropGoals();
    return true;
}

bool SendTask(unsigned int uid, int taskType, float x, float y, float z)
{
    if (!net::SendTaskMsg(uid, taskType, x, y, z))
    {
        DebugLog("[M2b] task not sent: no live link");
        return false;
    }
    DebugLog("[M2b] -> TASK uid=" + N2(uid) + " type=" + N2(taskType)
             + " at " + F2(x) + "," + F2(z));
    return true;
}

void ApplyRemoteTask(unsigned int uid, int taskType, float x, float y, float z)
{
    Character* c = FindSpawned(uid);
    if (c == 0)
    {
        DebugLog("[M2b] TASK for unknown uid=" + N2(uid) + " - no local counterpart (F079)");
        return;
    }
    if (!PlausibleObj(c)) return;

    // Suppress FIRST if not already: injecting an order into a character that is still
    // deciding for itself would race its own goal selection.
    if (!IsSuppressed(c))
        SuppressCharacter(c, true);

    // The engine's own order-injection point (F059/F081). clearOld=true so the puppet
    // follows the authority's latest intent rather than accumulating a queue; shift=false.
    // The `hand` subject is empty - this is a location order, not a target order.
    hand subject;
    Ogre::Vector3 loc(x, y, z);

    // Character -> AI (Character+0x650) -> AITaskSytem (AI+0x20), which IS-A OrdersReceiver.
    // Offsets are header-confirmed and both hops are validated: a wrong pointer here would
    // call a virtual on garbage.
    void* ai = GetCharacterAI(c);
    if (!PlausibleObj(ai))
    {
        ErrorLog("[M2b] TASK refused: AI pointer failed validation for uid=" + N2(uid));
        return;
    }
    AITaskSytem* orders = *(AITaskSytem**)((char*)ai + 0x20);
    if (!PlausibleObj(orders))
    {
        ErrorLog("[M2b] TASK refused: taskSystemAI failed validation for uid=" + N2(uid));
        return;
    }
    orders->issueOrder((TaskType)taskType, subject, loc, true, false);

    // issueOrder raises needGOAP, and ONLY the function we just gated consumes it (F081).
    // One pulse lets the puppet adopt the order - and per F082, a player order
    // short-circuits goal selection, so it adopts THIS order rather than choosing freely.
    PulseCharacter(c);

    DebugLog("[M2b] <- TASK uid=" + N2(uid) + " type=" + N2(taskType)
             + " injected at " + F2(x) + "," + F2(z) + ", pulse armed");
}

// F305. `desiredSpeed` is the ONLY thing that carries magnitude into a STEER_BY_DIRECTION drive,
// and this drive has never written it. Default 0 = leave it alone, so the untouched value can
// be measured before anything is changed.
// F322 - THE FIRST PUPPET RELEASE PATH IN THIS PROJECT.
//
// An adversarial review found that nothing anywhere erased from `g_puppets` - no `erase`, no
// `clear`, no release function. That was survivable only because a puppet outliving its character
// was already prevented by every reader validating pointers; it stopped being survivable the moment
// the authority could TELL us a character is gone, because leaving the puppet behind would drive a
// character we had just removed.
//
// Restores the engine's own `desiredSpeed` on the way out if the lever ever wrote it - lesson 21,
// anything we set on a puppet we must be able to unset, and "the character is going away" is
// exactly when an unset gets forgotten.
// F338 - **`restoreSpeed=false` EXISTS FOR ONE CALLER AND IT IS NOT AN OPTIMISATION.** The restore
// writes through `p.ch`, the address this puppet was bound to. Normally that is the right object.
// On the stale-uid path (F337) it provably is NOT: the decline happens precisely because the engine
// recycled that address and we re-adopted it under a different uid, so `p.ch` now points at
// **someone else's live character** - and it passes `PlausibleObj`, because it is a perfectly valid
// character. The restore would write this uid's saved `desiredSpeed` into that character's movement.
//
// Inert on a default build (the `drivespeed` lever is off, so `speedSaved` is never set) and not a
// memory-safety problem either way. It is a correctness problem the moment the lever is used, and
// the guard that would normally catch it - "is this pointer plausible" - is exactly the guard that
// cannot see the difference.
void DropPuppet(unsigned int uid, bool restoreSpeed)
{
    std::map<unsigned int, Puppet>::iterator it = g_puppets.find(uid);
    if (it == g_puppets.end()) { ++g_dropNoPuppet; return; }

    Puppet& p = it->second;

    // H015 / P059 - a puppet dropped mid-window would leave its AI RELEASED with nobody to close
    // the window: a free-running clone with no owner. Re-gate before forgetting it.
    if (p.nativeWindow)
    {
        bool regated = false;
        if (restoreSpeed && PlausibleObj(p.ch))
        {
            SuppressCharacter(p.ch, true);
            regated = IsSuppressed(p.ch);   // the gate can refuse (slots full, AI unreadable)
            DebugLog("[NC] window closed by DROP uid=" + N2(uid)
                     + (regated ? " - AI re-gated before forgetting" : " - RE-GATE REFUSED, AI left free"));
        }
        else
        {
            ErrorLog("[NC] window dropped WITHOUT re-gate uid=" + N2(uid)
                     + (restoreSpeed ? " - character unreadable" : " - stale-uid path (address may belong to a stranger)"));
        }
        p.nativeWindow = false;
        NoteNativeWindowDropped(uid, regated);
    }

    // F353 - **STOP THE CHARACTER BEFORE FORGETTING IT.**
    //
    // Under F062 ceasing to push does not stop a suppressed character - it keeps walking on the
    // last vector we gave it, forever, and after this erase nobody is left to stop it. That was
    // survivable before F350 only by accident: a puppet in its normal state sat inside the arrival
    // deadband and had already been zeroed by the arrival stop, so a drop at that moment was safe.
    // With the lead applied, a puppet whose authority is walking carries a LIVE `desiredMotion` and
    // `moveLimit` at every instant, so the accident no longer covers us.
    //
    // Gated on `restoreSpeed` for exactly the reason F338 gates the speed restore on it: on the
    // stale-uid path `p.ch` may be a recycled address belonging to a different LIVE character, and
    // writing a stop through it would halt a stranger. Every ordinary caller passes true.
    // F355 - THREE outcomes, three counters. The first version had two branches and a silent third
    // case (`restoreSpeed` true, character unreadable) - which is precisely the T090 `mv=NOCHAR`
    // cohort this whole release path exists for, so the one bucket that went uncounted was the
    // interesting one. **F357 - and the identity that comment then asserted was itself wrong in
    // both directions:** it named four buckets where there are six, and the `it == end()` return
    // above was counted nowhere at all. All six - `dropStopped`, `dropNoMovement`, `dropNoChar`,
    // `dropStopFaulted`, `dropStopSkipped`, `dropNoPuppet` - now sum to the number of calls.
    // A copy on a path (p.pathMode) leaves it too (SafeLeavePath): a zero direction alone leaves the engine's path request
    // live, and a character handed to this game's AI (UnpuppetForOwnership) would walk on to a destination the drive set.
    if (!restoreSpeed)             ++g_dropStopSkipped;
    else if (!PlausibleObj(p.ch))  ++g_dropNoChar;
    else
    {
        CharMovement* mv = p.ch->movement;
        if (!PlausibleObj(mv))     ++g_dropNoMovement;
        else if (p.pathMode ? SafeLeavePath(mv) : SafeStopMovement(mv)) ++g_dropStopped;
        else                       ++g_dropStopFaulted;
    }

    // T-189 runboost: a raised ceiling goes back before the row is forgotten - the drop, and the handoff (UnpuppetForOwnership:
    // a copy that becomes this game's own NPC never keeps a boosted max). The stale-uid path writes nothing (restoreSkipped).
    RunBoostEnd(p, restoreSpeed && RunBoostWritable(uid, p));

    if (restoreSpeed && p.speedSaved && PlausibleObj(p.ch)
        && RestoreDesiredSpeed(p.ch->movement, p.speedOriginal))
        ++g_speedRestored;
    else if (!restoreSpeed && p.speedSaved)
        ++g_speedRestoreSkipped;

    g_puppets.erase(it);
}

// mirror1 (crash T487) B - the puppet list asked directly, so a puppet can always be dropped.
bool HasPuppet(unsigned int uid) { return g_puppets.find(uid) != g_puppets.end(); }
/* M7a A1 build 1 [a1b1-rp0]: every uid this game holds a puppet (copy) for - the orphan-copy rule (net/session.cpp). MAIN THREAD. */
void PuppetUidsSnapshot(std::vector<unsigned int>* out)
{
    out->clear();
    for (std::map<unsigned int, Puppet>::const_iterator it = g_puppets.begin(); it != g_puppets.end(); ++it) out->push_back(it->first);
}

// mirror1 (crash T487) C - for the reclaim of a destroyed row: forget the uid's puppet only if it
// still names that object, and write nothing through the pointer (DropPuppet(uid, false)).
void DropPuppetIfObject(unsigned int uid, const void* obj)
{
    std::map<unsigned int, Puppet>::iterator it = g_puppets.find(uid);
    if (it == g_puppets.end() || (const void*)it->second.ch != obj) return;
    ++g_puppetsDroppedReclaim;
    DropPuppet(uid, false);
}

long long AdoptRefusedFullCount() { return g_adoptRefusedFull; }

bool SetPuppetNativeWindow(unsigned int uid, bool on)
{
    std::map<unsigned int, Puppet>::iterator it = g_puppets.find(uid);
    if (it == g_puppets.end()) return false;
    Puppet& p = it->second;
    if (on && !p.nativeWindow)
    {
        p.nativeMaxDrift = 0.0f;   // window-scoped: reset on OPEN only
        p.rcSteps = 0; p.rcMaxStep = 0.0f; p.rcSumStep = 0.0f; p.rcLastTickAt = 0.0; p.rcReqMax = 0.0f; p.rcVisMaxStep = 0.0f; p.rcVisMaxRate = 0.0f; p.rcVisMaxRateDowned = 0.0f; p.rcVisMaxRateAny = 0.0f; p.rcVisLastX = p.rcVisLastY = p.rcVisLastZ = 0.0f; p.rcVisPrimed = false; p.rcLayerGapMax = 0.0f; p.rcLayerGapLast = 0.0f; p.rcDriftSum = 0.0; p.rcDriftN = 0; p.rcSatSteps = 0; p.rcBoostedSteps = 0;   // review 4 item 9: authDesired is NOT reset here - it is protocol state
        // F062/F353: ceasing to push does not stop a driven character. Zero the last vector so the
        // AI starts from a standing body rather than one still walking our stale push.
        if (PlausibleObj(p.ch) && PlausibleObj(p.ch->movement)) SafeStopMovement(p.ch->movement);
        p.driving = false;
    }
    p.nativeWindow = on;
    return true;
}

// PROBE-START: P019 - the last authoritative prone write per uid (spawn.cpp's correction), for the vis_peak line
struct ProneWriteNote { unsigned int uid; double at; };
static ProneWriteNote g_proneWrites[64];
void ProneWritesForget() { for (int i = 0; i < 64; ++i) { g_proneWrites[i].uid = 0; g_proneWrites[i].at = 0.0; } }   // world teardown: uids are per world
void NoteProneWrite(unsigned int uid)
{
    const double now = NowSeconds(); int free_ = -1; double oldest = 1e300; int oldestI = 0;
    for (int i = 0; i < 64; ++i) { if (g_proneWrites[i].uid == uid) { g_proneWrites[i].at = now; return; } if (g_proneWrites[i].uid == 0 && free_ < 0) free_ = i; if (g_proneWrites[i].at < oldest) { oldest = g_proneWrites[i].at; oldestI = i; } }
    const int k = free_ >= 0 ? free_ : oldestI; g_proneWrites[k].uid = uid; g_proneWrites[k].at = now;
}
double ProneWriteAgoSeconds(unsigned int uid, double now)
{
    for (int i = 0; i < 64; ++i) if (g_proneWrites[i].uid == uid) return now - g_proneWrites[i].at;
    return -1.0;
}
// PROBE-END: P019
bool IsDownedCharacter(::Character* c) { return DownedImpl(c); }
   // review 4 item 12: the one predicate, exported for combat.cpp

void SetReconcile(bool on)
{
    g_reconcileOn = on;
    if (!on)
    {
        // F311: never print a step count beside a disarmed switch.
        g_rcSteps = 0; g_rcSumStep = 0.0; g_rcMaxStep = 0.0f; g_rcSaturated = 0; g_rcReqMax = 0.0f;
        g_rcVisMaxStep = 0.0f; g_rcVisMaxRate = 0.0f; g_rcLayerGapFrames = 0; g_rcLayerGapMax = 0.0f; g_visPeakLines = 0;
        g_rcSkippedDeadband = g_rcSkippedProne = g_rcSkippedRagdoll = g_rcSkippedNoMovement = 0;
        g_rcSkippedBadSetter = g_rcSkippedOff = g_rcSkippedNonFiniteVisible = g_rcSkippedNonFiniteField = g_rcSkippedBadTarget = g_rcSkippedZeroStep = g_rcSkippedVtImplausible = 0;
        g_rcSkippedRetired = g_rcSkippedNoChar = g_rcSkippedNoAnimEarly = g_rcSlotRefused = 0; g_rcSetterFaulted = 0;
        g_rcSlotVerified = -1;   // a refused verdict is not permanent for the session
        for (std::map<unsigned int, Puppet>::iterator it = g_puppets.begin(); it != g_puppets.end(); ++it)
        {
            Puppet& p = it->second;
            p.rcSteps = 0; p.rcMaxStep = 0.0f; p.rcSumStep = 0.0f; p.rcReqMax = 0.0f; p.rcVisMaxStep = 0.0f; p.rcVisMaxRate = 0.0f; p.rcVisMaxRateDowned = 0.0f; p.rcVisMaxRateAny = 0.0f;
            p.rcLayerGapMax = 0.0f; p.rcLayerGapLast = 0.0f; p.rcDriftSum = 0.0; p.rcDriftN = 0; p.rcSatSteps = 0; p.rcBoostedSteps = 0;   // review 4 item 9: authDesired stays
        }
    }
    DebugLog(std::string("[H016] reconcile ") + (on ? "ON" : "OFF") + " - tau=" + F3(kReconcileTau)
             + " maxRatePerSec=" + F3(kReconcileMaxRate) + " maxStepAbs=" + F3(kReconcileMaxStepAbs) + " deadband=" + F3(kReconcileDeadband));
}

void PuppetReconcileStats(unsigned int uid, ReconcileStats* out)
{
    // -1 means NOT MEASURED (no puppet row). 0 would read as "measured, and it was zero" - the
    // unfalsifiable-oracle shape F412 retracted.
    out->steps = -1; out->satSteps = 0; out->boostedSteps = 0; out->maxStep = -1.0f; out->sumStep = -1.0f; out->reqMax = -1.0f;
    out->visMaxStep = -1.0f; out->visMaxRate = -1.0f; out->visMaxRateDowned = -1.0f; out->visMaxRateAny = -1.0f; out->layerGapMax = -1.0f; out->driftMean = -1.0f;
    std::map<unsigned int, Puppet>::iterator it = g_puppets.find(uid);
    if (it == g_puppets.end()) return;
    const Puppet& p = it->second;
    out->steps = p.rcSteps; out->maxStep = p.rcMaxStep; out->sumStep = p.rcSumStep; out->reqMax = p.rcReqMax;
    out->visMaxStep = p.rcVisMaxStep; out->visMaxRate = p.rcVisMaxRate; out->visMaxRateDowned = p.rcVisMaxRateDowned; out->visMaxRateAny = p.rcVisMaxRateAny; out->layerGapMax = p.rcLayerGapMax;
    out->driftMean = (p.rcDriftN > 0) ? (float)(p.rcDriftSum / (double)p.rcDriftN) : -1.0f;
    out->satSteps = p.rcSatSteps; out->boostedSteps = p.rcBoostedSteps;   // H028
}

// E25 (review-p5p HIGH-1) - see replicate.h. 1 = this character is in the peer player's own faction, 0 = it is not,
// -1 = the read faulted (which is counted as "not", the safe side: a peer we cannot see does not veto anything).
// Only POINTERS are compared, so nothing here needs the Faction type to be complete.
int PeerFactionPod(::Character* c)
{
    __try
    {
        ::Faction* f = c->getOwnerFactionDirect();
        if (f == 0) return 0;
        return IsPeerFaction(f) ? 1 : 0;   /* stand1: ANY player's stand-in (coop-p<n>) */
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
bool PeerPlayerPosition(float* x, float* y, float* z, double* ageSec)
{
    const double now = NowSeconds();
    const Puppet* best = 0;
    for (std::map<unsigned int, Puppet>::const_iterator it = g_puppets.begin(); it != g_puppets.end(); ++it)
    {
        const Puppet& p = it->second;
        if (!PlausibleObj(p.ch) || IsRetiredObject(p.ch)) continue;
        if (PeerFactionPod(p.ch) != 1) continue;
        // The FRESHEST sample wins. The rows all belong to one squad walking together, so this is not a choice
        // between different places - it is a choice between two readings of the same place, and the newer one is
        // the one whose age the caller is about to judge.
        if (best == 0 || p.lastMoveAt > best->lastMoveAt) best = &p;
    }
    if (best == 0) return false;
    if (x != 0) *x = best->tx;
    if (y != 0) *y = best->ty;
    if (z != 0) *z = best->tz;
    if (ageSec != 0) *ageSec = now - best->lastMoveAt;
    return true;
}

/* P25 interior keep (owner 296 a / 315 a; scratchpad p25-interior-design.md option a). Every frame the engine's interior loader
   (1.0.65 0x7F6690 / 1.0.68 0x7F7230) walks PlayerInterface+0x2B0 - this game's player-faction characters in active squads - and
   for each one standing in a building with an inside resets that inside's 10-second keep-alive and loads it when missing. The other
   player's characters are COPIES here (a stand-in faction), so they are not in that list and a building the other player stands in
   keeps no inside on this game. This walks the copies of the other players' player-faction characters (the g_puppets rows
   PeerFactionPod answers 1 for - PeerPlayerPosition's test) and makes, for each, the engine's own tests and its own call:
     Character vt+0x1D8 ('where am I', a hand*) -> hand type +8 == 0 (a building) -> HandGetBuilding -> Building vt+0x330 == 0
     -> BuildingInterior* at Building+0x1F0 != 0 -> BuildingInteriorKeep (1.0.65 0x561AB0 / 1.0.68 0x562540): timer = 10, and
     the inside is switched on when +0x10 is null.
   NOTHING ELSE: the copies are never added to the engine's list, the floor cut-away setter (0x92B0F0) is not called, nothing reads
   or moves the camera - nothing on this game's screen changes. No engine memory is written by us. Stopping = not calling: the
   building's own tick counts the timer down by game time and frees the inside about 10 game-seconds later. MAIN THREAD (the
   GameWorld_gpuFrame detour, after the AI-worker wait), once per frame, one call per building per frame. */
static unsigned long long kIkKeepRva = 0; static AddrReg kIkKeepRva_reg("BuildingInteriorKeep", &kIkKeepRva);   /* P25: Steam_1.0.65 0x561AB0 (1.0.68 0x562540): void (BuildingInterior*) */
typedef const void* (*IkWhereFn)(void* self);           /* Character vt+0x1D8: the hand of what the character is in */
typedef unsigned char (*IkBldTestFn)(void* self);       /* Building vt+0x330: the engine skips the building when it answers non-zero */
typedef void (*IkKeepFn)(void* interior);
const size_t kIkWhereSlot = 0x1D8, kIkBldTestSlot = 0x330, kIkHandType = 0x8, kIkBldInterior = 0x1F0, kIkInteriorLoaded = 0x10;
enum { kIkInBld = 0, kIkNoHand, kIkNotInBld, kIkNoBld, kIkVt330, kIkNoInterior, kIkWhereFault };
struct IkKept { unsigned int uid; long long frame; double since; int rec; };   /* rec: P25 fold 2 - the copy's own +0x69C byte (-1 unread) */
static std::map<void*, IkKept> g_ikKept;   /* building -> the copy that keeps it; KEYS ONLY - never dereferenced across frames */
static long long g_ikFrame = 0, g_ikCopies = 0, g_ikInBld = 0, g_ikCalls = 0, g_ikLoads = 0, g_ikShared = 0;
static long long g_ikNotInBld = 0, g_ikNoBld = 0, g_ikVt330 = 0, g_ikNoInterior = 0, g_ikWhereFaults = 0, g_ikKeepFaults = 0;
static long long g_ikStarted = 0, g_ikReleased = 0, g_ikLogDropped = 0, g_ikLogN = 0;
static double g_ikLogAt = -1.0;
static int g_ikOff = 0;   /* 1 = the keep call faulted once: turned off for this process (as tickWait) */
/* P25 fold 2 (T776 FAIL). THE OWNER STATES WHERE ITS CHARACTER IS. The copy's own engine record (vt+0x1D8) is a lease on the
   copy's game (setter vt+0x1C8 -> 0x792310 sets +0x69C = 4; counted down by 0x5C7BF0) and went stale for copies (T776: B's copy
   "in the bar" 260+ s after it walked out). The keep now follows the owner's MSG_INSIDE word (insidewire.h); the copy's record
   is only a diagnostic (copyRecDisagree: frames where it and the owner's word differ). ownerSaid counts copy-frames by the
   word's reading: in / out / stale (never heard, or older than 15 s) / keyMiss (inside, but its game built no key). */
static long long g_ikSaidIn = 0, g_ikSaidOut = 0, g_ikSaidStale = 0, g_ikSaidKeyMiss = 0, g_ikCopyRecDisagree = 0;
static long long g_ikSaySent = 0, g_ikSaySendFailed = 0, g_ikSayKeyFail = 0, g_ikSayFaults = 0, g_ikSaidRecv = 0, g_ikSaidRefused = 0;
struct IkSaid { int inside; char key[p25inside::kInsideKeyMax + 1]; double at; };
static std::map<unsigned int, IkSaid> g_ikSaid;   /* owner side: own uid -> the last word sent (MAIN THREAD only) */
const int kIkRecShow = 4;
static unsigned int g_ikRecShowUid[kIkRecShow] = { 0 }; static int g_ikRecShowRec[kIkRecShow] = { 0 }; static int g_ikRecShowN = 0;   /* the report's POD copy */

/* P25 fold 2: the first half of the engine's steps (the old where-pod, split) -the character's own 'where am I' record up to the building.
   kIkInBld fills *bldOut. On the OWNER's game for its own character (the word it sends); on a copy only as a diagnostic. */
static int IkHandBuilding(void* ch, void** bldOut)
{
    __try
    {
        void** vt = *(void***)ch;
        if (vt == 0) return kIkNoHand;
        const void* h = ((IkWhereFn)vt[kIkWhereSlot / 8])(ch);
        if (h == 0) return kIkNoHand;
        if (*(const int*)((const char*)h + kIkHandType) != 0) return kIkNotInBld;
        void* bld = (void*)((const ::hand*)h)->asBuilding();   /* row HandGetBuilding (= the engine's 0x9F8050 lookup) */
        if (bld == 0) return kIkNoBld;
        *bldOut = bld;
        return kIkInBld;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return kIkWhereFault; }
}
/* P25 fold 2: the second half - the engine's own skip test (vt+0x330) and the inside at +0x1F0. kIkInBld fills *inOut. */
static int IkBuildingInterior(void* bld, void** inOut)
{
    __try
    {
        void** bvt = *(void***)bld;
        if (bvt == 0) return kIkNoBld;
        if (((IkBldTestFn)bvt[kIkBldTestSlot / 8])(bld) != 0) return kIkVt330;
        void* in = *(void**)((char*)bld + kIkBldInterior);
        if (in == 0) return kIkNoInterior;
        *inOut = in;
        return kIkInBld;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return kIkWhereFault; }
}
/* P25 fold 2 diagnostic: the copy's own lease byte (+0x69C, set to 4 by 0x792310), -1 = the read faulted */
static int IkRec69cPod(void* ch)
{
    __try { return (int)*(const unsigned char*)((const char*)ch + 0x69C); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* P25 fold 2: is this character in THIS game's own player faction (the characters whose word the owner sends)? -1 faulted */
static int IkOwnPlayerFactionPod(::Character* c)
{
    __try
    {
        ::Faction* f = c->getOwnerFactionDirect();
        if (f == 0) return 0;
        return IsPlayerFaction(f) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* 1 called (*wasLoaded = the inside was already on), 0 faulted. */
static int IkKeepPod(void* in, int* wasLoaded)
{
    __try
    {
        *wasLoaded = (*(void**)((char*)in + kIkInteriorLoaded) != 0) ? 1 : 0;
        ((IkKeepFn)AddrAbs(kIkKeepRva))(in);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* at most 40 start/stop lines a minute; the rest are counted (logDropped). */
static void IkLog(const std::string& s, double now)
{
    if (g_ikLogAt < 0.0 || now - g_ikLogAt >= 60.0) { g_ikLogAt = now; g_ikLogN = 0; }
    if (g_ikLogN >= 40) { ++g_ikLogDropped; return; }
    ++g_ikLogN;
    DebugLog(s);
}
static std::string IkPtr(const void* p)
{
    std::stringstream ss; ss.imbue(std::locale::classic());
    ss << p;
    return ss.str();
}

/* P25 fold 2, OWNER SIDE (ReplicateTick, MAIN THREAD): for each own player-faction character announced to the other games, the
   engine's own 'where am I' record (on the owner's game it is the engine's player list that keeps it true) turned into the
   building's P7n key (ObjectPositionKey countIt 3: a miss books nothing in the item counters - counted here as keyFail, sent
   as "inside, no key"). Sent on change and every 5 s (p25inside::InsideSendDue). */
static void IkOwnerSay(unsigned int uid, ::Character* c, double now)
{
    if (!PlausibleObj(c) || IkOwnPlayerFactionPod(c) != 1) return;
    void* bld = 0;
    const int w = IkHandBuilding(c, &bld);
    if (w == kIkWhereFault) { ++g_ikSayFaults; return; }
    int inside = 0;
    char key[p25inside::kInsideKeyMax + 1]; key[0] = 0;
    if (w == kIkInBld)
    {
        inside = 1;
        if (ObjectPositionKey(bld, key, (int)sizeof key, "", 3, 1) == 0) { key[0] = 0; ++g_ikSayKeyFail; }
    }
    std::map<unsigned int, IkSaid>::iterator f = g_ikSaid.find(uid);
    const bool ever = (f != g_ikSaid.end());
    if (!p25inside::InsideSendDue(ever, ever ? f->second.inside : 0, ever ? f->second.key : "", inside, key, ever ? f->second.at : 0.0, now)) return;
    const bool changed = !ever || f->second.inside != inside || std::strcmp(f->second.key, key) != 0;
    if (!net::SendInside(uid, inside, key)) { ++g_ikSaySendFailed; return; }
    ++g_ikSaySent;
    IkSaid& r = g_ikSaid[uid];
    r.inside = inside; std::memcpy(r.key, key, std::strlen(key) + 1); r.at = now;
    if (changed)
        IkLog("[P25] interior told the other games: own uid=" + N2((long long)uid)
              + (inside ? (key[0] != 0 ? " is INSIDE building key=" + std::string(key) : std::string(" is inside a building whose key this game could not build (keyFail)"))
                        : std::string(" is OUTSIDE")), now);
}

/* P25 review fold 2-1 (MED 1/2): the building search behind ObjectByPositionKeyHint walks every loaded object, so the keep runs
   at most every kIkPassEvery s (the engine's own inside timer is 10 game-s - a keep every half second holds it as well as one
   per frame), and a key that found no building is not searched again for kIkMissBackoff s unless the owner's key changes. */
const double kIkPassEvery = 0.5, kIkMissBackoff = 5.0;
struct IkMiss { char key[p25inside::kInsideKeyMax + 1]; double at; };
static std::map<unsigned int, IkMiss> g_ikMiss;
static double g_ikLastPass = -1.0;
long long g_ikMissBackoffSkips = 0;
void InteriorKeepTick()
{
    if (g_ikOff || kIkKeepRva == 0 || !AddrOk()) return;
    const double now = NowSeconds();
    if (g_ikLastPass >= 0.0 && now >= g_ikLastPass && now - g_ikLastPass < kIkPassEvery) return;
    g_ikLastPass = now;
    ++g_ikFrame;
    for (std::map<unsigned int, Puppet>::iterator it = g_puppets.begin(); it != g_puppets.end(); ++it)   /* P25 fold 2: writes insideHint */
    {
        ::Character* c = it->second.ch;
        /* review fold 1: the same liveness question every other call through p.ch asks (RunBoostWritable) - the table must still hold this uid for this object */
        if (!PlausibleObj(c) || IsRetiredObject(c) || !coopuid::DriveDecision(FindSpawnedUid(c), it->first) || PeerFactionPod(c) != 1) continue;
        ++g_ikCopies;
        /* P25 fold 2: THE OWNER'S WORD decides (insidewire.h InsideVerdict): no word, "outside", no key, or nothing heard for more
           than 15 s -> not in a building (the keep is released below); else the building its key names here. */
        Puppet& pw = it->second;
        const int said = p25inside::InsideVerdict(pw.insideSaid, std::strlen(pw.insideKey), pw.insideAt, now);
        if (said == p25inside::kSaidIn) ++g_ikSaidIn;
        else if (said == p25inside::kSaidOut) ++g_ikSaidOut;
        else if (said == p25inside::kSaidKeyMiss) ++g_ikSaidKeyMiss;
        else ++g_ikSaidStale;
        void* bld = 0; void* in = 0;
        if (said == p25inside::kSaidIn)
        {
            std::map<unsigned int, IkMiss>::iterator ms = g_ikMiss.find(it->first);
            const bool backoff = ms != g_ikMiss.end() && std::strcmp(ms->second.key, pw.insideKey) == 0 && now >= ms->second.at && now - ms->second.at < kIkMissBackoff;
            if (backoff) ++g_ikMissBackoffSkips;
            else
            {
                bld = ObjectByPositionKeyHint(pw.insideKey, pw.insideHint);
                if (bld == 0) { IkMiss m; std::memcpy(m.key, pw.insideKey, std::strlen(pw.insideKey) + 1); m.at = now; g_ikMiss[it->first] = m; }
                else if (ms != g_ikMiss.end()) g_ikMiss.erase(ms);
            }
        }
        /* the copy's own record - a DIAGNOSTIC only, never the decision */
        void* cb = 0;
        const int cw = IkHandBuilding(c, &cb);
        if (cw == kIkWhereFault)
        {
            if (++g_ikWhereFaults == 1)
                ErrorLog("[P25] interior keep: the copy's 'where am I' read faulted (copy uid=" + N2((long long)it->first) + ") - diagnostic only; the count is whereFaults");
        }
        else
        {
            const bool copyIn = (cw == kIkInBld), ownerIn = (said == p25inside::kSaidIn);
            if (copyIn != ownerIn || (copyIn && bld != 0 && cb != bld)) ++g_ikCopyRecDisagree;
        }
        if (said != p25inside::kSaidIn) { ++g_ikNotInBld; continue; }
        if (bld == 0) { ++g_ikNoBld; continue; }   /* the key names no building loaded here (or two) */
        pw.insideHint = bld;
        const int w = IkBuildingInterior(bld, &in);
        if (w != kIkInBld)
        {
            if (w == kIkWhereFault)
            {
                if (++g_ikWhereFaults == 1)
                    ErrorLog("[P25] interior keep: the building's inside read faulted (copy uid=" + N2((long long)it->first) + ") - skipped; the count is whereFaults");
            }
            else if (w == kIkNoBld) ++g_ikNoBld;
            else if (w == kIkVt330) ++g_ikVt330;
            else ++g_ikNoInterior;
            continue;
        }
        ++g_ikInBld;
        const int rec = IkRec69cPod(c);   /* P25 fold 2 diagnostic: the copy's own lease byte */
        std::map<void*, IkKept>::iterator k = g_ikKept.find(bld);
        if (k != g_ikKept.end() && k->second.frame == g_ikFrame) { ++g_ikShared; continue; }   /* another copy kept it this frame */
        int wasLoaded = 0;
        if (!IkKeepPod(in, &wasLoaded))
        {
            ++g_ikKeepFaults; g_ikOff = 1;
            g_ikKept.clear();   /* review fold 1: nothing is kept any more - the engine's own timer decides from here */
            ErrorLog("[P25] interior keep: BuildingInteriorKeep faulted (building=" + IkPtr(bld) + " copy uid=" + N2((long long)it->first)
                     + ") - turned off for this process");
            return;
        }
        ++g_ikCalls;
        if (!wasLoaded) ++g_ikLoads;
        if (k == g_ikKept.end())
        {
            IkKept e; e.uid = it->first; e.frame = g_ikFrame; e.since = now; e.rec = rec;
            g_ikKept[bld] = e;
            ++g_ikStarted;
            IkLog("[P25] interior kept for the other player: building=" + IkPtr(bld) + " copy uid=" + N2((long long)it->first)
                  + " key=" + std::string(pw.insideKey) + " copy +0x69C=" + N2((long long)rec)
                  + (wasLoaded ? " (inside already loaded)" : " (inside was not loaded - the engine loads it now)"), now);
        }
        else { k->second.frame = g_ikFrame; k->second.uid = it->first; k->second.rec = rec; }
    }
    for (std::map<void*, IkKept>::iterator k = g_ikKept.begin(); k != g_ikKept.end(); )
    {
        if (k->second.frame == g_ikFrame) { ++k; continue; }
        ++g_ikReleased;
        IkLog("[P25] interior released for the other player: building=" + IkPtr(k->first) + " copy uid=" + N2((long long)k->second.uid)
              + " after " + F3((float)(now - k->second.since)) + " s, copy +0x69C=" + N2((long long)k->second.rec) + " (no longer kept by the mod; the engine's own timer frees the inside about 10 game-s later unless its own rules keep it)", now);
        g_ikKept.erase(k++);
    }
    /* P25 fold 2: the kept copies' own +0x69C byte, copied for the report (plain values - the report walks no map) */
    int nShow = 0;
    for (std::map<void*, IkKept>::const_iterator k = g_ikKept.begin(); k != g_ikKept.end() && nShow < kIkRecShow; ++k, ++nShow)
    { g_ikRecShowUid[nShow] = k->second.uid; g_ikRecShowRec[nShow] = k->second.rec; }
    g_ikRecShowN = nShow;
}

/* P25 fold 2: declared in replicate.h - the owner's word lands on the puppet row; a changed word is logged. */
void ApplyRemoteInside(unsigned int uid, int inside, const std::string& key)
{
    std::map<unsigned int, Puppet>::iterator it = g_puppets.find(uid);
    if (it == g_puppets.end()) { ++g_ikSaidRefused; return; }   /* not our puppet: refused, counted */
    Puppet& p = it->second;
    const size_t n = key.size() < p25inside::kInsideKeyMax ? key.size() : p25inside::kInsideKeyMax;
    const int said = inside ? 1 : 0;
    const bool keyChanged = std::strlen(p.insideKey) != n || (n != 0 && std::memcmp(p.insideKey, key.data(), n) != 0);
    const bool changed = (p.insideSaid != said) || keyChanged;
    if (keyChanged) { if (n != 0) std::memcpy(p.insideKey, key.data(), n); p.insideKey[n] = 0; p.insideHint = 0; }
    p.insideSaid = said; p.insideAt = NowSeconds();
    ++g_ikSaidRecv;
    if (changed)
        IkLog("[P25] interior owner says: copy uid=" + N2((long long)uid)
              + (said ? (n != 0 ? " is INSIDE building key=" + std::string(p.insideKey) : std::string(" is inside a building whose key its game could not build (keyMiss)"))
                      : std::string(" is OUTSIDE")), p.insideAt);
}
static std::string IkRecShowText()
{
    if (g_ikRecShowN <= 0) return "none";
    std::string t;
    for (int i = 0; i < g_ikRecShowN && i < kIkRecShow; ++i)
    { if (i != 0) t += ","; t += N2((long long)g_ikRecShowUid[i]) + ":" + N2((long long)g_ikRecShowRec[i]); }
    return t;
}

std::string InteriorKeepToken()
{
    return "p25Interior[copies,inBld,calls,loadsStarted,shared,notInBld,noBld,vt330,noInterior,whereFaults,keepFaults,keptNow,started,released,logDropped]="
           + N2(g_ikCopies) + "," + N2(g_ikInBld) + "," + N2(g_ikCalls) + "," + N2(g_ikLoads) + "," + N2(g_ikShared) + "," + N2(g_ikNotInBld)
           + "," + N2(g_ikNoBld) + "," + N2(g_ikVt330) + "," + N2(g_ikNoInterior) + "," + N2(g_ikWhereFaults) + "," + N2(g_ikKeepFaults)
           + "," + N2((long long)g_ikKept.size()) + "," + N2(g_ikStarted) + "," + N2(g_ikReleased) + "," + N2(g_ikLogDropped)
           + " p25InteriorState=" + (kIkKeepRva == 0 ? "noAddress" : (g_ikOff ? "offFault" : "on"))
           /* P25 fold 2: copy-frames by the owner's word; frames the copy's own record disagreed; the owner side's sends; uid:+0x69C of kept copies */
           + " ownerSaid[in,out,stale,keyMiss]=" + N2(g_ikSaidIn) + "," + N2(g_ikSaidOut) + "," + N2(g_ikSaidStale) + "," + N2(g_ikSaidKeyMiss) + " missBackoffSkips=" + N2(g_ikMissBackoffSkips)
           + " copyRecDisagree=" + N2(g_ikCopyRecDisagree)
           + " p25Inside[sent,sendFailed,keyFail,sayFaults,recv,refused]=" + N2(g_ikSaySent) + "," + N2(g_ikSaySendFailed) + "," + N2(g_ikSayKeyFail)
           + "," + N2(g_ikSayFaults) + "," + N2(g_ikSaidRecv) + "," + N2(g_ikSaidRefused)
           + " p25KeptRec69C=" + IkRecShowText();
}

float PuppetWindowDriftNow(unsigned int uid)
{
    std::map<unsigned int, Puppet>::iterator it = g_puppets.find(uid);
    if (it == g_puppets.end()) return -1.0f;
    const Puppet& p = it->second;
    if (!PlausibleObj(p.ch) || IsRetiredObject(p.ch)) return -1.0f;
    Ogre::Vector3 cur;
    if (!SafeReadPosition(p.ch, &cur)) return -1.0f;
    const float dx = p.tx - cur.x, dz = p.tz - cur.z;
    const float d = sqrtf(dx * dx + dz * dz);
    return Finite(d) ? d : -1.0f;
}

// The owner's last streamed position for a copy driven here as a puppet - where the owner's character stands (or lies) now.
bool PuppetAuthorityPos(unsigned int uid, float* x, float* y, float* z)
{
    std::map<unsigned int, Puppet>::const_iterator it = g_puppets.find(uid);
    if (it == g_puppets.end()) return false;
    const Puppet& p = it->second;
    if (!Finite(p.tx) || !Finite(p.ty) || !Finite(p.tz)) return false;
    *x = p.tx; *y = p.ty; *z = p.tz;
    return true;
}

float PuppetWindowMaxDrift(unsigned int uid)
{


    std::map<unsigned int, Puppet>::iterator it = g_puppets.find(uid);
    return (it == g_puppets.end()) ? -1.0f : it->second.nativeMaxDrift;
}

int SnapPuppetToAuthority(unsigned int uid, float* driftBefore, float* driftSameCall)
{
    float* driftAfter = driftSameCall;
    if (driftBefore) *driftBefore = -1.0f;
    if (driftAfter)  *driftAfter  = -1.0f;
    std::map<unsigned int, Puppet>::iterator it = g_puppets.find(uid);
    if (it == g_puppets.end()) return 1;
    Puppet& p = it->second;
    if (!PlausibleObj(p.ch) || IsRetiredObject(p.ch)) return 2;   // F337: a recycled address is a stranger
    // mirror1 fold (review-mirror1 #8): DrivePuppet's question too - the table must hold this object LIVE UNDER THIS UID
    // ("not retired" is not "registered": T487's puppet had no row). Asked before any read through p.ch beyond the vtable
    // probe, and answered 2, the stranger code the caller already handles.
    if (!coopuid::DriveDecision(FindSpawnedUid(p.ch), uid)) return 2;
    if (!PlausibleObj(*(void**)((char*)p.ch + kAnimationClassOff))) return 3;
    CharMovement* mv = p.ch->movement;
    if (!PlausibleObj(mv)) return 4;
    // F324: a ragdolled or prone body skips the movement write-back, so a placement CANNOT take.
    if (SafeIsRagdoll(p.ch)) return 5;   // SEH-wrapped: the raw call double-dereferences (F316)
    // T-178 crawl1: a crawler (prone 2; not a ragdoll - 5 above) moves by the same CharMovement update as a walker
    if (!coopgetup::MoveDrivesProne(*(int*)((char*)p.ch + kProneStateOff), false)) return 6;

    Ogre::Vector3 cur = p.ch->worldPosition();
    float dx = p.tx - cur.x, dz = p.tz - cur.z;
    if (driftBefore) *driftBefore = sqrtf(dx * dx + dz * dz);
    P071NotePlacement(uid, p.ch, 3, p.tx, p.ty, p.tz);   // PROBE P071
    mv->teleportTo(Ogre::Vector3(p.tx, p.ty, p.tz), -1);
    PathForgetDest(p);   // D1 item 3 (snap 3)
    Ogre::Vector3 now = p.ch->worldPosition();
    dx = p.tx - now.x; dz = p.tz - now.z;
    if (driftAfter) *driftAfter = sqrtf(dx * dx + dz * dz);
    p.driving = false;
    p.nativeSnapScorePending = true;
    p.nativeSnapTick = g_tick;
    return 0;
}

// walk1: the proportional catch-up on / off (off = the H020 hysteresis catch-up), for an A/B.
void SetCatchup2(bool on) { g_catchup2 = on; }

// T-189 runboost: the lever. Off puts every raised ceiling back at once (the settle would next frame; the lever does not wait).
void SetRunBoost(bool on)
{
    g_runBoost = on;
    if (on) return;
    for (std::map<unsigned int, Puppet>::iterator it = g_puppets.begin(); it != g_puppets.end(); ++it)
        if (it->second.rbActive || it->second.rbCeiling) RunBoostEnd(it->second, RunBoostWritable(it->first, it->second));
}

// T-191: "uid:n,uid:n" for the far-snapped uids of this world (none = "none"). runboost2 (T-195 R6): the top 16 by count
// (ties: the lower uid), then ",other:n" = the snaps of every uid not named (the rest of the map and the overflow).
static std::string SnapFarByUidString()
{
    if (g_snapFarByUid.empty() && g_snapFarByUidOther == 0) return "none";
    std::vector<std::pair<long long, unsigned int> > v;   // (-count, uid): an ascending sort puts the most snapped first
    for (std::map<unsigned int, long long>::const_iterator it = g_snapFarByUid.begin(); it != g_snapFarByUid.end(); ++it)
        v.push_back(std::make_pair(-it->second, it->first));
    const size_t shown = v.size() < 16 ? v.size() : 16;
    std::partial_sort(v.begin(), v.begin() + shown, v.end());
    std::string s;
    for (size_t i = 0; i < shown; ++i)
    {
        if (!s.empty()) s += ",";
        s += N2((long long)v[i].second) + ":" + N2(-v[i].first);
    }
    long long other = g_snapFarByUidOther;
    for (size_t i = shown; i < v.size(); ++i) other -= v[i].first;
    if (other > 0) { if (!s.empty()) s += ","; s += "other:" + N2(other); }
    return s;
}

void SetDriveSpeed(float v)
{
    // F311 - switching the lever OFF puts the engine's own value back on every puppet we wrote to,
    // and the counters are reset so the report can never show a write count beside an arm that is
    // not writing. Without this, `driveSpeed=0.0 speedWrites=934158` was a reachable and actively
    // misleading report line.
    long long restored = 0, couldNot = 0;
    if (v == 0.0f && g_driveSpeed != 0.0f)
    {
        for (std::map<unsigned int, Puppet>::iterator it = g_puppets.begin();
             it != g_puppets.end(); ++it)
        {
            Puppet& p = it->second;
            if (!p.speedSaved) continue;
            if (PlausibleObj(p.ch) && RestoreDesiredSpeed(p.ch->movement, p.speedOriginal))
            {
                p.speedSaved = false;
                ++restored;
            }
            else ++couldNot;
        }
        g_speedRestored += restored;
    }

    g_driveSpeed = v;
    if (v == 0.0f) { g_speedWrites = 0; g_speedRefused = 0; g_speedAuthWrites = 0; g_speedAuthFloored = 0; g_speedArriveSynced = 0; g_speedCatchupFrames = 0; g_speedGaitFrames = 0; g_speedCatchupEnters = 0; }

    DebugLog("[M2] driveSpeed = " + F2(v)
             + (v == 0.0f ? "  (OFF - desiredSpeed left exactly as the engine leaves it;"
                            " mvWant= readings are the engine's own)"
                          : (v < 0.0f ? "  (each puppet uses its OWN maxSpeed; a puppet whose"
                                        " maxSpeed is not > 0 is REFUSED, not written with zero)"
                                      : "  (forced for every puppet)"))
             + (restored || couldNot
                ? "  restored " + N2(restored) + " puppets to the engine's own desiredSpeed"
                  + (couldNot ? ", COULD NOT restore " + N2(couldNot) : "")
                : ""));
}

float GetDriveSpeed() { return g_driveSpeed; }

// P037 / F315. See the constants block for why this is a TEST before it is a fix, and why it is
// triggered by the puppet's own displacement rather than by the size of the gap.
void SetCatchup(bool on)
{
    g_catchupOn = on;
    DebugLog(std::string("[M2] catchup = ") + (on ? "ON" : "OFF")
             + (on ? "  (a puppet that is pushed for two full windows and goes nowhere - or is still stalled after"
                     " an unstick order - and a puppet more than 250 units from its owner are PLACED at"
                     " the target; a placement that does not move the puppet gives up after 3 tries, and"
                     " one lost or stalled again soon after 3 placements gives up too)"
                   : "  (measurement only - stalledWindows still counts; neither the stuck snap nor the far"
                     " snap corrects anything)"));
}

void SetOrderDrive(bool on)
{
    g_orderDrive = on;
    if (!on) { g_orderIssued = 0; g_orderIssueFailed = 0; g_orderHeldFrames = 0; g_orderDoneFrames = 0; g_orderFallbackPush = 0; }
    DebugLog(std::string("[H021] orderdrive ") + (on ? "ON" : "OFF") + " - move orders re-issued beyond " + F2(kOrderReissueDist)
             + " units or every " + N2(kOrderReissueTicks) + " ticks; done within " + F2(kOrderDoneDist));
}

// D1 - `pathdrive on|off`. Off releases every copy from path mode; the next frame pushes it (STEER_BY_DIRECTION cancels the
// path) and the H024 unstick order is back - today's behaviour exactly.
void SetPathDrive(bool on)
{
    g_pathDrive = on;
    long long released = 0;
    if (!on)
        for (std::map<unsigned int, Puppet>::iterator it = g_puppets.begin(); it != g_puppets.end(); ++it)
            if (it->second.pathMode)   // D1-b item 5: released copies are stopped, so none keeps walking an old path
            {
                Puppet& q = it->second;
                q.pathMode = false; PathForgetDest(q); q.pathWantIssue = false; q.pathGrant = false; ++released;
                if (PlausibleObj(q.ch) && PlausibleObj(q.ch->movement) && !SafeLeavePath(q.ch->movement)) ++g_pathLeaveFaulted;
            }
    DebugLog(std::string("[D1] pathdrive ") + (on ? "ON" : "OFF") + " - path beyond " + F2(kPathEnter) + " (or stalled), push within "
             + F2(kPathLeave) + " once the owner stopped " + F2((float)kPathOwnerStillSec) + " s; owner moving: destination max(owner, copy speed) x " + F2(kPathLeadSec) + " s + " + F2(cooppath::kLeadPad) + " ahead of it (" + F2(kPathLeadMin) + "-" + F2(kPathLeadMax) + "), re-planned within max(" + F2(kPathNearEndMin) + ", copy speed x " + F2(kPathNearEndSec) + " s) of its end / on a > 30 deg turn / aim > " + F2(kPathOffLineMax) + " off its line, a too-close aim pushed forward, exact aim once when the owner stops; owner still: re-issue when the aim moved " + F2(kPathReissueDist) + " (next issue once the last path landed, at least " + F2((float)cooppath::kMinIssueGapSec) + " s apart, else floor " + F2((float)kPathIssueFloorSec) + " s per copy; " + N2((long long)kPathBudgetPerFrame) + " per frame)"
             + (on ? std::string() : " - released " + N2(released) + " copies from path mode"));
}

void SetLead(bool on)
{
    g_leadOn = on;
    DebugLog(std::string("[M2] lead = ") + (on ? "ON" : "OFF")
             + (on ? "  (F350: the peer aims at the authority's last position carried forward by"
                     " its own measured velocity, and stops only when the AUTHORITY has stopped -"
                     " so a correctly-tracking puppet is no longer halted five times a second by a"
                     " deadband wider than the step between updates)"
                   : "  (control arm - no extrapolation, and arrival stops on distance alone,"
                     " exactly as the deployed build does)"));
}

void ReportReplication()
{
    std::stringstream ss;
    ss << "[M2] REPORT streamedLastTick=" << N2(g_streamedLastTick)
       << " explicitTarget=" << (g_ownedUid ? N2(g_ownedUid) : "none")
       << " moveSent=" << N2(g_sent)
       << " moveRecv=" << N2(g_moveRecv)
       << " proneSnaps=" << N2(g_proneSnaps)
       // F305 - both printed so the arm of the run is never in doubt. driveSpeed=0.0 means the
       // desiredSpeed readings below are the ENGINE's own untouched values; anything else means
       // we are writing them and mvWant= is our number, not evidence.
       << " driveSpeed=" << F3(g_driveSpeed)
       << " intentSent=" << N2(g_intentSent) << " intentGoalResent=" << N2(g_intentGoalResent) << " intentRecv=" << N2(g_intentRecv) << " intentRefused=" << N2(g_intentRefused)
       << " handoffInjected=" << N2(g_handoffInjected) << " handoffMatched=" << N2(g_handoffMatched) << " handoffControl=" << N2(g_handoffControl) << " handoffControlMatched=" << N2(g_handoffControlMatched)
       << " handoffInjectFailed=" << N2(g_handoffInjectFailed) << " handoffQuiesceTimedOut=" << N2(g_handoffQuiesceTimedOut)
       << " rcBoostedSteps=" << N2(g_rcBoostedSteps)
       << " driveSkippedRetired=" << N2(g_driveSkippedRetired) << " driveSkippedUnregistered=" << N2(g_driveSkippedUnregistered) << " adoptRefusedFull=" << N2(g_adoptRefusedFull) << " adoptRefusedOtherUid=" << N2(g_adoptRefusedOtherUid) << " puppetsDroppedReclaim=" << N2(g_puppetsDroppedReclaim) << " rcSetterFaulted=" << N2(g_rcSetterFaulted) << " speedArriveSynced=" << N2(g_speedArriveSynced)
       << " orderdrive=" << (g_orderDrive ? "on" : "off") << " orderIssued=" << N2(g_orderIssued) << " orderIssueFailed=" << N2(g_orderIssueFailed) << " orderHeldFrames=" << N2(g_orderHeldFrames) << " orderDoneFrames=" << N2(g_orderDoneFrames) << " orderFallbackPush=" << N2(g_orderFallbackPush) << " unstickOrders=" << N2(g_unstickOrders) << " unstickResolved=" << N2(g_unstickResolved) << " unstickTimedOut=" << N2(g_unstickTimedOut) << " unstickHeldFrames=" << N2(g_unstickHeldFrames)
       << " pathdrive=" << (g_pathDrive ? "on" : "off") << " pathIssued=" << N2(g_pathIssued) << " pathDeduped=" << N2(g_pathDeduped) << " pathIssueFailed=" << N2(g_pathIssueFailed) << " pathToPush=" << N2(g_pathToPush) << " pushToPath=" << N2(g_pushToPath) << " pathEnterByStall=" << N2(g_pathEnterByStall) << " pathIneligibleFrames=" << N2(g_pathIneligibleFrames) << " pathFrames=" << N2(g_pathFrames) << " pathFailed=" << N2(g_pathFailed) << " pathFailedReadFaulted=" << N2(g_pathFailedReadFaulted) << " pathLeaveFaulted=" << N2(g_pathLeaveFaulted) << " pathMoveLe2=" << N2(g_pathMoveLe2) << " pathMoveLe10=" << N2(g_pathMoveLe10) << " pathMoveLe60=" << N2(g_pathMoveLe60) << " pathMoveGt60=" << N2(g_pathMoveGt60) << " pathMoveMaxFrames=" << N2(g_pathMoveMaxFrames) << " pathAwaitAbandoned=" << N2(g_pathAwaitAbandoned) << " pathDeadFallback=" << N2(g_pathDeadFallback) << " pathNoProgressFallback=" << N2(g_pathNoProgressFallback) << " pathLeaveOwnerStopped=" << N2(g_pathLeaveOwnerStopped) << " pathReplanNearEnd=" << N2(g_pathReplanNearEnd) << " pathReplanTurn=" << N2(g_pathReplanTurn) << " pathReplanOffLine=" << N2(g_pathReplanOffLine) << " pathStopIssue=" << N2(g_pathStopIssue) << " pathArrivedWhileOwnerMoving=" << N2(g_pathArrivedWhileOwnerMoving) << " pathAheadStopped=" << N2(g_pathAheadStopped) << " pathFailedRetryAim=" << N2(g_pathFailedRetryAim) << " pathStopIssueBypassFloor=" << N2(g_pathStopIssueBypassFloor) << " ownSmoothReset=" << N2(g_ownSmoothReset) << " pathReplanTooClose=" << N2(g_pathReplanTooClose) << " pathReplanExtended=" << N2(g_pathReplanExtended) << " pathReplanOnLanded=" << N2(g_pathReplanOnLanded) << " pathLandedReadFaulted=" << N2(g_pathLandedReadFaulted) << " pathBudgetDeferred=" << N2(g_pathBudgetDeferred)
       << " faceWrites=" << N2(g_faceWrites) << " faceWriteFailed=" << N2(g_faceWriteFailed) << " faceSkippedNoVector=" << N2(g_faceSkippedNoVector)
       << " orderConsumedFrames=" << N2(g_orderConsumedFrames) << " orderUnconsumedFrames=" << N2(g_orderUnconsumedFrames) << " orderRetries=" << N2(g_orderRetries) << " orderAheadRetracts=" << N2(g_orderAheadRetracts) << " orderAheadHeld=" << N2(g_orderAheadHeld)
       << " orderRunFrames=" << N2(g_orderRunFrames) << " orderWalkFrames=" << N2(g_orderWalkFrames) << " orderSpeedWrites=" << N2(g_orderSpeedWrites) << " orderSpeedWriteFailed=" << N2(g_orderSpeedWriteFailed)
       << " speedCatchupFrames=" << N2(g_speedCatchupFrames) << " speedGaitFrames=" << N2(g_speedGaitFrames) << " speedCatchupEnters=" << N2(g_speedCatchupEnters)
       // walk1: the proportional catch-up and the walking-lag histogram (once a second per moving copy; walk / run)
       << " catchup2=" << N2(g_catchup2 ? 1 : 0) << " c2Frames=" << N2(g_c2Frames) << " c2AheadFrames=" << N2(g_c2AheadFrames)
       << " walkLag[n,mean,<1,1-5,5-10,10-20,20-40,40+,ahead]=" << N2(g_walkLagN[0]) << "," << F2(g_walkLagN[0] > 0 ? (float)(g_walkLagSum[0] / (double)g_walkLagN[0]) : 0.0f)
       << "," << N2(g_walkLagBucket[0][0]) << "," << N2(g_walkLagBucket[0][1]) << "," << N2(g_walkLagBucket[0][2]) << "," << N2(g_walkLagBucket[0][3])
       << "," << N2(g_walkLagBucket[0][4]) << "," << N2(g_walkLagBucket[0][5]) << "," << N2(g_walkLagBucket[0][6])
       << " runLag[same]=" << N2(g_walkLagN[1]) << "," << F2(g_walkLagN[1] > 0 ? (float)(g_walkLagSum[1] / (double)g_walkLagN[1]) : 0.0f)
       << "," << N2(g_walkLagBucket[1][0]) << "," << N2(g_walkLagBucket[1][1]) << "," << N2(g_walkLagBucket[1][2]) << "," << N2(g_walkLagBucket[1][3])
       << "," << N2(g_walkLagBucket[1][4]) << "," << N2(g_walkLagBucket[1][5]) << "," << N2(g_walkLagBucket[1][6])
       << " trueRunLag[same]=" << N2(g_walkLagN[2]) << "," << F2(g_walkLagN[2] > 0 ? (float)(g_walkLagSum[2] / (double)g_walkLagN[2]) : 0.0f)
       << "," << N2(g_walkLagBucket[2][0]) << "," << N2(g_walkLagBucket[2][1]) << "," << N2(g_walkLagBucket[2][2]) << "," << N2(g_walkLagBucket[2][3])
       << "," << N2(g_walkLagBucket[2][4]) << "," << N2(g_walkLagBucket[2][5]) << "," << N2(g_walkLagBucket[2][6])   // T-196
       // T-189 runboost (H051): the raise for copies trailing a running owner (runLag above is its outcome)
       << " runboost=" << N2(g_runBoost ? 1 : 0) << " runBoost[frames,enters,restored,restoreFailed,overwritten,restoreSkipped,easeFrames,retryOk]="
       << N2(g_rbFrames) << "," << N2(g_rbEnters) << "," << N2(g_rbRestored) << "," << N2(g_rbRestoreFailed)
       << "," << N2(g_rbOverwritten) << "," << N2(g_rbRestoreSkipped) << "," << N2(g_rbEaseFrames) << "," << N2(g_rbRetryOk)
       << " speedWrites=" << N2(g_speedWrites) << " speedAuthWrites=" << N2(g_speedAuthWrites) << " speedAuthFloored=" << N2(g_speedAuthFloored)
       << " speedRefused=" << N2(g_speedRefused)
       << " speedRestored=" << N2(g_speedRestored)
       << " speedRestoreSkipped=" << N2(g_speedRestoreSkipped)
       // F340 - both halves always printed. If both are 0 across a run in which puppets were
       // driven, non-finite motion did not occur; if only one moved, it names the source.
       << " moveNonFinite=" << N2(g_moveNonFinite)
       << " driveNonFinite=" << N2(g_driveNonFinite)
       // F341/F342 - `moveForRetiredUids` is the DIVERGENCE COUNT: distinct characters the
       // authority still has and this world no longer does. `moveForRetired` is the MESSAGE count
       // for the same population - it says how long the divergence persisted, not how wide it is,
       // and T092's 172-of-173 was a single character.
       << " moveForRetiredUids=" << N2((long long)g_divergedCount)
       // F343 - the ceiling is printed BESIDE the value, because a saturated 64 and a true 64 are
       // different facts and nothing else in the line distinguishes them.
       << " moveForRetiredUidsCap=" << N2((long long)kDivergedSlots)
       << " moveForRetiredMsgs=" << N2(g_moveForRetired)
       << " moveForRetiredMsgsBeyondStore=" << N2(g_divergedBeyondStore)
       // F343 - every one of these is a MESSAGE count and now says so. `moveForUnheardMsgs` is also
       // an OVER-count whenever `lostUidRingOverflow` is non-zero: an evicted uid stops being
       // recognised and its traffic lands here, in the bucket labelled ordinary.
       << " moveForUnheardMsgs=" << N2(g_moveForUnheard)
       << " lostUidRingOverflow=" << N2(LostUidOverflowCount())
       // P037 - `stalledWindows` is the MEASUREMENT and rises whether or not the corrective is on.
       << " catchup=" << (g_catchupOn ? "on" : "off")
       << " stalledWindows=" << N2(g_stalledWindows)
       << " catchupSnaps=" << N2(g_catchupSnaps)
       << " catchupEffective=" << N2(g_catchupEffective)
       << " catchupIneffective=" << N2(g_catchupIneffective)
       << " catchupGaveUp=" << N2(g_catchupGaveUp)
       << " catchupSkippedRagdoll=" << N2(g_catchupSkippedRagdoll)
       // Decision 60 - placements by cause. snapStuck is catchupSnaps under its cause name (both
       // kept: readouts grep catchupSnaps). stalledNotClosing: item 5's added stall cause.
       << " snapStuck=" << N2(g_catchupSnaps)
       << " snapFar=" << N2(g_snapFar) << " snapFarByUid=" << SnapFarByUidString()   // T-191
       << " snapFarSkippedRagdoll=" << N2(g_snapFarSkippedRagdoll)
       << " snapFarSkippedGaveUp=" << N2(g_snapFarSkippedGaveUp)
       << " stalledNotClosing=" << N2(g_stalledNotClosing)
       // review-r2 - scores per cause, strikes, and the new declines (each counted once per snap decision).
       << " snapFarEffective=" << N2(g_snapEffectiveBy[kSnapCauseFar])
       << " snapFarIneffective=" << N2(g_snapIneffectiveBy[kSnapCauseFar])
       << " snapFarGaveUp=" << N2(g_snapGaveUpBy[kSnapCauseFar])
       << " snapStuckEffective=" << N2(g_snapEffectiveBy[kSnapCauseStuck])
       << " snapStuckIneffective=" << N2(g_snapIneffectiveBy[kSnapCauseStuck])
       << " snapStuckGaveUp=" << N2(g_snapGaveUpBy[kSnapCauseStuck])
       << " snapStrikes=" << N2(g_snapStrikes)
       << " snapRateCapped=" << N2(g_snapRateCapped)
       << " snapStuckAfterUnstick=" << N2(g_snapStuckAfterUnstick)
       << " snapSkippedAttached=" << N2(g_snapSkippedAttached)
       << " proneSnapSkippedCarried=" << N2(g_proneSnapSkippedCarried)
       << " proneSnapSkippedRagdoll=" << N2(g_proneSnapSkippedRagdoll)   // R3 item 5
       << " snapFarSkippedUnloaded=" << N2(g_snapFarSkippedUnloaded)
       << " snapSkippedStale=" << N2(g_snapSkippedStale)
       << " snapFarCancelledUnstick=" << N2(g_snapFarCancelledUnstick)
       // F350 / P-20. `lead=off` means this build behaves exactly as the deployed one did, which
       // is the control arm. Authority-side counters are SAMPLES (one per owned character per send
       // tick); peer-side counters are FRAMES (one per puppet per frame). The names say which.
       << " lead=" << (g_leadOn ? "on" : "off")
       << " velMovingSamples=" << N2(g_velMoving)
       << " velStoppedSamples=" << N2(g_velStopped)
       << " velFirstSamples=" << N2(g_velFirstSample)
       // F353 - the three that are DEFECTS rather than ordinary readings, kept out of
       // `velStopped` so a non-finite sample or a teleport cannot hide inside "standing still".
       << " velProneSamples=" << N2(g_velProne)
       << " velNonFiniteSamples=" << N2(g_velNonFinite)
       << " velTeleportSamples=" << N2(g_velTeleport)
       << " velBadClockSamples=" << N2(g_velBadClock)
       << " velNoSampleSlot=" << N2(g_velNoSampleSlot)
       << " velRagdollSamples=" << N2(g_velRagdoll)
       << " velInSomethingSamples=" << N2(g_velInSomething)
       << " velCarriedSamples=" << N2(g_velCarried)
       << " velLayerMismatch=" << N2(g_velLayerMismatch)
       << " moveVelAbsurd=" << N2(g_moveVelAbsurd)
       << " leadAppliedFrames=" << N2(g_leadAppliedFrames)
       << " velExpiredFrames=" << N2(g_velExpiredFrames)
       << " leadDistClampedFrames=" << N2(g_leadDistClampedFrames)
       << " leadDecayedOutFrames=" << N2(g_leadDecayedOutFrames)
       << " dropStopped=" << N2(g_dropStopped)
       << " dropNoMovement=" << N2(g_dropNoMovement)
       << " dropStopSkipped=" << N2(g_dropStopSkipped)
       << " dropNoChar=" << N2(g_dropNoChar)
       << " dropStopFaulted=" << N2(g_dropStopFaulted)
       << " dropNoPuppet=" << N2(g_dropNoPuppet)
       // THE P-20 NUMBER. `arriveStoppedFrames` is the old behaviour firing - a real stop because
       // the authority is stationary. `arriveHeldFrames` is the jank that no longer happens: an
       // arrival that would have stopped a character its authority was still walking.
       << " arriveStoppedFrames=" << N2(g_arriveStoppedFrames)
       << " arriveHeldFrames=" << N2(g_arriveHeldFrames)
       << " aimDegenerateFrames=" << N2(g_aimDegenerateFrames)
       << " | H016 reconcile=" << (g_reconcileOn ? 1 : 0) << " rcSteps=" << N2(g_rcSteps)
       << " rcMaxStep=" << F2(g_rcMaxStep) << " rcSum=" << F2((float)g_rcSumStep)
       << " rcSaturated=" << N2(g_rcSaturated) << " rcReqMax=" << F3(g_rcReqMax) << " rcVisMaxStep3D=" << F3(g_rcVisMaxStep)
       << " rcVisMaxRate=" << F2(g_rcVisMaxRate) << " rcLayerGapXZ[frames,max]=" << N2(g_rcLayerGapFrames) << "," << F3(g_rcLayerGapMax) << " visPeakLines=" << N2(g_visPeakLines)
       << " rcSlotVerified=" << N2((long long)g_rcSlotVerified)
       << " crawlDriven=" << N2(g_crawlDriven)   /* T-178 crawl1 */
       << " rcSkipped[deadband,prone,ragdoll,noMv,badSetter,off,nonFiniteVisible,nonFiniteField,retired,badTarget,zeroStep,vtImplausible,noChar,noAnimEarly,slotRefused]="
       << N2(g_rcSkippedDeadband) << "," << N2(g_rcSkippedProne) << "," << N2(g_rcSkippedRagdoll) << "," << N2(g_rcSkippedNoMovement)
       << "," << N2(g_rcSkippedBadSetter) << "," << N2(g_rcSkippedOff) << "," << N2(g_rcSkippedNonFiniteVisible) << "," << N2(g_rcSkippedNonFiniteField)
       << "," << N2(g_rcSkippedRetired) << "," << N2(g_rcSkippedBadTarget) << "," << N2(g_rcSkippedZeroStep) << "," << N2(g_rcSkippedVtImplausible)
       << "," << N2(g_rcSkippedNoChar) << "," << N2(g_rcSkippedNoAnimEarly) << "," << N2(g_rcSlotRefused)
       << " puppets=" << N2((long long)g_puppets.size());
    // the follow drive and its probes, in a statement of their own (the chain above is long)
    ss << " follow[interp,extrap,held,heldAtNewest,stopSent,stopRecv,staleMoveDropped,dRecalc]=" << N2(g_followInterp) << "," << N2(g_followExtrap)
       << "," << N2(g_followHeld) << "," << N2(g_followHeldAtNewest) << "," << N2(g_followStopSent) << "," << N2(g_followStopRecv)
       << "," << N2(g_followStaleMoveDropped) << "," << N2(g_followDRecalc)
       << " followMore[heldAtStop,stopOver,stopCleared,outOfOrder,paceFrames,stopSendFailed,stopUnknown,hiddenReplan]=" << N2(g_followHeldAtStop)
       << "," << N2(g_followStopOver) << "," << N2(g_followStopCleared) << "," << N2(g_followOutOfOrder) << "," << N2(g_followPaceFrames)
       << "," << N2(g_followStopSendFailed) << "," << N2(g_followStopUnknown) << "," << N2(g_followHiddenReplan)
       << " followIssue[stop,goal,keep,newest]=" << N2(g_followIssue[1]) << "," << N2(g_followIssue[2]) << "," << N2(g_followIssue[3]) << "," << N2(g_followIssue[4])
       << " followScale[replayFrames,idleFrames,ownWatch,ownWatchPeak,stopSentNpc,stopRecvNpc]=" << N2(g_followReplayFrames) << "," << N2(g_followIdleFrames)
       << "," << N2((long long)g_followOwn.size()) << "," << N2((long long)g_followOwnPeak) << "," << N2(g_followStopSentNpc) << "," << N2(g_followStopRecvNpc)
       << " followPath[issuedPlayer,issuedNpc,deferredPlayer,deferredNpc,stopWide,ownerExpired,ownSent,ownSentEvicted]=" << N2(g_followPathIssued[0]) << "," << N2(g_followPathIssued[1])
       << "," << N2(g_followPathDeferred[0]) << "," << N2(g_followPathDeferred[1]) << "," << N2(g_followStopWide) << "," << N2(g_followOwnerExpired)
       << "," << N2((long long)g_followOwnSent.size()) << "," << N2(g_followOwnSentEvicted)
       << " followSchedule[behindFrames,behindEnters,snapStuckOwnerPt,snapFarOwnerPt,snapBehind,unrecorded]=" << N2(g_followBehindFrames)
       << "," << N2(g_followBehindEnters) << "," << N2(g_followSnapOwnerPt[0]) << "," << N2(g_followSnapOwnerPt[1])
       << "," << N2(g_followSnapBehind) << "," << N2(g_followUnrecorded)
       << P115ReportFields()   // PROBE P115
       << P124ReportFields()   // PROBE P124
       << P123ReportFields();   // PROBE P123

    for (std::map<unsigned int, Puppet>::const_iterator it = g_puppets.begin();
         it != g_puppets.end(); ++it)
    {
        const Puppet& p = it->second;
        // F316 - same defect as the one that crashed the HOST in `ReportSpawns`: a plausibility
        // check on the character does not cover a call that double-dereferences a member pointer.
        // The peer did not crash on it in T086, which makes it latent rather than absent.
        Ogre::Vector3 cur(0, 0, 0);
        SafeReadPosition(p.ch, &cur);
        ss << " | uid=" << N2(it->first)
           << " applied=" << N2(p.applied)
           << " driftXZ=" << F2(p.lastDrift)
           << " maxDriftXZ=" << F2(p.maxDrift)
           << " driftY=" << F2(p.lastDriftY)
           << " maxDriftY=" << F2(p.maxDriftY)
           << " pushing=" << (p.driving ? "yes" : "no")
           // F353 - **`pushing=` AND `driftXZ` BOTH CHANGE MEANING WITH `lead on`, SO THE INPUTS
           // THAT CHANGED THEM ARE PRINTED BESIDE THEM.**
           //
           // T085's calibration - quoted twenty lines below - is that `pushing=` predicted parity
           // perfectly: 25 of 25 undriven puppets within 2.0 units, 0 of 24 driven ones. With the
           // lead applied, a CORRECTLY-tracking puppet reports `pushing=yes` on nearly every frame,
           // so the "undriven and close" cohort all but disappears and that calibration does not
           // carry over. `driftXZ` likewise now contains a deliberate lead as well as tracking
           // error. Neither is wrong; both are unreadable without these three.
           << " vel=" << F2(p.velX) << "," << F2(p.velZ)
           << " velAge=" << F3((float)(NowSeconds() - p.lastMoveAt))
           // P035 / T085 - **DID THE ENGINE ACCEPT THE DRIVE, OR ACCEPT IT AND IGNORE IT?**
           //
           // T085 settled what the failure looks like and left exactly one question open. `pushing=`
           // predicts parity PERFECTLY - 25 of 25 undriven puppets within 2.0 units, 0 of 24 driven
           // ones - so all position error lives in the drive path. And the worst cases are
           // unambiguous: `pushing=yes`, `applied=` climbing into the thousands, `spd=0.0`, and a
           // position byte-identical for **405 seconds**. The peer IS driving them and they are NOT
           // moving.
           //
           // It also REFUTED the explanation I had written down: a puppet **201 units** from the
           // peer's own player stayed frozen for those 405 s while one **2,051 units** away tracked
           // fine, so "the region is not streamed, therefore the character is not simulated" does
           // not survive. The executor said plainly that no existing counter separates *drive
           // refused* from *drive accepted and ignored*, and that this needs a new probe.
           //
           // This is it, read straight off `AbstractMovementBase` (which `CharMovement` derives
           // from), so it is the ENGINE's own view rather than ours:
           //   desiredSpeed > 0 with speedNow == 0  -> our drive landed; the engine will not act
           //   desiredSpeed == 0                        -> our drive did not stick at all
           //   speedCap == 0                            -> the character is clamped to stationary
           //   currentlyMoving / officiallyStopped      -> what the engine believes it is doing
           << MovementDebug(p.ch)
           // F257 - RENAMED. This is `pronedAt`: the PoseState recorded at the last DRIVE, not
           // the character's state now. T077 caught the two disagreeing - `[M2] REPORT` said
           // `prone=4` for a character that `[M5]` had already recorded standing up, and the
           // executor had to reconcile two of our own instruments against each other to see it.
           // The field was doing exactly what it was written to do; the NAME claimed something
           // else. Second time this session a label has cost a reader time (F206), so it now
           // says which it is, and the live value is printed beside it.
           // F342 - **EVER DRIVEN AT ALL**, which `pushing=` cannot say: that is "this frame", and
           // the population T092's non-finite puppets belong to is "never, not once". All five had
           // `pushing=no` AND `maxDriftXZ=0.0` - the drive took its arrival return on every tick
           // they ever had, so **our write path never touched them** - and one carried
           // `mvLimit=-0.0`, which squaring a distance cannot produce. Whatever wrote INF into
           // `desiredMotion`, it was not this mod. Without this field a reader has to infer
           // "never driven" from a maximum drift of 0.0, which is an inference about a different
           // quantity.
           << " everDriven=" << (p.everDriven ? "yes" : "no")
           << " pronedAtLastDrive=" << N2(p.pronedAt)
           << " proneNow=" << N2(PlausibleObj(p.ch)
                                 ? *(int*)((char*)p.ch + kProneStateOff) : -1)
           << " here=" << F2(cur.x) << "," << F2(cur.y) << "," << F2(cur.z)
           << " want=" << F2(p.tx) << "," << F2(p.ty) << "," << F2(p.tz);
    }
    DebugLog(ss.str());
    // PROBE-START: P093
    DebugLog(P093SummaryLine());
    // PROBE-END: P093
}

// review-p3o H2 - THE PUPPET TABLE AND THE STREAMED CHARACTER, DROPPED BEFORE THE ENGINE FREES THEM.
//
// `Puppet::ch` is a `Character*` the drive and reconcile loops call engine methods on, and
// `g_ownedChar` is the character this instance streams; `GameWorld::_clearAndDestroyGameWorldStuff`
// frees both without passing the destroy detour, so nothing else here would ever hear about it.
// Called from `detour_worldTeardown` (store.cpp) BEFORE `orig_worldTeardown`, on the main thread.
//
// **IT DOES NOT GO THROUGH `DropPuppet`, DELIBERATELY.** `DropPuppet` exists to leave a LIVE
// character in a sane state - it re-gates a released AI, writes a stop through `p.ch->movement` and
// restores the saved desired speed. Every one of those is a write into an object the engine is
// about to destroy, on a path (`SuppressCharacter`) that allocates and logs per puppet; and the
// state it is protecting - a walking body, a raised speed - stops existing with the world. So this
// forgets rather than releases, and touches no engine memory at all, which is the same discipline
// `ForgetContextPlatoons` follows.
//
// `windows` is reported because those puppets' AI is left released rather than re-gated. That is
// not a leak we are carrying: the AI objects are freed with the characters. It is printed so a run
// says how many were mid-window, not to imply anything survived.
void ReplicateWorldTeardown()
{
    ProneWritesForget();   // PROBE P019
    const size_t puppets = g_puppets.size();
    long long windows = 0;
    for (std::map<unsigned int, Puppet>::const_iterator it = g_puppets.begin(); it != g_puppets.end(); ++it)
        if (it->second.nativeWindow) ++windows;
    g_puppets.clear();
    g_followClk.clear(); g_followOwn.clear(); g_followOwnSent.clear();   // the owners' clocks and this game's stop watch belong to the world that is going
    g_adoptRefusedUids.clear();   // mirror1: the once-per-uid log belongs to the world that is going
    g_snapFarByUid.clear(); g_snapFarByUidOther = 0;   // runboost2 (T-195 R6): so is the far-snap list
    g_ikKept.clear();   /* P25 interior keep: its building keys name the world that is going (no log - the engine frees them) */
    g_ikMiss.clear(); g_ikLastPass = -1.0;   /* P25 review fold 2-1: the miss back-off and the pass clock belong to that world too */

    const unsigned int owned = g_ownedUid;
    g_ownedUid  = 0;
    g_ownedChar = 0;

    DebugLog("[M2] world teardown: forgot " + N2((long long)puppets) + " puppets ("
             + N2(windows) + " with a native window open) and the streamed character"
             + (owned ? " uid=" + N2((long long)owned) : std::string(" (none)")));
}

/* M8 (T-197 piece 8): a departed player's uids leave the moveForRetired store (F342 g_divergedUids) - the copies are gone because the
   player is, which is not the divergence that store names. The order of the rest is not kept (it is a set). MAIN THREAD. */
void ReplicateForgetUids(const unsigned int* uids, int n)
{
    if (uids == 0) return;
    for (int k = 0; k < n; ++k)
        for (int i = 0; i < g_divergedCount && i < kDivergedSlots; ++i)
            if (g_divergedUids[i] == uids[k]) { g_divergedUids[i] = g_divergedUids[g_divergedCount - 1]; g_divergedUids[--g_divergedCount] = 0; break; }
}

} // namespace coop
