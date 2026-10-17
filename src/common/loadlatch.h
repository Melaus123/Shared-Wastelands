/* src/common/loadlatch.h - THE LOAD LATCH ARITHMETIC, AND NOTHING ELSE (P7y, folding F610).
 *
 * ONE RULE, the same one clockmath.h keeps: nothing in here reads engine memory, reads a global, calls the
 * operating system or includes any header at all. It is a pure function of its arguments, so the offline
 * suite can sweep it instead of believing a comment.
 *
 * WHY IT EXISTS. StoreWorldLoadBegan() is called TWICE on one save load - once from detour_resetGame and
 * once from the detour_worldTeardown that orig_resetGame nests inside itself - and ONCE on a plain quit to
 * the title. The world generation must rise exactly once in both cases. P7w used the LOAD DEPTH as the
 * latch and that was wrong: detour_loadGame already holds the depth across the whole of
 * SaveManager::loadGame, so on the load path the depth is 2 at the first call and P7w's "<= 1" skipped the
 * only bump there was (F610, a regression against the deployed E94D732B0795). The latch is the LOAD.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_LOADLATCH_H
#define COOP_COMMON_LOADLATCH_H

namespace cooplatch {

/* SHOULD THIS CALL BUMP THE WORLD GENERATION?
     loadLatchRaised        - 1 while detour_resetGame holds the load open (raised beside its LoadDepthRaise,
                              cleared in its __finally), 0 otherwise
     alreadyBumpedThisLoad  - 1 when a call inside THIS load has already bumped
   Returns 1 = bump, 0 = book it as a nested call.
     latch 0                -> a standalone teardown (quit to the title). It bumps. There is nothing to nest
                               inside and no later call will follow it.
     latch 1, not yet bumped-> the FIRST call of this load. It bumps.
     latch 1, already bumped-> the nested detour_worldTeardown inside orig_resetGame. It does not.
   The caller does the test-and-set atomically; this function is the decision and holds no state. */
inline int WorldGenBumpDecide(int loadLatchRaised, int alreadyBumpedThisLoad)
{
    if (loadLatchRaised == 0) return 1;
    return (alreadyBumpedThisLoad == 0) ? 1 : 0;
}

}   /* namespace cooplatch */

#endif
