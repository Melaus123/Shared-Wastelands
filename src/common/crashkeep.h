/* crashkeep.h - A LAUNCH THAT DID NOT QUIT NORMALLY, KEPT WHERE RESTARTS NEVER CLEAR IT.
 *
 * The mod keeps the logs of the last cooplogrot::kLogKeep launches (src/common/logrotate.h), and a bug report carries them. A
 * player who restarts a few times after a crash before pressing REPORT A BUG has rotated the crashed launch's log away. So at
 * start-up, right after the rotation (coop_log.cpp), copy 1 - the previous launch's log - is judged, and when that launch did
 * not quit normally every part of its log and Kenshi's crash file that ended it are COPIED into
 * <mod folder>\crashes\<entry>\, a folder no restart clears; the newest kKeepEntries entries stay. Every later bug report
 * carries them (bugreport.cpp), cut first when the report is too big. Nothing asks the player, nothing is sent by itself.
 *
 * THE JUDGEMENT (Judge): the previous launch did not quit normally when its log
 *   - exists (else "no previous log") and opens without sharing writes (a game still writing it holds it open: "still running",
 *     not a crash; any other failure to open or read is "unreadable", never taken for "missing");
 *   - holds kHookOkText - written at start-up only when the exit hook AND every source of a quit route line are installed
 *     (RoutesMarked; store.cpp InstallStore), looked for in the first kHeadBytes of the launch's opening part ".start" and of its
 *     newest part. Without it the launch cannot be judged: an older build, or a hook that did not go in;
 *   - and holds no kRouteText line in the newest kRouteTailBytes of its newest part (or of its middle part ".prev"): every road
 *     that raises Kenshi's quit byte writes one the moment the quit is decided - the window close (its X, WM_CLOSE), the pause
 *     menu's EXIT GAME accepted, the title screen's EXIT (store.cpp) and the title screen's Escape (ui.cpp). "[EXIT] quit
 *     complete" proves nothing: Kenshi's own crash handling ends in the same exit routine, so a crashed launch writes it too.
 *
 * AN ENTRY: a folder named EntryName - the UTC time it was made and this process's id ("20261102-141502-8812"), with "-<n>"
 * added when that name is taken - built under the name + kPartialSuffix and renamed when every file is in, so an entry is never
 * half made: a ".part" folder is a copy that was cut off, removed with the old entries. The log parts take the names a launch's
 * own parts have (shared_wastelands_log.txt, .start.txt, .prev.txt); the crash file keeps its own name.
 *
 * PURE: shared with the offline suite. C++03 (VS2010 v100).
 */
#ifndef COOP_COMMON_CRASHKEEP_H
#define COOP_COMMON_CRASHKEEP_H

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "bugreport.h"   /* coopbug::BudgetPart, kOrderKeptCrash, kCrashNearSec */

