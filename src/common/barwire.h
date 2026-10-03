#pragma once
// refill1 (docs/design-refill1.md sections 2-4, user-approved 2026-09-26): BARS REFILL EVERY 5 IN-GAME DAYS.
// The pure half: the shared per-town bar record the notebook keeps in town_bars.txt (store protocol 50, TOWN_BAR 47),
// its wire form, its merge rule, and the two refill decisions (is a town due; how many squads each hire list is asked
// for). ONE header compiled into the plugin (towngen.cpp, store.cpp), the notebook (store_main.cpp) and the offline
// suite, so the three cannot hold different ideas of one record.
//
// A ROW, keyed by the town's FCS stringID:
//   usualSet     1 once the bar's usual size is recorded (the holder's first check-up after the town's first roll, or,
//                for a town first seen already rolled, its live count) - the LARGEST ever reported wins (review-refill1)
//   usualPeople  max(the free recruits counted then, the list's expected roll - BarUsualFromRoll below); the roll
//                already includes the host's recruit multiplier
//   usualSquads  hire squads with a free member counted then (the expected squads when the expected roll is larger)
//   lastFilled   absolute in-game hours of the last fill (the first roll or a top-up); < 0 = never. The LATEST wins.
// WIRE (TOWN_BAR, both ways): {u32 count 1..256, count x [u32 sidLen 1..128, sid bytes, u32 usualSet, u32 usualPeople,
//   u32 usualSquads, f64 lastFilled]}, little-endian as every store message.
// FILE LINE: v1<TAB>sid<TAB>usualSet<TAB>usualPeople<TAB>usualSquads<TAB>lastFilled
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace coopbar {

const double kRefillPeriodHours = 120.0;   // 5 in-game days (design s2 step 3)
const float  kRefillNearUnits = 250.0f;    // the nearby rule: a player character within this of the bar building (design s3)
const unsigned kBarMaxRows = 256, kBarMaxSid = 128, kBarMaxPeople = 10000, kBarMaxSquads = 1000;

struct BarRow
{
    unsigned usualSet, usualPeople, usualSquads;
    double lastFilled;
    BarRow() : usualSet(0), usualPeople(0), usualSquads(0), lastFilled(-1.0) {}
};
typedef std::map<std::string, BarRow> BarTable;
struct BarWireRow { std::string sid; BarRow row; };

inline bool BarSidOk(const std::string& s)
{
    if (s.empty() || s.size() > kBarMaxSid) return false;
    for (size_t i = 0; i < s.size(); ++i) if ((unsigned char)s[i] < 0x20) return false;   /* no tab, no newline: the file line stays one line */
    return true;
}
inline bool BarRowOk(const BarRow& r)
{
    if (r.usualSet > 1 || r.usualPeople > kBarMaxPeople || r.usualSquads > kBarMaxSquads) return false;
    if (!(r.lastFilled == r.lastFilled) || r.lastFilled > 1.0e9 || r.lastFilled < -1.0e9) return false;   /* NaN and absurd clocks refused */
    return true;
}

// The notebook's (and every game's) merge: 1 = the stored row changed. The usual size is the LARGEST ever reported
// (review-refill1: one chance roll can come out small; people decide, the squads come with them); the last-filled
// time moves only forward.
inline int BarMerge(BarTable* t, const std::string& sid, const BarRow& in)
{
    if (t == 0 || !BarSidOk(sid) || !BarRowOk(in)) return 0;
    BarTable::iterator it = t->find(sid);
    if (it == t->end()) { (*t)[sid] = in; return 1; }
    int changed = 0;
    BarRow& r = it->second;
    if (in.usualSet != 0 && (r.usualSet == 0 || in.usualPeople > r.usualPeople)) { r.usualSet = 1; r.usualPeople = in.usualPeople; r.usualSquads = in.usualSquads; changed = 1; }
    if (in.lastFilled > r.lastFilled) { r.lastFilled = in.lastFilled; changed = 1; }
    return changed;
}

inline void BarPutU32(std::vector<char>* b, unsigned v) { char c[4]; std::memcpy(c, &v, 4); b->insert(b->end(), c, c + 4); }
inline void BarPutF64(std::vector<char>* b, double v) { char c[8]; std::memcpy(c, &v, 8); b->insert(b->end(), c, c + 8); }
inline bool EncodeBarRows(std::vector<char>* out, const std::vector<BarWireRow>& rows)
{
    if (out == 0 || rows.empty() || rows.size() > kBarMaxRows) return false;
    std::vector<char> b;
    BarPutU32(&b, (unsigned)rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (!BarSidOk(rows[i].sid) || !BarRowOk(rows[i].row)) return false;
        BarPutU32(&b, (unsigned)rows[i].sid.size()); b.insert(b.end(), rows[i].sid.begin(), rows[i].sid.end());
        BarPutU32(&b, rows[i].row.usualSet); BarPutU32(&b, rows[i].row.usualPeople); BarPutU32(&b, rows[i].row.usualSquads);
        BarPutF64(&b, rows[i].row.lastFilled);
    }
    out->swap(b);
    return true;
}
// 1 = decoded (every row valid, nothing trailing), 0 = malformed (nothing is taken from it)
inline int DecodeBarRows(const char* p, size_t n, std::vector<BarWireRow>* out)
{
    if (out == 0) return 0;
    out->clear();
    if (p == 0 || n < 4) return 0;
    size_t at = 0; unsigned count = 0;
    std::memcpy(&count, p, 4); at = 4;
    if (count == 0 || count > kBarMaxRows) return 0;
    std::vector<BarWireRow> v;
    for (unsigned i = 0; i < count; ++i)
    {
        unsigned len = 0;
        if (n - at < 4) return 0;
        std::memcpy(&len, p + at, 4); at += 4;
        if (len == 0 || len > kBarMaxSid || n - at < (size_t)len + 20) return 0;
        BarWireRow w; w.sid.assign(p + at, len); at += len;
        std::memcpy(&w.row.usualSet, p + at, 4); std::memcpy(&w.row.usualPeople, p + at + 4, 4); std::memcpy(&w.row.usualSquads, p + at + 8, 4);
        std::memcpy(&w.row.lastFilled, p + at + 12, 8); at += 20;
        if (!BarSidOk(w.sid) || !BarRowOk(w.row)) return 0;
        v.push_back(w);
    }
    if (at != n) return 0;
    out->swap(v);
    return 1;
}
inline std::string BarLine(const std::string& sid, const BarRow& r)
{
    char b[96];
    _snprintf(b, 95, "\t%u\t%u\t%u\t%.6f\n", r.usualSet, r.usualPeople, r.usualSquads, r.lastFilled); b[95] = 0;
    return "v1\t" + sid + b;
}
// 1 = a row was read into the table (merged), 0 = the line is not a v1 row
inline int BarParseLine(const std::string& line0, BarTable* t)
{
    std::string line(line0);
    while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == '\n')) line.erase(line.size() - 1);
    if (line.compare(0, 3, "v1\t") != 0) return 0;
    std::vector<std::string> f;
    size_t s = 3;
    for (;;) { const size_t e = line.find('\t', s); if (e == std::string::npos) { f.push_back(line.substr(s)); break; } f.push_back(line.substr(s, e - s)); s = e + 1; }
    if (f.size() != 5) return 0;
    for (size_t i = 1; i < 4; ++i) { if (f[i].empty()) return 0; for (size_t k = 0; k < f[i].size(); ++k) if (f[i][k] < '0' || f[i][k] > '9') return 0; }
    if (f[4].empty()) return 0;
    char* end = 0;
    BarRow r;
    r.usualSet = (unsigned)std::strtoul(f[1].c_str(), 0, 10); r.usualPeople = (unsigned)std::strtoul(f[2].c_str(), 0, 10); r.usualSquads = (unsigned)std::strtoul(f[3].c_str(), 0, 10);
    r.lastFilled = std::strtod(f[4].c_str(), &end);
    if (end == 0 || *end != 0) return 0;
    if (!BarSidOk(f[0]) || !BarRowOk(r)) return 0;
    BarMerge(t, f[0], r);
    return 1;
}

