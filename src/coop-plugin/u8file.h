/* u8file.h - the Windows file calls the mod uses, taking UTF-8 paths.
 *
 * The game hands the mod UTF-8 text (its save folder, a save's name, a squad's name) and the mod's own paths are
 * UTF-8 too (the data folder is read from the environment as UTF-16 and converted). The older Windows calls (the
 * A-variants: GetFileAttributesA, FindFirstFileA, CreateFileA, fopen ...) read a path in the ANSI code page, so a
 * path holding a letter outside plain ASCII - a Russian user name, a squad named in Chinese - names a file that is
 * not there. Each function here converts the path and calls the wide (W) variant; the arguments and the answer are
 * the A-variant's, so a call site changes its function name and nothing else.
 *
 * A path that is not well-formed UTF-8 is read in the ANSI code page instead (U8W), as the A-call read it, so a path
 * that still reaches a wrapper in ANSI names the file it named before; each such reading is counted (g_u8AnsiFallback).
 * This DLL's own folder (PathNextToDll) is UTF-8 like every other path.
 *
 * Names a find loop returns (U8FindData::cFileName) are UTF-8, so they compare equal to the game's names.
 *
 * The world server (src/coop-store/store_main.cpp) compiles this same file (src/coop-store/build.bat): its paths and
 * names are UTF-8 too - its arguments are read as UTF-16 (wmain) and converted - and every file call it makes goes
 * through these wrappers.
 */
#pragma once

#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <string>

std::wstring U8W(const char* path);              /* UTF-8 (or, when malformed, ANSI) -> UTF-16 */
std::string  U8FromW(const wchar_t* w);          /* UTF-16 -> UTF-8 */
extern volatile LONG g_u8AnsiFallback;          /* how many non-empty, non-ASCII paths U8W read in the ANSI code page (not UTF-8) */

DWORD  U8GetFileAttributes(const char* path);
BOOL   U8GetFileAttributesEx(const char* path, GET_FILEEX_INFO_LEVELS level, LPVOID info);
HANDLE U8CreateFile(const char* path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition, DWORD flags, HANDLE tmpl);
BOOL   U8MoveFileEx(const char* from, const char* to, DWORD flags);
BOOL   U8DeleteFile(const char* path);
BOOL   U8CopyFile(const char* from, const char* to, BOOL failIfExists);
BOOL   U8CreateDirectory(const char* path, LPSECURITY_ATTRIBUTES sa);
BOOL   U8RemoveDirectory(const char* path);
/* the full path as UTF-8 in out: the bytes written (without the NUL), or the size needed (with it) when size is too small, 0 on failure */
DWORD  U8GetFullPathName(const char* path, DWORD size, char* out, char** filePart);
int    U8Mkdir(const char* path);                /* _mkdir's answer: 0 made, -1 not (errno set, EEXIST = already there) */
FILE*  U8fopen(const char* path, const char* mode);
/* SHFileOperation with UTF-8 pFrom / pTo lists (each name NUL-ended, the list ended by an empty name); fAnyOperationsAborted is copied back */
int    U8SHFileOperation(SHFILEOPSTRUCTA* op);
std::string U8GetEnv(const char* name);          /* an environment variable as UTF-8; "" when it is not set */

/* WIN32_FIND_DATAA's fields the mod reads, with cFileName in UTF-8 (a 260-character name needs up to 780 bytes) */
struct U8FindData
{
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD    nFileSizeHigh;
    DWORD    nFileSizeLow;
    char     cFileName[MAX_PATH * 4];
};
HANDLE U8FindFirstFile(const char* pattern, U8FindData* fd);   /* close with ::FindClose */
BOOL   U8FindNextFile(HANDLE h, U8FindData* fd);
