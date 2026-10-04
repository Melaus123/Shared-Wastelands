/* gamecalls.cpp - OUR OWN DEFINITIONS OF THE GAME METHODS THE PLUGIN CALLS.
 *
 * The headers in game/ declare the game's classes with no dllimport on member functions. Each definition below is
 * one such method: it calls the game's function at the address our own table row gives (coop::AddrAbs). The calling
 * code in every other file writes the ordinary call - `faction->getName()`.
 *
 * THE ROWS. One per method, name `<Class>_<method>`, RVA from the read of RE_Kenshi's Steam_1.0.65 list. Some
 * methods reuse rows other files already bound (HandGetBuilding, DestroyPlatoon, WorldDestroy,
 * AnimationClass_placeAnimationRoot, CharBody_finishAction, CharMovement_lastPathFailed, CharMovement_setPathGoal,
 * CharMovement_teleportTo, SecAddItem for InventorySection::placeItemDirect, GdcGetData for the one-string
 * GameDataContainer::getData): a name may be registered more than once. A row name is at most 47 characters, so
 * some names are shortened: AnimationClass_blocksWaypointMovement, AnimClassBase_<method> (the two
 * AnimationClassBase methods) and HealthPartStatus_recomputeHealth (MedicalSystem::HealthPartStatus).
 *
 * THE CALLS (x64). `this` goes in RCX. A member function returning a class BY VALUE (hand::toString,
 * RootObjectBase::getShownNameDirect, AITaskSytem::describeCurrentGoal - all std::string) takes a hidden pointer to
 * the caller's return storage in RDX, right after `this`; the game CONSTRUCTS the result there. So those pass RAW
 * storage (never an already-built string - that one would be overwritten, never destroyed, and leak), take the
 * built string out of it with swap, and destroy what is left. Character::worldPosition, AbstractMovementBase::
 * getDestination and SavedObjectState::savedPosition return an Ogre::Vector3 the same way (plain data, so it is
 * copied out of the slot and nothing is left to destroy); CombatClass::currentTarget returns a hand, rebuilt from its
 * five fields with hand's own five-field constructor, which writes exactly what the game's copy constructor writes:
 * the same vtable and the same five fields, both read in the exe's bytes. Character::captureSaveState is declared
 * and never defined: nothing calls it. bool comes back in AL and float in XMM0 (a float argument goes in the XMM
 * register of its position): every function-pointer type below matches its header declaration exactly.
 * RootObjectFactory::create takes an Ogre::Vector3 and an Ogre::Quaternion BY VALUE. x64 passes a
 * by-value argument larger than 8 bytes as a pointer to a copy the caller makes; its function-pointer type
 * declares both by value, exactly as the header does, so the compiler makes that copy and passes its address -
 * what the game's function expects. Its last argument (float age, the twelfth counting `this`) goes on the stack.
 * GameDataContainer::runConstructor is an ORDINARY member function of the game, not a C++ constructor: forwarding it
 * runs the game's constructor in place on the caller's bytes and nothing of ours.
 * SaveManager::getSingleton is STATIC: there is no `this`, so its function-pointer type takes no argument at all.
 * UtilityT::projectToScreen takes the vector and both floats BY REFERENCE - pointers in RDX, R8 and R9, no XMM
 * register.
 * SPECIAL MEMBERS. TripleInt's three-int constructor and hand::operator bool are defined here outright (each is the
 * game's whole function, read in the exe's bytes); GameDataReference's two constructors, destructor and operator=,
 * GameDataContainer::runConstructor and hand's two constructors (hand() and the five-field one - rows Hand_ctor and
 * Hand_ctorFields) are forwarded through rows. The DLL imports nothing but the game's Ogre and MyGUI DLLs, the
 * VS2010 runtime and Windows system DLLs (build.bat step 4).
 *
 * AN UNFILLED ROW. A row is 0 only before AddrInit, or when it found no table or no usable one (addresses.h).
 * When a table DID load, every row it carries is filled even if the gate then refused (a byte mismatch, a missing
 * name): the guard below does not stop a call then - what keeps these forwarders idle on that road is that a
 * refusal installs nothing but the title-screen hook. The guard is for a 0 row only: the forwarder calls nothing
 * and answers the neutral value - null, false, 0.0f, an empty string, a zero vector, the empty handle - rather
 * than jumping to address 0. The five that return a REFERENCE (RootObjectBase::getHandle, GameWorld::
 * activeCharacters, Faction::getName, AbstractMovementBase::headingVector, GameDataContainer::
 * allRecords) answer a static empty object of the right type.
 *
 * C++03 (VS2010 v100). No __try in the forwarders: three of them hold std::string locals (C2712). The one __try
 * is BuildEmptyHandGuarded (hand, below), a small function holding only PODs (gog1 fold 2).
 */
#include "addresses.h"
#include "game/hand.h"
#include "game/RootObjectBase.h"
#include "game/GameWorld.h"
#include "game/Faction.h"
#include "game/FactionRelations.h"
#include "game/Character.h"
#include "game/CharMovement.h"
#include "game/CharBody.h"
#include "game/AnimationClass.h"
#include "game/AITaskSystem.h"
#include "game/GameDataManager.h"
#include "game/Inventory.h"
#include "game/Item.h"
#include "game/RootObjectFactory.h"
#include "game/MedicalSystem.h"
#include "game/GameSaveState.h"
#include "game/SaveManager.h"
#include "game/UtilityT.h"
#include "game/TripleInt.h"
#include "game/GameData.h"
#include <new>
#include <string>

#include "game/CombatClass.h"   /* T-186 (mig5): replaces the local stand-in declaration of the same four */

namespace {

/* Raw, suitably aligned storage for one object the GAME constructs (a by-value return). */
template <class T>
union RawSlot
{
    double align8;
    void* alignPtr;
    char bytes[sizeof(T)];
};

template <class T>
void DestroyIn(T* p) { p->~T(); }

const std::string kNoName;   /* Faction::getName's answer for an unfilled row - it returns a reference */
/* GameWorld::activeCharacters's answer for an unfilled row: an EMPTY set. boost builds no buckets until the
   first insert, so constructing (and destroying) it allocates nothing and runs no game code. */
const GameHashSet<Character*>::type kNoCharacters;
/* AbstractMovementBase::headingVector's answer for an unfilled row: the zero vector. */
const Ogre::Vector3 kNoVector(0.0f, 0.0f, 0.0f);
/* GameDataContainer::allRecords's answer for an unfilled row: an EMPTY map - like kNoCharacters, it allocates
   nothing and runs no game code. */
const GameHashMap<int, GameData*>::type kNoAllData;
/* RootObjectBase::getHandle's answer for an unfilled row. Plain zeroed storage (no constructor runs at DLL load);
   coop::GcBuildEmptyHand builds the empty handle in it once, from AddrInit. Never destroyed. */
RawSlot<hand> kNoHandSlot;

}   /* anonymous namespace */

/* ---- hand ------------------------------------------------------------------------------------------------ */
static unsigned long long kGcHandGetBuilding = 0; static coop::AddrReg kGcHandGetBuilding_reg("HandGetBuilding", &kGcHandGetBuilding);   /* Steam_1.0.65 0x791D70 */
static unsigned long long kGcHandGetCharacter = 0; static coop::AddrReg kGcHandGetCharacter_reg("Hand_asCharacter", &kGcHandGetCharacter);   /* Steam_1.0.65 0x7974F0 */
static unsigned long long kGcHandGetItem = 0; static coop::AddrReg kGcHandGetItem_reg("Hand_asItem", &kGcHandGetItem);   /* Steam_1.0.65 0x791D90 */
static unsigned long long kGcHandGetPlatoon = 0; static coop::AddrReg kGcHandGetPlatoon_reg("Hand_getSquad", &kGcHandGetPlatoon);   /* Steam_1.0.65 0x791CF0 */
static unsigned long long kGcHandGetRootObjectBase = 0; static coop::AddrReg kGcHandGetRootObjectBase_reg("Hand_asObjectBase", &kGcHandGetRootObjectBase);   /* Steam_1.0.65 0x79C890 */
static unsigned long long kGcHandToString = 0; static coop::AddrReg kGcHandToString_reg("Hand_asText", &kGcHandToString);   /* Steam_1.0.65 0x9B4880 */
static unsigned long long kGcHandCtor = 0; static coop::AddrReg kGcHandCtor_reg("Hand_ctor", &kGcHandCtor);   /* Steam_1.0.65 0x0CCFC0 */
static unsigned long long kGcHandCtorFields = 0; static coop::AddrReg kGcHandCtorFields_reg("Hand_ctorFields", &kGcHandCtorFields);   /* Steam_1.0.65 0x0CCFF0 */

