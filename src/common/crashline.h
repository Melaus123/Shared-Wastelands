/* crashline.h - T-1374 (owner 1688): THE [CRASH] LINE - what kind of crash, and which file and spot crashed, written into the
 * mod's log when the game's crash handling starts.
 *
 * WHERE THE LINE COMES FROM (src/coop-plugin/crashwatch.cpp; .modding/05-findings.md F1036):
 *   main-loop  Kenshi's WinMain guards its main-loop call with one catch-all SEH block; the block's filter (1.0.65 0x1372BB0 /
 *              1.0.68 0x1373AB0, row KenshiCrashFilter) writes Kenshi's dump and answers 'handle it'. Its detour writes this
 *              line first, then runs it.
 *   worker     an exception no handler takes on any other thread (or on the main thread outside the guarded call) reaches the
 *              process's unhandled-exception filter: ours, set at the start and again at each world load, which writes the line
 *              and then runs the filter it replaced.
 *   abort      the C runtime's abort (msvcr100 - the game's and the mod's one runtime) raises SIGABRT; our signal handler writes
 *              the line and abort goes on.
 *   A buffer-overrun failure (status 0xC0000409, the /GS check) ends the process without running any of these: no line.
 *
 * THE LINE (one line, built in a stack buffer - the heap may be what crashed):
 *   [CRASH] <road> code=0x<code> [(<read|write|execute> of 0x<addr>)] [type=<thrown C++ type>] at <module>+0x<offset>
 *           thread=<id> <main|worker> utc=<yyyy-mm-ddThh:mm:ssZ> stack: <module>+0x<offset> < <module>+0x<offset> ... ;
 *           <mod and game version>
 *   abort's code is written as "code=SIGABRT". A module is the file name only (a folder would carry the Windows user name);
 *   an address in no module is "?+0x<address>". utc= is the moment of the crash on Windows' clock (UTC): crashkeep pairs
 *   Kenshi's crash file with this launch only when the file was written at or after it (crashkeep.h CrashFileOfKeptLaunch).
 *   With a stack overflow, or too little stack left for the walk, the short line (FormatShortLine) has
 *   "stack: not walked (<why>)" in place of the frames.
 *
 * PURE: no Windows header, no allocation, no std::string - the runtime calls these from inside its crash handler. Shared with
 * the offline suite. C++03 (VS2010 v100).
 */
#ifndef COOP_COMMON_CRASHLINE_H
#define COOP_COMMON_CRASHLINE_H

#include <cstddef>
#include <cstring>

