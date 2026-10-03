/* loadermarker.h - mig6 (RE_Kenshi migration stage 6, owner decision 118, 2026-09-29). The marker file the loader
 * reads, and nothing else.
 *
 * WHY. SharedWastelandsLoader.dll (src/kenshi-loader) is started by the game itself - one `Plugin=SharedWastelandsLoader` line in
 * the game's Plugins_x64.cfg (owner 117) - and it must find the multiplayer mod WITHOUT a hard-coded folder or DLL
 * name, so the same loader could later start other mods (owner 118). It looks in every mod folder for ONE file, the
 * marker, and the marker says which DLL in that folder to load and which of its exports to call.
 *
 * THE FILE: <mod folder>\shared-wastelands.loader.txt, plain ASCII (a UTF-8 byte-order mark is allowed and skipped),
 * at most 4096 bytes, one `key=value` per line (LF or CRLF). Blank lines and lines starting with '#' are skipped;
 * spaces and tabs around the key and the value are trimmed. The keys:
 *   format=1              required; the only format this loader reads. A later incompatible format gets a new number.
 *   dll=SharedWastelands.dll    required; a plain file name in the marker's own folder: 1-64 characters from A-Z a-z 0-9 . _ -,
 *                         ending in .dll, not starting with '.', no "..". A path, a drive or a slash is refused.
 *   start=coopEarlyStart  required; the export to call, a C identifier of 1-64 characters. It takes no arguments and
 *                         returns nothing (void (*)(void)).
 * Any other key is IGNORED (a later loader may add keys; this one must not refuse a file for carrying them). A known
 * key given twice, a line with no '=', a NUL byte, or a file over 4096 bytes is refused. The file is then not used and
 * the loader goes on to the next folder.
 *
 * PURE and CRT-FREE: no library call at all (the loader links no C runtime - /NODEFAULTLIB), no allocation, no
 * Windows. The offline suite (src/coop-test) tests exactly this code.
 *
 * C++03 (VS2010 v100).
 */
#pragma once
#include "names.h"   /* the on-disk names (mod folder, files, window texts) */

