// SaveManager.h - the game's SaveManager.
//
// 0x120 bytes, NOT polymorphic, derives from Ogre::GeneralAllocatedObject. Our code reads no field by name: padding
// as 8-byte words (8-byte alignment); the size is asserted in game/layout_asserts.inl. Member functions are the
// game's (gamecalls.cpp), declared with the game functions' own parameter and return types; getSingleton is static.
#pragma once

#include <string>
#include <ogre/OgreMemoryAllocatorConfig.h>
#include "forward.h"

class SaveManager : public Ogre::GeneralAllocatedObject
{
public:
    unsigned __int64 pad_0[0x120 / 8];

    static SaveManager* getSingleton();
    void load(const std::string& name);
    bool anySavesExist();
};