/* hand's two constructors (mig4 G3b): FORWARDED to the game's own. game/hand.h declares hand with no virtual
   function, so these C++ constructors write nothing themselves (no vtable pointer, no member initialiser): the
   game's constructor writes what it always wrote. hand() (0xCCFC0) writes the vtable pointer (RVA 0x16842D0),
   one qword 0xB at +0x8 (type RECORD_NONE, container 0) and index 0, and leaves containerStamp and serial
   unwritten; the five-field one (0xCCFF0) writes the vtable pointer and all five fields. RCX = this; index,
   serial and type in RDX, R8, R9; container and containerStamp on the stack.
   AN UNFILLED ROW (the file comment): the fields the game writes, from the arguments, every one of them, and a
   NULL vtable pointer - with no row there is no address for it. coop::GcBuildEmptyHand is on that road: AddrInit
   calls it BEFORE the table loads, so the empty handle RootObjectBase::getHandle answers for its own unfilled
   row carries a null vtable pointer. That answer is reached only
   when no usable table loaded, where nothing but the title-screen hook is installed. */
hand::hand()
{
    typedef hand* (*Fn)(void* self);
    const unsigned long long a = coop::AddrAbs(kGcHandCtor);
    if (a != 0) { ((Fn)a)(this); return; }
    vtbl_ = 0;
    type = RECORD_NONE;
    container = 0;
    containerStamp = 0;
    index = 0;
    serial = 0;
}

hand::hand(unsigned int _index, unsigned int _serial, itemType _type, unsigned int _container, unsigned int _containerSerial)
{
    typedef hand* (*Fn)(void* self, unsigned int index, unsigned int serial, itemType type, unsigned int container,
                        unsigned int containerStamp);
    const unsigned long long a = coop::AddrAbs(kGcHandCtorFields);
    if (a != 0) { ((Fn)a)(this, _index, _serial, _type, _container, _containerSerial); return; }
    vtbl_ = 0;
    type = _type;
    container = _container;
    containerStamp = _containerSerial;
    index = _index;
    serial = _serial;
}

Building* hand::asBuilding() const
{
    typedef Building* (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcHandGetBuilding);
    return a != 0 ? ((Fn)a)(this) : 0;
}

Character* hand::getCharacter() const
{
    typedef Character* (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcHandGetCharacter);
    return a != 0 ? ((Fn)a)(this) : 0;
}

Item* hand::getItem() const
{
    typedef Item* (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcHandGetItem);
    return a != 0 ? ((Fn)a)(this) : 0;
}

Platoon* hand::getSquad() const
{
    typedef Platoon* (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcHandGetPlatoon);
    return a != 0 ? ((Fn)a)(this) : 0;
}

RootObjectBase* hand::asObjectBase() const
{
    typedef RootObjectBase* (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcHandGetRootObjectBase);
    return a != 0 ? ((Fn)a)(this) : 0;
}

/* BY VALUE: RCX = this, RDX = raw storage the game constructs the string in. */
std::string hand::toString() const
{
    typedef std::string* (*Fn)(const void* self, std::string* retStorage);
    std::string out;
    const unsigned long long a = coop::AddrAbs(kGcHandToString);
    if (a == 0) return out;
    RawSlot<std::string> raw;
    std::string* built = ((Fn)a)(this, (std::string*)raw.bytes);
    out.swap(*built);
    DestroyIn(built);
    return out;
}

/* ---- RootObjectBase -------------------------------------------------------------------------------------- */
static unsigned long long kGcRobGetFaction = 0; static coop::AddrReg kGcRobGetFaction_reg("RootObjectBase_getOwnerFactionDirect", &kGcRobGetFaction);   /* Steam_1.0.65 0x593BB0 */
static unsigned long long kGcRobGetGameData = 0; static coop::AddrReg kGcRobGetGameData_reg("RootObjectBase_getRecordDirect", &kGcRobGetGameData);   /* Steam_1.0.65 0x0D1D30 */
static unsigned long long kGcRobGetName = 0; static coop::AddrReg kGcRobGetName_reg("RootObjectBase_getShownName", &kGcRobGetName);   /* Steam_1.0.65 0x0D3CA0 */
static unsigned long long kGcRobGetHandle = 0; static coop::AddrReg kGcRobGetHandle_reg("RootObjectBase_selfHandle", &kGcRobGetHandle);   /* Steam_1.0.65 0x0B85D0 */

Faction* RootObjectBase::getOwnerFactionDirect() const
{
    typedef Faction* (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcRobGetFaction);
    return a != 0 ? ((Fn)a)(this) : 0;
}

GameData* RootObjectBase::getRecordDirect() const
{
    typedef GameData* (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcRobGetGameData);
    return a != 0 ? ((Fn)a)(this) : 0;
}

/* BY VALUE: RCX = this, RDX = raw storage the game constructs the string in. */
std::string RootObjectBase::getShownNameDirect() const
{
    typedef std::string* (*Fn)(const void* self, std::string* retStorage);
    std::string out;
    const unsigned long long a = coop::AddrAbs(kGcRobGetName);
    if (a == 0) return out;
    RawSlot<std::string> raw;
    std::string* built = ((Fn)a)(this, (std::string*)raw.bytes);
    out.swap(*built);
    DestroyIn(built);
    return out;
}

/* A reference return: for an unfilled row, the empty handle. hand has a vtable, so the empty one must be BUILT by
   hand's own constructor (the game's null handle) - never zero bytes. It is built ONCE, by coop::GcBuildEmptyHand
   below, which AddrInit calls first - on the plugin's start thread, before any hook exists - so every caller finds
   it built, and nothing of the game runs while this DLL loads. (mig3 C3: it was a function-local static; VS2010
   sets such a static's guard flag BEFORE constructing it, so a second thread's first use could have read it
   unbuilt.) */
const hand& RootObjectBase::getHandle() const
{
    typedef const hand* (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcRobGetHandle);
    if (a != 0) return *((Fn)a)(this);
    return *(const hand*)kNoHandSlot.bytes;
}

namespace {
/* gog1 fold 2: the placement new in a function of its own, never inlined, so the __try below holds no object that
   needs unwinding (C2712 - this file is built with /EHsc). */
__declspec(noinline) void BuildEmptyHandRaw()
{
    new (kNoHandSlot.bytes) hand();
}
/* PODs only. Once the Hand_ctor row is bound, hand() calls the GAME's constructor - the only game code AddrInit runs -
   so a fault there (a pattern or a table that named the wrong place) is caught HERE and becomes AddrInit's refusal
   (addresses.cpp, HandCtorFaulted) instead of a crash. The filter takes every exception, a C++ one included. */
int BuildEmptyHandGuarded()
{
    __try { BuildEmptyHandRaw(); return 1; }
    __except (1 /* EXCEPTION_EXECUTE_HANDLER */) { return 0; }
}
}   /* anonymous namespace */

/* Called from AddrInit only (see getHandle above and addresses.h): 1 built, 0 the game's constructor faulted. */
int coop::GcBuildEmptyHand()
{
    return BuildEmptyHandGuarded();
}

/* ---- GameWorld ------------------------------------------------------------------------------------------- */
static unsigned long long kGcWorldDestroy = 0; static coop::AddrReg kGcWorldDestroy_reg("WorldDestroy", &kGcWorldDestroy);   /* Steam_1.0.65 0x798F50 */
static unsigned long long kGcWorldCharUpdateList = 0; static coop::AddrReg kGcWorldCharUpdateList_reg("GameWorld_activeCharacters", &kGcWorldCharUpdateList);   /* Steam_1.0.65 0x6638D0 */
static unsigned long long kGcWorldFrameSpeed = 0; static coop::AddrReg kGcWorldFrameSpeed_reg("GameWorld_frameSpeedScale", &kGcWorldFrameSpeed);   /* Steam_1.0.65 0x66BCD0 */
static unsigned long long kGcWorldIsPaused = 0; static coop::AddrReg kGcWorldIsPaused_reg("GameWorld_gamePaused", &kGcWorldIsPaused);   /* Steam_1.0.65 0x0DEDC0 */

