// GameWorld.h - the game's GameWorld.
//
// 0x8C8 bytes, polymorphic (+0 vtable pointer), derives from Ogre::GeneralAllocatedObject (the empty base's
// position: see the note in game/CharBody.h). Fields our code reads by name sit at their offsets in
// game/gamelayout.h - `gamedata` is the embedded GameDataManager (game/GameDataManager.h); the rest is padding.
// The size and every field offset are asserted in game/layout_asserts.inl. `player` is declared
// because the planning read listed it, though no code reads it by name today. Member functions are the game's
// (gamecalls.cpp), declared with the game functions' own parameter and return types.
#pragma once

#include <ogre/OgreMemoryAllocatorConfig.h>
#include "forward.h"
#include "OgreUnordered.h"
#include "GameDataManager.h"

class GameWorld : public Ogre::GeneralAllocatedObject
{
public:
    void* vtbl_;
    unsigned char pad_8[0x20 - 0x8];
    GameDataManager gamedata;
    unsigned char pad_1A0[0x4A0 - 0x1A0];
    RootObjectFactory* objectFactory;
    FactionDirectory* factionDirectory;
    unsigned char pad_4B0[0x580 - 0x4B0];
    PlayerInterface* player;
    unsigned char pad_588[0x8C8 - 0x588];

    bool destroy(RootObject* obj, bool justUnloaded, const char* debugInfo);
    const GameHashSet<Character*>::type& activeCharacters() const;
    float frameSpeedScale() const;
    bool isPaused() const;

private:
    ~GameWorld(); // virtual in the game. Never defined.
};
