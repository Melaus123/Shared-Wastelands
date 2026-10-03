/* src/common/deferredmerge.h - WHAT A NOTEBOOK FILE'S DEFERRED LOAD MERGES WHEN THE FILE FINALLY OPENS,
 * AND NOTHING ELSE (P8l, review-p8d C-1 / H-1 / H-2).
 *
 * THE RULE THIS FILE EXISTS TO ENFORCE is storemeta.h's and clockmath.h's: nothing in here reads a global,
 * calls the operating system or includes a Windows, ENet or Ogre header. Everything is a pure
 * function of its arguments. It is HEADER-ONLY, the shape loadlatch.h already uses, so it needs no extra
 * translation unit in any of the three build.bat files - only the include.
 *
 * WHY IT EXISTS. When uniques.txt / options.txt / clock.txt is ON DISK AND UNREADABLE at start, the relay
 * defers the load and REFUSES EVERY WRITE of that file until it opens - but it keeps accepting messages,
 * because a deferral that consumes its trigger is silent feature loss. So at the moment the file finally
 * opens there are TWO states to reconcile: what was on disk all along, and the changes that arrived while
 * nobody could see it. That reconciliation is a decision, it is where review-p8d C-1's revived DEAD unique
 * has to be caught, and a decision that can only be reproduced by locking a file is a decision that is
 * never tested. So it is pure, and it is here.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no >> template closer.
 */
#ifndef COOP_COMMON_DEFERREDMERGE_H
#define COOP_COMMON_DEFERREDMERGE_H

namespace coopmerge {

/* WHAT HAPPENS TO ONE unique-character row that arrived while uniques.txt could not be read, when the file
   is finally read. THE WHOLE POINT IS THE MIDDLE ONE: review-p5j H-2's "DEAD IS TERMINAL" guard is a
   comparison against THE STORED STATE, and while the load was deferred the stored map was EMPTY - so the
   guard could not fire, and review-p8d C-1 measured a character recorded dead coming back alive on disk.
   The guard is applied HERE instead, against the map that was actually on the file. */
enum UniqueMerge
{
    kMergeKeepLoaded     = 0,   /* the row on disk stands and the deferred change is REFUSED - it is a state
                                   above 0 for a character the file records as DEAD, and nothing revives one */
    kMergeTakeDeferred   = 1,   /* the deferred change is newer than the row on disk, and wins */
    kMergeInsertDeferred = 2    /* the file had no row for this character at all */
};

/* `loadedState` is meaningful only when haveLoadedRow is non-zero. State 0 is DEAD; 1 and 2 are not. */
inline int UniqueMergeDecide(int haveLoadedRow, int loadedState, int deferredState)
{
    if (!haveLoadedRow) return kMergeInsertDeferred;
    if (loadedState == 0 && deferredState != 0) return kMergeKeepLoaded;
    return kMergeTakeDeferred;
}

/* THE WORLD'S CLOCK, when clock.txt is read at last and a game has already seeded a time into memory.
   NEGATIVE MEANS "no clock" on either side. The rule is decision 46's, unchanged: this world's clock only
   ever moves FORWARD, so the answer is the later of the two and never the earlier - the file's recorded
   time is not thrown away by a joining game's save, and a game's newer time is not rolled back by a file
   that was locked for a second. */
inline double ClockMergeHours(double fileHours, double memoryHours)
{
    if (!(fileHours >= 0.0))   return memoryHours;
    if (!(memoryHours >= 0.0)) return fileHours;
    return fileHours > memoryHours ? fileHours : memoryHours;
}

/* review-p8l M-2. WHICH ROWS THE DEFERRED RETRY PUTS BACK ON THE WIRE WHEN uniques.txt FINALLY OPENS.
   P8l republished only the rows the merge REFUSED. The rows the FILE held - every unique state this
   world already had - were never sent, and a game that connected while the load was deferred had been
   handed an EMPTY unique map in its WELCOME and was never corrected: measured. options.txt and
   clock.txt already republish their MERGED state. The answer is the whole merged map, and whether a
   particular row was refused is a fact for the log, not a filter on the send.
   `inMerged` is 0 only for a row that is not in the merged map at all, which nothing may publish. */
inline int UniqueRepublishRow(int inMerged, int wasRefused)
{
    (void)wasRefused;
    return inMerged ? 1 : 0;
}

}   /* namespace coopmerge */

#endif
