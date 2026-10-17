/* src/common/walkdrop.h - T-609 part 2 (H4): A BUILDING THIS MOD DISMANTLES LEAVES THE MEMOISED BUILDING LIST AT ONCE.
 *
 * items.cpp keeps the engine's list of loaded buildings for one generation (g_boxWalkList; the generation moves once per ItemsTick
 * and at every zone deactivation), and every reader compares or dereferences its raw pointers. A building the mod dismantles
 * (build.cpp BdDismantlePod / BdConfirmDismantlePod) is freed by the engine's kill list a frame later - between two generations -
 * so its pointer is dropped from the current list straight after the call (WalkListDrop) and kept out of every fresh walk for
 * kGoneKeepDrains drains (a small ring, WalkListDropRecent), by which time the engine has taken it out of its own lists.
 * A new building the engine happens to place at the same address inside that window is left out of the list too: a box move into it
 * is refused for those drains (the safe direction).
 *
 * PURE C++03 (VS2010 v100), header-only. Swept by the offline suite (t609_walk_drop, t609_gone_guard). */
#ifndef WALKDROP_H_INCLUDED
#define WALKDROP_H_INCLUDED

namespace walkdrop {

const int kGoneCap = 64;                /* buildings remembered; the 65th replaces the oldest */
const long long kGoneKeepDrains = 3;    /* a dropped building is kept out of fresh walks in its own drain and the next two */

struct GoneRow { const void* p; long long drain; };

/* every entry equal to `gone` leaves list[0..n), the rest keep their order; returns the new count */
inline int WalkListDrop(void** list, int n, const void* gone)
{
    int k = 0;
    for (int i = 0; i < n; ++i) if (list[i] != gone) list[k++] = list[i];
    return k;
}

/* 1 = `p` was dropped fewer than `keep` drains ago */
inline int GoneRecent(const GoneRow* rows, int n, const void* p, long long drainNow, long long keep)
{
    for (int i = 0; i < n; ++i) if (rows[i].p == p && drainNow - rows[i].drain < keep) return 1;
    return 0;
}

/* remembers `p` as dropped in drain `drain` (an entry already there is re-stamped); a full ring replaces its oldest row */
inline void GoneAdd(GoneRow* rows, int cap, int* n, int* next, const void* p, long long drain)
{
    if (rows == 0 || cap <= 0 || n == 0 || next == 0 || p == 0) return;
    for (int i = 0; i < *n; ++i) if (rows[i].p == p) { rows[i].drain = drain; return; }
    if (*n < cap) { rows[*n].p = p; rows[*n].drain = drain; ++*n; return; }
    rows[*next].p = p; rows[*next].drain = drain;
    *next = (*next + 1) % cap;
}

/* a fresh walk's list without the buildings dropped fewer than `keep` drains ago; returns the new count */
inline int WalkListDropRecent(void** list, int n, const GoneRow* rows, int rowsN, long long drainNow, long long keep)
{
    int k = 0;
    for (int i = 0; i < n; ++i) if (GoneRecent(rows, rowsN, list[i], drainNow, keep) == 0) list[k++] = list[i];
    return k;
}

/* fold 1 (F4) + re-check: 0 (stays in the list) ONLY when the dismantle call did not fault, its state reads afterwards, and that
   state reads as NOT dismantled (build.cpp BdReadState / BdDismOf, the reads REMOVE and the P14 tombstone judge it by) - an
   unfinished dismantle REMOVE retries. A faulted call or an unreadable state may already have queued the building to be freed,
   so it leaves the list: dropping is the safe direction (this file's header). */
inline int GoneAfterDismantle(int callOk, int stateOk, int dismantled)
{
    return (callOk != 0 && stateOk != 0 && dismantled == 0) ? 0 : 1;
}

}   /* namespace walkdrop */

#endif
