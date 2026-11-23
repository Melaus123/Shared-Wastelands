/* addrhooked.h - rk1: A ROW ANOTHER MOD HAS HOOKED, WHEN THIS MOD ONLY CALLS IT.
 *
 * The address gate (src/coop-plugin/addresses.cpp, VerifyEntry) compares the bytes at every table row with the bytes the
 * table was read against. Another mod loaded first (RE_Kenshi 0.3.5, run T1339) may already have replaced a function's
 * first instruction with a 5-byte jump to its own code - its hook. The row then differs, and before rk1 every such
 * difference disabled the whole mod.
 *
 * For a row this mod only CALLS (never hooks or patches), that difference is harmless: the game calls the same address
 * and goes through the other mod's jump every time, and so does this mod. So such a row is ACCEPTED, but only when the
 * bytes prove it is still the same function in the same build:
 *   (a) the row is a code row whose bytes are read at the function's own start (atEntry), and the process's first byte
 *       is E9 - a 5-byte relative jump (E9 + 4 bytes of distance);
 *   (b) every expected byte AFTER those first 5 matches the process, so only the entry was patched - and there must BE
 *       such bytes (a row of 5 bytes or fewer proves nothing and is refused);
 *   (c) the caller says every registration of the row's name is call-only (callOnly != 0).
 * Anything else that differs is refused exactly as before. Not handled (refused): any other jump form - a short EB jump,
 * an FF 25 absolute jump (6 bytes + an 8-byte address, which overwrites more than 5 bytes), a push/ret pair - and a hook
 * placed further into the function than its first 5 bytes.
 *
 * Pure: no Windows call. Offline tests: src/coop-test/test_main.cpp (t_rk1_addr_hooked_call_only_verdict,
 * t_rk1_addr_uses_all_call_only).
 * C++03 (VS2010 v100).
 */
#pragma once

