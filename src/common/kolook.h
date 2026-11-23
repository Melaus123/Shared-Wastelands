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

/* T-1244 (design A, F-T1244-DOWNHOLD): a copy's kit (its worn items and its whole inventory, the owner's list) is the owner's
   inventory, and the owner's item moves land on a knocked-out copy at once (items.cpp ApplyItemMove has no down test). Putting
   the kit on queues only the re-attach step (+0x141), never the body rebuild (+0x143) the down / owner-says-knocked-out holds
   exist for (F939). So a LIVE HUMAN copy's kit is no longer held because the copy is down or its owner says knocked out; only
   its look waits. dead: 0 alive, nonzero dead or unreadable. downed: 0 standing, nonzero down or unreadable. human: 1 human,
   anything else not (dead animals keep waiting by the owner's decision; live animals keep today's rule with them). A copy
   the owner says dead keeps today's rule. The caller keeps every other hold (built, settled, KitApplyBusy) and adds the
   body-rebuild guard checks. */
inline bool KitPassesDownHold(int dead, int downed, bool ownerKo, bool ownerDead, int human)
{
    return dead == 0 && !ownerDead && human == 1 && (downed != 0 || ownerKo);
}

/* The same copy's kit while a look for it is queued: it no longer waits for that look only when LookGate holds the look now
   (kLookWait) and the copy passes KitPassesDownHold - the look is held for down / owner-says-knocked-out alone. A look LookGate
   would apply now (the first-look exception on a held knockdown) goes first, as before, and the kit after it. */
inline bool KitPassesLookHold(int dead, int downed, bool ownerKo, bool ownerDead, int human, bool firstLook, bool knockHeld)
{
    return KitPassesDownHold(dead, downed, ownerKo, ownerDead, human)
        && LookGate(dead, downed, ownerKo, ownerDead, false, firstLook, knockHeld) == kLookWait;
}

/* T-601 D1: a wake-up clock held back with a parked knockdown (spawn.cpp ProneParkTick) goes on when the hold ends, less the
   time it waited: koTimer - waitedMs / 1000, never below 0. A value outside the range ApplyLatch writes (0 or below, 3600 and
   above, or not a number: no clock running) comes back unchanged, so ApplyLatch treats it exactly as the owner sent it. */
inline float HeldKoTimerLeft(float koTimer, unsigned long waitedMs)
{
    if (!(koTimer > 0.0f) || !(koTimer < 3600.0f)) return koTimer;
    const float left = koTimer - (float)waitedMs / 1000.0f;
    return left > 0.0f ? left : 0.0f;
}

/* The owner's knock-out pose (4) onto a copy whose STATE carries the unconscious flag or wake-up clock (latchDowns).
   The copy's engine keeps pose 4 only on a body whose ragdoll bit is on (posture update 0x5CF620, build/decomp_5cf620.txt:
   144-170); on any other body it puts the pose straight back to 2 (crippled) or 0 at its next posture update - a stand-up
   while the owner lies knocked out (T-540, F1980 / F1981). The same STATE's flag or clock makes the copy's own engine knock
   it out itself, ragdoll first. So the pose is written only onto a body whose ragdoll bit is on; while it is off the pose is
   left to the copy's own engine, the first time and again at every later STATE that still finds the bit off.
   ragdollBit: the ENGINE's own bit, as 0x5CF620 tests it (Character +0x448 -> +0x2F8 bit 0, 0x7D0840): 1 on, 0 off,
   -1 unreadable (unreadable writes, as before). NOT the pose (a crippled copy is pose 2 standing, T3019) and NOT inRagdoll
   0x7D08A0 (also yes when carried or from +0x59 / +0x5A, F1982). leftBefore: already left once this knockdown.
   T-540 (owner 1873 a): before, a crippled copy (pose 2 counted 'limp') and the second STATE (leftBefore) were written bare. */
/* The engine's ragdoll bit as 0x7D0840 tests it: AnimationClass +0x2F8 non-zero with bit 0. raw = the 32-bit field read, or -1
   when it could not be read. 1 on, 0 off, -1 unreadable (KoPoseRoad's ragdollBit). */
inline int KoRagdollBit(long long raw)
{
    if (raw < 0) return -1;
    const unsigned int v = (unsigned int)raw;
    return (v != 0 && (v & 1) != 0) ? 1 : 0;
}
enum { kKoPoseWrite = 0, kKoPoseLeaveFirst = 1, kKoPoseLeaveAgain = 2 };
inline int KoPoseRoad(int prone, bool latchDowns, int ragdollBit, bool leftBefore)
{
    if (prone != 4 || !latchDowns || ragdollBit != 0) return kKoPoseWrite;
    return leftBefore ? kKoPoseLeaveAgain : kKoPoseLeaveFirst;
}

/* T-540 (T3024, LOG ONLY - decides nothing): the [M4] KO pose line names every decision KoPoseRoad makes on a copy's knock-out
   pose (4), one line per kind per knockdown per copy, the first write included (T3023 F1999: a first write wrote no line, so the
   bit read could not be told from 'unreadable'). Kinds: a write with nothing left before, a write after the pose was left, left
   to the copy's engine the first time, left again. */
enum { kKoLogWrite = 0, kKoLogWriteAfterLeft = 1, kKoLogLeft = 2, kKoLogLeftAgain = 3 };
inline int KoPoseLogKind(int road, bool leftBefore)
{
    if (road == kKoPoseLeaveFirst) return kKoLogLeft;
    if (road == kKoPoseLeaveAgain) return kKoLogLeftAgain;
    return leftBefore ? kKoLogWriteAfterLeft : kKoLogWrite;
}
inline const char* KoPoseLogKindText(int kind)
{
    switch (kind)
    {
    case kKoLogWrite:          return "written";
    case kKoLogWriteAfterLeft: return "written after left";
    case kKoLogLeft:           return "left to the copy's engine";
    case kKoLogLeftAgain:      return "left again";
    default:                   return "?";
    }
}
/* 1 = the first line of this kind this knockdown (its bit is set in *seen); 0 = that kind was already written (or no such kind).
   *seen starts at 0 for each knockdown of each copy. */
inline int KoPoseLogFirst(unsigned int* seen, int kind)
{
    if (kind < kKoLogWrite || kind > kKoLogLeftAgain) return 0;
    const unsigned int m = 1u << kind;
    if ((*seen & m) != 0) return 0;
    *seen |= m;
    return 1;
}
/* the ragBit field as the line writes it: 1, 0, or '-1(unreadable)' */
inline const char* KoRagBitText(int bit)
{
    return bit == 1 ? "1" : bit == 0 ? "0" : "-1(unreadable)";
}
/* which koPoseRoadWrite[bit1,bitUnreadable] slot a decision counts in: 0 = a pose-4 write onto a bit-on body, 1 = a pose-4 write
   with the bit unreadable, -1 = neither (left, another pose, or a write for want of a latch on a bit-off body) */
inline int KoPoseWriteSlot(int prone, int road, int ragdollBit)
{
    if (prone != 4 || road != kKoPoseWrite) return -1;
    if (ragdollBit == 1) return 0;
    if (ragdollBit < 0) return 1;
    return -1;
}

}   /* namespace coopkolook */

#endif