bool GameWorld::destroy(RootObject* obj, bool justUnloaded, const char* debugInfo)
{
    typedef bool (*Fn)(void* self, RootObject* obj, bool justUnloaded, const char* debugInfo);
    const unsigned long long a = coop::AddrAbs(kGcWorldDestroy);
    return a != 0 ? ((Fn)a)(this, obj, justUnloaded, debugInfo) : false;
}

/* A reference return (RAX carries it as a pointer): for an unfilled row, the static empty set. */
const GameHashSet<Character*>::type& GameWorld::activeCharacters() const
{
    typedef const GameHashSet<Character*>::type* (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcWorldCharUpdateList);
    return a != 0 ? *((Fn)a)(this) : kNoCharacters;
}

float GameWorld::frameSpeedScale() const
{
    typedef float (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcWorldFrameSpeed);
    return a != 0 ? ((Fn)a)(this) : 0.0f;
}

/* T-573: the 0.0 above for an unbound row is not a speed; PauseSnapshot asks this first. */
bool GameWorld::frameSpeedScaleBound() const
{
    return coop::AddrAbs(kGcWorldFrameSpeed) != 0;
}

bool GameWorld::isPaused() const
{
    typedef bool (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcWorldIsPaused);
    return a != 0 ? ((Fn)a)(this) : false;
}

/* ---- Faction --------------------------------------------------------------------------------------------- */
static unsigned long long kGcFacNewEmptyActive = 0; static coop::AddrReg kGcFacNewEmptyActive_reg("Faction_spawnEmptySquad", &kGcFacNewEmptyActive);   /* Steam_1.0.65 0x7F3880 */
static unsigned long long kGcFacPlatoonUnloaded = 0; static coop::AddrReg kGcFacPlatoonUnloaded_reg("Faction_addUnloadedSquad", &kGcFacPlatoonUnloaded);   /* Steam_1.0.65 0x7F24E0 */
static unsigned long long kGcFacDestroyPlatoon = 0; static coop::AddrReg kGcFacDestroyPlatoon_reg("DestroyPlatoon", &kGcFacDestroyPlatoon);   /* Steam_1.0.65 0x6BA680 */
static unsigned long long kGcFacGetName = 0; static coop::AddrReg kGcFacGetName_reg("Faction_factionName", &kGcFacGetName);   /* Steam_1.0.65 0x286BF0 */
static unsigned long long kGcFacUnloadedPlatoons = 0; static coop::AddrReg kGcFacUnloadedPlatoons_reg("Faction_unloadedSquads", &kGcFacUnloadedPlatoons);   /* Steam_1.0.65 0x37DAF0 */
static unsigned long long kGcFacSetName = 0; static coop::AddrReg kGcFacSetName_reg("Faction_renameTo", &kGcFacSetName);   /* Steam_1.0.65 0x385670 */

Platoon* Faction::spawnEmptySquad(GameData* squadTemplate, bool permanent, const Ogre::Vector3& p)
{
    typedef Platoon* (*Fn)(void* self, GameData* squadTemplate, bool permanent, const Ogre::Vector3* p);
    const unsigned long long a = coop::AddrAbs(kGcFacNewEmptyActive);
    return a != 0 ? ((Fn)a)(this, squadTemplate, permanent, &p) : 0;
}

void Faction::addUnloadedSquad(GameData* platoonstate, GameDataContainer* charactersState, GameData* squadTemplate,
                                    const Ogre::Vector3& pos, bool persistent)
{
    typedef void (*Fn)(void* self, GameData* platoonstate, GameDataContainer* charactersState, GameData* squadTemplate,
                       const Ogre::Vector3* pos, bool persistent);
    const unsigned long long a = coop::AddrAbs(kGcFacPlatoonUnloaded);
    if (a != 0) ((Fn)a)(this, platoonstate, charactersState, squadTemplate, &pos, persistent);
}

void Faction::removeSquad(Platoon* platoon)
{
    typedef void (*Fn)(void* self, Platoon* platoon);
    const unsigned long long a = coop::AddrAbs(kGcFacDestroyPlatoon);
    if (a != 0) ((Fn)a)(this, platoon);
}

const lektor<Platoon*>* Faction::unloadedSquads() const
{
    typedef const lektor<Platoon*>* (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcFacUnloadedPlatoons);
    return a != 0 ? ((Fn)a)(this) : 0;
}

const std::string& Faction::getName()
{
    typedef const std::string* (*Fn)(void* self);
    const unsigned long long a = coop::AddrAbs(kGcFacGetName);
    return a != 0 ? *((Fn)a)(this) : kNoName;
}

void Faction::setName(const std::string& _name)
{
    typedef void (*Fn)(void* self, const std::string* name);
    const unsigned long long a = coop::AddrAbs(kGcFacSetName);
    if (a != 0) ((Fn)a)(this, &_name);
}

/* ---- FactionDirectory -------------------------------------------------------------------------------------- */
static unsigned long long kGcFmAllFactions = 0; static coop::AddrReg kGcFmAllFactions_reg("FactionDirectory_allFactions", &kGcFmAllFactions);   /* Steam_1.0.65 0x877AB0 */
static unsigned long long kGcFmByName = 0; static coop::AddrReg kGcFmByName_reg("FactionDirectory_findFactionByName", &kGcFmByName);   /* Steam_1.0.65 0x2E7910 */
static unsigned long long kGcFmByStringId = 0; static coop::AddrReg kGcFmByStringId_reg("FactionDirectory_findFactionById", &kGcFmByStringId);   /* Steam_1.0.65 0x2E79E0 */
static unsigned long long kGcFmGetOrCreate = 0; static coop::AddrReg kGcFmGetOrCreate_reg("FactionDirectory_findOrAddFaction", &kGcFmGetOrCreate);   /* Steam_1.0.65 0x2E7650 */

const lektor<Faction*>* FactionDirectory::allFactions()
{
    typedef const lektor<Faction*>* (*Fn)(void* self);
    const unsigned long long a = coop::AddrAbs(kGcFmAllFactions);
    return a != 0 ? ((Fn)a)(this) : 0;
}

Faction* FactionDirectory::findFactionByName(const std::string& name)
{
    typedef Faction* (*Fn)(void* self, const std::string* name);
    const unsigned long long a = coop::AddrAbs(kGcFmByName);
    return a != 0 ? ((Fn)a)(this, &name) : 0;
}

Faction* FactionDirectory::findFactionById(const std::string& sid)
{
    typedef Faction* (*Fn)(void* self, const std::string* sid);
    const unsigned long long a = coop::AddrAbs(kGcFmByStringId);
    return a != 0 ? ((Fn)a)(this, &sid) : 0;
}

/* The GameData* overload (the only one the plugin calls). */
Faction* FactionDirectory::findOrAddFaction(GameData* data)
{
    typedef Faction* (*Fn)(void* self, GameData* data);
    const unsigned long long a = coop::AddrAbs(kGcFmGetOrCreate);
    return a != 0 ? ((Fn)a)(this, data) : 0;
}

/* ---- FactionRelations ------------------------------------------------------------------------------------ */
static unsigned long long kGcFrRelation = 0; static coop::AddrReg kGcFrRelation_reg("FactionRelations_relationTo", &kGcFrRelation);   /* Steam_1.0.65 0x6B20B0 */

float FactionRelations::relationTo(Faction* p)
{
    typedef float (*Fn)(void* self, Faction* p);
    const unsigned long long a = coop::AddrAbs(kGcFrRelation);
    return a != 0 ? ((Fn)a)(this, p) : 0.0f;
}

