// SavedObjectState.h - one object's saved state.
//
// 0xA8 bytes, NOT polymorphic (+0 is baseRecord). Fields only - no constructor or destructor is declared, so
// nothing of ours copies or destroys one. Every offset and the size are asserted in
// game/layout_asserts.inl. savedPosition and stateCount are defined in gamecalls.cpp, declared with the game functions' own parameter and return types.
#pragma once

#include <string>
#include <ogre/OgreVector3.h>
#include <ogre/OgreQuaternion.h>
#include "forward.h"
#include "Enums.h"
#include "OgreUnordered.h"
#include "GameData.h"

class SavedObjectState
{
public:
    void newInstanceId();
    int stateCount() const;
    Ogre::Vector3 savedPosition() const;

    GameData* baseRecord;
    GameDataContainer* sourceRecords;
    bool firstTime;
    GameData::ObjectInstance* instance;
    Ogre::Vector3 pos;
    Ogre::Quaternion rot;
    std::string instanceID;
    GameHashMap<itemType, GameData*>::type states;
};
