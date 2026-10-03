// AITaskSystem.h - the game's OrdersReceiver and AITaskSytem (sic - the game's own spelling).
//
// OrdersReceiver (0x210) is polymorphic (+0 vtable pointer) and derives from Ogre::GeneralAllocatedObject;
// AITaskSytem (0x3B8) derives from OrdersReceiver. Our code reads no field by name: padding; sizes are asserted
// in game/layout_asserts.inl (the empty base's position: see the note in game/CharBody.h). Member functions are
// the game's (gamecalls.cpp), declared with the game functions' own parameter and return types.
#pragma once

#include <string>
#include <ogre/OgreMemoryAllocatorConfig.h>
#include <ogre/OgreVector3.h>
#include "forward.h"
#include "Enums.h"
#include "hand.h"

class OrdersReceiver : public Ogre::GeneralAllocatedObject
{
public:
    void* vtbl_;
    unsigned char pad_8[0x210 - 0x8];

    void issueOrder(TaskType t, const hand& subject, const Ogre::Vector3& location, bool clearOld, bool shift);
    void dropAllOrders();
    void dropGoals();
    bool hasPendingOrders() const;

private:
    ~OrdersReceiver(); // virtual in the game. Never defined.
};

class AITaskSytem : public OrdersReceiver
{
public:
    unsigned char pad_210[0x3B8 - 0x210];

    std::string describeCurrentGoal();

private:
    ~AITaskSytem(); // virtual in the game. Never defined.
};
