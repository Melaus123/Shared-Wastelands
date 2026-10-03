// CharStats.h - the game's CharStats, for the one field the plugin reads by name (T-189 runboost, H051).
//
// CharStats derives from Ogre::GeneralAllocatedObject and has virtual functions (vtable at +0, first member
// `medical` at +0x8). The plugin reads only `runSpeed` (+0x17C): the character's run speed, computed by
// calculateMaxRunSpeed 0x885590 and pushed by MedicalSystem 0x644840 / 0x645DD0 through
// AnimationClass::applySpeedCap 0x51BED0 on to AnimationClass +0x19C. Its offset is asserted in
// game/layout_asserts.inl (the size is not: no code here needs it, and nothing here allocates one).
#pragma once

#include <ogre/OgreMemoryAllocatorConfig.h>
#include "forward.h"

class CharStats : public Ogre::GeneralAllocatedObject
{
public:
    void* vtbl_;
    unsigned char pad_8[0x17C - 0x8];
    float runSpeed;   // +0x17C the run speed

private:
    ~CharStats(); // virtual in the game. Never defined.
};
