// CharBody.h - the game's CharBody.
//
// 0x78 bytes, polymorphic (+0 vtable pointer), derives from Ogre::GeneralAllocatedObject. Our code reads no field
// by name: padding; the size is asserted in game/layout_asserts.inl. Member functions are the game's
// (gamecalls.cpp), declared with the game functions' own parameter and return types.
// NOTE (one base, so not asserted): VS2010, given this class with virtual functions declared, puts the empty base
// at +0x8, after the vtable pointer; ours puts it at +0 (the vtable pointer is a plain field here). The base holds
// no data and only static operator new/delete, so no field moves and no pointer to it is ever used.
#pragma once

#include <ogre/OgreMemoryAllocatorConfig.h>
#include "forward.h"
#include "Enums.h"

class CharBody : public Ogre::GeneralAllocatedObject
{
public:
    void* vtbl_;
    unsigned char pad_8[0x78 - 0x8];

    bool startAction(TaskType t, RootObject* target);
    void finishAction();

private:
    ~CharBody(); // virtual in the game. Never defined.
};
