// lektor.h - the game's growable array, lektor<T>, as this project reads it.
//
// WHAT THE GAME'S CODE SHOWS (our own reads of the executable, not a header):
//   F849  Building::getDoor 0xF7040 / hasAnOpenDoor 0x548F10 / destroyDoors 0x547DA0 walk the doors array at
//         Building+0x1B8: a u32 element count 8 bytes in (+0x1C0) and the element pointer 16 bytes in (+0x1C8).
//   F729  InventorySection::isLimitedSlotCompatible 0x74B0F0 and addVeryLimitedSlot 0x74A900 use the array at
//         section+0x90: count +0x98, capacity +0x9C, elements +0xA0 (items.cpp, the box probe note).
// So every lektor<T> is 0x18 bytes, whatever T is:
//     +0x00  8 bytes this project never reads or writes (the game's allocator word)
//     +0x08  count    u32, elements in use
//     +0x0C  maxSize  u32, elements the storage holds
//     +0x10  stuff    T*, the storage
// game/gamelayout.h keeps those four numbers and game/layout_asserts.inl checks this declaration against them.
//
// This array never owns its storage: the game fills it, the game frees it. Nothing here allocates, grows or
// frees `stuff`; clear() only forgets the elements (count = 0), and an array the game filled for us is read,
// never released. Only what other files call is provided.
#pragma once

#include <stdint.h>

template <typename T>
class lektor
{
public:
    unsigned char unusedWord[8];   // +0x00
    uint32_t      count;           // +0x08
    uint32_t      maxSize;         // +0x0C
    T*            stuff;           // +0x10

    typedef T*       iterator;
    typedef const T* const_iterator;

    lektor() : count(0), maxSize(0), stuff(0) {}

    uint32_t size() const { return count; }
    void     clear()      { count = 0; }

    // No range check: every caller already bounds its index by size() (and by its own cap).
    T&       operator[](uint32_t i)       { return stuff[i]; }
    const T& operator[](uint32_t i) const { return stuff[i]; }

    iterator       begin()       { return stuff; }
    iterator       end()         { return stuff + count; }
    const_iterator begin() const { return stuff; }
    const_iterator end()   const { return stuff + count; }
};