/* ---- Character (mig3 C2) ------------------------------------------------------------------------------------- */
static unsigned long long kGcChrGetAI = 0; static coop::AddrReg kGcChrGetAI_reg("Character_getBrain", &kGcChrGetAI);   /* Steam_1.0.65 0x268690 */
static unsigned long long kGcChrGetCombatClass = 0; static coop::AddrReg kGcChrGetCombatClass_reg("Character_getCombat", &kGcChrGetCombatClass);   /* Steam_1.0.65 0x5C8820 */
static unsigned long long kGcChrGetMoveSpeed = 0; static coop::AddrReg kGcChrGetMoveSpeed_reg("Character_currentMoveSpeed", &kGcChrGetMoveSpeed);   /* Steam_1.0.65 0x5C7940 */
static unsigned long long kGcChrGetPlatoon = 0; static coop::AddrReg kGcChrGetPlatoon_reg("Character_getSquad", &kGcChrGetPlatoon);   /* Steam_1.0.65 0x790F70 */
static unsigned long long kGcChrGetPosition = 0; static coop::AddrReg kGcChrGetPosition_reg("Character_worldPosition", &kGcChrGetPosition);   /* Steam_1.0.65 0x5CDBF0 */
static unsigned long long kGcChrGetProne = 0; static coop::AddrReg kGcChrGetProne_reg("Character_poseState", &kGcChrGetProne);   /* Steam_1.0.65 0x5C7380 */
static unsigned long long kGcChrIsDead = 0; static coop::AddrReg kGcChrIsDead_reg("Character_hasDied", &kGcChrIsDead);   /* Steam_1.0.65 0x620B20 */
static unsigned long long kGcChrIsPlayer = 0; static coop::AddrReg kGcChrIsPlayer_reg("Character_isPlayerControlled", &kGcChrIsPlayer);   /* Steam_1.0.65 0x790B30 */
static unsigned long long kGcChrIsRagdoll = 0; static coop::AddrReg kGcChrIsRagdoll_reg("Character_inRagdoll", &kGcChrIsRagdoll);   /* Steam_1.0.65 0x7D08A0 */
static unsigned long long kGcChrLoadSerialise = 0; static coop::AddrReg kGcChrLoadSerialise_reg("Character_restoreSaveState", &kGcChrLoadSerialise);   /* Steam_1.0.65 0x6264C0 */
static unsigned long long kGcChrSetProne = 0; static coop::AddrReg kGcChrSetProne_reg("Character_setPoseState", &kGcChrSetProne);   /* Steam_1.0.65 0x5C7390 */
static unsigned long long kGcChrSetSquadType = 0; static coop::AddrReg kGcChrSetSquadType_reg("Character_setSquadRole", &kGcChrSetSquadType);   /* Steam_1.0.65 0x620B30 */

AI* Character::getBrain()
{
    typedef AI* (*Fn)(void* self);
    const unsigned long long a = coop::AddrAbs(kGcChrGetAI);
    return a != 0 ? ((Fn)a)(this) : 0;
}

CombatClass* Character::getCombat() const
{
    typedef CombatClass* (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcChrGetCombatClass);
    return a != 0 ? ((Fn)a)(this) : 0;
}

ActivePlatoon* Character::getSquad() const
{
    typedef ActivePlatoon* (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcChrGetPlatoon);
    return a != 0 ? ((Fn)a)(this) : 0;
}

float Character::currentMoveSpeed() const
{
    typedef float (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcChrGetMoveSpeed);
    return a != 0 ? ((Fn)a)(this) : 0.0f;
}

/* BY VALUE: RCX = this, RDX = raw storage the game constructs the vector in. */
Ogre::Vector3 Character::worldPosition()
{
    typedef Ogre::Vector3* (*Fn)(void* self, Ogre::Vector3* retStorage);
    const unsigned long long a = coop::AddrAbs(kGcChrGetPosition);
    if (a == 0) return kNoVector;
    RawSlot<Ogre::Vector3> raw;
    return *((Fn)a)(this, (Ogre::Vector3*)raw.bytes);
}

PoseState Character::poseState() const
{
    typedef PoseState (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcChrGetProne);
    return a != 0 ? ((Fn)a)(this) : POSE_STANDING;
}

void Character::setPoseState(PoseState p)
{
    typedef void (*Fn)(void* self, PoseState p);
    const unsigned long long a = coop::AddrAbs(kGcChrSetProne);
    if (a != 0) ((Fn)a)(this, p);
}

void Character::restoreSaveState(SavedObjectState* state)
{
    typedef void (*Fn)(void* self, SavedObjectState* state);
    const unsigned long long a = coop::AddrAbs(kGcChrLoadSerialise);
    if (a != 0) ((Fn)a)(this, state);
}

void Character::setSquadRole(SquadRole memType)
{
    typedef void (*Fn)(void* self, SquadRole memType);
    const unsigned long long a = coop::AddrAbs(kGcChrSetSquadType);
    if (a != 0) ((Fn)a)(this, memType);
}

bool Character::hasDied() const
{
    typedef bool (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcChrIsDead);
    return a != 0 ? ((Fn)a)(this) : false;
}

bool Character::isPlayerControlled() const
{
    typedef bool (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcChrIsPlayer);
    return a != 0 ? ((Fn)a)(this) : false;
}

bool Character::inRagdoll() const
{
    typedef bool (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcChrIsRagdoll);
    return a != 0 ? ((Fn)a)(this) : false;
}

/* ---- AbstractMovementBase (mig3 C2) -------------------------------------------------------------------------- */
static unsigned long long kGcAmbGetDestination = 0; static coop::AddrReg kGcAmbGetDestination_reg("AbstractMovementBase_pathGoal", &kGcAmbGetDestination);   /* Steam_1.0.65 0x65DD40 */
static unsigned long long kGcAmbGetFacing = 0; static coop::AddrReg kGcAmbGetFacing_reg("AbstractMovementBase_headingVector", &kGcAmbGetFacing);   /* Steam_1.0.65 0x2AE000 */
static unsigned long long kGcAmbSetSpeedOrders = 0; static coop::AddrReg kGcAmbSetSpeedOrders_reg("AbstractMovementBase_orderMoveSpeed", &kGcAmbSetSpeedOrders);   /* Steam_1.0.65 0x3320D0 */

/* BY VALUE: RCX = this, RDX = raw storage the game constructs the vector in. */
Ogre::Vector3 AbstractMovementBase::getDestination() const
{
    typedef Ogre::Vector3* (*Fn)(const void* self, Ogre::Vector3* retStorage);
    const unsigned long long a = coop::AddrAbs(kGcAmbGetDestination);
    if (a == 0) return kNoVector;
    RawSlot<Ogre::Vector3> raw;
    return *((Fn)a)(this, (Ogre::Vector3*)raw.bytes);
}

const Ogre::Vector3& AbstractMovementBase::headingVector() const
{
    typedef const Ogre::Vector3* (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcAmbGetFacing);
    return a != 0 ? *((Fn)a)(this) : kNoVector;
}

void AbstractMovementBase::orderMoveSpeed(SpeedOrder speed)
{
    typedef void (*Fn)(void* self, SpeedOrder speed);
    const unsigned long long a = coop::AddrAbs(kGcAmbSetSpeedOrders);
    if (a != 0) ((Fn)a)(this, speed);
}

/* ---- CharMovement (mig3 C2) ---------------------------------------------------------------------------------- */
static unsigned long long kGcCmPathFailed = 0; static coop::AddrReg kGcCmPathFailed_reg("CharMovement_lastPathFailed", &kGcCmPathFailed);   /* Steam_1.0.65 0x65DDA0 */
static unsigned long long kGcCmSetDestination = 0; static coop::AddrReg kGcCmSetDestination_reg("CharMovement_setPathGoal", &kGcCmSetDestination);   /* Steam_1.0.65 0x6607E0 */
static unsigned long long kGcCmSetDirect = 0; static coop::AddrReg kGcCmSetDirect_reg("CharMovement_steerDirectly", &kGcCmSetDirect);   /* Steam_1.0.65 0x3332C0 */
static unsigned long long kGcCmSetMode = 0; static coop::AddrReg kGcCmSetMode_reg("CharMovement_setSteeringMode", &kGcCmSetMode);   /* Steam_1.0.65 0x65E680 */
static unsigned long long kGcCmTeleport = 0; static coop::AddrReg kGcCmTeleport_reg("CharMovement_teleportTo", &kGcCmTeleport);   /* Steam_1.0.65 0x65DEB0 */

void CharMovement::teleportTo(const Ogre::Vector3& p, int floor)
{
    typedef void (*Fn)(void* self, const Ogre::Vector3* p, int floor);
    const unsigned long long a = coop::AddrAbs(kGcCmTeleport);
    if (a != 0) ((Fn)a)(this, &p, floor);
}

bool CharMovement::lastPathFailed() const
{
    typedef bool (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcCmPathFailed);
    return a != 0 ? ((Fn)a)(this) : false;
}

void CharMovement::setPathGoal(const Ogre::Vector3& dest, PathPriority priority, bool notVertical)
{
    typedef void (*Fn)(void* self, const Ogre::Vector3* dest, PathPriority priority, bool notVertical);
    const unsigned long long a = coop::AddrAbs(kGcCmSetDestination);
    if (a != 0) ((Fn)a)(this, &dest, priority, notVertical);
}

void CharMovement::setSteeringMode(SteeringMode mode)
{
    typedef void (*Fn)(void* self, SteeringMode mode);
    const unsigned long long a = coop::AddrAbs(kGcCmSetMode);
    if (a != 0) ((Fn)a)(this, mode);
}

