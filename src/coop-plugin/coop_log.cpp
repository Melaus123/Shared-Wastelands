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
        std::wstring names[cooplogrot::kLogKeep];
        for (int n = 0; n < (int)cooplogrot::kLogKeep; ++n) names[n] = cooplogrot::RotatedName(name, n);
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
        if (!::DeleteFileW(names[last].c_str()))
        {
            err = ::GetLastError();
            if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) NoteRotateStep("delete", names[last], NULL, err);
        }
        /* oldest first, so each move lands on a name already freed; a failed move does not stop the newer ones, because
           the newest log (the previous launch, the one a crash leaves) is the one most worth keeping */
        for (int n = last - 1; n >= 0; --n)
        {
            if (::MoveFileExW(names[n].c_str(), names[n + 1].c_str(), MOVEFILE_REPLACE_EXISTING)) continue;
            err = ::GetLastError();
            if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) NoteRotateStep("rename", names[n], &names[n + 1], err);
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
            if (m != NULL && err != ERROR_ALREADY_EXISTS)
            {
                /* the first opener in this process: this run's log starts empty, as it always did */
                HANDLE t = ::CreateFileW(name.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                         NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                if (t != INVALID_HANDLE_VALUE) ::CloseHandle(t);
                ::ReleaseMutex(m);
            }
            else if (m != NULL)
            {
                /* a later copy: wait until the first has emptied the file, then append after its lines */
                if (::WaitForSingleObject(m, 5000) == WAIT_OBJECT_0) ::ReleaseMutex(m);
            }
            /* m is deliberately never closed: the name must outlive every copy's first log line */
        }
        g_file = h;
        g_fileState = 1;
    }

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
        try
        {
            line.reserve(kPrefixGap + len + len / 8 + 32);
            line.assign(kPrefixGap, ' ');
            line += " KenshiCoop: ";
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

        /* 2. INSIDE the lock: only our own state. The timestamp is taken here so lines stay in time order, then
              the prefix goes into the gap and the whole line is ONE WriteFile. */
        EnsureLock();
        ::EnterCriticalSection(&g_lock);
        try
        {
            char timeStr[48];
            FormatTime(timeStr, sizeof(timeStr));
            EnsureFile();
            if (g_fileState == 1 && g_rotateNote[0])
            {
                /* a failed start-up rotation is said once, as this log's first line */
                char note[sizeof(g_rotateNote) + 64];
                _snprintf_s(note, sizeof(note), _TRUNCATE, "%s KenshiCoop: %s\r\n", timeStr, g_rotateNote);
                g_rotateNote[0] = 0;
                DWORD noteWritten = 0;
                ::WriteFile(g_file, note, (DWORD)strlen(note), &noteWritten, NULL);
            }
            if (g_fileState == 1)
            {
                char prefix[kPrefixGap + 1];
                _snprintf_s(prefix, sizeof(prefix), _TRUNCATE, "%s%s", isError ? "Error " : "", timeStr);
                size_t plen  = strlen(prefix);
                size_t start = kPrefixGap - plen;
                memcpy(&line[start], prefix, plen);

                DWORD written = 0;
                ::WriteFile(g_file, line.data() + start, (DWORD)(line.size() - start), &written, NULL);
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