namespace cooploader {

enum { kMarkerMaxBytes = 4096, kMarkerNameMax = 64 };

/* The marker's file name, in the mod folder. Narrow here; the loader widens it (it is pure ASCII). */
inline const char* MarkerFileName() { return swnames::kLoaderMarker; }

struct MarkerResult
{
    int         ok;                        /* 1 = usable: format, dll and start all present and valid */
    int         format;                    /* the format= value, or -1 when absent / not a number */
    char        dll[kMarkerNameMax + 1];   /* NUL-terminated; "" unless ok */
    char        start[kMarkerNameMax + 1]; /* NUL-terminated; "" unless ok */
    int         badLine;                   /* the 1-based line that was refused, 0 = none (or a whole-file refusal) */
    int         ignoredKeys;               /* lines with an unknown key (skipped, counted for the log) */
    const char* why;                       /* a short English reason, "" when ok - a static string */
};

namespace detail {

inline bool IsSpace(char c) { return c == ' ' || c == '\t'; }
inline bool IsDigit(char c) { return c >= '0' && c <= '9'; }
inline bool IsAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
inline char Lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

/* [a, b) equals the NUL-terminated word w exactly. */
inline bool Eq(const char* a, const char* b, const char* w)
{
    while (a < b && *w != 0) { if (*a != *w) return false; ++a; ++w; }
    return a == b && *w == 0;
}

inline void Clear(char* s, int n) { int i; for (i = 0; i < n; ++i) s[i] = 0; }

/* A plain DLL file name: 1-64 of A-Z a-z 0-9 . _ -, ends ".dll" (any case), not starting '.', no "..". */
inline bool DllNameOk(const char* a, const char* b)
{
    const long n = (long)(b - a);
    long i;
    if (n < 5 || n > kMarkerNameMax) return false;
    if (a[0] == '.') return false;
    for (i = 0; i < n; ++i)
    {
        const char c = a[i];
        if (!(IsAlpha(c) || IsDigit(c) || c == '.' || c == '_' || c == '-')) return false;
        if (c == '.' && i + 1 < n && a[i + 1] == '.') return false;
    }
    return Lower(b[-4]) == '.' && Lower(b[-3]) == 'd' && Lower(b[-2]) == 'l' && Lower(b[-1]) == 'l';
}

/* A C identifier of 1-64 characters. */
inline bool IdentOk(const char* a, const char* b)
{
    const long n = (long)(b - a);
    long i;
    if (n < 1 || n > kMarkerNameMax) return false;
    if (!(IsAlpha(a[0]) || a[0] == '_')) return false;
    for (i = 1; i < n; ++i)
        if (!(IsAlpha(a[i]) || IsDigit(a[i]) || a[i] == '_')) return false;
    return true;
}

inline void Copy(char* dst, const char* a, const char* b)
{
    int i = 0;
    while (a < b && i < kMarkerNameMax) dst[i++] = *a++;
    dst[i] = 0;
}

inline void Refuse(MarkerResult* r, int line, const char* why)
{
    r->ok = 0;
    r->badLine = line;
    r->why = why;
    Clear(r->dll, kMarkerNameMax + 1);
    Clear(r->start, kMarkerNameMax + 1);
}

}   /* namespace detail */

/* Parse the marker's bytes. `len` is the byte count read from the file (the caller reads at most
   kMarkerMaxBytes + 1 so an oversized file is seen as such). Always fills every field of *out. */
inline void MarkerParse(const char* text, long len, MarkerResult* out)
{
    using namespace detail;
    const char* p;
    const char* end;
    int line = 0;
    bool haveFormat = false, haveDll = false, haveStart = false;

    out->ok = 0;
    out->format = -1;
    out->badLine = 0;
    out->ignoredKeys = 0;
    out->why = "";
    Clear(out->dll, kMarkerNameMax + 1);
    Clear(out->start, kMarkerNameMax + 1);

    if (text == 0 || len < 0) { Refuse(out, 0, "the marker file could not be read"); return; }
    if (len > kMarkerMaxBytes) { Refuse(out, 0, "the marker file is larger than 4096 bytes"); return; }
    {
        long i;
        for (i = 0; i < len; ++i)
            if (text[i] == 0) { Refuse(out, 0, "the marker file holds a NUL byte (it is not a text file)"); return; }
    }
    p = text;
    end = text + len;
    if (len >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;

    while (p < end)
    {
        const char* ls = p;
        const char* le = p;
        const char* eq;
        const char* ka;
        const char* kb;
        const char* va;
        const char* vb;
        while (le < end && *le != '\n') ++le;
        p = (le < end) ? le + 1 : end;
        ++line;
        if (le > ls && le[-1] == '\r') --le;
        while (ls < le && IsSpace(*ls)) ++ls;
        while (le > ls && IsSpace(le[-1])) --le;
        if (ls == le || *ls == '#') continue;

        eq = ls;
        while (eq < le && *eq != '=') ++eq;
        if (eq == le) { Refuse(out, line, "a line is not key=value"); return; }
        ka = ls; kb = eq;
        while (kb > ka && IsSpace(kb[-1])) --kb;
        va = eq + 1; vb = le;
        while (va < vb && IsSpace(*va)) ++va;

        if (Eq(ka, kb, "format"))
        {
            long v = 0;
            const char* q;
            if (haveFormat) { Refuse(out, line, "format= is given twice"); return; }
            haveFormat = true;
            if (va == vb || vb - va > 6) { Refuse(out, line, "format= is not a number"); return; }
            for (q = va; q < vb; ++q)
            {
                if (!IsDigit(*q)) { Refuse(out, line, "format= is not a number"); return; }
                v = v * 10 + (*q - '0');
            }
            out->format = (int)v;
            if (v != 1) { Refuse(out, line, "format= is not 1, the only format this loader reads"); return; }
        }
        else if (Eq(ka, kb, "dll"))
        {
            if (haveDll) { Refuse(out, line, "dll= is given twice"); return; }
            haveDll = true;
            if (!DllNameOk(va, vb)) { Refuse(out, line, "dll= is not a plain .dll file name (1-64 of A-Z a-z 0-9 . _ -, no path)"); return; }
            Copy(out->dll, va, vb);
        }
        else if (Eq(ka, kb, "start"))
        {
            if (haveStart) { Refuse(out, line, "start= is given twice"); return; }
            haveStart = true;
            if (!IdentOk(va, vb)) { Refuse(out, line, "start= is not an export name (a C identifier of 1-64 characters)"); return; }
            Copy(out->start, va, vb);
        }
        else
        {
            ++out->ignoredKeys;
        }
    }

    if (!haveFormat) { Refuse(out, 0, "format= is missing"); return; }
    if (!haveDll)    { Refuse(out, 0, "dll= is missing"); return; }
    if (!haveStart)  { Refuse(out, 0, "start= is missing"); return; }
    out->ok = 1;
}

}   /* namespace cooploader */
