// RootObject.h - the game's RootObject, DataObjectContainer and RootObjectContainer.
//
// RootObject (0xC0) derives from RootObjectBase and adds nothing our code reads by name (padding).
// DataObjectContainer (0x48) is polymorphic (+0 vtable pointer) and is declared with all four of its
// fields. That is REQUIRED, not decoration: RootObjectContainer (0x68) derives from it AND from
// Ogre::GeneralAllocatedObject, and VS2010 puts that empty base at +0x49 (and
// the next field at +0x50) only because DataObjectContainer contains a zero-sized subobject - the empty allocator
// inside std::string recordsFile. With plain padding in its place the base lands at +0x48, `things` at +0x48 and the
// class shrinks to 0x60 (seen 2026-09-28; the size and `things` asserts caught it). The empty base's own position
// cannot be asserted (game/gamelayout.h); `things` (+0x50) and the size are. Destructors private, never defined.
#pragma once

#include <string>
#include <ogre/OgreMemoryAllocatorConfig.h>
#include "forward.h"
#include "lektor.h"
#include "RootObjectBase.h"

class RootObject : public RootObjectBase
{
public:
    unsigned char pad_78[0xC0 - 0x78];

private:
    ~RootObject(); // virtual in the game. Never defined.
};

class DataObjectContainer
{
public:
    enum ContainerKind { KIND_BUILDING_INTERIOR = 5 };

    void* vtbl_;
    bool isSaved;
    GameDataContainer* objectRecords;
    std::string recordsFile;
    DataObjectContainer::ContainerKind containerKind;

private:
    ~DataObjectContainer(); // virtual in the game. Never defined.
};

class RootObjectContainer : public DataObjectContainer, public Ogre::GeneralAllocatedObject
{
public:
    lektor<RootObject*> things;

    void saveMembers(GameData* outputToInstanceCollectionOfSomeKind, GameDataContainer* source, PlacementTransform* offsetPosToSubtract, const std::string& mod);

private:
    ~RootObjectContainer(); // virtual in the game. Never defined.
};
