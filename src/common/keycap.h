/* keycap.h - T-1401 (owner 1735 a / 1736): A LOG LINE'S CAP PER KEY, THEN ONE SUMMARY A MINUTE PER KEY.
 *
 * Bug reports since 0.4.0 stayed unsolved because a shared line budget ran out (the building log's 60 lines, spent by hits) or a
 * line was written once per session and never again (the [ITEMS] box lines). A Cap gives each KEY (a building piece, a box, a door,
 * one kind of line) its own budget: the first `first` lines of a key are written in full; after that the key's lines are counted
 * and, at most once per `periodMs`, one summary says how many were not written and of which kinds (an attacker's faction, an
 * outcome). A key that goes quiet has its count written by Flush (a periodic call, and one before a report): nothing is lost
 * silently. A key whose lines stopped for a period starts a new window at its next line (its summary comes a period later), so one
 * line an hour after a burst is not reported on its own at once.
 *
 * Judge(cap, key, nowMs, kind, &summary): kWrite = write this line in full; kHeld = do not; kSummaryDue = do not write it in full
 * (or write it with the count - the caller's choice) and write the summary now (it counts this line). The table holds maxKeys keys;
 * every key after that shares one row (OverflowKey). LineKind: the words of a line before its first '=', ':' or '(' - the key of
 * "one kind of line" when a caller has no better one. AttackerKind: the short word a summary groups attackers by. FirstRise: which
 * counter of a list rose during a call (the reason a box change was refused).
 *
 * PURE: shared with the offline suite. Not thread-safe (each caller's tick owns its Cap). C++03 (VS2010 v100).
 */
#ifndef SW_KEYCAP_H
#define SW_KEYCAP_H

#include <string>
#include <vector>
#include <map>
#include <cstdio>

namespace swkeycap {

const unsigned int kMinuteMs = 60000u;
enum { kHeld = 0, kWrite = 1, kSummaryDue = 2 };
enum { kKindsKept = 6, kLineKindCap = 60 };

struct Row
{
    unsigned int written;        /* lines of this key written in full */
    unsigned int held;           /* lines not written since the last full line or summary */
    unsigned int since;          /* when the window of the held lines started */
    unsigned long long seen;     /* every line of this key */
    std::vector<std::pair<std::string, unsigned int> > kinds;   /* the held lines' kinds, in first-seen order */
    unsigned int kindsOther;     /* held lines whose kind did not fit in kKindsKept */
    Row() : written(0), held(0), since(0), seen(0), kindsOther(0) {}
};

struct Cap
{
    unsigned int first;          /* lines per key written in full */
    unsigned int periodMs;       /* at most one summary per key per this */
    size_t maxKeys;
    std::map<std::string, Row> rows;
    unsigned long long full, held, summaries;   /* totals for a REPORT line */
    Cap(unsigned int f, size_t mk, unsigned int period = kMinuteMs) : first(f), periodMs(period), maxKeys(mk), full(0), held(0), summaries(0) {}
};

struct Summary
{
    std::string key;
    unsigned int held;           /* lines not written in the window (the judged line included) */
    unsigned int spanMs;         /* the window's length */
    std::string kinds;           /* "'A' x3, 'B' x1, +2 of other kinds"; "" when no kind was given */
    Summary() : held(0), spanMs(0) {}
};

inline const char* OverflowKey() { return "(other keys - the table is full)"; }

inline Row& RowOf(Cap* c, const std::string& key)
{
    std::map<std::string, Row>::iterator it = c->rows.find(key);
    if (it != c->rows.end()) return it->second;
    if (c->rows.size() >= c->maxKeys) return c->rows[std::string(OverflowKey())];
    return c->rows[key];
}

inline void NoteKind(Row* r, const std::string& kind)
{
    if (kind.empty()) return;
    for (size_t i = 0; i < r->kinds.size(); ++i)
        if (r->kinds[i].first == kind) { ++r->kinds[i].second; return; }
    if (r->kinds.size() < (size_t)kKindsKept) r->kinds.push_back(std::make_pair(kind, 1u));
    else ++r->kindsOther;
}

inline std::string KindsText(const Row& r)
{
    std::string s;
    char n[32];
    for (size_t i = 0; i < r.kinds.size(); ++i)
    {
        std::sprintf(n, " x%u", r.kinds[i].second);
        if (i != 0) s += ", ";
        s += r.kinds[i].first + n;
    }
    if (r.kindsOther != 0)
    {
        std::sprintf(n, "+%u of other kinds", r.kindsOther);
        if (!s.empty()) s += ", ";
        s += n;
    }
    return s;
}

inline void TakeSummary(Cap* c, Row* r, const std::string& key, unsigned int nowMs, Summary* out)
{
    if (out != 0) { out->key = key; out->held = r->held; out->spanMs = nowMs - r->since; out->kinds = KindsText(*r); }
    r->held = 0; r->kinds.clear(); r->kindsOther = 0; r->since = nowMs;
    ++c->summaries;
}

inline int Judge(Cap* c, const std::string& key, unsigned int nowMs, const std::string& kind, Summary* out)
{
    Row& r = RowOf(c, key);
    ++r.seen;
    if (r.written < c->first) { ++r.written; ++c->full; r.since = nowMs; return kWrite; }
    ++c->held;
    ++r.held;
    NoteKind(&r, kind);
    if (r.held == 1 && nowMs - r.since >= c->periodMs) { r.since = nowMs; return kHeld; }   /* a new burst: its window starts now */
    if (nowMs - r.since >= c->periodMs)
    {
        const std::string k = (c->rows.find(key) != c->rows.end()) ? key : std::string(OverflowKey());
        TakeSummary(c, &r, k, nowMs, out);
        return kSummaryDue;
    }
    return kHeld;
}

/* Every key holding lines whose window is a period old (all: every key holding lines), one Summary each, the count started again. */
inline void Flush(Cap* c, unsigned int nowMs, bool all, std::vector<Summary>* out)
{
    for (std::map<std::string, Row>::iterator it = c->rows.begin(); it != c->rows.end(); ++it)
    {
        Row& r = it->second;
        if (r.held == 0) continue;
        if (!all && nowMs - r.since < c->periodMs) continue;
        Summary s;
        TakeSummary(c, &r, it->first, nowMs, &s);
        if (out != 0) out->push_back(s);
    }
}

/* The words of a line before its first '=', ':' or '(' (spaces at the end dropped), at most kLineKindCap characters. */
inline std::string LineKind(const char* line)
{
    if (line == 0) return std::string();
    size_t n = 0;
    while (line[n] != 0 && line[n] != '=' && line[n] != ':' && line[n] != '(' && n < (size_t)kLineKindCap) ++n;
    while (n > 0 && line[n - 1] == ' ') --n;
    return std::string(line, n);
}

/* A summary's word for who swung a hit: known 1 = read (its faction, else its name), 0 = the engine named none, else none carried. */
inline std::string AttackerKind(int known, const char* faction, const char* name)
{
    if (known == 1)
    {
        if (faction != 0 && faction[0] != 0) return std::string("faction '") + faction + "'";
        if (name != 0 && name[0] != 0) return std::string("'") + name + "' (faction not read)";
        return "a character not read";
    }
    if (known == 0) return "no attacker named by the engine";
    return "no attacker carried";
}

/* The first counter of a list that rose between before[] and after[]; -1 when none did. */
inline int FirstRise(const long long* before, const long long* after, int n)
{
    if (before == 0 || after == 0) return -1;
    for (int i = 0; i < n; ++i) if (after[i] > before[i]) return i;
    return -1;
}

}   /* namespace swkeycap */

#endif
