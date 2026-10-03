/* src/common/getupcrawl.h - T-178 crawl1 (H050, 2026-09-28): WHEN A COPY MAY GET UP FROM ITS RAGDOLL, AND WHICH PRONE
 * STATES THE MOVE DRIVE MAY WALK.
 *
 * ONE RULE, ownedmirror.h's: nothing in here reads engine memory, reads a global, calls the operating system or includes
 * a header, so the offline suite drives it directly. spawn.cpp (G1: NoteGetupState, GetupLocalReady) and replicate.cpp
 * (the direct drive, PathEligible, the reconcile, SnapPuppetToAuthority) call these.
 *
 * WHY (T-178, T527/T531/T533): a crippled character that gets up and CRAWLS on its owner's game (prone 2) stayed a
 * frozen ragdoll on the other game. Only the engine's own get-up task (orders 0x34 'Waking up' when crippled, 0x41
 * 'Getting up') ends a whole-body ragdoll; the copy's own periodic update queues it, and the G1 pulse lets the copy's
 * decision pass adopt it - but G1 refused every crawler (it wanted the copy's prone 0), and MOVE refused prone != 0.
 * Crawling uses the same CharMovement update as walking, so a crawler that is not a ragdoll is driven like a walker.
 *
 *   owner up from the ragdoll = the owner's own inRagdoll (the STATE bit) 0, its prone 0 (standing) or 2 (crawling),
 *                               its unconscious latch 0 and no wake-up clock running, not carried, not posed.
 *   copy ready                = the copy's prone EQUALS the owner's and is 0 or 2, and the copy's own unconscious
 *                               (medical +0x161) and dead (+0x164) bytes are 0 - what the engine reads before it queues
 *                               the get-up order for THIS character.
 *   MOVE drives               = prone 0, or prone 2 and not a ragdoll. Prone 1 / 3 / 4 and a ragdolled crawler keep
 *                               the downed paths (the prone snap, no path, no reconcile, no placement).
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_GETUPCRAWL_H
#define COOP_COMMON_GETUPCRAWL_H

namespace coopgetup {

/* MSG_STATE's latchBits word (statewire.h): bits 0-5 are the owner's medical latch (medical.h kLatch*), which ApplyLatch
   reads bit by bit; bit 6 (session protocol 86) is the OWNER's own inRagdoll (0x7D08A0), set when it reads 1 or cannot be
   read - "up" needs positive evidence. ApplyLatch never reads it, so the copy's medical fields are unchanged by it. */
const unsigned int kStateOwnerRagdollBit = 1u << 6;
const unsigned int kStateLatchMask       = 0x3Fu;   /* the six medical latch bits */

const int kProneStanding = 0;
const int kProneCrawling = 2;   /* crippled: the engine's own crawl, the same CharMovement path as walking */

/* A prone state a get-up ends in: standing, or crawling. */
inline bool GetupProne(int prone)
{
    return prone == kProneStanding || prone == kProneCrawling;
}

/* The owner's STATE says it is up from its ragdoll. koRunning = its wake-up clock is running (koTimer > 0). */
inline bool OwnerUpFromRagdoll(int ownerProne, bool ownerRagdoll, bool ownerUnconscious, bool koRunning,
                               bool carried, bool posed)
{
    return GetupProne(ownerProne) && !ownerRagdoll && !ownerUnconscious && !koRunning && !carried && !posed;
}

/* The COPY itself can get up into the owner's prone state. The bytes are the copy's own medical unconscious / dead. */
inline bool CopyReadyToGetUp(int copyProne, int ownerProne, unsigned char copyUnconscious, unsigned char copyDead)
{
    return copyProne == ownerProne && GetupProne(copyProne) && copyUnconscious == 0 && copyDead == 0;
}

/* MOVE may drive a copy in this prone state (walk, path, reconcile, place). ragdoll = the copy's inRagdoll; for prone 0
   the callers keep their own ragdoll test, as before. */
inline bool MoveDrivesProne(int prone, bool ragdoll)
{
    return prone == kProneStanding || (prone == kProneCrawling && !ragdoll);
}

/* The goal a spent get-up pulse may leave the copy on: the engine's task names for 0x41 and 0x34
   (.modding/03-systems/taskdata-table.md: 65 'Getting up', 52 'Waking up'). */
inline bool GetupGoalName(const char* goal)
{
    if (goal == 0) return false;
    const char* a = "Getting up";
    const char* b = "Waking up";
    int i = 0;
    bool ea = true, eb = true;
    for (;; ++i)
    {
        if (ea && goal[i] != a[i]) ea = false;
        if (eb && goal[i] != b[i]) eb = false;
        if (!ea && !eb) return false;
        if (goal[i] == 0) return true;
    }
}

}   /* namespace coopgetup */

#endif
