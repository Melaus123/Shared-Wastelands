// RootObjectFactory.h - the game's RootObjectFactory.
//
// 0x50 bytes, NOT polymorphic, derives from Ogre::GeneralAllocatedObject. Our code reads no field by name: padding
// as 8-byte words (8-byte alignment); the size is asserted in game/layout_asserts.inl. create and createItem are
// the game's (gamecalls.cpp), declared with the game functions' own parameter and return types (create takes the vector and quaternion BY VALUE - see
// gamecalls.cpp's comment).
#pragma once

#include <ogre/OgreMemoryAllocatorConfig.h>
#include <ogre/OgreVector3.h>
#include <ogre/OgreQuaternion.h>
#include "forward.h"

class RootObjectFactory : public Ogre::GeneralAllocatedObject
{
public:
    unsigned __int64 pad_0[0x50 / 8];

    RootObjectBase* create(GameData* data, Ogre::Vector3 position, bool isFromActiveLevelMod, Faction* owner, Ogre::Quaternion rotation, FactoryCallbackInterface* notifyTarget, RootObjectContainer* certainContainer, SavedObjectState* state, bool invisible, Building* homeBuilding, float age);
    Item* createItem(GameData* itemState);
};
