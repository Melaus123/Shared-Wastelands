/* addresses.cpp - P8h. See addresses.h.
 *
 * The pure half of this (the fingerprint arithmetic and the table parser) lives in src/common/addrtable.cpp
 * and is compiled into the offline test exe as well, so the format has ONE definition (6a lesson 11).
 * What is here is the part that needs Windows and the loaded image: find the executable, read its first
 * 64 KB, open the table, and compare bytes in memory.
 *
 * EVERY MEMORY READ IS BEHIND __try. A table naming an address outside the image would otherwise fault
 * inside the mod's start, which runs inside the game's own start-up (Ogre's Root::initialise), and take the game down. A read
 * that raises is a MISMATCH, never a pass: "I could not read it" must never be able to masquerade as "it
 * matched" (store.cpp's ReadPrologue rule, E30-3).
 *
 * NO C++ OBJECT MAY LIVE IN A FRAME CONTAINING __try (C2712), so every guarded read is its own tiny function.
 *
 * C++03 (VS2010 v100).
 */
#include "addresses.h"
#include "command_channel.h"   /* PathNextToDll - the same directory the command channel and the config use */
#include "../common/addrtable.h"
#include "../common/addrhooked.h"  /* rk1: a call-only row another mod has hooked - accepted or refused */
#include "../common/sigscan.h"    /* owner 97a: TitleScreen::update by its bytes when there is no usable table */
#include "../common/sigtable.h"   /* T-63 stage 7: every row by its signature when no table matches this executable */
#include "../common/storemeta.h"   /* coopstore::Crc32 - already offline-tested against the zip/png vectors */
#include "../common/reportroad.h"   /* T-1457: the report road of a switched-off game - its rows and their verdict */

#include "coop_log.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "u8file.h"   /* UTF-8 paths through the wide Windows file calls - the executable and the address tables */

#include <cstdio>
#include <cstring>
#include <fstream>
#include <exception>
#include <locale>
#include <new>
#include <sstream>
#include <string>
#include <vector>

using namespace coopaddr;

