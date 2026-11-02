#pragma once
// npcsense.h - NPCS ON THIS GAME TREAT A TEAMMATE'S CHARACTERS AS THIS GAME'S PLAYER'S OWN (owner decisions 1187 a, 1188: a
// teammate is treated as your own faction; a friend or a stranger as any other faction). Pure: the plugin (policy.cpp) and the
// offline suite compile this same header.
// The engine's NPC senses (run on the AI worker thread) and NPC memory have no shared "is this the player" function: each site asks
// inline whether a character's faction holds a PlayerInterface (Faction+0x250 not null). Each site's compare is replaced by a jump
// to the sight stub (coopcrime::SightStubBuild, crimewire.h) built with that compare's OWN bytes: it re-does the compare (a player
// faction: the engine's answer), else answers "a player faction" for a faction in its cells - this game's current TEAMMATES' factions
// only (never a friend or a stranger) - else re-does the compare (not a player: the engine's answer). The flags the jcc after the
// compare reads are then exactly the original's for every faction outside the cells.
// Sites 24 and 25 sit in CharMovement's 'move to a character' (0x65D9F0): site 25 is such a compare; site 24 is a CALL to the
// engine's 'counts as the player' function on the target (form kCallPlayer, below), whose stub calls that function first and
// answers "the player" for a teammate's character only where the engine answered no.
// Site 26 sits in the senses' 'inside my private building' check 0x8516F0 (called only by the sense check 0x8587B0, AI worker
// thread): its compare on the TARGET's faction is the last gate before the owner's check and event 9, so a teammate's character
// inside an NPC's private building is caught there as this game's player's own would be.
// C++03 (VS2010 v100).

