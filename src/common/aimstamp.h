#ifndef SW_COMMON_AIMSTAMP_H
#define SW_COMMON_AIMSTAMP_H
/* T-330: A CHARACTER AIMING ITS CROSSBOW OR BOW, FROM ITS OWNER'S GAME TO ITS COPY. Pure: no engine, no network - the offline
   suite tests every decision here.

   The engine raises a held crossbow / bow in its aim code (Steam 1.0.65 0x51DA50, 1.0.68 0x51E4E0; arguments: the ranged
   combat object, an unused one, a target position, a target): when the object's kind is a hand-held gun (+0x10 == 0xB), a gun
   is stored (+0x28), its state (+0x00) is not 1 and its character's +0x1A9 is set, a non-null target plays 'aimH' on layer 1
   through the layer play (every frame the engine calls it) and a null target fades 'aimH' out (the engine's own lowering). The
   target is only tested against null, and a zero target position skips the engine's turn toward it (decomp_51da50.txt). A copy
   never runs the engine's ranged update, so it never reaches the aim code and its crossbow stays lowered.

   1. THE STAMP (StampTable): the layer-play detour, on whatever thread plays an animation, stamps the animation object with the
      time whenever the call returns into the aim code. Lock-free and fixed: each slot has a sequence number (even = settled,
      odd = one writer holds it, as poselayer.h's StartTable), the animation object and the stamp. An object lives in one of
      kStampProbe slots from its hash: its own, else the first free one, else the one stamped longest ago.
   2. THE OWNER'S WORD (OwnDecide): aiming while its ranged mode is on and its stamp is inside its window (section 0: the character's own update cadence). It rides the
      character's STATE as latchBits bit 17, bit 18 'read' (statewire.h WithAim); a change is pushed at once.
   3. THE COPY'S STEP (CopyStep): right after the copy's own update, while its owner's word says aiming, its own ranged mode is
      on, it is fit to aim (not carried, ragdolled or dead) and the aim code's own tests pass (EngineReason), the aim code is
      called with a target; once any of that ends after it was raised, the aim code is called once with no target.
   4. THE RELOAD MOTION (OwnReloadDecide, ReloadStep): the owner's 'reloading' word rides STATE bits 19-20 (statewire.h
      WithReload); while it says so the copy shows the reload motion through the aim code's own reload branch, its three inputs
      set for that one call only, at a progress counted on the copy. */

#include "poselayer.h"   /* StartCas / StartPublish / StartNext: the sequence-number writes the stamp table shares */