namespace coopaddr {

enum
{
    kHookRefuse   = 0,   /* the bytes differ and nothing here allows it: the gate refuses, as before rk1 */
    kHookExact    = 1,   /* every byte matches */
    kHookAccepted = 2    /* another mod's 5-byte jump at the entry, the rest matches, and this mod only calls the row */
};

const int kHookJumpLen = 5;   /* E9 + a 4-byte relative distance */
const int kHookTailMin = 8;   /* review rk1 N2: at least this many matching bytes after the jump, or nothing is proven */

/* expected: the table's bytes; got: the process's bytes at the same place; nbytes: how many of each.
   atEntry: 1 when the row is a code row whose bytes start at the function's own address (verifyAt == rva).
   callOnly: 1 when every registration of the row's name only calls (or reads) the address. */
inline int AddrHookedVerdict(const unsigned char* expected, const unsigned char* got, int nbytes, int atEntry, int callOnly)
{
    int i;
    if (expected == 0 || got == 0 || nbytes <= 0) return kHookRefuse;
    for (i = 0; i < nbytes; ++i)
        if (expected[i] != got[i]) break;
    if (i == nbytes) return kHookExact;
    if (atEntry == 0 || callOnly == 0) return kHookRefuse;      /* (c), and (a)'s "at the function's start" */
    if (nbytes - kHookJumpLen < kHookTailMin) return kHookRefuse;   /* (b): too little after the jump to prove it */
    if (expected[0] == 0xE9) return kHookRefuse;                /* review rk1 N3: the function itself starts with a jump - a changed distance proves nothing */
    if (got[0] != 0xE9) return kHookRefuse;                     /* (a): only the 5-byte relative jump */
    for (i = kHookJumpLen; i < nbytes; ++i)
        if (expected[i] != got[i]) return kHookRefuse;          /* (b): the rest is not the same function */
    return kHookAccepted;
}
/* T-1447: the gate itself now calls AddrForeignVerdict (below), which adds FF 25 rip-relative, the game's function table
   for rows this mod does more than call, and "the jump leaves the game's image"; this E9 / call-only form stays for rk1's
   offline tests and keeps its meaning. */

/* review S2/S3 (2026-10-17): IS A SET OF REGISTRATIONS CALL-ONLY? uses: the use value of each registration in the set
   (1 = kAddrUseCallOnly in src/coop-plugin/addresses.h; anything else is strict); n: how many. 1 only when there is at
   least one and EVERY one is call-only - a single strict registration makes the whole set strict, and an empty set is
   strict (nobody declared a use).
   Two callers in addresses.cpp: RowCallOnly (the set = every registration of one row's NAME - the gate may then accept
   another mod's jump at the row's entry), and AddrHookRefusedAt (the set = every registration bound to the ADDRESS a
   hook is about to be installed at - when it is call-only, coop::AddHook refuses: the gate may have accepted another
   mod's jump there, and this mod promised only to call it). */
const unsigned char kHookUseCallOnly = 1;

inline int AddrUsesAllCallOnly(const unsigned char* uses, int n)
{
    int i;
    if (uses == 0 || n <= 0) return 0;
    for (i = 0; i < n; ++i)
        if (uses[i] != kHookUseCallOnly) return 0;
    return 1;
}

/* T-1447 (F1476 design A, re-check fold 2, 2026-11-19): ANOTHER MOD'S JUMP AT THE START OF A GAME FUNCTION - decided
   from the bytes and the game's own function table, never from a hand-made list (complete by construction).
   A code row whose bytes start at a GAME FUNCTION'S START (the row's address is the BeginAddress of an entry of the
   executable's own exception directory, .pdata - every non-leaf x64 function has one; a patch site inside a function has
   none) may carry another mod's hook: its first instruction replaced by a jump OUT of the game's image (to a DLL or to memory
   that mod allocated), every later byte the table names still matching. Then:
     - a row this mod only calls: the call goes through the other mod's jump, as the game's own calls do;
     - a row this mod hooks: our MinHook copies the jump into our trampoline (E9 as an absolute jump to its target,
       trampoline.c:200-229; FF 25 rip-relative with its distance re-aimed, :163-183), so the call runs our detour -> our
       trampoline -> the other mod's code -> the game; coop::AddHook re-reads the bytes first (hooks.cpp);
     - a row this mod reads at its start (a prologue check before a hook) finds the jump and skips that one hook, logged.
   A PATCH SITE (bytes this mod writes) lies inside a function, so it has no .pdata entry and another mod's jump there is
   refused, exactly as before. The signature road (no table) applies the same rule since T-1476 (AddrGateUse below).
   THE JUMP FORMS ACCEPTED: E9 rel32 (5 bytes) and FF 25 rip-relative with a non-zero distance (6 bytes, jmp [rip+d]); the
   destination (for FF 25, the 8-byte pointer's value) must lie outside the game's image. FF 25 00000000 + an 8-byte address
   (14 bytes) is refused: it overwrites 14 of the 16 bytes a row names, so the rest cannot prove the function (kWhyLongJump).
   USE: 1 = every registration of the name is call-only (no function-table check needed: we never touch its bytes); 2 = any
   other set (hooked, read, or not declared) - accepted only at a function start; 0 = no registration (a name read through
   Rva() whose use nobody declared, or a registry overflow) - refused. */
enum
{
    kWhyAccepted = 0, kWhyNotEntry = 1, kWhyNoUse = 2, kWhyNotFunction = 3, kWhyExpectedJump = 4, kWhyNoJump = 5,
    kWhyLongJump = 6, kWhyTooShort = 7, kWhyRestDiffers = 8, kWhyInImage = 9
};

inline const char* AddrWhyText(int why)
{
    switch (why)
    {
    case kWhyAccepted:     return "accepted";
    case kWhyNotEntry:     return "the row is not read at the function's own address";
    case kWhyNoUse:        return "no registration declares a use for this row";
    case kWhyNotFunction:  return "the address is not the start of a game function (no entry in the game's function table) - a patch site or a mid-function hook";
    case kWhyExpectedJump: return "the game's own bytes there start with a jump - a changed jump proves nothing";
    case kWhyNoJump:       return "the first instruction is not a jump this check knows (E9, or FF 25 rip-relative)";
    case kWhyLongJump:     return "the jump is the 14-byte FF 25 00000000 form, which leaves too few bytes to prove the function";
    case kWhyTooShort:     return "too few bytes after the jump to prove the function";
    case kWhyRestDiffers:  return "the bytes after the jump differ - not the same function";
    case kWhyInImage:      return "the jump stays inside the game's own image - not another mod's hook";
    }
    return "?";
}

/* The length of another mod's jump at `got` (n bytes): 5 for E9 rel32, 6 for FF 25 with a non-zero distance, 14 for
   FF 25 00000000 + address; 0 for anything else. Pure. */
inline int AddrForeignJumpLen(const unsigned char* got, int n)
{
    if (got == 0) return 0;
    if (n >= 5 && got[0] == 0xE9) return 5;
    if (n >= 6 && got[0] == 0xFF && got[1] == 0x25) return (got[2] | got[3] | got[4] | got[5]) != 0 ? 6 : 14;
    return 0;
}

/* expected / got / nbytes as AddrHookedVerdict. atEntry: the row's bytes start at its own address; use: 0 / 1 / 2 above;
   funcStart: the address is a .pdata BeginAddress; jumpOutside: the jump's destination lies outside the game's image (the
   caller reads it). *why (may be 0) gets a kWhy* code. Returns kHookExact, kHookAccepted or kHookRefuse. Pure. */
inline int AddrForeignVerdict(const unsigned char* expected, const unsigned char* got, int nbytes, int atEntry, int use,
                              int funcStart, int jumpOutside, int* why)
{
    int i, len, w = kWhyAccepted;
    if (expected == 0 || got == 0 || nbytes <= 0) { if (why) *why = kWhyTooShort; return kHookRefuse; }
    for (i = 0; i < nbytes; ++i)
        if (expected[i] != got[i]) break;
    if (i == nbytes) { if (why) *why = kWhyAccepted; return kHookExact; }
    len = AddrForeignJumpLen(got, nbytes);
    if (atEntry == 0) w = kWhyNotEntry;
    else if (use == 0) w = kWhyNoUse;
    else if (use != 1 && funcStart == 0) w = kWhyNotFunction;
    else if (expected[0] == 0xE9 || (nbytes >= 2 && expected[0] == 0xFF && expected[1] == 0x25)) w = kWhyExpectedJump;
    else if (len == 0) w = kWhyNoJump;
    else if (len == 14) w = kWhyLongJump;
    else if (nbytes - len < kHookTailMin) w = kWhyTooShort;
    else
    {
        for (i = len; i < nbytes; ++i)
            if (expected[i] != got[i]) { w = kWhyRestDiffers; break; }
        if (w == kWhyAccepted && jumpOutside == 0) w = kWhyInImage;
    }
    if (why) *why = w;
    return w == kWhyAccepted ? kHookAccepted : kHookRefuse;
}

/* T-1476 (F2011, owner 1876): THE USE THE GATE APPLIES TO A ROW, on either road (the road is no input: it does not change the
   rule). declared: the row's declared use (RowUse: 0 none, 1 call-only, 2 any other).
   Both roads apply the declared use: the signature road places each row from the executable FILE's own bytes (the file on
   disk, which no other mod can have hooked), so another mod's jump at a function's start changes only those first bytes in
   the running game - the same evidence the table road weighs, and AddrForeignVerdict decides it the same way. Before T-1476
   the signature road gave every row 0 (strict), and RE_Kenshi's jump on a call-only row refused every GOG game (F2011).
   A value other than 1 or 2 is 0 (strict). Pure. */
inline int AddrGateUse(int declared)
{
    return (declared == 1 || declared == 2) ? declared : 0;
}

/* THE SET'S USE (registrations of one row name): 1 when there is at least one and every one is call-only; 2 when there is
   at least one and any is not call-only; 0 for an empty set. */
inline int AddrUsesCombined(const unsigned char* uses, int n)
{
    if (uses == 0 || n <= 0) return 0;
    return AddrUsesAllCallOnly(uses, n) != 0 ? 1 : 2;
}

/* T-1447: WHERE A JUMP GOES - for the [ADDR] line that names the module behind another mod's jump. p: the bytes at `at`
   (n of them). Returns 1 with *dest = the destination for E9 rel32 (5 bytes), FF 25 00000000 + an 8-byte address (the
   14-byte absolute form multihook's relay uses) and 48 B8 imm64 FF E0 (mov rax / jmp rax, 12 bytes); 2 with *dest = the
   ADDRESS of the 8-byte pointer to read, for FF 25 with a non-zero distance (jmp [rip+d]); 0 for anything else or too few
   bytes. Pure. */
inline int AddrJumpDest(const unsigned char* p, int n, unsigned long long at, unsigned long long* dest)
{
    int i;
    if (p == 0 || dest == 0 || n < 5) return 0;
    if (p[0] == 0xE9)
    {
        const int rel = (int)((unsigned int)p[1] | ((unsigned int)p[2] << 8) | ((unsigned int)p[3] << 16) | ((unsigned int)p[4] << 24));
        *dest = at + 5ull + (unsigned long long)(long long)rel;
        return 1;
    }
    if (p[0] == 0xFF && p[1] == 0x25 && n >= 6)
    {
        const int rel = (int)((unsigned int)p[2] | ((unsigned int)p[3] << 8) | ((unsigned int)p[4] << 16) | ((unsigned int)p[5] << 24));
        if (rel != 0) { *dest = at + 6ull + (unsigned long long)(long long)rel; return 2; }
        if (n < 14) return 0;
        *dest = 0;
        for (i = 7; i >= 0; --i) *dest = (*dest << 8) | (unsigned long long)p[6 + i];
        return 1;
    }
    if (p[0] == 0x48 && p[1] == 0xB8 && n >= 12 && p[10] == 0xFF && p[11] == 0xE0)
    {
        *dest = 0;
        for (i = 7; i >= 0; --i) *dest = (*dest << 8) | (unsigned long long)p[2 + i];
        return 1;
    }
    return 0;
}

/* T-1447: the file-name part of a module path (after the last back or forward slash) - the log names a DLL, never a folder that may
   hold a user's name. Pure. */
inline const char* AddrFileNamePart(const char* path)
{
    const char* r = path;
    const char* q;
    if (path == 0) return "";
    for (q = path; *q != 0; ++q)
        if (*q == '\\' || *q == '/') r = q + 1;
    return r;
}

}   /* namespace coopaddr */
