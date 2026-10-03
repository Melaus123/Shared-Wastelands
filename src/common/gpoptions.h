#pragma once
// settings2 S2 (docs/design-settings1.md sections 1a, 1b and 2): the world's advanced options as notebook OPTIONS keys.
// ONE table and ONE range check, compiled into the plugin (settings.cpp), the notebook (store_main.cpp OptionValueOk)
// and the offline suite, so the three cannot hold different ideas of which key is legal.
//   gp.<key>  a GameplayOptions field (the global at 0x2132528, 36 bytes; design 1a). Written by the world's authority.
//   gt.<key>  a world-changing GameOptions field (design 1b). ACCEPTED by the notebook from S2 on so S4 needs no
//             notebook rebuild; no game sends or applies one before S4.
// Values are decimal text; floats are written with %.9g, which reads back to the same bits.
// RANGES: floats finite and inside 0.01..100 (Guess on the upper bound - the slider limits the Advanced window's ctor
// passes were not decoded; 0 is refused because research speed is a divisor, review-settings2 D6), bools 0/1, attacks 0..5,
// limbloss 0..3 (Guess on the upper bound).
#include <string>
#include <cstdlib>
#include <float.h>

namespace coopgp {

enum { kGpFloat = 0, kGpBool = 1 };
struct GpField { const char* key; int off; int kind; };
const int kGpCount = 11;
inline const GpField* GpFields()
{
    static const GpField f[kGpCount] = {
        { "cod", 0x00, kGpFloat }, { "ep", 0x04, kGpBool }, { "gdm", 0x08, kGpFloat }, { "bs", 0x0C, kGpFloat },
        { "nnm", 0x10, kGpFloat }, { "rs", 0x14, kGpFloat }, { "ps", 0x18, kGpFloat }, { "ht", 0x1C, kGpFloat },
        { "bl", 0x20, kGpBool }, { "ae", 0x21, kGpBool }, { "dh", 0x22, kGpBool } };
    return f;
}
// "gdm" or "gp.gdm" -> 0..10; anything else -> -1.
inline int GpIndex(const std::string& keyIn)
{
    const std::string key = (keyIn.compare(0, 3, "gp.") == 0) ? keyIn.substr(3) : keyIn;
    for (int i = 0; i < kGpCount; ++i) if (key == GpFields()[i].key) return i;
    return -1;
}
// The whole string must be one finite decimal number.
inline bool GpParseNumber(const std::string& v, double* out)
{
    if (v.empty() || v.size() > 32) return false;
    const char* b = v.c_str(); char* e = 0;
    const double d = std::strtod(b, &e);
    if (e == b || *e != 0 || !_finite(d)) return false;
    *out = d;
    return true;
}
// field kind + text -> ok, and the parsed number (a bool parses to 0 or 1).
inline bool GpKindValueOk(int kind, const std::string& v, double* out)
{
    if (kind == kGpBool) { if (v != "0" && v != "1") return false; if (out) *out = (v == "1") ? 1.0 : 0.0; return true; }
    double d = 0.0;
    if (!GpParseNumber(v, &d) || d < 0.01 || d > 100.0) return false;   /* review-settings2 D6: 0.01, not 0 - research speed (+0x14) is a DIVISOR (0x82DE40) */
    if (out) *out = d;
    return true;
}
// A full notebook key ("gp.gdm") and its value.
inline bool GpValueOk(const std::string& key, const std::string& value, double* out)
{
    if (key.compare(0, 3, "gp.") != 0) return false;
    const int i = GpIndex(key);
    return i >= 0 && GpKindValueOk(GpFields()[i].kind, value, out);
}
// gt.* (design 1b; applied from S4). pop squad raidsize raidfreq: floats as above; attacks 0..5; limbloss 0..3; civ 0/1.
inline bool GtValueOk(const std::string& key, const std::string& value)
{
    if (key.compare(0, 3, "gt.") != 0) return false;
    const std::string k = key.substr(3);
    double d = 0.0;
    if (k == "pop" || k == "squad" || k == "raidsize" || k == "raidfreq") return GpKindValueOk(kGpFloat, value, &d);
    if (k == "civ") return GpKindValueOk(kGpBool, value, &d);
    if (k == "attacks" || k == "limbloss")
    {
        if (value.empty() || value.size() > 2) return false;
        for (size_t i = 0; i < value.size(); ++i) if (value[i] < '0' || value[i] > '9') return false;
        const int n = std::atoi(value.c_str());
        return n >= 0 && n <= (k == "attacks" ? 5 : 3);
    }
    return false;
}

} // namespace coopgp
