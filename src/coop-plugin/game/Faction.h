// Faction.h - the game's Faction and FactionDirectory.
//
// Faction (0x288) is polymorphic (+0 vtable pointer) and derives from Ogre::GeneralAllocatedObject (the empty
// base's position: see the note in game/CharBody.h); FactionDirectory (0x50) is NOT polymorphic and derives from
// it too. Fields our code reads by name sit at their offsets in game/gamelayout.h; the rest is padding
// (FactionDirectory's as 8-byte words, so it keeps 8-byte alignment). Sizes and offsets are asserted in
// game/layout_asserts.inl. Member functions are the game's (gamecalls.cpp), declared with the game functions' own parameter and return types.
#pragma once

#include <string>
#include <ogre/OgreMemoryAllocatorConfig.h>
#include <ogre/OgreVector3.h>
#include "forward.h"
#include "lektor.h"

class Faction : public Ogre::GeneralAllocatedObject
{
public:
    void* vtbl_;
    unsigned char pad_8[0x78 - 0x8];
    FactionRelations* relations;
    unsigned char pad_80[0x288 - 0x80];

    Platoon* spawnEmptySquad(GameData* squadTemplate, bool permanent, const Ogre::Vector3& p);
    void addUnloadedSquad(GameData* platoonstate, GameDataContainer* charactersState, GameData* squadTemplate, const Ogre::Vector3& pos, bool persistent);
    void removeSquad(Platoon* platoon);
    const lektor<Platoon*>* unloadedSquads() const;
    const std::string& getName();
    void setName(const std::string& _name);

private:
    ~Faction(); // virtual in the game. Never defined.
};

class FactionDirectory : public Ogre::GeneralAllocatedObject
{
public:
    unsigned __int64 pad_0[0x50 / 8];

    const lektor<Faction*>* allFactions();
    Faction* findFactionByName(const std::string& name);
    Faction* findFactionById(const std::string& sid);
    Faction* findOrAddFaction(GameData* data);
};
