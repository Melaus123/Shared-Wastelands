// Inventory.h - the game's InventorySection, InventorySection::SectionItem and Inventory.
//
// InventorySection (0xD8) is polymorphic (+0 vtable pointer), no base; SectionItem (0x10, NOT polymorphic) is
// declared in full. Inventory (0x98) is polymorphic and derives from Ogre::GeneralAllocatedObject (the empty
// base's position: see the note in game/CharBody.h). Fields our code reads by name sit at their offsets in
// game/gamelayout.h; the rest is padding. Sizes and offsets are asserted in game/layout_asserts.inl.
// Non-virtual member functions are the game's (gamecalls.cpp), declared with the game functions' own parameter and return types.
// addItem, _addItem, takeItemOut and redrawPanel are virtual in the game: inline
// vtable calls at the slots in game/gamelayout.h (the calling rules: game/RootObjectBase.h).
#pragma once

#include <string>
#include <ogre/OgrePrerequisites.h>
#include <ogre/OgreMemoryAllocatorConfig.h>
#include "forward.h"
#include "gamelayout.h"

class InventorySection
{
public:
    class SectionItem
    {
    public:
        Item* item;
        unsigned short x;
        unsigned short y;
        unsigned short w;
        unsigned short h;
    };

    void* vtbl_;
    std::string name;
    unsigned char pad_30[0x40 - 0x30];
    Ogre::vector<InventorySection::SectionItem>::type items;
    unsigned char pad_60[0xC0 - 0x60];
    RootObject* notifyTarget;
    unsigned char pad_C8[0xD8 - 0xC8];

    void placeItemDirect(Item* item, int x, int y);
    bool fitsAt(Item* item, int x, int y);
    bool itemInArea(Item* item, int x, int y);
    Item* getItemAt(int x, int y);
    void recomputeWeight();
    void setEnabled(bool value);

    // ---- virtual in the game: called through the vtable (see game/RootObjectBase.h)
    bool addItem(Item* itemToAdd, int quantity)
    {
        typedef bool (*Fn)(InventorySection* self, Item* itemToAdd, int quantity);
        return (*(Fn*)((char*)vtbl_ + gamelayout::vt_InventorySection_addItem))(this, itemToAdd, quantity);
    }
    void _addItem(Item* item, int x, int y)
    {
        typedef void (*Fn)(InventorySection* self, Item* item, int x, int y);
        (*(Fn*)((char*)vtbl_ + gamelayout::vt_InventorySection__addItem))(this, item, x, y);
    }

private:
    ~InventorySection(); // virtual in the game. Never defined.
};

class Inventory : public Ogre::GeneralAllocatedObject
{
public:
    void* vtbl_;
    unsigned char pad_8[0x80 - 0x8];
    RootObject* notifyTarget;
    RootObject* owner;
    unsigned char pad_90[0x98 - 0x90];

    void clearAll(bool destroy, bool skipUnique);
    InventorySection* getSection(const std::string& name) const;
    void markChanged();

    // ---- virtual in the game: called through the vtable (see game/RootObjectBase.h)
    Item* takeItemOut(Item* it, int howmany, bool returnCopyIfSomeLeft)
    {
        typedef Item* (*Fn)(Inventory* self, Item* it, int howmany, bool returnCopyIfSomeLeft);
        return (*(Fn*)((char*)vtbl_ + gamelayout::vt_Inventory_takeItemOut))(this, it, howmany, returnCopyIfSomeLeft);
    }
    void redrawPanel()
    {
        typedef void (*Fn)(Inventory* self);
        (*(Fn*)((char*)vtbl_ + gamelayout::vt_Inventory_redrawPanel))(this);
    }
    bool addItem(Item* item, int quantity, bool dropOnFail, bool destroyOnFail)   // mig4 G2 (clothing.cpp)
    {
        typedef bool (*Fn)(Inventory* self, Item* item, int quantity, bool dropOnFail, bool destroyOnFail);
        return (*(Fn*)((char*)vtbl_ + gamelayout::vt_Inventory_addItem))(this, item, quantity, dropOnFail, destroyOnFail);
    }

private:
    ~Inventory(); // virtual in the game. Never defined.
};
