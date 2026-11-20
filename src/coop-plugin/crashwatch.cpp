/* crashwatch.cpp - T-1374 (owner 1688, 2026-11-15): "Build a crash line (what kind of crash, and which file and spot crashed)
 * into the mod's log, written when the game's crash handling starts."
 *
 * Kenshi's crash handling ends in exit(), so a crash reaches the mod's exit hook and Windows records nothing (F1036): until now no
 * log said which module or address crashed. This file writes ONE [CRASH] line per process (src/common/crashline.h) on three roads:
 *
 *   main-loop  WinMain's catch-all SEH block guards the main-loop call; its filter (1.0.65 0x1372BB0 / 1.0.68 0x1373AB0, row
 *              KenshiCrashFilter; rcx = the EXCEPTION_POINTERS, rdx = WinMain's frame) writes Kenshi's crashDump .dmp and returns
 *              'handle it'; the block then zips it with Kenshi's logs into its save root, shows 'Kenshi has crashed' and exits.
 *              The detour writes the line FIRST, then runs the original with both arguments unchanged and returns its answer.
 *   worker     SetUnhandledExceptionFilter: an exception nothing handles on another thread (or on the main thread outside the
 *              guarded call) reaches ours; it writes the line, then runs the filter it replaced and returns that one's answer
 *              (the C runtime's: a C++ exception -> terminate -> abort; anything else -> Windows Error Reporting). Set at the start
 *              and again at every world load; when the filter found there is not ours (and not the C runtime's), the module that
 *              replaced it is logged and ours is chained in front of it.
 *   abort      signal(SIGABRT) in msvcr100 - the game and this DLL share that one runtime (both import MSVCR100.dll; the DLL is
 *              built /MD) - writes the line, then runs the SIGABRT handler it replaced when there was one (not the default
 *              or "ignore"), and abort carries on as before.
 *   NOT COVERED: a buffer-overrun failure (status 0xC0000409, the /GS check and __fastfail) ends the process without any filter,
 *   handler or signal running - no line.
 *   A STACK OVERFLOW (0xC00000FD) reaches the filters on the crashed thread's own stack with only a few KB left, and the full
 *   line needs about 5 KB (the CONTEXT copy, the record, the frames, the line). So for that code, or whenever under kStackNeed
 *   (16 KB) of stack is left below the handler, the SHORT line is written instead (CrashShort: no walk, no copy, about 1 KB:
 *   "stack: not walked (..)"); the full path's buffers sit in functions of their own (never inlined), so the short path never
 *   reserves them. A fault inside either is caught (no line) rather than recursing.
 *
 * THE HANDLER'S RULES: stack buffers only (the heap may be what crashed); no C++ object beside __try (C2712); no C runtime
 * call; the whole handler inside __try, so a fault in it cannot recurse; claimed once per process (the first road to arrive
 * writes; a worker's C++ exception that goes on to abort writes once). The stack is walked from the exception's own CONTEXT
 * (RtlLookupFunctionEntry / RtlVirtualUnwind), so it is the crashed thread's stack, not the handler's; a crash at address 0
 * (a call through a null pointer) is shown as ?+0x0 and the walk goes on from its caller. The line carries the crash's moment
 * on Windows' clock (utc=, GetSystemTimeAsFileTime), which pairs Kenshi's crash file with this crash at the next start.
 *
 * C++03 (VS2010 v100).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "crashwatch.h"
#include "coop_log.h"
#include "hooks.h"
#include "addresses.h"
#include "../common/crashline.h"
#include "../common/modversion.h"

namespace coop { long StoreWorldGenNow(); }   /* store.cpp (store.h): this game's world-load generation */

#ifndef UNW_FLAG_NHANDLER
#define UNW_FLAG_NHANDLER 0x0   /* winnt.h's value: unwind only, no handler wanted (the VS2010 SDK's headers lack the name here) */
#endif

