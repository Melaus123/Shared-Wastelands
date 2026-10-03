// RootObjectBase.h - the root of every game object.
//
// 0x78 bytes and polymorphic, so +0 is the vtable pointer (vtbl_). Every field is declared at its offset in
// game/gamelayout.h; each offset and the size are asserted in game/layout_asserts.inl.
// Member functions are the game's (defined in gamecalls.cpp), declared with the game functions' own parameter and return types
// (non-virtual, public). The destructor is private and never defined.
//
// VIRTUAL CALLS. getRecord, isUnconscious, getPosition and getOwnerFaction are virtual in the game; our code
// calls them. Here each is an INLINE non-virtual member with the same name and signature
// that loads the function pointer from the object's own vtable at the slot byte offset kept in
// game/gamelayout.h (vt_* - proven against kenshi_x64.exe by tools/check_vtable_slots.py) and calls it.
// A derived class (Character, Platoon, Item ...) inherits the wrapper and reads ITS vtable, exactly as a
// virtual call would. The same pattern is used for Character::faceToward and the four Inventory /
// InventorySection virtuals (game/Character.h, game/Inventory.h).
// x64 calling rules (Confirmed in stage 3, gamecalls.cpp header comment), matched by each function-pointer type:
//   - `this` goes in RCX; the arguments follow in RDX, R8, R9 (a float argument in the XMM register of its
//     position); a reference argument is passed as a pointer (so `const T&` in the pointer type is the same).
//   - a member function returning a class BY VALUE (Ogre::Vector3 getPosition) takes a hidden pointer to the
//     caller's return storage in RDX, right after `this`, and returns that pointer in RAX. So the call is made
//     as `Ret* (*)(T* self, Ret* out, args...)` - NOT as a free function returning Ret by value, which would
//     put the hidden pointer in RCX, the wrong register.
//   - bool comes back in AL, a pointer in RAX, float in XMM0.
#pragma once

#include <string>
#include <ogre/OgreMemoryAllocatorConfig.h>
#include <ogre/OgreVector3.h>
#include "forward.h"
#include "Enums.h"
#include "hand.h"
#include "gamelayout.h"

class RootObjectBase
{
public:
    void* vtbl_;
    int liveCheckKey;
    Faction* owner;
    std::string shownName;
    GameData* data;
    Ogre::Vector3 pos;
    hand handle;

    GameData* getRecordDirect() const;
    std::string getShownNameDirect() const;
    Faction* getOwnerFactionDirect() const;
    const hand& getHandle() const;

    // ---- virtual in the game: called through the vtable (see the note at the top)
    GameData* getRecord() const
    {
        typedef GameData* (*Fn)(const RootObjectBase* self);
        return (*(Fn*)((char*)vtbl_ + gamelayout::vt_RootObjectBase_getRecord))(this);
    }
    bool isUnconscious() const
    {
        typedef bool (*Fn)(const RootObjectBase* self);
        return (*(Fn*)((char*)vtbl_ + gamelayout::vt_RootObjectBase_isUnconscious))(this);
    }
    Ogre::Vector3 getPosition()
    {
        typedef Ogre::Vector3* (*Fn)(RootObjectBase* self, Ogre::Vector3* out);
        Ogre::Vector3 out;
        (*(Fn*)((char*)vtbl_ + gamelayout::vt_RootObjectBase_getPosition))(this, &out);
        return out;
    }
    Faction* getOwnerFaction() const
    {
        typedef Faction* (*Fn)(const RootObjectBase* self);
        return (*(Fn*)((char*)vtbl_ + gamelayout::vt_RootObjectBase_getOwnerFaction))(this);
    }

private:
    ~RootObjectBase(); // virtual in the game: never defined, so nothing of ours destroys one the wrong way.
};