// THE DUE DECISION (design s2 step 3; the nearby rule and the owed-list test are the caller's, they need the engine).
// 1 = due, 0 = not yet, -1 = no world clock. `forced` is the TEST verb `refill now`: it skips the timer only.
inline int RefillDue(double nowHours, double lastFilled, int forced)
{
    if (nowHours < 0.0) return -1;
    if (forced != 0) return 1;
    if (lastFilled < 0.0) return 1;   /* recorded but never stamped: nothing to wait for */
    return (nowHours - lastFilled >= kRefillPeriodHours) ? 1 : 0;
}
// refill3 (T382): the rate limit of a forced town's "skipped at its check-up" line - the first is shown at once, then one per
// kForcedSkipLineMs of the tick clock (compared as a difference, so a wrapped GetTickCount is fine).
const unsigned kForcedSkipLineMs = 30000;
inline int ForcedSkipLineDue(unsigned nowMs, unsigned lastMs, int logged)
{
    if (logged == 0) return 1;
    return (nowMs - lastMs >= kForcedSkipLineMs) ? 1 : 0;
}
// missing -> hire squads wanted: ceil(missing / (usual people / usual squads)) = ceil(missing x squads / people).
inline int RefillSquadsWanted(int usualPeople, int usualSquads, int freeNow)
{
    if (usualPeople <= 0 || usualSquads <= 0) return 0;
    const int missing = usualPeople - (freeNow < 0 ? 0 : freeNow);
    if (missing <= 0) return 0;
    const long long w = ((long long)missing * usualSquads + usualPeople - 1) / usualPeople;
    return (w > 100000) ? 100000 : (int)w;
}
// The wanted squads SPREAD over the hire entries in list order, each at most its own original count x the multiplier;
// every non-hire entry (and a hire entry with no count of its own) is 0, so uniques and other bar squads are never
// rolled. Returns the squads asked for in total (<= wanted).
inline int RefillEntryCounts(int wanted, const int* orig, const unsigned char* hire, int n, int mult, int* out)
{
    int left = wanted < 0 ? 0 : wanted;
    if (mult < 1) mult = 1;
    for (int i = 0; i < n; ++i)
    {
        out[i] = 0;
        if (hire[i] == 0 || orig[i] <= 0 || left <= 0) continue;
        const int cap = orig[i] * mult;
        const int c = (left < cap) ? left : cap;
        out[i] = c; left -= c;
    }
    return (wanted < 0 ? 0 : wanted) - left;
}
// The nearby rule's geometry: 1 = (px,pz) is within r of any of the nb bar positions (ground plane).
inline int RefillNearAny(const float* bx, const float* bz, int nb, float px, float pz, float r)
{
    for (int i = 0; i < nb; ++i) { const float dx = px - bx[i], dz = pz - bz[i]; if (dx * dx + dz * dz <= r * r) return 1; }
    return 0;
}