namespace {

unsigned long long kKenshiCrashFilterRva = 0; coop::AddrReg kKenshiCrashFilterRva_reg("KenshiCrashFilter", &kKenshiCrashFilterRva);   /* WinMain's SEH filter funclet: Steam_1.0.65 0x1372BB0 / Steam_1.0.68 0x1373AB0 (F1036) */
unsigned long long kKenshiSaveRootRva = 0; coop::AddrReg kKenshiSaveRootRva_reg("KenshiSaveRootText", &kKenshiSaveRootRva);   /* a std::string VARIABLE: Kenshi's save root (the crash zip's folder; + "\save\" is the save folder). Steam_1.0.65 0x1F9EE38 / Steam_1.0.68 0x1F9FE48 - set in GameWorld's start-up (1.0.65 0x86C239 / 1.0.68 0x86D109), read by the crash zip (0x743E0E / 0x74493E) */

volatile LONG g_claimed = 0;      /* 1 once a road took the one line */
volatile LONG g_written = 0;      /* 1 once the line was handed to the log */
volatile LONG g_installed = 0;
volatile LONG g_mainTid = 0;
char g_version[swcrash::kVersionMax];
char g_ownModule[swcrash::kModuleMax];
LPTOP_LEVEL_EXCEPTION_FILTER volatile g_prevFilter = 0;   /* run after ours */
LPTOP_LEVEL_EXCEPTION_FILTER volatile g_basePrev = 0;     /* the filter ours replaced at the start */
volatile LONG g_filterTid = 0;    /* the thread inside ours when there is no TLS slot (a chained filter calling ours back is sent to g_basePrev) */
DWORD g_filterSlot = TLS_OUT_OF_INDEXES;   /* T-1374 fold (N6): per thread, non-zero while that thread is inside ours */
void (__cdecl* volatile g_prevAbort)(int) = 0;   /* T-1374 fold (N7): the SIGABRT handler ours replaced, run after ours; 0 = none */
typedef VOID (WINAPI* StackLimitsFn)(PULONG_PTR, PULONG_PTR);
StackLimitsFn g_stackLimits = 0;   /* GetCurrentThreadStackLimits (Windows 8 and later), looked up at the install */
long g_lastGen = 0;
int g_replacedSaid = 0;
const unsigned long long kStackNeed = 16 * 1024;   /* under this much stack left: the short line */

/* ---- the handler: no C++ objects from here to CrashCore ---- */
void PlaceOf(swcrash::Place* p, unsigned long long addr)
{
    HMODULE m = 0;
    char name[swcrash::kModuleMax];
    name[0] = 0;
    if (addr != 0 && ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                          (LPCWSTR)(ULONG_PTR)addr, &m) && m != 0)
    {
        wchar_t path[MAX_PATH];
        const DWORD n = ::GetModuleFileNameW(m, path, MAX_PATH);
        if (n != 0 && n < MAX_PATH) { path[n] = 0; swcrash::FileNameOnly(path, name, sizeof(name)); }
        if (name[0] == 0) { name[0] = '#'; name[1] = 0; }   /* a module whose name could not be read */
    }
    else m = 0;
    swcrash::SetPlace(p, name, addr, (unsigned long long)(ULONG_PTR)m);
}