namespace swnpc {

/* the compare forms. Every register form compares against a register that holds 0 at that site (read in each function: xor esi,esi
   at 0x670DC5; xor ebx,ebx at 0x6738C8; mov r15d,edi at 0x674D50 after xor edi,edi at 0x674C92 with no write between), so ZF=1
   means "no PlayerInterface" at every site, the polarity the stub's cell answer (cmp rax,0 with rax not null: ZF=0) assumes. */
enum Form { kCmpZero8 = 0, kCmpRsi7 = 1, kCmpRbx7 = 2, kCmpR15_7 = 3, kCallPlayer = 4 };
const int kForms = 4;   /* the compare forms; kCallPlayer (a call site) is not one: CallSiteOk checks it, CallStubBuild builds its stub */
inline int TestLen(int form) { return form == kCmpZero8 ? 8 : (form == kCallPlayer ? 5 : 7); }
/* the compare's own bytes: cmp qword [rax+0x250], 0 / rsi / rbx / r15 */
inline const unsigned char* TestBytes(int form)
{
    static const unsigned char t[kForms][8] = {
        { 0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00 },
        { 0x48, 0x39, 0xB0, 0x50, 0x02, 0x00, 0x00, 0x00 },
        { 0x48, 0x39, 0x98, 0x50, 0x02, 0x00, 0x00, 0x00 },
        { 0x4C, 0x39, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00 } };
    return t[(form >= 0 && form < kForms) ? form : 0];
}

/* one site: its address-table row, its compare form, the bytes after the compare (up to 10 bytes in all: the jcc, or the
   instruction between the compare and its jcc, which leaves the flags), and what it decides. The 10 bytes are the same in
   Steam 1.0.65 and 1.0.68 (capstone on both executables; the 1.0.68 site is +0xED0 for the senses, +0xA90 for the memory). A kCallPlayer site's
   next[] is the `84 C0` (test al, al) after its call; the call's rel32 differs between builds, so CallSiteOk checks that site, not
   Expected. */
struct Site { const char* row; int form; unsigned char next[3]; const char* what; };
const int kSites = 26;
const int kSenseSites = 8;   /* sites 0..7 run in the senses (AI worker thread); 8..21 in the memory; 22 (site 23, added
                                after the memory's) in the senses; 23 and 24 (sites 24, 25) in CharMovement's 'move to
                                a character' 0x65D9F0, which may run on the AI worker thread; 25 (site 26) in the
                                senses' 'inside my private building' 0x8516F0 (AI worker thread) */
inline const Site& SiteAt(int k)
{
    static const Site s[kSites] = {
        { "NpcSensePlayerTest1",  kCmpZero8, { 0x0F, 0x84, 0 }, "senses 1.0.65 0x8589B7: a seen player character raises the allied-player event 0x43" },
        { "NpcSensePlayerTest2",  kCmpZero8, { 0x74, 0x46, 0 }, "senses 1.0.65 0x858FE6: a seen player character raises the enemy-player event 0x42, else the neutral event 3" },
        { "NpcSensePlayerTest3",  kCmpZero8, { 0x74, 0x7B, 0 }, "senses 1.0.65 0x8590EC: the second place the enemy-player event 0x42 is raised (an enemy jumps straight here)" },
        { "NpcSensePlayerTest4",  kCmpZero8, { 0x0F, 0x84, 0 }, "senses 1.0.65 0x856E1B: an allied player character is seen by distance, no sneak roll" },
        { "NpcSensePlayerTest5",  kCmpZero8, { 0x0F, 0x85, 0 }, "senses 1.0.65 0x857108: the sight pass's player test (0x857200)" },
        { "NpcSensePlayerTest6",  kCmpZero8, { 0x75, 0x5C, 0 }, "senses 1.0.65 0x85714D: the sight pass's second player test (0x857200)" },
        { "NpcSensePlayerTest7",  kCmpZero8, { 0x74, 0x0E, 0 }, "senses 1.0.65 0x8573CC: the player mark 0xd" },
        { "NpcSensePlayerTest8",  kCmpZero8, { 0x74, 0x50, 0 }, "senses 1.0.65 0x8582A5: the forget timer" },
        { "NpcMemPlayerTest1",    kCmpRsi7,  { 0x74, 0x3D, 0x8B }, "memory 1.0.65 0x670E16: a record's key - the one shared player record (0x670DB0)" },
        { "NpcMemPlayerTest2",    kCmpZero8, { 0x48, 0x8D, 0 }, "memory 1.0.65 0x671006: a record's key - the one shared player record (0x670FF0)" },
        { "NpcMemPlayerTest3",    kCmpRbx7,  { 0x75, 0x1B, 0x48 }, "memory 1.0.65 0x6738FF: a new memory's key (0x673870)" },
        { "NpcMemPlayerTest4",    kCmpZero8, { 0x74, 0x35, 0 }, "memory 1.0.65 0x6739BC: a new memory's temporary-ally tag 3 (0x673870)" },
        { "NpcMemPlayerTest5",    kCmpZero8, { 0x75, 0x15, 0 }, "memory 1.0.65 0x673AD9: a record lookup (0x673AC0)" },
        { "NpcMemPlayerTest6",    kCmpZero8, { 0x48, 0x8B, 0 }, "memory 1.0.65 0x67439C: a record lookup (0x674380)" },
        { "NpcMemPlayerTest7",    kCmpR15_7, { 0x74, 0x07, 0xE8 }, "memory 1.0.65 0x674D62: a record lookup (0x674BF0)" },
        { "NpcMemPlayerTest8",    kCmpZero8, { 0x74, 0x12, 0 }, "memory 1.0.65 0x677CBB: a flag test, flag 3 always set for the player (0x677C90)" },
        { "NpcMemPlayerTest9",    kCmpZero8, { 0x74, 0x07, 0 }, "memory 1.0.65 0x677CE6: a flag test's record (0x677C90)" },
        { "NpcMemPlayerTest10",   kCmpZero8, { 0x48, 0x8B, 0 }, "memory 1.0.65 0x677D5C: a record's flags cleared (0x677D40)" },
        { "NpcMemPlayerTest11",   kCmpZero8, { 0x74, 0x14, 0 }, "memory 1.0.65 0x677DC7: tag 3 read for the player (0x677DA0)" },
        { "NpcMemPlayerTest12",   kCmpZero8, { 0x74, 0x07, 0 }, "memory 1.0.65 0x677DF4: a flag read's record (0x677DA0)" },
        { "NpcMemPlayerTest13",   kCmpZero8, { 0x74, 0x04, 0 }, "memory 1.0.65 0x677E67: tag 3 cleared for the player (0x677E40)" },
        { "NpcMemPlayerTest14",   kCmpZero8, { 0x74, 0x07, 0 }, "memory 1.0.65 0x677E84: a flag clear's record (0x677E40)" },
        { "NpcSensePlayerTest23", kCmpZero8, { 0x74, 0x1F, 0 }, "senses 1.0.65 0x8586A8: an enemy player character is put back on the NPC's watch list (0x858500), so 0x8587B0 runs for it" },
        { "NpcSensePlayerTest24", kCallPlayer, { 0x84, 0xC0, 0 }, "move to a character 1.0.65 0x65DA7D: a target that counts as the player (0x855CF0 on the target) is headed for at the mover's remembered position of it, not its true one (0x65D9F0)" },
        { "NpcSensePlayerTest25", kCmpZero8, { 0x0F, 0x84, 0 }, "move to a character 1.0.65 0x65DADD: the end point is moved a fixed distance for a player target (0x65D9F0)" },
        { "NpcSensePlayerTest26", kCmpZero8, { 0x74, 0x14, 0 }, "inside my private building 1.0.65 0x8517F4: a player target inside the looker's private building is caught (event 9 from 0x8587B0) unless the owner's vt+0x290 says no (0x8516F0)" } };
    return s[(k >= 0 && k < kSites) ? k : 0];
}
/* the 10 bytes a compare site must hold before it is patched: the compare, then the bytes after it. A call site has no fixed bytes
   (its rel32 is the build's own): 0, and CallSiteOk checks it. */
const int kCheckLen = 10;
inline int Expected(int k, unsigned char* out)
{
    const Site& s = SiteAt(k);
    if (s.form == kCallPlayer) return 0;
    const int L = TestLen(s.form);
    const unsigned char* t = TestBytes(s.form);
    for (int i = 0; i < L; ++i) out[i] = t[i];
    for (int i = L; i < kCheckLen; ++i) out[i] = s.next[i - L];
    return kCheckLen;
}

/* THE CALL FORM (kCallPlayer). The site is a direct call `E8 rel32` to the engine's 'counts as the player' function (1.0.65
   0x855CF0, 1.0.68 0x856BC0; row NpcCountsAsPlayer) with the target character in rcx, then `test al, al` (84 C0). The function
   reads the character's faction through the character's vtable +0x58 and answers 1 for a faction holding a PlayerInterface
   (Faction+0x250), else asks further. The call reaches it through one `E9 rel32` jump (the linker's jump table: 1.0.65 0x4110A,
   1.0.68 0x41132) or directly. */
const int kCallLen = 5;
const int kCallCheckLen = 7;   /* the call, then 84 C0 */
inline long long Rel32At(const unsigned char* p)
{
    const unsigned u = (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
    return (long long)(int)u;
}
/* the call's target (site + 5 + rel32); 0 when the site's first byte is not E8 */
inline unsigned long long CallDest(const unsigned char* at, unsigned long long site)
{
    return at[0] == 0xE8 ? site + 5ULL + (unsigned long long)Rel32At(at + 1) : 0ULL;
}
/* `at` = the 7 bytes at the site, `hop` = the 5 bytes at the call's target, `callee` = the function's address in this build:
   true when the site is `E8 rel32; 84 C0` and its call reaches the callee directly or through one E9 jump */
inline bool CallSiteOk(const unsigned char* at, unsigned long long site, const unsigned char* hop, unsigned long long callee)
{
    if (at == 0 || callee == 0 || at[0] != 0xE8 || at[5] != 0x84 || at[6] != 0xC0) return false;
    const unsigned long long dest = CallDest(at, site);
    if (dest == callee) return true;
    return hop != 0 && hop[0] == 0xE9 && dest + 5ULL + (unsigned long long)Rel32At(hop + 1) == callee;
}
inline void CallPut32(unsigned char* at, int v)
{
    const unsigned u = (unsigned)v;
    at[0] = (unsigned char)u; at[1] = (unsigned char)(u >> 8); at[2] = (unsigned char)(u >> 16); at[3] = (unsigned char)(u >> 24);
}
/* The call form's stub, called by the site's call in place of the engine's function (cells at cellsOff: hits at +0, cell k at
   +8+8k, the sight stub's layout). Entered with rsp = 8 mod 16, as any function; rsp is 16-aligned at both of its calls. Its two calls
   clobber the scratch registers (rax, rcx, rdx, r8-r11, xmm0-5, flags) exactly as the original call does; rcx comes back as the target. N = 45 + 13 * cells.
 *    0  51                        push rcx                      the target character
 *    1  48 83 EC 20               sub rsp, 0x20                 the callee's home space
 *    5  48 B8 <abs64>             mov rax, callTo               the site's own call target
 *   15  FF D0                     call rax                      the engine's answer
 *   17  84 C0                     test al, al
 *   19  0F 85 <rel32>             jnz DONE                      counts as the player: the engine's answer
 *   25  48 8B 4C 24 20            mov rcx, [rsp+0x20]           the target again
 *   30  48 8B 01                  mov rax, [rcx]
 *   33  FF 50 58                  call [rax+0x58]               its faction, read as the engine's function reads it
 *   36  48 85 C0                  test rax, rax
 *   39  0F 84 <rel32>             jz NO                         no faction: never a teammate (an empty cell holds 0)
 *   45+13k 48 3B 05 <rel32>       cmp rax, [rip -> cell k]
 *          0F 84 <rel32>          je HIT
 *    N  31 C0                     NO: xor eax, eax              al = 0, the engine's answer
 *  N+2  EB 0A                     jmp DONE
 *  N+4  F0 48 FF 05 <rel32>       HIT: lock inc qword [rip -> hits]
 * N+12  B0 01                     mov al, 1                     a teammate's character counts as the player
 * N+14  48 83 C4 20               DONE: add rsp, 0x20
 * N+18  59                        pop rcx
 * N+19  C3                        ret
 * Returns the length, CallStubLenFor(cells) = 65 + 13 * cells. */
inline int CallStubLenFor(int cells) { return 65 + 13 * cells; }
inline int CallStubBuild(unsigned char* p, unsigned long long callTo, int cells, int cellsOff)
{
    const int N = 45 + 13 * cells, hit = N + 4, done = N + 14;
    p[0] = 0x51;
    p[1] = 0x48; p[2] = 0x83; p[3] = 0xEC; p[4] = 0x20;
    p[5] = 0x48; p[6] = 0xB8;
    for (int i = 0; i < 8; ++i) p[7 + i] = (unsigned char)(callTo >> (8 * i));
    p[15] = 0xFF; p[16] = 0xD0;
    p[17] = 0x84; p[18] = 0xC0;
    p[19] = 0x0F; p[20] = 0x85; CallPut32(p + 21, done - 25);
    p[25] = 0x48; p[26] = 0x8B; p[27] = 0x4C; p[28] = 0x24; p[29] = 0x20;
    p[30] = 0x48; p[31] = 0x8B; p[32] = 0x01;
    p[33] = 0xFF; p[34] = 0x50; p[35] = 0x58;
    p[36] = 0x48; p[37] = 0x85; p[38] = 0xC0;
    p[39] = 0x0F; p[40] = 0x84; CallPut32(p + 41, N - 45);
    for (int k = 0; k < cells; ++k)
    {
        const int o = 45 + 13 * k;
        unsigned char* q = p + o;
        q[0] = 0x48; q[1] = 0x3B; q[2] = 0x05; CallPut32(q + 3, cellsOff + 8 + 8 * k - (o + 7));
        q[7] = 0x0F; q[8] = 0x84; CallPut32(q + 9, hit - (o + 13));
    }
    p[N] = 0x31; p[N + 1] = 0xC0;
    p[N + 2] = 0xEB; p[N + 3] = (unsigned char)(done - (N + 4));
    p[hit] = 0xF0; p[hit + 1] = 0x48; p[hit + 2] = 0xFF; p[hit + 3] = 0x05; CallPut32(p + hit + 4, cellsOff - (hit + 8));
    p[hit + 8] = 0xB0; p[hit + 9] = 0x01;
    p[done] = 0x48; p[done + 1] = 0x83; p[done + 2] = 0xC4; p[done + 3] = 0x20;
    p[done + 4] = 0x59;
    p[done + 5] = 0xC3;
    return done + 6;
}

/* this game's teammates' factions (the team table's, as team.cpp CellsTick gives them) for the cells: nulls and repeats left out,
   at most `cap`; those past it are counted in *dropped. Friends and strangers never reach this list. Returns the count written. */
inline int MateList(const void* const* facs, int n, const void** out, int cap, int* dropped)
{
    int w = 0, d = 0;
    for (int k = 0; k < n && facs != 0; ++k)
    {
        const void* f = facs[k];
        if (f == 0) continue;
        bool seen = false;
        for (int i = 0; i < w; ++i) if (out[i] == f) { seen = true; break; }
        if (seen) continue;
        if (w < cap) out[w++] = f; else ++d;
    }
    if (dropped != 0) *dropped = d;
    return w;
}

/* the log rule: a site's hit count is logged when it has risen since its last line, at most kSayCap lines per site */
const int kSayCap = 3;
inline bool SayNow(long long hits, long long saidAt, int said) { return said < kSayCap && hits > saidAt; }

}
