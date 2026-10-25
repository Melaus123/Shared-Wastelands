/* mig2 (RE_Kenshi migration stage 2, owner decision S2-60, 2026-09-28): OUR OWN log file - see coop_log.h.

   Rules this file keeps, because a logger that fails takes the evidence with it:
     - it never calls anything that logs (no DebugLog/ErrorLog, nothing of ours) - no recursion;
     - it never throws out of a log call and never crashes on a missing file: lines are dropped instead;
     - numbers are formatted with sprintf, not streams (RE_Kenshi's stream locale groups digits,
       common/addrtable.cpp);
     - every piece of state is zero-initialised POD, so a log call made from ANY static constructor, in any
       order, is safe. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include "coop_log.h"
#include "../common/logrotate.h"   /* the names of the kept copies of earlier launches' logs */
#include "../common/names.h"   /* the on-disk names (mod folder, files, window texts) */
#include "../common/modversion.h"   /* the mod's version, the log's first line */
#include "../common/linekeep.h"   /* T-910: the [KEEPER] lines kept in memory for the bug report */

namespace
{
    /* 0 = not yet, 1 = being initialised by one thread, 2 = ready. */
    volatile LONG    g_lockState = 0;
    CRITICAL_SECTION g_lock;

    /* guarded by g_lock */
    int           g_fileState = 0;              /* 0 = not open yet (each line tries again), 1 = open */
    HANDLE        g_file      = INVALID_HANDLE_VALUE;
    LARGE_INTEGER g_start;                       /* QueryPerformanceCounter at the first log call */
    LARGE_INTEGER g_freq;
    int           g_haveStart = 0;

    /* THE 10 MB CEILING (src/common/logrotate.h: CeilingStep, PartName). One block per process and log folder, shared by every
       copy of this DLL through a named file mapping, so two copies writing one file count its bytes together and both see a
       cut: `gen` moves at every cut, and a copy whose own gen differs closes its handle (which may name the renamed part) and
       opens the current file again. `rolling` lets one copy at a time rename. Without the mapping each copy keeps its own block. */
    struct LogShared
    {
        volatile LONG64 bytes;        /* bytes in the current part */
        volatile LONG64 prevBytes;    /* bytes in the .prev part now */
        volatile LONG64 cutBytes;     /* bytes deleted between .start and .prev this launch */
        volatile LONG64 slack;        /* extra room after a failed rename */
        volatile LONG   gen;          /* moves at every cut */
        volatile LONG   rolling;      /* 1 while one copy renames */
        volatile LONG   openingCut;   /* 1 once the opening part is .start */
        volatile LONG   parts;        /* parts begun after the opening one (the current part is number parts + 1) */
    };
    LogShared  g_ownShared;           /* zero-initialised: used when the mapping cannot be made */
    LogShared* g_shared = 0;          /* guarded by g_lock (the pointer); the block itself by its own interlocked fields */
    LONG       g_myGen  = 0;          /* guarded by g_lock: the gen this copy's handle was opened at */
    wchar_t    g_logName[2048];       /* guarded by g_lock: the current part's full path, "" until the file opened once */

    void EnsureLock()
    {
        if (g_lockState == 2) return;
        if (::InterlockedCompareExchange(&g_lockState, 1, 0) == 0)
        {
            ::InitializeCriticalSection(&g_lock);
            ::InterlockedExchange(&g_lockState, 2);
            return;
        }
        while (g_lockState != 2) ::Sleep(0);
    }