/* walks `c` (changed) outward; *count = the frames written so far, kept if a step faults */
void WalkFrames(CONTEXT* c, swcrash::Place* out, int max, volatile int* count)
{
    for (int i = 0; i < 64 && *count < max; ++i)
    {
        const DWORD64 pc = c->Rip;
        if (pc == 0)
        {   /* T-1374 fold (N5): at the crash itself, a call through a null pointer - the call pushed its return address and went
               to 0. Shown as ?+0x0, then that return address is popped as a leaf's, so the caller is shown. Later: the end. */
            if (i != 0 || c->Rsp == 0) break;
            PlaceOf(&out[*count], 0);
            *count = *count + 1;
            c->Rip = *(const DWORD64*)c->Rsp;
            c->Rsp += 8;
            continue;
        }
        PlaceOf(&out[*count], pc);
        *count = *count + 1;
        DWORD64 base = 0;
        PRUNTIME_FUNCTION f = ::RtlLookupFunctionEntry(pc, &base, NULL);
        if (f == 0)
        {   /* a leaf function: its return address is on top of the stack */
            if (c->Rsp == 0) break;
            c->Rip = *(const DWORD64*)c->Rsp;
            c->Rsp += 8;
        }
        else
        {
            PVOID handlerData = 0;
            DWORD64 establisher = 0;
            ::RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, pc, f, c, &handlerData, &establisher, NULL);
        }
    }
}
int WalkGuarded(CONTEXT* c, swcrash::Place* out, int max)
{
    volatile int count = 0;
    __try { WalkFrames(c, out, max, &count); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return count;
}

/* a C++ exception's thrown type, from its throw information (x64: [2] ThrowInfo, [3] the throwing module's base; image-relative
   offsets: ThrowInfo+0xC -> CatchableTypeArray, its first entry +4 -> CatchableType, +4 -> TypeDescriptor, its name at +0x10) */
void ThrownTypeGuarded(const EXCEPTION_RECORD* er, char* out, size_t n)
{
    out[0] = 0;
    __try
    {
        if (er->NumberParameters < 4) return;
        const ULONG_PTR magic = er->ExceptionInformation[0];
        if (magic != 0x19930520 && magic != 0x19930521 && magic != 0x19930522) return;
        const unsigned char* ti = (const unsigned char*)er->ExceptionInformation[2];
        const ULONG_PTR img = er->ExceptionInformation[3];
        if (ti == 0 || img == 0) return;
        const unsigned char* cta = (const unsigned char*)(img + (ULONG_PTR)(unsigned int)*(const int*)(ti + 0xC));
        if (*(const int*)cta < 1) return;
        const unsigned char* ct = (const unsigned char*)(img + (ULONG_PTR)(unsigned int)*(const int*)(cta + 4));
        const char* name = (const char*)(img + (ULONG_PTR)(unsigned int)*(const int*)(ct + 4) + 0x10);
        char deco[256];   /* a deeper name than this is cut and copied raw (no closing "@@") */
        size_t k = 0;
        for (; k + 1 < sizeof(deco) && name[k] != 0; ++k) deco[k] = name[k];
        deco[k] = 0;
        swcrash::TypeNameText(deco, out, n);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
}

/* now on Windows' clock: seconds since 1601-01-01 UTC (a FILETIME / 10^7, the clock of a file's write time) */
unsigned long long UtcNowSec()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    return ((((unsigned long long)ft.dwHighDateTime) << 32) | ft.dwLowDateTime) / 10000000ULL;
}
/* the bytes of stack left below this point on this thread: down to the reserve's floor (GetCurrentThreadStackLimits), or on an
   older Windows to the committed floor (NT_TIB StackLimit - less than is truly left, so the short line may be chosen with
   more stack than it needs; never the other way) */
__declspec(noinline) unsigned long long StackLeft()
{
    volatile char here = 0;
    ULONG_PTR low = 0, high = 0;
    const StackLimitsFn f = g_stackLimits;
    if (f != 0) f(&low, &high);
    if (low == 0) low = (ULONG_PTR)((NT_TIB*)::NtCurrentTeb())->StackLimit;
    const ULONG_PTR at = (ULONG_PTR)&here;
    return at > low ? (unsigned long long)(at - low) : 0;
}

/* the line: once per process. `ctx` is a copy (the walk changes it), 0 = no stack */
void CrashCore(int road, const EXCEPTION_RECORD* er, CONTEXT* ctx)
{
    if (::InterlockedCompareExchange(&g_claimed, 1, 0) != 0) return;
    swcrash::Record r;
    memset(&r, 0, sizeof(r));
    r.road = road;
    r.avKind = -1;
    r.thread = ::GetCurrentThreadId();
    r.mainThread = g_mainTid == 0 ? -1 : (r.thread == (unsigned long)g_mainTid ? 1 : 0);
    r.utcSec = UtcNowSec();
    memcpy(r.version, g_version, sizeof(r.version));
    r.version[sizeof(r.version) - 1] = 0;
    if (er != 0)
    {
        r.code = er->ExceptionCode;
        if ((r.code == swcrash::kCodeAccessViolation || r.code == 0xC0000006UL) && er->NumberParameters >= 2)
        {
            r.avKind = (int)er->ExceptionInformation[0];
            r.avAddress = er->ExceptionInformation[1];
        }
        if (r.code == swcrash::kCodeCppThrow) ThrownTypeGuarded(er, r.typeName, sizeof(r.typeName));
        PlaceOf(&r.at, (unsigned long long)(ULONG_PTR)er->ExceptionAddress);
    }
    swcrash::Place f[swcrash::kMaxFrames + 6];
    const int got = ctx != 0 ? WalkGuarded(ctx, f, swcrash::kMaxFrames + 6) : 0;
    int from = 0;
    if (road == swcrash::kRoadAbort)
    {   /* the walk starts in our signal handler: the frames shown start past it, and `at` is the first frame past the C
           runtime's raise/abort - abort's caller */
        from = swcrash::FirstOutside(f, got, 0, g_ownModule);
        const int a = swcrash::FirstOutside(f, got, from, "msvcr100.dll");
        if (a < got) r.at = f[a];
        else if (from < got) r.at = f[from];
    }
    for (int i = from; i < got && r.frames < swcrash::kMaxFrames; ++i) r.frame[r.frames++] = f[i];
    char line[kCrashLinePrefixGap + swcrash::kLineMax + 2];
    const size_t len = swcrash::FormatLine(r, line + kCrashLinePrefixGap, swcrash::kLineMax);
    CrashLogWriteRaw(line, sizeof(line), len);
    ::InterlockedExchange(&g_written, 1);
}
/* THE FULL LINE's own frames (never inlined, so their buffers are not reserved on the short line's road) */
__declspec(noinline) void CrashFull(int road, EXCEPTION_POINTERS* ep)
{
    CONTEXT c;
    if (ep != 0 && ep->ContextRecord != 0) { c = *ep->ContextRecord; CrashCore(road, ep->ExceptionRecord, &c); }
    else CrashCore(road, ep != 0 ? ep->ExceptionRecord : 0, 0);
}
__declspec(noinline) void CrashFullAbort()
{
    CONTEXT c;
    ::RtlCaptureContext(&c);
    CrashCore(swcrash::kRoadAbort, 0, &c);
}
/* THE SHORT LINE (a stack overflow, or under kStackNeed left): no walk, no CONTEXT copy, about 1 KB of stack in all */
__declspec(noinline) void CrashShort(int road, const EXCEPTION_RECORD* er, int why)
{
    if (::InterlockedCompareExchange(&g_claimed, 1, 0) != 0) return;
    const unsigned long thread = ::GetCurrentThreadId();
    const int mainThread = g_mainTid == 0 ? -1 : (thread == (unsigned long)g_mainTid ? 1 : 0);
    unsigned long code = 0;
    int avKind = -1;
    unsigned long long avAddress = 0;
    swcrash::Place at;
    swcrash::SetPlace(&at, "", 0, 0);
    if (er != 0)
    {
        code = er->ExceptionCode;
        if ((code == swcrash::kCodeAccessViolation || code == 0xC0000006UL) && er->NumberParameters >= 2)
        {
            avKind = (int)er->ExceptionInformation[0];
            avAddress = er->ExceptionInformation[1];
        }
        PlaceOf(&at, (unsigned long long)(ULONG_PTR)er->ExceptionAddress);
    }
    char line[kCrashLinePrefixGap + 448 + 2];
    const size_t len = swcrash::FormatShortLine(road, code, avKind, avAddress, at, thread, mainThread, UtcNowSec(), why, g_version,
                                                line + kCrashLinePrefixGap, 448);
    CrashLogWriteRaw(line, sizeof(line), len);
    ::InterlockedExchange(&g_written, 1);
}
void CrashGuarded(int road, EXCEPTION_POINTERS* ep)
{
    __try
    {
        const EXCEPTION_RECORD* er = ep != 0 ? ep->ExceptionRecord : 0;
        if (er != 0 && er->ExceptionCode == swcrash::kCodeStackOverflow) CrashShort(road, er, swcrash::kShortStackOverflow);
        else if (StackLeft() < kStackNeed) CrashShort(road, er, swcrash::kShortLittleStack);
        else CrashFull(road, ep);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

/* A: Kenshi's main-loop crash filter (x64: one calling convention; rdx is WinMain's frame, passed on unchanged) */
typedef LONG (*KenshiCrashFilterFn)(EXCEPTION_POINTERS*, void*);
KenshiCrashFilterFn orig_kenshiCrashFilter = 0;
LONG detour_kenshiCrashFilter(EXCEPTION_POINTERS* ep, void* frame)
{
    CrashGuarded(swcrash::kRoadMainLoop, ep);
    return orig_kenshiCrashFilter(ep, frame);
}

/* B: the process's unhandled-exception filter */
LONG WINAPI CrashTopFilter(EXCEPTION_POINTERS* ep)
{
    /* T-1374 fold (N6): "this thread is inside ours" is kept per thread (a TLS slot taken at the install), so two threads that
       crash at once each get the re-entry check; without a slot, one thread id for the process (the second thread then skips it) */
    const DWORD slot = g_filterSlot;
    const LONG me = (LONG)::GetCurrentThreadId();
    LONG was = 0;
    bool inside = false;
    if (slot != TLS_OUT_OF_INDEXES) inside = ::TlsGetValue(slot) != 0;
    else { was = ::InterlockedCompareExchange(&g_filterTid, me, 0); inside = was == me; }
    if (inside)
    {   /* a filter ours ran has called ours again (it chains to what it replaced, which was ours): go to the one ours replaced */
        LPTOP_LEVEL_EXCEPTION_FILTER b = g_basePrev;
        return (b != 0 && b != &CrashTopFilter) ? b(ep) : EXCEPTION_CONTINUE_SEARCH;
    }
    if (slot != TLS_OUT_OF_INDEXES) ::TlsSetValue(slot, (LPVOID)1);
    CrashGuarded(swcrash::kRoadWorker, ep);
    LPTOP_LEVEL_EXCEPTION_FILTER prev = g_prevFilter;
    const LONG answer = (prev != 0 && prev != &CrashTopFilter) ? prev(ep) : EXCEPTION_CONTINUE_SEARCH;
    if (slot != TLS_OUT_OF_INDEXES) ::TlsSetValue(slot, 0);
    else if (was == 0) ::InterlockedExchange(&g_filterTid, 0);
    return answer;
}

/* C: abort - our line, then (T-1374 fold N7) the SIGABRT handler ours replaced, when there was one */
void __cdecl CrashAbortSignal(int sig)
{
    __try
    {
        if (StackLeft() < kStackNeed) CrashShort(swcrash::kRoadAbort, 0, swcrash::kShortLittleStack);
        else CrashFullAbort();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    void (__cdecl* const prev)(int) = g_prevAbort;
    if (prev != 0 && prev != &CrashAbortSignal) prev(sig);
}

/* ---- outside the handler ---- */
std::string PlaceText(const void* p)
{
    if (p == 0) return "none";
    swcrash::Place pl;
    PlaceOf(&pl, (unsigned long long)(ULONG_PTR)p);
    char b[96];
    size_t len = 0;
    b[0] = 0;
    swcrash::PutPlace(b, sizeof(b), &len, pl);
    return b;
}
/* the C runtime's own filter: in msvcr100 or in the game executable (its start-up code sets it) */
bool RuntimeFilter(const void* p)
{
    if (p == 0) return true;
    HMODULE m = 0;
    if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)p, &m)) return false;
    return m == ::GetModuleHandleW(NULL) || m == ::GetModuleHandleW(L"msvcr100.dll");
}

#ifdef SW_DEV_BUILD
enum { kTestNone = 0, kTestAvMain = 1, kTestThrowMain = 2, kTestPurecall = 3, kTestAbort = 4 };
volatile LONG g_testArmed = 0;
}   /* namespace */
/* the thrown test type, in a NAMED namespace: its line reads type=swcrashtest::SwCrashTestThrow (in the unnamed one it would read
   type=`anonymous namespace'::SwCrashTestThrow) */