namespace swcrashkeep {

enum { kKeepEntries = 5 };
const unsigned long long kRouteTailBytes = 4ull * 1024ull * 1024ull;   /* a quit route line is looked for in this many newest bytes */
const unsigned long long kHeadBytes = 4ull * 1024ull * 1024ull;        /* the hook line in this many first bytes of a part */
const char* const kHookOkText = "[EXIT] quit routes marked: ";   /* followed by the roads' names */
const char* const kRouteText  = "[EXIT] quit route: ";
const char* const kFolder     = "crashes";
const wchar_t* const kFolderW = L"crashes";
const char* const kPartialSuffix = ".part";

/* what reading the previous launch's log found */
enum { kLogMissing = 0, kLogHeld = 1, kLogUnreadable = 2, kLogRead = 3 };
/* the judgement, and the other answers the start-up can give */
enum { kVerdictNoLog = 0, kVerdictRunning = 1, kVerdictUnreadable = 2, kVerdictNoHook = 3, kVerdictQuit = 4, kVerdictCrashed = 5,
       kVerdictNotMoved = 6 };

inline int Judge(int logState, bool hookOkSeen, bool routeSeen)
{
    if (logState == kLogMissing) return kVerdictNoLog;
    if (logState == kLogHeld) return kVerdictRunning;
    if (logState != kLogRead) return kVerdictUnreadable;
    if (!hookOkSeen) return kVerdictNoHook;
    return routeSeen ? kVerdictQuit : kVerdictCrashed;
}

/* Every source the judgement relies on is installed: the exit hook, the quit byte's address (the window close raises it through
   it), the window-close, menu-quit and title-exit hooks and the title screen's Escape gate. */
inline bool RoutesMarked(bool exitHook, bool quitFlag, bool windowHook, bool menuHook, bool titleHook, bool titleEscGate)
{
    return exitHook && quitFlag && windowHook && menuHook && titleHook && titleEscGate;
}

/* What reading the previous launch's log found, from each step's result. tailRead: the newest part's newest kRouteTailBytes were
   read; tailRoute: they hold a route line. startHas: the opening part (.start) holds the marker line - 1 yes, 0 no, -1 no such
   part, -2 could not be read; headHas: the same for the newest part's first kHeadBytes (used when startHas is not 1); prevHas:
   the middle part's (.prev) newest bytes hold a route line, same values (used when the tail has none). A step that was needed
   and could not be read makes the log kLogUnreadable - never "no marker" or "no route line". */
inline int ReadState(bool tailRead, bool tailRoute, int startHas, int headHas, int prevHas, bool* hook, bool* route)
{
    *hook = false;
    *route = false;
    if (!tailRead || startHas == -2) return kLogUnreadable;
    if (startHas == 1) *hook = true;
    else if (headHas == -2) return kLogUnreadable;
    else *hook = headHas == 1;
    if (tailRoute) *route = true;
    else if (prevHas == -2) return kLogUnreadable;
    else *route = prevHas == 1;
    return kLogRead;
}

/* `needle` somewhere in the n bytes at p */
inline bool HasText(const char* p, size_t n, const char* needle)
{
    const size_t m = std::strlen(needle);
    if (p == 0 || m == 0 || n < m) return false;
    for (size_t i = 0; i + m <= n; ++i)
        if (p[i] == needle[0] && std::memcmp(p + i, needle, m) == 0) return true;
    return false;
}

/* "yyyymmdd-hhmmss-<pid>", and "-<attempt>" after it for attempt 1 and up */
inline std::string EntryName(int year, int month, int day, int hour, int minute, int second, unsigned long pid, int attempt)
{
    char b[64];
    if (attempt > 0)
        std::sprintf(b, "%04d%02d%02d-%02d%02d%02d-%lu-%d", year, month, day, hour, minute, second, pid, attempt);
    else
        std::sprintf(b, "%04d%02d%02d-%02d%02d%02d-%lu", year, month, day, hour, minute, second, pid);
    return b;
}

/* a run of 1 to `most` digits at s[*i], moved past */
inline bool Digits(const std::string& s, size_t* i, size_t least, size_t most)
{
    size_t n = 0;
    while (*i < s.size() && s[*i] >= '0' && s[*i] <= '9' && n < most + 1) { ++*i; ++n; }
    return n >= least && n <= most;
}
/* A name EntryName makes, and nothing else - only such folders are ever counted or removed. */
inline bool IsEntryName(const std::string& s)
{
    size_t i = 0;
    if (!Digits(s, &i, 8, 8) || i >= s.size() || s[i++] != '-') return false;
    if (!Digits(s, &i, 6, 6) || i >= s.size() || s[i++] != '-') return false;
    if (!Digits(s, &i, 1, 10)) return false;
    if (i == s.size()) return true;
    if (s[i++] != '-') return false;
    return Digits(s, &i, 1, 3) && i == s.size();
}
/* An entry's folder while it is being made (a cut-off copy when it is still there later). */
inline bool IsPartialName(const std::string& s)
{
    const size_t m = std::strlen(kPartialSuffix);
    return s.size() > m && s.compare(s.size() - m, m, kPartialSuffix) == 0 && IsEntryName(s.substr(0, s.size() - m));
}

/* The entries among `names`, newest first (the name starts with its UTC time, so the name's order is the time order). */
inline std::vector<std::string> EntriesNewestFirst(const std::vector<std::string>& names)
{
    std::vector<std::string> e;
    for (size_t i = 0; i < names.size(); ++i) if (IsEntryName(names[i])) e.push_back(names[i]);
    std::sort(e.begin(), e.end());
    std::reverse(e.begin(), e.end());
    return e;
}
/* The entries to remove so that at most `keep` stay, the newest kept; a name that is not an entry is never named. */
inline std::vector<std::string> EntriesToRemove(const std::vector<std::string>& names, size_t keep)
{
    const std::vector<std::string> e = EntriesNewestFirst(names);
    std::vector<std::string> out;
    for (size_t i = keep; i < e.size(); ++i) out.push_back(e[i]);
    return out;
}

/* A kept log is the same file as one of the kept launches' logs the report carries anyway (copy 1 or 2): same size and same
   last-write second (a copy keeps its source's write time). An absent file (size 0 or time 0) matches nothing. */
inline bool SameLog(unsigned long long sizeA, long long writeA, unsigned long long sizeB, long long writeB)
{
    return sizeA != 0 && writeA != 0 && sizeA == sizeB && writeA == writeB;
}

/* THE REPORT'S SHARE. Every file of every entry has coopbug::kOrderKeptCrash: below every other order, so it is cut before any
   other log loses a byte, and it has no floor (SetBudgetFloors shares the room among the other kinds only) - it gets the room
   the rest leaves. Within it the oldest entry goes first and, in one entry, its crash file, then its middle part, then its
   opening part, then its newest part (cutFirst; never 1, which a launch's opening part of the other orders uses). */
enum { kKindNewest = 0, kKindOpening = 1, kKindMiddle = 2, kKindCrashFile = 3 };
inline int KeptCutFirst(int age, int kind) { return 10 + 4 * age + kind; }
/* A kept log: its segments are cut from the start, an opening part's from the end (its first bytes stay). */
inline coopbug::BudgetPart KeptLogPart(unsigned long long overhead, const std::vector<unsigned long long>& segBytes, int age, int kind)
{
    coopbug::BudgetPart p;
    p.fixed = overhead + sizeof(coopbug::kFinalBlock);
    p.segBytes = segBytes;
    p.order = coopbug::kOrderKeptCrash;
    p.cutFirst = KeptCutFirst(age, kind);
    p.cutFromEnd = kind == kKindOpening;
    return p;
}
/* A kept crash file is stored whole or not at all: its bytes are ONE segment, so it is cut in the first pass with the kept logs
   (a fixed, droppable part would only go after every other log had lost all it could). */
inline coopbug::BudgetPart KeptFilePart(unsigned long long overhead, unsigned long long bytes, int age)
{
    coopbug::BudgetPart p;
    p.fixed = overhead;
    p.segBytes.push_back(bytes);
    p.order = coopbug::kOrderKeptCrash;
    p.cutFirst = KeptCutFirst(age, kKindCrashFile);
    return p;
}

/* THE FOLDERS KENSHI'S CRASH FILE IS LOOKED FOR IN: the running program's folder and the main game folder (two folders up from
   the mod's folder, <game>\mods\<mod>\) - they differ when the program runs from a subfolder - each once (compared without
   case, a trailing slash or backslash added). Empty names are left out. Works on std::string and std::wstring. */
template <class S>
S FolderWithSlash(const S& f)
{
    typedef typename S::value_type C;
    if (f.empty() || f[f.size() - 1] == (C)'\\' || f[f.size() - 1] == (C)'/') return f;
    return f + (C)'\\';
}
template <class S>
S FolderUp(const S& folder, int levels)
{
    typedef typename S::value_type C;
    S f = FolderWithSlash(folder);
    for (int i = 0; i < levels && !f.empty(); ++i)
    {
        f.erase(f.size() - 1);   /* the trailing separator */
        typename S::size_type k = S::npos;
        for (typename S::size_type j = 0; j < f.size(); ++j) if (f[j] == (C)'\\' || f[j] == (C)'/') k = j;
        f = (k == S::npos) ? S() : f.substr(0, k + 1);
    }
    return f;
}
template <class S>
bool SameFolder(const S& a, const S& b)
{
    typedef typename S::value_type C;
    const S x = FolderWithSlash(a), y = FolderWithSlash(b);
    if (x.size() != y.size()) return false;
    for (typename S::size_type i = 0; i < x.size(); ++i)
    {
        C p = x[i], q = y[i];
        if (p == (C)'/') p = (C)'\\';
        if (q == (C)'/') q = (C)'\\';
        if (p >= (C)'A' && p <= (C)'Z') p = (C)(p - (C)'A' + (C)'a');
        if (q >= (C)'A' && q <= (C)'Z') q = (C)(q - (C)'A' + (C)'a');
        if (p != q) return false;
    }
    return true;
}
template <class S>
std::vector<S> CrashFolders(const S& programFolder, const S& modFolder)
{
    std::vector<S> out;
    S c[2];
    c[0] = FolderWithSlash(programFolder);
    c[1] = modFolder.empty() ? S() : FolderUp(modFolder, 2);
    for (int i = 0; i < 2; ++i)
    {
        if (c[i].empty()) continue;
        bool dup = false;
        for (size_t k = 0; k < out.size(); ++k) if (SameFolder(out[k], c[i])) dup = true;
        if (!dup) out.push_back(c[i]);
    }
    return out;
}

/* THE LOG LINE, written once after the version line. `entry` / `parts` / `crashFile` / `removed` are used by kVerdictCrashed;
   `err` by an unreadable log and a failed copy (copyFailed). The rule in one line: kept when the exit hook's install line is
   there and no quit route line follows it. */
inline std::string NoteLine(int verdict, bool copyFailed, unsigned long err, const std::string& entry, unsigned long long logBytes,
                            const std::string& parts, const std::string& crashFile, int kept, const std::string& removed)
{
    char b[96];
    if (verdict == kVerdictNoLog) return "[CRASHKEEP] no previous log";
    if (verdict == kVerdictRunning) return "[CRASHKEEP] previous launch still running (log held open) - not kept";
    if (verdict == kVerdictNoHook) return "[CRASHKEEP] previous launch's quit routes were not all marked (an exit or quit-road hook not installed, or an older build) - not judged";
    if (verdict == kVerdictQuit) return "[CRASHKEEP] previous launch quit normally";
    if (verdict == kVerdictNotMoved) return "[CRASHKEEP] previous launch's log did not move to copy 1 (log rotation failed) - not judged";
    if (verdict == kVerdictUnreadable)
    {
        std::sprintf(b, "%lu", err);
        return std::string("[CRASHKEEP] previous launch's log could not be read (Windows error ") + b + ") - not judged";
    }
    if (copyFailed)
    {
        std::sprintf(b, "%lu", err);
        return std::string("[CRASHKEEP] previous launch did not quit normally - copy failed (Windows error ") + b + "), nothing kept";
    }
    std::sprintf(b, "%llu", logBytes);
    std::string s = std::string("[CRASHKEEP] previous launch did not quit normally - kept in ") + kFolder + "\\" + entry + " (log " + b
                    + " bytes, parts " + parts + ", Kenshi crash file ";
    if (crashFile.empty()) { std::sprintf(b, "none within %u s", coopbug::kCrashNearSec); s += b; }
    else s += crashFile;
    std::sprintf(b, "); %d of %d kept, removed ", kept, (int)kKeepEntries);
    return s + b + (removed.empty() ? std::string("none") : removed);
}

}   /* namespace swcrashkeep */
#endif
