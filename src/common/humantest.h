/* src/common/humantest.h - T-293: THE HUMAN CLASS RULE (is this appearance object an AppearanceHuman?).
 *
 * ONE RULE, copybody.h's shape: nothing in here reads engine memory, reads a global, calls the operating system or
 * includes a header, so the offline suite drives it directly.
 *
 * WHY THE VTABLE POINTER. The game has exactly three appearance classes (RTTI in both Steam exes: AppearanceBase,
 * AppearanceHuman, AppearanceAnimal) and each has one vtable, so "the object's vtable pointer is AppearanceHuman's"
 * IS the class identity. The previous test looked for AppearanceHuman::setGender's address INSIDE the vtable and never
 * matched on either version (human=0 on every character, T625/T628/T633/T634): the game is incrementally linked, so
 * every vtable slot holds a 5-byte jump stub (E9 rel32) and never the function's own address - on 1.0.65 slot 5
 * (+0x28) holds 0x4A3CC, which jumps to setGender 0x52D530.
 *
 * HumanClassDecide: vt = the object's vtable pointer (0 = unreadable), base = the game's image base, humanVtRva = the
 * address-table row AppearanceHumanVt (0 = no row). 1 human, 0 another class, -1 cannot tell (never "human").
 * ThunkTarget: at = an address, b = the bytes there; an E9 rel32 jump -> where it goes, anything else -> at itself.
 * Used only to log what a slot really calls.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_HUMANTEST_H
#define COOP_COMMON_HUMANTEST_H

namespace coophuman {

enum { kHumanUnreadable = -1, kHumanNo = 0, kHumanYes = 1 };

inline int HumanClassDecide(unsigned long long vt, unsigned long long base, unsigned long long humanVtRva)
{
    if (vt == 0 || base == 0 || humanVtRva == 0) return kHumanUnreadable;
    return (vt == base + humanVtRva) ? kHumanYes : kHumanNo;
}

inline unsigned long long ThunkTarget(unsigned long long at, const unsigned char* b)
{
    if (b[0] != 0xE9) return at;
    const unsigned int u = (unsigned int)b[1] | ((unsigned int)b[2] << 8) | ((unsigned int)b[3] << 16)
                         | ((unsigned int)b[4] << 24);
    const long long rel = (long long)(int)u;
    return at + 5ull + (unsigned long long)rel;
}

}   /* namespace coophuman */

#endif