void CharMovement::steerDirectly(const Ogre::Vector3& d, float limit)
{
    typedef void (*Fn)(void* self, const Ogre::Vector3* d, float limit);
    const unsigned long long a = coop::AddrAbs(kGcCmSetDirect);
    if (a != 0) ((Fn)a)(this, &d, limit);
}

/* ---- CharBody (mig3 C2) -------------------------------------------------------------------------------------- */
static unsigned long long kGcCbEndAction = 0; static coop::AddrReg kGcCbEndAction_reg("CharBody_finishAction", &kGcCbEndAction);   /* Steam_1.0.65 0x5C5CE0 */
static unsigned long long kGcCbSetAction = 0; static coop::AddrReg kGcCbSetAction_reg("CharBody_startAction", &kGcCbSetAction);   /* Steam_1.0.65 0x5C5BF0 */

bool CharBody::startAction(TaskType t, RootObject* target)
{
    typedef bool (*Fn)(void* self, TaskType t, RootObject* target);
    const unsigned long long a = coop::AddrAbs(kGcCbSetAction);
    return a != 0 ? ((Fn)a)(this, t, target) : false;
}

void CharBody::finishAction()
{
    typedef void (*Fn)(void* self);
    const unsigned long long a = coop::AddrAbs(kGcCbEndAction);
    if (a != 0) ((Fn)a)(this);
}

/* ---- AnimationClassBase / AnimationClass (mig3 C2) ----------------------------------------------------------- */
static unsigned long long kGcAcbActionWeight = 0; static coop::AddrReg kGcAcbActionWeight_reg("AnimClassBase_actionBlendWeight", &kGcAcbActionWeight);   /* Steam_1.0.65 0x5B2840 */
static unsigned long long kGcAcbStillPlaying = 0; static coop::AddrReg kGcAcbStillPlaying_reg("AnimClassBase_isActionAnimating", &kGcAcbStillPlaying);   /* Steam_1.0.65 0x5B2A20 */
static unsigned long long kGcAcIncapable = 0; static coop::AddrReg kGcAcIncapable_reg("AnimationClass_blocksWaypointMovement", &kGcAcIncapable);   /* Steam_1.0.65 0x51BDE0 */
static unsigned long long kGcAcSetMaxSpeed = 0; static coop::AddrReg kGcAcSetMaxSpeed_reg("AnimationClass_applySpeedCap", &kGcAcSetMaxSpeed);   /* Steam_1.0.65 0x51BED0 (T-189 runboost) */
static unsigned long long kGcAcSetPosition = 0; static coop::AddrReg kGcAcSetPosition_reg("AnimationClass_placeAnimationRoot", &kGcAcSetPosition);   /* Steam_1.0.65 0x5B1070 */

float AnimationClassBase::actionBlendWeight()
{
    typedef float (*Fn)(void* self);
    const unsigned long long a = coop::AddrAbs(kGcAcbActionWeight);
    return a != 0 ? ((Fn)a)(this) : 0.0f;
}

bool AnimationClassBase::isActionAnimating()
{
    typedef bool (*Fn)(void* self);
    const unsigned long long a = coop::AddrAbs(kGcAcbStillPlaying);
    return a != 0 ? ((Fn)a)(this) : false;
}

bool AnimationClass::blocksWaypointMovement()
{
    typedef bool (*Fn)(void* self);
    const unsigned long long a = coop::AddrAbs(kGcAcIncapable);
    return a != 0 ? ((Fn)a)(this) : false;
}

/* T-189 runboost: `this` in RCX, the float in XMM1 (its position), nothing returned (read at 0x51BED0). */
void AnimationClass::applySpeedCap(float speed)
{
    typedef void (*Fn)(void* self, float speed);
    const unsigned long long a = coop::AddrAbs(kGcAcSetMaxSpeed);
    if (a != 0) ((Fn)a)(this, speed);
}

/* The one-argument overload (the only one the plugin calls). */
void AnimationClass::setPosition(const Ogre::Vector3& pos)
{
    typedef void (*Fn)(void* self, const Ogre::Vector3* pos);
    const unsigned long long a = coop::AddrAbs(kGcAcSetPosition);
    if (a != 0) ((Fn)a)(this, &pos);
}

/* ---- OrdersReceiver / AITaskSytem (mig3 C2) ------------------------------------------------------------------ */
static unsigned long long kGcOrAddOrder = 0; static coop::AddrReg kGcOrAddOrder_reg("OrdersReceiver_issueOrder", &kGcOrAddOrder);   /* Steam_1.0.65 0x5078F0 */
static unsigned long long kGcOrClearGoals = 0; static coop::AddrReg kGcOrClearGoals_reg("OrdersReceiver_dropGoals", &kGcOrClearGoals);   /* Steam_1.0.65 0x509F40 */
static unsigned long long kGcOrClearOrders = 0; static coop::AddrReg kGcOrClearOrders_reg("OrdersReceiver_dropAllOrders", &kGcOrClearOrders);   /* Steam_1.0.65 0x506A10 */
static unsigned long long kGcOrHasOrders = 0; static coop::AddrReg kGcOrHasOrders_reg("OrdersReceiver_hasPendingOrders", &kGcOrHasOrders);   /* Steam_1.0.65 0x68FAB0 */
static unsigned long long kGcAtsGoalString = 0; static coop::AddrReg kGcAtsGoalString_reg("AITaskSytem_describeCurrentGoal", &kGcAtsGoalString);   /* Steam_1.0.65 0x50E930 */

void OrdersReceiver::issueOrder(TaskType t, const hand& subject, const Ogre::Vector3& location, bool clearOld, bool shift)
{
    typedef void (*Fn)(void* self, TaskType t, const hand* subject, const Ogre::Vector3* location, bool clearOld, bool shift);
    const unsigned long long a = coop::AddrAbs(kGcOrAddOrder);
    if (a != 0) ((Fn)a)(this, t, &subject, &location, clearOld, shift);
}

void OrdersReceiver::dropAllOrders()
{
    typedef void (*Fn)(void* self);
    const unsigned long long a = coop::AddrAbs(kGcOrClearOrders);
    if (a != 0) ((Fn)a)(this);
}

void OrdersReceiver::dropGoals()
{
    typedef void (*Fn)(void* self);
    const unsigned long long a = coop::AddrAbs(kGcOrClearGoals);
    if (a != 0) ((Fn)a)(this);
}

bool OrdersReceiver::hasPendingOrders() const
{
    typedef bool (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcOrHasOrders);
    return a != 0 ? ((Fn)a)(this) : false;
}

/* BY VALUE: RCX = this, RDX = raw storage the game constructs the string in. */
std::string AITaskSytem::describeCurrentGoal()
{
    typedef std::string* (*Fn)(void* self, std::string* retStorage);
    std::string out;
    const unsigned long long a = coop::AddrAbs(kGcAtsGoalString);
    if (a == 0) return out;
    RawSlot<std::string> raw;
    std::string* built = ((Fn)a)(this, (std::string*)raw.bytes);
    out.swap(*built);
    DestroyIn(built);
    return out;
}

/* ---- CombatClass (mig3 C2) ----------------------------------------------------------------------------------- */
static unsigned long long kGcCcGetAttackTarget = 0; static coop::AddrReg kGcCcGetAttackTarget_reg("CombatClass_currentTarget", &kGcCcGetAttackTarget);   /* Steam_1.0.65 0x33A2A0 */
static unsigned long long kGcCcInitCombatMode = 0; static coop::AddrReg kGcCcInitCombatMode_reg("CombatClass_enterCombat", &kGcCcInitCombatMode);   /* Steam_1.0.65 0x664F20 */
static unsigned long long kGcCcSetTargetHandle = 0; static coop::AddrReg kGcCcSetTargetHandle_reg("CombatClass_setTarget", &kGcCcSetTargetHandle);   /* Steam_1.0.65 0x664BC0 */
static unsigned long long kGcCcSetCombatState = 0; static coop::AddrReg kGcCcSetCombatState_reg("CombatClass_setSwordState", &kGcCcSetCombatState);   /* Steam_1.0.65 0x60C240 */

/* BY VALUE: RCX = this, RDX = raw storage the game constructs the hand in. The answer is rebuilt from the five
   fields by hand's five-field constructor - the same vtable and fields the game's copy constructor writes (see
   the file comment) - and returned as a temporary, so it is built straight in the caller's storage. */