// THE EXPECTED ROLL of one bar list entry (review-refill1 item 3; recruit-refill-read R4; decomp 9fbe50:71-96), count c
// and chance p %: a floor g = int(p x 0.5 x 0.01 x c) squads when c > 2 (else 0), then one more squad per d100 at or under
// p, STOPPING AT THE FIRST MISS or at c. Expected squads = g + (q + q^2 + ... + q^(c-g)), q = p / 100 - not g + (c-g) x q,
// because the roll stops at the first miss.
inline double BarExpectedSquads(int count, int chance)
{
    if (count <= 0) return 0.0;
    if (count > 10000) count = 10000;
    if (chance < 0) chance = 0;
    if (chance > 100) chance = 100;
    int g = 0;
    if (count > 2) g = (int)((double)chance * 0.5 * 0.01 * (double)count);
    if (g >= count) return (double)count;
    const double q = (double)chance / 100.0;
    double sum = 0.0, qk = 1.0;
    for (int k = g; k < count; ++k) { qk *= q; sum += qk; }
    return (double)g + sum;
}
// THE USUAL SIZE the holder reports (review-refill1 item 3): people = max(the roll's own count, the expected roll), the
// expected roll = expected squads x people per squad (the roll's people / squads, or 3 when it made no squad). When the
// expected roll is the larger, the squads are the expected squads rounded (at least 1), so the people-per-squad ratio the
// top-up divides by stays the roll's own. expectedSquads < 0 = not known: the count as it stands.
inline void BarUsualFromRoll(int rollPeople, int rollSquads, double expectedSquads, unsigned* people, unsigned* squads)
{
    if (rollPeople < 0) rollPeople = 0;
    if (rollSquads < 0) rollSquads = 0;
    *people = (unsigned)rollPeople; *squads = (unsigned)rollSquads;
    if (!(expectedSquads > 0.0)) return;
    if (expectedSquads > (double)kBarMaxSquads) expectedSquads = (double)kBarMaxSquads;
    const double pps = (rollSquads > 0 && rollPeople > 0) ? (double)rollPeople / (double)rollSquads : 3.0;
    double epd = expectedSquads * pps + 0.5;
    if (epd > (double)kBarMaxPeople) epd = (double)kBarMaxPeople;
    const int ep = (int)epd;
    if (ep <= rollPeople) return;
    int es = (int)(expectedSquads + 0.5);
    if (es < 1) es = 1;
    *people = (unsigned)ep; *squads = (unsigned)es;
}

// THE USUAL SIZE FROM A LIVE COUNT (refill2; T374: The Hub recorded usual=1 from=live with no expected-roll floor because
// its bar list was not known yet). expectedSquads < 0 = the list is not known: 0, nothing recorded - a later check-up asks
// again (usualWaitList). Otherwise 1 and *people / *squads = BarUsualFromRoll: max(the live count, the expected roll).
inline int BarUsualFromLive(int livePeople, int liveSquads, double expectedSquads, unsigned* people, unsigned* squads)
{
    *people = 0; *squads = 0;
    if (!(expectedSquads >= 0.0)) return 0;
    BarUsualFromRoll(livePeople, liveSquads, expectedSquads, people, squads);
    return 1;
}

}   // namespace coopbar
