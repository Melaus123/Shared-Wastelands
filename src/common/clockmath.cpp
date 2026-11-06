/* src/common/clockmath.cpp - see clockmath.h. NO ENGINE READ, NO GLOBAL, NO WINDOWS HEADER. */
#include "clockmath.h"

#include <cmath>
#include <cstring>

namespace coopclock {

ClockTuning ClockDefaultTuning()
{
    ClockTuning t;
    t.deadBandMinutes   = 1.0;
    t.throttleFraction  = 0.5;
    t.forwardSetMinutes = 30.0;
    t.forwardBoundHours = 24.0;
    t.joinBoundMinutes  = 7.0 * 24.0 * 60.0;   /* P8a (F637): the same seven in-game days the notebook's seed bound uses; clock1 (46c): BACKWARD jumps only */
    return t;
}

ClockAct ClockDecide(double localAbs, double sharedAbs, double prevLocalAbs,
                     int joinJumpOwed, const ClockTuning& t)
{
    ClockAct out;
    out.kind = kClockNothing;
    out.writeAbs = 0.0;
    out.gapMinutes = (sharedAbs - localAbs) * 60.0;          /* + = this game is BEHIND the shared clock */
    out.engineAdvanceHours = (prevLocalAbs < 0.0) ? -1.0 : (localAbs - prevLocalAbs);
    out.capHours = 0.0;

    /* THE JOIN JUMP. EITHER DIRECTION - decision 46, and it is what SaveManager::loadGame 0x373DC0 does itself
       at decomp_373dc0.txt:212 (Clock::restore 0x66D0C0) one line below resetGame. It is exempt from
       forwardBoundHours BY CONSTRUCTION: that bound is tested only below this line.
       P8a (F637 / review-p7y HIGH-1) - IT IS NOT EXEMPT FROM joinBoundMinutes.  Seven in-game days is the width
       amendment 46b already refuses a seed offer at, and until P8a the two disagreed: the notebook refused the
       offer and one second later this jump took the refused game to the world's clock regardless, backward by
       the whole gap.  Over the bound NOTHING IS WRITTEN - writeAbs is set to localAbs, so a caller that ignores
       the kind still cannot move this clock - and the caller books the refusal and names both clocks.
       clock1 (user 2026-09-26, AMENDMENT 46c) - THE BOUND BINDS ONLY A BACKWARD JUMP, i.e. a joining game AHEAD
       of the world by more than joinBoundMinutes.  A backward jump parks every pending engine timer ("at hour X")
       until the clock climbs back past it; that is the harm the bound exists for, and it is also the only gap
       the notebook refuses (kPullSeedOverBound is an offer AHEAD of the world), so the two sides still never
       disagree about one gap.  A FORWARD jump - this game BEHIND the world - is what the engine's own load does
       and only makes due timers fire, and the notebook never refuses that direction (an offer behind the world
       is kPullIgnore), so it is taken at ANY size: a new character, or any late joiner whose save is behind,
       can join a world of any age. */
    if (joinJumpOwed)
    {
        /* gapMinutes < 0 means this game is AHEAD, so the jump would be BACKWARD by -gapMinutes */
        if (-out.gapMinutes > t.joinBoundMinutes) { out.kind = kClockJumpOverBound; out.writeAbs = localAbs; return out; }
        out.kind = kClockJump; out.writeAbs = sharedAbs; return out;
    }

    {
        const double absGap = (out.gapMinutes < 0.0) ? -out.gapMinutes : out.gapMinutes;
        if (absGap <= t.deadBandMinutes) { out.kind = kClockNothing; return out; }
    }

    /* AHEAD. NOTHING IS WRITTEN, EVER, IN ANY BAND. There is no arm below this line that can produce a target
       under localAbs, which is why the post-condition holds by construction rather than by a clamp that could
       be got wrong. The ahead gap is closed by the notebook's bounded forward pull (ClockPullDecide) and is
       reported as aheadPassesNoWrite meanwhile. */
    if (out.gapMinutes < 0.0) { out.kind = kClockAheadNoWrite; return out; }

    if (out.gapMinutes > t.forwardSetMinutes)
    {
        /* review-p7j H-3. 24 in-game hours is not drift; the most likely cause is two different saves sharing
           one notebook folder, and writing that set would move a world by a day on a guess. */
        if (out.gapMinutes > t.forwardBoundHours * 60.0) { out.kind = kClockOverBound; return out; }
        out.kind = kClockForwardSet; out.writeAbs = sharedAbs; return out;
    }

    /* THE ENGINE ADDED NOTHING SINCE THE LAST PASS - it is paused, held by a dialogue window, or inside a load.
       review-p7j H-1's failing scenario ends HERE, with no write at all, instead of with a negative one.
       `<= 0.0` and not `== 0.0`: a negative measured advance means something else moved this clock and this
       pass has no business scaling it. -1.0 (no previous pass in this world epoch) lands here too. */
    if (out.engineAdvanceHours <= 0.0) { out.kind = kClockNothing; return out; }

    out.capHours = t.throttleFraction * out.engineAdvanceHours;
    {
        const double gapHours = out.gapMinutes / 60.0;
        const double add = (out.capHours < gapHours) ? out.capHours : gapHours;   /* the tighter of cap and gap */
        out.kind = kClockThrottle;
        out.writeAbs = localAbs + add;   /* >= localAbs: both cap and gap are strictly positive in this arm */
    }
    return out;
}

double ClockSharedHoursAt(double srvHours, double srvAtMs, double nowMs,
                          float srvSpeed, float hourRealSeconds)
{
    if (srvHours < 0.0) return -1.0;
    if (!(hourRealSeconds > 0.0f)) return srvHours;
    {
        const double dtSec = (nowMs - srvAtMs) / 1000.0;
        if (dtSec < 0.0 || dtSec > 30.0) return srvHours;   /* a stale or wrapped reading is not extrapolated */
        return srvHours + dtSec * (double)srvSpeed / (double)hourRealSeconds;
    }
}

int ClockSpeedIsValid(float speed)
{
    /* NaN fails both comparisons; +infinity fails the upper bound; negatives fail the lower one. */
    return (speed >= 0.0f && speed <= kClockSpeedMax) ? 1 : 0;
}

int ClockSpeedIsBuiltIn(float speed)
{
    return (speed == 0.0f || speed == 1.0f || speed == 2.0f || speed == 5.0f) ? 1 : 0;
}

void LiveSpeedMemoReset(LiveSpeedMemo* m)
{
    m->lastSeen = -1.0f; m->appliedFor = -1.0f; m->pending = -1.0f;
    m->adoptAt = 0u; m->holdAt = 0u; m->adopted = 0; m->held = 0; m->holding = 0;
}

void LiveSpeedNoteSeen(LiveSpeedMemo* m, float seen)
{
    m->lastSeen = seen;
    m->appliedFor = -1.0f;   /* the last recorded change is no longer this game's own apply */
    m->holding = 0;
}

void LiveSpeedNoteApplied(LiveSpeedMemo* m, float seenAfter, float srv)
{
    LiveSpeedNoteSeen(m, seenAfter);
    m->appliedFor = srv;
}

void LiveSpeedNoteWaited(LiveSpeedMemo* m, float live)
{
    m->pending = live;
}

void LiveSpeedNoteAdopted(LiveSpeedMemo* m, float seenNow, unsigned int nowMs, int voteCounts)
{
    m->lastSeen = seenNow;
    m->appliedFor = -1.0f;
    m->pending = -1.0f;
    m->adoptAt = nowMs; m->adopted = 1;
    m->holding = (voteCounts && (!m->held || (unsigned int)(nowMs - m->holdAt) >= kClockSpeedRevoteMs)) ? 1 : 0;
    if (m->holding) { m->holdAt = nowMs; m->held = 1; }
}

static int LiveSpeedUnaccountedPace(const LiveSpeedMemo& m, float live)
{
    return (m.lastSeen >= 0.0f && live != m.lastSeen && live > 0.0f && ClockSpeedIsValid(live)) ? 1 : 0;
}

float LiveSpeedAdoptValue(const LiveSpeedMemo& m, float live)
{
    if (LiveSpeedUnaccountedPace(m, live)) return live;
    return (m.pending >= 0.0f) ? m.pending : live;
}

int ClockLiveSpeedDecide(const LiveSpeedMemo& m, float live, float srv, float myVote, unsigned int nowMs, int answered)
{
    const unsigned int sinceAdopt = m.adopted ? (unsigned int)(nowMs - m.adoptAt) : 0u;
    const int gapPassed = (!m.adopted || sinceAdopt >= kClockSpeedAdoptGapMs) ? 1 : 0;
    const int revoteDue = (!m.adopted || sinceAdopt >= kClockSpeedRevoteMs) ? 1 : 0;
    const int pace = (live > 0.0f && ClockSpeedIsValid(live)) ? 1 : 0;
    if (LiveSpeedUnaccountedPace(m, live) && (live != myVote || revoteDue)) return gapPassed ? kLiveSpeedAdopt : kLiveSpeedWait;
    if (m.pending >= 0.0f && gapPassed) return kLiveSpeedAdopt;
    if (live == srv) return kLiveSpeedNothing;
    if (m.holding && !answered && pace && live == myVote && m.adopted && sinceAdopt < kClockSpeedVoteHoldMs) return kLiveSpeedHold;
    if (m.appliedFor >= 0.0f && m.appliedFor == srv && live == m.lastSeen) return kLiveSpeedNothing;
    return kLiveSpeedApply;
}

int ClockSetterCallAccounted(int callerInGameImage, int callerThisMod, float speed, int engineHeldBefore)
{
    return (callerInGameImage || callerThisMod || !(speed > 0.0f) || engineHeldBefore) ? 1 : 0;
}

void ClockEncodeDown(char out14[14], double hours, float speed,
                     unsigned char modeByte, unsigned char seedState)
{
    std::memcpy(&out14[0], &hours, 8);
    std::memcpy(&out14[8], &speed, 4);
    out14[12] = (char)modeByte;
    out14[13] = (char)seedState;
}

int ClockParseDown(const char* p, std::size_t n, double* hours, float* speed,
                   unsigned char* modeByte, unsigned char* seedState)
{
    if (p == 0 || n < 13) return 0;   /* NONE of the out-parameters is written on a refusal */
    std::memcpy(hours, &p[0], 8);
    std::memcpy(speed, &p[8], 4);
    *modeByte = (unsigned char)p[12];
    *seedState = (n >= 14) ? (unsigned char)p[13] : (unsigned char)0;
    return 1;
}

int ClockPullDecide(double shared, int seeded, int windowOpen, double fromGame,
                    double pullBoundMinutes, double mismatchBoundMinutes, double seedBoundMinutes,
                    double* newSharedOut)
{
    /* A world with no time at all is worse than any other outcome here: this arm is what gives an empty
       notebook folder a clock, whatever the window says. P7w: this is ALSO the arm T235 needed and never
       reached - an unseeded notebook takes the FIRST offer whenever it arrives, and the window governs
       only what happens to the offers AFTER it. */
    if (!seeded) { *newSharedOut = fromGame; return kPullSeed; }
    {
        const double delta = fromGame - shared;
        if (delta <= 0.0) return kPullIgnore;                  /* FORWARD ONLY, ALWAYS - decision 46 */
        /* P7y (amendment 46b, and F612 / review-p7w M-2): THE WIDTH TEST COMES BEFORE THE WINDOW. Until
           P7y an offer inside the seed window was adopted whatever its size, because this test sat BELOW
           the windowOpen arm - so two games pointed at saves 300 days apart moved the shared world 299
           days forward on the first offer pair, with nothing refusing it and nothing written down that
           the window suspended the refusal. Seven in-game days is the amendment's own width: by
           construction no game can DRIFT that far from the others (there is no single-player mode and no
           relative speed), so a gap this wide is a bug, a tampered save or two saves in one folder.
           NOTHING IS WRITTEN on this arm: the offering game keeps its own clock, counted. */
        /* clock1 (user 2026-09-26, amendment 46c; review-clock1 D1): INSIDE the first-players window the furthest
           offer wins at ANY distance (decision 46a's own rule). A brand-new character starts on day 1, so a world's
           first sitting can legitimately pair day 1 with day 300 - and if the day-1 game's offer seeded first, the
           P7y order refused the real world's clock and left the world on day 1 all sitting. Outside the window the
           seven-day width still refuses: a game that far ahead of a running world is a bug or a wrong save. */
        if (windowOpen) { *newSharedOut = fromGame; return kPullWindowAdvance; }
        if (delta * 60.0 > seedBoundMinutes) return kPullSeedOverBound;
        /* 24 in-game hours is not drift, and moving a world by a day on a guess is worse than not
           converging. Refused, and counted apart from the rate limit below (review-p7j H-3). */
        if (delta * 60.0 > mismatchBoundMinutes) return kPullOverBound;
        /* P7w (F583): BOUNDED PER PASS, UNBOUNDED IN TOTAL. The write is at most pullBoundMinutes and the
           next pass repeats it, so the gap converges instead of standing open forever. */
        if (delta * 60.0 > pullBoundMinutes)
        { *newSharedOut = shared + pullBoundMinutes / 60.0; return kPullRateLimited; }
        *newSharedOut = fromGame; return kPullPlayForward;
    }
}

float EffectiveSpeedFrom(const std::vector<void*>& order,
                         const std::map<void*, float>& votes,
                         int consensus, float lastRealSpeed)
{
    /* review-p7e H-3: NOT 0.0. Nobody connected means this world's clock does not ADVANCE - which the caller
       enforces with its own !order.empty() guard - and it does NOT mean the world is paused. */
    if (order.empty()) return lastRealSpeed;
    if (!consensus)
    {
        std::map<void*, float>::const_iterator it = votes.find(order[0]);
        return (it == votes.end()) ? 1.0f : it->second;
    }
    {
        float best = -1.0f;
        for (std::size_t i = 0; i < order.size(); ++i)
        {
            std::map<void*, float>::const_iterator it = votes.find(order[i]);
            if (it == votes.end()) continue;   /* a game that has never voted is NOT counted as 1x */
            if (best < 0.0f || it->second < best) best = it->second;
        }
        return (best < 0.0f) ? 1.0f : best;
    }
}

int TimeModeOfWire(const char* wire)
{
    if (wire == 0) return -1;
    if (std::strcmp(wire, "fixed") == 0) return kTimeModeAdmin;
    if (std::strcmp(wire, "consensus") == 0) return kTimeModeSlowest;
    if (std::strcmp(wire, "vote") == 0) return kTimeModeAgree;
    if (std::strcmp(wire, "locked") == 0) return kTimeModeNormal;
    return -1;
}

const char* TimeModeWire(int mode)
{
    if (mode == kTimeModeAdmin) return "fixed";
    if (mode == kTimeModeSlowest) return "consensus";
    if (mode == kTimeModeAgree) return "vote";
    if (mode == kTimeModeNormal) return "locked";
    return "";
}

static int SpeedListHas(const std::vector<void*>& list, void* p)
{
    for (std::size_t i = 0; i < list.size(); ++i) if (list[i] == p) return 1;
    return 0;
}

float WorldSpeedFor(int mode, const std::vector<void*>& inWorld, const std::vector<void*>& admins,
                    const std::map<void*, float>& votes, const std::map<void*, long long>& voteSeq, float lastRealSpeed)
{
    /* nobody in the world: the clock holds (the caller's own test) and the published speed stays the last real one */
    if (inWorld.empty()) return lastRealSpeed;
    if (mode == kTimeModeSlowest) return EffectiveSpeedFrom(inWorld, votes, 1, lastRealSpeed);
    /* owner 1351 a: PAUSE IS A SPEED. Normal speed only has one speed, so nobody can pause it; under Everyone agrees a pause is
       compared like any other choice, so the world pauses only when every game in it paused. */
    if (mode == kTimeModeNormal) return 1.0f;
    if (mode == kTimeModeAgree)
    {
        float agreed = -1.0f;
        int same = 1;
        for (std::size_t i = 0; i < inWorld.size(); ++i)
        {
            std::map<void*, float>::const_iterator it = votes.find(inWorld[i]);
            const float v = (it == votes.end()) ? 1.0f : it->second;   /* a game that never chose is at 1x */
            if (agreed < 0.0f) agreed = v;
            else if (v != agreed) same = 0;
        }
        if (!same || agreed < 0.0f) return 1.0f;
        return agreed;
    }
    /* Admin only (and a byte no mode has, which the world server never stores): the latest choice an admin in the world made
       while an admin */
    {
        float speed = 1.0f;
        long long latest = 0;
        int found = 0;
        for (std::size_t i = 0; i < inWorld.size(); ++i)
        {
            void* const p = inWorld[i];
            if (!SpeedListHas(admins, p)) continue;
            std::map<void*, float>::const_iterator vit = votes.find(p);
            if (vit == votes.end()) continue;
            std::map<void*, long long>::const_iterator sit = voteSeq.find(p);
            if (sit == voteSeq.end()) continue;   /* no choice made while an admin: the world server keeps voteSeq only for an admin's press */
            const long long at = sit->second;
            if (!found || at > latest) { found = 1; latest = at; speed = vit->second; }
        }
        return speed;
    }
}

int TimeModePressCounts(int mode, int adminNow, float speed)
{
    (void)speed;   /* owner 1351 a: a pause is answered like any speed in every mode, so the value no longer decides */
    if (mode == kTimeModeSlowest || mode == kTimeModeAgree) return 1;
    if (mode == kTimeModeNormal) return 0;   /* one speed, pause included: no press moves it */
    if (mode == kTimeModeAdmin) return adminNow ? 1 : 0;
    return 0;
}

int SpeedPressAnswer(float chosen, float worldSpeed, long long clocksSinceSend)
{
    if (clocksSinceSend < 1) return kSpeedPressWait;
    if (std::fabs(chosen - worldSpeed) < 0.001f) return kSpeedPressTaken;
    return (clocksSinceSend >= 2) ? kSpeedPressNotTaken : kSpeedPressWait;
}

const char* SpeedNotTakenLine(int mode)
{
    if (mode == kTimeModeAdmin) return "Only an admin can change the game speed.";
    if (mode == kTimeModeNormal) return "This world always runs at normal speed.";
    if (mode == kTimeModeAgree) return "The speed changes when every player picks the same one.";
    if (mode == kTimeModeSlowest) return "The slowest speed picked is used.";
    return "";
}

int SpeedLineDue(int saidBefore, unsigned int lastSaidMs, unsigned int nowMs)
{
    if (!saidBefore) return 1;
    return (nowMs - lastSaidMs >= kSpeedLineGapMs) ? 1 : 0;
}

int DialoguePauseLetThrough(int site, int linked, int* suppressed)
{
    if (suppressed == 0) return 1;
    if (site == 1)
    {
        *suppressed = linked ? 1 : 0;
        return linked ? 0 : 1;
    }
    if (site == 2)
    {
        if (*suppressed != 0) { *suppressed = 0; return 0; }
        return 1;
    }
    return 1;
}

int MenuPauseDecide(int site, int linked, int inWorld, int mode, MenuPauseState* st)
{
    if (st == 0) return kMenuPass;
    if (site == kMenuSiteMenuOpen || site == kMenuSiteBuildEnter)
    {
        int* const mine = (site == kMenuSiteMenuOpen) ? &st->menuSkipped : &st->buildSkipped;
        if (!linked || !inWorld) { *mine = 0; return kMenuPass; }
        *mine = 1;
        if (site == kMenuSiteBuildEnter && mode == kTimeModeSlowest && !st->voteHeld) { st->voteHeld = 1; return kMenuSkipVote; }
        return kMenuSkip;
    }
    if (site == kMenuSiteMenuClose || site == kMenuSiteBuildLeave)
    {
        int* const mine = (site == kMenuSiteMenuClose) ? &st->menuSkipped : &st->buildSkipped;
        if (*mine == 0) return kMenuPass;
        *mine = 0;
        if (site == kMenuSiteBuildLeave && st->voteHeld && st->menuOpen == 0 && st->talkOpen == 0) { st->voteHeld = 0; return kMenuSkipRestore; }
        return kMenuSkip;
    }
    return kMenuPass;
}

int MenuPauseNoteGameMenu(int open, int linked, int inWorld, int mode, MenuPauseState* st)
{
    if (st == 0) return kMenuPass;
    if (open && linked && inWorld)
    {
        st->menuOpen = 1;
        if (mode == kTimeModeSlowest && !st->voteHeld) { st->voteHeld = 1; return kMenuSkipVote; }
        return kMenuPass;
    }
    {
        const int was = st->menuOpen;
        st->menuOpen = 0;
        if (was && st->voteHeld && st->buildSkipped == 0 && st->talkOpen == 0) { st->voteHeld = 0; return kMenuSkipRestore; }
    }
    return kMenuPass;
}

int MenuPauseNoteTalkWindow(int open, int linked, int inWorld, int mode, MenuPauseState* st)
{
    if (st == 0) return kMenuPass;
    if (open && linked && inWorld)
    {
        if (st->talkOpen) return kMenuPass;   /* polled every frame: only the opening edge votes */
        st->talkOpen = 1;
        if (mode == kTimeModeSlowest && !st->voteHeld) { st->voteHeld = 1; return kMenuSkipVote; }
        return kMenuPass;
    }
    {
        const int was = st->talkOpen;
        st->talkOpen = 0;
        if (was && st->voteHeld && st->buildSkipped == 0 && st->menuOpen == 0) { st->voteHeld = 0; return kMenuSkipRestore; }
    }
    return kMenuPass;
}
void MenuPauseNotePress(MenuPauseState* st) { if (st != 0) st->voteHeld = 0; }
void MenuPauseNoteWorldClosed(MenuPauseState* st)
{
    if (st == 0) return;
    st->menuSkipped = 0; st->buildSkipped = 0; st->voteHeld = 0; st->menuOpen = 0; st->talkOpen = 0;
}

}   /* namespace coopclock */