hand CombatClass::currentTarget() const
{
    typedef hand* (*Fn)(const void* self, hand* retStorage);
    const unsigned long long a = coop::AddrAbs(kGcCcGetAttackTarget);
    if (a == 0) return hand();
    RawSlot<hand> raw;
    hand* built = ((Fn)a)(this, (hand*)raw.bytes);
    const unsigned int index = built->index, serial = built->serial, container = built->container,
                       containerStamp = built->containerStamp;
    const itemType type = built->type;
    DestroyIn(built);
    return hand(index, serial, type, container, containerStamp);
}

bool CombatClass::enterCombat(const hand& subject, int end, bool focusedTarget)
{
    typedef bool (*Fn)(void* self, const hand* subject, int end, bool focusedTarget);
    const unsigned long long a = coop::AddrAbs(kGcCcInitCombatMode);
    return a != 0 ? ((Fn)a)(this, &subject, end, focusedTarget) : false;
}

void CombatClass::setSwordState(swordStateEnum state)
{
    typedef void (*Fn)(void* self, swordStateEnum state);
    const unsigned long long a = coop::AddrAbs(kGcCcSetCombatState);
    if (a != 0) ((Fn)a)(this, state);
}

void CombatClass::setTarget(Character* c)
{
    typedef void (*Fn)(void* self, Character* c);
    const unsigned long long a = coop::AddrAbs(kGcCcSetTargetHandle);
    if (a != 0) ((Fn)a)(this, c);
}

/* ---- Inventory (mig3 C3) ------------------------------------------------------------------------------------- */
static unsigned long long kGcInvClearAll = 0; static coop::AddrReg kGcInvClearAll_reg("Inventory_emptyAll", &kGcInvClearAll);   /* Steam_1.0.65 0x749A70 */
static unsigned long long kGcInvGetSection = 0; static coop::AddrReg kGcInvGetSection_reg("Inventory_sectionNamed", &kGcInvGetSection);   /* Steam_1.0.65 0x747070 */
static unsigned long long kGcInvNotifyModified = 0; static coop::AddrReg kGcInvNotifyModified_reg("Inventory_markChanged", &kGcInvNotifyModified);   /* Steam_1.0.65 0x746A50 */

void Inventory::clearAll(bool destroy, bool skipUnique)
{
    typedef void (*Fn)(void* self, bool destroy, bool skipUnique);
    const unsigned long long a = coop::AddrAbs(kGcInvClearAll);
    if (a != 0) ((Fn)a)(this, destroy, skipUnique);
}

InventorySection* Inventory::getSection(const std::string& name) const
{
    typedef InventorySection* (*Fn)(const void* self, const std::string* name);
    const unsigned long long a = coop::AddrAbs(kGcInvGetSection);
    return a != 0 ? ((Fn)a)(this, &name) : 0;
}

void Inventory::markChanged()
{
    typedef void (*Fn)(void* self);
    const unsigned long long a = coop::AddrAbs(kGcInvNotifyModified);
    if (a != 0) ((Fn)a)(this);
}

/* ---- InventorySection (mig3 C3) ------------------------------------------------------------------------------ */
static unsigned long long kGcSecAddItem = 0; static coop::AddrReg kGcSecAddItem_reg("SecAddItem", &kGcSecAddItem);   /* Steam_1.0.65 0x748BE0 */
static unsigned long long kGcSecCanItemGoHere = 0; static coop::AddrReg kGcSecCanItemGoHere_reg("InventorySection_fitsAt", &kGcSecCanItemGoHere);   /* Steam_1.0.65 0x74B300 */
static unsigned long long kGcSecExistsInFootprint = 0; static coop::AddrReg kGcSecExistsInFootprint_reg("InventorySection_itemInArea", &kGcSecExistsInFootprint);   /* Steam_1.0.65 0x745BB0 */
static unsigned long long kGcSecGetItemAt = 0; static coop::AddrReg kGcSecGetItemAt_reg("InventorySection_itemAt", &kGcSecGetItemAt);   /* Steam_1.0.65 0x7461C0 */
static unsigned long long kGcSecRecalcWeight = 0; static coop::AddrReg kGcSecRecalcWeight_reg("InventorySection_recomputeWeight", &kGcSecRecalcWeight);   /* Steam_1.0.65 0x748620 */
static unsigned long long kGcSecSetEnabled = 0; static coop::AddrReg kGcSecSetEnabled_reg("InventorySection_setUsable", &kGcSecSetEnabled);   /* Steam_1.0.65 0x745360 */

void InventorySection::placeItemDirect(Item* item, int x, int y)
{
    typedef void (*Fn)(void* self, Item* item, int x, int y);
    const unsigned long long a = coop::AddrAbs(kGcSecAddItem);
    if (a != 0) ((Fn)a)(this, item, x, y);
}

bool InventorySection::fitsAt(Item* item, int x, int y)
{
    typedef bool (*Fn)(void* self, Item* item, int x, int y);
    const unsigned long long a = coop::AddrAbs(kGcSecCanItemGoHere);
    return a != 0 ? ((Fn)a)(this, item, x, y) : false;
}

bool InventorySection::itemInArea(Item* item, int x, int y)
{
    typedef bool (*Fn)(void* self, Item* item, int x, int y);
    const unsigned long long a = coop::AddrAbs(kGcSecExistsInFootprint);
    return a != 0 ? ((Fn)a)(this, item, x, y) : false;
}

Item* InventorySection::getItemAt(int x, int y)
{
    typedef Item* (*Fn)(void* self, int x, int y);
    const unsigned long long a = coop::AddrAbs(kGcSecGetItemAt);
    return a != 0 ? ((Fn)a)(this, x, y) : 0;
}

void InventorySection::recomputeWeight()
{
    typedef void (*Fn)(void* self);
    const unsigned long long a = coop::AddrAbs(kGcSecRecalcWeight);
    if (a != 0) ((Fn)a)(this);
}

void InventorySection::setEnabled(bool value)
{
    typedef void (*Fn)(void* self, bool value);
    const unsigned long long a = coop::AddrAbs(kGcSecSetEnabled);
    if (a != 0) ((Fn)a)(this, value);
}

/* ---- InventoryItemBase / Item (mig3 C3) ---------------------------------------------------------------------- */
static unsigned long long kGcIibIsStolen = 0; static coop::AddrReg kGcIibIsStolen_reg("InventoryItemBase_wasStolen", &kGcIibIsStolen);   /* Steam_1.0.65 0x79D9B0 */
static unsigned long long kGcItemSerialiseInInv = 0; static coop::AddrReg kGcItemSerialiseInInv_reg("Item_saveIntoInventoryRecord", &kGcItemSerialiseInInv);   /* Steam_1.0.65 0x75CBA0 */

bool InventoryItemBase::wasStolen(bool includeUnknown) const
{
    typedef bool (*Fn)(const void* self, bool includeUnknown);
    const unsigned long long a = coop::AddrAbs(kGcIibIsStolen);
    return a != 0 ? ((Fn)a)(this, includeUnknown) : false;
}

GameData* Item::saveIntoInventoryRecord(GameDataContainer* container, GameData* refList)
{
    typedef GameData* (*Fn)(void* self, GameDataContainer* container, GameData* refList);
    const unsigned long long a = coop::AddrAbs(kGcItemSerialiseInInv);
    return a != 0 ? ((Fn)a)(this, container, refList) : 0;
}

/* ---- GameDataContainer (mig3 C3) ----------------------------------------------------------------------------- */
static unsigned long long kGcGdcCreateNewData = 0; static coop::AddrReg kGcGdcCreateNewData_reg("GameDataContainer_newRecord", &kGcGdcCreateNewData);   /* Steam_1.0.65 0x6C0400 */
static unsigned long long kGcGdcGetAllData = 0; static coop::AddrReg kGcGdcGetAllData_reg("GameDataContainer_allRecords", &kGcGdcGetAllData);   /* Steam_1.0.65 0x36AFD0 */
static unsigned long long kGcGdcGetData = 0; static coop::AddrReg kGcGdcGetData_reg("GdcGetData", &kGcGdcGetData);   /* Steam_1.0.65 0x6BD190 */
static unsigned long long kGcGdcGetDataByName = 0; static coop::AddrReg kGcGdcGetDataByName_reg("GameDataContainer_findRecordByName", &kGcGdcGetDataByName);   /* Steam_1.0.65 0x6BFA50 */
static unsigned long long kGcGdcGetDataOfType = 0; static coop::AddrReg kGcGdcGetDataOfType_reg("GameDataContainer_listRecordsOfType", &kGcGdcGetDataOfType);   /* Steam_1.0.65 0x6C00D0 */
static unsigned long long kGcGdcRenameData = 0; static coop::AddrReg kGcGdcRenameData_reg("GameDataContainer_renameRecord", &kGcGdcRenameData);   /* Steam_1.0.65 0x6BF820 */

