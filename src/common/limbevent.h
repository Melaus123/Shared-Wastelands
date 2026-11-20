/* src/common/limbevent.h - T-1442 step 2 (F1427 design B): A LOST LIMB GOES OUT AT THE AMPUTATION, NOT ON THE TIMED LIMB WATCH.
 *
 * ONE RULE, ragdollrest.h's: nothing in here reads engine memory, reads a global, calls the operating system or includes a
 * header, so the offline suite drives it directly (the interlocked operations are compiler intrinsics, declared below - not calls).
 *
 * WHY IT EXISTS. The amputation hook (clothing.cpp detour_limbAmputate, MedicalSystem::amputate) may run on an AI worker, where
 * nothing may allocate, lock or log, and the STATE send (StatePush) is main-thread only. So the hook's OWNER branch - a character
 * this game drives, not /god, not the mod's own apply onto a copy - NOTES the character's uid here after the engine's amputate
 * returns (the new limb state exists only then), and the main thread DRAINS the notes the next frame (clothing.cpp
 * LimbEventDrainTick): a character whose limb fingerprint changed is pushed at once, and the fingerprint is stored where the limb
 * watch (LimbOwnWatchTick) keeps its own, so the watch then finds it unchanged and does not send it a second time.
 *
 * SHAPE. kLimbEvSlots fixed slots, each one uid (0 = empty); nothing allocated. Add: a uid already queued is not queued again
 * (cheap: a scan; two threads adding the same uid at once may both get a slot - the drain then pushes the first and finds the
 * second unchanged); otherwise the first empty slot is taken by compare-exchange; no empty slot = overflow, counted - the limb
 * watch is the backstop and still sends that change on its own clock. Drain: each slot is emptied by exchange.
 *
 * THREADING CONTRACT. Any number of adders (any thread: LimbEvAdd), ONE drainer (the main thread: LimbEvDrain, LimbEvReset).
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no <atomic>.
 */
#ifndef COOP_COMMON_LIMBEVENT_H
#define COOP_COMMON_LIMBEVENT_H

#if defined(_MSC_VER)
extern "C" long _InterlockedCompareExchange(long volatile* Destination, long Exchange, long Comparand);
extern "C" long _InterlockedExchange(long volatile* Target, long Value);
extern "C" long _InterlockedIncrement(long volatile* Addend);
extern "C" long _InterlockedDecrement(long volatile* Addend);
#pragma intrinsic(_InterlockedCompareExchange, _InterlockedExchange, _InterlockedIncrement, _InterlockedDecrement)
#endif

