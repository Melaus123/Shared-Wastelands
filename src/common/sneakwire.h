/* src/common/sneakwire.h - SNEAKING ON THE OTHER GAME: the owner's stealth mode in MSG_STATE, and what the copy's game does.
 *
 * The owner reads its character's stealth mode (Character +0xD4, the byte Character::setStealthMode 0x5C9F10 compares and
 * writes - the call the orders panel's sneak button, OrdersPanel::toggleStealth 0x7208E0, makes) and sends it as bit 7 of
 * STATE's latchBits word: in every periodic STATE, in the catch-up STATE a late arrival gets, and in a STATE pushed at
 * once when the mode changes. The copy's game calls the same engine setter on the copy, so the copy crouches and that
 * game's own detection treats it as sneaking.
 * Bits 0-5 of the word are the medical latch and bit 6 the owner's inRagdoll (getupcrawl.h); ApplyLatch reads only 0-5.
 *
 * Built and read here only, so the offline suite (src/coop-test/test_main.cpp) checks the same bits spawn.cpp uses.
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_SNEAKWIRE_H
#define COOP_COMMON_SNEAKWIRE_H

namespace coopsneak {

const unsigned int kStateOwnerSneakBit = 1u << 7;

/* The word with the owner's stealth mode folded in. modeRead: 1 sneaking, 0 not, -1 unreadable - sent as not sneaking,
   so the copy then stands upright as it always did. The other bits are kept. */
inline unsigned int WithSneak(unsigned int latchBits, int modeRead)
{
    return (modeRead == 1) ? (latchBits | kStateOwnerSneakBit) : (latchBits & ~kStateOwnerSneakBit);
}

/* The owner's word: true = its character is in stealth mode. */
inline bool SneakWanted(unsigned int latchBits) { return (latchBits & kStateOwnerSneakBit) != 0; }

/* What the copy's game does with the owner's word, given the copy's own mode (1 sneaking, 0 not, -1 unreadable). */
const int kSneakNone       = 0;   /* the copy already matches - nothing is called */
const int kSneakSetOn      = 1;   /* setStealthMode(true) */
const int kSneakSetOff     = 2;   /* setStealthMode(false) */
const int kSneakUnreadable = 3;   /* the copy's mode could not be read - nothing is called */

inline int SneakAction(bool want, int copyMode)
{
    if (copyMode != 0 && copyMode != 1) return kSneakUnreadable;
    if ((copyMode == 1) == want) return kSneakNone;
    return want ? kSneakSetOn : kSneakSetOff;
}

/* The [SNEAK] log line: the first kSneakLogFirst, then one in every kSneakLogEvery. n counts from 1. */
const long long kSneakLogFirst = 8;
const long long kSneakLogEvery = 100;
inline bool SneakLogDue(long long n) { return n >= 1 && (n <= kSneakLogFirst || (n % kSneakLogEvery) == 0); }

}  // namespace coopsneak

#endif
