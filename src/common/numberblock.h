/* src/common/numberblock.h - WHERE A FACTION'S SQUAD COUNTER STANDS IN THIS GAME'S NUMBER BLOCK.
 *
 * ONE RULE, the one squadorigin.h keeps: nothing in here reads engine memory, reads a global or calls the operating
 * system. A pure function of its arguments, so the offline suite drives every branch.
 *
 * WHY. A squad id is "<faction name>_<n>", and <n> comes from the faction's squad counter. Decision 31(a) gives each relay
 * slot a block of 65,536 numbers (slot s: s x 65,536 up to the next block), so n / 65,536 names the game that numbered a
 * squad (store.cpp GroupBlockOwner). A counter must therefore stand inside this game's own block, and past every number of
 * that block this game has already handed out: the ones in the world server's records, and the ones whose squads were
 * deleted (the deleted marks), which a session that never saved left in no counter.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no lambdas.
 */
#ifndef NUMBERBLOCK_H_INCLUDED
#define NUMBERBLOCK_H_INCLUDED

#include <cstddef>

namespace numblock {

const unsigned kBlockSize = 65536u;   /* numbers per slot */
const int kMaxBlocks = 16;            /* decision 31(a)'s cap: a number at or above 16 x 65,536 is not read as an id's number */

/* the slot whose block this game's counters are set to: the relay's slot while the relay is linked and has named one (mySlot);
   otherwise the slot the relay last named in this run of the game (lastKnownSlot, -1 none) - but only while this game is in the
   same world, with the same relay, as when that slot was named (lastKnownSameWorld 1; the caller compares, T-642 review 1). A
   slot named in another world, by another relay, or a game now played alone (lastKnownSameWorld 0) names no block here.
   -1 = no slot known: no block. */
inline int SlotToApply(int relayLinked, int mySlot, int lastKnownSlot, int lastKnownSameWorld)
{
    if (relayLinked != 0 && mySlot >= 0) return mySlot;
    return (lastKnownSlot >= 0 && lastKnownSameWorld != 0) ? lastKnownSlot : -1;
}

/* how CounterForBlock set a counter */
const int kKept = 0, kRaised = 1, kMovedDown = 2, kHeld = 3;

/* a faction's counter once slot's block is applied. counter = the counter now; loadFloorKnown / loadFloor = the counter as this
   world's load left it (squadorigin.h's load floor); leftAt = the highest counter the faction had inside slot's block earlier in
   this world (0 none). The counter never goes below a number this world may already have handed out in slot's block:
   - inside slot's block: kept;
   - below it: raised to the block's floor, or past leftAt / the load floor when either lies inside the block;
   - above it (another game's block): moved down to the highest of the block's floor, leftAt and the load floor - only when
     the counter sits exactly on its own block's floor (it has handed out no number in the block it would leave), the load
     floor is known and the load itself did not leave the counter above slot's block. Otherwise held where it is (kHeld): a
     counter past its own block's floor has handed out numbers there, and moved down and saved it would let a later session
     in that block hand them out again (T-642 review 2); a load that left it above the block means the save's own squads may
     hold any number below it.
   *how = kKept, kRaised, kMovedDown or kHeld. A slot outside 0..15: unchanged, kKept. */
inline unsigned CounterForBlock(unsigned counter, int slot, int loadFloorKnown, unsigned loadFloor, unsigned leftAt, int* how)
{
    *how = kKept;
    if (slot < 0 || slot >= kMaxBlocks) return counter;
    const unsigned lo = (unsigned)slot * kBlockSize;
    if ((counter >> 16) == (unsigned)slot) return counter;
    if (counter > lo && counter > ((counter >> 16) << 16)) { *how = kHeld; return counter; }   /* T-642 review 2: numbers handed out in the block it would leave */
    if (counter > lo && loadFloorKnown == 0) { *how = kHeld; return counter; }
    if (counter > lo && (loadFloor >> 16) > (unsigned)slot) { *how = kHeld; return counter; }
    unsigned target = lo;
    if ((leftAt >> 16) == (unsigned)slot && leftAt > target) target = leftAt;
    if (loadFloorKnown != 0 && (loadFloor >> 16) == (unsigned)slot && loadFloor > target) target = loadFloor;
    *how = counter < lo ? kRaised : kMovedDown;
    return target;
}

/* the counter floor from one faction's deleted marks (bit n of bits = number n deleted, size bytes): one past the highest
   number of mySlot's block that is marked, never lower than floorNow - but never past the block's last number (lo + 65,535):
   one past a mark on that number is the next game's block floor (T-642 review 4), so the floor is capped there and *capped
   (when given) is set to 1; else 0. No mark in the block, or no slot (-1, or past the cap): floorNow. */
inline unsigned OwnMarkFloor(const char* bits, size_t size, int mySlot, unsigned floorNow, int* capped = 0)
{
    if (capped != 0) *capped = 0;
    if (mySlot < 0 || mySlot >= kMaxBlocks || bits == 0) return floorNow;
    const size_t lo = (size_t)mySlot * (kBlockSize / 8u), hi = lo + kBlockSize / 8u;
    size_t b = size < hi ? size : hi;
    while (b > lo)
    {
        --b;
        const unsigned v = (unsigned char)bits[b];
        if (v == 0) continue;
        unsigned bit = 7u; while (bit > 0u && (v & (1u << bit)) == 0u) --bit;
        const unsigned n = (unsigned)b * 8u + bit, last = (unsigned)mySlot * kBlockSize + (kBlockSize - 1u);
        unsigned fl = n + 1u;
        if (fl > last) { fl = last; if (capped != 0) *capped = 1; }
        return fl > floorNow ? fl : floorNow;
    }
    return floorNow;
}

} // namespace numblock

#endif