GameData* GameDataContainer::newRecord(itemType type, const std::string& forceID, const std::string& name)
{
    typedef GameData* (*Fn)(void* self, itemType type, const std::string* forceID, const std::string* name);
    const unsigned long long a = coop::AddrAbs(kGcGdcCreateNewData);
    return a != 0 ? ((Fn)a)(this, type, &forceID, &name) : 0;
}

/* The one-string overload (the only one the plugin calls). */
GameData* GameDataContainer::getData(const std::string& sid)
{
    typedef GameData* (*Fn)(void* self, const std::string* sid);
    const unsigned long long a = coop::AddrAbs(kGcGdcGetData);
    return a != 0 ? ((Fn)a)(this, &sid) : 0;
}

GameData* GameDataContainer::findRecordByName(const std::string& dataName, itemType category)
{
    typedef GameData* (*Fn)(void* self, const std::string* dataName, itemType category);
    const unsigned long long a = coop::AddrAbs(kGcGdcGetDataByName);
    return a != 0 ? ((Fn)a)(this, &dataName, category) : 0;
}

void GameDataContainer::listRecordsOfType(lektor<GameData*>& list, itemType type)
{
    typedef void (*Fn)(void* self, lektor<GameData*>* list, itemType type);
    const unsigned long long a = coop::AddrAbs(kGcGdcGetDataOfType);
    if (a != 0) ((Fn)a)(this, &list, type);
}

/* A reference return (RAX carries it as a pointer): for an unfilled row, the static empty map. */
const GameHashMap<int, GameData*>::type& GameDataContainer::allRecords() const
{
    typedef const GameHashMap<int, GameData*>::type* (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcGdcGetAllData);
    return a != 0 ? *((Fn)a)(this) : kNoAllData;
}

void GameDataContainer::renameRecord(GameData* data, const std::string& n)
{
    typedef void (*Fn)(void* self, GameData* data, const std::string* n);
    const unsigned long long a = coop::AddrAbs(kGcGdcRenameData);
    if (a != 0) ((Fn)a)(this, data, &n);
}

/* ---- RootObjectFactory / RootObjectContainer (mig3 C3) ------------------------------------------------------- */
static unsigned long long kGcRofCreate = 0; static coop::AddrReg kGcRofCreate_reg("RootObjectFactory_spawn", &kGcRofCreate);   /* Steam_1.0.65 0x582970 */
static unsigned long long kGcRofCreateItem = 0; static coop::AddrReg kGcRofCreateItem_reg("RootObjectFactory_spawnItem", &kGcRofCreateItem);   /* Steam_1.0.65 0x5825F0 */
static unsigned long long kGcRocSerialiseThings = 0; static coop::AddrReg kGcRocSerialiseThings_reg("RootObjectContainer_saveMembers", &kGcRocSerialiseThings);   /* Steam_1.0.65 0x36DC70 */

/* position and rotation BY VALUE (see the file comment): the function-pointer type keeps them by value. */
RootObjectBase* RootObjectFactory::create(GameData* data, Ogre::Vector3 position, bool isFromActiveLevelMod, Faction* owner,
                                          Ogre::Quaternion rotation, FactoryCallbackInterface* notifyTarget,
                                          RootObjectContainer* certainContainer, SavedObjectState* state, bool invisible,
                                          Building* homeBuilding, float age)
{
    typedef RootObjectBase* (*Fn)(void* self, GameData* data, Ogre::Vector3 position, bool isFromActiveLevelMod, Faction* owner,
                                  Ogre::Quaternion rotation, FactoryCallbackInterface* notifyTarget,
                                  RootObjectContainer* certainContainer, SavedObjectState* state, bool invisible,
                                  Building* homeBuilding, float age);
    const unsigned long long a = coop::AddrAbs(kGcRofCreate);
    return a != 0 ? ((Fn)a)(this, data, position, isFromActiveLevelMod, owner, rotation, notifyTarget, certainContainer,
                            state, invisible, homeBuilding, age) : 0;
}

/* The one-argument overload (the only one the plugin calls). */
Item* RootObjectFactory::createItem(GameData* itemState)
{
    typedef Item* (*Fn)(void* self, GameData* itemState);
    const unsigned long long a = coop::AddrAbs(kGcRofCreateItem);
    return a != 0 ? ((Fn)a)(this, itemState) : 0;
}

/* The four-argument overload (the only one the plugin calls). */
void RootObjectContainer::saveMembers(GameData* outputToInstanceCollectionOfSomeKind, GameDataContainer* source,
                                          PlacementTransform* offsetPosToSubtract, const std::string& mod)
{
    typedef void (*Fn)(void* self, GameData* outputToInstanceCollectionOfSomeKind, GameDataContainer* source,
                       PlacementTransform* offsetPosToSubtract, const std::string* mod);
    const unsigned long long a = coop::AddrAbs(kGcRocSerialiseThings);
    if (a != 0) ((Fn)a)(this, outputToInstanceCollectionOfSomeKind, source, offsetPosToSubtract, &mod);
}

/* ---- MedicalSystem / HealthPartStatus (mig3 C4) -------------------------------------------------------------- */
static unsigned long long kGcMedGetPart = 0; static coop::AddrReg kGcMedGetPart_reg("MedicalSystem_partAt", &kGcMedGetPart);   /* Steam_1.0.65 0x2AA5C0 */
static unsigned long long kGcMedGetPartCount = 0; static coop::AddrReg kGcMedGetPartCount_reg("MedicalSystem_countBodyParts", &kGcMedGetPartCount);   /* Steam_1.0.65 0x2AA5B0 */
static unsigned long long kGcMedValidate = 0; static coop::AddrReg kGcMedValidate_reg("MedicalSystem_clampHealth", &kGcMedValidate);   /* Steam_1.0.65 0x644B10 */
static unsigned long long kGcHpsUpdateDerived = 0; static coop::AddrReg kGcHpsUpdateDerived_reg("HealthPartStatus_recomputeHealth", &kGcHpsUpdateDerived);   /* Steam_1.0.65 0x643190 */

/* The index overload (the only one the plugin calls). */
MedicalSystem::HealthPartStatus* MedicalSystem::partAt(unsigned __int64 index)
{
    typedef MedicalSystem::HealthPartStatus* (*Fn)(void* self, unsigned __int64 index);
    const unsigned long long a = coop::AddrAbs(kGcMedGetPart);
    return a != 0 ? ((Fn)a)(this, index) : 0;
}

int MedicalSystem::countBodyParts() const
{
    typedef int (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcMedGetPartCount);
    return a != 0 ? ((Fn)a)(this) : 0;
}

void MedicalSystem::clampHealth()
{
    typedef void (*Fn)(void* self);
    const unsigned long long a = coop::AddrAbs(kGcMedValidate);
    if (a != 0) ((Fn)a)(this);
}

void MedicalSystem::HealthPartStatus::recomputeHealth()
{
    typedef void (*Fn)(void* self);
    const unsigned long long a = coop::AddrAbs(kGcHpsUpdateDerived);
    if (a != 0) ((Fn)a)(this);
}

/* ---- SavedObjectState (mig3 C4) --------------------------------------------------------------------------------- */
static unsigned long long kGcGssGetPos = 0; static coop::AddrReg kGcGssGetPos_reg("SavedObjectState_savedPosition", &kGcGssGetPos);   /* Steam_1.0.65 0x37DA00 */
static unsigned long long kGcGssNumStates = 0; static coop::AddrReg kGcGssNumStates_reg("SavedObjectState_stateCount", &kGcGssNumStates);   /* Steam_1.0.65 0x37F5D0 */

/* BY VALUE: RCX = this, RDX = the slot the game writes the vector in (plain data - nothing to destroy). */
Ogre::Vector3 SavedObjectState::savedPosition() const
{
    typedef Ogre::Vector3* (*Fn)(const void* self, Ogre::Vector3* retStorage);
    const unsigned long long a = coop::AddrAbs(kGcGssGetPos);
    if (a == 0) return kNoVector;
    RawSlot<Ogre::Vector3> raw;
    return *((Fn)a)(this, (Ogre::Vector3*)raw.bytes);
}

