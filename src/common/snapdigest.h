/* src/common/snapdigest.h - snap1 (user decision 2026-09-26: an automatic parity checker on every test run). The two pure
 * pieces of the read-only `snapshot [<tag>]` verb, kept here so the offline suite checks the code the plugin runs:
 *
 *   SnapDigest     - an ORDER-FREE 32-bit digest of a list of (item sid, section, quantity) rows. The rows are SORTED first,
 *                    then FNV-1a runs over "sid 0x1F section 0x1F qty(4 bytes) 0x1E" for each row. An empty list is 0 and a
 *                    non-empty list is never 0. The same items in any order give the same 8 hex digits on both games; one
 *                    quantity, one section or one row more or less changes them. A character's backpack contents ride in
 *                    the same list with the section "<bag section>/<inner section>".
 *   SnapNextMinute - the next WHOLE in-game minute strictly after `hours` (absolute world hours), in hours; -1 when the
 *                    clock is unknown (hours < 0).
 *   SnapTargetMinute - snap2: the DEFAULT target of `snapshot <tag>` - the next whole in-game minute at least
 *                    kSnapLeadMinutes (3) in-game minutes after `hours`. The two games are NOT sent the verb in the same
 *                    second: the harness sends A, waits for A's ACK, then sends B (T378: 2-3 real seconds, ~1.6 in-game
 *                    minutes), so the harness now reads A's `armed target=` and sends B `snapshot <tag> at <target>`;
 *                    the lead is what keeps a plain `snapshot` on both games on one minute when that hand-over fails.
 *   SnapParseArgs  - snap2: `<tag> [at <world hours>] [detail <uid>]`, the two options in either order.
 *
 * Pure: no engine memory, no Windows. C++03 (VS2010 v100).
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace coopsnap {

struct SnapRow
{
    std::string sid;
    std::string section;
    int qty;
    SnapRow() : qty(0) {}
    SnapRow(const std::string& s, const std::string& sec, int q) : sid(s), section(sec), qty(q) {}
};

inline bool SnapRowLess(const SnapRow& a, const SnapRow& b)
{
    if (a.sid != b.sid) return a.sid < b.sid;
    if (a.section != b.section) return a.section < b.section;
    return a.qty < b.qty;
}

inline unsigned int SnapFnvByte(unsigned int h, unsigned char b) { h ^= b; h *= 16777619u; return h; }

inline unsigned int SnapFnvText(unsigned int h, const std::string& s)
{
    for (size_t i = 0; i < s.size(); ++i) h = SnapFnvByte(h, (unsigned char)s[i]);
    return h;
}

/* By value on purpose: the copy is sorted, the caller's order is left alone. */
inline unsigned int SnapDigest(std::vector<SnapRow> rows)
{
    if (rows.empty()) return 0;
    std::sort(rows.begin(), rows.end(), SnapRowLess);
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        h = SnapFnvText(h, rows[i].sid);
        h = SnapFnvByte(h, 0x1F);
        h = SnapFnvText(h, rows[i].section);
        h = SnapFnvByte(h, 0x1F);
        const unsigned int q = (unsigned int)rows[i].qty;
        for (int k = 0; k < 4; ++k) h = SnapFnvByte(h, (unsigned char)((q >> (8 * k)) & 0xFFu));
        h = SnapFnvByte(h, 0x1E);
    }
    return (h == 0) ? 1u : h;   /* 0 is kept for "empty" */
}

inline double SnapNextMinute(double hours)
{
    if (!(hours >= 0.0)) return -1.0;
    const double m = std::floor(hours * 60.0 + 1e-6);
    return (m + 1.0) / 60.0;
}

/* snap2: a boundary exactly kSnapLeadMinutes ahead counts ("at least"); -1 when the clock is unknown. */
const double kSnapLeadMinutes = 3.0;
inline double SnapTargetMinute(double hours)
{
    if (!(hours >= 0.0)) return -1.0;
    const double m = std::ceil(hours * 60.0 + kSnapLeadMinutes - 1e-6);
    return m / 60.0;
}

struct SnapArgs
{
    std::string tag;      /* "-" when none was given (snap1's default) */
    int haveAt;           /* 1 = `at <hours>` was given: sample at that exact world instant */
    double at;
    unsigned int detailUid;   /* 0 = no `detail`; else one [SNAP] item line per item of this character */
    SnapArgs() : tag("-"), haveAt(0), at(-1.0), detailUid(0) {}
};

/* Splits on spaces and tabs. Returns "" when the text parsed, else what is wrong with it (the tag's characters are the
   caller's check). A number must be the whole token: `at 35.9x` and `detail 12a` are refused, as are a negative `at`,
   `detail 0`, a uid over 32 bits, an option given twice, and any other word. */
inline std::string SnapParseArgs(const std::string& in, SnapArgs* out)
{
    *out = SnapArgs();
    std::vector<std::string> tok;
    std::string cur;
    for (size_t i = 0; i <= in.size(); ++i)
    {
        const char ch = (i < in.size()) ? in[i] : ' ';
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') { if (!cur.empty()) { tok.push_back(cur); cur.clear(); } }
        else cur += ch;
    }
    if (tok.empty()) return std::string();
    out->tag = tok[0];
    int haveDetail = 0;
    for (size_t i = 1; i < tok.size(); i += 2)
    {
        const std::string& k = tok[i];
        if (k != "at" && k != "detail") return "unknown word '" + k + "'";
        if (i + 1 >= tok.size()) return "'" + k + "' needs a value";
        const std::string& v = tok[i + 1];
        if (k == "at")
        {
            if (out->haveAt != 0) return "'at' given twice";
            char* end = 0;
            const double h = std::strtod(v.c_str(), &end);
            if (end == v.c_str() || *end != 0 || !(h >= 0.0) || h > 1.0e9) return "'at' needs world hours (a number >= 0), not '" + v + "'";
            out->haveAt = 1; out->at = h;
        }
        else
        {
            if (haveDetail != 0) return "'detail' given twice";
            if (v.empty() || v.size() > 10) return "'detail' needs a character uid, not '" + v + "'";
            unsigned long long u = 0;
            for (size_t c = 0; c < v.size(); ++c)
            {
                if (v[c] < '0' || v[c] > '9') return "'detail' needs a character uid, not '" + v + "'";
                u = u * 10u + (unsigned long long)(v[c] - '0');
            }
            if (u == 0 || u > 0xFFFFFFFFull) return "'detail' needs a character uid, not '" + v + "'";
            haveDetail = 1; out->detailUid = (unsigned int)u;
        }
    }
    return std::string();
}

}   /* namespace coopsnap */
