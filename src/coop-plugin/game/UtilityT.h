// UtilityT.h - the game's UtilityT.
//
// 0x60 bytes, NOT polymorphic, no base. Our code reads no field by name: padding as 8-byte words (8-byte
// alignment); the size is asserted in game/layout_asserts.inl. projectToScreen is the game's (gamecalls.cpp),
// declared with the game function's own parameter and return types.
#pragma once

#include <ogre/OgreVector3.h>
#include "forward.h"

class UtilityT
{
public:
    unsigned __int64 pad_0[0x60 / 8];

    bool projectToScreen(const Ogre::Vector3& pos, float& x, float& y);
};
