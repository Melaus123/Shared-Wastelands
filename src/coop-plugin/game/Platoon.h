// Platoon.h - the game's Platoon and ActivePlatoon.
//
// Platoon (0x200) derives from RootObjectBase (+0, which carries the vtable pointer) and from
// Ogre::GeneralAllocatedObject (empty, +0x78); ActivePlatoon (0xF8) derives from RootObjectContainer. Our code
// reads no field of either by name: padding. Sizes and base offsets are
// asserted in game/layout_asserts.inl. (The planning read listed Platoon::squadType, hasUniques and
// canRefresh; every `.squadType` / `.hasUniques` / `.canRefresh` in our code is on our own structs, so they are
// not declared.)
#pragma once

#include <ogre/OgreMemoryAllocatorConfig.h>
#include "forward.h"
#include "RootObjectBase.h"
#include "RootObject.h"

class Platoon : public RootObjectBase, public Ogre::GeneralAllocatedObject
{
public:
    unsigned char pad_78[0x200 - 0x78];

private:
    ~Platoon(); // virtual in the game. Never defined.
};

class ActivePlatoon : public RootObjectContainer
{
public:
    unsigned char pad_68[0xF8 - 0x68];

private:
    ~ActivePlatoon(); // virtual in the game. Never defined.
};
