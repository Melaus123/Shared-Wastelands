/* src/common/goalrefill.h - T-918 (fix attempt 1, design A, 2026-11-07; fold 1): A CHARACTER THIS GAME TAKES OVER GETS ITS DEFAULT
 * GOAL SET BACK, SO ITS OWN AI CAN FIGHT BACK.
 *
 * ONE RULE, ownedmirror.h's: nothing in here reads engine memory, reads a global, calls the operating system or includes a header,
 * so the offline suite drives it directly. replicate.cpp calls these: QueueAdd at the take (UnpuppetForOwnership, which every take
 * road passes through), Decide and AfterFill at the K2 safe point (combat.cpp detour_tsRagdollUpdates -> GoalRefillSafePointDrain).
 *
 * WHY (run T2159, Confirmed): every character this game took over had an EMPTY default goal set. While it was a copy the mod emptied
 * it on purpose (replicate.cpp ClearAttackOrder -> the engine's dropGoals, which clears all five priority lists - T075's protection:
 * a copy must not decide for itself), and nothing filled it again when the copy became this game's own. So its tier-3 'Self
 * preservation' goal (TaskType 32) could never start and it never fought back. When 32 is missing, the engine's setupAI (1.0.65
 * 0x6210E0) fills the set as the engine fills a new character's.
 *
 * WHERE (fold 1, review M1): setupAI ends with AI vt+0x30 (0x5A4050, updateStateBroadcast), which otherwise only the AI worker runs,
 * every frame, ungated (F401) - so the take QUEUES the uid and the fill runs at the K2 safe point, where the worker is paused. There
 * the uid is checked live again: still this game's own, the engine still knows it, not dead, its AI and order system readable.
 *
 * Decide, per queued uid at a safe point:
 *   drop    no longer this game's own, gone, dead, or no AI / order system - nothing written, the entry leaves the queue.
 *   keep    goal 32 is there - never filled twice (no duplicate set). Leaves the queue.
 *   refill  goal 32 is missing - run setupAI once, then AfterFill.
 *   retry   hasTaskOfType faulted - ask again at the next safe point, while tries are left.
 *   fail    hasTaskOfType faulted on the last try.
 * AfterFill: done (32 is there now) / fail (setupAI ran and 32 is still missing - never run twice) / retry (setupAI faulted or the
 * after-read faulted, tries left) / fail (no tries left).
 * tries = the attempts already made for this uid (0 on the first); maxTries bounds them (one take is asked once, counted once).
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef SW_COMMON_GOALREFILL_H
#define SW_COMMON_GOALREFILL_H

namespace goalrefill {

enum Action { kDrop = 0, kKeep = 1, kRefill = 2, kRetry = 3, kFail = 4, kDone = 5 };
enum QueueAnswer { kQueueAdd = 0, kQueueHave = 1, kQueueFull = 2 };

const int kSelfPreservationType = 32;   /* TaskType 0x20 'Self preservation', added at priority 3 by setupAI */
const int kMaxTries = 3;                /* safe points one take may use (one per frame) */
const unsigned int kQueueCap = 1024;   /* queued uids at once (T2159's B: 381 copies had dropGoals) */

/* At the take: present = the uid is already queued; n = the queue's size now. A uid is queued once. */
inline int QueueAdd(bool present, unsigned int n, unsigned int cap)
{
    if (present) return kQueueHave;
    if (n >= cap) return kQueueFull;
    return kQueueAdd;
}

/* At a safe point. mine: this game owns the uid now. live: the engine still knows the character, it has not died, and its AI and
   order system read plausible. has32: hasTaskOfType(orders, 32) - 1 yes, 0 no, -1 the call faulted (only read when mine && live). */
inline int Decide(bool mine, bool live, int has32, int tries, int maxTries)
{
    if (!mine || !live) return kDrop;
    if (has32 > 0) return kKeep;
    if (has32 == 0) return kRefill;
    return (tries + 1 < maxTries) ? kRetry : kFail;
}

/* After setupAI ran (ran = it returned without a fault). has32After: hasTaskOfType after the fill (1 / 0 / -1 faulted). */
inline int AfterFill(bool ran, int has32After, int tries, int maxTries)
{
    if (ran && has32After == 1) return kDone;
    if (ran && has32After == 0) return kFail;   /* it ran and 32 is still missing: never run again (each run adds its goals anew) */
    return (tries + 1 < maxTries) ? kRetry : kFail;   /* setupAI faulted / unbound, or the after-read faulted: the next safe point
                                                         asks hasTaskOfType first, so a set that has 32 is kept, never filled twice */
}

inline const char* ActionName(int a)
{
    return a == kDrop ? "drop" : a == kKeep ? "keep" : a == kRefill ? "refill" : a == kRetry ? "retry" : a == kFail ? "fail"
         : a == kDone ? "done" : "?";
}

}   /* namespace goalrefill */

#endif
