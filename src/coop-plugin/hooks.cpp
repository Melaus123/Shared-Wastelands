/* mig1 (RE_Kenshi migration stage 1): coop::AddHook over the vendored MinHook "multihook" 4f18d18.
   A per-hook identity from a counter starting at 1, MH_CreateHookEx, then MH_EnableHookEx. This copy of MinHook
   is ours, so it is initialised here on the first AddHook. See hooks.h. */
#include "hooks.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <MinHook.h>
#include <stdio.h>

#include "coop_log.h"
#include "addresses.h"   /* review S3: AddrHookRefusedAt - a call-only row is never hooked */

namespace
{
    volatile LONG   g_mhInitState = 0;   /* 0 not started, 1 in progress, 2 done */
    volatile LONG64 g_hookIdent   = 0;   /* InterlockedIncrement64 hands out 1, 2, 3 ... */
    volatile LONG   g_installed   = 0;
    volatile LONG   g_failed      = 0;
    volatile LONG   g_callOnlyLogged = 0;   /* review S3: the first refused call-only target is logged, once */

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
        /* review S3 (2026-10-17): a row registered call-only (kAddrUseCallOnly) is one the gate may have accepted with
           another mod's jump at its entry - hooking it would detour that mod's jump, not the game's function. Refused
           (counted as failed), whatever the caller's next step; logged once per launch. */
        const char* callOnly = AddrHookRefusedAt((unsigned long long)(uintptr_t)target);
        if (callOnly != 0)
        {
            ::InterlockedIncrement(&g_failed);
            if (::InterlockedCompareExchange(&g_callOnlyLogged, 1, 0) == 0)
            {
                char line[240];
                _snprintf_s(line, sizeof(line), _TRUNCATE,
                            "[HOOK] AddHook REFUSED: row %s is registered call-only (kAddrUseCallOnly) - this mod must not hook it",
                            callOnly);
                ErrorLog(line);
            }
            return FAIL;
        }
        /* T-1447 (F1476 design A): a row where the gate accepted ANOTHER MOD'S jump.
           The live bytes are read again first: the jump still there and the rest unchanged, or the hook is refused. Then
           our MinHook hooks on top as usual - it copies the leading E9 into our trampoline as an absolute jump to its real
           target (third_party/minhook/src/trampoline.c:200-229), so the call runs our detour -> our trampoline -> the other
           mod's relay -> its detour -> the game.
           A "hooks, may chain" row is any row at a game function's start (src/common/addrhooked.h AddrForeignVerdict).
           NEVER UNHOOK such a hook (and this mod unhooks nothing - no MH_DisableHook / MH_RemoveHook / MH_Uninitialize
           outside the library): multihook's disable reads the E9 at the target and, when its relay is not ours, treats the
           memory around it as another multihook buffer and CALLS a pointer stored there (hook.c:554-560) - over a foreign
           jump that is a foreign pointer. */
        {
            const char* chainRow = 0;
            const int chained = AddrChainedCheck((unsigned long long)(uintptr_t)target, &chainRow);
            if (chained == 0)
            {
                ::InterlockedIncrement(&g_failed);
                const char* other = AddrChainedModule((unsigned long long)(uintptr_t)target);
                char line[400];
                _snprintf_s(line, sizeof(line), _TRUNCATE,
                            "[HOOK] AddHook REFUSED: row %s carried another mod's jump (to %s) the address gate accepted (hooks,"
                            " may chain), and its bytes changed before our hook - not hooked", chainRow != 0 ? chainRow : "?",
                            other != 0 ? other : "unknown");
                ErrorLog(line);
                return FAIL;
            }
            if (chained == 1)
            {
                char line[300];
                _snprintf_s(line, sizeof(line), _TRUNCATE,
                            "[HOOK] row %s: the other mod's jump is still in place and the rest unchanged - our hook goes on top of it"
                            " (hooks, may chain)", chainRow != 0 ? chainRow : "?");
                DebugLog(line);
            }
        }
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
        AddrChainedHooked((unsigned long long)(uintptr_t)target);   /* T-1447: a chained row now carries OUR jump - a later AddHook there re-checks against it */
        return SUCCESS;
    }

    void HookCounts(long* installed, long* failed)
    {
        if (installed) *installed = (long)g_installed;
        if (failed)    *failed    = (long)g_failed;
    }
}
