// CombatClass.h - the game's CombatClass.
//
// 0x2B8 bytes, polymorphic (+0 vtable pointer), derives from Ogre::GeneralAllocatedObject. Our code reads no field
// by name: padding; the size is asserted in game/layout_asserts.inl (the empty base's position: see the note in
// game/CharBody.h). Member functions are the game's (gamecalls.cpp), declared with the game functions' own parameter and return types - the four
// gamecalls.cpp defines and combat.cpp calls.
#pragma once

#include <ogre/OgreMemoryAllocatorConfig.h>
#include "forward.h"
#include "Enums.h"
#include "hand.h"

class CombatClass : public Ogre::GeneralAllocatedObject
{
public:
    void* vtbl_;
    unsigned char pad_8[0x2B8 - 0x8];

    hand currentTarget() const;
    bool enterCombat(const hand& subject, int end, bool focusedTarget);
    void setSwordState(swordStateEnum state);
    void setTarget(Character* c);

private:
    ~CombatClass(); // virtual in the game. Never defined.
};
