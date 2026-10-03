/* loader.cpp - SharedWastelandsLoader.dll. Owner decisions 114, 116, 117, 118, 123, 124.
 *
 * WHAT IT IS. An Ogre plugin. The game's own Plugins_x64.cfg gets one line, `Plugin=SharedWastelandsLoader` (owner 117 - an
 * installer adds it, owner 115), and the file lives in the game's own folder (owner 114). Ogre reads that cfg while the
 * game starts, prepends PluginFolder (.\) and appends .dll, loads this DLL and calls its dllStartPlugin; at shutdown it
 * calls dllStopPlugin. (Inferred from Ogre 1.x Root::loadPlugin/unloadPlugins: a missing dllStartPlugin, a DLL that will
 * not load, or an exception out of it STOPS THE GAME STARTING, and dllStopPlugin is looked up and called without a check -
 * so both exports exist, and neither can throw or fail.)
 *
 * WHEN IT DECIDES. Ogre loads its plugins BEFORE Kenshi's launcher window, and the launcher writes Kenshi's mod list
 * (data\mods.cfg) only when the player presses OK - so dllStartPlugin decides nothing. It registers ONE plugin object with
 * Ogre's Root::installPlugin (src/common/ogreplugin.h: a hand-built vtable, the OgreMain_x64.dll exports found by name)
 * and returns, having read no file, loaded nothing and written nothing. Ogre calls that object's initialise() from
 * Root::initialise, which the game reaches only after the launcher closed with OK; THAT is where the decision below is
 * made, from the mod list the launcher has just written. Cancel closes the game without reaching it. If Root is already
 * initialised when dllStartPlugin runs, Ogre calls initialise() inside installPlugin and the decision is made there and
 * then, by the same code. dllStopPlugin
 * uninstalls the object (the DLL is unloaded right after it, and Ogre must not keep a pointer into it).
 *
 * THE DECISION (owner 118): it starts the multiplayer mod ONLY, but finds it by a MARKER FILE rather than a hard-coded
 * folder or DLL name, so it could become a general loader later. In order:
 *   1. <game folder>\mods\*\shared-wastelands.loader.txt       (a local mods copy wins - manager decision)
 *   2. <game folder>\..\..\workshop\content\233860\*\<same name>  (Steam Workshop items for Kenshi, app 233860;
 *                                                                  no item id is hard-coded)
 * The FIRST marker that parses (src/common/loadermarker.h), whose DLL exists AND whose folder is SWITCHED ON in Kenshi's
 * own mod list is used: that DLL is loaded by full path and the export the marker names (coopEarlyStart for the multiplayer
 * mod, which starts the mod at once) is called once. A marker that does not parse, names a DLL that is not there, or sits
 * in a folder that is switched off is skipped and counted - a switched-off local copy never blocks a switched-on Workshop
 * copy.
 *
 * SWITCHED ON (owner decisions 123/124): a folder is switched on when one of its *.mod files is named on a line of
 * <game folder>\data\mods.cfg (src/common/modsenabled.h). The list is read ONCE, at the decision; if it is missing,
 * unreadable or larger than 64 KB NOTHING is loaded and the log line says why (fail closed, owner 124).
 *
 * THE GAME FOLDER is this loader's OWN folder (it is installed in the game folder, owner 114), not the running exe's:
 * RE_Kenshi can relaunch the game from <game>\RE_Kenshi\, and the exe's folder is then the wrong place to look.
 * ONE line goes to <game folder>\SharedWastelandsLoader_log.txt (rewritten each start) saying what was found and loaded or
 * why nothing was: written at the decision, before the mod's start export is called; or by dllStartPlugin when the object
 * could not be registered; or by dllStopPlugin when the game closed before the decision was made.
 *
 * NEVER THROW, NEVER FAIL: every road ends in a log line and a return. The mod's start export catches its own exceptions.
 *
 * KERNEL32 ONLY, NO C RUNTIME: linked /NODEFAULTLIB with its own entry point (LoaderEntry) and /GS- (no security cookie,
 * which lives in the CRT). Chosen over the static CRT because this DLL is loaded into every game start by the game
 * itself: with no CRT there is no CRT start-up code, no heap of its own and no second runtime in the process - the
 * whole DLL is the code below. With no CRT start-up, no global may need code to initialise it (the plugin object and
 * its vtable are filled in by dllStartPlugin). Every buffer is a fixed-size global (no stack frame near 4 KB, so no
 * __chkstk), and no library call is made except the KERNEL32 ones (OgreMain is reached through GetProcAddress). The build
 * gate (build.bat) checks the import list is KERNEL32.dll alone.
 *
 * C++03 (VS2010 v100).
 */
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "loadermarker.h"
#include "names.h"   /* the on-disk names (mod folder, files, window texts) */
#include "modsenabled.h"   /* owner 123/124: is a folder switched on in Kenshi's mod list */
#include "ogreplugin.h"    /* the Ogre plugin object's layout and the OgreMain exports */