namespace coopaim {

/* ---- 0. freshness at the character's own update cadence (shared by the aim stamp and the reload time left) ------------------
   'It stopped' is told from the character's OWN update cadence, not from the wall clock alone. A character on the camera is
   updated every frame, so the engine re-plays its aim and counts its reload down every frame; one far from the camera is
   updated on a shared turn (at most 6 characters per frame, its saved-up time spent in one step - 05-findings 'T-943 THE LAST
   TWO SITES'), so its events come in steps ~0.3 s apart (T1717: an NPC 6,400 units away, its reload word flickering). An event
   stays fresh for its window: kFreshGapK times the interval between the character's last two events, at least kFreshMs and at
   most kFreshCapMs (a truly stopped one still ends). With no earlier event to measure from (or one kFreshCapMs ago or more) the
   window is the cap, until the next event gives the cadence.
   The mark: ONE 8-byte value - bits 0-31 the event's time (a GetTickCount value), bit 32 set (an event was seen), bits 33-48 the
   interval in ms (at most kFreshCapMs), bit 49 the time was carried forward by a pause (not an event). 0 = no event. */
const unsigned int kFreshMs = 250;      /* the smallest window: an every-frame character's event this old means it stopped */
const unsigned int kFreshGapK = 3;      /* the window = this many of the character's own intervals between events */
const unsigned int kFreshCapMs = 2000;  /* the largest window (and the interval taken when none can be measured) */
const unsigned long long kFreshMarkSeen = 1ull << 32;
const int kFreshMarkGapShift = 33;
const unsigned long long kFreshMarkGapMask = 0xFFFFull;
const unsigned long long kFreshMarkPaused = 1ull << 49;

inline long long FreshMark(unsigned int at, unsigned int gapMs = kFreshCapMs, int paused = 0)
{
    if (gapMs > kFreshCapMs) gapMs = kFreshCapMs;
    return (long long)(kFreshMarkSeen | ((unsigned long long)gapMs << kFreshMarkGapShift) | (paused != 0 ? kFreshMarkPaused : 0ull)
                       | (unsigned long long)at);
}
inline unsigned int FreshMarkAt(long long m) { return (unsigned int)((unsigned long long)m & 0xFFFFFFFFull); }
inline unsigned int FreshMarkGap(long long m) { return (unsigned int)(((unsigned long long)m >> kFreshMarkGapShift) & kFreshMarkGapMask); }
inline int FreshMarkPaused(long long m) { return (((unsigned long long)m & kFreshMarkPaused) != 0) ? 1 : 0; }

/* the window for an interval of gapMs: kFreshGapK x gapMs inside kFreshMs .. kFreshCapMs (an every-frame character keeps 250 ms) */
inline unsigned int FreshWindow(unsigned int gapMs)
{
    if (gapMs > kFreshCapMs) gapMs = kFreshCapMs;
    unsigned int w = gapMs * kFreshGapK;
    if (w < kFreshMs) w = kFreshMs;
    if (w > kFreshCapMs) w = kFreshCapMs;
    return w;
}

/* the window of mark m (kFreshMs when no event) */
inline unsigned int FreshMarkWindow(long long m) { return (m != 0) ? FreshWindow(FreshMarkGap(m)) : kFreshMs; }

/* the mark after an event at now, m the mark before it (0 none): the interval is the time since m's event (kFreshCapMs when none,
   or that long ago or more); after a pause the larger of that and the interval carried through it - the pause's frames are not
   events */
inline long long FreshMarkEvent(long long m, unsigned int now)
{
    unsigned int gap = kFreshCapMs;
    if (m != 0)
    {
        const unsigned int since = now - FreshMarkAt(m);
        if (since < kFreshCapMs)
        {
            gap = since;
            if (FreshMarkPaused(m) != 0 && FreshMarkGap(m) > gap) gap = FreshMarkGap(m);
        }
    }
    return FreshMark(now, gap, 0);
}

/* the mark after a frame of a pause (the game's frame step 0: no event can come): an event still inside its window is carried
   to now (its interval kept), so the word stays as it was; anything else is returned unchanged */
inline long long FreshMarkPause(long long m, unsigned int now)
{
    if (m != 0 && now - FreshMarkAt(m) < FreshMarkWindow(m)) return FreshMark(now, FreshMarkGap(m), 1);
    return m;
}

/* ---- 1. the stamp table --------------------------------------------------------------------------------------------- */

const unsigned int kStampSlots = 512;   /* a power of two */
const unsigned int kStampProbe = 8;
const int kStampTries = 4;              /* compare-exchange / unsettled-read retries before a call gives up */

struct StampSlot
{
    volatile long seq;
    const void* volatile anim;            /* the AnimationClass (0 = free slot) */
    volatile long long mark;              /* FreshMark: when the aim code last played on it, and the interval before that play */
};

struct StampTable
{
    StampSlot s[kStampSlots];
};

/* StampNote's results */
enum { kStampDropped = 0, kStampUpdated = 1, kStampNew = 2, kStampEvicted = 3 };

/* MAIN THREAD, before the detour is installed */
inline void StampClear(StampTable* t)
{
    for (unsigned int i = 0; i < kStampSlots; ++i) { t->s[i].seq = 0; t->s[i].anim = 0; t->s[i].mark = 0; }
}

inline unsigned int StampHash(const void* anim)
{
    unsigned long long h = (unsigned long long)anim * 0x9E3779B97F4A7C15ull;
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 32;
    return (unsigned int)h & (kStampSlots - 1u);
}

/* ANY THREAD: the aim code played on anim at time now (FreshMarkEvent: the interval since its last play is kept with it; a slot
   newly taken has none - the cap). paused != 0: a frame of a pause instead - only anim's own slot is touched, and only to carry a
   play still inside its window to now (FreshMarkPause); kStampDropped when there is nothing to carry. kStampDropped also: anim is
   0, or every slot of its window stayed busy with other writers for kStampTries rounds (nothing written). */
inline int StampNote(StampTable* t, const void* anim, unsigned int now, int paused = 0)
{
    if (anim == 0) return kStampDropped;
    const unsigned int h = StampHash(anim);
    for (int attempt = 0; attempt < kStampTries; ++attempt)
    {
        int mine = -1, freeAt = -1, oldAt = -1;
        long mineSeq = 0, freeSeq = 0, oldSeq = 0;
        long long mineMark = 0;
        unsigned int oldAge = 0;
        for (unsigned int i = 0; i < kStampProbe; ++i)
        {
            const unsigned int at = (h + i) & (kStampSlots - 1u);
            StampSlot& x = t->s[at];
            const long s1 = x.seq;
            if ((s1 & 1) != 0) continue;
            const void* a = x.anim;
            const long long m = x.mark;
            if (x.seq != s1) continue;
            if (a == anim)
            {
                if (FreshMarkAt(m) == now) return kStampUpdated;
                mine = (int)at; mineSeq = s1; mineMark = m; break;
            }
            if (paused != 0) continue;
            if (a == 0) { if (freeAt < 0) { freeAt = (int)at; freeSeq = s1; } continue; }
            const unsigned int age = now - FreshMarkAt(m);
            if (oldAt < 0 || age > oldAge) { oldAt = (int)at; oldSeq = s1; oldAge = age; }
        }
        int at = -1, result = kStampDropped;
        long seen = 0;
        long long next = 0;
        if (mine >= 0)
        {
            next = (paused != 0) ? FreshMarkPause(mineMark, now) : FreshMarkEvent(mineMark, now);
            if (next == mineMark) return kStampDropped;   /* a pause frame with nothing fresh to carry */
            at = mine; seen = mineSeq; result = kStampUpdated;
        }
        else if (paused != 0) return kStampDropped;
        else if (freeAt >= 0) { at = freeAt; seen = freeSeq; result = kStampNew; next = FreshMarkEvent(0, now); }
        else if (oldAt >= 0) { at = oldAt; seen = oldSeq; result = kStampEvicted; next = FreshMarkEvent(0, now); }
        if (at < 0) continue;
        StampSlot& x = t->s[at];
        if (poselayer::StartCas(&x.seq, poselayer::StartNext(seen), seen) != seen) continue;   /* another writer took it: look again */
        x.anim = anim;
        x.mark = next;   /* one 8-byte store: the time and its interval together */
        poselayer::StartPublish(&x.seq, poselayer::StartNext(poselayer::StartNext(seen)));
        return result;
    }
    return kStampDropped;
}

/* ANY THREAD: 1 and *stamp (and *windowMs, its freshness window - FreshMarkWindow) when anim has a settled stamp, 0 none
   (*windowMs then kFreshMs) */
inline int StampFind(const StampTable* t, const void* anim, unsigned int* stamp, unsigned int* windowMs = 0)
{
    if (stamp != 0) *stamp = 0;
    if (windowMs != 0) *windowMs = kFreshMs;
    if (anim == 0) return 0;
    const unsigned int h = StampHash(anim);
    for (unsigned int i = 0; i < kStampProbe; ++i)
    {
        const StampSlot& x = t->s[(h + i) & (kStampSlots - 1u)];
        for (int tries = 0; tries < kStampTries; ++tries)
        {
            const long s1 = x.seq;
            if ((s1 & 1) != 0) continue;
            const void* a = x.anim;
            const long long m = x.mark;
            if (x.seq != s1) continue;
            if (a == anim)
            {
                if (stamp != 0) *stamp = FreshMarkAt(m);
                if (windowMs != 0) *windowMs = FreshMarkWindow(m);
                return 1;
            }
            break;   /* a settled slot holding another object */
        }
    }
    return 0;
}

/* ---- 2. the owner's word -------------------------------------------------------------------------------------------- */

/* rangedMode: the character's ranged mode (1 on, 0 off, -1 unreadable); haveStamp: StampFind's answer (1 a stamp, 0 none,
   -1 the stamp cannot be known - no hook, or the animation object unreadable); windowMs: the stamp's freshness window (StampFind:
   the character's own update cadence; kFreshMs for an every-frame character). 1 aiming, 0 not, -1 not known. */
inline int OwnDecide(int rangedMode, int haveStamp, unsigned int stamp, unsigned int now, unsigned int windowMs = kFreshMs)
{
    if (rangedMode < 0) return -1;
    if (rangedMode == 0) return 0;
    if (haveStamp < 0) return -1;
    if (haveStamp == 0) return 0;
    return (now - stamp < windowMs) ? 1 : 0;
}

/* two samples of the owner's word (-1 not known): true when both are known and they differ */
inline bool Changed(int seen, int now)
{
    return seen >= 0 && now >= 0 && (seen != 0) != (now != 0);
}

/* ---- 3. the copy's step --------------------------------------------------------------------------------------------- */

const int kGunKindHand = 0xB;   /* the ranged combat object's kind (+0x10) the aim code acts on; anything else is a turret's */

/* why the aim code would not raise the gun: kReady it would */
enum { kReady = 0, kRefTurret = 1, kRefNoGun = 2, kRefState1 = 3, kRefFlag = 4 };

/* the aim code's own tests, in its order: kind (+0x10), a stored gun (+0x28), the state (+0x00; 1 lowers), the character's
   +0x1A9 */
inline int EngineReason(int kind, int gunSet, int state, int flag)
{
    if (kind != kGunKindHand) return kRefTurret;
    if (gunSet == 0) return kRefNoGun;
    if (state == 1) return kRefState1;
    if (flag == 0) return kRefFlag;
    return kReady;
}

enum { kStepNothing = 0, kStepCall = 1, kStepFade = 2 };

/* want: the owner's word (1 aiming, 0 not, -1 none); copyMode: the copy's own ranged mode (1 on, 0 off, -1 unreadable); fit: the
   copy may aim (not handed here, carried, ragdolled or dead); engine: EngineReason (-1 unreadable); raised: the last step
   called the aim code with a target. kStepCall: call it with a target; kStepFade: call it once with none; kStepNothing. */
inline int CopyStep(int want, int copyMode, int fit, int engine, int raised)
{
    if (want == 1 && copyMode == 1 && fit != 0 && engine == kReady) return kStepCall;
    return raised != 0 ? kStepFade : kStepNothing;
}

/* ---- 4. the reload motion ------------------------------------------------------------------------------------------- */
/* The owner's reload check (Steam 1.0.65 0x437940, 1.0.68 0x437CE0) sets the ranged combat object's +0x80, counts its time left
   (+0x78) down every frame it runs and has the gun write its progress 0 -> 1 at +0xE0. The aim code's reload branch plays the
   reload motion at that progress while +0x80 is set, the gun is empty (+0x20 == 0) and the progress is above 0
   (decomp_51da50.txt:69-84). A copy runs neither, so on the copy the three are set for one call of the aim code each frame and
   put back right after it; nothing stays for the copy's engine to read. */

/* 'The reload check stopped': the time left has not changed for longer than its window (section 0, FreshMark).
   kReloadFreshMs is the smallest window (kFreshMs). */
const unsigned int kReloadFreshMs = kFreshMs;
const float kReloadFirst = 0.02f;          /* the copy's first frame: above the branch's 0, far from the end pose (1.0) */
const float kReloadFallbackSec = 3.0f;     /* the copy's reload time when its own cannot be read */
const float kReloadMinSec = 0.2f;          /* the copy's own reload time is taken only inside these bounds */
const float kReloadMaxSec = 30.0f;
const float kReloadStepMaxSec = 0.5f;      /* one frame's step is taken up to this: a hitch or a load does not jump the motion */

/* the owner's time left as sampled every frame in one mirror slot. The main thread alone writes it; the stance setter's detour
   reads it on another thread. The change, its time and the character's interval between changes are ONE 8-byte store (mark), so
   a reader never pairs a change with another change's time or window; a slot taken by another character clears mark before c
   changes, and ReloadOwnRead reads c again after mark - a reader that raced a reassignment sees 'no change' (at worst one 'not
   reloading' word, which the next push or STATE replaces). */
struct ReloadSeen
{
    volatile unsigned int uid;
    const void* volatile c;
    volatile int have;          /* left below was read at least once for this character */
    volatile float left;        /* the time left last read */
    volatile long long mark;    /* 0 = no change seen; else FreshMark(the latest change's time, the interval, paused) */
};

/* 1 and *at (and *windowMs, its freshness window) when slot s saw a change, 0 none (*windowMs then kFreshMs). One read of
   mark: the time and the window always belong to the same change. */
inline int ReloadSeenChanged(const ReloadSeen* s, unsigned int* at, unsigned int* windowMs = 0)
{
    const long long m = s->mark;
    if (at != 0) *at = FreshMarkAt(m);
    if (windowMs != 0) *windowMs = FreshMarkWindow(m);
    return (m != 0) ? 1 : 0;
}

inline void ReloadSeenClear(ReloadSeen* s)
{
    s->mark = 0; s->have = 0; s->left = 0.0f; s->uid = 0u; s->c = 0;
}

/* MAIN THREAD, every frame: slot s holds character c (uid) whose time left read as `left` (read 0 = unreadable, nothing noted). A
   new character or uid starts the slot over with no change seen. A change records its time and the interval since the change
   before (FreshMarkEvent). stopped: the game's frame
   step is 0 (paused) - the time left cannot fall, so a change still inside its window is kept fresh (its interval kept) and the
   owner's word stays as it was. */
inline void ReloadSeenNote(ReloadSeen* s, unsigned int uid, const void* c, int read, float left, int stopped, unsigned int now)
{
    if (s->uid != uid || s->c != c) { ReloadSeenClear(s); s->uid = uid; s->c = c; }
    if (read == 0) return;
    if (stopped != 0) s->mark = FreshMarkPause(s->mark, now);
    else if (s->have != 0 && left != s->left) s->mark = FreshMarkEvent(s->mark, now);
    s->have = 1;
    s->left = left;
}

/* the owner's word. rangedMode: 1 on, 0 off, -1 unreadable; read: the ranged combat object (and its gun, when one is stored) read
   (0 = unreadable); kind / flag (+0x80) / ammo (gun +0x20) / progress (gun +0xE0): the branch's own inputs, flag 0 when no gun is
   stored; changed: 1 the time left was seen changing at changedAt, 0 not since the slot's first sample, -1 never sampled;
   windowMs: that change's freshness window (ReloadSeenChanged; kReloadFreshMs for an every-frame character).
   1 reloading, 0 not, -1 not known. */
inline int OwnReloadDecide(int rangedMode, int read, int kind, int flag, int ammo, float progress, int changed,
                           unsigned int changedAt, unsigned int now, unsigned int windowMs = kReloadFreshMs)
{
    if (rangedMode < 0) return -1;
    if (rangedMode == 0) return 0;
    if (read == 0) return -1;
    if (kind != kGunKindHand || flag == 0 || ammo != 0 || !(progress > 0.0f)) return 0;
    if (changed < 0) return -1;
    if (changed == 0) return 0;
    return (now - changedAt < windowMs) ? 1 : 0;
}

/* the owner's aim word as sent: 'not aiming' while it reloads - its aim stamp stays fresh for a moment into the reload (the
   reload motion is not the stamped play), and the copy starts no reload while the aim word is on */
inline int AimWhileReload(int aim, int reloading)
{
    return (reloading == 1) ? 0 : aim;
}

/* the copy may show the motion: not handed to this game, its owner present, fit (not carried, ragdolled or dead), its ranged mode
   on and the aim code's own tests pass */
inline int ReloadMayShow(int handedHere, int ownerGone, int fit, int copyMode, int engine)
{
    return (handedHere == 0 && ownerGone == 0 && fit != 0 && copyMode == 1 && engine == kReady) ? 1 : 0;
}

/* the copy's reload time: its own when read and inside the bounds, else the fixed one (a NaN fails both compares) */
inline float ReloadSeconds(int read, float sec)
{
    return (read != 0 && sec >= kReloadMinSec && sec <= kReloadMaxSec) ? sec : kReloadFallbackSec;
}

/* the time into the shown motion after one more frame: the engine's frame step dt when it was read (dtRead) - the step the
   owner's reload check counts its time left down by, so the motion follows the world's speed and stands still while paused -
   else the wall-clock seconds since the last step. A step that is not above 0 adds nothing; one above kReloadStepMaxSec adds
   that much. */
inline float ReloadAdvance(float elapsed, int dtRead, float dt, float wallSec)
{
    float d = (dtRead != 0) ? dt : wallSec;
    if (!(d > 0.0f)) d = 0.0f;
    if (d > kReloadStepMaxSec) d = kReloadStepMaxSec;
    return elapsed + d;
}

/* the progress shown `elapsed` seconds into a reload of sec seconds: kReloadFirst at the start (never 0, never the end pose on
   the first frame), at most 1 - the end pose, held while the owner's word still says reloading (its no-bolt freeze) */
inline float ReloadProgress(float elapsed, float sec)
{
    float p = (sec > 0.0f) ? elapsed / sec : 1.0f;
    if (!(p >= kReloadFirst)) p = kReloadFirst;
    if (p > 1.0f) p = 1.0f;
    return p;
}

enum { kRlNothing = 0, kRlStart = 1, kRlCall = 2, kRlEnd = 3 };
/* why a shown motion ends; kRlWhyFault and kRlWhyNoGun come from the call (ReloadEndAfterCall) */
enum { kRlWhyNone = 0, kRlWhyAim = 1, kRlWhyFired = 2, kRlWhyOwner = 3, kRlWhyRefused = 4, kRlWhyFault = 5, kRlWhyNoGun = 6 };

/* want: the owner's reload word (1, else not); aimWant: its aim word; fired: the owner's bolt was fired on this copy since the last
   step; ok: ReloadMayShow; on: the motion is shown; spent: a motion under this word already ended. kRlStart (then call), kRlCall
   (also at the end pose, for as long as the word says reloading), kRlEnd with *why, or kRlNothing. */
inline int ReloadStep(int want, int aimWant, int fired, int ok, int on, int spent, int* why)
{
    if (why != 0) *why = kRlWhyNone;
    if (on == 0)
        return (want == 1 && aimWant != 1 && fired == 0 && ok != 0 && spent == 0) ? kRlStart : kRlNothing;
    int w = kRlWhyNone;
    if (aimWant == 1) w = kRlWhyAim;
    else if (fired != 0) w = kRlWhyFired;
    else if (want != 1) w = kRlWhyOwner;
    else if (ok == 0) w = kRlWhyRefused;
    if (w == kRlWhyNone) return kRlCall;
    if (why != 0) *why = w;
    return kRlEnd;
}

/* after the copy's call - callResult 1 called, 0 no plausible gun (nothing written), -1 faulted: kRlWhyNone the motion goes on,
   else why it ends */
inline int ReloadEndAfterCall(int callResult)
{
    if (callResult == 1) return kRlWhyNone;
    return (callResult == 0) ? kRlWhyNoGun : kRlWhyFault;
}

/* an ended motion gets the engine's own lowering (one aim-code call with no target) - not after a faulted call, not when the aim
   was raised this same frame (aimCalled), and only where the aim code's own tests pass */
inline int ReloadLowerAfterEnd(int why, int aimCalled, int engine)
{
    return (why != kRlWhyFault && aimCalled == 0 && engine == kReady) ? 1 : 0;
}

/* the 'spent' latch after a step: a motion that ended while the word still says reloading is not shown again until the word falls,
   the owner aims or fires - each of which comes before the owner's next reload */
inline int ReloadSpentNext(int spent, int step, int want, int aimWant, int fired)
{
    if (want != 1 || aimWant == 1 || fired != 0) return 0;
    return (step == kRlEnd) ? 1 : spent;
}

}   // namespace coopaim

#endif
