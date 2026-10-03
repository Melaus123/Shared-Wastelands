/* src/common/uidslots.h - THE PER-COPY WORD TABLES' SLOT BOOKKEEPING, AS PURE FUNCTIONS.
 *
 * WHAT IT IS FOR.  medical.cpp keeps three fixed tables keyed by the uid of a copy (this game's copy of a character
 * another game drives): the owner's latest hunger, its "knocked out" word and its "dead" word.  The medical, knockout
 * and death detours read them lock-free on the AI worker; the main thread is the one writer.  A uid's slot is RELEASED
 * when its copy is removed on this game, when its ownership moves to or from this game, and at world teardown and
 * session end - so a table holds the copies present now, not every copy the session ever saw, and a value left from an
 * earlier stay of a character here can never be read again.  The fixed size stays only as a safety net: an insert that
 * finds no slot is refused and counted by the caller.
 *
 * THE SCHEME (uidtable.h's index, keyed by a uid instead of an address).  Linear probing from the uid's home slot.  A
 * released slot becomes a TOMBSTONE, never empty: it is non-empty, so no probe run is broken and a reader walking past it
 * keeps going.  A tombstone becomes empty again only when the slot after it is already empty (Trimmable): such a slot
 * cannot lie inside any entry's run, because the empty slot after it would already have ended that run.  An insert reuses
 * the first tombstone of the run, after walking the whole run, so a uid is never held twice.  Every walk stops after
 * `probe` slots, so a crowded table never costs a whole-table walk inside a detour; an entry therefore always sits within
 * `probe` slots of its home, and a lookup that walks `probe` slots has seen every place it could be.
 * After a long churn every free slot can be a tombstone (no empty slot left to end a run and let a trim start); the tables
 * then still work - an insert takes the first tombstone within the probe - but a lookup that misses walks the whole probe,
 * which is the cost the probe limit bounds.  World teardown and session end put every slot back to empty.
 *
 * The same rule as uidtable.h: nothing in here reads engine memory, reads a global, calls the operating system or includes
 * any header.  The plugin keeps the storage and the interlocked publication order; the offline suite drives these.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef KMP_COMMON_UIDSLOTS_H
#define KMP_COMMON_UIDSLOTS_H

namespace uidslot {

/* A slot's key: 0 = never used (ends a probe run), kTomb = released (a run goes on through it), else the uid it holds. */
const unsigned int kEmpty = 0u;
const unsigned int kTomb  = 0xFFFFFFFFu;

/* May this uid be stored?  0 is "unassigned" everywhere; 0xFFFFFFFF (seat 1023's last counter, uidlayout.h) is the
   tombstone's key, so that one uid is refused and counted as a full table by the caller. */
inline int Storable(unsigned int uid) { return (uid != kEmpty && uid != kTomb) ? 1 : 0; }

/* The uid's home slot: a Fibonacci multiply, masked (the tables are powers of two). */
inline int Home(unsigned int uid, int mask) { return (int)((uid * 2654435761u) & (unsigned int)mask); }

/* The walk length: `probe`, never more than the table. */
inline int ProbeLen(int mask, int probe) { return (probe <= 0 || probe > mask + 1) ? mask + 1 : probe; }

/* FIND.  `keys(i)` returns slot i's key.  Returns the slot holding `uid`, or -1.  An empty slot ends the run; a tombstone
   does not. */
template <class K>
inline int Find(const K& keys, int mask, int probe, unsigned int uid)
{
    if (!Storable(uid)) return -1;
    const int home = Home(uid, mask), n = ProbeLen(mask, probe);
    for (int p = 0; p < n; ++p)
    {
        const int s = (home + p) & mask;
        const unsigned int k = keys(s);
        if (k == kEmpty) return -1;
        if (k == uid) return s;
    }
    return -1;
}

/* WHERE TO INSERT.  Returns the slot to write - the first tombstone of the run if there is one, else the empty slot that
   ends it - or -1 when the walk found neither (the table is full around this home).  When `uid` is ALREADY in the run,
   *existing is set to its slot and that slot is returned: the caller updates it in place. */
template <class K>
inline int InsertAt(const K& keys, int mask, int probe, unsigned int uid, int* existing)
{
    *existing = -1;
    if (!Storable(uid)) return -1;
    const int home = Home(uid, mask), n = ProbeLen(mask, probe);
    int firstTomb = -1;
    for (int p = 0; p < n; ++p)
    {
        const int s = (home + p) & mask;
        const unsigned int k = keys(s);
        if (k == uid) { *existing = s; return s; }
        if (k == kTomb) { if (firstTomb < 0) firstTomb = s; continue; }
        if (k == kEmpty) return firstTomb >= 0 ? firstTomb : s;
    }
    return firstTomb;
}

/* MAY THIS TOMBSTONE BECOME EMPTY?  Only when the next slot is already empty.  The caller walks backwards from a fresh
   tombstone while this answers 1. */
template <class K>
inline int Trimmable(const K& keys, int mask, int s)
{
    return (keys(s & mask) == kTomb && keys((s + 1) & mask) == kEmpty) ? 1 : 0;
}

}   /* namespace uidslot */

#endif
