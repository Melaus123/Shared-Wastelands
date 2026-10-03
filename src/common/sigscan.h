/* sigscan.h - owner decision 97a (2026-09-28). A byte-pattern scanner, and the one pattern the plugin uses.
 *
 * WHY. With no usable address table for the running executable the plugin refuses to install anything, but
 * the refusal is meant to be SEEN: a greyed-out MULTIPLAYER button on the title screen carrying the reason.
 * That button rides the title-screen hook (P005, TitleScreen::update), and that
 * hook's address comes from the table - so with no table there would be no button. This finds that ONE function
 * by its bytes instead. Nothing else is ever found this way.
 *
 * PURE: no Windows, no game, no allocation - so the offline suite (src/coop-test) tests exactly this code.
 * The caller decides what range to scan (addresses.cpp: the loaded image's .text) and guards the reads.
 *
 * C++03 (VS2010 v100).
 */
#pragma once

#include <cstddef>
#include <cstring>

namespace coopsig {

/* count: how many places in the range match (all of them are counted, so "exactly once" can be enforced
   and a refusal can say how many it saw). first: the offset of the first match from the range start, or
   -1 when there is none. Bad input (null pointer, empty pattern, a mask whose length is not the pattern's,
   a mask character other than 'x' or '?') gives count 0 and first -1. */
struct SigResult
{
    int       count;
    long long first;
};

/* mask: one character per pattern byte - 'x' = the byte must equal pat[i], '?' = any byte (pat[i] is then
   ignored). A match may END on the last byte of the range; nothing past the range is ever read. */
inline SigResult SigScan(const unsigned char* p, size_t len, const unsigned char* pat, const char* mask, size_t n)
{
    SigResult r;
    r.count = 0;
    r.first = -1;
    if (p == 0 || pat == 0 || mask == 0 || n == 0 || len < n) return r;
    if (std::strlen(mask) != n) return r;
    size_t a = n;   /* the first fixed byte, used to skip ahead with memchr */
    size_t k;
    for (k = 0; k < n; ++k)
    {
        if (mask[k] != 'x' && mask[k] != '?') return r;
        if (mask[k] == 'x' && a == n) a = k;
    }
    const size_t last = len - n;   /* the last offset a match can start at */
    size_t i = 0;
    while (i <= last)
    {
        if (a < n)
        {
            const void* hit = std::memchr(p + i + a, pat[a], last - i + 1);
            if (hit == 0) break;
            i = (size_t)((const unsigned char*)hit - p) - a;
        }
        for (k = 0; k < n; ++k)
            if (mask[k] == 'x' && p[i + k] != pat[k]) break;
        if (k == n)
        {
            if (r.count == 0) r.first = (long long)i;
            ++r.count;
        }
        ++i;
    }
    return r;
}

/* TitleScreen::update - the first 98 bytes of the function, entry at offset 0.
   Read 2026-09-28 from the files on disk (scratch script, capstone disassembly), and it matches EXACTLY ONCE
   in each executable's .text:
     Steam 1.0.65 as patched by RE_Kenshi (kenshi_x64.exe, MD5 df4a5a7e...)  rva 0x9129B0 (= the table row)
     Steam 1.0.68 (kenshi_x64_vanilla.exe, MD5 8a03c256...)                  rva 0x913880
   The whole 739-byte function, masked the same way, also matches once in each: same code, moved 0xED0.
   WILDCARDS: every rip-relative displacement and every rel32 call/jump target (they change whenever
   anything moves), and the first 15 bytes - the prologue (push rdi / sub rsp,50h / mov [rsp+20h],-2),
   which is the most generic part of the function and is exactly where another mod's hook would write its
   jump; without them the pattern is still unique in both files. */
static const size_t kTitleUpdateSigLen = 98;
static const unsigned char kTitleUpdateSig[98] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x48,0x89,0x5C,0x24,0x78,                   /* mov [rsp+78h],rbx */
    0x0F,0x29,0x74,0x24,0x40,                   /* movaps [rsp+40h],xmm6 */
    0x0F,0x29,0x7C,0x24,0x30,                   /* movaps [rsp+30h],xmm7 */
    0x48,0x8B,0xF9,                             /* mov rdi,rcx */
    0x8B,0x05,0x00,0x00,0x00,0x00,              /* mov eax,[rip+?] */
    0x33,0xDB,                                  /* xor ebx,ebx */
    0xA8,0x01,                                  /* test al,1 */
    0x0F,0x85,0x00,0x00,0x00,0x00,              /* jne ? */
    0x83,0xC8,0x01,                             /* or eax,1 */
    0x89,0x05,0x00,0x00,0x00,0x00,              /* mov [rip+?],eax */
    0x48,0x8D,0x05,0x00,0x00,0x00,0x00,         /* lea rax,[rip+?] */
    0x48,0x89,0x05,0x00,0x00,0x00,0x00,         /* mov [rip+?],rax */
    0x48,0x39,0x1D,0x00,0x00,0x00,0x00,         /* cmp [rip+?],rbx */
    0x0F,0x85,0x00,0x00,0x00,0x00,              /* jne ? */
    0x48,0x8D,0x0D,0x00,0x00,0x00,0x00,         /* lea rcx,[rip+?] */
    0xFF,0x15,0x00,0x00,0x00,0x00               /* call [rip+?] */
};
static const char kTitleUpdateMask[] =
    "???????????????"
    "xxxxx" "xxxxx" "xxxxx" "xxx"
    "xx????" "xx" "xx" "xx????" "xxx" "xx????"
    "xxx????" "xxx????" "xxx????" "xx????" "xxx????" "xx????";

}   /* namespace coopsig */
