// hand.h - the game's object handle.
//
// Layout (0x20 bytes; every offset asserted in game/layout_asserts.inl):
//   +0x00  the vtable pointer (the game's hand has virtual ==/!= operators; its vtable is at RVA
//          the address table's HandVt row, checked by tools/check_vtable_slots.py, not used here)
//   +0x08  type, +0x0C container, +0x10 containerStamp, +0x14 index, +0x18 serial
//
// Member functions: the game's, defined in gamecalls.cpp (address-table rows). Each is
// declared with the game functions' own parameter and return types (non-virtual, public).
// The game's three VIRTUAL operators (== bool, == hand, != hand) are NOT callable through this header.
// They are declared private and never defined so that `a == b` on two hands
// fails to compile instead of silently comparing the two handles' `operator bool` results.
#pragma once

#include <string>
#include "forward.h"
#include "Enums.h"

class hand
{
public:
    hand(GameData* fromLoadedState, itemType typ);
    hand(RootObjectBase* from);
    hand(const int from);
    hand(const hand& from);
    hand(unsigned int _index, unsigned int _serial, itemType _type, unsigned int _container, unsigned int _containerSerial);
    hand();

    void* vtbl_;
    itemType type;
    unsigned int container;
    unsigned int containerStamp;
    unsigned int index;
    unsigned int serial;

    std::string toString() const;
    void fromString(const std::string& str);
    bool operator==(const RootObjectBase* a) const;
    bool operator!=(const RootObjectBase* a) const;
    operator bool() const;
    Character* getCharacter() const;
    Platoon* getSquad() const;
    ActivePlatoon* asActiveSquad() const;
    Building* asBuilding() const;
    Item* getItem() const;
    RootObject* asObject() const;
    RootObjectBase* asObjectBase() const;
    TownBase* asTown() const;
    std::string explainState();
    bool operator<(const hand& h) const;
    hand& operator=(const hand& __that);
    const hand& operator=(RootObjectBase* a);
    const hand& operator=(const int& a);
    void setNull();
    bool isNull() const;
    bool isValid() const;
    bool isObjectHandle() const;
    bool sameSquad(const hand& h) const;

private:
    // virtual in the game (vtable +0x0, +0x8, +0x10): see the note at the top. Never defined.
    bool operator==(bool a) const;
    bool operator==(const hand& a) const;
    bool operator!=(const hand& a) const;
};
