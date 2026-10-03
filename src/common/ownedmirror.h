/* src/common/ownedmirror.h - A MIRROR OF THE OWNED-UID SET THAT A WORKER THREAD MAY READ (O1, recheck-c2b; O1-b).
 *
 * ONE RULE, the same one clockmath.h, loadlatch.h and retirewithdraw.h keep: nothing in here reads engine
 * memory, reads a global, calls the operating system or includes any header at all, so the offline suite
 * drives it directly.  (The one interlocked pointer swap is a compiler intrinsic, declared below - not a call.)
 *
 * WHY IT EXISTS.  net::IsUidMine answers from `g_localOwned`, a std::set the MAIN thread mutates (SetLocalOwner,
 * TakeLocalOwner, ReleaseLocalOwner).  Detours that run on engine WORKER threads asked it anyway -
 * detour_hitByMelee on the AI worker (T024/F095), detour_medicalUpdate, and C2's knockout gate and applyDamage
 * skip - and a red-black-tree find racing an erase can walk a freed node.  This table is written by the SAME
 * three functions, right beside each set mutation, and answers the same question without a lock and without
 * allocating.
 *
 * SHAPE.  Two open-addressed tables (linear probing) of kOwnedMirrorCap slots each - two STATIC buffers, nothing
 * is ever allocated.  Each slot is a key and a state (empty / live / tombstone).  ONE of the two is PUBLISHED
 * (`pub`): readers and the three writers use it.  Capacity follows decision 54 - no player-count dependence:
 * 32768 is far above the largest owned count seen (streamedLastTick ~300 on the driving game).
 *
 * REBUILD (O1-b, review-o1 MEDIUM).  Tombstones never leave a table by themselves unless they end a chain, so a
 * long session could leave no empty slot and every miss would scan the whole table.  So the MAIN thread, once per
 * SessionTick (OwnedMirrorMaintain), asks OwnedMirrorNeedsRebuild: an insert was refused since the last rebuild,
 * or fewer than 1/4 of the slots are empty and some of them are tombstones (a crowded table with no tombstones
 * would rebuild into the same table - pointless).  If so it clears the SPARE buffer, inserts every uid of
 * g_localOwned into it (a fresh table: no tombstones), and publishes it with ONE interlocked pointer swap.  A uid
 * that was refused goes in with the rest (review-o1 LOW: a refusal is healed by the next tick's rebuild).
 *
 * THE RETIRED BUFFER.  After a swap, a reader that loaded the old pointer just before it may still be probing the
 * old table - which is unchanged and answers the old truth, as a reader racing an update always could.  That
 * buffer is written again only by the NEXT rebuild, at the earliest one SessionTick (one frame) later, and a
 * lookup is a few dozen loads that finish within a frame.  That is the whole reclamation argument: it rests on a
 * reader never being held (descheduled mid-lookup) for a full frame.  Inferred, not measured.
 *
 * THREADING CONTRACT.  ONE writer (the main thread), any number of readers.  A reader loads `pub` ONCE per lookup
 * and probes that table only.  The writer stores the KEY before the STATE; a reader loads the STATE before the
 * KEY.  All fields are volatile, and VS2010's (v100, x64) default volatile semantics give volatile stores release
 * ordering and volatile loads acquire ordering (there is no /volatile switch in VS2010 - it arrived in VS2012);
 * x64 also does not reorder stores with stores or loads with loads.  So a reader that sees `live` sees the key
 * that was written for it, and a reader that sees the new `pub` sees the whole rebuilt table (the swap is a full
 * barrier after every store into it).
 * A slot never goes from non-empty to anything a probe could mistake for "keep going past a live key":
 *   - erase turns live -> tombstone (probes continue past it);
 *   - a tombstone is turned back to EMPTY only when the slot after it is empty, walking backwards, so
 *     no live key lies beyond it in any probe chain - a reader stopping there early loses nothing;
 *   - insert reuses the first tombstone in the uid's chain (key first, then state), else the first empty.
 * A lookup racing an update answers either the old or the new truth for THAT uid - never a crash.
 *
 * FULL.  An insert that finds no tombstone and no empty slot (kOwnedMirrorCap uids live at once) is refused and
 * counted in `full` and `refusedSinceRebuild`; until the rebuild re-inserts it, a lookup answers "not mine" for
 * it.  A rebuild that itself runs out of slots (more than kOwnedMirrorCap uids owned at once) counts the uids it
 * could not hold in `full` but NOT in `refusedSinceRebuild`, so it does not rebuild every tick for nothing.  That
 * is a fixed ceiling of kOwnedMirrorCap live owned uids - `full` on the [M1] line says whether it was reached.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no <atomic>.
 */
