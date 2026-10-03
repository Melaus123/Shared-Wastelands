// FactionRelations.h - the game's FactionRelations.
//
// 0x68 bytes, polymorphic (+0 vtable pointer), no base. Our code reads no field by name: padding; the size is
// asserted in game/layout_asserts.inl. relationTo is the game's (gamecalls.cpp), declared with the game
// function's own parameter and return types.
#pragma once

#include "forward.h"

class FactionRelations
{
public:
    void* vtbl_;
    unsigned char pad_8[0x68 - 0x8];

    float relationTo(Faction* p);

private:
    ~FactionRelations(); // virtual in the game. Never defined.
};