namespace swcrashtest { struct SwCrashTestThrow { int kind; }; }
namespace {
using swcrashtest::SwCrashTestThrow;
struct SwPureBase
{
    SwPureBase();
    virtual ~SwPureBase() {}
    virtual void Hit() = 0;
};
SwPureBase::SwPureBase()
{
    SwPureBase* volatile self = this;   /* read back through a volatile: the call cannot be resolved at compile time */
    self->Hit();                         /* in the base's constructor the slot is the runtime's _purecall */
}
struct SwPureDerived : SwPureBase { void Hit() {} };
DWORD WINAPI CrashTestThread(LPVOID p)
{
    if ((ULONG_PTR)p == 1)
    {
        volatile int* volatile z = 0;
        *z = 1;
    }
    else
    {
        SwCrashTestThrow t;
        t.kind = 2;
        throw t;
    }
    return 0;
}
void CrashTestMainStep()
{
    const LONG k = ::InterlockedExchange(&g_testArmed, kTestNone);
    if (k == kTestAvMain)
    {
        volatile int* volatile z = 0;
        *z = 1;
    }
    else if (k == kTestThrowMain)
    {
        SwCrashTestThrow t;
        t.kind = 1;
        throw t;
    }
    else if (k == kTestPurecall)
    {
        SwPureDerived d;
        d.Hit();
    }
    else if (k == kTestAbort) ::abort();
}
#endif

}   /* namespace */

