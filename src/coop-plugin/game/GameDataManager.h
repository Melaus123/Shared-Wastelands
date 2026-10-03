// GameDataManager.h - the game's data containers.
//
// GameDataContainer is 0x180 bytes and polymorphic (virtual destructor), so +0 is the vtable pointer.
// Our code reads none of its fields by name, so everything after the vtable pointer is padding; the size
// is asserted in game/layout_asserts.inl. GameDataManager derives from it and adds no fields.
// Member functions are the game's, forwarded through address-table rows in gamecalls.cpp, declared
// with the game functions' own parameter and return types (non-virtual, public). No virtual functions: the destructors
// are private and never defined, so nothing of ours can destroy a container the wrong way.
// runConstructor runs the game's constructor in place on the caller's raw bytes: an ordinary member function,
// not a C++ constructor, forwarded through an address-table row in gamecalls.cpp (mig4 B).
#pragma once

#include <string>
#include "forward.h"
#include "Enums.h"
#include "lektor.h"
#include "OgreUnordered.h"

class GameDataContainer
{
public:
    void insertRecord(GameData* dat, const std::string& forceID);
    GameDataContainer* runConstructor();
    GameData* newRecord(itemType type, const std::string& forceID, const std::string& name);
    GameData* getData(const std::string& sid);
    GameData* getData(const std::string& sid, itemType category);
    GameData* getData(int id);
    GameData* findRecordByName(const std::string& dataName, itemType category);
    void listRecordsOfType(lektor<GameData*>& list, itemType type);
    const GameHashMap<int, GameData*>::type& allRecords() const;
    void renameRecord(GameData* data, const std::string& n);
    void deleteRecord(GameData* dat);
    bool loadDataFile(std::string file, bool isActive, bool readOnly, Serialisable* moreData);
    bool load(const std::string& filename, const std::string& modName, int modIndex, Serialisable* moreData, bool keepDeletedInstances);
    bool save(const std::string& filename, Serialisable* moreData);
    void clearKeepSquads();
    void clearAndFree();
    int nextRecordId();
    void setName(const std::string& name);

    void* vtbl_;
    unsigned char pad_8[0x180 - 0x8];

private:
    ~GameDataContainer(); // virtual in the game: see the note at the top. Never defined.
};

class GameDataManager : public GameDataContainer
{
public:
    GameData* sectorRecord(int x, int y);

private:
    ~GameDataManager(); // virtual in the game: see the note at the top. Never defined.
};
