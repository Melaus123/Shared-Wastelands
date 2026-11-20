/* winrecords.h - HOW THE LAST LAUNCH ENDED, FROM WINDOWS' OWN RECORDS (T-1344, owner 1653).
 *
 * The crash keeper (crashkeep.h) calls a launch "did not quit normally" at the next start, but its log cannot say how it died:
 * the mod catches nothing as the game dies. Windows keeps its own records of a program that faulted or hung and of a shutdown
 * that was not clean. So at that same start - only after that verdict - the keeper reads them (winrecords_read.h) and writes
 * ONE more [CRASHKEEP] line:
 *   - the Application log: Application Error (event 1000), Windows Error Reporting (1001) and Application Hang (1002) entries
 *     whose program is the game's exe - exception code, the faulting module's FILE NAME, offset, time;
 *   - the System log: unexpected shutdown (6008), Kernel-Power 41 (rebooted without a clean shutdown), a shutdown or restart
 *     started (1074) - time and kind only;
 *   - the names of crash-dump files for the game exe in %LOCALAPPDATA%\CrashDumps, and the Kenshi crash file the keeper found.
 * PRIVACY: an entry's XML carries full paths (AppPath, ModulePath, AttachedFiles), the machine name, a user SID and, in 1074, a
 * user name. Only the fields named above are ever picked, and every one passes FileNameOnly + Safe here: no path, user name,
 * machine name or SID can reach the line.
 *
 * THIS HEADER IS THE PURE PART: the time window, the field picking from an entry's rendered XML, the stripping, the line and its
 * caps. Shared with the offline suite. C++03 (VS2010 v100). Times are whole seconds since 1601 (FILETIME / 10^7, as
 * crashkeep's CkFileTimeSec gives them).
 */
