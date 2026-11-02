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

}   /* namespace fightready */
