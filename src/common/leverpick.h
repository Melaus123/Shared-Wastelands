/* src/common/leverpick.h - T-1218 (owner decision 1440 a, 2026-11-03): the one-target rule for every TEST-ONLY command that picks
 * its target by name, key or template.
 *
 * A pick by name or key used to take the nearest or the first-loaded match, so a request that named "Dust Bandit" or a key fragment
 * shared by two buildings acted on whichever one the walk met first (runs T1421, T1555, T1801: the wrong object). Now:
 *   0 matches          - refused by the caller in its own "no such ..." words (kNone);
 *   exactly 1 match    - picked (kOne);
 *   several matches    - refused (kSeveral), naming the count and the first kShowMax matches in the resolver's order;
 *   `<text>#<k>`       - (manager's decision 2026-11-03) picks the k-th match (1-based) in the resolver's stable order: nearest first
 *                        for a pick by a character's name, load order (the walk's own order) for a pick by key; a k beyond the
 *                        matches is refused naming the count (kOutOfRange).
 * Tiers: a resolver may rank its matches (Add's tier: lower wins). Only the lowest tier present is counted, so an EXACT key match
 * (tier 0) wins over substring matches (tier 1) and a request naming a full key keeps working however many keys contain it.
 *
 * Pure: no engine memory, no Windows; the offline suite runs the same functions. C++03 (VS2010 v100).
 */
#pragma once

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace leverpick {

const int kShowMax = 5;     /* matches named in a refusal */
const int kSelMax  = 999;   /* the largest #k */

const int kNone       = 0;  /* nothing matched: the caller's own words */
const int kOne        = 1;  /* exactly one match, or #k within the matches: picked */
const int kSeveral    = 2;  /* several matches and no #k: refused */
const int kOutOfRange = 3;  /* #k beyond the matches: refused */

/* `<text>#<k>` -> *base = <text> (trailing spaces / tabs cut), *k = k. 0 = no selector (*base = sel, *k = 0; a '#' followed by
   anything but digits is part of the text); 1 = a selector; -1 = a malformed one: "#" with no digits, k = 0, k > kSelMax, or no
   text before it (*base = sel, *k = 0). */
inline int SplitSelector(const std::string& sel, std::string* base, int* k)
{
    *k = 0; *base = sel;
    const std::string::size_type h = sel.rfind('#');
    if (h == std::string::npos) return 0;
    const std::string digits = sel.substr(h + 1);
    if (digits.empty()) return -1;
    for (size_t i = 0; i < digits.size(); ++i) if (digits[i] < '0' || digits[i] > '9') return 0;
    if (digits.size() > 3) return -1;
    int v = 0;
    for (size_t i = 0; i < digits.size(); ++i) v = v * 10 + (digits[i] - '0');
    std::string b = sel.substr(0, h);
    while (!b.empty() && (b[b.size() - 1] == ' ' || b[b.size() - 1] == '\t')) b.erase(b.size() - 1);
    if (v < 1 || v > kSelMax || b.empty()) return -1;
    *base = b; *k = v;
    return 1;
}

/* The rule: count = the matches counted (the lowest tier's), k = the #k (0 = none). */
inline int Verdict(long count, int k)
{
    if (count <= 0) return kNone;
    if (k <= 0) return (count == 1) ? kOne : kSeveral;
    return (k <= count) ? kOne : kOutOfRange;
}

inline std::string Num(long v) { char b[24]; std::sprintf(b, "%ld", v); return b; }

/* "a, b, c" - the first kShowMax names, then " and N more" when count is larger. */
inline std::string ShowList(const std::vector<std::string>& names, long count)
{
    std::string r;
    long shown = 0;
    for (size_t i = 0; i < names.size() && shown < kShowMax; ++i, ++shown) { if (i) r += ", "; r += names[i]; }
    if (count > shown) r += " and " + Num(count - shown) + " more";
    return r;
}

/* The refusal words (the caller prefixes its `error <verb>: ` or its module's own line head). what = the matches' description
   ("characters named 'dust bandit' within 1000 units"), order = the order words ("nearest first", "load order").
   kSeveral:    "3 characters named 'x' ... match (nearest first): a, b, c - exactly one must match; add #1..#3 to pick one"
   kOutOfRange: "#5 asked but only 3 characters named 'x' ... match (nearest first): a, b, c" */
inline std::string Refusal(int verdict, long count, int k, const std::string& what, const char* order, const std::vector<std::string>& firstNames)
{
    const std::string tail = std::string(" match (") + order + "): " + ShowList(firstNames, count);
    if (verdict == kSeveral) return Num(count) + " " + what + tail + " - exactly one must match; add #1..#" + Num(count) + " to pick one";
    if (verdict == kOutOfRange) return "#" + Num(k) + " asked but only " + Num(count) + " " + what + tail;
    return std::string();
}

inline std::string BadSelector(const std::string& sel)
{
    return "a malformed #<k> in '" + sel + "' (#1..#" + Num(kSelMax) + " after the name or key)";
}

/* The matches of one pick. Add every candidate that matches; Decide applies the rule over the lowest tier present, the rows sorted
   by `order` ascending (equal orders keep the order they were added in). T is copied: a pointer, an index or a std::pair. */
template <class T>
class Picker
{
public:
    struct Row { int tier; double order; unsigned long seq; T item; std::string name; };
    explicit Picker(int k) : k_(k), seq_(0) {}
    void Add(const T& item, int tier, double order, const std::string& name)
    {
        Row r; r.tier = tier; r.order = order; r.seq = seq_++; r.item = item; r.name = name;
        rows_.push_back(r);
    }
    /* the matches counted: those of the lowest tier present */
    long Count() const
    {
        long n = 0; const int t = BestTier();
        for (size_t i = 0; i < rows_.size(); ++i) if (rows_[i].tier == t) ++n;
        return n;
    }
    long Total() const { return (long)rows_.size(); }
    /* kOne: *out = the pick (*outRow, when given, its row). kSeveral / kOutOfRange: *why = the refusal. kNone: nothing set. */
    int Decide(T* out, const std::string& what, const char* order, std::string* why, Row* outRow = 0) const
    {
        std::vector<Row> use;
        const int t = BestTier();
        for (size_t i = 0; i < rows_.size(); ++i) if (rows_[i].tier == t) use.push_back(rows_[i]);
        std::stable_sort(use.begin(), use.end(), Less());
        const int v = Verdict((long)use.size(), k_);
        if (v == kOne)
        {
            const Row& r = use[(k_ > 0) ? (size_t)(k_ - 1) : 0];
            *out = r.item;
            if (outRow != 0) *outRow = r;
        }
        else if (v != kNone)
        {
            std::vector<std::string> names;
            for (size_t i = 0; i < use.size() && (int)i < kShowMax; ++i) names.push_back(use[i].name);
            *why = Refusal(v, (long)use.size(), k_, what, order, names);
        }
        return v;
    }
private:
    struct Less { bool operator()(const Row& a, const Row& b) const { return a.order < b.order; } };
    int BestTier() const
    {
        int t = 0x7FFFFFFF;
        for (size_t i = 0; i < rows_.size(); ++i) if (rows_[i].tier < t) t = rows_[i].tier;
        return t;
    }
    int k_;
    unsigned long seq_;
    std::vector<Row> rows_;
};

}   /* namespace leverpick */