namespace coop {

void InstallCrashWatch()
{
    if (::InterlockedExchange(&g_installed, 1) != 0) return;
    ::InterlockedExchange(&g_mainTid, (LONG)::GetCurrentThreadId());
    g_filterSlot = ::TlsAlloc();   /* before the filter is set: it reads the slot */
    {
        HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll");
        if (k32 != 0) g_stackLimits = (StackLimitsFn)::GetProcAddress(k32, "GetCurrentThreadStackLimits");
    }
    {
        const std::string table = AddrTableName(), fp = AddrExeFingerprint();
        swcrash::VersionText(modversion::kName, table.c_str(), fp.c_str(), g_version, sizeof(g_version));
    }
    {
        swcrash::Place me;
        PlaceOf(&me, (unsigned long long)(ULONG_PTR)&InstallCrashWatch);
        size_t len = 0;
        swcrash::Put(g_ownModule, sizeof(g_ownModule), &len, me.known ? me.module : "");
    }
    /* A */
    std::string a;
    const unsigned long long at = AddrAbs(kKenshiCrashFilterRva);
    if (kKenshiCrashFilterRva == 0 || at == 0) a = "NOT installed (row KenshiCrashFilter not filled)";
    else
    {
        const HookStatus h = AddHook((void*)at, (void*)&detour_kenshiCrashFilter, (void**)&orig_kenshiCrashFilter);
        a = (h == SUCCESS && orig_kenshiCrashFilter != 0) ? "installed" : "NOT installed (AddHook failed)";
    }
    char rva[32];
    _snprintf_s(rva, sizeof(rva), _TRUNCATE, "0x%llX", kKenshiCrashFilterRva);
    /* B */
    LPTOP_LEVEL_EXCEPTION_FILTER prev = ::SetUnhandledExceptionFilter(&CrashTopFilter);
    if (prev == &CrashTopFilter) prev = 0;
    g_basePrev = prev;
    g_prevFilter = prev;
    g_lastGen = StoreWorldGenNow();
    /* C */
    void (__cdecl* prevSig)(int) = ::signal(SIGABRT, &CrashAbortSignal);
    const bool sigChained = prevSig != SIG_ERR && prevSig != SIG_DFL && prevSig != SIG_IGN && prevSig != 0 && prevSig != &CrashAbortSignal;
    if (sigChained) g_prevAbort = prevSig;
    DebugLog(std::string("[CRASHWATCH] the crash line is armed: main-loop filter ") + a + " at rva " + rva
             + "; unhandled-exception filter set (it runs the one it replaced: " + PlaceText((const void*)prev) + ")"
             + "; SIGABRT handler " + (prevSig == SIG_ERR ? "NOT set" : "set")
             + (sigChained ? " (it runs the one it replaced: " + PlaceText((const void*)prevSig) + ")" : "")
             + (g_filterSlot == TLS_OUT_OF_INDEXES ? "; no TLS slot - the filter's re-entry check is one for the process" : "")
             + "; Kenshi's save root " + (KenshiSaveRoot().empty() ? "not known" : "known")
             + ". A buffer-overrun failure (0xC0000409) bypasses all three.");
}

void CrashWatchTick()
{
    ::InterlockedExchange(&g_mainTid, (LONG)::GetCurrentThreadId());
#ifdef SW_DEV_BUILD
    CrashTestMainStep();   /* TEST-ONLY: faults here, in the game's own main-loop call, outside any __try of the mod */
#endif
    if (g_installed == 0) return;
    const long gen = StoreWorldGenNow();
    if (gen == g_lastGen) return;
    g_lastGen = gen;
    {   /* T-1374 fold (N8): the save-root look the start-up left for later (the save root was not known yet) - once, now */
        const std::string ck = CrashKeepSaveRootLook(KenshiSaveRoot(), true);
        if (!ck.empty()) DebugLog(ck);
    }
    /* a world load: ours again, in front of whatever is there now */
    LPTOP_LEVEL_EXCEPTION_FILTER prev = ::SetUnhandledExceptionFilter(&CrashTopFilter);
    if (prev == &CrashTopFilter) return;
    g_prevFilter = prev;
    if (RuntimeFilter((const void*)prev)) return;
    if (g_replacedSaid >= 8) return;
    ++g_replacedSaid;
    DebugLog("[CRASHWATCH] at a world load the unhandled-exception filter was not ours: " + PlaceText((const void*)prev)
             + " had replaced it - ours is set again and runs that one after itself");
}

int CrashLineWritten() { return g_written != 0 ? 1 : 0; }

}   /* namespace coop */

