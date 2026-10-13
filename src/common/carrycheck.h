/* src/common/carrycheck.h - what RESET WORLD carries for one player, made comparable. The research, the map knowledge and the
 * faction standings one game holds for its player are each reduced to a count and a sum, so a log line written before the reset
 * and one written after it compare exactly. Used by the TEST-ONLY `carryread` / `carryset` levers (store.cpp).
 *
 * THE SUM: FNV-1a 32 (basis 0x811C9DC5, prime 0x01000193) over the set's lines SORTED, each line followed by '\n'. The engine's
 * own order never moves it, a line held twice counts twice, and the same lines in any order give the same sum. A float enters
 * a line as its 32 bits in hex, so the sum is exact (no rounding, no locale); the readable row lines carry %.9g beside it.
 *
 * Lines: research = the tech's stringID; map = "<town stringID> d<discovered byte> e<explored byte>" for a town with either byte
 * set; standings = "<faction stringID> <rel> <trust> <trustNeg> f<flags> | <relBack> <trustBack> <trustNegBack> f<flagsBack>"
 * (the first half this player's faction towards that faction, the second that faction towards this player's).
 *
 * Pure: no engine memory, no Windows; the offline suite runs it. C++03 (VS2010 v100).
 */
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace swcarry {

const unsigned int kCarryBasis   = 2166136261u;   /* FNV-1a 32 basis 0x811C9DC5 */
const unsigned int kCarryPrime   = 16777619u;     /* FNV-1a 32 prime 0x01000193 */
const size_t       kCarryTagMax  = 32;   /* `carryread <tag>`: the word that pairs a before line with an after line */
const size_t       kCarryListMax = 12;   /* a set is listed on its summary line when it holds at most this many lines */

inline unsigned int CarryFnv(unsigned int h, const std::string& s)
{
    for (size_t i = 0; i < s.size(); ++i) { h ^= (unsigned char)s[i]; h *= kCarryPrime; }
    return h;
}
inline unsigned int CarrySum(const std::vector<std::string>& linesIn)
{
    std::vector<std::string> v(linesIn);
    std::sort(v.begin(), v.end());
    unsigned int h = kCarryBasis;
    const std::string nl("\n");
    for (size_t i = 0; i < v.size(); ++i) { h = CarryFnv(h, v[i]); h = CarryFnv(h, nl); }
    return h;
}
inline std::string CarryHex(unsigned int h) { char b[16]; std::sprintf(b, "%08x", h); return std::string(b); }
inline unsigned int CarryFloatBits(float f) { unsigned int u = 0; std::memcpy(&u, &f, sizeof(u)); return u; }
inline std::string CarryFloatText(float f) { char b[48]; std::sprintf(b, "%.9g", (double)f); return std::string(b); }
inline std::string CarryInt(long long v) { char b[32]; std::sprintf(b, "%lld", v); return std::string(b); }

/* the set on its summary line: "[a, b, c]" sorted when it holds at most kCarryListMax lines, else "(N, not listed)" */
inline std::string CarryList(const std::vector<std::string>& linesIn)
{
    if (linesIn.size() > kCarryListMax) return "(" + CarryInt((long long)linesIn.size()) + ", not listed)";
    std::vector<std::string> v(linesIn);
    std::sort(v.begin(), v.end());
    std::string o("[");
    for (size_t i = 0; i < v.size(); ++i) o += (i ? ", " : "") + v[i];
    return o + "]";
}

inline std::string CarryMapLine(const std::string& sid, int disc, int expl)
{
    return sid + " d" + CarryInt(disc) + " e" + CarryInt(expl);
}

struct CarryFac
{
    std::string sid;
    float rel, trust, trustNeg, relBack, trustBack, trustNegBack;
    int   flags, flagsBack;
    CarryFac() : rel(0), trust(0), trustNeg(0), relBack(0), trustBack(0), trustNegBack(0), flags(0), flagsBack(0) {}
};
/* the line the sum is taken over: every float as its bits */
inline std::string CarryFacSumLine(const CarryFac& f)
{
    return f.sid + " " + CarryHex(CarryFloatBits(f.rel)) + " " + CarryHex(CarryFloatBits(f.trust)) + " " + CarryHex(CarryFloatBits(f.trustNeg))
           + " f" + CarryInt(f.flags) + " | " + CarryHex(CarryFloatBits(f.relBack)) + " " + CarryHex(CarryFloatBits(f.trustBack)) + " "
           + CarryHex(CarryFloatBits(f.trustNegBack)) + " f" + CarryInt(f.flagsBack);
}
/* the readable row line */
inline std::string CarryFacText(const CarryFac& f)
{
    return f.sid + " mine->it rel=" + CarryFloatText(f.rel) + " trust=" + CarryFloatText(f.trust) + " trustNeg=" + CarryFloatText(f.trustNeg)
           + " flags=" + CarryInt(f.flags) + " | it->mine rel=" + CarryFloatText(f.relBack) + " trust=" + CarryFloatText(f.trustBack)
           + " trustNeg=" + CarryFloatText(f.trustNegBack) + " flags=" + CarryInt(f.flagsBack);
}
/* a row worth printing on its own: any value or flag not zero (each faction's starting standing comes from the game data, so
   "not zero" is the only default this header can know) */
inline int CarryFacNonZero(const CarryFac& f)
{
    return (f.rel != 0.0f || f.trust != 0.0f || f.trustNeg != 0.0f || f.relBack != 0.0f || f.trustBack != 0.0f || f.trustNegBack != 0.0f
            || f.flags != 0 || f.flagsBack != 0) ? 1 : 0;
}

/* the words of a lever's argument, split on blanks */
inline std::vector<std::string> CarryWords(const std::string& s)
{
    std::vector<std::string> w;
    size_t i = 0;
    while (i < s.size())
    {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
        size_t e = i;
        while (e < s.size() && s[e] != ' ' && s[e] != '\t' && s[e] != '\r' && s[e] != '\n') ++e;
        if (e > i) w.push_back(s.substr(i, e - i));
        i = e;
    }
    return w;
}
inline int CarryTagOk(const std::string& t)
{
    if (t.empty() || t.size() > kCarryTagMax) return 0;
    for (size_t i = 0; i < t.size(); ++i)
    {
        const char c = t[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return 0;
    }
    return 1;
}
/* `carryread [<tag>] [full]`, in either order: 1 = good (*tag "-" when none given), 0 = refused (outputs untouched) */
inline int CarryReadParse(const std::string& args, std::string* tag, int* full)
{
    const std::vector<std::string> w = CarryWords(args);
    std::string t; int f = 0;
    for (size_t i = 0; i < w.size(); ++i)
    {
        if (w[i] == "full") { if (f) return 0; f = 1; continue; }
        if (!t.empty() || !CarryTagOk(w[i])) return 0;
        t = w[i];
    }
    *tag = t.empty() ? std::string("-") : t; *full = f;
    return 1;
}

const int kCarrySetBad = 0, kCarrySetResearch = 1, kCarrySetMap = 2, kCarrySetStanding = 3;
/* `carryset research <techStringID|any>` | `carryset map <townStringID|town name|any> [explored]` | `carryset standing <faction
   stringID or name>`. A town or faction name may hold blanks (single blanks kept as typed); the outputs are untouched on a refusal. */
inline int CarrySetParse(const std::string& args, std::string* target, int* explored)
{
    std::vector<std::string> w = CarryWords(args);
    if (w.size() < 2) return kCarrySetBad;
    const std::string kind = w[0];
    int ex = 0;
    if (kind == "map" && w.size() >= 3 && w[w.size() - 1] == "explored") { ex = 1; w.pop_back(); }
    std::string rest;
    for (size_t i = 1; i < w.size(); ++i) rest += (i > 1 ? " " : "") + w[i];
    if (kind == "research") { if (w.size() != 2) return kCarrySetBad; *target = rest; *explored = 0; return kCarrySetResearch; }
    if (kind == "map") { *target = rest; *explored = ex; return kCarrySetMap; }
    if (kind == "standing") { *target = rest; *explored = 0; return kCarrySetStanding; }
    return kCarrySetBad;
}

}   /* namespace swcarry */