#ifndef COOP_COMMON_WINRECORDS_H
#define COOP_COMMON_WINRECORDS_H

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace swwinrec {

enum { kPerKind = 3 };                 /* the newest this many entries of each kind */
enum { kLineCap = 600 };               /* the line's most bytes */
enum { kBudgetMs = 200 };              /* the most the reading may hold up the start (both logs together) */
enum { kAppShareMs = 130 };            /* the Application log's share of it; the System log gets the rest */
const long long kSlackSec = 60;        /* the window opens this much before the launch's start */
const long long kMaxWindowSec = 7ll * 24 * 3600;   /* never further back than a week */
const long long kDefaultWindowSec = 24ll * 3600;   /* no start known: the last day */
const unsigned long kErrTimeout = 1460;            /* ERROR_TIMEOUT: the search was cut at its time limit */

/* the kinds, in the line's order */
enum { kAppError = 0, kWer = 1, kHang = 2, kUnexpected = 3, kKernelPower = 4, kShutdownStarted = 5, kKinds = 6 };

/* THE WINDOW: from the previous launch's start to now. The start is its log's creation time; Windows can hand a new file the
   creation time of a file of the same name deleted or renamed in the 15 s before ("tunnelling"), so the start is never taken
   before the launch before it ended (`olderEnd`, copy 2's last write). Either missing: the other one; both: the last day.
   kSlackSec earlier, never more than kMaxWindowSec back, never after now. Returns the window's opening second. */
inline long long WindowStart(long long created, long long olderEnd, long long now)
{
    long long s = created;
    if (s <= 0 || (olderEnd > 0 && olderEnd > s)) s = olderEnd;
    if (s <= 0) s = now - kDefaultWindowSec;
    s -= kSlackSec;
    if (s < now - kMaxWindowSec) s = now - kMaxWindowSec;
    if (s > now) s = now;
    return s;
}

/* The event log's query for the window: entries no older than `ageMs` (timediff counts back from the moment of the query). The
   Application log's is narrowed to program `exe` by the entry's named field (AppName for 1000 / 1002, P1 for 1001; the event log
   compares it without case - measured on this machine 2026-11-15), so other programs' entries cost no search time; "*" = any
   program. Windows 7 writes those fields unnamed, so there the narrowed query finds nothing (Inferred). */
inline std::string Safe(const std::string& s, size_t most);
inline std::string AppQuery(unsigned long long ageMs, const std::string& exe)
{
    char b[400];
    std::sprintf(b, "*[System[Provider[@Name='Application Error' or @Name='Windows Error Reporting' or @Name='Application Hang'] and "
                    "(EventID=1000 or EventID=1001 or EventID=1002) and TimeCreated[timediff(@SystemTime) <= %llu]]", ageMs);
    std::string q = b;
    if (exe != "*")
    {
        const std::string e = Safe(exe, 64);   /* no quote can reach the query */
        q += " and EventData[Data[@Name='AppName']='" + e + "' or Data[@Name='P1']='" + e + "']";
    }
    return q + "]";
}
inline std::string SysQuery(unsigned long long ageMs)
{
    char b[400];
    std::sprintf(b, "*[System[Provider[@Name='EventLog' or @Name='Microsoft-Windows-Kernel-Power' or @Name='User32'] and "
                    "(EventID=6008 or EventID=41 or EventID=1074) and TimeCreated[timediff(@SystemTime) <= %llu]]]", ageMs);
    return b;
}

/* ---- picking fields out of an entry's rendered XML ---- */

/* &amp; &lt; &gt; &apos; &quot; undone; anything else is left as it is */
inline std::string XmlDecode(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '&')
        {
            static const char* const ent[5] = { "&amp;", "&lt;", "&gt;", "&apos;", "&quot;" };
            static const char rep[5] = { '&', '<', '>', '\'', '"' };
            bool done = false;
            for (int k = 0; k < 5 && !done; ++k)
            {
                const size_t n = std::strlen(ent[k]);
                if (s.compare(i, n, ent[k]) == 0) { o += rep[k]; i += n - 1; done = true; }
            }
            if (done) continue;
        }
        o += s[i];
    }
    return o;
}
/* The text inside the first <tag ...>text</tag> (the tag name whole: "<EventID" does not match "<EventIDx"); empty if none. */
inline std::string XmlText(const std::string& xml, const char* tag)
{
    const std::string open = std::string("<") + tag;
    size_t at = 0;
    while ((at = xml.find(open, at)) != std::string::npos)
    {
        const size_t after = at + open.size();
        if (after < xml.size() && (xml[after] == '>' || xml[after] == ' ' || xml[after] == '/'))
        {
            const size_t gt = xml.find('>', after);
            if (gt == std::string::npos || xml[gt - 1] == '/') return std::string();
            const size_t lt = xml.find('<', gt + 1);
            return lt == std::string::npos ? std::string() : XmlDecode(xml.substr(gt + 1, lt - gt - 1));
        }
        at = after;
    }
    return std::string();
}
/* The value of attribute `attr` on the first <tag ...> (either quote); empty if none. */
inline std::string XmlAttr(const std::string& xml, const char* tag, const char* attr)
{
    const std::string open = std::string("<") + tag + " ";
    const size_t at = xml.find(open);
    if (at == std::string::npos) return std::string();
    const size_t gt = xml.find('>', at);
    const std::string head = xml.substr(at, gt == std::string::npos ? std::string::npos : gt - at);
    const std::string key = std::string(" ") + attr + "=";
    const size_t k = head.find(key);
    if (k == std::string::npos || k + key.size() >= head.size()) return std::string();
    const char q = head[k + key.size()];
    if (q != '\'' && q != '"') return std::string();
    const size_t end = head.find(q, k + key.size() + 1);
    if (end == std::string::npos) return std::string();
    return XmlDecode(head.substr(k + key.size() + 1, end - k - key.size() - 1));
}
/* An EventData field: <Data Name='name'>value</Data> when the entry names its fields (Windows 8 and later), else the
   `index`-th <Data> (0 = first; older Windows writes them unnamed, in the same order). A <Data .../> is empty. */
inline std::string DataField(const std::string& xml, const char* name, int index)
{
    size_t at = 0;
    int n = 0;
    bool anyNamed = false;
    std::string positional;
    bool havePositional = false;
    while ((at = xml.find("<Data", at)) != std::string::npos)
    {
        const size_t after = at + 5;
        if (after >= xml.size() || (xml[after] != '>' && xml[after] != ' ' && xml[after] != '/')) { at = after; continue; }
        const size_t gt = xml.find('>', after);
        if (gt == std::string::npos) break;
        const std::string head = xml.substr(at, gt - at + 1);
        std::string value;
        if (xml[gt - 1] != '/')
        {
            const size_t lt = xml.find("</Data>", gt + 1);
            if (lt == std::string::npos) break;
            value = XmlDecode(xml.substr(gt + 1, lt - gt - 1));
        }
        const std::string nm = XmlAttr(head, "Data", "Name");
        if (!nm.empty())
        {
            anyNamed = true;
            if (nm == name) return value;
        }
        else if (n == index) { positional = value; havePositional = true; }
        ++n;
        at = gt + 1;
    }
    return (!anyNamed && havePositional) ? positional : std::string();
}

/* ---- stripping: what may reach the line ---- */

/* The last part of a path (after the last \ or /): a module given as C:\Users\<name>\...\x.dll is x.dll. */
inline std::string FileNameOnly(const std::string& s)
{
    const size_t k = s.find_last_of("\\/");
    return k == std::string::npos ? s : s.substr(k + 1);
}
/* Only the characters a file name, version or code is made of (letters, digits, . _ - + ~ ( )), at most `most`; anything
   else - a space, a colon, a quote, a byte above 127 - ends the name there. "?" when nothing is left. */
