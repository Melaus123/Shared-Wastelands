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
#include "../common/crashkeep.h"   /* a launch that did not quit normally, kept in the crashes folder at the next start */
#include <vector>

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
    /* guarded by g_lock: the start-up's [CRASHKEEP] line (src/common/crashkeep.h), written into the new log after the version line */
    char g_crashNote[640];
    /* the start-up's crash keep, handed from the rotation (log lock held) to the first log line that has let the lock go:
       0 nothing to do, 1 waiting, 2 claimed by one thread. Plain arrays: a log call may come before any constructor has run. */
    volatile LONG g_ckState = 0;
    wchar_t g_ckFolder[2048];
    wchar_t g_ckName[2048];
    int g_ckPrevMoved = 0;

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

    /* ---- A LAUNCH THAT DID NOT QUIT NORMALLY, KEPT (src/common/crashkeep.h). Runs with no lock held (RunCrashKeepOnce), so no
       thread's log line waits for the reading and copying; nothing here logs - its line goes to g_crashNote (SetCrashNote). ---- */

    long long CkFileTimeSec(const FILETIME& ft) { return (long long)((((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000000ull); }

    std::wstring CkWiden(const std::string& s)   /* the names here are ASCII */
    {
        std::wstring w;
        for (size_t i = 0; i < s.size(); ++i) w += (wchar_t)(unsigned char)s[i];
        return w;
    }

    /* Up to `want` bytes of an open file from byte `at` (fewer at its end). False on a read error. */
    bool CkReadAt(HANDLE h, unsigned long long at, unsigned long long want, std::string* out)
    {
        out->clear();
        LARGE_INTEGER p;
        p.QuadPart = (LONGLONG)at;
        if (!::SetFilePointerEx(h, p, NULL, FILE_BEGIN)) return false;
        out->resize((size_t)want);
        size_t got = 0;
        while (got < out->size())
        {
            const size_t left = out->size() - got;
            DWORD n = 0;
            if (!::ReadFile(h, &(*out)[got], (DWORD)(left > (1u << 20) ? (1u << 20) : left), &n, NULL)) return false;
            if (n == 0) break;
            got += n;
        }
        out->resize(got);
        return true;
    }

    /* `text` in the first (head) or the newest (!head) `bytes` of the file at `path`: 1 yes, 0 no, -1 the file is not there,
       -2 it could not be read (*err). */
    int CkFileHas(const std::wstring& path, bool head, unsigned long long bytes, const char* text, DWORD* err)
    {
        HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE)
        {
            const DWORD e = ::GetLastError();
            if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return -1;
            *err = e;
            return -2;
        }
        std::string buf;
        LARGE_INTEGER sz;
        bool ok = ::GetFileSizeEx(h, &sz) != 0;
        if (ok)
        {
            const unsigned long long n = (unsigned long long)sz.QuadPart;
            ok = CkReadAt(h, (head || n <= bytes) ? 0 : n - bytes, bytes, &buf);
        }
        if (!ok) *err = ::GetLastError();
        ::CloseHandle(h);
        if (!ok) return -2;
        return swcrashkeep::HasText(buf.data(), buf.size(), text) ? 1 : 0;
    }

    /* Reads launch 1's log for the judgement (swcrashkeep::Judge): returns swcrashkeep::kLog*, with whether the exit hook's install
       line and a quit route line are there, and the newest part's size. `part` = launch 1's parts (cooplogrot::kPart*). */
    int CkReadPrevious(const std::wstring part[], bool* hook, bool* route, unsigned long long* size, DWORD* err)
    {
        *hook = false; *route = false; *size = 0; *err = 0;
        /* no write sharing: a game still writing this log holds it open for writing, and the open then fails with a sharing
           violation - a game that is still running, not a crash */
        HANDLE h = ::CreateFileW(part[cooplogrot::kPartCurrent].c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE)
        {
            *err = ::GetLastError();
            if (*err == ERROR_FILE_NOT_FOUND || *err == ERROR_PATH_NOT_FOUND) return swcrashkeep::kLogMissing;
            return *err == ERROR_SHARING_VIOLATION ? swcrashkeep::kLogHeld : swcrashkeep::kLogUnreadable;
        }
        bool tailRead = false, tailRoute = false;
        int headHas = -2;
        LARGE_INTEGER sz;
        std::string buf;
        if (::GetFileSizeEx(h, &sz))
        {
            const unsigned long long n = (unsigned long long)sz.QuadPart;
            const unsigned long long tail = swcrashkeep::kRouteTailBytes;
            *size = n;
            tailRead = CkReadAt(h, n > tail ? n - tail : 0, tail, &buf);
            if (tailRead) tailRoute = swcrashkeep::HasText(buf.data(), buf.size(), swcrashkeep::kRouteText);
            if (tailRead && CkReadAt(h, 0, swcrashkeep::kHeadBytes, &buf)) headHas = swcrashkeep::HasText(buf.data(), buf.size(), swcrashkeep::kHookOkText) ? 1 : 0;
        }
        if (!tailRead || headHas == -2) *err = ::GetLastError();
        ::CloseHandle(h);
        DWORD e2 = 0;
        const int startHas = CkFileHas(part[cooplogrot::kPartStart], true, swcrashkeep::kHeadBytes, swcrashkeep::kHookOkText, &e2);
        const int prevHas = tailRoute ? 0 : CkFileHas(part[cooplogrot::kPartPrev], false, swcrashkeep::kRouteTailBytes, swcrashkeep::kRouteText, &e2);
        if (*err == 0) *err = e2;
        return swcrashkeep::ReadState(tailRead, tailRoute, startHas, headHas, prevHas, hook, route);
    }

    /* Kenshi's crash file that ended the previous launch, looked for in each of `folders` (swcrashkeep::CrashFolders): one
       coopbug::CrashFromLastLaunch places in that launch (written within kCrashNearSec of copy 1's last write, and before this
       launch began), at most 4 MB, a .zip before a .dmp, the newest first - as the bug report picks it. Its full path, or empty. */
    std::wstring CkFindCrashFile(const std::vector<std::wstring>& folders, long long prevLogEnd, std::wstring* fileName)
    {
        fileName->clear();
        if (prevLogEnd <= 0) return std::wstring();
        FILETIME ct, et, kt, ut;
        if (!::GetProcessTimes(::GetCurrentProcess(), &ct, &et, &kt, &ut)) return std::wstring();
        const long long launch = CkFileTimeSec(ct);
        std::wstring best;
        int bestKind = 0;
        long long bestAt = 0;
        for (size_t d = 0; d < folders.size(); ++d)
        {
            WIN32_FIND_DATAW fd;
            HANDLE f = ::FindFirstFileW((folders[d] + L"crashDump*").c_str(), &fd);
            if (f == INVALID_HANDLE_VALUE) continue;
            do
            {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                char narrow[MAX_PATH];
                NarrowFilePart(std::wstring(fd.cFileName), narrow, sizeof(narrow));
                const int kind = coopbug::CrashFileKind(narrow);
                const unsigned long long size = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                const long long t = CkFileTimeSec(fd.ftLastWriteTime);
                if (kind == 0 || size > (4ull << 20) || !coopbug::CrashFromLastLaunch(t, prevLogEnd, launch)) continue;
                if (best.empty() || kind > bestKind || (kind == bestKind && t > bestAt))
                { best = folders[d] + fd.cFileName; *fileName = fd.cFileName; bestKind = kind; bestAt = t; }
            } while (::FindNextFileW(f, &fd));
            ::FindClose(f);
        }
        return best;
    }

    /* Removes one entry's folder whole: every file in it, then the folder. 0, or the first Windows error. */
    DWORD CkRemoveFolder(const std::wstring& dir)
    {
        DWORD first = 0;
        WIN32_FIND_DATAW fd;
        HANDLE f = ::FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (f != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                const std::wstring p = dir + L"\\" + fd.cFileName;
                ::SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);   /* a read-only copy goes too */
                if (!::DeleteFileW(p.c_str()) && first == 0) first = ::GetLastError();
            } while (::FindNextFileW(f, &fd));
            ::FindClose(f);
        }
        if (!::RemoveDirectoryW(dir.c_str()) && first == 0) first = ::GetLastError();
        return first;
    }

    /* The previous launch did not quit normally: every part of its log (`part`, launch 1's) and Kenshi's crash file that ended it
       go into a new entry of <folder>crashes\, built under its ".part" name and renamed when every file is in; then only the
       newest swcrashkeep::kKeepEntries entries stay (older ones, and a ".part" a cut-off copy left, are removed whole). Returns
       the [CRASHKEEP] line. */
    std::string CkKeep(const std::wstring& folder, const std::wstring part[], unsigned long long logBytes)
    {
        using namespace swcrashkeep;
        /* Kenshi's crash file is paired now, while it is there: a later crash writes the same file name again */
        std::wstring program;
        {
            wchar_t p[2048];
            const DWORD n = ::GetModuleFileNameW(NULL, p, 2048);
            if (n != 0 && n < 2048)
            {
                const std::wstring s(p, n);
                const std::wstring::size_type k = s.find_last_of(L"\\/");
                if (k != std::wstring::npos) program = s.substr(0, k + 1);
            }
        }
        long long prevEnd = 0;
        WIN32_FILE_ATTRIBUTE_DATA ad;
        if (::GetFileAttributesExW(part[cooplogrot::kPartCurrent].c_str(), GetFileExInfoStandard, &ad)) prevEnd = CkFileTimeSec(ad.ftLastWriteTime);
        std::wstring crashFileName;
        const std::wstring crashFile = CkFindCrashFile(CrashFolders(program, folder), prevEnd, &crashFileName);

        const std::wstring root = folder + kFolderW;
        if (!::CreateDirectoryW(root.c_str(), NULL))
        {
            const DWORD e = ::GetLastError();
            if (e != ERROR_ALREADY_EXISTS) return NoteLine(kVerdictCrashed, true, e, "", 0, "", "", 0, "");
        }
        SYSTEMTIME t;
        ::GetSystemTime(&t);
        std::string entry;
        std::wstring building;
        DWORD err = ERROR_ALREADY_EXISTS;
        for (int attempt = 0; attempt < 10; ++attempt)
        {
            const std::string e = EntryName(t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, (unsigned long)::GetCurrentProcessId(), attempt);
            const std::wstring w = root + L"\\" + CkWiden(e);
            if (::GetFileAttributesW(w.c_str()) != INVALID_FILE_ATTRIBUTES) continue;   /* that name is an entry already */
            const std::wstring b = w + CkWiden(kPartialSuffix);
            if (::CreateDirectoryW(b.c_str(), NULL)) { entry = e; building = b; break; }
            err = ::GetLastError();
            if (err != ERROR_ALREADY_EXISTS) break;
        }
        if (entry.empty()) return NoteLine(kVerdictCrashed, true, err, "", 0, "", "", 0, "");
        /* the log's parts under the names a launch's own parts have, then the crash file under its own */
        err = 0;
        std::string parts;
        const std::wstring base(swnames::kLogW);
        for (int k = 0; k < (int)cooplogrot::kPartKinds && err == 0; ++k)
        {
            const std::wstring to = building + L"\\" + cooplogrot::PartName(base, 0, k);
            if (::CopyFileW(part[k].c_str(), to.c_str(), TRUE))
            {
                if (!parts.empty()) parts += "+";
                parts += k == (int)cooplogrot::kPartStart ? "start" : k == (int)cooplogrot::kPartPrev ? "prev" : "newest";
                continue;
            }
            const DWORD e = ::GetLastError();
            /* a log under 2 MB has no .start part and one that was never cut twice no .prev part */
            if (k != (int)cooplogrot::kPartCurrent && (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)) continue;
            err = e;
        }
        std::string crashName;
        if (err == 0 && !crashFile.empty())
        {
            if (::CopyFileW(crashFile.c_str(), (building + L"\\" + crashFileName).c_str(), TRUE))
            {
                char nb[MAX_PATH];
                NarrowFilePart(crashFileName, nb, sizeof(nb));
                crashName = nb;
            }
            else err = ::GetLastError();
        }
        if (err == 0 && !::MoveFileExW(building.c_str(), (root + L"\\" + CkWiden(entry)).c_str(), 0)) err = ::GetLastError();
        if (err != 0)
        {
            CkRemoveFolder(building);   /* never half an entry */
            return NoteLine(kVerdictCrashed, true, err, "", 0, "", "", 0, "");
        }

        std::vector<std::string> names, partial;
        WIN32_FIND_DATAW fd;
        HANDLE f = ::FindFirstFileW((root + L"\\*").c_str(), &fd);
        if (f != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                char nb[MAX_PATH];
                NarrowFilePart(std::wstring(fd.cFileName), nb, sizeof(nb));
                if (IsEntryName(nb)) names.push_back(nb);
                else if (IsPartialName(nb)) partial.push_back(nb);
            } while (::FindNextFileW(f, &fd));
            ::FindClose(f);
        }
        const std::vector<std::string> old = EntriesToRemove(names, kKeepEntries);
        int kept = (int)names.size();
        std::string removed;
        for (size_t i = 0; i < old.size(); ++i)
        {
            const DWORD e = CkRemoveFolder(root + L"\\" + CkWiden(old[i]));
            if (!removed.empty()) removed += ", ";
            removed += old[i];
            if (e == 0) { --kept; continue; }
            char b[64];
            _snprintf_s(b, sizeof(b), _TRUNCATE, " (not removed: Windows error %lu)", (unsigned long)e);
            removed += b;
        }
        for (size_t i = 0; i < partial.size(); ++i) CkRemoveFolder(root + L"\\" + CkWiden(partial[i]));
        return NoteLine(kVerdictCrashed, false, 0, entry, logBytes, parts, crashName, kept, removed);
    }

    /* The [CRASHKEEP] line, for the next log line to write (under the log lock: the line is read under it). */
    void SetCrashNote(const char* text)
    {
        EnsureLock();
        ::EnterCriticalSection(&g_lock);
        _snprintf_s(g_crashNote, sizeof(g_crashNote), _TRUNCATE, "%s", text);
        ::LeaveCriticalSection(&g_lock);
    }

    /* After the start-up rotation: the previous launch's newest part became copy 1 (prevMoved 1), was not there (0) or did not
       move (-1, so copy 1 is an older launch's, judged at an earlier start). Judges copy 1 (swcrashkeep::Judge) and keeps it when
       that launch did not quit normally (CkKeep). The outcome goes to g_crashNote. Never throws. No lock held. */
    void KeepCrashedLaunch(const std::wstring& folder, const std::wstring& name, int prevMoved)
    {
        try
        {
            std::wstring part[cooplogrot::kPartKinds];
            for (int k = 0; k < (int)cooplogrot::kPartKinds; ++k) part[k] = cooplogrot::PartName(name, 1, k);
            int verdict = prevMoved < 0 ? (int)swcrashkeep::kVerdictNotMoved : (int)swcrashkeep::kVerdictNoLog;
            bool hook = false, route = false;
            unsigned long long logBytes = 0;
            DWORD err = 0;
            if (prevMoved > 0)
            {   /* the reading first, then the judgement: a call's arguments have no set order, so hook and route passed beside the
                   reading in one call were copied before it filled them in (T1815: every launch judged "not all marked") */
                const int logState = CkReadPrevious(part, &hook, &route, &logBytes, &err);
                verdict = swcrashkeep::Judge(logState, hook, route);
            }
            const std::string line = verdict == swcrashkeep::kVerdictCrashed ? CkKeep(folder, part, logBytes)
                                                                             : swcrashkeep::NoteLine(verdict, false, err, "", 0, "", "", 0, "");
            SetCrashNote(line.c_str());
        }
        catch (...)
        {
            SetCrashNote("[CRASHKEEP] stopped: out of memory - nothing kept");
        }
    }

    /* The start-up's crash keep, once per copy of this DLL that rotated, run by the first log line after the rotation once that
       line has let the log lock go (WriteLine). Any thread; the first to claim it runs it. Its line is written by the next log
       line. */
    void RunCrashKeepOnce()
    {
        if (g_ckState != 1 || ::InterlockedCompareExchange(&g_ckState, 2, 1) != 1) return;
        try { KeepCrashedLaunch(std::wstring(g_ckFolder), std::wstring(g_ckName), g_ckPrevMoved); }
        catch (...) { SetCrashNote("[CRASHKEEP] stopped: out of memory - nothing kept"); }
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
        int prevMoved = 0;   /* the previous launch's newest part: 1 moved to copy 1, 0 not there, -1 its move failed */
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
                const bool prevPart = n == 0 && k == (int)cooplogrot::kPartCurrent;
                if (::MoveFileExW(names[n][k].c_str(), names[n + 1][k].c_str(), MOVEFILE_REPLACE_EXISTING)) { if (prevPart) prevMoved = 1; continue; }
                err = ::GetLastError();
                if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND)
                {
                    NoteRotateStep("rename", names[n][k], &names[n + 1][k], err);
                    if (prevPart) prevMoved = -1;
                }
            }
        ::ReleaseMutex(m);
        /* m is deliberately never closed: a later copy's CreateMutexW must find it */
        /* copy 1 is the previous launch now: it is judged, and kept in the crashes folder when it did not quit normally - but not
           here, where the caller holds the log lock (g_lock) and every thread's log line would wait for the reading and copying
           (up to about 18 MB). It is handed to the first log line that has let the lock go (RunCrashKeepOnce). Only this copy of
           the DLL rotated, so nothing else here moves copy 1 meanwhile. */
        if (folder.size() < 2048 && name.size() < 2048)
        {
            wcscpy_s(g_ckFolder, 2048, folder.c_str());
            wcscpy_s(g_ckName, 2048, name.c_str());
            g_ckPrevMoved = prevMoved;
            ::InterlockedExchange(&g_ckState, 1);
        }
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
            if (g_fileState == 1 && g_crashNote[0])
            {
                /* the start-up's judgement of the previous launch, said once (src/common/crashkeep.h) */
                char note[sizeof(g_crashNote) + 64];
                _snprintf_s(note, sizeof(note), _TRUNCATE, "%s KenshiCoop: %s\r\n", timeStr, g_crashNote);
                g_crashNote[0] = 0;
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
        RunCrashKeepOnce();   /* the start-up's crash keep, once, with no lock held */
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
