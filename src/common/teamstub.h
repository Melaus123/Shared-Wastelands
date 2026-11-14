#pragma once
/* T-1128 (owner 1188, 1194 a): the engine bytes policy.cpp's teammate patches for a teammate's house and research bench expect at
 * their sites, and the teammate cells' write, shared by the plugin (its policy.cpp) and the offline tests
 * (their test_main.cpp) so the two can never disagree. Pure: no engine, no Windows header.
 *
 * The house: the host test 0x4D5F30's owner compare (1.0.65 0x4D6222 / 1.0.68 0x4D6CB2), `cmp qword [rax+0x250], r12` (r12 = 0)
 * then `jne +0x95` to `mov al,1` - all 13 bytes are checked; the stub replaces the first 7 (the compare) and returns to the jne.
 * The research bench: ResearchBuilding::operate's two owner tests (1.0.65 0x29B8D3 / 0x29B90B, 1.0.68 0x29B913 / 0x29B94B),
 * `cmp qword [rax+0x250], 0` then `je` (0F 84 rel32) to the function's end - the test and the je's opcode are checked; the stub
 * replaces the 8-byte test and returns to the je. */
namespace teamstub {

const unsigned char kHouseOwnerOrig[13] = { 0x4C, 0x39, 0xA0, 0x50, 0x02, 0x00, 0x00, 0x0F, 0x85, 0x95, 0x00, 0x00, 0x00 };
const int kHouseOwnerTestLen = 7;   /* the compare the stub re-does; the jne after it stays */
const int kHouseOwnerOrigLen = 13;
const unsigned char kResearchTestOrig[10] = { 0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00, 0x0F, 0x84 };
const int kResearchTestLen = 8;     /* the test the stub re-does; the je after it stays */
const int kResearchOrigLen = 10;

/* T-1150 (owner 1491 a): the engine's 'nearest finished building that takes item kind X' search (1.0.65 0x59A240 / 1.0.68
 * 0x59ACD0; hauling 0x997A80 at 0x997B17 / 0x997B9A and the orders object's 0x5A2200 / 0x5A4F10 call it) takes, for a searcher that counts as the
 * player, a building of another faction only when that faction is a player faction: at 1.0.65 0x59A39A / 1.0.68 0x59AE2A
 * `cmp qword [rax+0x250], 0` (rax = the building's faction, building vt+0x58) then `je` (0F 84 rel32, +0xBC) to the skip.
 * The same 10 bytes as the research bench's tests; checked separately so either can change alone. The stub replaces the 8-byte
 * test and returns to the je at site + 8. */
const unsigned char kHaulSearchTestOrig[10] = { 0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00, 0x0F, 0x84 };
const int kHaulSearchTestLen = 8;   /* the test the stub re-does; the je after it stays */
const int kHaulSearchOrigLen = 10;
/* the je's rel32 at both builds (0xBC: to 1.0.65 0x59A464 / 1.0.68 0x59AEF4, the search loop's 'next building'); the 16-byte table
 * rows hold it, the patch does not check it (only the test and the je's opcode) */
const int kHaulSearchSkipRel = 0xBC;

/* The teammate cells of one stub (cell k at cells[k], cap cells) set to the first n factions in facs, the rest 0; a cell is written
 * only when it changes (a stub on another thread reads them). Returns the number of non-zero factions held. */
inline int MateCellsWrite(void* volatile* cells, int cap, void* const* facs, int n)
{
    int held = 0;
    for (int k = 0; k < cap; ++k)
    {
        void* want = (k < n && facs != 0) ? facs[k] : 0;
        if (want != 0) ++held;
        if (cells[k] != want) cells[k] = want;
    }
    return held;
}

/* 1 when fac is non-zero and held in one of the cap cells (any thread: each cell is read once, as the stubs read them) */
inline int MateCellsHas(void* const volatile* cells, int cap, const void* fac)
{
    if (fac == 0 || cells == 0) return 0;
    for (int k = 0; k < cap; ++k) { void* c = cells[k]; if (c != 0 && c == fac) return 1; }
    return 0;
}

/* T-1150 second gate: the engine's 'may this searcher use this building' owner filter (1.0.65 0x6207A0 / 1.0.68 0x621230) drops,
 * before the patched compare at 0x59A39A ever sees it, any other faction's building that has an inventory - a teammate's box too.
 * The answer the hooked filter gives: the engine's own when it keeps the building; otherwise kept only while the storage search's
 * candidate gather runs on this thread (inSearch) and the building's owner faction is a teammate's (mate). */
inline int HaulPoolPass(int engineKeeps, int inSearch, int mate)
{
    if (engineKeeps != 0) return 1;
    return (inSearch != 0 && mate != 0) ? 1 : 0;
}

/* T-1250 (parity with T-1150, owner 1187 a / 1499): the same filter is asked by the engine's other building finders - the nearest
 * allies' building 0x8C9700, the special-function building (machines) 0x8CDD10, the building scorer 0x8CF8A0, the turrets finder
 * 0x8DA0A0, the cages finder 0x8DA640 (building function type 8 = a cage) and the area gathers 0x9FF7C0 / 0x9FAB00 when their caller
 * hands them an ownership record (the haul gather 0x8CAFD0 and the prisoner gatherings' cage checks 0x290B30 / 0x295AD0; the
 * gathers' other callers hand none and get the relation test 0x6B22E0 instead, never this filter); the table search 0x8C97A0 refuses
 * on its own test whatever the filter says - and each dropped a teammate's building with an inventory. The hooked filter's answer
 * when the engine drops a building (engineKeeps 0):
 *   kMayUseKeptSearch  the storage search's gather marks this thread (inSearch) and the owner is a teammate's (mate) - T-1150,
 *                      counted on its own
 *   kMayUseKeptJob     any other caller, when the owner is a teammate's AND the ownership record asked about is this game's own
 *                      player faction's (askerIsPlayer: record+0x70 holds a PlayerInterface) - so an NPC's, a stand-in's (a
 *                      teammate's copy here) and every other faction's question keeps the engine's answer - and the caller is not
 *                      the bed cost 0x330A70 (bedCost: a pass there goes on to the town's 'public beds' test - may not use
 *                      where the town sells none, else free when 0x791D20(town+0xD0) answers 0, else 'bed usage cost'; a
 *                      teammate's bed is not this row's)
 *   kMayUseEngine      otherwise: the engine's answer stands (a kept building is always the engine's own answer) */
const int kMayUseEngine = 0;
const int kMayUseKeptSearch = 1;
const int kMayUseKeptJob = 2;
inline int MayUsePass(int engineKeeps, int inSearch, int mate, int askerIsPlayer, int bedCost)
{
    if (engineKeeps != 0 || mate == 0) return kMayUseEngine;
    if (HaulPoolPass(engineKeeps, inSearch, mate) != 0) return kMayUseKeptSearch;
    return (askerIsPlayer != 0 && bedCost == 0) ? kMayUseKeptJob : kMayUseEngine;
}

/* T-1250: the eating search BuildingFinder_LootFoodAndStuff (1.0.65 0x8D9D50 / 1.0.68 0x8DAC20; rbp = the finder, rsi = the
 * building looked at, [rbp+0x28] = the hungry character) does not ask the filter above; it keeps a building only when its owner
 * ([rsi+0x10]) is the eater's own faction, else only for an eater whose faction is not a player's (Read, capstone on both Steam
 * builds, 1.0.65 shown; 1.0.68 0x8DAC94 / 0x8DACA4 the same bytes):
 *     0x8D9DC1  call [rax+0x58]                       the eater's faction
 *     0x8D9DC4  48 39 46 10                           cmp [rsi+0x10], rax     the building's owner is the eater's faction?
 *     0x8D9DC8  74 18                                 je 0x8D9DE2             yes: kept
 *     0x8D9DD1  call [rax+0x58]                       the eater's faction again (rax)
 *     0x8D9DD4  48 83 B8 50 02 00 00 00               cmp qword [rax+0x250], 0   the eater's faction has a PlayerInterface?
 *     0x8D9DDC  0F 85 F0 01 00 00                     jne 0x8D9FD2            yes (ZF=0): skipped - a player eats only from its own
 *     0x8D9DE2  ...                                   kept (ZF=1)
 * So ZF=1 means "kept" here - the opposite polarity of the box / research / haul tests, and the teammate is the building's owner in
 * rsi, not the faction in rax. The 8-byte test is replaced (E9 rel32 + 3 NOPs, ending at the jne) by this stub on its own page:
 *     0  48 83 B8 50 02 00 00 00   cmp qword [rax+0x250], 0     the original test
 *     8  0F 84 <rel32>             je BACK                      not a player's eater: ZF=1, the original answer
 *    14  50                        push rax                     (the eater's faction, put back on every road below)
 *    15  48 8B 46 10               mov rax, [rsi+0x10]          the building's owner faction
 *    19  48 85 C0                  test rax, rax
 *    22  0F 84 <rel32>             je NOTMATE                   no owner: never a teammate (empty cells hold 0)
 *    28  per cell k, at 28+13k:
 *          48 3B 05 <rel32>        cmp rax, [rip -> cell k]
 *          0F 84 <rel32>           je HIT
 *     N  58                        NOTMATE: pop rax             (N = 28 + 13 * cells)
 *   N+1  48 83 B8 50 02 00 00 00   cmp qword [rax+0x250], 0     the original test again: ZF=0, skipped as before
 *   N+9  EB 0C                     jmp BACK
 *  N+11  58                        HIT: pop rax
 *  N+12  F0 48 FF 05 <rel32>       lock inc qword [rip -> hits]
 *  N+20  48 39 C0                  cmp rax, rax                 ZF=1: kept, as for the eater's own building
 *  N+23  FF 25 00000000 <abs64>    BACK: jmp [rip] -> site + 8 (the jne, left in place)
 * It reads rax, rsi and its own page, calls nothing and leaves every register as it found it (only the flags change, and only the
 * jne reads them: the next instruction 0x8D9DE2 writes rax and calls). cellsOff = the cells' offset on the page (hits at cellsOff,
 * cell k at cellsOff + 8 + 8k, the box stubs' layout). Returns the length, EatStubLenFor(cells). */
const unsigned char kFoodSearchTestOrig[10] = { 0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00, 0x0F, 0x85 };
const int kFoodSearchTestLen = 8;   /* the test the stub re-does; the jne after it stays */
const int kFoodSearchOrigLen = 10;
inline int EatStubLenFor(int cells) { return 65 + 13 * cells; }
inline void EatPut32(unsigned char* at, int v)
{
    const unsigned int u = (unsigned int)v;
    at[0] = (unsigned char)(u & 0xFF); at[1] = (unsigned char)((u >> 8) & 0xFF); at[2] = (unsigned char)((u >> 16) & 0xFF); at[3] = (unsigned char)((u >> 24) & 0xFF);
}
inline int EatStubBuild(unsigned char* p, unsigned long long site, int cells, int cellsOff)
{
    const int N = 28 + 13 * cells;
    const int hit = N + 11, back = hit + 12;
    for (int i = 0; i < kFoodSearchTestLen; ++i) p[i] = kFoodSearchTestOrig[i];
    p[8] = 0x0F; p[9] = 0x84; EatPut32(p + 10, back - 14);
    p[14] = 0x50;
    p[15] = 0x48; p[16] = 0x8B; p[17] = 0x46; p[18] = 0x10;
    p[19] = 0x48; p[20] = 0x85; p[21] = 0xC0;
    p[22] = 0x0F; p[23] = 0x84; EatPut32(p + 24, N - 28);
    for (int k = 0; k < cells; ++k)
    {
        const int o = 28 + 13 * k;
        unsigned char* q = p + o;
        q[0] = 0x48; q[1] = 0x3B; q[2] = 0x05; EatPut32(q + 3, cellsOff + 8 + 8 * k - (o + 7));
        q[7] = 0x0F; q[8] = 0x84; EatPut32(q + 9, hit - (o + 13));
    }
    p[N] = 0x58;
    for (int i = 0; i < kFoodSearchTestLen; ++i) p[N + 1 + i] = kFoodSearchTestOrig[i];
    p[N + 9] = 0xEB; p[N + 10] = (unsigned char)(back - (N + 11));
    p[hit] = 0x58;
    p[hit + 1] = 0xF0; p[hit + 2] = 0x48; p[hit + 3] = 0xFF; p[hit + 4] = 0x05; EatPut32(p + hit + 5, cellsOff - (hit + 9));
    p[hit + 9] = 0x48; p[hit + 10] = 0x39; p[hit + 11] = 0xC0;
    p[back] = 0xFF; p[back + 1] = 0x25; EatPut32(p + back + 2, 0);
    const unsigned long long backTo = site + (unsigned long long)kFoodSearchTestLen;
    for (int i = 0; i < 8; ++i) p[back + 6 + i] = (unsigned char)((backTo >> (8 * i)) & 0xFF);
    return back + 14;
}

/* T-1250 fix attempt 2 (design A, 2026-11-07): a teammate's outpost counts as this player's town at the two engine gates that asked
 * for this game's OWN player faction (Read, decomp 1.0.65; bytes Confirmed on both Steam builds). A teammate's outpost is shown on
 * this game under that teammate's stand-in faction, so neither gate ever said yes for it.
 * GATE 1, the character's town update 0x5A4750 (1.0.68 0x5A51E0): its call at 0x5A47D8 (1.0.68 0x5A5268) asks the town list's
 *   nearest town 0x927040 (1.0.68 0x927F10) of exactly this game's player faction ([PlayerInterface+0x2A0]); that town becomes the
 *   character's town (+0x88 of its AI data) when the character stands inside it at 1.25x its radius (0x925E80). Otherwise the
 *   character KEEPS its old town (Sho-Battai in run T2167) while inside that one at 1.5x. The fix asks the same search once per
 *   teammate faction when no own town covers the spot; TeamTownTakeMate below is the rule, TeamTownNearer the choice among them.
 * GATE 2, 'Getting food''s need 0x59B140 (1.0.68 0x59BBD0), for a town whose owner (vt+0x58) is not the character's faction:
 *     0x59B25A  41 FF 90 C0 02 00 00   call [r8+0x2C0]          0x951190: town+0x10 == this game's player faction, or that
 *                                                                 faction in the town's set (+0xE8)
 *     0x59B261  84 C0                  test al, al
 *     0x59B263  0F 84 FE FE FF FF      je 0x59B167              no: the need answers 0 (xorps xmm0 / the epilogue)
 *     0x59B269  48 83 BE 28 01 00 00 00  cmp qword [rsi+0x128], 0   yes: on to the hunger need
 *   rsi = the town (1.0.68 0x59BCF1 / 0x59BCF3 / 0x59BBF7 / 0x59BCF9, the same bytes). TeamTownGate2 is the rule. */
const unsigned char kTownGateTestOrig[8] = { 0x84, 0xC0, 0x0F, 0x84, 0xFE, 0xFE, 0xFF, 0xFF };
const int kTownGateOrigLen = 8;     /* test al,al / je rel32: both replaced; the stub jumps to either target itself */
/* gate 1: 1 = the teammate's town is the answer - only when no town of this game's own player faction covers the spot and a
 * teammate's does; else the engine's own answer */
inline int TeamTownTakeMate(int ownCovers, int mateCovers) { return (ownCovers == 0 && mateCovers != 0) ? 1 : 0; }
/* gate 1, among several teammates' towns that cover the spot: 1 = the candidate at squared distance d2 replaces the best so far
 * (the nearest wins, as the engine's search picks the nearest; the first of two at the same distance stays) */
inline int TeamTownNearer(int haveBest, float bestD2, float d2) { return (haveBest == 0 || d2 < bestD2) ? 1 : 0; }
/* gate 2, A MODEL of the stub's rule for the tests (the plugin never calls this - the real gate is TownGateStubBuild's code): the
 * town passes when the engine said yes, or when its owner field (+0x10) is a teammate's faction (a teammate cell) */
inline int TeamTownGate2(int engineYes, int ownerIsMate) { return (engineYes != 0 || ownerIsMate != 0) ? 1 : 0; }
/* gate 2's stub, on its own page; the 8 bytes at the site become E9 rel32 + 3 NOPs:
 *     0  F0 48 FF 05 <rel32>       lock inc qword [rip -> calls]   every ask (calls at cellsOff + 8 + 8*cells, after the cells)
 *     8  84 C0                     test al, al                    the original test
 *    10  0F 85 <rel32>             jne PASS                       the engine said yes: its answer
 *    16  50                        push rax
 *    17  48 8B 46 10               mov rax, [rsi+0x10]            the town's owner faction (the field 0x951190 compares)
 *    21  48 85 C0                  test rax, rax
 *    24  0F 84 <rel32>             je NOTMATE                     no owner: never a teammate (empty cells hold 0)
 *    30  per cell k, at 30+13k:
 *          48 3B 05 <rel32>        cmp rax, [rip -> cell k]
 *          0F 84 <rel32>           je HIT
 *     N  58                        NOTMATE: pop rax               (N = 30 + 13 * cells)
 *   N+1  EB 0B                     jmp REFUSE
 *   N+3  58                        HIT: pop rax
 *   N+4  F0 48 FF 05 <rel32>       lock inc qword [rip -> hits]
 *  N+12  EB 0E                     jmp PASS
 *  N+14  FF 25 00000000 <abs64>    REFUSE: jmp [rip] -> the je's own target (refuseTo)
 *  N+28  FF 25 00000000 <abs64>    PASS: jmp [rip] -> site + 8 (the instruction after the je)
 * It reads al, rsi and its own page, calls nothing and leaves every register as it found it; only the flags change, and neither
 * target reads them (0x59B167 xorps, 0x59B269 cmp). cellsOff = the cells' offset on the page (hits at cellsOff, cell k at
 * cellsOff + 8 + 8k, the box stubs' layout). Returns the length, TownGateStubLenFor(cells). */
inline int TownGateStubLenFor(int cells) { return 72 + 13 * cells; }
inline int TownGateStubBuild(unsigned char* p, unsigned long long site, unsigned long long refuseTo, int cells, int cellsOff)
{
    const int N = 30 + 13 * cells;
    const int hit = N + 3, refuse = N + 14, pass = N + 28;
    const int callsOff = cellsOff + 8 + 8 * cells;
    p[0] = 0xF0; p[1] = 0x48; p[2] = 0xFF; p[3] = 0x05; EatPut32(p + 4, callsOff - 8);
    p[8] = 0x84; p[9] = 0xC0;
    p[10] = 0x0F; p[11] = 0x85; EatPut32(p + 12, pass - 16);
    p[16] = 0x50;
    p[17] = 0x48; p[18] = 0x8B; p[19] = 0x46; p[20] = 0x10;
    p[21] = 0x48; p[22] = 0x85; p[23] = 0xC0;
    p[24] = 0x0F; p[25] = 0x84; EatPut32(p + 26, N - 30);
    for (int k = 0; k < cells; ++k)
    {
        const int o = 30 + 13 * k;
        unsigned char* q = p + o;
        q[0] = 0x48; q[1] = 0x3B; q[2] = 0x05; EatPut32(q + 3, cellsOff + 8 + 8 * k - (o + 7));
        q[7] = 0x0F; q[8] = 0x84; EatPut32(q + 9, hit - (o + 13));
    }
    p[N] = 0x58;
    p[N + 1] = 0xEB; p[N + 2] = (unsigned char)(refuse - (N + 3));
    p[hit] = 0x58;
    p[hit + 1] = 0xF0; p[hit + 2] = 0x48; p[hit + 3] = 0xFF; p[hit + 4] = 0x05; EatPut32(p + hit + 5, cellsOff - (hit + 9));
    p[hit + 9] = 0xEB; p[hit + 10] = (unsigned char)(pass - (hit + 11));
    p[refuse] = 0xFF; p[refuse + 1] = 0x25; EatPut32(p + refuse + 2, 0);
    for (int i = 0; i < 8; ++i) p[refuse + 6 + i] = (unsigned char)((refuseTo >> (8 * i)) & 0xFF);
    const unsigned long long passTo = site + (unsigned long long)kTownGateOrigLen;
    p[pass] = 0xFF; p[pass + 1] = 0x25; EatPut32(p + pass + 2, 0);
    for (int i = 0; i < 8; ++i) p[pass + 6 + i] = (unsigned char)((passTo >> (8 * i)) & 0xFF);
    return pass + 14;
}

/* T-1122 (owner 1499: a teammate imitates being in the same faction): three engine tests on a raid's road ask a faction's find-near
 * (1.0.65 0x6BA4D0 / 1.0.68 0x6BA820, vtable slot 0: it searches that faction's own squads only) for the town owner's faction:
 * 1 announce - the announcement's owner test (0x354930 / 0x354990): the talk starts only if someone is found near the speaker;
 * 2 assaultStart - Package_PassingByTownAssault slot 12 (0x290410 / 0x290450): the assault on a player's town starts only if someone
 *   is found near the town; 3 assaultTargets - the same package's slot 6 (0x290080 / 0x2900C0): the raiders' target list is
 *   filled from what is found near the town. The plugin (policy.cpp) repeats the same search for the owner's teammates' factions.
 * These are its pure decisions. */
const int kRaidSiteAnnounce = 1, kRaidSiteAssaultStart = 2, kRaidSiteAssaultTargets = 3, kRaidSites = 3;
inline const char* RaidSiteName(int site)
{
    return site == kRaidSiteAnnounce ? "announce" : site == kRaidSiteAssaultStart ? "assaultStart" : site == kRaidSiteAssaultTargets ? "assaultTargets" : "?";
}
/* 1 when the search is repeated for teammates under a site's mark: the two yes / no tests (announce, assaultStart) only when the
 * engine's own search found nobody; the target list always (a teammate is a target as the owner's own characters are) */
inline int RaidNearWiden(int site, int engineFound)
{
    if (site < 1 || site > kRaidSites) return 0;
    if (site == kRaidSiteAssaultTargets) return 1;
    return engineFound == 0 ? 1 : 0;
}
/* the yes / no tests stop at the first teammate's faction that finds someone; the target list takes every teammate's */
inline int RaidNearStopAtFirst(int site)
{
    return site == kRaidSiteAssaultTargets ? 0 : 1;
}
/* the find-near call is the marked site's own: its return address lies inside that site's function (start, start + len] */
inline int RaidNearCallInside(unsigned long long ret, unsigned long long start, unsigned long long len)
{
    return (start != 0 && ret > start && ret <= start + len) ? 1 : 0;
}
/* T-1122 (owner 1499, manager decision 2026-11-05): the assault-start test 0x290410 (1.0.68 0x290450) goes on only for a town whose
 * faction is a player faction: at 1.0.65 0x290484 / 1.0.68 0x2904C4 `cmp qword [rax+0x250], 0` (rax = the town's faction, town
 * vt+0x58) then `je` (0F 84 rel32, +0x126) to `return 0`. The same 10 bytes as the research bench's and the haul search's tests;
 * checked on their own. The stub replaces the 8-byte test and returns to the je at site + 8. */
const unsigned char kAssaultTownTestOrig[10] = { 0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00, 0x0F, 0x84 };
const int kAssaultTownTestLen = 8;   /* the test the stub re-does; the je after it stays */
const int kAssaultTownOrigLen = 10;
/* A MODEL of the stub's rule for the tests: the plugin never calls this - the real gate is the machine-code stub Test8Patch builds
 * (coopcrime::SightStubBuild; its bytes for this site are checked in the offline tests).
 * The gate's answer with the stub: 'a player's town' when the town's faction has a PlayerInterface (this game's own player faction)
 * or is in the teammate cells (a stand-in of a player in this game's player's team); any other faction (an NPC's, a non-team
 * player's stand-in) as before */
inline int AssaultTownGate(int hasPlayerInterface, int teammateCell)
{
    return (hasPlayerInterface != 0 || teammateCell != 0) ? 1 : 0;
}
/* the team rule (team.cpp TeamSameAnyThread's own rule on its slot index): two different players' slots, both in the same team
 * (team number not 0); a slot of -1 is no player's (an NPC faction) */
inline int RaidNearSameTeam(int ownerSlot, unsigned ownerTeam, int slot, unsigned team)
{
    if (ownerSlot < 0 || slot < 0 || slot == ownerSlot) return 0;
    return (ownerTeam != 0 && team == ownerTeam) ? 1 : 0;
}
/* which of n candidate factions the search is repeated for, in their order: candSlot[k] the slot of candidate k's player (-1 none),
 * candTeam[k] that slot's team number; out[] gets their indices, at most cap; returns how many. An owner of no player's (an NPC
 * town: ownerSlot -1) or in no team (ownerTeam 0) gets none. */
inline int RaidNearPick(int ownerSlot, unsigned ownerTeam, const int* candSlot, const unsigned* candTeam, int n, int* out, int cap)
{
    if (ownerSlot < 0 || ownerTeam == 0 || candSlot == 0 || candTeam == 0 || out == 0) return 0;
    int m = 0;
    for (int k = 0; k < n && m < cap; ++k)
        if (RaidNearSameTeam(ownerSlot, ownerTeam, candSlot[k], candTeam[k]) != 0) out[m++] = k;
    return m;
}

/* T-1309 (owner 1187 a, 512): the engine's "is this object mine" test isObjectMine (1.0.65 0x5A3180 / 1.0.68 0x5A3C10; its only
 * reference is the task-condition registration at 1.0.65 0x61553D, condition 0x46, asked by the task-start check 0x60EEB0 for the
 * lock / unlock tasks; the bash / smash tasks ask it negated) ends, when no earlier road answered, with the faction compare (Read,
 * build/decomp_5a3180.txt:69-75; capstone on both Steam builds, 1.0.65 shown, 1.0.68 0x5A3DD1 the same 16 bytes):
 *     0x5A3332  call [rax+0x58]          the object's faction -> rdi (rsi = the object, resolved from the handle)
 *     0x5A333E  call [rdx+0x58]          the asking character's faction -> rax
 *     0x5A3341  48 3B C7                 cmp rax, rdi
 *     0x5A3344  0F 94 C0                 sete al                the answer: mine only when the factions are the same
 *     0x5A3347  48 8B 6C 24 48 ...       the epilogue (rbp, rbx, rdi restored from the stack; also the target of the two earlier
 *                                        answers' jumps, which stays untouched)
 * A teammate's gate or door is owned on this game by that teammate's stand-in faction, so the compare said no and the unlock order
 * failed (T2268). The 6 bytes (cmp / sete) become E9 rel32 + 1 NOP to this stub on its own page:
 *     0  48 3B C7                cmp rax, rdi            the engine's own compare
 *     3  74 17                   je YES                  the same faction: the engine's yes, nothing else asked
 *     5  48 89 C1                mov rcx, rax            the asker's faction
 *     8  48 89 FA                mov rdx, rdi            the object's faction
 *    11  49 89 F0                mov r8, rsi             the object
 *    14  48 B8 <abs64>           mov rax, helper         policy.cpp's MineTeamAsk (any thread): the team rule below
 *    24  FF D0                   call rax                al = its answer
 *    26  EB 02                   jmp BACK
 *    28  B0 01                   YES: mov al, 1
 *    30  FF 25 00000000 <abs64>  BACK: jmp [rip] -> site + 6 (the epilogue)
 * At the site rsp is 16-aligned with the function's own 0x20 outgoing-argument space free (three pushes and sub rsp,0x20 after the
 * entry), so the call is an ordinary call; rcx / rdx / r8 / rax are dead there (the epilogue restores rbp / rbx / rdi and returns
 * al), and the helper keeps rbx / rsi / rdi / rbp / r12 / r13 as every x64 function does. Returns the length, MineStubLen. */
const unsigned char kMineFactionTestOrig[16] = { 0x48, 0x3B, 0xC7, 0x0F, 0x94, 0xC0, 0x48, 0x8B, 0x6C, 0x24, 0x48, 0x48, 0x8B, 0x5C, 0x24, 0x40 };
const int kMineFactionTestLen = 6;    /* cmp rax,rdi / sete al: both replaced; the stub sets al itself */
const int kMineFactionOrigLen = 16;   /* the compare and the epilogue's first two loads, all checked (the table row's 16 bytes) */
const int kMineStubLen = 44;
inline int MineStubBuild(unsigned char* p, unsigned long long site, unsigned long long helper)
{
    p[0] = 0x48; p[1] = 0x3B; p[2] = 0xC7;
    p[3] = 0x74; p[4] = 0x17;
    p[5] = 0x48; p[6] = 0x89; p[7] = 0xC1;
    p[8] = 0x48; p[9] = 0x89; p[10] = 0xFA;
    p[11] = 0x49; p[12] = 0x89; p[13] = 0xF0;
    p[14] = 0x48; p[15] = 0xB8;
    for (int i = 0; i < 8; ++i) p[16 + i] = (unsigned char)((helper >> (8 * i)) & 0xFF);
    p[24] = 0xFF; p[25] = 0xD0;
    p[26] = 0xEB; p[27] = 0x02;
    p[28] = 0xB0; p[29] = 0x01;
    p[30] = 0xFF; p[31] = 0x25; EatPut32(p + 32, 0);
    const unsigned long long backTo = site + (unsigned long long)kMineFactionTestLen;
    for (int i = 0; i < 8; ++i) p[36 + i] = (unsigned char)((backTo >> (8 * i)) & 0xFF);
    return kMineStubLen;
}
/* The team rule the helper applies, asked only when the factions differ (the engine's answer would be no). Owner 512: a team
 * behaves as one faction - so the answer is the one the engine gives when the object's faction is the asker's own:
 *   askerMate / objMate     the asker's / the object's faction is in the teammate cells (this game's current teammates'
 *                           stand-ins; empty with no team)
 *   askerPlayer / objPlayer the faction has a PlayerInterface (this game's own player faction)
 *   cage                    the object takes the engine's earlier "a player's own cage" road (kind 0, vt+0x78 with +0x128 set,
 *                           building function vt+0x2F0 == 8): for its own faction's cage the engine answers no
 *                           (decomp_5a3180:23-33), so a teammate's cage answers no as well
 * Yes only when one side is a teammate's stand-in, both sides are in the team (this game's player or a teammate) and the object
 * is not a cage; every other case keeps the engine's no - a stranger's, a non-team player's, a town's or an NPC's object, and
 * everything with no team. engineYes: the same faction (the stub's own je) - the engine's yes always stands. */
inline int MineTeamAnswer(int engineYes, int askerMate, int askerPlayer, int objMate, int objPlayer, int cage)
{
    if (engineYes != 0) return 1;
    if (askerMate == 0 && objMate == 0) return 0;
    const int askerIn = (askerMate != 0 || askerPlayer != 0) ? 1 : 0;
    const int objIn = (objMate != 0 || objPlayer != 0) ? 1 : 0;
    return (askerIn != 0 && objIn != 0 && cage == 0) ? 1 : 0;
}

}   /* namespace teamstub */
