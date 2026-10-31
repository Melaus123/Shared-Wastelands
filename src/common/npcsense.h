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
// C++03 (VS2010 v100).

namespace swnpc {

/* the compare forms. Every register form compares against a register that holds 0 at that site (read in each function: xor esi,esi
   at 0x670DC5; xor ebx,ebx at 0x6738C8; mov r15d,edi at 0x674D50 after xor edi,edi at 0x674C92 with no write between), so ZF=1
   means "no PlayerInterface" at every site, the polarity the stub's cell answer (cmp rax,0 with rax not null: ZF=0) assumes. */
enum Form { kCmpZero8 = 0, kCmpRsi7 = 1, kCmpRbx7 = 2, kCmpR15_7 = 3 };
const int kForms = 4;
inline int TestLen(int form) { return form == kCmpZero8 ? 8 : 7; }
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
   Steam 1.0.65 and 1.0.68 (capstone on both executables; the 1.0.68 site is +0xED0 for the senses, +0xA90 for the memory). */
struct Site { const char* row; int form; unsigned char next[3]; const char* what; };
const int kSites = 22;
const int kSenseSites = 8;   /* sites 0..7 run in the senses (AI worker thread); 8..21 in the memory */
inline const Site& SiteAt(int k)
{
    static const Site s[kSites] = {
        { "NpcSensePlayerTest1",  kCmpZero8, { 0x0F, 0x84, 0 }, "senses 1.0.65 0x8589B7: a seen player character raises the allied-player event 0x43" },
        { "NpcSensePlayerTest2",  kCmpZero8, { 0x74, 0x46, 0 }, "senses 1.0.65 0x858FE6: a seen player character raises the enemy-player event 0x42" },
        { "NpcSensePlayerTest3",  kCmpZero8, { 0x74, 0x7B, 0 }, "senses 1.0.65 0x8590EC: a seen player character, else the neutral event 3" },
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
        { "NpcMemPlayerTest14",   kCmpZero8, { 0x74, 0x07, 0 }, "memory 1.0.65 0x677E84: a flag clear's record (0x677E40)" } };
    return s[(k >= 0 && k < kSites) ? k : 0];
}
/* the 10 bytes the site must hold before it is patched: the compare, then the bytes after it; 10 always */
const int kCheckLen = 10;
inline int Expected(int k, unsigned char* out)
{
    const Site& s = SiteAt(k);
    const int L = TestLen(s.form);
    const unsigned char* t = TestBytes(s.form);
    for (int i = 0; i < L; ++i) out[i] = t[i];
    for (int i = L; i < kCheckLen; ++i) out[i] = s.next[i - L];
    return kCheckLen;
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
