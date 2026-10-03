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
 * INPUTS (0 / nonzero): onMain = the call is on the main thread; copy = a replicated copy this game does not own;
 * entity = it has a body (app +0xD8); ragdoll = the ragdoll object exists; inPass = the main thread is inside the
 * engine's ragdoll pass now; fault = a read faulted. Off the main thread it never waits; on the main thread a fault waits.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_COPYBODY_H
#define COOP_COMMON_COPYBODY_H

namespace coopbody {

enum { kCopyBodyRun = 0, kCopyBodyWait = 1 };

inline int CopyBodyDecide(int onMain, int copy, int entity, int ragdoll, int inPass, int fault)
{
    if (onMain == 0) return kCopyBodyRun;
    if (fault != 0) return kCopyBodyWait;
    if (copy == 0 || entity == 0) return kCopyBodyRun;   /* not a copy, or no old body: nothing is destroyed */
    return (ragdoll != 0 || inPass != 0) ? kCopyBodyWait : kCopyBodyRun;
}

}   /* namespace coopbody */

#endif
