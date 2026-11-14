/* src/common/fileprobe.cpp - see fileprobe.h. C++03. Compiled by the plugin, the world server and the offline suite, each with
   ../coop-plugin/u8file.cpp (every file call takes a UTF-8 path and calls the wide Windows call). */
#include "fileprobe.h"
#include "../coop-plugin/u8file.h"

#include <windows.h>
#include <cstdio>
#include <fstream>

namespace coopstore {

namespace {
volatile LONG64 g_fileLockedSeen = 0, g_fileLockedRetried = 0;   /* T-1280: fileLocked[seen,retried] on the program's REPORT line */

std::string Num(long long v)
{
    char b[32];
    _snprintf(b, sizeof b - 1, "%lld", v);
    b[sizeof b - 1] = 0;
    return std::string(b);
}
}   // namespace

FileProbe ProbeFile(const std::string& path)
{
    FileProbe p;
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (U8GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &d) == 0)
    {
        const DWORD e = ::GetLastError();
        p.err = (unsigned long)e;
        /* ONLY THESE TWO MEAN ABSENT, and the attribute query is what a FILE_SHARE_NONE open does not
           defeat - which is why the pre-P8b loader survived exactly this case. An access denial or a
           device error says "it may well be there", which is not the same claim. */
        p.kind = (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? kFileAbsent : kFileUnreadable;
        return p;
    }
    p.attrLen = (long long)((((unsigned long long)d.nFileSizeHigh) << 32) | d.nFileSizeLow);
    if ((d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    { p.kind = kFileUnreadable; p.err = (unsigned long)ERROR_DIRECTORY; p.attrLen = -1; return p; }
    {
        std::ifstream f(U8W(path.c_str()).c_str(), std::ios::binary);
        if (!f)
        {
            /* The stream carries no Win32 error, so the open is repeated with a plain read share purely to
               get one for the log: "a scanner has it" (32) and "we are not allowed near it" (5) ask for
               different repairs, and a 0 here says it opens fine and the refusal was our own. */
            HANDLE h = U8CreateFile(path.c_str(), GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
            p.err = (h == INVALID_HANDLE_VALUE) ? (unsigned long)::GetLastError() : 0ul;
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            p.kind = kFileUnreadable;
            return p;
        }
        f.seekg(0, std::ios::end);
        const std::streamoff n = f.tellg();
        f.seekg(0);
        if (n < 0 || (long long)n > kProbeMaxBytes)
        { p.kind = kFileUnreadable; p.err = 0; return p; }   /* our own cap, and the 0 says so */
        p.bytes.resize((size_t)n);
        if (n) f.read(&p.bytes[0], n);
        if (!f)
        { p.bytes.clear(); p.kind = kFileUnreadable; p.err = (unsigned long)::GetLastError(); return p; }
        p.len = (long long)n;
    }
    /* A FILE OF ZERO BYTES IS READABLE WITH A LENGTH OF 0. That is a fact about the file, not a failure to
       read it, and Crc32 of no bytes is 0x00000000 by construction rather than by a special case. */
    p.crc = Crc32(p.bytes.empty() ? 0 : &p.bytes[0], p.bytes.size());
    p.kind = kFileReadable;
    return p;
}

std::string ProbeLeafName(const std::string& path)
{
    const std::string::size_type cut = path.find_last_of("\\/");
    return cut == std::string::npos ? path : path.substr(cut + 1);
}

int FileLockedStep(int kind, int* held)
{
    if (kind == kFileUnreadable)
    {
        const int r = (*held == 0) ? kLockSkipFirst : kLockSkipAgain;
        *held = 1;
        return r;
    }
    if (*held != 0) { *held = 0; return kLockRecovered; }
    return kLockGo;
}

int FileLockedNote(int kind, int* held)
{
    const int r = FileLockedStep(kind, held);
    if (r == kLockSkipFirst) ::InterlockedIncrement64(&g_fileLockedSeen);   /* T-1280 fold 1 (L8): the run once - its retries are not counted again */
    else if (r == kLockRecovered) ::InterlockedIncrement64(&g_fileLockedRetried);
    return r;
}

std::string FileLockedToken()
{
    return " fileLocked[seen,retried]=" + Num((long long)::InterlockedCompareExchange64(&g_fileLockedSeen, 0, 0)) + ","
         + Num((long long)::InterlockedCompareExchange64(&g_fileLockedRetried, 0, 0));
}

std::string FileLockedLine(const char* what, const std::string& path, const FileProbe& p, const char* keep)
{
    return std::string(what) + ": " + ProbeLeafName(path) + " is ON DISK (" + Num(p.attrLen) + " bytes) and could NOT BE READ (Windows error "
         + Num((long long)p.err) + (p.ownOpenFailed != 0 ? ", its own open failed and it read only on a second look" : (p.err == 0 ? ", the reader's own size cap" : "")) + ") - " + keep + "." + FileLockedToken();
}

}   /* namespace coopstore */
