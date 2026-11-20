/* winrecords_read.h - THE READER for winrecords.h: Windows' event logs and the CrashDumps folder, at the start after a launch that
 * did not quit normally (coop_log.cpp KeepCrashedLaunch), and in the offline suite's local-check switch (coop_test.exe
 * --winrecords). Not pure: it calls Windows. Everything it hands on goes through winrecords.h's picking and stripping.
 *
 * The event-log API (wevtapi.dll: EvtQuery / EvtNext / EvtRender / EvtClose) is loaded by full path from the system folder when
 * it runs, never linked: a Windows without it, or one that refuses, writes "could not read (<code>)" and the game starts as
 * before. Each log is read newest first, ONE entry per EvtNext call, with that call's wait cut to what is left of the budget
 * (swwinrec::kBudgetMs for both logs together, the Application log stopping at kAppShareMs): a log with few matching entries is
 * otherwise searched to its oldest record. A search cut by the limit keeps what it found and says so (kErrTimeout).
 * C++03 (VS2010 v100). Wide calls only (the suite is not built UNICODE). Throws only std::bad_alloc.
 */
#ifndef COOP_COMMON_WINRECORDS_READ_H
#define COOP_COMMON_WINRECORDS_READ_H

#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include "winrecords.h"

namespace swwinrec {

typedef HANDLE (WINAPI* EvtQueryFn)(HANDLE, LPCWSTR, LPCWSTR, DWORD);
typedef BOOL (WINAPI* EvtNextFn)(HANDLE, DWORD, HANDLE*, DWORD, DWORD, PDWORD);
typedef BOOL (WINAPI* EvtRenderFn)(HANDLE, HANDLE, DWORD, DWORD, PVOID, PDWORD, PDWORD);
typedef BOOL (WINAPI* EvtCloseFn)(HANDLE);

const DWORD kEvtQueryChannelPath = 0x1;
const DWORD kEvtQueryReverseDirection = 0x200;
const DWORD kEvtRenderEventXml = 1;
enum { kMostRendered = 256 };   /* entries looked at per log, at most (the time budget is the real limit) */

struct EvtApi
{
    HMODULE dll;
    EvtQueryFn query;
    EvtNextFn next;
    EvtRenderFn render;
    EvtCloseFn close;
};

/* wevtapi.dll from the system folder; 0 or the Windows error */
inline unsigned long LoadEvt(EvtApi* a)
{
    a->dll = 0; a->query = 0; a->next = 0; a->render = 0; a->close = 0;
    wchar_t sys[MAX_PATH];
    const UINT n = ::GetSystemDirectoryW(sys, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 16) return ::GetLastError() ? ::GetLastError() : ERROR_PATH_NOT_FOUND;
    a->dll = ::LoadLibraryW((std::wstring(sys, n) + L"\\wevtapi.dll").c_str());
    if (!a->dll) return ::GetLastError();
    a->query = (EvtQueryFn)::GetProcAddress(a->dll, "EvtQuery");
    a->next = (EvtNextFn)::GetProcAddress(a->dll, "EvtNext");
    a->render = (EvtRenderFn)::GetProcAddress(a->dll, "EvtRender");
    a->close = (EvtCloseFn)::GetProcAddress(a->dll, "EvtClose");
    if (!a->query || !a->next || !a->render || !a->close)
    {
        ::FreeLibrary(a->dll);
        a->dll = 0;
        return ERROR_PROC_NOT_FOUND;
    }
    return 0;
}

/* One log, newest first, until t0 + limitMs: matching entries (ParseApp for the Application log, ParseSys for the System log)
   added to *out, the newest kPerKind of each kind. 0, kErrTimeout (cut, what was found stays) or the Windows error. */
inline unsigned long ReadLog(const EvtApi& a, const wchar_t* channel, const std::string& query, bool app, const std::string& exe,
                             DWORD t0, DWORD limitMs, std::vector<Record>* out)
{
    const std::wstring q(query.begin(), query.end());   /* the query is ASCII */
    HANDLE rs = a.query(NULL, channel, q.c_str(), kEvtQueryChannelPath | kEvtQueryReverseDirection);
    if (!rs) return ::GetLastError();
    int have[kKinds] = { 0, 0, 0, 0, 0, 0 };
    unsigned long err = 0;
    std::vector<wchar_t> buf(8192);
    for (int rendered = 0; rendered < kMostRendered; ++rendered)
    {
        const DWORD used = ::GetTickCount() - t0;
        if (used >= limitMs) { err = kErrTimeout; break; }
        HANDLE ev = NULL;
        DWORD got = 0;
        if (!a.next(rs, 1, &ev, limitMs - used, 0, &got) || got == 0)
        {
            const DWORD e = ::GetLastError();
            if (e != ERROR_NO_MORE_ITEMS) err = e != 0 ? e : kErrTimeout;
            break;
        }
        DWORD bytes = 0, props = 0;
        BOOL ok = a.render(NULL, ev, kEvtRenderEventXml, (DWORD)(buf.size() * sizeof(wchar_t)), &buf[0], &bytes, &props);
        if (!ok && ::GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytes < (1u << 20))
        {
            buf.resize(bytes / sizeof(wchar_t) + 1);
            ok = a.render(NULL, ev, kEvtRenderEventXml, (DWORD)(buf.size() * sizeof(wchar_t)), &buf[0], &bytes, &props);
        }
        a.close(ev);
        if (!ok) continue;
        std::string xml;
        const size_t chars = bytes / sizeof(wchar_t);
        xml.reserve(chars);
        for (size_t i = 0; i < chars && buf[i] != 0; ++i) xml += buf[i] < 128 ? (char)buf[i] : '?';
        Record r;
        if (!(app ? ParseApp(xml, exe, &r) : ParseSys(xml, &r))) continue;
        if (r.kind < 0 || r.kind >= kKinds || have[r.kind] >= kPerKind) continue;
        ++have[r.kind];
        out->push_back(r);
        const int a0 = app ? kAppError : kUnexpected, a1 = app ? kHang : kShutdownStarted;
        bool full = true;
        for (int k = a0; k <= a1; ++k) if (have[k] < kPerKind) full = false;
        if (full) break;
    }
    a.close(rs);
    return err;
}

/* The game's crash dumps in `dir` (with its trailing backslash) written in the window, newest first (exe "*": any .dmp). */
inline std::vector<std::string> DumpNames(const std::wstring& dir, const std::string& exe, long long windowStart)
{
    std::vector<std::pair<long long, std::string> > found;
    WIN32_FIND_DATAW fd;
    HANDLE f = ::FindFirstFileW((dir + L"*.dmp").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::string name;
            for (const wchar_t* p = fd.cFileName; *p; ++p) name += *p < 128 ? (char)*p : '?';
            if (exe == "*" ? !SameName(name.size() > 4 ? name.substr(name.size() - 4) : name, ".dmp") : !IsGameDump(name, exe)) continue;
            const long long t = (long long)((((unsigned long long)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime) / 10000000ull);
            if (t < windowStart) continue;
            found.push_back(std::make_pair(-t, name));
        } while (::FindNextFileW(f, &fd));
        ::FindClose(f);
    }
    std::sort(found.begin(), found.end());
    std::vector<std::string> out;
    for (size_t i = 0; i < found.size() && i < (size_t)kPerKind; ++i) out.push_back(found[i].second);
    return out;
}

/* THE WHOLE READING: both logs and the dumps for program `exe` over [windowStart, now] (seconds since 1601), as the line.
   *elapsedMs: how long it took. */
inline std::string Gather(const std::string& exe, long long windowStart, long long now, const std::string& kenshiFile, DWORD* elapsedMs)
{
    const DWORD t0 = ::GetTickCount();
    const unsigned long long ageMs = now > windowStart ? (unsigned long long)(now - windowStart) * 1000ull : 0ull;
    std::vector<Record> recs;
    unsigned long appErr = 0, sysErr = 0;
    EvtApi a;
    const unsigned long le = LoadEvt(&a);
    if (le != 0) appErr = sysErr = le;
    else
    {
        appErr = ReadLog(a, L"Application", AppQuery(ageMs, exe), true, exe, t0, kAppShareMs, &recs);
        sysErr = ReadLog(a, L"System", SysQuery(ageMs), false, exe, t0, kBudgetMs, &recs);
        ::FreeLibrary(a.dll);
    }
    std::vector<std::string> dumps;
    wchar_t local[MAX_PATH];
    const DWORD n = ::GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (n != 0 && n < MAX_PATH - 16) dumps = DumpNames(std::wstring(local, n) + L"\\CrashDumps\\", exe, windowStart);
    *elapsedMs = ::GetTickCount() - t0;
    return Line(recs, appErr, sysErr, dumps, kenshiFile, (now - windowStart) / 60);
}

}   /* namespace swwinrec */
#endif
