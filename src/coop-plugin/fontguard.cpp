// fontguard.cpp - T-730: a MyGUI text with no font answers its font name with an empty name instead of crashing.
//
// Kenshi's title screen refreshes the fonts of its texts (1.0.68 0x3E7F30, under the title screen's show 0x912E80) by asking each
// MyGUI EditText for its font name. MyGUI's EditText::getFontName (MyGUIEngine_x64.dll) is `mov rax,[rcx+0xE0]; add rax,8; ret`:
// with no font (+0xE0 null) it answers address 8, and the next read (FontManager::getByName, 8+0x10) faults. A text has no font
// when Kenshi builds its options panel (on the first key press or mouse button) before any font has loaded. The detour below
// answers such a text with an empty name, and MyGUI's setFontName("") then picks the default font. Every other text runs
// MyGUI's own body: that path takes no lock, allocates nothing and logs nothing, because it runs at every font refresh.
#include <windows.h>
#include <string>
#include <cstdio>
#include <cstring>
#include "fontguard.h"
#include "hooks.h"
#include "coop_log.h"
#include "../common/fontguard.h"   /* swfont: the body bytes, the jumps, the guard's test, the empty name's layout */

namespace {

const char* const kGetFontNameExport =
    "?getFontName@EditText@MyGUI@@UEBAAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ";

typedef const void* (*GetFontNameFn)(const void* self);
GetFontNameFn g_origGetFontName = 0;
volatile LONG g_fontlessAnswered = 0;
volatile LONG g_fontlessLogged = 0;

/* The empty name handed back: one permanent object laid out as the game runtime's std::string, never freed or moved, so the
   reference MyGUI receives stays valid however long it is held. Writable, so a stray write into it cannot fault. */
swfont::RuntimeString g_emptyName = { { 0 }, 0, swfont::kInlineCapacity };

const void* detour_editTextGetFontName(const void* self)
{
    const void* font = *(const void* const*)((const char*)self + swfont::kEditTextFontOffset);
    if (swfont::FontGuardNeeded(font) == 0) return g_origGetFontName(self);
    ::InterlockedIncrement(&g_fontlessAnswered);
    if (::InterlockedCompareExchange(&g_fontlessLogged, 1, 0) == 0)
        DebugLog("[FONTGUARD] a text with no font was asked its font name - answered with an empty"
                 " name so it takes the default font instead of crashing");
    return &g_emptyName;
}

/* Copies n bytes from p; 0 when the read faults. No C++ objects in this function (it holds an SEH frame). */
int ReadCodeBytes(const void* p, unsigned char* out, int n)
{
    __try
    {
        std::memcpy(out, p, (size_t)n);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

std::string HexOf(const unsigned char* b, int n)
{
    if (b == 0 || n <= 0) return "none";
    std::string s;
    char t[8];
    for (int i = 0; i < n; ++i)
    {
        std::sprintf(t, i == 0 ? "%02X" : " %02X", (unsigned int)b[i]);
        s += t;
    }
    return s;
}

std::string OffsetText(unsigned long long at, HMODULE dll)
{
    char t[64];
    std::sprintf(t, "0x%llX", at - (unsigned long long)dll);
    return t;
}

void NotInstalled(const std::string& reason, const unsigned char* b, int n)
{
    ErrorLog("[FONTGUARD] NOT installed - " + reason + " (body bytes: " + HexOf(b, n) + ")");
}

}   /* namespace */

namespace coop {

void InstallFontGuard()
{
    HMODULE dll = ::GetModuleHandleW(L"MyGUIEngine_x64.dll");
    if (dll == 0) { NotInstalled("MyGUIEngine_x64.dll is not loaded in this process", 0, 0); return; }
    FARPROC exported = ::GetProcAddress(dll, kGetFontNameExport);
    if (exported == 0) { NotInstalled("MyGUIEngine_x64.dll does not export EditText::getFontName", 0, 0); return; }
    unsigned long long at = (unsigned long long)exported;
    unsigned char head[6];
    int hops = 0;
    for (;;)
    {
        if (ReadCodeBytes((const void*)at, head, 6) == 0)
        {
            NotInstalled("the code at DLL offset " + OffsetText(at, dll) + " could not be read", 0, 0);
            return;
        }
        unsigned long long next = 0;
        const int kind = swfont::FontGuardJump(head, 6, at, &next);
        if (kind == swfont::kNoJump) break;
        if (hops >= swfont::kMaxJumps)
        {
            NotInstalled("more than 4 jumps from the export, the last at DLL offset " + OffsetText(at, dll), head, 6);
            return;
        }
        if (kind == swfont::kJumpIndirect)
        {
            unsigned long long target = 0;
            if (ReadCodeBytes((const void*)next, (unsigned char*)&target, 8) == 0)
            {
                NotInstalled("the jump slot of the jump at DLL offset " + OffsetText(at, dll) + " could not be read", head, 6);
                return;
            }
            next = target;
        }
        at = next;
        ++hops;
    }
    unsigned char body[swfont::kBodySize];
    if (ReadCodeBytes((const void*)at, body, swfont::kBodySize) == 0)
    {
        NotInstalled("the body at DLL offset " + OffsetText(at, dll) + " could not be read", head, 6);
        return;
    }
    if (swfont::FontGuardBodyMatches(body, swfont::kBodySize) == 0)
    {
        NotInstalled("the body at DLL offset " + OffsetText(at, dll) + " is not the `48 8B 81 E0 00 00 00 48 83 C0 08 C3` this build"
                     " was read against", body, swfont::kBodySize);
        return;
    }
    const HookStatus h = AddHook((void*)at, (void*)&detour_editTextGetFontName, (void**)&g_origGetFontName);
    if (h != SUCCESS || g_origGetFontName == 0)
    {
        NotInstalled("AddHook failed at DLL offset " + OffsetText(at, dll), body, swfont::kBodySize);
        return;
    }
    unsigned char after[5] = { 0, 0, 0, 0, 0 };   /* the hook counts as installed only when the patched bytes read back as a jump */
    if (ReadCodeBytes((const void*)at, after, 5) == 0 || after[0] != 0xE9)
    {
        NotInstalled("AddHook answered SUCCESS but the body does not start with the hook's jump at DLL offset " + OffsetText(at, dll), after, 5);
        return;
    }
    DebugLog("[FONTGUARD] installed on MyGUI EditText::getFontName (body bytes verified; the patched bytes read back as the hook's jump)");
}

void ReportFontGuard()
{
    char b[96];
    std::sprintf(b, "[FONTGUARD] fontless texts answered=%ld", (long)::InterlockedCompareExchange(&g_fontlessAnswered, 0, 0));
    DebugLog(b);
}

}   /* namespace coop */
