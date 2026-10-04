/* src/common/kolook.h - A COPY MET WHILE ITS OWNER SAYS KNOCKED OUT TAKES ITS OWNER'S LOOKS BEFORE IT GOES DOWN.
 *
 * ONE RULE, ownedmirror.h's: nothing in here reads engine memory, reads a global, calls the operating system or includes a
 * header, so the offline suite drives it directly.
 *
 * WHY. Applying a copy's appearance (or its worn items) rebuilds its body. A rebuild on a body that is lying down or limp
 * reads a scene node the rebuild has already destroyed (a crash), and a rebuild on a corpse turns its ragdoll off (the
 * corpse jumps). So a copy's looks wait while it lies down, while it is dead, and while its owner says knocked out or dead.
 * A copy that arrives while its owner is knocked out still STANDS for a moment: its knockdown is held back while its looks
 * are pending (spawn.cpp KnockdownMustWait). In that moment - alive, standing, the hold live - its FIRST look goes through,
 * and its worn items after it; the same hold then keeps the knockdown back while the rebuild those applies queue is in
 * flight (a queued rebuild always holds it), so the copy goes down dressed as its owner. A copy that is already lying keeps
 * waiting (it is dressed once it stands); a dead copy never takes a rebuild.
 *
 * kFirstLookWaitMs bounds a knockdown held for the copy's looks (counted from the first held knockdown). 5 s: the hold
 * starts at the first knocked-out STATE, which can come before the body is built and settled, so its bound covers the build
 * as well as the apply. (A SPAWN death held for the first look has its own 3 s in medical.cpp: its clock starts once the
 * body is built, so it covers the apply only.)
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_KOLOOK_H
#define COOP_COMMON_KOLOOK_H

namespace coopkolook {

const unsigned long kFirstLookWaitMs = 5000;

enum { kLookApply = 0, kLookWait = 1, kLookNever = 2 };

/* The knockdown hold for a copy is live: it exists, it has not given up, and it is still inside the bound (the next ask of
   the hold would keep the knockdown back for the looks). */
inline bool KnockHoldLive(bool present, bool gaveUp, unsigned long elapsedMs)
{
    return present && !gaveUp && elapsedMs < kFirstLookWaitMs;
}

/* The look gate. dead: 0 alive, nonzero dead or unreadable. downed: 0 standing, nonzero lying down, limp, about to be, or
   unreadable. ownerKo / ownerDead: the owner's last word. deadLookWanted: a copy its SPAWN said dead still waits for its
   first look. firstLook: no look applied to this copy yet. knockHeld: KnockHoldLive for this copy.
   kLookNever = a dead copy (its entry stays and is never applied); kLookWait = not now, asked again next tick. */
inline int LookGate(int dead, int downed, bool ownerKo, bool ownerDead, bool deadLookWanted, bool firstLook, bool knockHeld)
{
    if (dead != 0) return kLookNever;
    if (downed != 0) return kLookWait;
    if (ownerDead && !deadLookWanted) return kLookWait;
    if (ownerKo && !(firstLook && knockHeld)) return kLookWait;
    return kLookApply;
}

/* LookGate let this look through only because the knockdown is held (the owner says knocked out, not dead). A look the
   owner's word "dead" lets through goes by the dead-arrival rule (deadLookWanted) and is not this exception. */
inline bool LookThroughKoHold(int dead, int downed, bool ownerKo, bool ownerDead, bool deadLookWanted, bool firstLook,
                              bool knockHeld)
{
    return ownerKo && !ownerDead
        && LookGate(dead, downed, ownerKo, ownerDead, deadLookWanted, firstLook, knockHeld) == kLookApply;
}

/* The clothing gate's matching exception: an alive, standing copy its owner says knocked out (and not dead), whose first
   look went through LookThroughKoHold, takes its worn items while the same hold is live. Every other owner-KO copy waits. */
inline bool ClothingThroughKoHold(int dead, int downed, bool ownerKo, bool ownerDead, bool lookWentThrough, bool knockHeld)
{
    return dead == 0 && downed == 0 && ownerKo && !ownerDead && lookWentThrough && knockHeld;
}

/* That copy's worn items, ready to apply, missed the exception: its first look went through it, the copy is alive and its
   owner still says knocked out (not dead), but the hold is no longer live or the copy is already down - the items wait while
   the owner says knocked out, like any owner-KO copy's (counted lookKoArrival clothLate, so a run tells it from a pass). */
inline bool ClothingMissedKoHold(int dead, int downed, bool ownerKo, bool ownerDead, bool lookWentThrough, bool knockHeld)
{
    return lookWentThrough && dead == 0 && ownerKo && !ownerDead
        && !ClothingThroughKoHold(dead, downed, ownerKo, ownerDead, lookWentThrough, knockHeld);
}

}   /* namespace coopkolook */

#endif
