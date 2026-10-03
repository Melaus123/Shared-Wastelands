#pragma once
/* mig1 (RE_Kenshi migration stage 1, owner decisions S2-60/83/84, 2026-09-28): OUR OWN hook call.

   coop::AddHook installs through a private copy of MinHook's "multihook" variant (third_party/minhook, upstream
   m417z/minhook branch multihook commit 4f18d18, BSD-2, unmodified):
     - MinHook is initialised once, on the first AddHook call (thread-safe; "already initialised" is fine);
     - every hook gets its own identity from a counter that starts at 1;
     - MH_CreateHookEx, then MH_EnableHookEx; either failing returns FAIL, success returns SUCCESS.
   Another MinHook copy may be in the process (another mod loader's). Two copies stacking on one
   function is the normal case: the later one relocates the earlier one's jump.

   On a failure ONE line is logged: "[HOOK] AddHook FAILED at create|enable: MH status <name>(<n>) target rva 0x..".
   startPlugin logs the totals once, after the last install (HookCounts). Every hook is installed during the
   preload - there are no run-time installs - so the count is the total. The address-refusal early return
   logs the same line before it returns. */

namespace coop
{
    enum HookStatus
    {
        SUCCESS,
        FAIL
    };

    HookStatus AddHook(void* target, void* detour, void** original);

    /* The installs that succeeded and failed since the DLL loaded (any thread, any time). */
    void HookCounts(long* installed, long* failed);
}