namespace {
/* 1 = read, 0 = empty or too long, -1 = faulted. The game's std::string (msvcr100 / v100 layout): 16 bytes of text or a pointer
   to it (capacity >= 16), the size at +0x10, the capacity at +0x18. */
int SaveRootTextPod(const unsigned char* g, char* out, size_t n)
{
    __try
    {
        const unsigned long long size = *(const unsigned long long*)(g + 0x10), cap = *(const unsigned long long*)(g + 0x18);
        if (size == 0 || size >= n || cap < size) return 0;
        const char* p = cap >= 16 ? *(const char* const*)g : (const char*)g;
        memcpy(out, p, (size_t)size);
        out[size] = 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
}

namespace coop {
std::wstring KenshiSaveRoot()
{
    const unsigned long long a = AddrAbs(kKenshiSaveRootRva);
    if (kKenshiSaveRootRva == 0 || a == 0) return std::wstring();
    char b[1024];
    if (SaveRootTextPod((const unsigned char*)(ULONG_PTR)a, b, sizeof(b)) != 1) return std::wstring();
    wchar_t w[1024];
    const int n = ::MultiByteToWideChar(CP_ACP, 0, b, -1, w, 1024);
    if (n <= 1) return std::wstring();
    return std::wstring(w);
}

#ifdef SW_DEV_BUILD
std::string CrashTestCommand(const std::string& kind)
{
    LONG main = kTestNone;
    if (kind == "av-main") main = kTestAvMain;
    else if (kind == "throw-main") main = kTestThrowMain;
    else if (kind == "purecall") main = kTestPurecall;
    else if (kind == "abort") main = kTestAbort;
    if (main != kTestNone)
    {
        ::InterlockedExchange(&g_testArmed, main);
        DebugLog("[CRASHTEST] armed: " + kind + " - the next main-loop frame faults (TEST-ONLY)");
        return "ok crashtest " + kind;
    }
    if (kind == "av-worker" || kind == "throw-worker")
    {
        DebugLog("[CRASHTEST] " + kind + ": a new thread faults now (TEST-ONLY)");
        HANDLE t = ::CreateThread(NULL, 0, &CrashTestThread, (LPVOID)(ULONG_PTR)(kind == "av-worker" ? 1 : 2), 0, NULL);
        if (t == NULL) return "error crashtest: the thread could not be made";
        ::CloseHandle(t);
        return "ok crashtest " + kind;
    }
    DebugLog("[CRASHTEST] refused: usage crashtest av-main | av-worker | throw-main | throw-worker | purecall | abort");
    return "error crashtest: usage crashtest av-main | av-worker | throw-main | throw-worker | purecall | abort";
}
#endif
}   /* namespace coop */
