/* src/common/copybody.h - THE COPY BODY-REBUILD RULE: a replicated copy's body is never rebuilt while the engine is using it.
 *
 * ONE RULE, ownedmirror.h's: nothing in here reads engine memory, reads a global, calls the operating system or
 * includes a header, so the offline suite drives it directly.
 *
 * WHY. A body rebuild (AppearanceHuman::createBody / AppearanceAnimal::createBody - the only appearance step that
 * replaces the body entity) destroys the old body's scene node and calls set-body with no body, which deletes the
 * get-up blend and clears the animation object's body fields. Two engine states make that fatal:
 *  - RAGDOLL (AnimationClass +0x2E8 set): the animal createBody destroys the node before set-body, whose ragdoll kill
 *    then reads that node.
 *  - INSIDE THE ENGINE'S RAGDOLL PASS (ThreadSafeRagdollUpdates, main thread): the get-up blend's last update calls the
 *    animation update, which calls AppearanceBase::update, which runs a queued createBody right there - set-body deletes
 *    the blend that is still running and the blend then reads the old body's destroyed node.
 * The guard returns without calling createBody: the pending byte (+0x143) stays set and the engine calls again on its
 * next ordinary update, so a held rebuild is retried until it runs and is never lost. A rebuild between frames is safe:
 * set-body cleans the blend up and the pass then skips the character.
 *
 * A HOLD OUTLIVES AN OWNERSHIP CHANGE. A copy held lying in ragdoll that becomes this game's own (an area hand-over) is still
 * lying in ragdoll with the rebuild the mod queued while it was a copy, so its hold goes on (heldBefore); so does a rebuild the
 * mod queued that has not run yet, hold or no hold (the caller's mark). A rebuild the engine
 * queues on a body this game already owned, with no hold carried over, is left to the engine as in a game without the mod.
 *
 * INPUTS (0 / nonzero): onMain = the call is on the main thread; replicated = a character the mod replicates (it has a
 * uid), whoever owns it; owned = this game owns that uid now; heldBefore = this uid's rebuild is already being held (its
 * hold began while it was a copy, or the mod queued it and it has not run yet, or the caller is the mod's own look change); entity = it has a body (app +0xD8); ragdoll = the ragdoll object exists; inPass = the main
 * thread is inside the engine's ragdoll pass now; fault = a read faulted. Off the main thread it never waits; on the main
 * thread a fault waits.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_COPYBODY_H
#define COOP_COMMON_COPYBODY_H

namespace coopbody {

enum { kCopyBodyRun = 0, kCopyBodyWait = 1 };

/* heldBefore: this uid's rebuild is already being held (its hold began while it was a copy, or the caller is the mod's own look
   change). A body this game owns waits only then - the rebuild the mod queued while it was a copy outlives an ownership change;
   a rebuild the engine queues on its own character is left to the engine, as in a game without the mod. */
inline int CopyBodyDecide(int onMain, int replicated, int owned, int heldBefore, int entity, int ragdoll, int inPass, int fault)
{
    if (onMain == 0) return kCopyBodyRun;
    if (owned != 0 && heldBefore == 0) return kCopyBodyRun;          /* this game's own, no hold carried over */
    if (fault != 0) return kCopyBodyWait;
    if (replicated == 0 || entity == 0) return kCopyBodyRun;         /* not replicated, or no old body: nothing is destroyed */
    return (ragdoll != 0 || inPass != 0) ? kCopyBodyWait : kCopyBodyRun;
}

}   /* namespace coopbody */

#endif