    /* The folder of the DLL this function lives in (SharedWastelands.dll), with a trailing backslash; empty on failure. */
    std::wstring DllFolder()
    {
        HMODULE mod = NULL;
        if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  (LPCWSTR)(void*)&DllFolder, &mod))
            return std::wstring();
        wchar_t path[2048];
        DWORD n = ::GetModuleFileNameW(mod, path, (DWORD)(sizeof(path) / sizeof(path[0])));
        if (n == 0 || n >= (DWORD)(sizeof(path) / sizeof(path[0]))) return std::wstring();
        std::wstring p(path, n);
        std::wstring::size_type slash = p.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return std::wstring();
        return p.substr(0, slash + 1);
    }

    /* guarded by g_lock: this copy has done (or skipped) the start-up rotation; "" or the one line saying the rotation
       failed, written into the new log by the first line that finds the file open */
    int  g_rotateDone = 0;
    char g_rotateNote[320];
    int  g_versionSaid = 0;   /* guarded by g_lock: the version line is this log's first line */

    /* The file part of a path, each wchar_t truncated to a char (the names here are ASCII). Allocates nothing. */
    void NarrowFilePart(const std::wstring& path, char* out, size_t outSize)
    {
        std::wstring::size_type slash = path.find_last_of(L"\\/");
        std::wstring::size_type i = (slash == std::wstring::npos) ? 0 : slash + 1;
        size_t k = 0;
        for (; i < path.size() && k + 1 < outSize; ++i) out[k++] = (char)path[i];
        out[k] = 0;
    }

    /* Keeps the first failed rotation step for the log; a later failure adds nothing. */
    void NoteRotateStep(const char* verb, const std::wstring& from, const std::wstring* to, DWORD err)
    {
        if (g_rotateNote[0]) return;
        char a[96], b[96];
        NarrowFilePart(from, a, sizeof(a));
        b[0] = 0;
        if (to) NarrowFilePart(*to, b, sizeof(b));
        _snprintf_s(g_rotateNote, sizeof(g_rotateNote), _TRUNCATE,
                    "log rotation failed: could not %s %s%s%s (Windows error %lu); this log opened anyway, the earlier launches' "
                    "logs may not all have moved", verb, a, to ? " to " : "", b, (unsigned long)err);
    }

    /* Caller holds g_lock. The ceiling's shared block for this log folder: a named mapping keyed by the process and the folder's
       identity (volume serial + file index, the same whatever the path spelling). *created = this copy made it. Falls back to
       this copy's own block. Logs nothing, allocates nothing that can throw after the folder handle is closed. */
    LogShared* SharedBlock(const std::wstring& folder, bool* created)
    {
        *created = true;
        std::wstring dir = folder;
        if (dir.size() > 3 && (dir[dir.size() - 1] == L'\\' || dir[dir.size() - 1] == L'/')) dir.erase(dir.size() - 1);
        BY_HANDLE_FILE_INFORMATION fi;
        BOOL haveId = FALSE;
        HANDLE d = ::CreateFileW(dir.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (d != INVALID_HANDLE_VALUE) { haveId = ::GetFileInformationByHandle(d, &fi); ::CloseHandle(d); }
        if (!haveId) return &g_ownShared;
        wchar_t mname[96];
        swprintf_s(mname, 96, L"Local\\SharedWastelands-logceil-%lu-%08lx-%08lx%08lx", (unsigned long)::GetCurrentProcessId(),
                   (unsigned long)fi.dwVolumeSerialNumber, (unsigned long)fi.nFileIndexHigh, (unsigned long)fi.nFileIndexLow);
        HANDLE m = ::CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)sizeof(LogShared), mname);
        const DWORD err = ::GetLastError();
        if (m == NULL) return &g_ownShared;
        void* v = ::MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LogShared));
        if (v == NULL) { ::CloseHandle(m); return &g_ownShared; }
        *created = err != ERROR_ALREADY_EXISTS;   /* a new mapping is zero-filled */
        return (LogShared*)v;   /* m and the view are deliberately never closed: the other copy must find the same block */
    }

    /* Caller holds g_lock. ONCE PER COPY OF THIS DLL, BEFORE THE LOG IS OPENED: the earlier launches' logs move one place
       older (src/common/logrotate.h) - the oldest kept copy is deleted, copy n becomes copy n+1, the previous launch's log
       becomes copy 1 - so this launch writes a new file and the logs of the last cooplogrot::kLogKeep launches stay on disk,
       a crashed launch's among them. Two copies of this DLL in one process share one folder: a per-process named mutex keyed
       by the FOLDER's identity (volume serial + file index, the same whatever the path spelling), created owned, lets only
       the first copy rotate and makes a later copy wait until it has, so no copy moves another copy's fresh log away. A step
       that fails does not stop the next step or the log opening; the first failure goes to g_rotateNote. A copy that cannot
       identify the folder or make the mutex does not rotate (the log is then emptied at opening, as without rotation).
       Logs nothing. May throw std::bad_alloc before the mutex is taken; the caller catches it. */
    void RotateLogsOnce(const std::wstring& folder, const std::wstring& name)
    {
        if (g_rotateDone) return;
        g_rotateDone = 1;

        /* every name is built before the mutex is taken, so nothing below can throw while it is held */
        /* each launch's parts move with it: the log, its .start and its .prev (src/common/logrotate.h PartName) */
        std::wstring names[cooplogrot::kLogKeep][cooplogrot::kPartKinds];
        for (int n = 0; n < (int)cooplogrot::kLogKeep; ++n)
            for (int k = 0; k < (int)cooplogrot::kPartKinds; ++k) names[n][k] = cooplogrot::PartName(name, n, k);
        std::wstring dir = folder;
        if (dir.size() > 3 && (dir[dir.size() - 1] == L'\\' || dir[dir.size() - 1] == L'/')) dir.erase(dir.size() - 1);

        BY_HANDLE_FILE_INFORMATION fi;
        BOOL  haveId = FALSE;
        DWORD err    = 0;
        HANDLE d = ::CreateFileW(dir.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (d != INVALID_HANDLE_VALUE)
        {
            haveId = ::GetFileInformationByHandle(d, &fi);
            if (!haveId) err = ::GetLastError();
            ::CloseHandle(d);
        }
        else err = ::GetLastError();
        if (!haveId)
        {
            _snprintf_s(g_rotateNote, sizeof(g_rotateNote), _TRUNCATE,
                        "log rotation skipped: the log folder could not be identified (Windows error %lu); the previous "
                        "launch's log was not kept", (unsigned long)err);
            return;
        }

        wchar_t mname[96];
        swprintf_s(mname, 96, L"Local\\KenshiMultiplayer-logrot-%lu-%08lx-%08lx%08lx", (unsigned long)::GetCurrentProcessId(),
                   (unsigned long)fi.dwVolumeSerialNumber, (unsigned long)fi.nFileIndexHigh, (unsigned long)fi.nFileIndexLow);
        HANDLE m = ::CreateMutexW(NULL, TRUE, mname);
        err = ::GetLastError();
        if (m == NULL)
        {
            _snprintf_s(g_rotateNote, sizeof(g_rotateNote), _TRUNCATE,
                        "log rotation skipped: its lock could not be made (Windows error %lu); the previous launch's log was "
                        "not kept", (unsigned long)err);
            return;
        }
        if (err == ERROR_ALREADY_EXISTS)
        {
            /* another copy in this process rotates this folder: wait until it has, then open the log it left */
            const DWORD w = ::WaitForSingleObject(m, 5000);
            if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) ::ReleaseMutex(m);
            return;   /* m is deliberately never closed */
        }

        const int last = (int)cooplogrot::kLogKeep - 1;
        for (int k = 0; k < (int)cooplogrot::kPartKinds; ++k)
        {
            if (::DeleteFileW(names[last][k].c_str())) continue;
            err = ::GetLastError();
            if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) NoteRotateStep("delete", names[last][k], NULL, err);
        }
        /* oldest first, so each move lands on a name already freed; a failed move does not stop the newer ones, because
           the newest log (the previous launch, the one a crash leaves) is the one most worth keeping. A part a launch did not
           have (a log under 2 MB has no .start) is simply not there to move. */
        for (int n = last - 1; n >= 0; --n)
            for (int k = 0; k < (int)cooplogrot::kPartKinds; ++k)
            {
                if (::MoveFileExW(names[n][k].c_str(), names[n + 1][k].c_str(), MOVEFILE_REPLACE_EXISTING)) continue;
                err = ::GetLastError();
                if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) NoteRotateStep("rename", names[n][k], &names[n + 1][k], err);
            }
        ::ReleaseMutex(m);
        /* m is deliberately never closed: a later copy's CreateMutexW must find it */
    }

    /* Caller holds g_lock. Opens the file if it is not open yet. A failed attempt is NOT remembered: the next
       line tries again (one CreateFileW per line, only until it succeeds), so one bad moment - the folder
       briefly locked, a scanner holding the file - does not cost the whole run's log. May throw
       std::bad_alloc; the caller catches it. Logs nothing. */
    void EnsureFile()
    {
        if (g_fileState == 1) return;
        std::wstring folder = DllFolder();
        if (folder.empty()) return;
        std::wstring name = folder + swnames::kLogW;
        RotateLogsOnce(folder, name);   /* before the file is opened, so the previous launch's log is kept, not emptied */
        bool sharedNew = false;
        if (g_shared == 0) g_shared = SharedBlock(folder, &sharedNew);
        if (name.size() < sizeof(g_logName) / sizeof(g_logName[0])) wcscpy_s(g_logName, sizeof(g_logName) / sizeof(g_logName[0]), name.c_str());
        bool emptied = false;
        /* mig6 fold (review of 7d96a7d, F7): ONE LOG PER PROCESS. Two copies of this DLL can be loaded from the same folder
           under different path spellings (two modules, two sets of these statics, ONE file); with CREATE_ALWAYS the second
           wiped the first's lines. Now: the handle is APPEND-ONLY (FILE_APPEND_DATA without FILE_WRITE_DATA - every
           WriteFile lands at the current end, so two handles never overwrite each other), and the file is emptied only by
           the FIRST opener of THIS FILE in THIS PROCESS - a per-process named mutex keyed by the file's identity (volume
           serial + file index, the same whatever the path spelling), created owned so a second copy waits until the
           first has emptied it. The mutex handle is kept for the life of the process. If the identity or the mutex cannot
           be had, this copy appends (it never wipes another copy's lines; an old run's lines may then stay above). */
        HANDLE h = ::CreateFileW(name.c_str(), FILE_APPEND_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return;
        BY_HANDLE_FILE_INFORMATION fi;
        if (::GetFileInformationByHandle(h, &fi))
        {
            wchar_t mname[96];
            swprintf_s(mname, 96, L"Local\\KenshiCoop-log-%lu-%08lx-%08lx%08lx", (unsigned long)::GetCurrentProcessId(),
                       (unsigned long)fi.dwVolumeSerialNumber, (unsigned long)fi.nFileIndexHigh, (unsigned long)fi.nFileIndexLow);
            HANDLE m = ::CreateMutexW(NULL, TRUE, mname);
            const DWORD err = ::GetLastError();
            static HANDLE s_keptMutex = NULL;   /* this module's one kept handle; a second open (a reopen after a failure) closes its own */
            if (m != NULL && err != ERROR_ALREADY_EXISTS)
            {
                /* the first opener in this process: this run's log starts empty, as it always did - unless the ceiling has
                   already cut this launch's log (gen moved): this file is then a part another copy began, never emptied */
                if (g_shared->gen == 0)
                {
                    HANDLE t = ::CreateFileW(name.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                             NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (t != INVALID_HANDLE_VALUE) { ::CloseHandle(t); emptied = true; ::InterlockedExchange64(&g_shared->bytes, 0); }
                }
                ::ReleaseMutex(m);
            }
            else if (m != NULL)
            {
                /* a later copy: wait until the first has emptied the file, then append after its lines */
                if (::WaitForSingleObject(m, 5000) == WAIT_OBJECT_0) ::ReleaseMutex(m);
            }
            /* the first handle is kept for the life of the process: the name must outlive every copy's first log line; a later
               open by this same copy (the file reopened after a failed open or cut) closes its extra handle */
            if (m != NULL)
            {
                if (s_keptMutex == NULL) s_keptMutex = m;
                else ::CloseHandle(m);
            }
        }
        if (sharedNew && !emptied)
        {
            /* this copy made the ceiling's block but did not empty the file: count what is already in it */
            LARGE_INTEGER sz;
            if (::GetFileSizeEx(h, &sz)) ::InterlockedExchange64(&g_shared->bytes, sz.QuadPart);
        }
        g_myGen = g_shared->gen;
        g_file = h;
        g_fileState = 1;
    }

    /* Caller holds g_lock. One write that counts toward the ceiling. */
    void WriteCounted(const char* data, DWORD len)
    {
        DWORD written = 0;
        ::WriteFile(g_file, data, len, &written, NULL);
        if (g_shared != 0) ::InterlockedExchangeAdd64(&g_shared->bytes, (LONG64)written);
    }

#ifndef SW_DEV_BUILD
    /* THE CEILING IS THE RELEASE BUILD'S ONLY: the DEV build's log is never cut (a test readout needs the whole log). */
    /* Caller holds g_lock. Another copy cut the log (gen moved): this copy's handle may name the renamed part, so it opens the
       current file again (appending, never emptying). A failed open leaves the file closed; the next line tries again. */
    void ReopenAfterCut()
    {
        if (g_file != INVALID_HANDLE_VALUE) ::CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
        g_fileState = 0;
        g_myGen = g_shared->gen;
        if (g_logName[0] == 0) return;
        HANDLE h = ::CreateFileW(g_logName, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return;
        g_file = h;
        g_fileState = 1;
    }

    /* Caller holds g_lock and the file is open. THE 10 MB CEILING THAT KEEPS THE START (src/common/logrotate.h): before a line
       of lineLen bytes is written, the opening part is renamed to .start when it would pass 2 MB, and later each current part
       that would pass 4 MB replaces .prev; a new current part begins with one line saying so. One copy renames at a time; a
       rename that fails leaves the file as it is, says so once per try, and gives the part 1 MB more before the next try. May
       throw std::bad_alloc (the names); the caller catches it. */
    void CeilingBeforeWrite(DWORD lineLen, const char* timeStr)
    {
        LogShared* s = g_shared;
        if (s == 0 || g_logName[0] == 0) return;
        const int step = cooplogrot::CeilingStep((unsigned long long)s->bytes, lineLen, s->openingCut != 0, (unsigned long long)s->slack);
        if (step == cooplogrot::kCeilKeep) return;
        /* the names before `rolling` is taken: building them may throw, and a throw after it would leave every copy unable to cut */
        const std::wstring cur(g_logName);
        const std::wstring to = cooplogrot::PartName(cur, 0, step == cooplogrot::kCeilCutOpening ? (int)cooplogrot::kPartStart : (int)cooplogrot::kPartPrev);
        if (::InterlockedCompareExchange(&s->rolling, 1, 0) != 0) return;   /* another copy is cutting now */
        if (s->gen != g_myGen) { ::InterlockedExchange(&s->rolling, 0); ReopenAfterCut(); return; }   /* it just did */
        ::CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
        const BOOL moved = ::MoveFileExW(cur.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING);
        const DWORD err = moved ? 0 : ::GetLastError();
        HANDLE h = ::CreateFileW(cur.c_str(), FILE_APPEND_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        g_file = h;
        g_fileState = h != INVALID_HANDLE_VALUE ? 1 : 0;
        char note[512];
        note[0] = 0;
        if (moved)
        {
            const LONG64 had = s->bytes;
            if (step == cooplogrot::kCeilCutOpening) ::InterlockedExchange(&s->openingCut, 1);
            else { ::InterlockedExchangeAdd64(&s->cutBytes, s->prevBytes); ::InterlockedExchange64(&s->prevBytes, had); }
            const LONG part = ::InterlockedIncrement(&s->parts) + 1;
            ::InterlockedExchange64(&s->bytes, 0);
            ::InterlockedExchange64(&s->slack, 0);
            g_myGen = ::InterlockedIncrement(&s->gen);
            if (step == cooplogrot::kCeilCutOpening)
                _snprintf_s(note, sizeof(note), _TRUNCATE,
                            "%s KenshiCoop: log continues: part %ld of this launch; the opening part (the first 2 MB: mods, versions, the "
                            "join) is kept in %s for the whole launch; Shared Wastelands %s, %s\r\n",
                            timeStr, (long)part, "shared_wastelands_log.start.txt", modversion::kName, LogBuildLabel());
            else
                _snprintf_s(note, sizeof(note), _TRUNCATE,
                            "%s KenshiCoop: log continues: part %ld of this launch; %.1f MB between the opening (.start) and the previous "
                            "part (.prev) were cut to keep the log under 10 MB; Shared Wastelands %s, %s\r\n",
                            timeStr, (long)part, (double)s->cutBytes / (1024.0 * 1024.0), modversion::kName, LogBuildLabel());
        }
        else
        {
            ::InterlockedExchangeAdd64(&s->slack, (LONG64)(cooplogrot::kPartBytes / 4u));
            char a[96];
            NarrowFilePart(to, a, sizeof(a));
            _snprintf_s(note, sizeof(note), _TRUNCATE,
                        "%s KenshiCoop: log size ceiling: could not rename the log to %s (Windows error %lu); it grows on and the "
                        "next try is 1 MB later\r\n", timeStr, a, (unsigned long)err);
        }
        ::InterlockedExchange(&s->rolling, 0);
        if (g_fileState == 1 && note[0]) WriteCounted(note, (DWORD)strlen(note));
    }
#endif

    /* Caller holds g_lock. "<sec>.<msec>" since the first log call. */
    void FormatTime(char* out, size_t outSize)
    {
        LARGE_INTEGER now;
        ::QueryPerformanceCounter(&now);
        if (!g_haveStart)
        {
            ::QueryPerformanceFrequency(&g_freq);
            g_start = now;
            g_haveStart = 1;
        }
        unsigned long long ms = 0;
        if (g_freq.QuadPart > 0 && now.QuadPart >= g_start.QuadPart)
        {
            unsigned long long ticks = (unsigned long long)(now.QuadPart - g_start.QuadPart);
            unsigned long long freq  = (unsigned long long)g_freq.QuadPart;
            ms = (ticks / freq) * 1000ull + ((ticks % freq) * 1000ull) / freq;
        }
        _snprintf_s(out, outSize, _TRUNCATE, "%llu.%03llu", ms / 1000ull, ms % 1000ull);
    }

    /* T-910: the [KEEPER] lines of this launch (LogKeeperLinesText), made at the first one, under g_lock. */
    swlinekeep::LineKeep* g_keeperKeep = 0;

    /* Room left at the front of every line for "Error " + "<sec>.<msec>", filled in under the lock. The widest
       possible prefix is 6 + 20 digits + 4 = 30 characters. */
    const size_t kPrefixGap = 40;

    void WriteLine(bool isError, const char* msg, size_t len)
    {
        /* 1. OUTSIDE the lock: everything that reads the caller's text. If that text is corrupt (an engine string
              that is not what it claims) and reading it faults, the fault leaves here with NO lock held - the
              caller's own handler sees it and every other thread keeps logging. Holding the lock across this
              copy was a hang: the fault skipped the unlock and the next log line on any thread blocked forever. */
        std::string line;
        size_t bodyAt = 0;   /* T-910: where the caller's text starts in `line` */
        try
        {
            line.reserve(kPrefixGap + len + len / 8 + 32);
            line.assign(kPrefixGap, ' ');
            line += " KenshiCoop: ";
            bodyAt = line.size();
            /* every '\n' reaches the file as CRLF. */
            for (size_t i = 0; i < len; ++i)
            {
                if (msg[i] == '\n') line += '\r';
                line += msg[i];
            }
            line += "\r\n";
        }
        catch (...)
        {
            return;   /* out of memory while building the line: the line is dropped, the game goes on */
        }
        const bool keeperLine = bodyAt != 0 && line.compare(bodyAt, 8, "[KEEPER]") == 0;   /* T-910: read from our own copy */

        /* 2. INSIDE the lock: only our own state. The timestamp is taken here so lines stay in time order, then
              the prefix goes into the gap and the whole line is ONE WriteFile. */
        EnsureLock();
        ::EnterCriticalSection(&g_lock);
        try
        {
            char timeStr[48];
            FormatTime(timeStr, sizeof(timeStr));
            if (keeperLine)
            {
                /* T-910: kept for the bug report whether or not the file takes it - the line as the file has it, its line end left off */
                if (g_keeperKeep == 0) g_keeperKeep = new swlinekeep::LineKeep(swlinekeep::kKeepFirst, swlinekeep::kKeepLast);
                const std::string kept = std::string(isError ? "Error " : "") + timeStr + line.substr(kPrefixGap, line.size() - kPrefixGap - 2);
                swlinekeep::Keep(g_keeperKeep, kept.data(), kept.size());
            }
            EnsureFile();
            if (g_fileState == 1 && g_versionSaid == 0)
            {
                /* every log starts with the mod's version */
                g_versionSaid = 1;
                char ver[160];
                _snprintf_s(ver, sizeof(ver), _TRUNCATE, "%s KenshiCoop: Shared Wastelands %s, %s\r\n", timeStr, modversion::kName,
                            LogBuildLabel());
                WriteCounted(ver, (DWORD)strlen(ver));
            }
            if (g_fileState == 1 && g_rotateNote[0])
            {
                /* a failed start-up rotation is said once, as this log's first line */
                char note[sizeof(g_rotateNote) + 64];
                _snprintf_s(note, sizeof(note), _TRUNCATE, "%s KenshiCoop: %s\r\n", timeStr, g_rotateNote);
                g_rotateNote[0] = 0;
                WriteCounted(note, (DWORD)strlen(note));
            }
            if (g_fileState == 1)
            {
                char prefix[kPrefixGap + 1];
                _snprintf_s(prefix, sizeof(prefix), _TRUNCATE, "%s%s", isError ? "Error " : "", timeStr);
                size_t plen  = strlen(prefix);
                size_t start = kPrefixGap - plen;
                memcpy(&line[start], prefix, plen);

#ifndef SW_DEV_BUILD
                if (g_shared != 0 && g_shared->gen != g_myGen) ReopenAfterCut();   /* another copy cut the log */
                if (g_fileState == 1) CeilingBeforeWrite((DWORD)(line.size() - start), timeStr);
#endif
                if (g_fileState == 1) WriteCounted(line.data() + start, (DWORD)(line.size() - start));
            }
        }
        catch (...)
        {
            /* out of memory while opening the file: the line is dropped, the next line tries again */
        }
        ::LeaveCriticalSection(&g_lock);
    }

    /* wide -> narrow: each wchar_t truncated to a char. */
    void WriteWide(bool isError, const std::wstring& message)
    {
        std::string narrow;
        try
        {
            narrow.assign(message.begin(), message.end());
        }
        catch (...)
        {
            return;
        }
        WriteLine(isError, narrow.data(), narrow.size());
    }
}

void DebugLog(const std::wstring& message) { WriteWide(false, message); }
void DebugLog(const std::string& message)  { WriteLine(false, message.data(), message.size()); }
void DebugLog(const char* message)         { if (message) WriteLine(false, message, strlen(message)); else WriteLine(false, "", 0); }
void ErrorLog(const std::wstring& message) { WriteWide(true, message); }
void ErrorLog(const std::string& message)  { WriteLine(true, message.data(), message.size()); }
void ErrorLog(const char* message)         { if (message) WriteLine(true, message, strlen(message)); else WriteLine(true, "", 0); }

/* T-910: coop_log.h */
std::string LogKeeperLinesText(unsigned long long* seen)
{
    std::string out;
    unsigned long long n = 0;
    EnsureLock();
    ::EnterCriticalSection(&g_lock);
    try
    {
        if (g_keeperKeep != 0) { out = swlinekeep::Text(*g_keeperKeep); n = g_keeperKeep->seen; }
    }
    catch (...)
    {
        out.clear();   /* out of memory: the report goes without the file */
    }
    ::LeaveCriticalSection(&g_lock);
    if (seen != 0) *seen = n;
    return out;
}

const char* LogBuildLabel()
{
#ifdef SW_DEV_BUILD
    return "DEV build";
#else
    return "release build";
#endif
}

namespace
{
    /* Every gate QuietLogPass has seen, with its family's words (callers pass literals), so QuietLogFlush can reach the gates
       each file keeps to itself. A gate past the table is not flushed; its own count lines still come. */
    enum { kQuietGates = 64 };
    swlogbudget::Gate* volatile g_quietGates[kQuietGates];
    const char* volatile g_quietFamily[kQuietGates];
    volatile LONG g_quietGateCount = 0;
    volatile LONG g_quietFlushAt = 0, g_quietFlushSet = 0;

    void QuietRegister(swlogbudget::Gate* gate, const char* family)
    {
        const LONG n = g_quietGateCount;
        for (LONG i = 0; i < n && i < (LONG)kQuietGates; ++i) if (g_quietGates[i] == gate) return;
        if (n >= (LONG)kQuietGates) return;
        const LONG i = ::InterlockedIncrement(&g_quietGateCount) - 1;
        if (i >= (LONG)kQuietGates) return;
        g_quietFamily[i] = family;
        ::MemoryBarrier();
        g_quietGates[i] = gate;   /* a flush that reads the slot before this line skips it */
    }

    void QuietCountLine(const char* family, const char* keyText, unsigned long long key, unsigned int held, unsigned int spanMs)
    {
        char keyHex[32];
        keyHex[0] = 0;
        if ((keyText == 0 || keyText[0] == 0) && key != 0) _snprintf_s(keyHex, sizeof(keyHex), _TRUNCATE, "key %llx", key);
        const char* k = keyText && keyText[0] ? keyText : keyHex;
        char line[400];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "%s%s%s%s: %u more lines like this in the last %u s were not written (the release "
                    "build writes the first %d in full, then this count at most once a minute)", family ? family : "?",
                    k[0] ? " [" : "", k, k[0] ? "]" : "", held, spanMs / 1000u, (int)swlogbudget::kFullLines);
        DebugLog(line);
    }
}

bool QuietLogPass(swlogbudget::Gate* gate, const char* family, unsigned long long key, const char* keyText)
{
    if (gate == 0) return true;
    QuietRegister(gate, family);
    unsigned int held = 0, spanMs = 0;
    const bool full = swlogbudget::Pass(gate, key, ::GetTickCount(), &held, &spanMs);
    if (held != 0) QuietCountLine(family, keyText, key, held, spanMs);
    return full;
}

namespace
{
    /* T-879: every ChangedLogSlot ChangedLogPass has seen, so QuietLogFlush(false) can write the lines they hold. Guarded by g_lock.
       A slot past the table is not flushed; its changed lines still come. */
    enum { kChangedSlots = 16 };
    ChangedLogSlot* g_changedSlots[kChangedSlots];
    int g_changedSlotCount = 0;

    unsigned long long ChangedHash(const std::string& s)
    {
        unsigned long long h = 1469598103934665603ULL;   /* FNV-1a, 64 bit */
        for (size_t i = 0; i < s.size(); ++i) { h ^= (unsigned char)s[i]; h *= 1099511628211ULL; }
        return h;
    }

    /* Under g_lock: the slot joins the table QuietLogFlush(false) reads, once. */
    void ChangedRegister(ChangedLogSlot* slot)
    {
        if (slot->registered != 0) return;
        slot->registered = 1;
        if (g_changedSlotCount < (int)kChangedSlots) g_changedSlots[g_changedSlotCount++] = slot;
    }

    /* Writes the newest line each slot held back since its last line, and forgets it (the slot's key is unchanged). */
    void ChangedLogFlushHeld()
    {
        for (int i = 0; ; ++i)
        {
            std::string line;
            bool more = false;
            EnsureLock();
            ::EnterCriticalSection(&g_lock);
            try
            {
                more = i < g_changedSlotCount && i < (int)kChangedSlots;
                if (more && g_changedSlots[i] != 0 && g_changedSlots[i]->held != 0) line.swap(*g_changedSlots[i]->held);
            }
            catch (...)
            {
                line.clear();
            }
            ::LeaveCriticalSection(&g_lock);
            if (!more) break;
            if (!line.empty()) DebugLog(line);
        }
    }
}

bool ChangedLogPass(ChangedLogSlot* slot, const std::string& key, const std::string& text)
{
    if (slot == 0) return true;
    const unsigned long long h = ChangedHash(key);
    bool write = true;
    EnsureLock();
    ::EnterCriticalSection(&g_lock);
    try
    {
        ChangedRegister(slot);
        if (slot->wrote != 0 && slot->keyHash == h)
        {
            write = false;
            if (slot->held == 0) slot->held = new std::string();
            slot->held->assign(text);
        }
        else
        {
            slot->wrote = 1;
            slot->keyHash = h;
            if (slot->held != 0) slot->held->clear();
        }
    }
    catch (...)
    {
        write = true;   /* out of memory: write the line rather than lose it */
    }
    ::LeaveCriticalSection(&g_lock);
    return write;
}

bool IntervalLogPass(ChangedLogSlot* slot, unsigned int gapMs, const std::string& text)
{
    if (slot == 0) return true;
    const unsigned long now = ::GetTickCount();
    bool write = true;
    EnsureLock();
    ::EnterCriticalSection(&g_lock);
    try
    {
        ChangedRegister(slot);
        if (slot->wrote != 0 && (unsigned long)(now - slot->lastMs) < (unsigned long)gapMs)
        {
            write = false;
            if (slot->held == 0) slot->held = new std::string();
            slot->held->assign(text);
        }
        else
        {
            slot->wrote = 1;
            slot->lastMs = now;
            if (slot->held != 0) slot->held->clear();
        }
    }
    catch (...)
    {
        write = true;   /* out of memory: write the line rather than lose it */
    }
    ::LeaveCriticalSection(&g_lock);
    return write;
}

void QuietLogFlush(bool periodic)
{
    const unsigned int now = ::GetTickCount();
    if (!periodic) ChangedLogFlushHeld();   /* T-879: the newest timed lines held back (SW_CHANGEDN / SW_INTERVALN), before the log ends or is read */
    if (periodic)
    {
        if (::InterlockedCompareExchange(&g_quietFlushSet, 1, 0) == 0) { ::InterlockedExchange(&g_quietFlushAt, (LONG)now); return; }
        if (now - (unsigned int)g_quietFlushAt < swlogbudget::kCountGapMs) return;
    }
    ::InterlockedExchange(&g_quietFlushAt, (LONG)now);
    LONG n = g_quietGateCount;
    if (n > (LONG)kQuietGates) n = (LONG)kQuietGates;
    for (LONG i = 0; i < n; ++i)
    {
        swlogbudget::Gate* g = g_quietGates[i];
        if (g == 0) continue;
        int cursor = 0;
        unsigned long long key = 0;
        unsigned int held = 0, spanMs = 0;
        while (swlogbudget::TakeHeld(g, &cursor, now, &key, &held, &spanMs)) QuietCountLine(g_quietFamily[i], 0, key, held, spanMs);
    }
}

std::wstring LogPartPath(int copy, int part)
{
    try
    {
        const std::wstring folder = DllFolder();
        if (folder.empty()) return std::wstring();
        return cooplogrot::PartName(folder + swnames::kLogW, copy, part);
    }
    catch (...) { return std::wstring(); }
}

std::wstring LogFilePath(int copy)
{
    try
    {
        const std::wstring folder = DllFolder();
        if (folder.empty()) return std::wstring();
        return cooplogrot::RotatedName(folder + swnames::kLogW, copy);
    }
    catch (...) { return std::wstring(); }
}
