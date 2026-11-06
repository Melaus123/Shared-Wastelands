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

}   /* namespace teamstub */
