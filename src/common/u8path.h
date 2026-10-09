/* u8path.h - UTF-8 <-> UTF-16 text conversion with no Windows dependency.
 *
 * The game hands the mod its paths and names as UTF-8 (the save folder under C:\Users\<name>, a save's name, a
 * squad's name), and the mod's own strings are UTF-8 too. Windows' file calls that take any character are the
 * wide (W) ones, which take UTF-16. The plugin's wrappers (src/coop-plugin/u8file.h) convert through these two
 * functions; the offline suite tests them directly. wchar_t is 16 bits on Windows: a character above U+FFFF
 * (an emoji) is a surrogate pair of two wchar_t.
 *
 * Malformed input never stops a conversion: each bad UTF-8 byte, and each unpaired surrogate, becomes U+FFFD.
 */
#pragma once

#include <string>

namespace u8path {

/* One code point of s at byte i: *cp set and the number of bytes it takes returned; 0 = the byte at i does not
   start a well-formed sequence (a stray continuation byte, an overlong form, an encoded surrogate, a value above
   U+10FFFF or a sequence cut short by the end of s). */
inline size_t Utf8Next(const std::string& s, size_t i, unsigned long* cp)
{
    const unsigned char c0 = (unsigned char)s[i];
    if (c0 < 0x80) { *cp = c0; return 1; }
    size_t n; unsigned long v, lowest;
    if (c0 >= 0xC2 && c0 <= 0xDF)      { n = 2; v = c0 & 0x1F; lowest = 0x80; }
    else if (c0 >= 0xE0 && c0 <= 0xEF) { n = 3; v = c0 & 0x0F; lowest = 0x800; }
    else if (c0 >= 0xF0 && c0 <= 0xF4) { n = 4; v = c0 & 0x07; lowest = 0x10000; }
    else return 0;
    if (i + n > s.size()) return 0;
    for (size_t k = 1; k < n; ++k)
    {
        const unsigned char c = (unsigned char)s[i + k];
        if ((c & 0xC0) != 0x80) return 0;
        v = (v << 6) | (c & 0x3F);
    }
    if (v < lowest || v > 1114111u /* U+10FFFF, the highest Unicode code point */ || (v >= 0xD800 && v <= 0xDFFF)) return 0;
    *cp = v;
    return n;
}

/* true = every byte of s belongs to a well-formed UTF-8 sequence (plain ASCII is well-formed UTF-8) */
inline bool Utf8Valid(const std::string& s)
{
    unsigned long cp = 0;
    for (size_t i = 0; i < s.size(); )
    {
        const size_t n = Utf8Next(s, i, &cp);
        if (n == 0) return false;
        i += n;
    }
    return true;
}

/* UTF-8 -> UTF-16. Returns false when any byte was malformed (each such byte became U+FFFD in *out). */
inline bool Utf8ToUtf16(const std::string& s, std::wstring* out)
{
    out->clear();
    out->reserve(s.size());
    bool clean = true;
    unsigned long cp = 0;
    for (size_t i = 0; i < s.size(); )
    {
        const size_t n = Utf8Next(s, i, &cp);
        if (n == 0) { out->push_back((wchar_t)0xFFFD); clean = false; ++i; continue; }
        i += n;
        if (cp >= 0x10000)
        {
            cp -= 0x10000;
            out->push_back((wchar_t)(0xD800 + (cp >> 10)));
            out->push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
        }
        else out->push_back((wchar_t)cp);
    }
    return clean;
}

/* UTF-16 -> UTF-8. An unpaired surrogate becomes U+FFFD (EF BF BD). */
inline std::string Utf16ToUtf8(const std::wstring& w)
{
    std::string out;
    out.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i)
    {
        unsigned long cp = (unsigned long)(unsigned short)w[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size())
        {
            const unsigned long lo = (unsigned long)(unsigned short)w[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); ++i; }
            else cp = 0xFFFD;
        }
        else if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
        if (cp < 0x80) out.push_back((char)cp);
        else if (cp < 0x800)
        {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

}   /* namespace u8path */
