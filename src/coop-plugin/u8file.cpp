/* u8file.cpp - the Windows file calls with UTF-8 paths (see u8file.h). */
#include "u8file.h"
#include "../common/u8path.h"

#include <direct.h>
#include <cstring>

volatile LONG g_u8AnsiFallback = 0;

std::wstring U8W(const char* path)
{
    std::wstring w;
    if (path == 0 || path[0] == 0) return w;
    const std::string s(path);
    if (u8path::Utf8Valid(s)) { u8path::Utf8ToUtf16(s, &w); return w; }
    /* not UTF-8: read in the ANSI code page, as the A-call this replaces did. Plain ASCII is well-formed UTF-8 and the empty
       string returned above, so every path counted here holds a byte above 0x7F that is not UTF-8. */
    ::InterlockedIncrement(&g_u8AnsiFallback);
    const int n = ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), 0, 0);
    if (n <= 0) return w;
    w.resize((size_t)n);
    ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string U8FromW(const wchar_t* w)
{
    if (w == 0) return std::string();
    return u8path::Utf16ToUtf8(std::wstring(w));
}

DWORD U8GetFileAttributes(const char* path)
{
    return ::GetFileAttributesW(U8W(path).c_str());
}

BOOL U8GetFileAttributesEx(const char* path, GET_FILEEX_INFO_LEVELS level, LPVOID info)
{
    return ::GetFileAttributesExW(U8W(path).c_str(), level, info);
}

HANDLE U8CreateFile(const char* path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition, DWORD flags, HANDLE tmpl)
{
    return ::CreateFileW(U8W(path).c_str(), access, share, sa, disposition, flags, tmpl);
}

BOOL U8MoveFileEx(const char* from, const char* to, DWORD flags)
{
    const std::wstring f = U8W(from), t = U8W(to);
    return ::MoveFileExW(f.c_str(), to == 0 ? 0 : t.c_str(), flags);
}

BOOL U8DeleteFile(const char* path)
{
    return ::DeleteFileW(U8W(path).c_str());
}

BOOL U8CopyFile(const char* from, const char* to, BOOL failIfExists)
{
    const std::wstring f = U8W(from), t = U8W(to);
    return ::CopyFileW(f.c_str(), t.c_str(), failIfExists);
}

BOOL U8CreateDirectory(const char* path, LPSECURITY_ATTRIBUTES sa)
{
    return ::CreateDirectoryW(U8W(path).c_str(), sa);
}

BOOL U8RemoveDirectory(const char* path)
{
    return ::RemoveDirectoryW(U8W(path).c_str());
}

DWORD U8GetFullPathName(const char* path, DWORD size, char* out, char** filePart)
{
    if (filePart) *filePart = 0;
    const std::wstring w = U8W(path);
    std::wstring buf(4096, L'\0');
    DWORD n = ::GetFullPathNameW(w.c_str(), (DWORD)buf.size(), &buf[0], 0);
    if (n >= (DWORD)buf.size())   /* longer than 4095 characters: ask again with the size Windows named */
    {
        buf.assign((size_t)n + 1, L'\0');
        n = ::GetFullPathNameW(w.c_str(), (DWORD)buf.size(), &buf[0], 0);
        if (n >= (DWORD)buf.size()) return 0;
    }
    if (n == 0) return 0;
    const std::string u = u8path::Utf16ToUtf8(buf.substr(0, n));
    if (out == 0 || u.size() + 1 > size) return (DWORD)(u.size() + 1);
    std::memcpy(out, u.c_str(), u.size() + 1);
    return (DWORD)u.size();
}

int U8Mkdir(const char* path)
{
    return ::_wmkdir(U8W(path).c_str());
}

FILE* U8fopen(const char* path, const char* mode)
{
    const std::wstring p = U8W(path), m = U8W(mode);
    return ::_wfopen(p.c_str(), m.c_str());
}

/* a NUL-separated list ended by an empty name, each name converted */
static std::wstring U8ListW(const char* list)
{
    std::wstring out;
    if (list != 0)
        for (const char* p = list; *p != 0; p += std::strlen(p) + 1) { out += U8W(p); out.push_back(L'\0'); }
    out.push_back(L'\0');   /* with c_str()'s own terminator: the list ends in two NULs even when it is empty */
    return out;
}

int U8SHFileOperation(SHFILEOPSTRUCTA* op)
{
    const std::wstring from = U8ListW(op->pFrom), to = U8ListW(op->pTo);
    const std::wstring title = U8W(op->lpszProgressTitle);
    SHFILEOPSTRUCTW w;
    std::memset(&w, 0, sizeof(w));
    w.hwnd = op->hwnd;
    w.wFunc = op->wFunc;
    w.pFrom = from.c_str();
    w.pTo = op->pTo == 0 ? 0 : to.c_str();
    w.fFlags = op->fFlags;
    w.lpszProgressTitle = op->lpszProgressTitle == 0 ? 0 : title.c_str();
    const int rc = ::SHFileOperationW(&w);
    op->fAnyOperationsAborted = w.fAnyOperationsAborted;
    op->hNameMappings = w.hNameMappings;
    return rc;
}

std::string U8GetEnv(const char* name)
{
    const std::wstring n = U8W(name);
    const DWORD need = ::GetEnvironmentVariableW(n.c_str(), 0, 0);
    if (need == 0) return std::string();
    std::wstring v((size_t)need, L'\0');
    const DWORD got = ::GetEnvironmentVariableW(n.c_str(), &v[0], need);
    if (got == 0 || got >= need) return std::string();
    v.resize(got);
    return u8path::Utf16ToUtf8(v);
}

static void U8FindCopy(const WIN32_FIND_DATAW& w, U8FindData* fd)
{
    fd->dwFileAttributes = w.dwFileAttributes;
    fd->ftCreationTime = w.ftCreationTime;
    fd->ftLastAccessTime = w.ftLastAccessTime;
    fd->ftLastWriteTime = w.ftLastWriteTime;
    fd->nFileSizeHigh = w.nFileSizeHigh;
    fd->nFileSizeLow = w.nFileSizeLow;
    const std::string name = U8FromW(w.cFileName);
    const size_t n = name.size() < sizeof(fd->cFileName) - 1 ? name.size() : sizeof(fd->cFileName) - 1;
    std::memcpy(fd->cFileName, name.c_str(), n);
    fd->cFileName[n] = 0;
}

HANDLE U8FindFirstFile(const char* pattern, U8FindData* fd)
{
    WIN32_FIND_DATAW w;
    const HANDLE h = ::FindFirstFileW(U8W(pattern).c_str(), &w);
    if (h != INVALID_HANDLE_VALUE) U8FindCopy(w, fd);
    return h;
}

BOOL U8FindNextFile(HANDLE h, U8FindData* fd)
{
    WIN32_FIND_DATAW w;
    if (!::FindNextFileW(h, &w)) return FALSE;
    U8FindCopy(w, fd);
    return TRUE;
}
