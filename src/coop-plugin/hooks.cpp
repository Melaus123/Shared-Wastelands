/* mig1 (RE_Kenshi migration stage 1): coop::AddHook over the vendored MinHook "multihook" 4f18d18.
   A per-hook identity from a counter starting at 1, MH_CreateHookEx, then MH_EnableHookEx. This copy of MinHook
   is ours, so it is initialised here on the first AddHook. See hooks.h. */
#include "hooks.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <MinHook.h>
#include <stdio.h>

#include "coop_log.h"

namespace
{
    volatile LONG   g_mhInitState = 0;   /* 0 not started, 1 in progress, 2 done */
    volatile LONG64 g_hookIdent   = 0;   /* InterlockedIncrement64 hands out 1, 2, 3 ... */
    volatile LONG   g_installed   = 0;
    volatile LONG   g_failed      = 0;

    void EnsureMinHook()
    {
        if (g_mhInitState == 2)
            return;
        if (::InterlockedCompareExchange(&g_mhInitState, 1, 0) == 0)
        {
            const MH_STATUS st = MH_Initialize();
            if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED)
            {
                char line[160];
                _snprintf_s(line, sizeof(line), _TRUNCATE, "[HOOK] MH_Initialize FAILED: MH status %s(%d)",
                            MH_StatusToString(st), (int)st);
                ErrorLog(line);
            }
            ::InterlockedExchange(&g_mhInitState, 2);
            return;
        }
        while (g_mhInitState != 2)
            ::Sleep(0);
    }

    void LogFailure(const char* step, MH_STATUS st, void* target)
    {
        const unsigned long long rva =
            (unsigned long long)((uintptr_t)target - (uintptr_t)::GetModuleHandleA(0));
        char line[200];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[HOOK] AddHook FAILED at %s: MH status %s(%d) target rva 0x%llX",
                    step, MH_StatusToString(st), (int)st, rva);
        ErrorLog(line);
    }
}

namespace coop
{
    HookStatus AddHook(void* target, void* detour, void** original)
    {
        EnsureMinHook();
        const ULONG_PTR ident = (ULONG_PTR)::InterlockedIncrement64(&g_hookIdent);
        MH_STATUS status = MH_CreateHookEx(ident, target, detour, original);
        if (status != MH_OK)
        {
            ::InterlockedIncrement(&g_failed);
            LogFailure("create", status, target);
            return FAIL;
        }
        status = MH_EnableHookEx(ident, target);
        if (status != MH_OK)
        {
            ::InterlockedIncrement(&g_failed);
            LogFailure("enable", status, target);
            return FAIL;
        }
        ::InterlockedIncrement(&g_installed);
        return SUCCESS;
    }

    void HookCounts(long* installed, long* failed)
    {
        if (installed) *installed = (long)g_installed;
        if (failed)    *failed    = (long)g_failed;
    }
}
