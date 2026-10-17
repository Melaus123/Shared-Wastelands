/* hbmath.h - E47-S2 (P8f re-prepared). THE HEARTBEAT'S ARITHMETIC, AS PURE FUNCTIONS.
 *
 * HEADER-ONLY (inline), so the plugin and the offline suite (src/coop-test/test_main.cpp, build.bat step 0 - a gate)
 * compile the same text with no build.bat change. No clock, no global, no engine memory, no OS header. C++03.
 */
#ifndef COOP_HBMATH_H
#define COOP_HBMATH_H

namespace coophb {

const unsigned int kPeriodMs          = 30000;   /* 30 s per owned awake world group */
const unsigned int kDirtyGapMs        = 2000;    /* a group publishes at most once every 2 s */
const unsigned int kBudgetBytesPerSec = 65536;   /* 64 KB/s */
const unsigned int kGiantBytes        = 262144;  /* 256 KB serialised = a giant record */
const unsigned int kGiantMultiplier   = 5;       /* a giant heartbeats at 5x the period (an item event still uses the gap; a wake alone does not) */
const unsigned int kBigRecordBytes    = 2097152; /* 2 MB: a record over this size is named once in the log */
const unsigned int kWakeLoadedSkipMs  = 10000;   /* a wake's publish is skipped only when visited this soon after the wake applied the record */

/* HOW MANY GROUPS THE CURSOR ADVANCES THIS TICK: ceil(n * elapsedMs / periodMs), at least 1, at most n, at most cap
 * (cap 0 = none). `elapsedMs` is WALL time since the previous tick - store.cpp's "1 Hz" gate is a FRAME count
 * (++g_tick % 118), so a quota in ticks would be right on one machine only. 0 for an empty ring or no elapsed time. */
inline unsigned int CursorQuota(unsigned int n, unsigned int periodMs, unsigned int elapsedMs, unsigned int cap)
{
    if (n == 0 || elapsedMs == 0 || periodMs == 0) return 0;
    const unsigned long long num = (unsigned long long)n * (unsigned long long)elapsedMs;
    unsigned long long q = (num + (unsigned long long)periodMs - 1ULL) / (unsigned long long)periodMs;
    if (q == 0ULL) q = 1ULL;
    if (q > (unsigned long long)n) q = (unsigned long long)n;
    if (cap != 0 && q > (unsigned long long)cap) q = (unsigned long long)cap;
    return (unsigned int)q;
}

/* MAY THIS GROUP PUBLISH NOW? `sinceLastMs` = now - its last visit.
 * THE CURSOR'S CYCLE IS THE PERIOD (F724): a turn publishes once past the 2 s floor. A per-group `>= 30000` test on top
 * of a ring whose own cycle is already <= 30 s delivers a 50 s heartbeat at fifty groups, silently. The one exception is
 * the giant rule: a record at or above giantBytes publishes on the FLOOR only every periodMs * giantMul. A dirty group
 * (an event fired) uses the gap whatever its size - the giant rule slows the floor, and an event is not the floor. The caller
 * passes dirty = false for a group marked only by a wake: waking is not a change to its record, so a giant waits as on the floor. */
inline bool DueOnTurn(bool dirty, unsigned int sinceLastMs, unsigned int lastBytes,
                      unsigned int periodMs, unsigned int minGapMs, unsigned int giantBytes, unsigned int giantMul)
{
    if (!dirty && giantBytes != 0 && lastBytes >= giantBytes && giantMul > 1)
    {
        const unsigned long long w = (unsigned long long)periodMs * (unsigned long long)giantMul;
        const unsigned int want = (w > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (unsigned int)w;
        return sinceLastMs >= want;
    }
    return sinceLastMs >= minGapMs;
}

/* THE BYTE BUDGET. `wouldSend` is the group's LAST known record size (0 = unknown - allowed, the first publish is how the
 * size becomes known). review-s2hb fold (HIGH stall): a record LARGER than the whole budget is admitted into an EMPTY second
 * (spent == 0) - before, it was never admitted and the cursor stopped behind it for ever - and what it spent over the budget is
 * owed by the following seconds (BudgetCarry), so `spentThisSecond` may start a second above 0. A refusal is a DEFERRAL to the
 * next pass: the caller moves PAST the group (marking it owed, so it goes first next tick) and the others go on. */
inline bool BudgetAllows(unsigned int spentThisSecond, unsigned int wouldSend, unsigned int budgetBytesPerSec)
{
    if (budgetBytesPerSec == 0) return true;
    if (spentThisSecond == 0) return true;
    if (spentThisSecond >= budgetBytesPerSec) return false;
    if (wouldSend == 0) return true;
    return (unsigned long long)spentThisSecond + (unsigned long long)wouldSend <= (unsigned long long)budgetBytesPerSec;
}

/* THE BUDGET'S DEBT: what the next one-second window starts with. BudgetAllows still lets a record of any size into an empty
 * second, so nothing waits for ever, but the bytes a window spent over the budget are owed: when `elapsedSec` whole seconds have
 * passed since the window opened, the new window starts at the old spend less one budget per elapsed second, never below 0.
 * While what is owed fills a whole budget every send is refused (a full second); a 2 MB record at 64 KB/s holds the heartbeat
 * for about 31 s. A window that stayed within the budget owes nothing, so a caller under the budget sees each new second empty.
 * `elapsedSec` 0 = the same window (unchanged). Budget 0 = no budget and no debt. */
inline unsigned int BudgetCarry(unsigned int spentInWindow, unsigned int budgetBytesPerSec, unsigned int elapsedSec)
{
    if (budgetBytesPerSec == 0) return 0;
    if (elapsedSec == 0) return spentInWindow;
    const unsigned long long paid = (unsigned long long)budgetBytesPerSec * (unsigned long long)elapsedSec;
    if ((unsigned long long)spentInWindow <= paid) return 0;
    return (unsigned int)((unsigned long long)spentInWindow - paid);
}

/* What a window has spent after `bytes` more went out, saturating instead of wrapping (a 4 GB spend cannot read as small). */
inline unsigned int BudgetSpend(unsigned int spentInWindow, unsigned int bytes)
{
    const unsigned long long v = (unsigned long long)spentInWindow + (unsigned long long)bytes;
    return (v > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (unsigned int)v;
}

/* THE BUDGET FOR ONE GROUP. `owedAndSpent` is the window's spend including what earlier seconds still owe (BudgetCarry);
 * `sentThisSecond` is what this second itself sent. A group marked by an item move whose last known record is under one second's
 * budget is held only to this second's own sends: an item hand-over's small record then goes within about a second even while a
 * big record's debt is being paid (that debt can last a minute and more), and the link still carries no more than one budget of
 * records in any one second. Every other group - a giant, a size not yet known, a group no item move marked - also pays the debt. */
/* A group that has already waited kDebtMaxWaitMs behind the debt is also held only to this second's own sends: item-move
 * records keep adding to the debt while they go, so without this bound a steady flow of them could hold every other group for
 * as long as the flow lasts. */
const unsigned int kDebtMaxWaitMs = 30000;
inline bool BudgetAllowsGroup(bool itemMoved, unsigned int owedAndSpent, unsigned int sentThisSecond, unsigned int wouldSend,
                              unsigned int budgetBytesPerSec, unsigned int waitedMs = 0)
{
    if (waitedMs >= kDebtMaxWaitMs) return BudgetAllows(sentThisSecond, wouldSend, budgetBytesPerSec);
    if (itemMoved && wouldSend != 0 && wouldSend < budgetBytesPerSec) return BudgetAllows(sentThisSecond, wouldSend, budgetBytesPerSec);
    return BudgetAllows(owedAndSpent, wouldSend, budgetBytesPerSec);
}

/* THE WAKE THAT APPLIED THE WORLD'S RECORD: is its publish skipped at this visit? Only when the group was marked by the wake
 * alone (no item move), the wake applied a record (`loadedAt` its writtenAt, 0 = none), that record is still both the world's
 * newest here (`recordWrittenAt`) and the one this copy last wrote or loaded (`localWrittenAt`; 0 = none known), and the visit
 * comes within `windowMs` of the wake. A later visit - a giant's long wait, a long budget deferral - publishes: by then the
 * squad has moved and changed since the load, and skipping would hold those changes back for another whole wait. */
inline bool WakeLoadedSkip(bool wakeOnly, long long loadedAt, long long recordWrittenAt, long long localWrittenAt,
                           unsigned int sinceLoadMs, unsigned int windowMs)
{
    if (!wakeOnly || loadedAt == 0) return false;
    if (recordWrittenAt != loadedAt || localWrittenAt != loadedAt) return false;
    return sinceLoadMs <= windowMs;
}

/* Bytes per second over a window, rounded; 0 for a zero-length window. */
inline unsigned int BytesPerSec(unsigned int bytesInWindow, unsigned int windowMs)
{
    if (windowMs == 0) return 0;
    const unsigned long long v = ((unsigned long long)bytesInWindow * 1000ULL + (unsigned long long)windowMs / 2ULL)
                                 / (unsigned long long)windowMs;
    return (v > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (unsigned int)v;
}

}   /* namespace coophb */

#endif