namespace {

/* ---- the registry -------------------------------------------------------------------------------------
   Zero-initialised POD plus a plain int. Both are zero before ANY static constructor runs, so AddrReg's
   constructor is safe whatever order the linker picks (the static-initialisation-order problem does not
   apply to zero-initialised storage). */
const int kMaxRegs = kAddrMaxRegs;
typedef AddrSlot RegSlot;   /* gog1 fold 2: addrtable.h's POD, so AddrSettleSlots binds and clears these very slots */
RegSlot g_regs[kMaxRegs];
unsigned char g_regUse[kMaxRegs];   /* rk1: beside g_regs[i] - 1 = kAddrUseCallOnly, 0 = strict (zero-initialised: strict) */
int     g_regCount = 0;
int     g_regOverflow = 0;

/* ---- state --------------------------------------------------------------------------------------------- */
volatile LONG g_ok      = 0;    /* 1 only after a clean AddrInit */
volatile LONG g_ran     = 0;
AddrEntry     g_entries[kAddrMaxEntries];
int           g_entryCount = 0;

long long g_tableLoaded = 0;      /* addrTableLoaded    - 1 when a table for this fingerprint was read */
long long g_checked     = 0;      /* addrEntriesChecked - entries whose bytes were actually compared */
long long g_mismatch    = 0;      /* addrMismatch       - entries whose bytes did NOT match */
long long g_varEntries  = 0;      /* addrVarEntries     - writable globals: present, NOT verifiable */
long long g_unreadable  = 0;      /* addrUnreadable     - the read itself raised; counted INSIDE addrMismatch */
long long g_hookedFirst = 0;      /* addrLooksHooked    - a mismatch whose first byte is a jump: someone else's hook */
long long g_hookedAccepted = 0;   /* addrHookedAccepted - rk1: a call-only row hooked by another mod, the rest matching:
                                     accepted, NOT in addrMismatch nor addrLooksHooked (src/common/addrhooked.h) */
long long g_hookedChained = 0;    /* addrHookedChained  - T-1447: a row this mod does more than call, at a game function's start,
                                     another mod's jump on it, the rest matching: accepted - our hook goes on top (not in
                                     addrHookedAccepted, addrMismatch nor addrLooksHooked) */
/* T-1447: the rows accepted chained - where, the bytes the gate accepted (for AddrChainedCheck) and the module named. One
   per such row, sized at the table load (written only inside AddrInit, before any hook exists). */
struct ChainedRow { unsigned long long abs; const char* name; int nbytes; unsigned char got[kAddrBytesMax]; std::string module; };
std::vector<ChainedRow> g_chained;
long long g_bindMissing = 0;      /* addrBindMissing    - a registered name the table does not carry */
long long g_optMissed   = 0;      /* addrOptionalMissed - T-1453 / T-294: optional rows not found or failing their check; each
                                     one's feature is off (its slots 0), the mod runs; NOT in addrMismatch */
std::string g_optMissedNames;     /* "<row> (<feature>)", comma-separated - the start-up summary names them */
long long g_badLines    = 0;
std::string g_firstMismatch;      /* the FIRST mismatching entry, named - a refusal that names nothing is not a measurement */
std::string g_fingerprint;
std::string g_version;
std::string g_tablePath;
std::string g_why;                /* the one sentence the log line and the caption both come from */
int         g_reportRan = 0;      /* T-1457: AddrResolveReportRows ran (once, on the refused road) */
int         g_reportOk  = 0;      /* T-1457: 1 = the report road's rows found and checked - REPORT A BUG may start */
int         g_testLoadOk = 0;     /* T-1457 TEST-ONLY: the switched-off test build's `load` rows found and checked */
char        g_caption[96];

std::string N(long long v) { std::ostringstream o; o.imbue(std::locale::classic()); o << v; return o.str(); }
std::string H(unsigned long long v)
{
    char b[32];
    std::sprintf(b, "0x%llX", v);   /* sprintf, never a stream: F030's locale inserts separators into numbers */
    return std::string(b);
}
std::string Hex(const unsigned char* p, int n)
{
    std::string s;
    char b[4];
    int i;
    for (i = 0; i < n; ++i) { std::sprintf(b, "%02X", p[i]); s += b; }
    return s;
}

uintptr_t Base() { return (uintptr_t)::GetModuleHandleA(0); }

/* ---- guarded reads (no C++ object in these frames) ----------------------------------------------------- */
int ReadBytesPod(const void* p, unsigned char* out, unsigned int n)
{
    __try { std::memcpy(out, p, n); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int ReadPtrPod(const void* p, unsigned long long* out)
{
    __try { *out = *(const unsigned long long*)p; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* ---- the executable's own path and first 64 KB ---------------------------------------------------------- */
int ExePath(std::string* out)
{
    /* through the wide call, as UTF-8 (ReadHead and the signature search open it through the wide name): a game folder holding
       letters outside the ANSI code page is named exactly. 32768 UTF-16 units is the longest path Windows has. */
    std::wstring buf(32768, L'\0');
    const DWORD n = ::GetModuleFileNameW(NULL, &buf[0], (DWORD)buf.size());
    if (n == 0 || n >= (DWORD)buf.size()) return 0;
    buf.resize((size_t)n);
    *out = U8FromW(buf.c_str());
    return 1;
}

/* THE FILE ON DISK, not the loaded image. The loaded image's first 64 KB contains .text bytes that another
   mod's hooks may already have rewritten, which would make the fingerprint depend on WHO LOADED FIRST; the
   file cannot change under us and is what the table's author fingerprinted offline. */
int ReadHead(const std::string& path, std::vector<unsigned char>* head, unsigned long long* fileSize)
{
    std::ifstream f(U8W(path.c_str()).c_str(), std::ios::binary);   /* the path is UTF-8 (ExePath): opened through the wide name */
    if (!f) return 0;
    f.seekg(0, std::ios::end);
    const std::ifstream::pos_type endPos = f.tellg();
    if (endPos <= 0) return 0;
    *fileSize = (unsigned long long)endPos;
    f.seekg(0, std::ios::beg);
    head->resize(65536);
    f.read((char*)&(*head)[0], 65536);
    const std::streamsize got = f.gcount();
    if (got < 0x400) return 0;
    head->resize((size_t)got);
    return 1;
}

int ReadWholeFile(const std::string& path, std::string* out)
{
    std::ifstream f(U8W(path.c_str()).c_str(), std::ios::binary);   /* the path is UTF-8 (PathNextToDll): opened through the wide name */
    if (!f) return 0;
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    /* A table larger than a megabyte is not a table. Refuse it rather than parse it. */
    if (out->size() > 1048576u) { out->clear(); return 0; }
    return 1;
}

/* rk1: 1 when EVERY registration of `name` is kAddrUseCallOnly - and there is at least one, and none was lost to a
   registry overflow (an unregistered row may still be read through Rva(name), whose use nobody declared). T-1447: 2 when
   any one is not call-only (accepted only at a game function's start, AddrForeignVerdict). 0 otherwise. */
int RowUse(const char* name)
{
    unsigned char uses[kMaxRegs];
    int i, n = 0;
    if (name == 0 || g_regOverflow != 0) return 0;
    for (i = 0; i < g_regCount; ++i)
    {
        if (g_regs[i].name == 0 || std::strcmp(g_regs[i].name, name) != 0) continue;
        uses[n++] = g_regUse[i];
    }
    return AddrUsesCombined(uses, n);   /* review S2 / T-1447: the rule itself, offline-tested (src/common/addrhooked.h) */
}

/* T-1447: IS `rva` THE START OF A GAME FUNCTION? A binary search of the executable's own exception directory (.pdata,
   RUNTIME_FUNCTION entries sorted by BeginAddress - every non-leaf x64 function has one). 1 yes, 0 no or unreadable. */
int IsFunctionStartPod(uintptr_t base, unsigned long rva)
{
    __try
    {
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
        const IMAGE_NT_HEADERS64* nt = (const IMAGE_NT_HEADERS64*)(base + (uintptr_t)dos->e_lfanew);
        const IMAGE_DATA_DIRECTORY* d = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if (d->VirtualAddress == 0 || d->Size < sizeof(RUNTIME_FUNCTION)) return 0;
        const RUNTIME_FUNCTION* f = (const RUNTIME_FUNCTION*)(base + (uintptr_t)d->VirtualAddress);
        const unsigned long count = d->Size / (unsigned long)sizeof(RUNTIME_FUNCTION);
        unsigned long lo = 0, hi = count;
        while (lo < hi)
        {
            const unsigned long mid = lo + (hi - lo) / 2;
            if (f[mid].BeginAddress < rva) lo = mid + 1; else hi = mid;
        }
        return (lo < count && f[lo].BeginAddress == rva) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

unsigned long long ImageSizePod(uintptr_t base)
{
    __try
    {
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
        return (unsigned long long)((const IMAGE_NT_HEADERS64*)(base + (uintptr_t)dos->e_lfanew))->OptionalHeader.SizeOfImage;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* T-1447: where another mod's jump (`got`, read at `site`) goes, and 1 when that lies OUTSIDE the game's image (a DLL or
   memory that mod allocated) - E9 rel32, FF 25 rip-relative (the 8-byte pointer read), FF 25 00000000 + address. */
int ForeignJumpOutside(uintptr_t base, uintptr_t site, const unsigned char* got, int n, unsigned long long* dest)
{
    unsigned long long d = 0;
    *dest = 0;
    const int k = AddrJumpDest(got, n, (unsigned long long)site, &d);
    if (k == 0) return 0;
    if (k == 2 && !ReadPtrPod((const void*)(uintptr_t)d, &d)) return 0;
    *dest = d;
    const unsigned long long size = ImageSizePod(base);
    return (size != 0 && (d < (unsigned long long)base || d >= (unsigned long long)base + size)) ? 1 : 0;
}

/* T-1447: WHICH MODULE IS BEHIND ANOTHER MOD'S JUMP. GetModuleHandleEx on the address; the log takes the FILE NAME only (never
   a folder that may hold a user's name). "" when the address lies in no module (a relay page). */
std::string ModuleFileAt(unsigned long long addr)
{
    HMODULE m = 0;
    if (addr == 0 || !::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                           (LPCWSTR)(uintptr_t)addr, &m) || m == 0)
        return std::string();
    std::wstring buf(32768, L'\0');
    const DWORD n = ::GetModuleFileNameW(m, &buf[0], (DWORD)buf.size());
    if (n == 0 || n >= (DWORD)buf.size()) return std::string("(a module whose name could not be read)");
    buf.resize((size_t)n);
    const std::string full = U8FromW(buf.c_str());
    return std::string(AddrFileNamePart(full.c_str()));
}

/* T-1447: the module another mod's jump at `site` leads to: the E9's target when it lies in a module; else (a relay page,
   as multihook's slots below the game's image, hook.c:87-93) ONE more jump is followed - FF 25 00000000 + address (the
   14-byte form), FF 25 [rip+d], or mov rax / jmp rax - and THAT module is named. "unknown" when neither lands in one.
   *how gets the walk, for the log. Every read is guarded (ReadBytesPod / ReadPtrPod). */
std::string ForeignJumpModule(uintptr_t site, std::string* how)
{
    unsigned char b[14];
    unsigned long long d1 = 0, d2 = 0;
    const int k0 = ReadBytesPod((const void*)site, b, 14) ? AddrJumpDest(b, 14, (unsigned long long)site, &d1) : 0;
    if (k0 == 0 || (k0 == 2 && !ReadPtrPod((const void*)(uintptr_t)d1, &d1)))
    { *how = "the jump could not be read"; return "unknown"; }
    *how = "jump -> " + H(d1);
    std::string m = ModuleFileAt(d1);
    if (!m.empty()) return m;
    if (!ReadBytesPod((const void*)(uintptr_t)d1, b, 14)) { *how += " (in no module; its bytes could not be read)"; return "unknown"; }
    const int k = AddrJumpDest(b, 14, d1, &d2);
    if (k == 2)
    {
        unsigned long long p = 0;
        if (!ReadPtrPod((const void*)(uintptr_t)d2, &p)) { *how += " (in no module; its jump's pointer could not be read)"; return "unknown"; }
        d2 = p;
    }
    if (k == 0) { *how += " (in no module, and no jump there: " + Hex(b, 14) + ")"; return "unknown"; }
    *how += " (in no module: a relay) -> " + H(d2);
    m = ModuleFileAt(d2);
    return m.empty() ? std::string("unknown") : m;
}

/* ---- one entry, verified -------------------------------------------------------------------------------- */
/* Returns 1 matched, 0 mismatched, -1 not applicable (a writable global), 2 ACCEPTED HOOKED (rk1): a code row whose
   first instruction another mod replaced with its 5-byte jump, every later byte matching, and callOnly != 0 (every
   registration of the row only calls it - RowUse; on both roads since T-1476). Fills `detail` on a mismatch
   with what was EXPECTED and what was THERE - the inputs, not only the answer - and on an accepted row with both. */
int VerifyEntry(uintptr_t base, const AddrEntry& e, int callOnly /* T-1447: the row's use (RowUse): 0 none, 1 call-only, 2 any other */, std::string* detail)
{
    if (e.kind == kAddrVar) return -1;

    if (e.kind == kAddrPtr)
    {
        unsigned long long v = 0;
        if (!ReadPtrPod((const void*)(base + e.verifyAt), &v))
        {
            ++g_unreadable;
            *detail = "the pointer at rva " + H(e.verifyAt) + " could not be read at all";
            return 0;
        }
        if (v < (unsigned long long)base || (v - (unsigned long long)base) != (unsigned long long)e.ptrRva)
        {
            *detail = "the pointer at rva " + H(e.verifyAt) + " resolves to rva "
                    + (v >= (unsigned long long)base ? H(v - (unsigned long long)base) : std::string("(below the image base)"))
                    + ", expected " + H(e.ptrRva);
            return 0;
        }
        return 1;
    }

    {
        unsigned char got[kAddrBytesMax];
        if (e.nbytes <= 0 || e.nbytes > kAddrBytesMax)
        {
            *detail = "the table gives no bytes to check";
            return 0;
        }
        if (!ReadBytesPod((const void*)(base + e.verifyAt), got, (unsigned int)e.nbytes))
        {
            ++g_unreadable;
            *detail = "the " + N(e.nbytes) + " bytes at rva " + H(e.verifyAt) + " could not be read at all";
            return 0;
        }
        if (AddrBytesMatch(e, got) == 0)
        {
            *detail = "at rva " + H(e.verifyAt) + " the table expects " + Hex(e.bytes, e.nbytes)
                    + " and this process has " + Hex(got, e.nbytes);
            /* rk1: somebody else's 5-byte jump at the entry of a row this mod only calls, the rest the same function -
               accepted (src/common/addrhooked.h). Every other difference still refuses, below. */
            const int atEntry = (e.kind == kAddrCode && e.verifyAt == e.rva) ? 1 : 0;
            unsigned long long dest = 0;
            const int outside = ForeignJumpOutside(base, base + (uintptr_t)e.verifyAt, got, e.nbytes, &dest);
            const int fstart = (atEntry != 0 && callOnly == 2) ? IsFunctionStartPod(base, e.rva) : 0;
            int why = kWhyAccepted;
            if (AddrForeignVerdict(e.bytes, got, e.nbytes, atEntry, callOnly, fstart, outside, &why) == kHookAccepted) return 2;
            /* A jump as the FIRST byte is not a different game build - it is somebody else's hook, and it
               is a different repair (that address needs a shadow window in the table). Say which it is. */
            if (atEntry != 0 && (got[0] == 0xE9 || got[0] == 0xEB || got[0] == 0xFF || got[0] == 0x68))
            {
                ++g_hookedFirst;
                *detail += " - and the first byte is a jump, so this address is most likely ALREADY HOOKED by"
                           " another mod rather than being a different game build";
                *detail += std::string("; refused: ") + AddrWhyText(why)
                         + (outside != 0 ? " (the jump leads to " : " (the jump leads to rva/address ") + H(dest) + ")";
            }
            return 0;
        }
        return 1;
    }
}

/* T-1453 / T-294 (owner 1823 a, 'make sure the log reports it'): one line per optional row missed, counted for the summary
   and the periodic report (AddrReportToken). */
void NoteOptionalMissed(const AddrEntry& e, const std::string& why)
{
    ++g_optMissed;
    if (!g_optMissedNames.empty()) g_optMissedNames += ", ";
    g_optMissedNames += std::string(e.name) + " (" + e.optional + ")";
    ErrorLog(AddrOptionalMissLine(e, why));
}

void SetCaption(const char* s)
{
    std::strncpy(g_caption, s, sizeof(g_caption) - 1);
    g_caption[sizeof(g_caption) - 1] = 0;
}

/* ---- owner 97a: THE TITLE SCREEN WITHOUT A TABLE ----------------------------------------------------------
   A refusal is meant to be SEEN - the greyed-out MULTIPLAYER button carrying the caption - and that button rides
   the title-screen hook (P005), whose address is the table row TitleScreen_update. With NO table, or one that is
   not usable, that row stays 0 and there would be no button; so on exactly those two roads the function is found
   by its bytes (src/common/sigscan.h) in the LOADED image's .text, and used only if the match is UNIQUE. It is
   never used when a table loaded: there every row the table carries is already filled (a byte-mismatch refusal
   included), and a pattern must not overrule a table. Nothing but this one row is ever filled this way.
   The loaded image, not the file: this is the address the hook is installed at. The section headers are read in
   memory and every read is behind __try, as everything else in this file (F032). Returns 1 scanned, 0 a read
   raised, -1 no .text section, -2 the headers are not a PE image's (T-169: said apart in the log line). */
int ScanTextPod(uintptr_t base, coopsig::SigResult* res, unsigned long* textRva)
{
    __try
    {
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return -2;
        const IMAGE_NT_HEADERS64* nt = (const IMAGE_NT_HEADERS64*)(base + (uintptr_t)dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return -2;
        const IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
        WORD i;
        for (i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            if (std::memcmp(s[i].Name, ".text\0\0\0", 8) != 0) continue;
            *textRva = (unsigned long)s[i].VirtualAddress;
            *res = coopsig::SigScan((const unsigned char*)(base + s[i].VirtualAddress), (size_t)s[i].Misc.VirtualSize,
                                    coopsig::kTitleUpdateSig, coopsig::kTitleUpdateMask, coopsig::kTitleUpdateSigLen);
            return 1;
        }
        return -1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* Fills the TitleScreen_update slot from the pattern, or leaves it 0. One log line either way. */
void TitleByPattern(const char* why)
{
    unsigned long long* slot = 0;
    int i;
    for (i = 0; i < g_regCount; ++i)
        if (std::strcmp(g_regs[i].name, "TitleScreen_update") == 0) { slot = g_regs[i].slot; break; }
    if (slot == 0)
    {
        ErrorLog("[ADDR] title screen pattern not searched: no file registers the row TitleScreen_update");
        return;
    }
    coopsig::SigResult r;
    r.count = 0;
    r.first = -1;
    unsigned long textRva = 0;
    const int got = ScanTextPod(Base(), &r, &textRva);
    if (got == 1 && r.count == 1 && r.first >= 0)
    {
        *slot = (unsigned long long)textRva + (unsigned long long)r.first;
        DebugLog("[ADDR] title screen found by pattern at rva " + H(*slot) + " (" + why + ")");
        return;
    }
    ErrorLog("[ADDR] title screen pattern not found ("
             + (got == 1 ? N(r.count) + " matches, and only exactly 1 is used"
                         : std::string(got == 0 ? "reading the loaded image raised"
                                       : got == -2 ? "the loaded image's headers are not a PE image's"
                                                   : "the loaded image has no .text section"))
             + "; " + why + ") - the title-screen hook is NOT installed, so no button shows the reason: the [ADDR] line"
               " above is the only place it is said.");
}

}   /* anonymous namespace */

namespace coop {

AddrReg::AddrReg(const char* name, unsigned long long* slot, int use)
{
    if (g_regCount >= kMaxRegs) { ++g_regOverflow; return; }
    g_regs[g_regCount].name = name;
    g_regs[g_regCount].slot = slot;
    g_regUse[g_regCount] = (unsigned char)(use == kAddrUseCallOnly ? kHookUseCallOnly : 0);   /* rk1: anything else is strict */
    ++g_regCount;
}

int AddrOk() { return (int)::InterlockedCompareExchange(&g_ok, 0, 0); }

/* mig3 (RE_Kenshi migration stage 3) - see addresses.h. */
static unsigned long long kGameWorldPtrRva = 0; static AddrReg kGameWorldPtrRva_reg("GameWorldGlobal", &kGameWorldPtrRva);   /* Steam_1.0.65 0x21330B0 */
static unsigned long long kOptionsPtrRva = 0; static AddrReg kOptionsPtrRva_reg("OptionsHolderGlobal", &kOptionsPtrRva);   /* Steam_1.0.65 0x2132440 */

unsigned long long AddrAbs(unsigned long long rva) { return rva == 0 ? 0ull : (unsigned long long)Base() + rva; }
unsigned long long AddrTextEnd()
{
    static volatile unsigned long long s_end = 0;
    if (s_end != 0) return s_end;
    const unsigned char* b = (const unsigned char*)Base();
    if (b == 0) return 0;
    const IMAGE_NT_HEADERS64* nt = (const IMAGE_NT_HEADERS64*)(b + ((const IMAGE_DOS_HEADER*)b)->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (std::memcmp(sec[i].Name, ".text", 6) == 0) { s_end = (unsigned long long)sec[i].VirtualAddress + sec[i].Misc.VirtualSize; break; }
    return s_end;
}
::GameWorld* GameWorldPtr() { return (::GameWorld*)AddrAbs(kGameWorldPtrRva); }
::GameOptions* OptionsPtr() { return (::GameOptions*)AddrAbs(kOptionsPtrRva); }

unsigned long long Rva(const char* name)
{
    const AddrEntry* e = AddrFind(g_entries, g_entryCount, name);
    return e ? (unsigned long long)e->rva : 0ull;
}

/* review S3 (2026-10-17) - see addresses.h. Every registration bound to `target`; refused only when all of them are
   call-only (src/common/addrhooked.h AddrUsesAllCallOnly - one strict registration there means the gate checked those
   bytes exactly). After a registry overflow the gate treated every row as strict, so nothing is refused. */
const char* AddrHookRefusedAt(unsigned long long target)
{
    unsigned char uses[kMaxRegs];
    const char* first = 0;
    int i, n = 0;
    if (target == 0 || g_regOverflow != 0) return 0;
    for (i = 0; i < g_regCount; ++i)
    {
        if (g_regs[i].slot == 0 || *g_regs[i].slot == 0 || AddrAbs(*g_regs[i].slot) != target) continue;
        if (first == 0) first = g_regs[i].name;
        uses[n++] = g_regUse[i];
    }
    return AddrUsesAllCallOnly(uses, n) != 0 ? (first != 0 ? first : "?") : 0;
}

/* T-1447 - see addresses.h. The list is written once inside AddrInit, before any hook exists. */
int AddrChainedCheck(unsigned long long target, const char** row)
{
    int i;
    if (row) *row = 0;
    if (target == 0) return -1;
    for (i = 0; i < (int)g_chained.size(); ++i)
    {
        if (g_chained[i].abs != target) continue;
        if (row) *row = g_chained[i].name;
        unsigned char now[kAddrBytesMax];
        if (!ReadBytesPod((const void*)(uintptr_t)target, now, (unsigned int)g_chained[i].nbytes)) return 0;
        return std::memcmp(now, g_chained[i].got, (size_t)g_chained[i].nbytes) == 0 ? 1 : 0;
    }
    return -1;
}

void AddrChainedHooked(unsigned long long target)
{
    size_t i;
    for (i = 0; i < g_chained.size(); ++i)
        if (g_chained[i].abs == target)
            ReadBytesPod((const void*)(uintptr_t)target, g_chained[i].got, (unsigned int)g_chained[i].nbytes);
}

const char* AddrChainedModule(unsigned long long target)
{
    size_t i;
    for (i = 0; i < g_chained.size(); ++i)
        if (g_chained[i].abs == target) return g_chained[i].module.c_str();
    return 0;
}

const char* AddrRefusalCaption() { return g_caption; }

std::string AddrExeFingerprint() { return g_fingerprint; }

std::string AddrTableName() { return g_version; }

/* T-63 stage 7 (owner 99) - THE PATTERN ROAD. No addresses/<fingerprint>.txt matches this executable, so every row is
   looked for by its signature (addresses/signatures.sig, tools/gen_signatures.py) in the executable FILE's .text -
   the file, not the loaded image, for the same reason the fingerprint reads the file: another mod may have hooked
   code in memory before we look. EVERY row must be found exactly once and pass its own check (src/common/sigtable.h);
   anything less returns 0 with *why filled and every failing row logged, and the caller refuses exactly as it does
   with no table. On success g_entries holds the resolved set, named 'pattern:<fingerprint>', and AddrInit runs the
   SAME per-row verification against the running process that a table gets. Proven only on the two Steam builds
   (tools/prove_signatures.py reproduces both tables exactly); any other build is unproven until tested there. */
/* The executable FILE, whole (at most 256 MB), for a signature search. 1, or 0 with *why. T-1457: shared by the pattern road and
   the report road of a switched-off game (AddrResolveReportRows). */
static int ReadExeImage(const std::string& exe, std::vector<unsigned char>* img, std::string* why)
{
    std::ifstream f(U8W(exe.c_str()).c_str(), std::ios::binary);   /* UTF-8, from ExePath */
    if (!f) { *why = "the executable file could not be opened for the signature search"; return 0; }
    f.seekg(0, std::ios::end);
    const std::ifstream::pos_type endPos = f.tellg();
    if (endPos <= 0 || (unsigned long long)endPos > 268435456ull)
    {
        *why = "the executable file's size is not one the signature search reads";
        return 0;
    }
    img->resize((size_t)endPos);
    f.seekg(0, std::ios::beg);
    f.read((char*)&(*img)[0], (std::streamsize)img->size());
    if (f.gcount() != (std::streamsize)img->size()) { *why = "the executable file could not be read whole"; return 0; }
    return 1;
}

static int PatternResolve(const std::string& exe, std::string* why)
{
    const std::string sigPath = PathNextToDll("addresses\\signatures.sig");
    std::string sigText;
    if (!ReadWholeFile(sigPath, &sigText))
    {
        *why = "there is no signatures file either ('" + sigPath + "')";
        return 0;
    }
    coopsigtab::SigFile sf;
    coopsigtab::SigParseFile(sigText.c_str(), sigText.size(), &sf);

    std::vector<unsigned char> img;
    if (!ReadExeImage(exe, &img, why)) return 0;
    coopsigtab::PeView pe;
    if (!coopsigtab::SigParsePe(&img[0], img.size(), &pe))
    {
        *why = "the executable file has no readable .text section for the signature search";
        return 0;
    }
    coopsigtab::SigOutcome oc;
    const int n = coopsigtab::SigResolveAll(pe, sf, g_entries, kAddrMaxEntries, &oc);
    if (n <= 0)
    {
        *why = "the signature search did not find every row: " + N((long long)sf.rows.size() + (long long)sf.nosig.size())
             + " rows, " + N(oc.resolved) + " found exactly once, " + N(oc.failed) + " not, "
             + N((long long)oc.missedRows.size()) + " optional dropped"   /* T-1453 review: every row counted */
             + (oc.fails.empty() ? std::string("") : "; the first: " + oc.fails[0]);
        size_t i;
        for (i = 0; i < oc.fails.size() && i < 64; ++i)
            ErrorLog("[ADDR] signature row not resolved: " + oc.fails[i]);
        if (oc.fails.size() > 64)
            ErrorLog("[ADDR] ... and " + N((long long)oc.fails.size() - 64) + " more signature rows not resolved");
        g_entryCount = 0;
        return 0;
    }
    g_entryCount = n;
    g_version = "pattern:" + g_fingerprint;
    g_tablePath = sigPath;
    size_t m;
    for (m = 0; m < oc.missedRows.size(); ++m)   /* T-1453: an optional row missed - only its feature is off */
        NoteOptionalMissed(g_entries[oc.missedRows[m]], "signature search: " + oc.missedWhy[m]);
    for (m = 0; m < oc.skipped.size() && m < 64; ++m)   /* T-1453 review: a found row's check that named a dropped row */
        ErrorLog("[ADDR] signature cross-check skipped: " + oc.skipped[m]);
    if (oc.skipped.size() > 64)
        ErrorLog("[ADDR] ... and " + N((long long)oc.skipped.size() - 64) + " more signature cross-checks skipped");
    DebugLog("[ADDR] no address table for fingerprint " + g_fingerprint + ": " + N(oc.resolved) + " of " + N(n)
             + " rows found by signature in '" + exe + "' (each exactly once, each passing its own check; every required"
             " row found; optional rows not found: " + N((long long)oc.missedRows.size()) + "; cross-checks skipped for them: "
             + N((long long)oc.skipped.size()) + "). This build is NOT one the"
             " signatures were proven on (Steam 1.0.65 / 1.0.68); the per-row verification below still decides.");
    return 1;
}

/* gog1 fold 2: the game's own Hand_ctor FAULTED when GcBuildEmptyHand called it (caught by the __try/__except in
   gamecalls.cpp, BuildEmptyHandGuarded - AddrInit's catch (...) cannot catch a fault, see AddrInit). A refusal exactly
   like a set that failed its check against the running game: every slot back to 0, no entries, the empty handle built
   again with no game code (every slot is 0 now, so hand() writes its fields itself), the caption, one [ADDR] line,
   and owner 97a's title pattern. */
static int HandCtorFaulted()
{
    const AddrEntry* h = AddrFind(g_entries, g_entryCount, "Hand_ctor");
    const std::string at = h != 0 ? std::string(H(h->rva)) : std::string("(no row)");
    AddrClearSlots(g_regs, g_regCount);
    g_entryCount = 0;
    ::InterlockedExchange(&g_ok, 0);
    GcBuildEmptyHand();
    g_why = "the game's own hand constructor (row Hand_ctor, rva " + at + ") faulted when called to build the empty handle";
    SetCaption("MOD DISABLED: unsupported game version");
    ErrorLog("[ADDR] kenshi-coop is DISABLED in this game: " + g_why + ". Every address slot was cleared; nothing was"
             " hooked and no link was opened; Kenshi runs exactly as it does without this mod.");
    TitleByPattern("the game's hand constructor faulted");   /* owner 97a, as on every road without a usable table */
    return 0;
}

/* T-1457 TEST LEVER (owner decision 1824, F1707): KENSHI_SW_TEST_ADDR_FAIL=<row name> makes that one row of the loaded set fail its
   check, so a Steam game the tables describe is refused AS A BUILD THIS MOD WAS NOT MADE FOR IS (GOG, F1542): every slot cleared,
   no entries, the caption, one [ADDR] line, and owner 97a's title pattern - the road a switched-off game's REPORT A BUG starts from
   (AddrResolveReportRows). Read once, here, in the TEST build only (SW_DEV_BUILD - a player's build has no lever); set by the bench
   only (tools/run-T102.ps1 -AddrFailRow, for game B's launch only); nothing in the package sets it. A name the set does not carry is logged and changes nothing. */
#ifdef SW_DEV_BUILD   /* review of 2cfa25778: the lever is the TEST build's only - a player's build never reads it */
static int AddrTestFailRow(const std::string& row, bool byPattern)
{
    AddrClearSlots(g_regs, g_regCount);
    g_entryCount = 0;
    ::InterlockedExchange(&g_ok, 0);
    g_why = "TEST LEVER KENSHI_SW_TEST_ADDR_FAIL: the row " + row + " was made to fail its check against this game (the "
            + std::string(byPattern ? "signature" : "table") + " road), as on a build this mod was not made for";
    SetCaption("MOD DISABLED: unsupported game version");
    ErrorLog("[ADDR] kenshi-coop is DISABLED in this game: " + g_why + ". Every address slot was cleared; nothing was hooked and"
             " no link was opened; Kenshi runs exactly as it does without this mod.");
    TitleByPattern("the TEST LEVER failed one row");   /* owner 97a, as on every road without a usable set */
    return 0;
}
#endif   /* SW_DEV_BUILD - T-1457 TEST LEVER */

static int AddrInitBody()
{
    std::string exe, text;
    std::vector<unsigned char> head;
    unsigned long long fileSize = 0;
    unsigned int tds = 0, soi = 0;
    AddrParseResult pr;
    int i;

    GcBuildEmptyHand();   /* mig3 C3: once, here - before any hook, on every road (gamecalls.cpp, getHandle) */
    g_caption[0] = 0;

    if (!ExePath(&exe) || !ReadHead(exe, &head, &fileSize))
    {
        g_why = "the game executable's own first 64 KB could not be read (path '" + exe + "')";
        SetCaption("MOD DISABLED: unsupported game version");
        ErrorLog("[ADDR] kenshi-coop is DISABLED in this game: " + g_why + ". Nothing was hooked and no link was"
                 " opened; Kenshi runs exactly as it does without this mod. addrTableLoaded=0 addrEntriesChecked=0"
                 " addrMismatch=0.");
        return 0;
    }
    if (!AddrReadPeHeader(&head[0], head.size(), &tds, &soi))
    {
        g_why = "'" + exe + "' is not a 64-bit Windows executable this plugin can fingerprint";
        SetCaption("MOD DISABLED: unsupported game version");
        ErrorLog("[ADDR] kenshi-coop is DISABLED in this game: " + g_why + ". Nothing was hooked and no link was"
                 " opened; Kenshi runs exactly as it does without this mod.");
        return 0;
    }

    g_fingerprint = AddrFingerprint(tds, soi,
                                    coopstore::Crc32(&head[0], head.size()),
                                    (unsigned int)(fileSize & 0xFFFFFFFFull));
    g_tablePath = PathNextToDll(("addresses\\" + g_fingerprint + ".txt").c_str());

    bool byPattern = false;
    std::string patternWhy;
    const bool haveTable = ReadWholeFile(g_tablePath, &text) != 0;
    if (!haveTable)
        byPattern = PatternResolve(exe, &patternWhy) == 1;   /* T-63 stage 7: every row by its signature, or refused */
    if (!haveTable && !byPattern)
    {
        g_why = "there is no address table for this executable (fingerprint " + g_fingerprint + "), and " + patternWhy;
        SetCaption("MOD DISABLED: unsupported game version");
        ErrorLog("[ADDR] kenshi-coop is DISABLED in this game: " + g_why + ". Every address this mod uses was"
                 " read out of ONE build - Kenshi 1.0.65 x64 Steam - and using them on another build would be"
                 " arbitrary code, so nothing was hooked and no link was opened: Kenshi runs exactly as it does"
                 " without this mod. To add support for this build, put its table at '" + g_tablePath + "'."
                 " addrTableLoaded=0 addrEntriesChecked=0 addrMismatch=0.");
        TitleByPattern("no address table for this build");   /* owner 97a */
        return 0;
    }
    g_tableLoaded = 1;

    if (!byPattern)
        g_entryCount = AddrParseTable(text.c_str(), text.size(), g_entries, kAddrMaxEntries, &pr);
    g_badLines = pr.badLines;
    if (!byPattern)
        g_version = pr.version;

    if (!byPattern && (pr.fingerprint != g_fingerprint || g_entryCount <= 0 || pr.badLines > 0 || pr.overflow > 0
        || (pr.declaredCount >= 0 && pr.declaredCount != g_entryCount)))
    {
        g_why = "the address table at '" + g_tablePath + "' is not usable"
                " (its own !fingerprint is '" + pr.fingerprint + "', this executable is '" + g_fingerprint
                + "'; entries=" + N(g_entryCount) + " declared=" + N(pr.declaredCount)
                + " badLines=" + N(pr.badLines) + " overflow=" + N(pr.overflow)
                + (pr.firstBad.empty() ? std::string("")
                                       : "; the first line it refused is line " + N(pr.firstBadLine) + ": '" + pr.firstBad + "'")
                + ")";
        SetCaption("MOD DISABLED: unsupported game version");
        ErrorLog("[ADDR] kenshi-coop is DISABLED in this game: " + g_why + ". Nothing was hooked and no link was"
                 " opened; Kenshi runs exactly as it does without this mod. addrTableLoaded=1 addrEntriesChecked=0"
                 " addrMismatch=0.");
        g_entryCount = 0;
        TitleByPattern("no usable address table for this build");   /* owner 97a */
        return 0;
    }

#ifdef SW_DEV_BUILD
    {   /* T-1457 TEST LEVER: KENSHI_SW_TEST_ADDR_FAIL=<row> - see AddrTestFailRow (the TEST build's only) */
        char lv[96];
        const DWORD ln = ::GetEnvironmentVariableA("KENSHI_SW_TEST_ADDR_FAIL", lv, (DWORD)sizeof(lv));
        if (ln > 0 && ln < (DWORD)sizeof(lv))
        {
            if (AddrFind(g_entries, g_entryCount, lv) != 0) return AddrTestFailRow(lv, byPattern);
            ErrorLog(std::string("[ADDR] TEST LEVER: KENSHI_SW_TEST_ADDR_FAIL=") + lv + " names no row of this set - nothing changed");
        }
        else if (ln >= (DWORD)sizeof(lv))
            ErrorLog("[ADDR] TEST LEVER: KENSHI_SW_TEST_ADDR_FAIL is longer than a row name - nothing changed");
    }
#endif   /* SW_DEV_BUILD - T-1457 TEST LEVER */

    {
        const uintptr_t base = Base();
        long long refusedRows = 0;   /* T-1476 review: every refused required row gets its own line, the first kRefusedLogMax */
        const long long kRefusedLogMax = 16;
        for (i = 0; i < g_entryCount; ++i)
        {
            if (g_entries[i].missed != 0) continue;   /* T-1453: an optional row the signature search missed (logged there) */
            std::string detail;
            /* T-1476 (F2011, owner 1876): BOTH ROADS apply the row's declared use. The signature road places the row from the
               executable FILE's own bytes (PatternResolve -> ReadExeImage: the file on disk, never the hooked process); another
               mod's 5-byte jump at the function's start changes only those first bytes in the running game, so the declared
               use decides, exactly as on the table road (T-1447). Before, the signature road kept every row strict and
               RE_Kenshi's jump on the call-only SoundPostEvent refused every GOG game. Every other check is unchanged. */
            const int use = AddrGateUse(RowUse(g_entries[i].name));   /* T-1447: 0 strict, 1 call-only, 2 hooks, may chain; both roads */
            const int v = VerifyEntry(base, g_entries[i], use, &detail);
            if (v < 0) { ++g_varEntries; continue; }
            ++g_checked;
            if (v == 2 && use == 2)
            {
                /* T-1447: a row we HOOK, another mod hooked first - our hook goes on top (AddHook re-reads these bytes first,
                   AddrChainedCheck). The bytes are kept for that check; more such rows than the list holds refuse. */
                const uintptr_t at = base + (uintptr_t)g_entries[i].verifyAt;
                ChainedRow cr;
                cr.abs = (unsigned long long)at;
                cr.name = g_entries[i].name;
                cr.nbytes = g_entries[i].nbytes;
                if (!ReadBytesPod((const void*)at, cr.got, (unsigned int)cr.nbytes))
                {
                    ++g_mismatch;
                    if (g_firstMismatch.empty())
                        g_firstMismatch = std::string(g_entries[i].name) + ": another mod's jump at a game function's start,"
                                          " but its bytes could not be read again to keep them - " + detail;
                    if (++refusedRows <= kRefusedLogMax)
                        ErrorLog(std::string("[ADDR] row refused: ") + g_entries[i].name + " (" + AddrKindName(g_entries[i].kind)
                                 + ", rva " + H(g_entries[i].rva) + "): another mod's jump at a game function's start, but its"
                                 " bytes could not be read again to keep them - " + detail);
                    continue;
                }
                std::string how;
                const std::string mod = ForeignJumpModule(at, &how);
                cr.module = mod;
                if (g_chained.empty()) g_chained.reserve((size_t)g_entryCount);
                g_chained.push_back(cr);
                ++g_hookedChained;
                DebugLog(std::string("[ADDR] ") + g_entries[i].name + " (rva " + H(g_entries[i].rva) + ") starts with another"
                         " mod's jump to " + mod + " - our hook is chained on top (hooks, may chain) [" + how + "; " + detail + "]");   /* a row we only read at its start: its own check skips that one hook, logged there */
                continue;
            }
            if (v == 2)
            {
                ++g_hookedAccepted;
                DebugLog(std::string("[ADDR] ") + g_entries[i].name + " (rva " + H(g_entries[i].rva) + ") starts with another"
                         " mod's jump (hooked); the rest of its bytes match and this mod only calls it, so it is accepted ("
                         + detail + ")");
            }
            if (v == 0)
            {
                /* T-1453 / T-294: an OPTIONAL row that does not match this game is missed - its slots get 0 and its users
                   switch its feature off; only a required row's mismatch switches the mod off */
                if (AddrOptionalMiss(&g_entries[i]) != 0)
                {
                    NoteOptionalMissed(g_entries[i], "its bytes do not match this game: " + detail);
                    continue;
                }
                ++g_mismatch;
                if (g_firstMismatch.empty())
                    g_firstMismatch = std::string(g_entries[i].name) + " (" + AddrKindName(g_entries[i].kind)
                                    + ", rva " + H(g_entries[i].rva) + "): " + detail;
                if (++refusedRows <= kRefusedLogMax)   /* T-1476 review: T3027 had addrMismatch=2 with one row unnamed */
                    ErrorLog(std::string("[ADDR] row refused: ") + g_entries[i].name + " (" + AddrKindName(g_entries[i].kind)
                             + ", rva " + H(g_entries[i].rva) + "): " + detail);
            }
        }
        if (refusedRows > kRefusedLogMax)
            ErrorLog("[ADDR] ... and " + N(refusedRows - kRefusedLogMax) + " more rows refused (the first "
                     + N(kRefusedLogMax) + " are named above)");
    }

    /* Fill every registered slot, and treat a name the table does not carry exactly like a mismatch: a slot
       left at zero would make `base + 0` a call target, which is the crash this whole file exists to stop. */
    int missing = 0, firstMissing = -1;
    const int settled = AddrSettleSlots(g_entries, g_entryCount, g_regs, g_regCount,
                                        (g_mismatch != 0 || g_regOverflow != 0) ? 1 : 0, byPattern ? 1 : 0,
                                        &missing, &firstMissing);   /* gog1 fold 2: bind; a pattern-road refusal clears every slot */
    g_bindMissing += missing;
    if (g_regOverflow != 0 && g_firstMismatch.empty())
        g_firstMismatch = "the plugin binds more addresses than its registry holds (" + N((long long)kMaxRegs) + ") - " + N((long long)g_regOverflow) + " left out";
    if (firstMissing >= 0 && g_firstMismatch.empty())
        g_firstMismatch = std::string(g_regs[firstMissing].name) + ": the plugin asks for this name and the table"
                          " does not carry it";

    if (!settled)
    {
        g_why = "the address table '" + g_tablePath + "' does not describe this process";
        SetCaption("MOD DISABLED: unsupported game version");
        ErrorLog("[ADDR] kenshi-coop is DISABLED in this game: " + g_why + ". addrTableLoaded=1 addrEntriesChecked="
                 + N(g_checked) + " addrMismatch=" + N(g_mismatch) + " addrVarEntries=" + N(g_varEntries)
                 + " addrBindMissing=" + N(g_bindMissing) + " addrUnreadable=" + N(g_unreadable)
                 + " addrLooksHooked=" + N(g_hookedFirst) + " addrHookedAccepted=" + N(g_hookedAccepted)
                 + " addrHookedChained=" + N(g_hookedChained) + " addrRegOverflow=" + N(g_regOverflow)
                 + ". FIRST MISMATCH: " + g_firstMismatch
                 + ". Nothing was hooked and no link was opened; Kenshi runs exactly as it does without this mod.");
        if (byPattern)
        {
            /* gog1 fold (LOW): a signature set that FAILED its check against the running game gives no address to
               anyone - not even TitleScreen_update to the refusal's title hook (startRefused). Only owner 97a's own
               title pattern may place that hook, exactly as on every other road without a usable table. */
            g_entryCount = 0;   /* every slot is already 0: AddrSettleSlots cleared them (addrtable.cpp; offline test
                                   addr_pattern_refusal_clears_every_slot) */
            TitleByPattern("the signature set failed its check against the running game");
        }
        return 0;
    }

    /* gog1 fold 2: the ONE call of game code AddrInit makes. It runs under its own __try/__except (gamecalls.cpp,
       BuildEmptyHandGuarded); a fault is a refusal (HandCtorFaulted), never a crash. */
    if (!GcBuildEmptyHand()) return HandCtorFaulted();
    /* mig4 G3b: built again now the Hand_ctor row is bound, so the empty handle is the game's own
                            (its vtable, type RECORD_NONE); the first build above ran before any row was filled */
    ::InterlockedExchange(&g_ok, 1);
    DebugLog("[ADDR] address table '" + g_version + "' matched this executable: fingerprint " + g_fingerprint
             + ", addrTableLoaded=1 addrEntriesChecked=" + N(g_checked) + " addrMismatch=0 addrVarEntries="
             + N(g_varEntries) + " addrHookedAccepted=" + N(g_hookedAccepted) + " addrHookedChained=" + N(g_hookedChained)
             + " addrBound=" + N(g_regCount) + " addrOptionalMissed=" + N(g_optMissed)
             + (g_optMissed != 0 ? " (features off: " + g_optMissedNames + ")" : std::string(""))
             + " from '" + g_tablePath + "'."
             " addrVarEntries are writable globals - they have no constant bytes to compare and are NOT part of"
             " addrEntriesChecked; they ride on the other entries and on the fingerprint (build/prep-p8h.md).");
    return 1;
}

/* gog1 fold (LOW), corrected in fold 2: WHAT AddrInit's WRAPPER CATCHES - C++ EXCEPTIONS ONLY. The pattern road
   reads the whole executable (~36.7 MB) into memory, and an allocation that fails there throws std::bad_alloc;
   caught here, it refuses the start instead of leaving it half-done. Any C++ exception (std::bad_alloc, any std::exception, anything else thrown) is a refusal like every other:
   every slot back to 0, no entries, the caption and one [ADDR] line - the log line itself inside its own try.
   This file is built with /EHsc, so catch (...) does NOT catch an access violation or any other SEH fault. Those
   are guarded where they can happen, each by its own __try/__except in a small function holding only PODs (C2712):
   every read of the running image in this file (the small readers above and ScanTextPod), and the one call of game
   code AddrInit makes - the game's Hand_ctor, through GcBuildEmptyHand (gamecalls.cpp, BuildEmptyHandGuarded),
   whose fault refuses through HandCtorFaulted. */
static int AddrInitThrew(const char* what)
{
    AddrClearSlots(g_regs, g_regCount);
    g_entryCount = 0;
    ::InterlockedExchange(&g_ok, 0);
    SetCaption("MOD DISABLED: unsupported game version");
    try
    {
        g_why = std::string("reading the game executable or the address files raised an exception (") + what + ")";
        ErrorLog("[ADDR] kenshi-coop is DISABLED in this game: " + g_why + ". Nothing was hooked and no link was"
                 " opened; Kenshi runs exactly as it does without this mod.");
        TitleByPattern("AddrInit raised an exception");   /* owner 97a, as on every road without a usable table */
    }
    catch (...) {}
    return 0;
}

int AddrInit()
{
    if (::InterlockedExchange(&g_ran, 1) != 0) return AddrOk();
    try { return AddrInitBody(); }
    catch (const std::bad_alloc&) { return AddrInitThrew("out of memory (std::bad_alloc)"); }
    catch (const std::exception& e) { return AddrInitThrew(e.what()); }
    catch (...) { return AddrInitThrew("an unknown exception"); }
}

std::string AddrReportToken()
{
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << "addr[ok=" << AddrOk()
      << ",tableLoaded=" << g_tableLoaded
      << ",entriesChecked=" << g_checked
      << ",mismatch=" << g_mismatch
      << ",varEntries=" << g_varEntries
      << ",unreadable=" << g_unreadable
      << ",looksHooked=" << g_hookedFirst
      << ",hookedAccepted=" << g_hookedAccepted
      << ",hookedChained=" << g_hookedChained
      << ",bindMissing=" << g_bindMissing
      << ",optMissed=" << g_optMissed   /* T-1453: optional rows missed, their features off */
      << ",bound=" << g_regCount
      << ",badLines=" << g_badLines
      << ",fp=" << g_fingerprint
      << "]";
    return o.str();
}

/* T-1457 (owner decision 1824, F1707) - THE REPORT ROAD OF A SWITCHED-OFF GAME. See addresses.h. Each row the report needs
   (src/common/reportroad.h ReportRows) is looked for by ITS OWN signature in the executable FILE (the signatures file, SigSubset +
   SigResolveAll: exactly once, its own check, the cross-checks over these rows), then its bytes are compared with the RUNNING game
   (VerifyEntry, strict: another mod's jump on one of them is a mismatch), and only then is its slot written. Every row gets one
   [ADDR] line. A required row that fails leaves the report off (logged) - the game stays plain Kenshi. */
int AddrReportRowsOk() { return g_reportOk; }
int AddrTestLoadRowsOk() { return g_testLoadOk; }

static int ReportRowsOff(const std::string& why)
{
    ErrorLog("[ADDR] REPORT A BUG stays OFF on this switched-off game: " + why + ". Nothing was hooked for it; Kenshi runs exactly as"
             " it does without this mod.");
    return 0;
}

static int ReportRowsBody()
{
#ifdef SW_DEV_BUILD
    const int dev = 1;
#else
    const int dev = 0;
#endif
    int rc = 0;
    const coopreport::ReportRow* rr = coopreport::ReportRows(&rc);
    std::vector<const char*> names, feats;
    int i;
    for (i = 0; i < rc; ++i)
    {
        if (rr[i].testOnly != 0 && dev == 0) continue;
        names.push_back(rr[i].name);
        feats.push_back(rr[i].feature);
    }
    const std::string sigPath = PathNextToDll("addresses\\signatures.sig");
    std::string sigText;
    if (!ReadWholeFile(sigPath, &sigText)) return ReportRowsOff("there is no signatures file ('" + sigPath + "')");
    coopsigtab::SigFile all, sub;
    coopsigtab::SigParseFile(sigText.c_str(), sigText.size(), &all);
    std::vector<std::string> absent;
    coopsigtab::SigSubset(all, &names[0], &feats[0], (int)names.size(), &sub, &absent);
    int absentRequired = 0;
    size_t a;
    for (a = 0; a < absent.size(); ++a)
    {
        int optional = 0;
        for (i = 0; i < (int)names.size(); ++i) if (absent[a] == names[i] && feats[i] != 0) optional = 1;
        if (!optional) ++absentRequired;
        ErrorLog("[ADDR] report row " + absent[a] + ": not in the signatures file" + (optional ? " - only its feature is off" : ""));
    }
    std::string exe, why;
    std::vector<unsigned char> img;
    if (!ExePath(&exe)) return ReportRowsOff("the game executable's path could not be read");
    if (!ReadExeImage(exe, &img, &why)) return ReportRowsOff(why);
    coopsigtab::PeView pe;
    if (!coopsigtab::SigParsePe(&img[0], img.size(), &pe)) return ReportRowsOff("the executable file has no readable .text section");
    AddrEntry ent[16];
    coopsigtab::SigOutcome oc;
    const int got = (int)sub.rows.size() <= 16 ? coopsigtab::SigResolveAll(pe, sub, ent, 16, &oc) : 0;
    if (got <= 0)
    {
        for (a = 0; a < oc.fails.size() && a < 16; ++a) ErrorLog("[ADDR] report row not resolved: " + oc.fails[a]);
        return ReportRowsOff("the signature search did not find every row the report needs exactly once ("
                             + N((long long)oc.resolved) + " of " + N((long long)sub.rows.size()) + " found"
                             + (oc.fails.empty() ? std::string("") : "; the first: " + oc.fails[0]) + ")");
    }
    /* the check against the running game - strict for every row (use 0; the start-up signature road applies each row's declared
       use since T-1476, this report road is unchanged) (VerifyEntry's counters are the gate's: kept) */
    const long long unreadable = g_unreadable, hookedFirst = g_hookedFirst;
    const uintptr_t base = Base();
    int live[16];
    for (i = 0; i < got; ++i)
    {
        live[i] = 0;
        if (ent[i].missed != 0)
        {
            size_t m;
            std::string mw;
            for (m = 0; m < oc.missedRows.size(); ++m) if (oc.missedRows[m] == i) mw = oc.missedWhy[m];
            ErrorLog(std::string("[ADDR] report row ") + ent[i].name + " (optional: " + ent[i].optional + ") not found by its own bytes - "
                     + ent[i].optional + " is off on this road (" + mw + ")");
            continue;
        }
        std::string detail;
        live[i] = VerifyEntry(base, ent[i], 0, &detail);
        const bool ok = live[i] == 1 || live[i] == -1;
        const std::string line = std::string("[ADDR] report row ") + ent[i].name + " found by its own bytes exactly once at rva "
                               + H(ent[i].rva) + (live[i] == -1 ? " (a writable global: its second reference names it too)"
                                                  : ok ? "; its bytes in the running game match" : "; its bytes in the running game do NOT match: " + detail);
        if (ok) DebugLog(line); else ErrorLog(line);
        if (!ok && ent[i].optional[0] != 0) AddrOptionalMiss(&ent[i]);   /* only its feature off */
    }
    g_unreadable = unreadable; g_hookedFirst = hookedFirst;
    std::string vwhy;
    if (!coopreport::ReportRowsVerdict(got, absentRequired, ent, live, got, &vwhy)) return ReportRowsOff(vwhy);
    /* the title hook's row: owner 97a's title pattern may have placed it already (the loaded image) - the two must agree */
    const AddrEntry* te = AddrFind(ent, got, "TitleScreen_update");
    for (i = 0; i < g_regCount; ++i)
    {
        if (g_regs[i].name != 0 && std::strcmp(g_regs[i].name, "TitleScreen_update") == 0 && te != 0 && *g_regs[i].slot != 0
            && *g_regs[i].slot != (unsigned long long)te->rva)
            return ReportRowsOff("the title screen's row by its signature (rva " + H(te->rva) + ") is not where the title hook's own"
                                 " search put it (rva " + H(*g_regs[i].slot) + ")");
    }
    /* bind: every slot registered under one of these names gets its row's rva, or 0 for a missed optional row */
    int bound = 0;
    for (i = 0; i < g_regCount; ++i)
    {
        if (g_regs[i].name == 0) continue;
        const AddrEntry* e = AddrFind(ent, got, g_regs[i].name);
        if (e == 0) continue;
        *g_regs[i].slot = e->missed != 0 ? 0ull : (unsigned long long)e->rva;
        ++bound;
    }
    int testLoad = dev;
    for (i = 0; i < got; ++i) if (std::strcmp(ent[i].optional, "test-load") == 0 && ent[i].missed != 0) testLoad = 0;
    g_testLoadOk = testLoad;
    g_reportOk = 1;
    DebugLog("[ADDR] REPORT A BUG on this switched-off game: its " + N((long long)got) + " rows found by their own bytes, each exactly"
             " once and matching the running game (" + N((long long)bound) + " slots bound; nothing else bound, nothing hooked yet)"
             + (dev ? std::string(testLoad ? "; TEST-ONLY load rows found" : "; TEST-ONLY load rows NOT found - the test reader's load is off") : std::string("")));
    return 1;
}

int AddrResolveReportRows()
{
    if (g_reportRan != 0) return g_reportOk;
    g_reportRan = 1;
    if (AddrOk() != 0 || ::InterlockedCompareExchange(&g_ran, 0, 0) == 0) return 0;   /* only after a refusal */
    try { return ReportRowsBody(); }
    catch (const std::exception& e) { try { ReportRowsOff(std::string("an exception (") + e.what() + ")"); } catch (...) {} }
    catch (...) { try { ReportRowsOff("an unknown exception"); } catch (...) {} }
    g_reportOk = 0;
    return 0;
}

}   /* namespace coop */
