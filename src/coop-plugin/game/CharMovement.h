// CharMovement.h - the game's NxUserControllerHitReport, AbstractMovementBase, CharMovement, PathPriority and
// SteeringMode.
//
// NxUserControllerHitReport (0x8) is only a vtable pointer. AbstractMovementBase (0x108) derives from it (+0) and
// from Ogre::GeneralAllocatedObject (empty, +0x8); CharMovement (0x3B8) derives from AbstractMovementBase. Sizes,
// base offsets and field offsets are asserted in game/layout_asserts.inl. Enums hold only their last enumerator
// (the convention of game/Enums.h).
// Member functions are the game's (gamecalls.cpp), declared with the game functions' own parameter and return types.
#pragma once

#include <ogre/OgreMemoryAllocatorConfig.h>
#include <ogre/OgreVector3.h>
#include "forward.h"
#include "Enums.h"

enum PathPriority { PATH_PRIORITY_MEDIUM = 1, PATH_PRIORITY_HIGH = 2 };   // G2: PATH_PRIORITY_MEDIUM (ai_spike, replicate)
enum SteeringMode   { STEER_BY_DIRECTION = 2 };

class NxUserControllerHitReport
{
public:
    void* vtbl_;

private:
    ~NxUserControllerHitReport(); // virtual in the game. Never defined.
};

class AbstractMovementBase : public NxUserControllerHitReport, public Ogre::GeneralAllocatedObject
{
public:
    unsigned char pad_8[0xB4 - 0x8];
    float speedCap;       // +0xB4 T-189: Havok gets min(desiredSpeed, speedCap) (update 0x65F510)
    float speedNow;
    float desiredSpeed;
    unsigned char pad_C0[0x108 - 0xC0];

    Ogre::Vector3 getDestination() const;
    const Ogre::Vector3& headingVector() const;
    void orderMoveSpeed(SpeedOrder speed);

private:
    ~AbstractMovementBase(); // virtual in the game. Never defined.
};

class CharMovement : public AbstractMovementBase
{
public:
    unsigned char pad_108[0x3B8 - 0x108];

    void teleportTo(const Ogre::Vector3& p, int floor);
    bool lastPathFailed() const;
    void setPathGoal(const Ogre::Vector3& dest, PathPriority priority, bool notVertical);
    void setSteeringMode(SteeringMode mode);
    void steerDirectly(const Ogre::Vector3& d, float limit);

private:
    ~CharMovement(); // virtual in the game. Never defined.
};
