/* src/common/ragdollrest.h - R3 (read-ragdoll 2026-09-22): THE PENDING REST MOVES THE PHYSICS-THREAD DETOUR TAKES.
 *
 * ONE RULE, ownedmirror.h's: nothing in here reads engine memory, reads a global, calls the operating system or
 * includes a header, so the offline suite drives it directly (the interlocked operations are compiler intrinsics,
 * declared below - not calls).
 *
 * WHY IT EXISTS. A ragdolled character's position lives only in its physics bodies, and the ONE engine call that moves
 * a live ragdoll - RagdollClass translate 0x7D1580 - is made by the below-ground rescue 0x7D38D0, which runs every tick
 * from threadSafeRagdollUpdateUT 0x5B76F0 inside the engine's ragdoll pass (ThreadSafeRagdollUpdates), which runs on the
 * main thread (a crash dump's stack: the Ogre frame loop -> the pass -> threadSafeRagdollUpdateUT; appearance.cpp counts any
 * pass entry off the main thread as ragdollPass[offMain]). So the main thread (which
 * knows the owner's rest position) POSTS a move here, keyed by the copy's RagdollClass pointer, and the detour on
 * 0x7D38D0 TAKES it and makes the engine's own translate inside the engine's own moment for moving that ragdoll.
 *
 * SLIDE (decision 61, user 2026-09-22): a big correction slides instead of jumping. The slot keeps the TOTAL delta
 * and a step count; each hook call takes ONE step (delta / steps) and puts the slot back READY until the last one.
 *
 * SHAPE. kRestSlots fixed slots, nothing allocated. Each slot: ragdoll key, the AnimationClass that owns it, the delta,
 * and a state: EMPTY -> WRITING (the main thread fills it) -> READY -> TAKING (a detour applies it) -> EMPTY (RestDone).
 * Every transition out of READY is a compare-exchange, so a move is applied at most once and a withdraw either wins
 * (no further step) or loses (a step is being made). `pending` counts unfinished moves: the detour's fast path is one
 * load of it.
 *
 * THREADING CONTRACT. ONE writer (the main thread: RestPost, RestWithdraw), any number of takers (RestTake, RestDone,
 * RestAbandon). R3-b (review-r3): one ragdoll never has two moves in the table - RestPost refuses (busy) while one is
 * READY or mid-step, and a taker that finds the live ragdoll carried / inactive abandons the move it holds TAKING.
 * A taker that wins READY -> TAKING re-reads the key: a slot taken by another thread and re-posted for another
 * ragdoll between its check and its exchange (ABA) is put back READY, untouched.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no <atomic>.
 */
#ifndef COOP_COMMON_RAGDOLLREST_H
#define COOP_COMMON_RAGDOLLREST_H

#if defined(_MSC_VER)
extern "C" long _InterlockedCompareExchange(long volatile* Destination, long Exchange, long Comparand);
extern "C" long _InterlockedExchange(long volatile* Target, long Value);
extern "C" long _InterlockedIncrement(long volatile* Addend);
extern "C" long _InterlockedDecrement(long volatile* Addend);
#pragma intrinsic(_InterlockedCompareExchange, _InterlockedExchange, _InterlockedIncrement, _InterlockedDecrement)
#endif

