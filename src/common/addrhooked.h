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

}   /* namespace coopaddr */
