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
const unsigned int kGiantMultiplier   = 5;       /* a giant heartbeats at 5x the period (an event still uses the gap) */

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
 * (an event fired) uses the gap whatever its size - the giant rule slows the floor, and an event is not the floor. */
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
 * (spent == 0) - before, it was never admitted and the cursor stopped behind it for ever. A refusal is a DEFERRAL to the
 * next pass: the caller moves PAST the group (marking it owed, so it goes first next tick) and the others go on. */
inline bool BudgetAllows(unsigned int spentThisSecond, unsigned int wouldSend, unsigned int budgetBytesPerSec)
{
    if (budgetBytesPerSec == 0) return true;
    if (spentThisSecond == 0) return true;
    if (spentThisSecond >= budgetBytesPerSec) return false;
    if (wouldSend == 0) return true;
    return (unsigned long long)spentThisSecond + (unsigned long long)wouldSend <= (unsigned long long)budgetBytesPerSec;
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
