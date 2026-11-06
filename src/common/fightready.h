/* src/common/fightready.h - T-890: the getting-ready pause after this game's own player character's blow on another player's
 * copy, set from the blow's real result.
 *
 * On the attacker's game the engine's hit on another player's copy is not run (the victim's game decides it), so the engine's
 * swing-contact routine (1.0.65 0x609860) reads "no hit" and writes the attacker's fight object getting-ready timer (+0x148)
 * with its no-hit value (0.2 s). The victim's game's real result for that blow (HIT's decided blow, hitwire.h Blow.result:
 * 0 no hit, 5 blocked, 2 landed) arrives later; at the K2 safe point the plugin then sets the timer to the engine's own value
 * for that result, read through the engine's lookup (1.0.65 0x883120) with the index the contact routine passes for it.
 *
 * Pure: the match of a result to its contact, the arm (held / counting / late) and the new timer. No engine memory, no
 * Windows, no allocation; the offline suite runs the same functions. C++03 (VS2010 v100).
 */
#pragma once

namespace fightready {

/* the lookup index 0x609860 passes for each hit result (dis 609a18-609a76): 0 no hit -> 6, 5 blocked -> 1, any other -> 0 */
const int kIndexLanded  = 0;
const int kIndexBlocked = 1;
const int kIndexNoHit   = 6;
inline int LookupIndex(int result)
{
    if (result == 0) return kIndexNoHit;
    if (result == 5) return kIndexBlocked;
    return kIndexLanded;
}

/* why a result wrote nothing (the [FIGHT] ready line's "late why=") - kWhyNone: it was written or added */
const int kWhyNone        = 0;
const int kWhyTimerDone   = 1;   /* the pause had already ended: the engine counted the no-hit value down */
const int kWhyNewSwing    = 2;   /* a new swing began (another swing start, or the timer rewritten above the contact's value) */
const int kWhyNoContact   = 3;   /* no unanswered contact by this attacker: it never reached one, or its result came already */
const int kWhyOtherVictim = 4;   /* the attacker's last contact was another victim or blow, or a hit this game ran itself */
const int kWhyNoFight     = 5;   /* the attacker or its fight object is not readable here, or the lookup is not in the table */
const int kWhyLoading     = 6;   /* a world is loading or tearing down: no engine write */
const int kWhyNotShorter  = 7;   /* the copy's held value is not above the no-hit value: a write would not shorten its pause */
const int kWhyOtherWriter = 8;   /* something other than the countdown changed the copy's timer after its swing ended */
inline const char* WhyName(int why)
{
    switch (why)
    {
    case kWhyTimerDone:   return "timerDone";
    case kWhyNewSwing:    return "newSwing";
    case kWhyNoContact:   return "noContact";
    case kWhyOtherVictim: return "otherVictim";
    case kWhyNoFight:     return "noFight";
    case kWhyLoading:     return "loading";
    case kWhyNotShorter:  return "notShorter";
    case kWhyOtherWriter: return "otherWriter";
    default:              return "none";
    }
}

/* The attacker's LAST contact, as the hit detour saw it (AI worker), stamped on the main thread with the attacker's swing
   number at the time the note was taken off the ring. */
struct Contact
{
    int          have;        /* 1: a contact was noted for this attacker */
    unsigned int victim;      /* the struck character's uid */
    int          combo;       /* the hit's comboId (fight +0x26C) */
    int          suppressed;  /* 1: this game did not run the engine's hit (the victim's game decides it); 0: it ran it */
    int          flag;        /* the fight object's +0x144 at the hit call */
    unsigned int swing;       /* the attacker's swing number when the note was drained */
    int          resolved;    /* 1: a result was already matched to this contact */
};

/* The match rule: kWhyNone when the result belongs to the attacker's last contact - this victim and comboId, suppressed, the
   same swing number as now, not yet resolved; otherwise why it does not. */
inline int Match(const Contact& c, unsigned int victim, int combo, unsigned int swingNow)
{
    if (!c.have || c.resolved) return kWhyNoContact;
    if (c.victim != victim || c.combo != combo || !c.suppressed) return kWhyOtherVictim;
    if (c.swing != swingNow) return kWhyNewSwing;
    return kWhyNone;
}

/* The arm, from the live timer and flag against the contact's: */
const int kArmHeld      = 0;   /* the timer is still the contact's value and the flag as at contact: the countdown has not begun */
const int kArmCounting  = 1;   /* the flag is set and part of the contact's value is left: the countdown runs */
const int kArmTimerDone = 2;   /* the countdown ended (timer at or below 0, or the flag cleared) */
const int kArmNewSwing  = 3;   /* the timer is above the contact's value: something rewrote it (decide's 0.5, a new contact) */
inline int Arm(float timer, int flag, int flagAtContact, float contactValue)
{
    if (timer == contactValue && flag == flagAtContact) return kArmHeld;
    if (timer > contactValue) return kArmNewSwing;
    if (flag != 0 && timer > 0.0f) return kArmCounting;
    return kArmTimerDone;
}

/* The timer to write: held -> the result's value (the countdown then starts from it); counting -> what is left plus
   (value - the contact's value), so the whole pause is the result's; late -> unchanged. */
inline float After(int arm, float timer, float value, float contactValue)
{
    if (arm == kArmHeld) return value;
    if (arm == kArmCounting) return timer + (value - contactValue);
    return timer;
}

/* the late arm's why */
inline int ArmWhy(int arm)
{
    if (arm == kArmTimerDone) return kWhyTimerDone;
    if (arm == kArmNewSwing) return kWhyNewSwing;
    return kWhyNone;
}

/* ---- The copy's pause after a swing that got no result on its owner's game ----
   The owner's engine keeps its no-hit value (index 6) after a swing that touched another player's copy when no result came:
   the victim's game, which decides the blow, saw no contact and sent no HIT. The attacker's copy on that game touched nothing,
   so its engine keeps decide's no-contact value. The owner's SWING_END says what its swing met (hitwire.h end word, the same
   numbers as kOwnerEnd*). A copy's swing is followed from its start to its end; once it has ended and the owner's word has
   arrived, in either order, CopyDecide says whether to write and CopyAfter what. */
const int kOwnerEndUnknown   = 0;   /* the owner's game could not tell */
const int kOwnerEndResult    = 1;   /* the owner's swing got its result, or ran its own hit */
const int kOwnerEndNoResult  = 2;   /* it touched a character whose game decides the blow, and no result came by its end */
const int kOwnerEndUntouched = 3;   /* it touched nothing */

const int kCopyWrite         = 0;   /* write the no-hit value, less what the countdown already ran */
const int kCopyKeepContact   = 1;   /* the copy's own swing touched here: its engine set the timer from its own hit */
const int kCopyKeepResult    = 2;   /* the owner's swing got its result: the engines set the timer from that blow */
const int kCopyKeepUntouched = 3;   /* the owner's swing touched nothing either: both engines keep the no-contact value */
const int kCopyKeepUnknown   = 4;   /* the owner's game could not tell: the engine's value stands */
inline int CopyDecide(int ownerEnd, int copyContact)
{
    if (copyContact) return kCopyKeepContact;
    if (ownerEnd == kOwnerEndNoResult) return kCopyWrite;
    if (ownerEnd == kOwnerEndResult) return kCopyKeepResult;
    if (ownerEnd == kOwnerEndUntouched) return kCopyKeepUntouched;
    return kCopyKeepUnknown;
}

/* One copy swing: armed at its start, swinging once a fight-routine call leaves it in the attack (state 0), ended when a call
   leaves the attack (or it was left between two calls), done once its word was used. From its end until the decision the copy's
   timer is still followed, one fight-routine call at a time: the record says whether the countdown alone touched it since. The
   countdown is the fight routine's own step (1.0.65 0x60CC10: while the flag +0x144 is set, +0x148 less the call's frame time),
   so each followed call either leaves the timer as it was or lowers it by that call's frame time; the timer between two calls
   is left as the last call left it. Anything else is another writer, and following stops; it also stops when the flag clears
   (the pause ended), so a word that never comes costs at most one pause of reads. */
const int kCopyIdle = 0, kCopyArmed = 1, kCopySwinging = 2, kCopyEnded = 3, kCopyDone = 4;
const float kCopyEps = 0.0005f;   /* how far one countdown step may differ from the call's frame time (float rounding) */
struct CopySwing
{
    int   phase;
    int   contact;    /* 1: the copy's swing touched someone here */
    int   haveEnd;    /* 1: the owner's SWING_END word arrived */
    int   ownerEnd;   /* kOwnerEnd* */
    float held;       /* the timer in the attack, before the call that left it: where the countdown starts */
    float ran;        /* the countdown's time since the swing ended (its own steps only) */
    float last;       /* the timer after the last followed call */
    int   lastFlag;   /* the countdown flag after the last followed call */
    int   rewritten;  /* 1: something other than the countdown changed the timer or flag after the end: following stopped */
    int   over;       /* 1: the countdown flag went clear (the pause ended): following stopped */
};
inline void CopyStart(CopySwing* s)
{
    s->phase = kCopyArmed; s->contact = 0; s->haveEnd = 0; s->ownerEnd = kOwnerEndUnknown; s->held = 0.0f;
    s->ran = 0.0f; s->last = 0.0f; s->lastFlag = 0; s->rewritten = 0; s->over = 0;
}
inline int CopyRunning(const CopySwing& s) { return (s.phase == kCopyArmed || s.phase == kCopySwinging) ? 1 : 0; }
/* ended, and its timer still followed (no other writer seen, the countdown not over) */
inline int CopyFollowed(const CopySwing& s) { return (s.phase == kCopyEnded && !s.rewritten && !s.over) ? 1 : 0; }
/* the detour reads the copy's fight object around each fight-routine call: running or followed */
inline int CopyTracked(const CopySwing& s) { return (CopyRunning(s) || CopyFollowed(s)) ? 1 : 0; }
inline void CopyContact(CopySwing* s) { if (CopyRunning(*s)) s->contact = 1; }
/* one countdown step: d is nothing (0) or the call's frame time */
inline int CopyStepIsCountdown(float d, float frame) { return (d - frame <= kCopyEps && frame - d <= kCopyEps) ? 1 : 0; }
/* The swing has just ended: the record starts from the timer and flag after the call that ended it. What that call (and, when
   the attack was left between two calls, the gap before it) took off held must be nothing or one countdown step; anything else
   is another writer. A clear flag after it: no countdown of this swing runs (over). */
inline void CopySeed(CopySwing* s, float timerAfter, int flagAfter, float frame)
{
    s->phase = kCopyEnded;
    s->last = timerAfter;
    s->lastFlag = flagAfter;
    s->ran = s->held - timerAfter;
    s->rewritten = (s->ran == 0.0f || (s->ran > 0.0f && CopyStepIsCountdown(s->ran, frame))) ? 0 : 1;
    s->over = (!s->rewritten && flagAfter == 0) ? 1 : 0;
}
/* One fight-routine call while running: the state, timer and flag around it and the call's frame time. */
inline void CopyStep(CopySwing* s, int stBefore, float timerBefore, int stAfter, float timerAfter, int flagAfter, float frame)
{
    if (s->phase == kCopyArmed)
    {
        if (stAfter == 0) { s->phase = kCopySwinging; s->held = timerAfter; }
        else if (stBefore == 0) { s->held = timerBefore; CopySeed(s, timerAfter, flagAfter, frame); }   /* the whole swing inside the first call seen */
        return;
    }
    if (s->phase != kCopySwinging) return;
    if (stBefore != 0) { CopySeed(s, timerAfter, flagAfter, frame); return; }   /* left between two calls: held stays the last timer seen in the attack */
    if (stAfter == 0) { s->held = timerAfter; return; }
    s->held = timerBefore;
    CopySeed(s, timerAfter, flagAfter, frame);
}
/* One fight-routine call while followed: the timer and flag before it must be as the last call left them, and in the call the
   flag either clears (over), or stays set with the timer unchanged or lowered by the frame time (ran grows); else rewritten. */
inline void CopyFollow(CopySwing* s, float timerBefore, int flagBefore, float timerAfter, int flagAfter, float frame)
{
    if (!CopyFollowed(*s)) return;
    if (timerBefore != s->last || flagBefore != s->lastFlag) s->rewritten = 1;
    else if (flagAfter == 0) s->over = 1;
    else if (flagBefore != 0)
    {
        const float d = timerBefore - timerAfter;
        if (d == 0.0f) { /* nothing ran */ }
        else if (CopyStepIsCountdown(d, frame)) s->ran += d;
        else s->rewritten = 1;
    }
    else s->rewritten = 1;
    s->last = timerAfter;
    s->lastFlag = flagAfter;
}
/* The owner's word: 1 taken (the swing is armed, running or ended and has no word yet), 0 no swing here to take it. */
inline int CopyOwnerEnd(CopySwing* s, int ownerEnd)
{
    if (s->haveEnd || !(CopyRunning(*s) || s->phase == kCopyEnded)) return 0;
    s->haveEnd = 1;
    s->ownerEnd = ownerEnd;
    return 1;
}
inline int CopyDue(const CopySwing& s) { return (s.phase == kCopyEnded && s.haveEnd) ? 1 : 0; }

/* The copy's timer from the swing's record and the live timer and flag: the no-hit value less what the countdown ran since the
   swing ended (the record's ran, its own steps only), 0 when it ran longer (the pause ends at the engine's next countdown step).
   kWhyNone with *after / *ran set; else why nothing is written: newSwing (the timer is above held), otherWriter (the record saw
   another write, or the live timer is not the one the last followed call left), timerDone (the countdown flag went clear while
   followed, is clear now or the timer ran out - the engine clears the flag when the pause ends and its next decide step puts the
   timer back at held, so a clear flag is never this swing's pause), notShorter. */
inline int CopyAfter(const CopySwing& s, float timer, int flag, float noHit, float* after, float* ran)
{
    *after = timer;
    *ran = s.ran;
    if (timer > s.held) return kWhyNewSwing;
    if (s.rewritten) return kWhyOtherWriter;
    if (s.over) return kWhyTimerDone;
    if (timer - s.last > kCopyEps || s.last - timer > kCopyEps) return kWhyOtherWriter;
    if (flag == 0 || timer <= 0.0f) return kWhyTimerDone;
    if (!(noHit < s.held)) return kWhyNotShorter;
    const float a = noHit - s.ran;
    *after = (a > 0.0f) ? a : 0.0f;
    return kWhyNone;
}

}   /* namespace fightready */
