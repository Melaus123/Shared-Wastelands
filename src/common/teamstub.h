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

}   /* namespace teamstub */
