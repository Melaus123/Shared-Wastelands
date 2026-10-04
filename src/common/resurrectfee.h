/* src/common/resurrectfee.h - T-556: the resurrection fee and the host options behind it. Pure: no engine memory, no Windows.
 * One header compiled into the plugin (resurrect.cpp), the world server (store_main.cpp OptionValueOk) and the offline suite
 * (src/coop-test/test_main.cpp), so the three cannot hold different ideas of the keys, their legal values or the price.
 *
 * HOST OPTIONS (owner 485, 491, 497): kept by the world server in options.txt, set by the host alone (OnOptions), carried to
 * every game in the OPTIONS map. A key the map does not name reads as its default.
 *   resurrect        on | off                  absent = off (bringing characters back is the host's choice)
 *   resurrectfee     0 .. 2147483647 (digits)  absent = kDefaultAmount; the host's typed amount
 *   resurrectgrowth  steady | steep            absent = steep (owner 497)
 *
 * THE PRICE (owner 494, 497). A = how many of this player's brought-back characters are ALIVE right now (one that died again
 * does not count). steady = amount x A; steep = amount x A x A; A = 0 -> free. The price never exceeds kMoneyMax (the engine's
 * money field is a signed 32-bit int); a price that would is capped there and marked capped.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once

#include <sstream>
#include <string>

namespace swfee {

const char* const kKeyOn = "resurrect";
const char* const kKeyAmount = "resurrectfee";
const char* const kKeyGrowth = "resurrectgrowth";
const long long kMoneyMax = 2147483647LL;   /* the money field's largest value */
const long long kDefaultAmount = 1000;      /* the amount a world whose host never typed one uses */
const int kGrowthSteady = 0;
const int kGrowthSteep = 1;
const int kDefaultGrowth = kGrowthSteep;

/* "on" -> 1, "off" -> 0, anything else -1 */
inline int OnCode(const std::string& v) { return v == "on" ? 1 : v == "off" ? 0 : -1; }
/* "steady" / "steep" -> the growth code, anything else -1 */
inline int GrowthCode(const std::string& v) { return v == "steady" ? kGrowthSteady : v == "steep" ? kGrowthSteep : -1; }
inline const char* GrowthName(int g) { return g == kGrowthSteady ? "steady" : g == kGrowthSteep ? "steep" : "?"; }
/* the amount's text -> 0 .. kMoneyMax; -1 = not one: empty, a character other than 0-9, a leading zero before another digit,
   more than 10 digits, or a value over kMoneyMax */
inline long long AmountCode(const std::string& v)
{
    if (v.empty() || v.size() > 10 || (v.size() > 1 && v[0] == '0')) return -1;
    long long n = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (v[i] < '0' || v[i] > '9') return -1;
        n = n * 10 + (v[i] - '0');
    }
    return n <= kMoneyMax ? n : -1;
}
inline bool IsOptionKey(const std::string& k) { return k == kKeyOn || k == kKeyAmount || k == kKeyGrowth; }
/* the world server's check: one of the three keys with a legal value */
inline bool OptionValueOk(const std::string& k, const std::string& v)
{
    if (k == kKeyOn) return OnCode(v) >= 0;
    if (k == kKeyAmount) return AmountCode(v) >= 0;
    if (k == kKeyGrowth) return GrowthCode(v) >= 0;
    return false;
}

/* The price and its parts (the FALLEN tab explains each price from these). */
struct Price
{
    long long amount;   /* the host's amount */
    int growth;         /* kGrowthSteady / kGrowthSteep */
    int alive;          /* A: this player's brought-back characters alive now */
    long long price;    /* what this bring-back costs, 0 .. kMoneyMax */
    int capped;         /* 1 = the rule's number was over kMoneyMax and the price is kMoneyMax */
    Price() : amount(0), growth(kDefaultGrowth), alive(0), price(0), capped(0) {}
};
/* An amount below 0 reads as 0, over kMoneyMax as kMoneyMax; A below 0 as 0; a growth other than steady reads as steep. */
inline Price PriceFor(long long amount, int growth, int alive)
{
    Price p;
    p.amount = amount < 0 ? 0 : amount > kMoneyMax ? kMoneyMax : amount;
    p.growth = growth == kGrowthSteady ? kGrowthSteady : kGrowthSteep;
    p.alive = alive < 0 ? 0 : alive;
    if (p.amount == 0 || p.alive == 0) return p;
    long long v = p.amount;
    const int times = p.growth == kGrowthSteep ? 2 : 1;
    for (int i = 0; i < times; ++i)
    {
        if (v > kMoneyMax / p.alive) { v = kMoneyMax; p.capped = 1; break; }
        v *= p.alive;
    }
    p.price = v;
    return p;
}
/* The rule with its numbers, for log lines: "steep: 1000 x 2 x 2 = 4000", "steady: 1000 x 3 = 3000", "free: none alive". */
inline std::string RuleWords(const Price& p)
{
    std::ostringstream o;
    if (p.alive == 0) { o << GrowthName(p.growth) << ": free (no brought-back character alive)"; return o.str(); }
    o << GrowthName(p.growth) << ": " << p.amount << " x " << p.alive;
    if (p.growth == kGrowthSteep) o << " x " << p.alive;
    o << " = " << p.price;
    if (p.capped) o << " (capped at the money field's largest value)";
    return o.str();
}

} /* namespace swfee */
