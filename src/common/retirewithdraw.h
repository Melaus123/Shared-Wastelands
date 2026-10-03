/* src/common/retirewithdraw.h - WHAT HAPPENS TO ONE RETIRED UID, AND NOTHING ELSE (P8m, H-ghost).
 *
 * ONE RULE, the same one clockmath.h and loadlatch.h keep (p97-rw): nothing in here reads engine
 * memory, reads a global, calls the operating system or includes any header at all.  It is a pure
 * function of its arguments, so the offline suite can drive the whole sequence instead of believing a
 * comment.
 *
 * WHY IT EXISTS.  A character the holder's engine takes away (a region streams out) is RETIRED by P034:
 * the mirror slot is marked `dead` and nothing is ever dereferenced through it again.  Retirement sent
 * NOTHING on the wire, and the one pass that withdraws an announcement - AnnouncePass - begins by
 * looking the uid up through FindSpawned, which is BUILT to answer 0 for a retired object.  So the pass
 * whose job is "tell the peer to drop this character" skipped exactly the characters that needed
 * dropping: T236d, game A, retired=205, unloadsSent=1, zero `-> UNLOAD` lines in the whole run, and
 * seven puppets still standing on game B 250 s after A's engine had removed them.
 *
 * THE REPAIR IS A REGISTRY, NOT A SCAN.  Retirement PUSHES the uid into a pending set; the announce
 * cadence drains it.  The drain never asks whether the object can be found - the object is gone, that
 * is the whole premise - so the fix cannot be undone by the lookup that caused the hole.
 *
 * RECURRENCE-COVERED.  A pending uid stays pending until the send ACTUALLY happens.  A link that is
 * down defers it and counts the deferral; it does not consume it.  Only three things end a pending
 * entry: the send succeeded, the peer was never told about this uid in the first place, or the
 * character came back (restore), which CANCELS the withdrawal.  Retire-then-restore inside one tick is
 * therefore two steps over the same entry and the FINAL state is what the drain sees - nothing is sent.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_RETIREWITHDRAW_H
#define COOP_COMMON_RETIREWITHDRAW_H

namespace cooprw {

/* THE STATE OF ONE UID in the pending-withdraw set.  kAbsent means "not in the set" - it is a state
   rather than a hole so that one step function can express entering and leaving. */
enum { kAbsent = 0, kPending = 1, kWithdrawn = 2 };

/* WHAT HAPPENED TO IT. */
enum {
    kEvRetire       = 1,   /* P034 retired the mirror: the holder's engine no longer has this character */
    kEvSent         = 2,   /* the UNLOAD went out on the wire */
    kEvSendFailed   = 3,   /* the transport declined - link down.  NOT a resolution */
    kEvNotAnnounced = 4,   /* the peer was never told about this uid, so there is nothing to withdraw */
    kEvRestore      = 5    /* the handle resolves again: the character came back */
};

/* WHICH COUNTER THE CALLER MUST MOVE.  Named rather than returned as a bool pair, because
   `deferredLinkDown` is an ATTEMPT count (one entry can produce many) while the other three are
   RESOLUTIONS (one entry produces exactly one), and a caller that cannot tell them apart will write an
   identity that does not close. */
enum {
    kCountNone               = 0,
    kCountSent               = 1,
    kCountDeferredLinkDown   = 2,
    kCountCancelledByRestore = 3,
    kCountNotAnnounced       = 4
};

/* ONE TRANSITION.  Returns the new state; *countOut names the counter to move (kCountNone for most).
   Total over every (state, event) pair - there is no unhandled combination and no assertion to trip. */
inline int RetireWithdrawStep(int state, int ev, int* countOut)
{
    int count = kCountNone;
    int next  = state;

    if (ev == kEvRetire)
    {
        /* From ANY state, including kWithdrawn: a character that came back, was re-announced and has
           now gone again is a fresh withdrawal owed to the peer. */
        next = kPending;
    }
    else if (ev == kEvSent)
    {
        if (state == kPending) { next = kWithdrawn; count = kCountSent; }
    }
    else if (ev == kEvSendFailed)
    {
        /* THE ENTRY SURVIVES.  This is the whole of "recurrence-covered": a deferral is counted and
           the uid is still owed, so the next pass tries again. */
        if (state == kPending) count = kCountDeferredLinkDown;
    }
    else if (ev == kEvNotAnnounced)
    {
        if (state == kPending) { next = kWithdrawn; count = kCountNotAnnounced; }
    }
    else if (ev == kEvRestore)
    {
        /* Pending: the withdrawal is CANCELLED before it ever reached the wire.
           Withdrawn: the UNLOAD is already out, and the existing announce pass re-announces the
           character with a SPAWN when it is held and loaded again - so the entry is simply forgotten
           here and nothing is counted as a cancellation, because nothing was cancelled. */
        if (state == kPending) count = kCountCancelledByRestore;
        next = kAbsent;
    }

    if (countOut != 0) *countOut = count;
    return next;
}

/* THE DRAIN'S QUESTION, asked of the state and of nothing else.  It deliberately does NOT take an
   object, a pointer or a handle: the object is gone, which is why we are here. */
inline int RetireWithdrawShouldSend(int state) { return (state == kPending) ? 1 : 0; }

/* May the caller forget this entry?  Only kAbsent; kWithdrawn is kept so a later retire of the same
   uid is a fresh event rather than a duplicate send. */
inline int RetireWithdrawKeep(int state) { return (state == kAbsent) ? 0 : 1; }

/* THE PRE-PATCH DECISION, written out so the suite can show the hole rather than describe it.  The old
   withdraw leg was reached only through `FindSpawned(uid)`, and a retired object resolves to nothing
   (spawn.cpp:823 `if (IsRetiredObject(...)) return 0;`), so the answer for the entire retired
   population was 0.  `mirrorRetired` is 1 when the mirror slot is marked dead. */
inline int RetireWithdrawLegacyWouldSend(int mirrorRetired) { return mirrorRetired ? 0 : 1; }

}   /* namespace cooprw */

#endif
