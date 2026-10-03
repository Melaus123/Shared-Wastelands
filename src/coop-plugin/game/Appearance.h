// Appearance.h - the game's AppearanceBase.
//
// 0x190 bytes, polymorphic (+0 vtable pointer), derives from Ogre::GeneralAllocatedObject (the empty base's
// position: see the note in game/CharBody.h). attachments is declared at its offset in game/gamelayout.h, as the
// boost map that GameHashMap<...>::type names; the rest is padding. The size and the offset are asserted in
// game/layout_asserts.inl.
#pragma once

#include <string>
#include <ogre/OgreMemoryAllocatorConfig.h>
#include "forward.h"
#include "OgreUnordered.h"

class AppearanceBase : public Ogre::GeneralAllocatedObject
{
public:
    void* vtbl_;
    unsigned char pad_8[0x10 - 0x8];
    GameHashMap<std::string, AttachedEntity*>::type attachments;
    unsigned char pad_50[0x190 - 0x50];

private:
    ~AppearanceBase(); // virtual in the game. Never defined.
};
