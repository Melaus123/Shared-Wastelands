/* M15 (T-197, more than two players, piece #3) - THE WORLD SERVER'S ONCE-A-SECOND JOBS ON A FIXED SCHEDULE,
 * AND A TIME BUDGET FOR EACH PASS OF MESSAGE HANDLING - AS PURE ARITHMETIC.
 *
 * Until M15 coop-store's loop ran its once-a-second jobs (clock, area map, leases, HELLO deadline, the 60 s
 * counter line, ...) only after its network wait came back EMPTY - 50 ms with no incoming message at all -
 * so under steady traffic from ~30-50 players they starved (.modding/investigations/server-load-read.md 4c;
 * build/design-many.md M15). Now:
 *
 *   FixedSchedule  The jobs are DUE at start, start + 1 s, start + 2 s, ... on a monotonic clock the caller
 *                  supplies (microseconds; store_main.cpp reads QueryPerformanceCounter). A pass that runs
 *                  late does not shift the grid: the next one is still due on the next whole period. A pass
 *                  more than a whole period late SKIPS the slots it missed (no catch-up burst) and counts
 *                  them. How late each pass ran is kept for the report line (max, sum, passes).
 *   PassBudget     One pass of message handling ends at the EARLIER of (a) its time budget from its start
 *                  and (b) the moment the jobs fall due - checked after EVERY event, and the network wait
 *                  inside the pass never reaches past that end. So a flood of messages delays the jobs by
 *                  at most the handling of ONE message. At least one event is handled per pass (the wait
 *                  may be 0, never negative), so the network is never starved by the jobs either.
 *
 * No Windows, no ENet, no clock reads here: the offline suite drives both directly (coop-test, m15_*).
 * Loop thread only. C++03 (VS2010).
 */
#ifndef COOP_LOOPTICK_H
#define COOP_LOOPTICK_H

namespace cooptick {

struct TickStats
{
    long long lateMaxUs, lateSumUs, passes, skipped;
    TickStats() : lateMaxUs(0), lateSumUs(0), passes(0), skipped(0) {}
};

class FixedSchedule
{
public:
    explicit FixedSchedule(long long periodUs) : m_period(periodUs > 0 ? periodUs : 1), m_due(0), m_started(false) {}
    /* The first pass is due at once (as before M15: the loop's first turn ran the jobs). */
    void Start(long long nowUs) { m_due = nowUs; m_started = true; }
    bool Started() const { return m_started; }
    bool Due(long long nowUs) const { return m_started && nowUs >= m_due; }
    long long DueAt() const { return m_due; }
    long long UsUntilDue(long long nowUs) const { return (!m_started || nowUs >= m_due) ? 0 : m_due - nowUs; }
    /* A pass STARTS now. Returns how late it is (us, >= 0) and moves the due time to the next slot of the grid
       that is still ahead; *skippedOut = whole slots missed (0 unless the pass was a full period late). */
    long long Ran(long long nowUs, long long* skippedOut)
    {
        long long late = nowUs - m_due;
        if (late < 0) late = 0;
        m_due += m_period;
        long long skipped = 0;
        if (m_due <= nowUs)
        {
            skipped = (nowUs - m_due) / m_period + 1;
            m_due += skipped * m_period;
        }
        ++m_stats.passes; m_stats.lateSumUs += late; m_stats.skipped += skipped;
        if (late > m_stats.lateMaxUs) m_stats.lateMaxUs = late;
        if (skippedOut != 0) *skippedOut = skipped;
        return late;
    }
    /* For the report line: the passes since the previous call, then starts counting again. */
    TickStats Take() { TickStats s = m_stats; m_stats = TickStats(); return s; }
private:
    long long m_period, m_due;
    bool m_started;
    TickStats m_stats;
};

class PassBudget
{
public:
    explicit PassBudget(long long budgetUs) : m_budget(budgetUs > 0 ? budgetUs : 0), m_end(0), m_events(0), m_max(0) {}
    /* A pass begins: it ends after its budget or when the periodic jobs fall due, whichever is first. */
    void Begin(long long nowUs, long long usUntilDue)
    {
        const long long span = (usUntilDue < m_budget) ? (usUntilDue > 0 ? usUntilDue : 0) : m_budget;
        m_end = nowUs + span;
        m_events = 0;
    }
    bool Spent(long long nowUs) const { return nowUs >= m_end; }
    /* The longest the network wait may block now: wantMs, cut to what is left of the pass, rounded UP to whole
       ms (so a sub-ms remainder waits 1 ms instead of spinning with 0-waits); 0 once the pass is spent. */
    int WaitMs(long long nowUs, int wantMs) const
    {
        if (wantMs < 0) wantMs = 0;
        if (nowUs >= m_end) return 0;
        const long long leftMs = (m_end - nowUs + 999) / 1000;
        return leftMs < (long long)wantMs ? (int)leftMs : wantMs;
    }
    void Count() { ++m_events; if (m_events > m_max) m_max = m_events; }
    long long Events() const { return m_events; }
    /* For the report line: the most events one pass handled since the previous call. */
    long long TakeMax() { const long long m = m_max; m_max = 0; return m; }
private:
    long long m_budget, m_end, m_events, m_max;
};

}   /* namespace cooptick */

#endif