namespace cooplimbev {

enum { kLimbEvSlots = 64 };
enum { kLimbEvOverflow = 0, kLimbEvNoted = 1, kLimbEvAlready = 2, kLimbEvNoUid = -1 };

struct LimbEvTable
{
    volatile long uid[kLimbEvSlots];   /* a noted uid, as long (0 = empty) */
    volatile long pending;             /* uids in the table now - the drain's fast path */
    volatile long noted;               /* adds that took a slot, all time */
    volatile long already;             /* adds whose uid was already queued, all time */
    volatile long overflow;            /* adds that found no empty slot, all time (the limb watch sends those) */
};

/* MAIN THREAD. Empties one slot by exchange; `pending` goes down only for a slot that held a uid (fold 2b: never written directly,
   so an add racing the teardown leaves it consistent - the add's own increment balances the decrement, whichever runs first). */
inline void LimbEvResetSlot(LimbEvTable* t, int i)
{
    if (t == 0 || i < 0 || i >= (int)kLimbEvSlots) return;
    if (_InterlockedExchange(&t->uid[i], 0) != 0) _InterlockedDecrement(&t->pending);
}
/* MAIN THREAD (the world teardown; the suite). Empties every slot - the notes are dropped, the all-time counters (noted, already,
   overflow) are kept. A static LimbEvTable starts empty. */
inline void LimbEvReset(LimbEvTable* t)
{
    int i;
    if (t == 0) return;
    for (i = 0; i < (int)kLimbEvSlots; ++i) LimbEvResetSlot(t, i);
}

/* ANY THREAD (the amputate hook: no allocation, no lock). kLimbEvNoted, kLimbEvAlready (that uid waits in the table now),
   kLimbEvOverflow (no empty slot - counted) or kLimbEvNoUid (uid 0: not a character this game knows). */
inline int LimbEvAdd(LimbEvTable* t, unsigned int uid)
{
    int i;
    if (t == 0 || uid == 0) return kLimbEvNoUid;
    const long v = (long)uid;
    for (i = 0; i < (int)kLimbEvSlots; ++i)   /* fold 2b: each slot read by compare-exchange (a full barrier, after the engine's limb writes) */
        if (_InterlockedCompareExchange(&t->uid[i], v, v) == v) { _InterlockedIncrement(&t->already); return kLimbEvAlready; }
    for (i = 0; i < (int)kLimbEvSlots; ++i)
    {
        if (t->uid[i] != 0) continue;
        if (_InterlockedCompareExchange(&t->uid[i], v, 0) != 0) continue;
        _InterlockedIncrement(&t->pending);
        _InterlockedIncrement(&t->noted);
        return kLimbEvNoted;
    }
    _InterlockedIncrement(&t->overflow);
    return kLimbEvOverflow;
}

/* MAIN THREAD. Empties the table into out[0..max) (each slot by exchange); returns how many were taken. A uid taken twice (two
   adders raced past the scan) comes out twice: the caller's fingerprint test finds the second unchanged. A slot past `max` stays
   for the next drain. */
inline int LimbEvDrain(LimbEvTable* t, unsigned int* out, int max)
{
    int i, n = 0;
    if (t == 0 || out == 0 || max <= 0 || t->pending == 0) return 0;
    for (i = 0; i < (int)kLimbEvSlots && n < max; ++i)
    {
        if (t->uid[i] == 0) continue;
        const long v = _InterlockedExchange(&t->uid[i], 0);
        if (v == 0) continue;
        _InterlockedDecrement(&t->pending);
        out[n++] = (unsigned int)(unsigned long)v;
    }
    return n;
}

/* ---- the drain's decision for one noted uid (MAIN THREAD) --------------------------------------------------------------- */
/* What the drain does with a noted uid whose limbs it has just read: kEvGone (no longer this game's, or not spawned, or the read
   faulted - nothing sent, nothing stored), kEvUnchanged (the fingerprint equals the one the watch keeps - the watch or an
   earlier event already sent it), kEvPush (it differs, or the watch has none yet: an amputation is a change - send it and store
   the fingerprint). */
enum { kEvGone = 0, kEvUnchanged = 1, kEvPush = 2 };
inline int LimbEvVerdict(int stillMine, int readOk, int haveStored, unsigned long long stored, unsigned long long now)
{
    if (stillMine == 0 || readOk == 0) return kEvGone;
    if (haveStored != 0 && stored == now) return kEvUnchanged;
    return kEvPush;
}

/* Does the drain store the new fingerprint? Only when the STATE went out: a push that found no road leaves the watch's old
   fingerprint, so the watch (the backstop) still sees the change and tries again on its own clock. */
inline int LimbEvStores(int verdict, int sent) { return (verdict == kEvPush && sent != 0) ? 1 : 0; }

/* The limb watch's own decision - clothing.cpp LimbOwnWatchTick calls it (fold 2b; its behaviour is main's): 0 first sight
   (recorded, not sent), 1 unchanged (silent), 2 changed (sent). The suite checks that a fingerprint the drain stored makes the
   watch silent. */
enum { kWatchFirstSight = 0, kWatchSilent = 1, kWatchSends = 2 };
inline int LimbWatchSays(int haveStored, unsigned long long stored, unsigned long long now)
{
    if (haveStored == 0) return kWatchFirstSight;
    return (stored == now) ? kWatchSilent : kWatchSends;
}

/* The [LIMBS] event line's cap per launch. */
const long long kLimbEvLogCap = 200;

}   // namespace cooplimbev

#endif
