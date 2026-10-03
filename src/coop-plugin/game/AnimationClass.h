// AnimationClass.h - the game's AnimationClassBase and AnimationClass.
//
// AnimationClassBase (0xE8) is polymorphic (+0 vtable pointer) and derives from Ogre::GeneralAllocatedObject;
// AnimationClass (0x300) derives from AnimationClassBase. Our code reads no field by name: padding; sizes are
// asserted in game/layout_asserts.inl (the empty base's position: see the note in game/CharBody.h). Member
// functions are the game's (gamecalls.cpp), declared with the game functions' own parameter and return types.
#pragma once

#include <ogre/OgreMemoryAllocatorConfig.h>
#include <ogre/OgreVector3.h>
#include "forward.h"

class AnimationClassBase : public Ogre::GeneralAllocatedObject
{
public:
    void* vtbl_;
    unsigned char pad_8[0xE8 - 0x8];

    float actionBlendWeight();
    bool isActionAnimating();

private:
    ~AnimationClassBase(); // virtual in the game. Never defined.
};

// T-189 runboost: the game's AnimationLimits (0x1C8, embedded at AnimationClass +0xF0). Only `speedCap` (+0xAC,
// = AnimationClass +0x19C) is read by name: the ceiling CharMovement::periodicUpdate 0x660330 copies to +0xB4.
class AnimationLimits
{
public:
    unsigned char pad_0[0xAC];
    float speedCap;
    unsigned char pad_B0[0x1C8 - 0xB0];
};

class AnimationClass : public AnimationClassBase
{
public:
    unsigned char pad_E8[0xF0 - 0xE8];
    AnimationLimits movementLimits;   // +0xF0 (T-189)
    unsigned char pad_2B8[0x300 - 0x2B8];

    bool blocksWaypointMovement();
    void setPosition(const Ogre::Vector3& pos);
    void applySpeedCap(float speed);   // T-189: 0x51BED0 - +0x19C = speed; +0x178 / +0x180 clamped down to it

private:
    ~AnimationClass(); // virtual in the game. Never defined.
};