/* With no C runtime, memset must exist here: the VS2010 optimiser turns a plain zeroing loop (loadermarker.h's Clear) into
   a memset call. `#pragma function` stops it being treated as an intrinsic, and the volatile store stops this loop itself
   from being turned back into a memset call. */
extern "C" void* __cdecl memset(void* dst, int c, size_t n);
#pragma function(memset)
extern "C" void* __cdecl memset(void* dst, int c, size_t n)
{
    volatile unsigned char* p = (volatile unsigned char*)dst;
    while (n-- != 0) *p++ = (unsigned char)c;
    return dst;
}

namespace {

enum { kPath = 1024, kLog = 6144 };

/* All state is here: the loader runs on the game's main thread - registration inside Ogre's plugin load, the decision
   inside Ogre's Root::initialise. */
HMODULE g_self = 0;           /* this DLL (LoaderEntry) - its folder is the game folder */
wchar_t g_game[kPath];        /* the game folder (this loader's own), no trailing backslash */
wchar_t g_dir[kPath];         /* the folder being searched */
wchar_t g_find[kPath];        /* <dir>\* */
wchar_t g_cand[kPath];        /* <dir>\<sub>\<marker> */
wchar_t g_modDir[kPath];      /* the chosen mod folder */
wchar_t g_dllPath[kPath];     /* the chosen DLL, full path */
wchar_t g_markerPath[kPath];  /* the chosen marker, full path */
wchar_t g_tmp[kPath];
wchar_t g_search[kPath];     /* the mods / Workshop folder being searched */
WIN32_FIND_DATAW g_fd;
char g_text[cooploader::kMarkerMaxBytes + 2];
cooploader::MarkerResult g_mr;
char g_chosenDll[cooploader::kMarkerNameMax + 1];
char g_chosenStart[cooploader::kMarkerNameMax + 1];
char g_log[kLog];
int  g_logLen = 0;
int  g_markersBad = 0;         /* markers that did not parse */
int  g_markersNoDll = 0;       /* markers that parsed but named a DLL that is not there */
char g_firstBad[512];          /* the first skipped marker's reason, for the log line */
/* owner 123/124: Kenshi's mod list and the switched-on check */
wchar_t g_modsCfg[kPath];                          /* <game>\data\mods.cfg */
char g_mods[coopmods::kModsCfgMaxBytes + 2];       /* its bytes (a GLOBAL: 64 KB never goes near the stack) */
long g_modsLen = 0;
wchar_t g_modFind[kPath];                          /* <mod folder>\*.mod */
WIN32_FIND_DATAW g_fdMod;                          /* separate from g_fd: SearchDir's own enumeration is still open */
char g_modNameU[kPath * 3];                        /* one .mod file name, UTF-8 */
int  g_switchedOff = 0;                            /* usable copies skipped because switched off */
char g_firstOff[512];                              /* the first switched-off folder, for the log line */
/* the Ogre plugin object (src/common/ogreplugin.h) - filled in by dllStartPlugin, never by an initialiser. The table Ogre
   sees is g_vtbl + 1; g_vtbl[0], the slot just before it, holds Ogre::Plugin's complete-object locator (0 when not found). */
struct HandPlugin { void* const* vtbl; };
void* g_vtbl[1 + swogre::kPluginSlots];
int g_noTypeInfo = 0;                              /* 1 = no Ogre::Plugin vtable export, 2 = its slot [-1] is not a locator */
HandPlugin g_plugin;
swogre::Vs2010String g_pluginName;
typedef void* (*RootGetFn)(void);
typedef void (*RootPluginFn)(void* root, void* plugin);
void* g_root = 0;                                  /* Ogre's Root the object is installed in; 0 = not installed */
RootPluginFn g_uninstall = 0;
volatile LONG g_decided = 0;                       /* the decision has run (or is running) */

/* ---- tiny string helpers (no CRT) ---------------------------------------------------------------------------- */
int WLen(const wchar_t* s) { int n = 0; while (s[n] != 0) ++n; return n; }

/* dst = a (+ b)(+ c); false (and dst = "") when it would not fit */
bool WJoin(wchar_t* dst, const wchar_t* a, const wchar_t* b, const wchar_t* c)
{
    const wchar_t* parts[3] = { a, b, c };
    int n = 0, i;
    for (i = 0; i < 3; ++i)
    {
        const wchar_t* s = parts[i];
        if (s == 0) continue;
        while (*s != 0)
        {
            if (n >= kPath - 1) { dst[0] = 0; return false; }
            dst[n++] = *s++;
        }
    }
    dst[n] = 0;
    return true;
}

/* ASCII -> wide; the marker's names are ASCII by construction (loadermarker.h). */
void Widen(wchar_t* dst, int cap, const char* s)
{
    int i = 0;
    while (s[i] != 0 && i < cap - 1) { dst[i] = (wchar_t)(unsigned char)s[i]; ++i; }
    dst[i] = 0;
}

void CopyA(char* dst, int cap, const char* s)
{
    int i = 0;
    while (s[i] != 0 && i < cap - 1) { dst[i] = s[i]; ++i; }
    dst[i] = 0;
}

void LogA(const char* s) { while (*s != 0 && g_logLen < kLog - 3) g_log[g_logLen++] = *s++; }

void LogW(const wchar_t* s)
{
    const int room = kLog - 3 - g_logLen;
    if (room <= 0) return;
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s, -1, g_log + g_logLen, room, 0, 0);
    if (n > 0) g_logLen += n - 1;   /* n counts the NUL */
}

