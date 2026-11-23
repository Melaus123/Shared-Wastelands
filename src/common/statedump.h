#pragma once
/* T-1429 step 2 (owner decision 1777, 05-findings F1241): THE STATE DUMP'S LINE FORMAT - one stable line per loaded thing, so two
   dumps (before / after a relaunch, or two games) can be paired by key offline (tools/dump_compare.py).

       [DUMP] <tag> <kind> <key> f1=v1 f2=v2 ...
       [DUMP] <tag> total <kind> n=<all> printed=<lines> cap=<cap>

   kind: area / building / door / furniture / box / ground / squad / character / cage (v3). key: the mod's own record key (a building's P7n position key, a
   box's key, a ground item's G| key, a squad's world id) - never a pointer, so it survives a relaunch. A key that occurs twice in
   one dump gets #2, #3 .. in key-then-fields order. A field this game could not read prints '?'. Keys and values are escaped
   (space, '%', '=' and anything outside printable ASCII as %XX) so a line splits on spaces and each field on its first '='.
   Header-only, no engine types; shared by items.cpp / build.cpp and the offline suite (coop-test). */

#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>

namespace coopdump {

const int kDumpCapPerKind = 1500;   /* lines per kind per dump; the total line always says how many there were */
const int kDumpTagCap = 24;

/* the tag a lever line names: 1-24 of [A-Za-z0-9_-] */
inline int DumpTagOk(const std::string& t)
{
    if (t.empty() || (int)t.size() > kDumpTagCap) return 0;
    for (size_t i = 0; i < t.size(); ++i)
    {
        const char c = t[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}
inline std::string DumpEsc(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const unsigned char c = (unsigned char)s[i];
        if (c <= 0x20 || c >= 0x7F || c == '%' || c == '=') { char h[4]; std::sprintf(h, "%%%02X", (unsigned)c); o += h; }
        else o += (char)c;
    }
    return o.empty() ? std::string("%00") : o;   /* an empty key / value still makes one word */
}
/* " name=value" (the value escaped) */
inline std::string DumpField(const char* name, const std::string& value) { return std::string(" ") + name + "=" + DumpEsc(value); }
inline std::string DumpInt(long long v) { char b[32]; std::sprintf(b, "%lld", v); return b; }
/* a float to two decimals; NaN / huge = '?' */
inline std::string DumpF2(double v)
{
    if (!(v > -1.0e9 && v < 1.0e9)) return "?";
    char b[48]; std::sprintf(b, "%.2f", v);
    if (std::string(b) == "-0.00") return "0.00";
    return b;
}
/* a position to one decimal: x,y,z */
inline std::string DumpPos(const float* p)
{
    for (int i = 0; i < 3; ++i) if (!(p[i] > -1.0e7f && p[i] < 1.0e7f)) return "?";
    char b[96]; std::sprintf(b, "%.1f,%.1f,%.1f", p[0], p[1], p[2]);
    return b;
}
/* T-1429 v3: a character's position to 10 units - each axis rounded to the nearest 10 ("-0" printed "0"), so a character that stands
   still reads the same on both games; '?' when an axis is NaN / huge */
inline std::string DumpPos10(const float* p)
{
    std::string o;
    for (int i = 0; i < 3; ++i)
    {
        if (!(p[i] > -1.0e7f && p[i] < 1.0e7f)) return "?";
        const double r = (double)p[i] / 10.0;
        long long v = (long long)(r < 0 ? -(long long)(-r + 0.5) : (long long)(r + 0.5)) * 10;
        o += (i == 0 ? "" : ",") + DumpInt(v);
    }
    return o;
}
/* T-1429 v3: a building's outline (six floats): 'unset' when any is NaN or huge - a box the engine has not filled yet (v2 printed a
   NaN as '-1.$'), else each to one decimal */
inline std::string DumpOutline(const float* o)
{
    for (int i = 0; i < 6; ++i) if (!(o[i] == o[i]) || !(o[i] > -1.0e30f && o[i] < 1.0e30f)) return "unset";
    char b[200];
    std::sprintf(b, "%.1f,%.1f,%.1f,%.1f,%.1f,%.1f", o[0], o[1], o[2], o[3], o[4], o[5]);
    return b;
}
inline std::string DumpHex(unsigned int v) { char b[16]; std::sprintf(b, "%08x", v); return b; }
/* FNV-1a 32, for the short digests (members, outlines) */
inline unsigned int DumpFnv(const void* p, size_t n, unsigned int h = 2166136261u)
{
    const unsigned char* c = (const unsigned char*)p;
    for (size_t i = 0; i < n; ++i) { h ^= c[i]; h *= 16777619u; }
    return h;
}

/* T-1429 (F1302): a town's name as the dump prints it - the engine's own town name with its trailing instance number dropped.
   The engine names a generic town '<template name> <n>' and hands n out again every launch (T2568: the same 19,31 camp read
   'Dust Bandit Camp 48' on one game and 'Dust Bandit Camp 29' on the other), so the number says nothing about WHICH town it is.
   Only ' <digits>' at the very end goes, and only after a non-empty name; anything else is kept as it is. tools/dump_compare.py
   (stable_town) applies the same rule to older dumps. */
inline std::string DumpTownName(const std::string& n)
{
    size_t e = n.size();
    while (e > 0 && n[e - 1] >= '0' && n[e - 1] <= '9') --e;
    if (e == n.size() || e < 2 || n[e - 1] != ' ') return n;
    return n.substr(0, e - 1);
}

struct DumpRow { std::string key; std::string fields; };   /* fields: " f1=v1 f2=v2" (DumpField's form) */
inline bool DumpRowLess(const DumpRow& a, const DumpRow& b) { return a.key < b.key || (a.key == b.key && a.fields < b.fields); }

/* sorts the rows by key (then fields) and marks a repeated key #2, #3 .. so every key in one kind is unique */
inline void DumpSortRows(std::vector<DumpRow>* rows)
{
    std::sort(rows->begin(), rows->end(), DumpRowLess);
    int rep = 1;
    std::string last;
    std::vector<std::string> base(rows->size());
    for (size_t i = 0; i < rows->size(); ++i) base[i] = (*rows)[i].key;
    for (size_t i = 0; i < rows->size(); ++i)
    {
        if (i > 0 && base[i] == base[i - 1]) { ++rep; (*rows)[i].key = base[i] + "#" + DumpInt(rep); }
        else rep = 1;
    }
}
inline std::string DumpLine(const std::string& tag, const std::string& kind, const DumpRow& r)
{
    return "[DUMP] " + tag + " " + kind + " " + DumpEsc(r.key) + r.fields;
}
inline std::string DumpTotalLine(const std::string& tag, const std::string& kind, long long n, long long printed, const std::string& extra)
{
    return "[DUMP] " + tag + " total " + kind + " n=" + DumpInt(n) + " printed=" + DumpInt(printed) + " cap=" + DumpInt(kDumpCapPerKind) + extra;
}

}   /* namespace coopdump */
