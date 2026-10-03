// GameData.h - one record of the game's data (FCS) database, and GameDataReference.
//
// GameData is 0x300 bytes and polymorphic (the game gives it a virtual destructor), so +0 is the vtable
// pointer. Every field is declared at its offset in game/gamelayout.h; each offset and the size are
// asserted in game/layout_asserts.inl. No virtual functions are declared: the
// destructor is private and never defined so that nothing of ours can destroy a GameData the wrong way.
// Member functions are the game's, declared with the game functions' own parameter and return types (non-virtual, public).
#pragma once

#include <string>
#include <map>
#include <ogre/OgreVector3.h>
#include <ogre/OgreQuaternion.h>
#include "forward.h"
#include "Enums.h"
#include "lektor.h"
#include "OgreUnordered.h"
#include "TripleInt.h"
#include "hand.h"

// One entry of a GameData reference list: 0x40 bytes - values (+0x00), sid (+0x10), ptr (+0x38).
class GameDataReference
{
public:
    GameDataReference(const GameDataReference& __that);
    GameDataReference(const std::string& _sid, TripleInt _values);
    GameDataReference();
    TripleInt values;
    std::string sid;
    GameData* ptr;
    GameData* getPtr(GameDataContainer* source) const;
    ~GameDataReference();
    GameDataReference& operator=(const GameDataReference& __that);
};

class GameData
{
public:
    // One placed instance (0x68 bytes): the value type of `instances`.
    class ObjectInstance
    {
    public:
        Ogre::Vector3 pos;
        Ogre::Quaternion rot;
        std::string templateRef;
        short created;
        short modified;
        lektor<std::string> stateRecordIds;
    };

    void* vtbl_;
    int aliveStamp;
    GameDataContainer* ownerContainer;
    bool isDetached;
    int id;
    bool readOnly;
    std::string name;
    itemType type;
    std::string stringID;
    bool fromActiveMod;
    std::map<std::string, GameData::ObjectInstance, std::less<std::string>,
             Ogre::STLAllocator<std::pair<std::string const, GameData::ObjectInstance>, Ogre::GeneralAllocPolicy> > instances;
    int idCounter;
    GameHashMap<std::string, bool>::type activeFlags;
    GameHashMap<std::string, bool>::type boolFields;
    GameHashMap<std::string, std::string>::type stringFields;
    GameHashMap<std::string, int>::type intFields;
    GameHashMap<std::string, float>::type floatFields;
    GameHashMap<std::string, std::string>::type fileFields;
    GameHashMap<std::string, Ogre::Vector3>::type vectorFields;
    GameHashMap<std::string, Ogre::Quaternion>::type rotationFields;
    GameHashMap<std::string, Ogre::vector<GameDataReference>::type>::type referenceLists;
    unsigned short createOrder;

    bool isValid() const;
    GameDataContainer* getOwnerContainer() const;
    void destroy();
    bool getHandle(hand& handle, const std::string& _name);
    void listInstances(lektor<GameData::ObjectInstance*>& out);
    GameData::ObjectInstance* addInstance(GameData::ObjectInstance* from);
    void addInstance(GameData* referenceObject, const PlacementTransform& positionRotation);
    void addInstance(GameData* referenceObject);
    GameData::ObjectInstance* addInstance(const SavedObjectState& referenceObject, const PlacementTransform& position, PlacementTransform* offsetPos);

private:
    ~GameData(); // virtual in the game: see the note at the top. Never defined.
};
