/* T-1403 (owner 1755 a, 1756 a) - THE WORLD SERVER'S START-UP PROGRESS, read by the game that started it on the same computer.
   The world server writes a small text file in its world folder (temp file, then renamed over the real one) while it starts: the
   step it is in, and for the record walk the entries read so far (n) and the walk's total (m, from the folder listing). It is written
   at each step's start and, inside a step, at most once a second when n moved; it is removed when the server's main loop starts.
   The game's HOST press reads it (ui.cpp PressTick) and fails only when it has not moved for kStallMs, or the server process ended
   (coopui::PressVerdictOf). No network message: the file is the road the game already has into the world folder (host.key). */
#ifndef SW_STARTPROGRESS_H
#define SW_STARTPROGRESS_H

#include <string>
#include <cstdlib>

namespace startprog {

const char* const kFile        = "startup.progress";   /* in the world folder */
const char* const kStepRecords = "records";            /* the record walk: the only step whose counts are shown */
const unsigned    kStallMs     = 30000u;               /* HOST fails when the progress has not moved for this long */
const double      kWriteGapSec = 1.0;                  /* inside a step, a write at most this often */

struct Row
{
    unsigned long pid;   /* the world server's process id - a file a stopped server left is not this one's */
    std::string   step;
    long long     n, m;
    Row() : pid(0), n(0), m(0) {}
};

inline std::string Num(long long v)
{
    if (v == 0) return "0";
    const bool neg = v < 0;
    unsigned long long u = neg ? (unsigned long long)(-(v + 1)) + 1ULL : (unsigned long long)v;
    std::string s;
    while (u != 0) { s.insert(s.begin(), (char)('0' + (int)(u % 10ULL))); u /= 10ULL; }
    return neg ? "-" + s : s;
}
/* 19156 -> "19,156" (the owner's approved form) */
inline std::string Grouped(long long v)
{
    const std::string d = Num(v < 0 ? -v : v);
    std::string out;
    for (size_t i = 0; i < d.size(); ++i)
    {
        if (i != 0 && (d.size() - i) % 3 == 0) out += ',';
        out += d[i];
    }
    return v < 0 ? "-" + out : out;
}

inline std::string Format(const Row& r)
{
    return "pid " + Num((long long)r.pid) + "\nstep " + r.step + "\nn " + Num(r.n) + "\nm " + Num(r.m) + "\n";
}
/* false = not a whole progress file (a torn read is never taken for one: all four lines must be there) */
inline bool Parse(const std::string& text, Row* out)
{
    Row r; int have = 0;
    size_t at = 0;
    while (at < text.size())
    {
        size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(at, e - at);
        at = e + 1;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        const size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        const std::string k = line.substr(0, sp), v = line.substr(sp + 1);
        if (v.empty()) continue;
        if (k == "step") { r.step = v; have |= 1; continue; }
        char* end = 0;
        if (v[0] < '0' || v[0] > '9') continue;   /* digits only: no sign */
        const long long x = (long long)strtoul(v.c_str(), &end, 10);
        if (*end != 0) continue;
        if (k == "pid") { r.pid = (unsigned long)x; have |= 2; }
        else if (k == "n") { r.n = x; have |= 4; }
        else if (k == "m") { r.m = x; have |= 8; }
    }
    if (have != 15) return false;
    if (out) *out = r;
    return true;
}
/* the file is this press's server's: the pid it was started with. 0 = this game started none (a world kept running, a start that
   failed) - then no file is (review fold: a file a crashed or refused server left is never taken for this press's) */
inline bool RowIsOurs(const Row& r, unsigned long pid) { return pid != 0 && r.pid == pid; }

/* THE HOST STATUS LINE (owner 1756 a, exact): while the server reads its records, `Starting "<world>"...  <n> / <m>` - the line as
   it is, two spaces, the counts with thousands separators; before any count (or in another step) the line as it is. */
inline std::string StatusWithCounts(const std::string& line, bool have, const Row& r)
{
    if (!have || r.step != kStepRecords || r.m <= 0) return line;
    return line + "  " + Grouped(r.n) + " / " + Grouped(r.m);
}

/* THE MOVE WATCH (game side, pure): moved = a progress row this press has not seen before (another step, another count). Started at
   the press, so no progress file at all counts as not moved since the press. Times are GetTickCount values (wrap-safe). */
struct Watch
{
    int         seen;
    std::string key;
    unsigned    movedAtMs;
    Watch() : seen(0), movedAtMs(0) {}
};
inline std::string KeyOf(const Row& r) { return Num((long long)r.pid) + " " + r.step + " " + Num(r.n) + " " + Num(r.m); }
inline void WatchStart(Watch* w, unsigned pressMs) { w->seen = 0; w->key.clear(); w->movedAtMs = pressMs; }
/* have = a row of this press's server was read now; returns the ms since the progress last moved (or since the press) */
inline unsigned WatchStill(Watch* w, bool have, const Row& r, unsigned nowMs)
{
    if (have)
    {
        const std::string k = KeyOf(r);
        if (w->seen == 0 || k != w->key) { w->seen = 1; w->key = k; w->movedAtMs = nowMs; }
    }
    return (unsigned)(nowMs - w->movedAtMs);
}
/* review fold: the file gone after this press read a row of it (the server removes it when its main loop starts) is one move, once -
   the world's own answer (its WELCOME) is what comes next, and it gets the whole kStallMs from there */
inline void WatchGone(Watch* w, unsigned nowMs) { if (w->seen != 0 && w->key != "gone") { w->key = "gone"; w->movedAtMs = nowMs; } }

/* T-1403 (T2520) review fold: the file gone is the server's "listening" only after a row of it was read, the first time it is seen gone,
   and while the server process still runs - it removes the file on its early exits as well (port busy, writer, rules, folder move, a close) */
inline int ListeningOf(int seen, int alreadyGone, int serverRunning) { return (seen != 0 && alreadyGone == 0 && serverRunning != 0) ? 1 : 0; }
/* server side: inside a step, write when n moved and the last write is kWriteGapSec old */
inline bool WriteDue(long long n, long long wroteN, double now, double wroteAt) { return n != wroteN && now - wroteAt >= kWriteGapSec; }
/* server side, the walk's end (review fold): the last write before "loads" shows the whole walk done - n = m (records added or gone
   while it ran: the larger of the two) */
inline void WalkDone(Row* r) { if (r->n > r->m) r->m = r->n; r->n = r->m; }
/* the capped log line: one per tenth of the walk (0..10) */
inline int TenthOf(long long n, long long m) { if (m <= 0) return 0; if (n >= m) return 10; return n <= 0 ? 0 : (int)((n * 10) / m); }
/* TEST ONLY (--walk-slow-sec): the wait before each walk entry so the walk takes about totalSec; capped at 10 s an entry so the
   progress still moves well inside kStallMs */
inline unsigned SlowEntryMs(double totalSec, long long m)
{
    if (totalSec <= 0.0 || m <= 0) return 0u;
    const double ms = totalSec * 1000.0 / (double)m;
    return ms >= 10000.0 ? 10000u : (unsigned)ms;
}
/* TEST ONLY (review fold): when entry n (1-based) of the slowed walk may begin, in seconds after the walk's start - n times the
   per-entry wait, not rounded, so a wait shorter than the sleep step does not stretch the walk (the server sleeps until this time) */
inline double SlowTargetSec(double totalSec, long long m, long long n)
{
    if (totalSec <= 0.0 || m <= 0 || n <= 0) return 0.0;
    double per = totalSec / (double)m;
    if (per > 10.0) per = 10.0;
    return per * (double)n;
}

}   /* namespace startprog */

#endif