int SavedObjectState::stateCount() const
{
    typedef int (*Fn)(const void* self);
    const unsigned long long a = coop::AddrAbs(kGcGssNumStates);
    return a != 0 ? ((Fn)a)(this) : 0;
}

/* ---- SaveManager (mig3 C4) ----------------------------------------------------------------------------------- */
static unsigned long long kGcSmGetSingleton = 0; static coop::AddrReg kGcSmGetSingleton_reg("SaveManager_instancePtr", &kGcSmGetSingleton);   /* Steam_1.0.65 0x37DBC0 */
static unsigned long long kGcSmLoad = 0; static coop::AddrReg kGcSmLoad_reg("SaveManager_loadSave", &kGcSmLoad);   /* Steam_1.0.65 0x47AC10 */
static unsigned long long kGcSmSavesExist = 0; static coop::AddrReg kGcSmSavesExist_reg("SaveManager_anySavesExist", &kGcSmSavesExist);   /* Steam_1.0.65 0x36B540 */

/* STATIC: no `this`. */
SaveManager* SaveManager::getSingleton()
{
    typedef SaveManager* (*Fn)();
    const unsigned long long a = coop::AddrAbs(kGcSmGetSingleton);
    return a != 0 ? ((Fn)a)() : 0;
}

/* The one-string overload (the only one the plugin calls). */
void SaveManager::load(const std::string& name)
{
    typedef void (*Fn)(void* self, const std::string* name);
    const unsigned long long a = coop::AddrAbs(kGcSmLoad);
    if (a != 0) ((Fn)a)(this, &name);
}

bool SaveManager::anySavesExist()
{
    typedef bool (*Fn)(void* self);
    const unsigned long long a = coop::AddrAbs(kGcSmSavesExist);
    return a != 0 ? ((Fn)a)(this) : false;
}

/* ---- UtilityT (mig3 C4) -------------------------------------------------------------------------------------- */
static unsigned long long kGcUtWorldToScreenPX = 0; static coop::AddrReg kGcUtWorldToScreenPX_reg("UtilityT_projectToScreen", &kGcUtWorldToScreenPX);   /* Steam_1.0.65 0x9B2520 */

/* pos, x and y BY REFERENCE (pointers in RDX, R8, R9). An unfilled row answers false and leaves x and y alone. */
bool UtilityT::projectToScreen(const Ogre::Vector3& pos, float& x, float& y)
{
    typedef bool (*Fn)(void* self, const Ogre::Vector3* pos, float* x, float* y);
    const unsigned long long a = coop::AddrAbs(kGcUtWorldToScreenPX);
    return a != 0 ? ((Fn)a)(this, &pos, &x, &y) : false;
}

/* ---- mig4 B (RE_Kenshi migration stage 4 part B) --------------------------------------------------------------- */
/* TripleInt(int,int,int) and hand::operator bool are DEFINED here, not forwarded, and need no row: each is the game's
   whole function, read in the exe's bytes. Steam_1.0.65 0x6DF10 stores the three ints in order and returns this;
   0x64540 answers whether the type at +0x8 is not 0xB. 0xB is RECORD_NONE (Enums.h), the engine's own empty marker:
   the value hand's default constructor writes (0xCCFC0), combat.cpp's kNullHandType and items.cpp's kItemTypeNull. */
TripleInt::TripleInt(int a, int b, int c)
{
    value[0] = a;
    value[1] = b;
    value[2] = c;
}

hand::operator bool() const
{
    return type != RECORD_NONE;
}

/* GameDataReference (size 0x40: values +0x0, sid +0x10, ptr +0x38) and GameDataContainer::runConstructor are FORWARDED:
   the game's own code keeps building, copying and freeing these objects.
   WHAT C++ ADDS AROUND THE BODIES. A constructor builds every member before its body runs and a destructor destroys
   every member after its body, so each forwarder below was checked against the game function it calls:
   - the two constructors first set values = 0,0,0 (the three-int constructor above - TripleInt's default constructor
     is declared but not defined, and would be a new import), sid = an empty string (VS2010's inline empty string: no
     allocation) and ptr = null. The game's constructor (0x9C6C0) and copy constructor (0x9DCA0) then re-initialise
     sid as an empty inline string before assigning it, and write values and ptr, so the empty string they replace
     owned nothing and nothing leaks.
   - the destructor: the game's 0x69580 frees sid's buffer when it has one and leaves sid an EMPTY inline string
     (capacity 15, size 0, first byte 0); the string destructor the compiler runs after the body then has nothing to
     free - there is no double free.
   - operator= and GameDataContainer::runConstructor are ordinary member functions: nothing implicit runs.
   THE ARGUMENTS. The copy constructor and operator= take the other reference BY REFERENCE (a pointer in RDX), as the
   header declares. The constructor takes sid BY REFERENCE (RDX) and _values BY VALUE: 12 bytes, which x64 passes as a
   pointer to a copy the CALLER made (R8) - the game's function reads the three ints through R8. Its function-pointer
   type takes that pointer (&_values - our own by-value parameter IS the caller's copy) rather than TripleInt by value:
   by value would make the compiler copy it with TripleInt's copy constructor, which game/TripleInt.h declares and
   nothing here defines, so the link would fail.
   AN UNFILLED ROW (the file comment): a row is 0 only when no table loaded, and then "a refusal installs nothing but
   the title-screen hook", so none of these is reached. If one were: the two constructors leave the empty reference
   built above (values 0,0,0, empty sid, ptr null - the engine's own "no reference" state for ptr); operator= leaves
   *this unchanged; the destructor leaves sid to the compiler's string destructor, which frees it; and
   GameDataContainer::runConstructor answers null and builds NOTHING - the caller's raw bytes stay raw (store.cpp's two
   callers ignore the answer), which no neutral value can make safe: only the row being filled can. */
static unsigned long long kGcGdrCtor = 0; static coop::AddrReg kGcGdrCtor_reg("GameDataReference_ctor", &kGcGdrCtor);   /* Steam_1.0.65 0x09C6C0 */
static unsigned long long kGcGdrCopy = 0; static coop::AddrReg kGcGdrCopy_reg("GameDataReference_copy", &kGcGdrCopy);   /* Steam_1.0.65 0x09DCA0 */
static unsigned long long kGcGdrAssign = 0; static coop::AddrReg kGcGdrAssign_reg("GameDataReference_assign", &kGcGdrAssign);   /* Steam_1.0.65 0x0A2370 */
static unsigned long long kGcGdrDtor = 0; static coop::AddrReg kGcGdrDtor_reg("GameDataReference_dtor", &kGcGdrDtor);   /* Steam_1.0.65 0x069580 */
static unsigned long long kGcGdcCtor = 0; static coop::AddrReg kGcGdcCtor_reg("GameDataContainer_ctor", &kGcGdcCtor);   /* Steam_1.0.65 0x3883E0 */

GameDataReference::GameDataReference(const std::string& _sid, TripleInt _values)
    : values(0, 0, 0), ptr(0)
{
    typedef GameDataReference* (*Fn)(void* self, const std::string* sid, const TripleInt* values);
    const unsigned long long a = coop::AddrAbs(kGcGdrCtor);
    if (a != 0) ((Fn)a)(this, &_sid, &_values);
}

GameDataReference::GameDataReference(const GameDataReference& that)
    : values(0, 0, 0), ptr(0)
{
    typedef GameDataReference* (*Fn)(void* self, const GameDataReference* that);
    const unsigned long long a = coop::AddrAbs(kGcGdrCopy);
    if (a != 0) ((Fn)a)(this, &that);
}

GameDataReference& GameDataReference::operator=(const GameDataReference& that)
{
    typedef GameDataReference* (*Fn)(void* self, const GameDataReference* that);
    const unsigned long long a = coop::AddrAbs(kGcGdrAssign);
    return a != 0 ? *((Fn)a)(this, &that) : *this;
}

GameDataReference::~GameDataReference()
{
    typedef void (*Fn)(void* self);
    const unsigned long long a = coop::AddrAbs(kGcGdrDtor);
    if (a != 0) ((Fn)a)(this);
}

/* The caller's raw 0x180 bytes in RCX; the game builds the container there and returns this. */
GameDataContainer* GameDataContainer::runConstructor()
{
    typedef GameDataContainer* (*Fn)(void* self);
    const unsigned long long a = coop::AddrAbs(kGcGdcCtor);
    return a != 0 ? ((Fn)a)(this) : 0;
}
