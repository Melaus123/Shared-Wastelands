// Character.h - the game's Character.
//
// 0x6D8 bytes. Bases: RootObject (+0, which carries the vtable pointer) and Ogre::GeneralAllocatedObject (empty,
// +0xC0 - asserted in game/layout_asserts.inl with the size and every field offset below). Fields our code reads by
// name sit at their offsets in game/gamelayout.h; the rest is padding.
// `medical` is the embedded MedicalSystem (game/MedicalSystem.h). PoseState is in game/Enums.h.
// Member functions are the game's (gamecalls.cpp), declared with the game functions' own parameter and return types. captureSaveState is
// declared only: nothing calls it and nothing defines it.
// faceToward is virtual in the game: an inline vtable call, see game/RootObjectBase.h.
#pragma once

#include <ogre/OgreMemoryAllocatorConfig.h>
#include <ogre/OgreVector3.h>
#include "forward.h"
#include "Enums.h"
#include "gamelayout.h"
#include "GameSaveState.h"
#include "RootObject.h"
#include "MedicalSystem.h"

class Character : public RootObject, public Ogre::GeneralAllocatedObject
{
public:
    unsigned char pad_C0[0x2E8 - 0xC0];
    Inventory* inventory;
    unsigned char pad_2F0[0x448 - 0x2F0];
    AnimationClass* animation;
    CharStats* stats;
    MedicalSystem medical;
    unsigned char pad_608[0x640 - 0x608];
    CharMovement* movement;
    CharBody* body;
    AI* ai;
    ActivePlatoon* platoon;
    unsigned char pad_660[0x6D8 - 0x660];

    AI* getBrain();
    CombatClass* getCombat() const;
    ActivePlatoon* getSquad() const;
    float currentMoveSpeed() const;
    Ogre::Vector3 worldPosition();
    PoseState poseState() const;
    void setPoseState(PoseState p);
    SavedObjectState captureSaveState(GameDataContainer* container, GameData* refList, PlacementTransform* offsetPosToSubtract);
    void restoreSaveState(SavedObjectState* state);
    void setSquadRole(SquadRole memType);
    bool hasDied() const;
    bool isPlayerControlled() const;
    bool inRagdoll() const;

    // ---- virtual in the game: called through the vtable (see game/RootObjectBase.h)
    void faceToward(const Ogre::Vector3& v, bool fullbodyFacing)
    {
        typedef void (*Fn)(Character* self, const Ogre::Vector3& v, bool fullbodyFacing);
        (*(Fn*)((char*)vtbl_ + gamelayout::vt_Character_faceToward))(this, v, fullbodyFacing);
    }

private:
    ~Character(); // virtual in the game. Never defined.
};