void LogU(unsigned long v)
{
    char b[16];
    int n = 0;
    do { b[n++] = (char)('0' + (v % 10)); v /= 10; } while (v != 0 && n < 15);
    while (n > 0 && g_logLen < kLog - 3) g_log[g_logLen++] = b[--n];
}

void Log2(unsigned long v) { if (v < 10) LogA("0"); LogU(v); }

/* Write the one line, replacing the previous start's. Failure is silent: there is nowhere else to say it. */
void FlushLog()
{
    g_log[g_logLen++] = '\r';
    g_log[g_logLen++] = '\n';
    if (g_game[0] == 0 || !WJoin(g_tmp, g_game, swnames::kLoaderLogW, 0)) return;   /* no game folder: nowhere to write */
    HANDLE h = ::CreateFileW(g_tmp, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    ::WriteFile(h, g_log, (DWORD)g_logLen, &wrote, 0);
    ::CloseHandle(h);
}

/* ---- the search ---------------------------------------------------------------------------------------------- */
bool IsFile(const wchar_t* p)
{
    const DWORD a = ::GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/* Read the marker at g_cand into g_text and parse it into g_mr; false when unreadable. */
bool ReadMarker()
{
    HANDLE h = ::CreateFileW(g_cand, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) { cooploader::MarkerParse(0, -1, &g_mr); return false; }
    DWORD got = 0;
    const BOOL ok = ::ReadFile(h, g_text, (DWORD)(cooploader::kMarkerMaxBytes + 1), &got, 0);
    ::CloseHandle(h);
    if (!ok) { cooploader::MarkerParse(0, -1, &g_mr); return false; }
    cooploader::MarkerParse(g_text, (long)got, &g_mr);
    return true;
}

void NoteBad(const char* why)
{
    if (g_firstBad[0] != 0) return;
    int n = 0;
    const int room = (int)sizeof(g_firstBad) - 1;
    const int w = ::WideCharToMultiByte(CP_UTF8, 0, g_cand, -1, g_firstBad, room - 8, 0, 0);
    if (w > 0) n = w - 1;
    const char* sep = ": ";
    while (*sep != 0 && n < room) g_firstBad[n++] = *sep++;
    while (*why != 0 && n < room) g_firstBad[n++] = *why++;
    g_firstBad[n] = 0;
}

/* Owner 123: is the mod folder `folder` switched on - does one of its *.mod files appear as a line of
   Kenshi's mod list (g_mods)? The name must END in .mod: FindFirstFile's *.mod also matches 8.3 short names. */
bool FolderSwitchedOn(const wchar_t* folder)
{
    if (!WJoin(g_modFind, folder, L"\\*.mod", 0)) return false;
    HANDLE f = ::FindFirstFileW(g_modFind, &g_fdMod);
    if (f == INVALID_HANDLE_VALUE) return false;
    bool on = false;
    do
    {
        if ((g_fdMod.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        const wchar_t* n = g_fdMod.cFileName;
        const int len = WLen(n);
        if (len < 5 || n[len - 4] != L'.' || (n[len - 3] | 0x20) != L'm' || (n[len - 2] | 0x20) != L'o'
            || (n[len - 1] | 0x20) != L'd') continue;
        const int w = ::WideCharToMultiByte(CP_UTF8, 0, n, -1, g_modNameU, (int)sizeof(g_modNameU), 0, 0);
        if (w <= 1) continue;
        if (coopmods::ModsCfgLists(g_mods, g_modsLen, g_modNameU)) { on = true; break; }
    } while (::FindNextFileW(f, &g_fdMod));
    ::FindClose(f);
    return on;
}

void NoteOff(const wchar_t* folder)
{
    if (g_firstOff[0] != 0) return;
    const int w = ::WideCharToMultiByte(CP_UTF8, 0, folder, -1, g_firstOff, (int)sizeof(g_firstOff) - 1, 0, 0);
    if (w <= 0) g_firstOff[0] = 0;
}

/* Search every direct subfolder of `dir` for the marker; true (and the g_chosen* / g_*Path set) on the first usable one.
   *seen counts the markers found here, usable or not. */
bool SearchDir(const wchar_t* dir, int* seen)
{
    if (!WJoin(g_dir, dir, 0, 0)) return false;
    if (!WJoin(g_find, g_dir, L"\\*", 0)) return false;
    HANDLE f = ::FindFirstFileW(g_find, &g_fd);
    if (f == INVALID_HANDLE_VALUE) return false;
    wchar_t marker[64];
    Widen(marker, 64, cooploader::MarkerFileName());
    bool found = false;
    do
    {
        if ((g_fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        const wchar_t* n = g_fd.cFileName;
        if (n[0] == L'.' && (n[1] == 0 || (n[1] == L'.' && n[2] == 0))) continue;
        if (!WJoin(g_tmp, g_dir, L"\\", n)) continue;
        if (!WJoin(g_cand, g_tmp, L"\\", marker)) continue;
        if (!IsFile(g_cand)) continue;
        ++*seen;
        if (!ReadMarker() || g_mr.ok == 0)
        {
            ++g_markersBad;
            NoteBad(g_mr.why);
            continue;
        }
        wchar_t dllW[cooploader::kMarkerNameMax + 1];
        Widen(dllW, cooploader::kMarkerNameMax + 1, g_mr.dll);
        if (!WJoin(g_dllPath, g_tmp, L"\\", dllW) || !IsFile(g_dllPath))
        {
            ++g_markersNoDll;
            NoteBad("the DLL it names is not in that folder");
            continue;
        }
        if (!FolderSwitchedOn(g_tmp))   /* owner 123: switched off -> the NEXT folder, never a stop */
        {
            ++g_switchedOff;
            NoteOff(g_tmp);
            continue;
        }
        WJoin(g_modDir, g_tmp, 0, 0);
        WJoin(g_markerPath, g_cand, 0, 0);
        CopyA(g_chosenDll, cooploader::kMarkerNameMax + 1, g_mr.dll);
        CopyA(g_chosenStart, cooploader::kMarkerNameMax + 1, g_mr.start);
        found = true;
        break;
    } while (::FindNextFileW(f, &g_fd));
    ::FindClose(f);
    return found;
}

void LogStamp()
{
    SYSTEMTIME t;
    ::GetLocalTime(&t);
    LogU(t.wYear); LogA("-"); Log2(t.wMonth); LogA("-"); Log2(t.wDay); LogA(" ");
    Log2(t.wHour); LogA(":"); Log2(t.wMinute); LogA(":"); Log2(t.wSecond); LogA(" ");
}

void LogHead()
{
    LogStamp();
    LogA(swnames::kLoaderPlugin); LogA(" (pid "); LogU(::GetCurrentProcessId()); LogA("): ");
}

/* g_game = THIS loader's own folder (owner 114: it is installed there). Not the exe's: RE_Kenshi can relaunch the game
   from <game>\RE_Kenshi\. false (and the reason logged) when it cannot be worked out. */
bool FindGameFolder()
{
    const DWORD n = ::GetModuleFileNameW(g_self, g_tmp, kPath);
    if (g_self == 0 || n == 0 || n >= (DWORD)kPath) { LogA("the loader's own path could not be read - nothing loaded"); return false; }
    int cut = WLen(g_tmp);
    while (cut > 0 && g_tmp[cut - 1] != L'\\' && g_tmp[cut - 1] != L'/') --cut;
    if (cut <= 1) { LogA("the loader's own path has no folder - nothing loaded"); return false; }
    g_tmp[cut - 1] = 0;
    WJoin(g_game, g_tmp, 0, 0);
    return true;
}

/* THE DECISION - from the plugin object's initialise(), inside Ogre's Root::initialise, after the launcher's OK. */
void Decide()
{
    LogHead();
    LogA("decided after the launcher's OK (Ogre's Root::initialise): ");
    if (g_noTypeInfo != 0)
        LogA(g_noTypeInfo == 1 ? "[OgreMain_x64.dll has no Ogre::Plugin vtable export, so the plugin object carries no type information] "
                               : "[Ogre::Plugin's vtable has no type information before it, so the plugin object carries none] ");
    if (!FindGameFolder()) return;

    /* Kenshi's own mod list, read ONCE, now (owner 123). Missing, unreadable or over 64 KB -> nothing loaded (owner 124). */
    if (!WJoin(g_modsCfg, g_game, L"\\data\\mods.cfg", 0)) { LogA("the path of Kenshi's mod list is too long - nothing loaded"); return; }
    {
        HANDLE h = ::CreateFileW(g_modsCfg, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
        DWORD e = 0, got = 0;
        BOOL ok = FALSE;
        if (h == INVALID_HANDLE_VALUE) e = ::GetLastError();
        else
        {
            ok = ::ReadFile(h, g_mods, (DWORD)(coopmods::kModsCfgMaxBytes + 1), &got, 0);
            e = ::GetLastError();
            ::CloseHandle(h);
        }
        const coopmods::ModsCfgState st = coopmods::ModsCfgReadState(h != INVALID_HANDLE_VALUE, ok != FALSE, (unsigned long)got);
        if (st != coopmods::kModsCfgUsable)
        {
            LogA("Kenshi's mod list "); LogW(g_modsCfg); LogA(" "); LogA(coopmods::ModsCfgStateWhy(st));
            if (st != coopmods::kModsCfgTooLarge) { LogA(" (Windows error "); LogU(e); LogA(")"); }
            LogA(" - nothing loaded: the multiplayer mod stays off when its switch in the mod list cannot be read");
            return;
        }
        g_modsLen = (long)got;
    }

    int seenMods = 0, seenWorkshop = 0;
    const char* where = "";
    if (WJoin(g_search, g_game, L"\\mods", 0) && SearchDir(g_search, &seenMods))
        where = "the game's mods folder";
    else
    {
        /* Steam: <library>\steamapps\common\Kenshi -> <library>\steamapps\workshop\content\233860 */
        if (WJoin(g_tmp, g_game, L"\\..\\..\\workshop\\content\\233860", 0))
        {
            const DWORD m = ::GetFullPathNameW(g_tmp, kPath, g_search, 0);
            if (m > 0 && m < (DWORD)kPath && SearchDir(g_search, &seenWorkshop)) where = "the Steam Workshop folder";
        }
    }
    if (where[0] == 0)
    {
        LogA("no usable ");
        LogA(cooploader::MarkerFileName());
        LogA(" in "); LogW(g_game); LogA("\\mods\\*\\ ("); LogU((unsigned long)seenMods);
        LogA(" found) or the Steam Workshop folder ("); LogU((unsigned long)seenWorkshop);
        LogA(" found) - nothing loaded; the game starts without the multiplayer mod");
        if (g_switchedOff > 0)
        {
            LogA(". "); LogU((unsigned long)g_switchedOff);
            LogA(" usable cop(ies) are switched off in Kenshi's mod list "); LogW(g_modsCfg); LogA(" (first: "); LogA(g_firstOff); LogA(")");
        }
        if (g_firstBad[0] != 0) { LogA(". First skipped: "); LogA(g_firstBad); }
        return;
    }

    LogA("marker "); LogW(g_markerPath); LogA(" (in "); LogA(where);
    LogA("; markers skipped: "); LogU((unsigned long)(g_markersBad + g_markersNoDll)); LogA(")");
    if (g_firstBad[0] != 0) { LogA(" [first skipped: "); LogA(g_firstBad); LogA("]"); }
    LogA(" [switched on in Kenshi's mod list "); LogW(g_modsCfg);
    if (g_switchedOff > 0) { LogA("; "); LogU((unsigned long)g_switchedOff); LogA(" switched-off cop(ies) skipped, first: "); LogA(g_firstOff); }
    LogA("]");
    if (g_mr.ignoredKeys > 0) { LogA(" ["); LogU((unsigned long)g_mr.ignoredKeys); LogA(" unknown key(s) ignored]"); }

    /* Load by full path. The mod's own folder is searched for ITS dependencies first, then the game folder and System32
       (LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS). A system without those flags answers
       ERROR_INVALID_PARAMETER, and then the older LOAD_WITH_ALTERED_SEARCH_PATH does the same for a full path. */
    HMODULE h = ::LoadLibraryExW(g_dllPath, 0, 0x00000100 | 0x00001000);
    if (h == 0 && ::GetLastError() == ERROR_INVALID_PARAMETER)
        h = ::LoadLibraryExW(g_dllPath, 0, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (h == 0)
    {
        const DWORD e = ::GetLastError();
        LogA("; LoadLibrary of "); LogW(g_dllPath); LogA(" FAILED (Windows error "); LogU(e);
        LogA(") - nothing started; the game starts without the multiplayer mod");
        return;
    }
    typedef void (*StartFn)(void);
    StartFn fn = (StartFn)::GetProcAddress(h, g_chosenStart);
    if (fn == 0)
    {
        LogA("; loaded "); LogW(g_dllPath); LogA(" but it has no export '"); LogA(g_chosenStart);
        LogA("' - nothing started; the game starts without the multiplayer mod");
        return;
    }
    LogA("; loaded "); LogW(g_dllPath); LogA("; calling "); LogA(g_chosenStart);
    LogA(" now (its own log says what it did)");
    FlushLog();
    g_logLen = -1;   /* written: the caller must not write it again */
    fn();
}

/* ---- the Ogre plugin object (src/common/ogreplugin.h) ------------------------------------------------------------ */
void* PluginDestructor(void* self, unsigned int) { return self; }   /* never called: Ogre deletes only plugins it created */
const void* PluginGetName(const void*) { return &g_pluginName; }
void PluginNothing(void*) {}
void PluginInitialise(void*)
{
    if (::InterlockedExchange(&g_decided, 1) != 0) return;   /* Ogre initialises a plugin once; a second call does nothing */
    Decide();
    if (g_logLen >= 0) FlushLog();
}

/* The complete-object locator Ogre::Plugin's own vtable is preceded by (its slot [-1]); 0, and g_noTypeInfo says why, when
   the export is missing or that slot does not hold one. Every read is inside OgreMain_x64.dll's mapped image. */
void* OgrePluginLocator(HMODULE ogre)
{
    const unsigned char* base = (const unsigned char*)ogre;
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) { g_noTypeInfo = 2; return 0; }
    const IMAGE_NT_HEADERS64* nt = (const IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) { g_noTypeInfo = 2; return 0; }
    const unsigned long long size = nt->OptionalHeader.SizeOfImage;
    const unsigned char* vt = (const unsigned char*)::GetProcAddress(ogre, swogre::PluginVtableExport());
    if (vt == 0) { g_noTypeInfo = 1; return 0; }
    if (vt < base + 8 || (unsigned long long)(vt - base) >= size) { g_noTypeInfo = 2; return 0; }
    void* col = *(void* const*)(vt - 8);
    if (!swogre::IsObjectLocator(base, size, col)) { g_noTypeInfo = 2; return 0; }
    return col;
}

/* Install the plugin object in Ogre's Root; false (and the reason logged) when it cannot be. */
bool Register()
{
    void** vt = g_vtbl + 1;
    vt[swogre::kSlotDestructor] = (void*)&PluginDestructor;
    vt[swogre::kSlotGetName] = (void*)&PluginGetName;
    vt[swogre::kSlotInstall] = (void*)&PluginNothing;
    vt[swogre::kSlotInitialise] = (void*)&PluginInitialise;
    vt[swogre::kSlotShutdown] = (void*)&PluginNothing;
    vt[swogre::kSlotUninstall] = (void*)&PluginNothing;
    g_plugin.vtbl = vt;
    swogre::Vs2010StringSet(&g_pluginName, "SWLoader");

    HMODULE ogre = ::GetModuleHandleW(swogre::OgreMainDll());
    if (ogre == 0)
    {
        LogA("OgreMain_x64.dll is not loaded in this process (Windows error "); LogU(::GetLastError());
        LogA(") - the loader could not register with Ogre; nothing loaded, the game starts without the multiplayer mod");
        return false;
    }
    g_vtbl[0] = OgrePluginLocator(ogre);
    RootGetFn getRoot = (RootGetFn)::GetProcAddress(ogre, swogre::RootGetSingletonPtrExport());
    RootPluginFn install = (RootPluginFn)::GetProcAddress(ogre, swogre::RootInstallPluginExport());
    RootPluginFn uninstall = (RootPluginFn)::GetProcAddress(ogre, swogre::RootUninstallPluginExport());
    if (getRoot == 0 || install == 0 || uninstall == 0)
    {
        LogA("OgreMain_x64.dll lacks ");
        LogA(getRoot == 0 ? swogre::RootGetSingletonPtrExport() : install == 0 ? swogre::RootInstallPluginExport()
                                                                               : swogre::RootUninstallPluginExport());
        LogA(" - the loader could not register with Ogre; nothing loaded, the game starts without the multiplayer mod");
        return false;
    }
    void* root = getRoot();
    if (root == 0)
    {
        LogA("Ogre's Root does not exist yet - the loader could not register with Ogre; nothing loaded, the game starts"
             " without the multiplayer mod");
        return false;
    }
    g_root = root;
    g_uninstall = uninstall;
    g_logLen = 0;   /* registered: dllStartPlugin's header is not written; the decision writes its own line */
    /* With Root not yet initialised (the launcher has not closed) this only lists the object. With Root already initialised
       Ogre calls initialise() inside this call, and the decision is made and logged before it returns. */
    install(root, &g_plugin);
    return true;
}

}   /* anonymous namespace */

/* Registers the plugin object and returns: no file is read or written and nothing is loaded here, unless the object
   cannot be registered - then that is the final answer and the log line says why - or Root is already initialised, when
   the decision runs inside the registration (Register). */
extern "C" __declspec(dllexport) void dllStartPlugin(void)
{
    static LONG once = 0;
    if (::InterlockedExchange(&once, 1) != 0) return;   /* Ogre loads a plugin once; a second call does nothing */
    LogHead();
    if (FindGameFolder() && Register()) return;   /* registered: this function writes nothing */
    g_decided = 1;   /* nothing will decide: this line is the answer */
    FlushLog();
}

/* Takes the plugin object out of Ogre's Root before Ogre unloads this DLL. When the game closes before the decision (the
   launcher's Cancel, or a close before Ogre's start-up finished) the log line says so. */
extern "C" __declspec(dllexport) void dllStopPlugin(void)
{
    if (g_root == 0) return;
    if (::InterlockedExchange(&g_decided, 1) == 0)
    {
        g_logLen = 0;
        LogHead();
        if (FindGameFolder())
        {
            LogA("the game closed before Ogre's start-up finished (the launcher's Cancel, or a close before the game window)"
                 " - nothing loaded");
            FlushLog();
        }
    }
    void* root = g_root;
    g_root = 0;
    g_uninstall(root, &g_plugin);
}

extern "C" BOOL WINAPI LoaderEntry(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) { g_self = inst; ::DisableThreadLibraryCalls(inst); }
    return TRUE;
}