namespace swcrash {

enum { kRoadMainLoop = 0, kRoadWorker = 1, kRoadAbort = 2 };
enum { kMaxFrames = 12, kModuleMax = 40, kTypeMax = 96, kVersionMax = 96, kLineMax = 1024 };
const unsigned long kCodeAccessViolation = 0xC0000005UL;
const unsigned long kCodeCppThrow = 0xE06D7363UL;
const unsigned long kCodeStackOverflow = 0xC00000FDUL;
enum { kShortStackOverflow = 1, kShortLittleStack = 2 };   /* why the short line has no stack */
/* the line's own start: crashkeep judges a launch whose log holds it as crashed (src/common/crashkeep.h). No other line of the
   mod starts with it ("[CRASHKEEP]", "[CRASHWATCH]" and "[CRASHTEST]" do not: their bracket is not closed right after CRASH). */
const char* const kLineText = "[CRASH] ";

/* one place in the code: a module's file name and the offset in it, or no module (known 0: offset is the address itself) */
struct Place
{
    char module[kModuleMax];
    unsigned long long offset;
    int known;
};

struct Record
{
    int road;                  /* kRoad* */
    unsigned long code;        /* the exception code (unused for kRoadAbort) */
    int avKind;                /* an access violation's kind: 0 read, 1 write, 8 execute; -1 = not an access violation */
    unsigned long long avAddress;
    char typeName[kTypeMax];   /* a C++ exception's thrown type, "" when not one or not readable */
    Place at;
    unsigned long thread;
    int mainThread;            /* 1 the game's main thread, 0 another, -1 not known */
    unsigned long long utcSec; /* the crash's moment: seconds since 1601-01-01 UTC (a FILETIME / 10^7); 0 = not known */
    int frames;
    Place frame[kMaxFrames];
    char version[kVersionMax]; /* "mod <version>, game <table or fingerprint>" */
};

inline const char* RoadText(int road)
{
    return road == kRoadMainLoop ? "main-loop" : road == kRoadWorker ? "worker" : road == kRoadAbort ? "abort" : "unknown-road";
}
inline const char* AvKindText(int kind)
{
    return kind == 0 ? "read" : kind == 1 ? "write" : kind == 8 ? "execute" : "access";
}

/* --- bounded appenders: out always NUL-terminated, *len never past n - 1 --- */
inline void Put(char* out, size_t n, size_t* len, const char* s)
{
    if (n == 0) return;
    for (; s != 0 && *s != 0 && *len + 1 < n; ++s) out[(*len)++] = *s;
    out[*len] = 0;
}
inline void PutHex(char* out, size_t n, size_t* len, unsigned long long v, int minDigits)
{
    char b[20];
    int k = 0;
    do { const int d = (int)(v & 0xF); b[k++] = (char)(d < 10 ? '0' + d : 'A' + d - 10); v >>= 4; } while (v != 0 && k < 16);
    while (k < minDigits && k < 16) b[k++] = '0';
    char r[24] = "0x";
    int j = 2;
    while (k > 0) r[j++] = b[--k];
    r[j] = 0;
    Put(out, n, len, r);
}
inline void PutDec(char* out, size_t n, size_t* len, unsigned long long v)
{
    char b[24];
    int k = 0;
    do { b[k++] = (char)('0' + (int)(v % 10)); v /= 10; } while (v != 0 && k < 20);
    char r[24];
    int j = 0;
    while (k > 0) r[j++] = b[--k];
    r[j] = 0;
    Put(out, n, len, r);
}
/* v in decimal, at least minDigits digits (leading zeros) */
inline void PutDecPad(char* out, size_t n, size_t* len, unsigned long long v, int minDigits)
{
    unsigned long long w = v;
    int digits = 0;
    do { ++digits; w /= 10; } while (w != 0);
    for (; digits < minDigits && digits < 20; ++digits) Put(out, n, len, "0");
    PutDec(out, n, len, v);
}

/* --- the crash's moment on Windows' clock: seconds since 1601-01-01 UTC <-> "yyyy-mm-ddThh:mm:ssZ" (proleptic Gregorian;
   integer arithmetic only - written from inside the handler) --- */
const unsigned long long kDays1601From0000_03_01 = 584694ULL;   /* 0000-03-01 + 584694 days = 1601-01-01 */
inline void PutUtc(char* out, size_t n, size_t* len, unsigned long long sec1601)
{
    const unsigned long long z = sec1601 / 86400ULL + kDays1601From0000_03_01, secs = sec1601 % 86400ULL;
    const unsigned long long era = z / 146097ULL, doe = z - era * 146097ULL;
    const unsigned long long yoe = (doe - doe / 1460ULL + doe / 36524ULL - doe / 146096ULL) / 365ULL;
    const unsigned long long doy = doe - (365ULL * yoe + yoe / 4ULL - yoe / 100ULL), mp = (5ULL * doy + 2ULL) / 153ULL;
    const unsigned long long d = doy - (153ULL * mp + 2ULL) / 5ULL + 1ULL, m = mp < 10ULL ? mp + 3ULL : mp - 9ULL;
    const unsigned long long y = yoe + era * 400ULL + (m <= 2ULL ? 1ULL : 0ULL);
    PutDecPad(out, n, len, y, 4); Put(out, n, len, "-"); PutDecPad(out, n, len, m, 2); Put(out, n, len, "-"); PutDecPad(out, n, len, d, 2);
    Put(out, n, len, "T"); PutDecPad(out, n, len, secs / 3600ULL, 2); Put(out, n, len, ":"); PutDecPad(out, n, len, (secs / 60ULL) % 60ULL, 2);
    Put(out, n, len, ":"); PutDecPad(out, n, len, secs % 60ULL, 2); Put(out, n, len, "Z");
}
/* "yyyy-mm-ddThh:mm:ssZ" at s (n bytes there, at least 20 needed) -> seconds since 1601-01-01 UTC; 0 = not that shape, a field
   out of range or a year before 1601 */
inline unsigned long long ParseUtc(const char* s, size_t n)
{
    if (s == 0 || n < 20) return 0;
    const char shape[] = "dddd-dd-ddTdd:dd:ddZ";
    for (int i = 0; i < 20; ++i)
        if (shape[i] == 'd' ? !(s[i] >= '0' && s[i] <= '9') : s[i] != shape[i]) return 0;
    const unsigned long long y = (unsigned long long)((s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0'));
    const unsigned long long m = (unsigned long long)((s[5] - '0') * 10 + (s[6] - '0')), d = (unsigned long long)((s[8] - '0') * 10 + (s[9] - '0'));
    const unsigned long long hh = (unsigned long long)((s[11] - '0') * 10 + (s[12] - '0')), mm = (unsigned long long)((s[14] - '0') * 10 + (s[15] - '0'));
    const unsigned long long ss = (unsigned long long)((s[17] - '0') * 10 + (s[18] - '0'));
    if (y < 1601 || m < 1 || m > 12 || d < 1 || d > 31 || hh > 23 || mm > 59 || ss > 59) return 0;
    const unsigned long long yy = m <= 2 ? y - 1 : y;
    const unsigned long long era = yy / 400ULL, yoe = yy - era * 400ULL;
    const unsigned long long doy = (153ULL * (m > 2 ? m - 3 : m + 9) + 2ULL) / 5ULL + d - 1ULL;
    const unsigned long long doe = yoe * 365ULL + yoe / 4ULL - yoe / 100ULL + doy;
    const unsigned long long z = era * 146097ULL + doe;   /* days since 0000-03-01 */
    if (z < kDays1601From0000_03_01) return 0;
    return (z - kDays1601From0000_03_01) * 86400ULL + hh * 3600ULL + mm * 60ULL + ss;
}

/* The file name of a module's full path, without its folder; a character outside printable ASCII becomes '?'. */
inline void FileNameOnly(const wchar_t* path, char* out, size_t n)
{
    if (n == 0) return;
    out[0] = 0;
    if (path == 0) return;
    const wchar_t* s = path;
    for (const wchar_t* p = path; *p != 0; ++p) if (*p == L'\\' || *p == L'/' || *p == L':') s = p + 1;
    size_t len = 0;
    for (; *s != 0 && len + 1 < n; ++s) out[len++] = (*s >= 0x20 && *s < 0x7F) ? (char)*s : '?';
    out[len] = 0;
}

/* A place from its module's name (or "" for none), the address and the module's base. */
inline void SetPlace(Place* p, const char* module, unsigned long long address, unsigned long long base)
{
    size_t len = 0;
    p->module[0] = 0;
    if (module != 0 && module[0] != 0 && base != 0 && address >= base)
    {
        Put(p->module, sizeof(p->module), &len, module);
        p->offset = address - base;
        p->known = 1;
    }
    else { p->offset = address; p->known = 0; }
}
inline void PutPlace(char* out, size_t n, size_t* len, const Place& p)
{
    Put(out, n, len, p.known ? p.module : "?");
    Put(out, n, len, "+");
    PutHex(out, n, len, p.offset, 1);
}

/* the same file name, ignoring case */
inline bool SameModule(const char* a, const char* b)
{
    if (a == 0 || b == 0) return false;
    for (;; ++a, ++b)
    {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return false;
        if (x == 0) return true;
    }
}
/* The first of frames [from, n) not in `module` (n when there is none). A known=0 frame is in no module. */
inline int FirstOutside(const Place* f, int n, int from, const char* module)
{
    for (int i = from < 0 ? 0 : from; i < n; ++i) if (!f[i].known || !SameModule(f[i].module, module)) return i;
    return n;
}

/* One part of a decorated name is the compiler's name for an unnamed namespace: "?A" alone, or "?A0x" and hex digits. */
inline bool AnonNamespacePart(const char* p, size_t n)
{
    if (n < 2 || p[0] != '?' || p[1] != 'A') return false;
    if (n == 2) return true;
    if (n < 5 || p[2] != '0' || p[3] != 'x') return false;
    for (size_t i = 4; i < n; ++i)
        if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f') || (p[i] >= 'A' && p[i] <= 'F'))) return false;
    return true;
}
/* A thrown C++ type's decorated name (".?AVInternalErrorException@Ogre@@") as written in code ("Ogre::InternalErrorException").
   An unnamed namespace's part ("?A0x1b2c3d4e") is written "`anonymous namespace'", as the compiler's own undecorator writes it.
   Only the innermost kTypeParts parts are read: a name with more is written with "..." in place of its outer parts
   ("...::n3::...::D"). A name this cannot read plainly (a template, a basic type) is copied as it is, printable characters
   only. True = undecorated. */
enum { kTypeParts = 16 };
inline bool TypeNameText(const char* deco, char* out, size_t n)
{
    if (n == 0) return false;
    out[0] = 0;
    if (deco == 0) return false;
    size_t len = 0;
    const size_t dl = std::strlen(deco);
    const char* body = 0;
    if (dl > 4 && std::strncmp(deco, ".?AV", 4) == 0) body = deco + 4;
    else if (dl > 4 && std::strncmp(deco, ".?AU", 4) == 0) body = deco + 4;
    bool plain = body != 0 && dl >= 6 && deco[dl - 1] == '@' && deco[dl - 2] == '@' && *body != '@';
    const char* end = plain ? deco + dl - 2 : 0;   /* the first '@' of the closing "@@" */
    if (plain)
    {   /* every part is a plain name or an unnamed namespace's */
        const char* s = body;
        for (const char* p = body; p <= end && plain; ++p)
        {
            if (p != end && *p != '@') continue;
            const size_t pl = (size_t)(p - s);
            if (!AnonNamespacePart(s, pl))
                for (size_t c = 0; c < pl; ++c)
                {
                    const char x = s[c];
                    if (!((x >= 'A' && x <= 'Z') || (x >= 'a' && x <= 'z') || (x >= '0' && x <= '9') || x == '_')) { plain = false; break; }
                }
            s = p + 1;
        }
    }
    if (!plain)
    {
        for (const char* p = deco; *p != 0 && len + 1 < n; ++p) out[len++] = (*p >= 0x21 && *p < 0x7F) ? *p : '?';
        out[len] = 0;
        return false;
    }
    /* the parts, innermost first, end at "@@": written outermost first, joined by "::" */
    const char* comp[kTypeParts];
    size_t clen[kTypeParts];
    int k = 0;
    bool more = false;
    const char* s = body;
    for (const char* p = body; p <= end; ++p)
        if (p == end || *p == '@')
        {
            if (k == kTypeParts) { if (p > s) more = true; break; }
            comp[k] = s; clen[k] = (size_t)(p - s); ++k; s = p + 1;
        }
    bool first = true;
    if (more) { Put(out, n, &len, "..."); first = false; }
    for (int i = k - 1; i >= 0; --i)
    {
        if (clen[i] == 0) continue;
        if (!first) Put(out, n, &len, "::");
        first = false;
        if (AnonNamespacePart(comp[i], clen[i])) { Put(out, n, &len, "`anonymous namespace'"); continue; }
        for (size_t c = 0; c < clen[i] && len + 1 < n; ++c) out[len++] = comp[i][c];
        out[len] = 0;
    }
    return true;
}

/* The line's head, up to and with the time: "[CRASH] <road> code=.. [(..)] [type=..] at <place> thread=<id> <kind>[ utc=..]" */
inline void PutHead(char* out, size_t n, size_t* len, int road, unsigned long code, int avKind, unsigned long long avAddress,
                    const char* typeName, const Place& at, unsigned long thread, int mainThread, unsigned long long utcSec)
{
    Put(out, n, len, kLineText);
    Put(out, n, len, RoadText(road));
    if (road == kRoadAbort) Put(out, n, len, " code=SIGABRT");
    else
    {
        Put(out, n, len, " code=");
        PutHex(out, n, len, code, 8);
        if (code == kCodeAccessViolation && avKind >= 0)
        {
            Put(out, n, len, " (");
            Put(out, n, len, AvKindText(avKind));
            Put(out, n, len, " of ");
            PutHex(out, n, len, avAddress, 1);
            Put(out, n, len, ")");
        }
        if (code == kCodeCppThrow && typeName != 0 && typeName[0] != 0) { Put(out, n, len, " type="); Put(out, n, len, typeName); }
    }
    Put(out, n, len, " at ");
    PutPlace(out, n, len, at);
    Put(out, n, len, " thread=");
    PutDec(out, n, len, thread);
    Put(out, n, len, mainThread == 1 ? " main" : mainThread == 0 ? " worker" : " thread-kind-unknown");
    if (utcSec != 0) { Put(out, n, len, " utc="); PutUtc(out, n, len, utcSec); }
}

/* THE LINE, from a record. Returns its length (it always fits n - 1; a cut line still ends with what fitted). */
inline size_t FormatLine(const Record& r, char* out, size_t n)
{
    size_t len = 0;
    if (n == 0) return 0;
    out[0] = 0;
    PutHead(out, n, &len, r.road, r.code, r.avKind, r.avAddress, r.typeName, r.at, r.thread, r.mainThread, r.utcSec);
    Put(out, n, &len, " stack:");
    if (r.frames <= 0) Put(out, n, &len, " none");
    for (int i = 0; i < r.frames && i < kMaxFrames; ++i)
    {
        Put(out, n, &len, i == 0 ? " " : " < ");
        PutPlace(out, n, &len, r.frame[i]);
    }
    Put(out, n, &len, "; ");
    Put(out, n, &len, r.version[0] != 0 ? r.version : "version unknown");
    return len;
}

/* THE SHORT LINE: a stack overflow, or too little stack left to walk it (crashwatch.cpp) - no Record (about 1 KB) and no walk:
   the same head, "stack: not walked (<why>)" and the version. `why` is kShort*. Returns its length, as FormatLine. */
inline size_t FormatShortLine(int road, unsigned long code, int avKind, unsigned long long avAddress, const Place& at, unsigned long thread,
                              int mainThread, unsigned long long utcSec, int why, const char* version, char* out, size_t n)
{
    size_t len = 0;
    if (n == 0) return 0;
    out[0] = 0;
    PutHead(out, n, &len, road, code, avKind, avAddress, "", at, thread, mainThread, utcSec);
    Put(out, n, &len, why == kShortStackOverflow ? " stack: not walked (a stack overflow)" : " stack: not walked (under 16 KB of stack left)");
    Put(out, n, &len, "; ");
    Put(out, n, &len, version != 0 && version[0] != 0 ? version : "version unknown");
    return len;
}

/* "mod <modVersion>, game <game>" - game is the address table's name, else the executable's fingerprint, else "unknown" */
inline void VersionText(const char* modVersion, const char* table, const char* fingerprint, char* out, size_t n)
{
    size_t len = 0;
    if (n == 0) return;
    out[0] = 0;
    Put(out, n, &len, "mod ");
    Put(out, n, &len, modVersion != 0 && modVersion[0] != 0 ? modVersion : "unknown");
    Put(out, n, &len, ", game ");
    Put(out, n, &len, table != 0 && table[0] != 0 ? table : fingerprint != 0 && fingerprint[0] != 0 ? fingerprint : "unknown");
}

}   /* namespace swcrash */
#endif
