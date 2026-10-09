/* src/common/corpsedecay.h - A COPY'S DEAD BODY NEVER ROTS ON ITS OWN; ITS OWNER'S DESPAWN IS WHAT REMOVES IT.
 * The pure decision only: no engine memory, no Windows, no globals. The plugin (medical.cpp CorpseDecayTick) and the offline
 * suite (coop-test) compile the same function. C++03 (VS2010 v100).
 *
 * WHAT THE GAME DOES. Character::_NV_periodicUpdate (0x5CC300 in 1.0.65, 0x5CCD90 in 1.0.68) keeps a dead body's rot start
 * as a 4-byte float at Character+0xD0, in in-game hours: 0 means "not started" and the update stores the clock's `now` there.
 * When the body is neither carried (+0x3D4) nor in a bed or cage (+0x2F8) and now - start is over kDecayHours, the update
 * takes the body off the death list and destroys it with the reason 'corpse decayed'. While carried or bedded it pulls the
 * start forward so the body is never more than 6 hours into its rot.
 *
 * WHAT THE MOD DOES WITH IT. A body this game shows for another game (a copy) gets the same kind of write the game makes
 * for a carried body: while it is in this game's copy-control list, its start is kept so that now - start never exceeds
 * kDecayHours - kCopyMarginHours, on every frame (a forward clock jump cannot outrun it). Every route that ends a copy - its
 * owner's DESPAWN or UNLOAD, the owner's departure (PLAYER_GONE, the session peer gone, the orphan rule) - takes it off that
 * list first, so a held body goes when its owner's body rots or its owner leaves. A link outage does not release the hold:
 * the copy is kept through the outage like every other copy of that owner.
 */
#ifndef COOP_COMMON_CORPSEDECAY_H
#define COOP_COMMON_CORPSEDECAY_H

namespace corpsedecay {

/* The game's corpse rot limit in in-game hours: the float read by the comparison in _NV_periodicUpdate, at 0x16B2BFC in
   1.0.65 and 0x16B3BFC in 1.0.68 (both read 12.0). */
const float kDecayHours = 12.0f;
/* How far short of the limit a copy's body is held, in in-game hours. */
const float kCopyMarginHours = 0.25f;

/* 1 and *newStart = now - (decayHours - marginHours) when the body is another game's (isCopy), the body is dead (deadState 1; 0 alive, -1 unreadable), its rot has started (start != 0), the held age is positive and
   the body is older than it (now - start > the held age). Otherwise 0 and *newStart is not written: an own body, a living or unreadable body, a rot not started, a body under the held age, a clock gone backwards, a margin that
   leaves no held age. */
inline int CopyDecayPin(int isCopy, int deadState, float start, double now, float decayHours, float marginHours,
                        float* newStart)
{
    if (isCopy == 0 || deadState != 1 || start == 0.0f) return 0;
    const double limit = (double)decayHours - (double)marginHours;
    if (!(limit > 0.0)) return 0;
    if (!(now - (double)start > limit)) return 0;
    if (newStart != 0) *newStart = (float)(now - limit);
    return 1;
}

}   /* namespace corpsedecay */

#endif