#ifndef COOP_COMMON_OWNEDMIRROR_H
#define COOP_COMMON_OWNEDMIRROR_H

#if defined(_MSC_VER)
extern "C" void* _InterlockedExchangePointer(void* volatile* Target, void* Value);
#pragma intrinsic(_InterlockedExchangePointer)
#endif

namespace coopown {

enum { kOwnedMirrorCap = 32768 };                /* power of two */
enum { kSlotEmpty = 0, kSlotLive = 1, kSlotTomb = 2 };

struct OwnedTable
{
    volatile unsigned int key[kOwnedMirrorCap];
    volatile unsigned int state[kOwnedMirrorCap];
    volatile long live;    /* live keys now (writer-maintained, read by reports) */
    volatile long tombs;   /* tombstones now */
};

struct OwnedMirror
{
    OwnedTable            buf[2];                /* the published one and the spare; never allocated */
    OwnedTable* volatile  pub;                   /* 0 (a zeroed static) means buf[0] */
    volatile long full;                  /* inserts refused because no slot was free, all time - ownedMirrorRefused */
    volatile long refusedSinceRebuild;   /* refused by an ordinary insert since the last rebuild */
    volatile long rebuilds;              /* tables published by a rebuild - ownedMirrorRebuilds */
};

/* Any thread: the published table - ONE load of `pub`.  A lookup uses what this returned and nothing else. */
inline OwnedTable* OwnedMirrorCur(const OwnedMirror* m)
{
    OwnedTable* t = m->pub;
    return (t != 0) ? t : (OwnedTable*)&m->buf[0];
}

inline void OwnedTableClear(OwnedTable* t)
{
    int i;
    for (i = 0; i < (int)kOwnedMirrorCap; ++i) { t->state[i] = kSlotEmpty; t->key[i] = 0; }
    t->live = 0; t->tombs = 0;
}

/* Zero it.  A static OwnedMirror is already zero; this is for the suite. */
inline void OwnedMirrorReset(OwnedMirror* m)
{
    OwnedTableClear(&m->buf[0]);
    OwnedTableClear(&m->buf[1]);
    m->pub = 0; m->full = 0; m->refusedSinceRebuild = 0; m->rebuilds = 0;
}

inline unsigned int OwnedMirrorHome(unsigned int uid)
{
    /* Fibonacci hashing: uids are (seat << 22) | counter (uidlayout.h, M4 / owner 205 A), so the low bits alone would cluster.
       32 - 15 = 17: the top 15 bits of the product index 32768 slots. */
    return (unsigned int)((uid * 2654435761u) >> 17) & (unsigned int)(kOwnedMirrorCap - 1);
}

inline bool OwnedTableHas(const OwnedTable* t, unsigned int uid)
{
    unsigned int at = OwnedMirrorHome(uid);
    int n;
    for (n = 0; n < (int)kOwnedMirrorCap; ++n)
    {
        const unsigned int s = t->state[at];           /* state BEFORE key */
        if (s == kSlotEmpty) return false;
        if (s == kSlotLive && t->key[at] == uid) return true;
        at = (at + 1) & (unsigned int)(kOwnedMirrorCap - 1);
    }
    return false;
}

/* Writer only.  1 = inserted, 0 = already live, -1 = no free slot (not counted here). */
inline int OwnedTableInsert(OwnedTable* t, unsigned int uid)
{
    unsigned int at = OwnedMirrorHome(uid);
    int firstTomb = -1, firstEmpty = -1, n;
    for (n = 0; n < (int)kOwnedMirrorCap; ++n)
    {
        const unsigned int s = t->state[at];
        if (s == kSlotEmpty) { firstEmpty = (int)at; break; }
        if (s == kSlotLive && t->key[at] == uid) return 0;
        if (s == kSlotTomb && firstTomb < 0) firstTomb = (int)at;
        at = (at + 1) & (unsigned int)(kOwnedMirrorCap - 1);
    }
    const int slot = (firstTomb >= 0) ? firstTomb : firstEmpty;
    if (slot < 0) return -1;
    const bool wasTomb = (t->state[slot] == kSlotTomb);
    t->key[slot] = uid;                                /* key BEFORE state */
    t->state[slot] = kSlotLive;
    if (wasTomb) --t->tombs;
    ++t->live;
    return 1;
}

/* Writer only.  1 = erased, 0 = was not live. */
inline int OwnedTableErase(OwnedTable* t, unsigned int uid)
{
    const unsigned int mask = (unsigned int)(kOwnedMirrorCap - 1);
    unsigned int at = OwnedMirrorHome(uid);
    int n;
    for (n = 0; n < (int)kOwnedMirrorCap; ++n)
    {
        const unsigned int s = t->state[at];
        if (s == kSlotEmpty) return 0;
        if (s == kSlotLive && t->key[at] == uid) break;
        at = (at + 1) & mask;
    }
    if (n == (int)kOwnedMirrorCap) return 0;
    t->state[at] = kSlotTomb;
    ++t->tombs;
    --t->live;
    /* Reclaim: a tombstone followed by an empty slot ends every chain through it, so it may become empty;
       walk backwards while that stays true.  Never loops forever: at most the whole table. */
    if (t->state[(at + 1) & mask] == kSlotEmpty)
    {
        for (n = 0; n < (int)kOwnedMirrorCap && t->state[at] == kSlotTomb; ++n)
        {
            t->state[at] = kSlotEmpty;
            --t->tombs;
            at = (at + mask) & mask;                   /* at - 1 */
        }
    }
    return 1;
}

/* Any thread.  Same answer as the owned set at the moment of the last completed update. */
inline bool OwnedMirrorHas(const OwnedMirror* m, unsigned int uid)
{
    return OwnedTableHas(OwnedMirrorCur(m), uid);
}

/* Any thread.  kit3 (kit2 review + T522, review D8): true while every owned uid is certainly in the published table - no
   ordinary insert refused since the last rebuild, and not every slot live (a rebuild that ran out of slots leaves it full).
   False = a "not mine" answer may be an owned uid the table could not hold. */
inline bool OwnedMirrorExact(const OwnedMirror* m)
{
    if (m->refusedSinceRebuild > 0) return false;
    return OwnedMirrorCur(m)->live < (long)kOwnedMirrorCap;
}

/* Writer only.  1 = inserted, 0 = already live, -1 = refused (counted; the next rebuild re-inserts it). */
inline int OwnedMirrorInsert(OwnedMirror* m, unsigned int uid)
{
    const int r = OwnedTableInsert(OwnedMirrorCur(m), uid);
    if (r < 0) { ++m->full; ++m->refusedSinceRebuild; }
    return r;
}

/* Writer only.  1 = erased, 0 = was not live. */
inline int OwnedMirrorErase(OwnedMirror* m, unsigned int uid)
{
    return OwnedTableErase(OwnedMirrorCur(m), uid);
}

/* Report numbers, from the published table. */
inline long OwnedMirrorLive(const OwnedMirror* m)  { return OwnedMirrorCur(m)->live; }
inline long OwnedMirrorTombs(const OwnedMirror* m) { return OwnedMirrorCur(m)->tombs; }
inline long OwnedMirrorEmpty(const OwnedMirror* m)
{
    const OwnedTable* t = OwnedMirrorCur(m);
    return (long)kOwnedMirrorCap - t->live - t->tombs;
}

/* Writer only: should the main thread rebuild now?  A refusal since the last rebuild, or fewer than 1/4 of the
   slots empty with at least one tombstone to reclaim. */
inline bool OwnedMirrorNeedsRebuild(const OwnedMirror* m)
{
    if (m->refusedSinceRebuild > 0) return true;
    const OwnedTable* t = OwnedMirrorCur(m);
    const long empty = (long)kOwnedMirrorCap - t->live - t->tombs;
    return empty * 4 < (long)kOwnedMirrorCap && t->tombs > 0;
}

/* Writer only - a rebuild is Begin, one Add per owned uid, Publish, all in one main-thread call.  Begin clears and
   returns the SPARE (the buffer retired by the previous rebuild, at least one tick ago). */
inline OwnedTable* OwnedMirrorRebuildBegin(OwnedMirror* m)
{
    OwnedTable* cur = OwnedMirrorCur(m);
    OwnedTable* spare = (cur == &m->buf[0]) ? &m->buf[1] : &m->buf[0];
    OwnedTableClear(spare);
    return spare;
}
inline void OwnedMirrorRebuildAdd(OwnedMirror* m, OwnedTable* spare, unsigned int uid)
{
    if (OwnedTableInsert(spare, uid) < 0) ++m->full;   /* more owned uids than slots: counted, not re-triggered */
}
inline void OwnedMirrorRebuildPublish(OwnedMirror* m, OwnedTable* spare)
{
#if defined(_MSC_VER)
    _InterlockedExchangePointer((void* volatile*)&m->pub, (void*)spare);   /* ONE swap; readers see all or none */
#else
    m->pub = spare;
#endif
    m->refusedSinceRebuild = 0;
    ++m->rebuilds;
}

}   /* namespace coopown */

#endif
