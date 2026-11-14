/* src/common/physqueued.h - T-1046 fix 1: IS A PHYSICS PIECE ALREADY QUEUED FOR DELETE?
 *
 * Pure: no engine memory, no globals, no OS call, no include - the offline suite sweeps it. The plugin (combat.cpp,
 * the ScythePhysicsT first-ready detour) calls it inside its guarded read of the engine's two lists.
 *
 * The physics part of a character's hair / clothing piece (a ScythePhysicsT) is handed to the physics holder's delete
 * queue when its piece is torn down. The holder keeps two lists of objects waiting to be deleted: the main list (new
 * entries this frame) and the delete list (what the physics thread deletes next; once a frame the main list is appended
 * to it). Every entry is deleted once, so an object on either list is already on its way out. If its own one-time setup
 * ("first ready", engine vtable slot 15) runs after that, it finds its owner gone and queues the object a second time ->
 * the object is deleted twice (T2301, both games). The fix asks the lists first: an object already on one of them gets
 * no first-ready setup.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_PHYSQUEUED_H
#define COOP_COMMON_PHYSQUEUED_H

namespace physqueued {

/* Whether obj is among a list's live entries data[0 .. count).
   1 = it is there, 0 = it is not, -1 = the list cannot be searched (count above maxScan - an unreadable or implausible
   count - or no data behind a non-zero count): the caller then treats the answer as unknown and keeps today's behaviour.
   Only the live entries are searched: slots at or past count are old entries the engine leaves in place. */
inline int ListHolds(const unsigned long long* data, unsigned int count, unsigned long long obj, unsigned int maxScan)
{
    if (count == 0) return 0;
    if (count > maxScan || data == 0) return -1;
    for (unsigned int i = 0; i < count; ++i)
        if (data[i] == obj) return 1;
    return 0;
}

/* The decision: 1 = skip the first-ready setup (the object is already on the main list or the delete list), 0 = run it.
   onMain / onDel are ListHolds answers; an unknown answer (-1) never skips - the caller runs the setup, as today. */
inline int SkipFirstReady(int onMain, int onDel)
{
    if (onMain < 0 || onDel < 0) return 0;
    return (onMain > 0 || onDel > 0) ? 1 : 0;
}

}   /* namespace physqueued */

#endif
