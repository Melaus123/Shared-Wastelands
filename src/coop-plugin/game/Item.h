// Item.h - the game's InventoryItemBase and Item.
//
// InventoryItemBase (0x190) derives from RootObject (+0, which carries the vtable pointer) and from
// Ogre::GeneralAllocatedObject (empty, +0xC0); Item (0x1E8) derives from InventoryItemBase. Fields our code
// reads by name sit at their offsets in game/gamelayout.h; the rest is padding. Sizes, base offsets and field
// offsets are asserted in game/layout_asserts.inl. Member functions are the game's (gamecalls.cpp),
// declared with the game functions' own parameter and return types.
#pragma once

#include <ogre/OgreMemoryAllocatorConfig.h>
#include "forward.h"
#include "Enums.h"
#include "RootObject.h"

class InventoryItemBase : public RootObject, public Ogre::GeneralAllocatedObject
{
public:
    unsigned char pad_C0[0x11C - 0xC0];
    float quality;
    unsigned char pad_120[0x124 - 0x120];
    ItemFunction functionKind;
    unsigned char pad_128[0x12C - 0x128];
    int quantity;
    unsigned char pad_130[0x190 - 0x130];

    bool wasStolen(bool includeUnknown) const;

private:
    ~InventoryItemBase(); // virtual in the game. Never defined.
};

class Item : public InventoryItemBase
{
public:
    unsigned char pad_190[0x1E8 - 0x190];

    GameData* saveIntoInventoryRecord(GameDataContainer* container, GameData* refList);

private:
    ~Item(); // virtual in the game. Never defined.
};