namespace cooprest {

enum { kRestSlots = 64 };
enum { kRestEmpty = 0, kRestWriting = 1, kRestReady = 2, kRestTaking = 3 };
enum { kRestStepUnits = 4, kRestMaxSteps = 30 };   /* decision 61: one step per 4 units, at most 30 */

struct RestSlot
{
    void* volatile ragdoll;   /* the copy's RagdollClass (AnimationClass +0x2E8) - the key */
    void* volatile anim;      /* the AnimationClass that owned it when posted (notifyRagdollNeedsAnUpdate's this) */
    volatile float d[3];      /* the TOTAL move: owner's rest - the copy's AnimationClass +0x98 */
    volatile long  steps;     /* decision 61: the move is made in this many equal steps, one per hook call */
    volatile long  done;      /* steps made so far - written only by the thread holding the slot TAKING */
    volatile long  state;
};

struct RestTable
{
    RestSlot      s[kRestSlots];
    volatile long pending;    /* unfinished moves now (READY or mid-step) - the detour's fast path */
    volatile long full;       /* posts refused because no slot was free, all time */
    volatile long busy;       /* R3-b: posts refused because that ragdoll already had a move in the table, all time */
};

/* Zero it. A static RestTable is already zero; this is for the suite. */
inline void RestReset(RestTable* t)
{
    int i;
    for (i = 0; i < (int)kRestSlots; ++i)
    {
        t->s[i].state = kRestEmpty; t->s[i].ragdoll = 0; t->s[i].anim = 0;
        t->s[i].d[0] = 0.0f; t->s[i].d[1] = 0.0f; t->s[i].d[2] = 0.0f; t->s[i].steps = 0; t->s[i].done = 0;
    }
    t->pending = 0; t->full = 0; t->busy = 0;
}

/* Decision 61 (user, 2026-09-22): a big correction SLIDES. steps = clamp(|delta| / 4 units, 1, 30), so a small
   correction is one step and a large one ~30 consecutive calls of the hook (about 0.5 s). */
inline long RestStepsFor(float len)
{
    if (!(len > 0.0f)) return 1;
    const float n = len / (float)kRestStepUnits;
    if (n >= (float)kRestMaxSteps) return kRestMaxSteps;
    return (n < 1.0f) ? 1 : (long)n;
}

enum { kRestPostFull = 0, kRestPosted = 1, kRestPostBusy = -1 };

/* MAIN THREAD only. Fills a free slot: kRestPosted, or kRestPostFull (no free slot - counted in `full`), or
   kRestPostBusy (R3-b, review-r3: a move for the same ragdoll is still in the table, READY or a step being made -
   counted in `busy`). Nothing is replaced: the caller withdraws the old move and posts again on a later tick, so a
   ragdoll never has two moves in flight (no overshoot). */

inline int RestPost(RestTable* t, void* ragdoll, void* anim, float dx, float dy, float dz, long steps)
{
    int i;
    if (t == 0 || ragdoll == 0) return kRestPostFull;
    if (steps < 1) steps = 1;
    if (steps > kRestMaxSteps) steps = kRestMaxSteps;
    for (i = 0; i < (int)kRestSlots; ++i)
    {
        const long st = t->s[i].state;                 /* state BEFORE key */
        if (st != kRestEmpty && t->s[i].ragdoll == ragdoll) { _InterlockedIncrement(&t->busy); return kRestPostBusy; }
    }
    for (i = 0; i < (int)kRestSlots; ++i)
    {
        RestSlot* s = &t->s[i];
        if (s->state != kRestEmpty) continue;
        if (_InterlockedCompareExchange(&s->state, kRestWriting, kRestEmpty) != kRestEmpty) continue;
        s->ragdoll = ragdoll; s->anim = anim; s->d[0] = dx; s->d[1] = dy; s->d[2] = dz; s->steps = steps; s->done = 0;
        _InterlockedIncrement(&t->pending);            /* before READY, so the fast path cannot miss it */
        _InterlockedExchange(&s->state, kRestReady);
        return kRestPosted;
    }
    _InterlockedIncrement(&t->full);
    return kRestPostFull;
}

/* ANY THREAD. Takes ONE STEP of the READY move for `ragdoll`: the slot index (pass it to RestDone once the step is
   made), or -1. `anim` receives the posted AnimationClass and `step` (3 floats) the delta / steps to move now. */
inline int RestTake(RestTable* t, void* ragdoll, void** anim, float* step)
{
    int i;
    if (t == 0 || ragdoll == 0 || t->pending == 0) return -1;
    for (i = 0; i < (int)kRestSlots; ++i)
    {
        RestSlot* s = &t->s[i];
        if (s->state != kRestReady || s->ragdoll != ragdoll) continue;
        if (_InterlockedCompareExchange(&s->state, kRestTaking, kRestReady) != kRestReady) continue;
        if (s->ragdoll != ragdoll) { _InterlockedExchange(&s->state, kRestReady); continue; }   /* ABA - not ours */
        const float n = (float)((s->steps < 1) ? 1 : s->steps);
        if (anim != 0) *anim = s->anim;
        if (step != 0) { step[0] = s->d[0] / n; step[1] = s->d[1] / n; step[2] = s->d[2] / n; }
        return i;
    }
    return -1;
}

/* ANY THREAD, after RestTake's caller has made the step: counts it; after the LAST step the slot is free again,
   otherwise it goes back to READY for the next call. Returns true when that was the last step. */
inline bool RestDone(RestTable* t, int slot)
{
    if (t == 0 || slot < 0 || slot >= (int)kRestSlots) return false;
    RestSlot* s = &t->s[slot];
    if (s->state != kRestTaking) return false;
    s->done = s->done + 1;                             /* only the TAKING holder writes it */
    if (s->done < s->steps) { _InterlockedExchange(&s->state, kRestReady); return false; }
    s->ragdoll = 0; s->anim = 0;
    _InterlockedDecrement(&t->pending);
    _InterlockedExchange(&s->state, kRestEmpty);
    return true;
}

/* ANY THREAD, INSTEAD of RestDone (R3-b, review-r3 item 2): the taker drops the move it holds TAKING without making
   the step - the live ragdoll went inactive or was picked up. The slot is free again (the same protocol as a
   withdraw: key cleared, pending down, then EMPTY); the rest of the slide is never made. True = abandoned. */
inline bool RestAbandon(RestTable* t, int slot)
{
    if (t == 0 || slot < 0 || slot >= (int)kRestSlots) return false;
    RestSlot* s = &t->s[slot];
    if (s->state != kRestTaking) return false;
    s->ragdoll = 0; s->anim = 0;
    _InterlockedDecrement(&t->pending);
    _InterlockedExchange(&s->state, kRestEmpty);
    return true;
}

/* MAIN THREAD. Is a move for `ragdoll` posted and not yet finished (READY, or TAKING until RestDone)? */
inline bool RestIsPending(const RestTable* t, void* ragdoll)
{
    int i;
    if (t == 0 || ragdoll == 0) return false;
    for (i = 0; i < (int)kRestSlots; ++i)
    {
        const long st = t->s[i].state;                 /* state BEFORE key */
        if ((st == kRestReady || st == kRestTaking) && t->s[i].ragdoll == ragdoll) return true;
    }
    return false;
}

/* MAIN THREAD. Withdraws a READY move for `ragdoll`: true = withdrawn (it will never be applied),
   false = there was none, or a taker already has it. */
inline bool RestWithdraw(RestTable* t, void* ragdoll)
{
    int i;
    if (t == 0 || ragdoll == 0) return false;
    for (i = 0; i < (int)kRestSlots; ++i)
    {
        RestSlot* s = &t->s[i];
        if (s->state != kRestReady || s->ragdoll != ragdoll) continue;
        if (_InterlockedCompareExchange(&s->state, kRestWriting, kRestReady) != kRestReady) return false;
        s->ragdoll = 0; s->anim = 0;
        _InterlockedDecrement(&t->pending);
        _InterlockedExchange(&s->state, kRestEmpty);
        return true;
    }
    return false;
}

}   /* namespace cooprest */

#endif
