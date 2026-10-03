// PROBE-START: P113 (reuse) - T-341: is a second ~GameData at one address the SAME object or a NEW one built there.
// Pure (no includes): the plugin's P113 block and the offline suite include this one header.
// vt values are exe RVAs of the word at rec+0 on ENTRY to the hooked ~GameData (1.0.65 0xB5ED0 / 1.0.68 0xB5F10), 0 = not
// inside the exe. On entry rec+0 still holds the vtable of the subclass whose destructor called the base: that destructor
// stores its own vtable and then calls ~GameData (decomp_b6610.txt line 11-12, GameDataCopyStandalone), and ~GameData
// itself stores GameData::vftable only as its first instruction (decomp_b5ed0.txt line 7), i.e. AFTER the hook. So after a
// destroy rec+0 holds GameData::vftable (gdVt, learned at run time); a real second destroy of the SAME object enters with
// either the first vtable again (the subclass destructor re-stores it) or gdVt (a virtual call through the left-over base
// vtable), or with a non-exe word (the freed block's allocator words). Any other vtable, or a different type (+0x50, which
// ~GameData never clears), means a live object of another class was constructed at the address: REUSE.
#ifndef P113REUSE_H
#define P113REUSE_H
namespace p113 {
enum { kP113SameObject = 0, kP113ReuseType = 1, kP113ReuseClass = 2 };
inline int P113ReuseDecide(int firstType, unsigned long long firstVt, int nowType, unsigned long long nowVt, unsigned long long gdVt)
{
    if (firstType != nowType) return kP113ReuseType;
    if (firstVt != 0 && nowVt != 0 && nowVt != firstVt && nowVt != gdVt) return kP113ReuseClass;
    return kP113SameObject;
}
}
#endif
// PROBE-END: P113 (reuse)