inline std::string Safe(const std::string& s, size_t most)
{
    std::string o;
    for (size_t i = 0; i < s.size() && o.size() < most; ++i)
    {
        const char c = s[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'
                        || c == '+' || c == '~' || c == '(' || c == ')';
        if (!ok) break;
        o += c;
    }
    return o.empty() ? std::string("?") : o;
}
/* A file name from a field that may hold a path: the file part, then Safe. */
inline std::string SafeFile(const std::string& s) { return Safe(FileNameOnly(s), 64); }
/* A hex number as "0x" + its digits without leading zeros ("00000000003590bf" -> "0x3590bf", "0xc0000005" -> "0xc0000005");
   "?" when it is not hex. */
inline std::string Hex(const std::string& s)
{
    std::string h = s;
    if (h.size() > 2 && h[0] == '0' && (h[1] == 'x' || h[1] == 'X')) h = h.substr(2);
    if (h.empty() || h.size() > 16) return "?";
    for (size_t i = 0; i < h.size(); ++i)
    {
        const char c = h[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return "?";
    }
    size_t z = 0;
    while (z + 1 < h.size() && h[z] == '0') ++z;
    return "0x" + h.substr(z);
}
/* "2026-11-13T08:54:39.7493417Z" -> "2026-11-13 08:54:39Z"; "?" when it is not that shape. */
inline std::string UtcTime(const std::string& s)
{
    if (s.size() < 19) return "?";
    for (size_t i = 0; i < 19; ++i)
    {
        const char c = s[i];
        const bool digit = c >= '0' && c <= '9';
        if (i == 4 || i == 7) { if (c != '-') return "?"; }
        else if (i == 10) { if (c != 'T') return "?"; }
        else if (i == 13 || i == 16) { if (c != ':') return "?"; }
        else if (!digit) return "?";
    }
    return s.substr(0, 10) + " " + s.substr(11, 8) + "Z";
}
/* the same name without regard to case (ASCII) */
inline bool SameName(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        char p = a[i], q = b[i];
        if (p >= 'A' && p <= 'Z') p = (char)(p - 'A' + 'a');
        if (q >= 'A' && q <= 'Z') q = (char)(q - 'A' + 'a');
        if (p != q) return false;
    }
    return true;
}
/* A crash dump Windows writes for the game: "<exe>.<digits>.dmp" (any case). */
inline bool IsGameDump(const std::string& name, const std::string& exe)
{
    if (exe.empty() || name.size() < exe.size() + 6 || !SameName(name.substr(0, exe.size()), exe) || name[exe.size()] != '.') return false;
    if (!SameName(name.substr(name.size() - 4), ".dmp")) return false;
    size_t i = exe.size() + 1;
    const size_t end = name.size() - 4;
    if (i >= end) return false;
    for (; i < end; ++i) if (name[i] < '0' || name[i] > '9') return false;
    return true;
}

/* ---- one entry ---- */
struct Record
{
    int kind;
    std::string time;     /* UtcTime */
    std::string program;  /* SafeFile */
    std::string detail;   /* the rest, already stripped: "code=.. module=.. offset=.." etc. */
    Record() : kind(-1) {}
};

/* An Application log entry for program `exe` (compared without case; "*" = any program, the local check's switch) as a Record.
   False when it is not one of the three kinds or not that program. */
inline bool ParseApp(const std::string& xml, const std::string& exe, Record* r)
{
    const std::string id = XmlText(xml, "EventID");
    std::string app;
    if (id == "1000")
    {
        app = DataField(xml, "AppName", 0);
        r->kind = kAppError;
        r->detail = "code=" + Hex(DataField(xml, "ExceptionCode", 6)) + " module=" + SafeFile(DataField(xml, "ModuleName", 3))
                    + " offset=" + Hex(DataField(xml, "FaultingOffset", 7));
    }
    else if (id == "1001")
    {
        app = DataField(xml, "P1", 5);
        r->kind = kWer;
        /* the report's kind (APPCRASH, BEX64, AppHangB1 ...) and its P4 / P7 / P8 (for APPCRASH: module, code, offset; for BEX64:
           module, offset, code) - kept as the report gives them, stripped */
        r->detail = Safe(DataField(xml, "EventName", 2), 32) + " p4=" + SafeFile(DataField(xml, "P4", 8)) + " p7="
                    + Safe(DataField(xml, "P7", 11), 24) + " p8=" + Safe(DataField(xml, "P8", 12), 24);
    }
    else if (id == "1002")
    {
        app = DataField(xml, "AppName", 0);
        r->kind = kHang;
        r->detail.clear();
    }
    else return false;
    app = FileNameOnly(app);
    if (exe != "*" && !SameName(app, exe)) return false;
    r->program = SafeFile(app);
    r->time = UtcTime(XmlAttr(xml, "TimeCreated", "SystemTime"));
    return true;
}
/* A System log entry of the three kinds as a Record (time and kind only; 1074's kind is its "restart" / "power off" word, letters
   and spaces only - its program path and user name are never read). */
inline bool ParseSys(const std::string& xml, Record* r)
{
    const std::string id = XmlText(xml, "EventID");
    r->program.clear();
    r->detail.clear();
    if (id == "6008") r->kind = kUnexpected;
    else if (id == "41") r->kind = kKernelPower;
    else if (id == "1074")
    {
        r->kind = kShutdownStarted;
        const std::string t = DataField(xml, "param5", 4);
        for (size_t i = 0; i < t.size() && r->detail.size() < 16; ++i)
        {
            const char c = t[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == ' ') r->detail += c;
            else break;
        }
    }
    else return false;
    r->time = UtcTime(XmlAttr(xml, "TimeCreated", "SystemTime"));
    return true;
}

/* one entry's words in the line */
inline std::string RecordWords(const Record& r)
{
    switch (r.kind)
    {
    case kAppError: return "app-error " + r.program + " " + r.detail + " at " + r.time;
    case kWer: return "error report " + r.program + " " + r.detail + " at " + r.time;
    case kHang: return "hang " + r.program + " at " + r.time;
    case kUnexpected: return "unexpected shutdown (6008) logged at " + r.time;
    case kKernelPower: return "restart without a clean shutdown (kernel-power 41) at " + r.time;
    case kShutdownStarted: return "shutdown/restart started (" + (r.detail.empty() ? std::string("?") : r.detail) + ") at " + r.time;
    }
    return "?";
}
/* The entries of kinds [from, to), the newest kPerKind of each, in the order given (the reader gives newest first). */
inline std::string KindsWords(const std::vector<Record>& recs, int from, int to)
{
    std::string s;
    for (int k = from; k < to; ++k)
    {
        int n = 0;
        for (size_t i = 0; i < recs.size() && n < kPerKind; ++i)
        {
            if (recs[i].kind != k) continue;
            if (!s.empty()) s += ", ";
            s += RecordWords(recs[i]);
            ++n;
        }
    }
    return s;
}
inline std::string ErrWords(unsigned long err)
{
    char b[64];
    if (err == kErrTimeout) return "not finished within the time limit (1460)";
    std::sprintf(b, "could not read (%lu)", err);
    return b;
}

/* THE LINE. `appErr` / `sysErr`: 0, or the Windows error that stopped that log's reading (kErrTimeout: cut at its time limit -
   what was found before stays). `dumps`: crash-dump file names (newest first); `kenshiFile`: the keeper's Kenshi crash file
   name or empty; `windowMin`: how many minutes before the reading the window opened. At most kLineCap bytes. */
inline std::string Line(const std::vector<Record>& recs, unsigned long appErr, unsigned long sysErr, const std::vector<std::string>& dumps,
                        const std::string& kenshiFile, long long windowMin)
{
    char b[96];
    std::sprintf(b, "%lld", windowMin < 0 ? 0ll : windowMin);
    std::string s = std::string("[CRASHKEEP] how the last launch ended (Windows records, last ") + b + " min): ";
    const std::string app = KindsWords(recs, kAppError, kHang + 1);
    if (!app.empty()) s += app + (appErr != 0 ? " (" + ErrWords(appErr) + ")" : std::string());
    else if (appErr != 0) s += "application log " + ErrWords(appErr);
    else s += "no Windows crash entry (a kill, a power loss or a close Windows does not record)";
    const std::string sys = KindsWords(recs, kUnexpected, kKinds);
    s += "; system: ";
    if (!sys.empty()) s += sys + (sysErr != 0 ? " (" + ErrWords(sysErr) + ")" : std::string());
    else if (sysErr != 0) s += ErrWords(sysErr);
    else s += "none";
    s += "; dumps: ";
    if (dumps.empty()) s += "none";
    for (size_t i = 0; i < dumps.size() && i < (size_t)kPerKind; ++i) s += (i ? ", " : "") + SafeFile(dumps[i]);
    s += "; Kenshi crash file: " + (kenshiFile.empty() ? std::string("none") : SafeFile(kenshiFile));
    if (s.size() > (size_t)kLineCap) s = s.substr(0, kLineCap - 3) + "...";
    return s;
}

}   /* namespace swwinrec */
#endif
