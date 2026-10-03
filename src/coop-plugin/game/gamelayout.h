// gamelayout.h - THE GAME'S MEMORY LAYOUT AS PLAIN NUMBERS.
//
// One named constant per fact: a class size, a field's byte offset from the start of the object, or a
// virtual-function table slot's byte offset. Nothing in this file names a game type.
//
// HOW THE NUMBERS ARE HELD: game/layout_asserts.inl static_asserts every size and offset below against our
// declarations (game/*.h); build.bat step 1c compiles it (game/decl_check.cpp). A declaration that drifts from
// its number stops the build before the DLL step. The vtable slot numbers cannot be checked by a compiler;
// tools/check_vtable_slots.py checks them offline against kenshi_x64.exe and reads them from this file.
//
// Keep this file pure ASCII and free of any #include.
#pragma once

namespace gamelayout {

// ---- lektor<T>: the game's growable array. Same layout for every T (the element type only changes
// what `stuff` points at).
static const unsigned lektor_size           = 0x18; // F351: 8-byte allocator word, count, maxSize, stuff
static const unsigned lektor_count          = 0x08; // F351
static const unsigned lektor_maxSize        = 0x0C; // F351
static const unsigned lektor_stuff          = 0x10; // F351

// ---- GameHashMap / GameHashSet (boost unordered containers over Ogre's allocator)
static const unsigned GameHashMap_size = 0x40; // member strides (boolFields 0xF8 -> stringFields 0x138)
static const unsigned GameHashSet_size = 0x40; // (mainList 0x110 -> name 0x150)

// ---- library types the game's classes embed (third-party headers; a sanity floor)
static const unsigned std_string_size       = 0x28; // (name 0x28 -> type 0x50)
static const unsigned Ogre_Vector3_size     = 0x0C; // (pos 0x20 -> rot 0x2C)
static const unsigned Ogre_Quaternion_size  = 0x10; // ObjectInstance (rot 0xC -> templateRef 0x20)

// ---- enums: every enum is 4 bytes; the value of the enum's LAST (largest) enumerator is kept so our
// declaration spans the same value range as the game's.
static const unsigned enum_size             = 4;    // VS2010 enums are int-sized
static const unsigned itemType_max          = 114;  // RECORD_TYPE_LAST
static const unsigned TaskType_max          = 290;  // TASK_BREAK_GATE_ORDER
static const unsigned swordStateEnum_max    = 11;   // SWORD_APPROACH
static const unsigned SquadRole_max   = 4;    // ROLE_SLAVE
static const unsigned SpeedOrder_max         = 4;    // SPEED_UNCHANGED
static const unsigned PoseState_max        = 4;    // POSE_KNOCKED_OUT
// the other enumerators our code names (each value asserted in game/layout_asserts.inl)
static const unsigned itemType_RECORD_CHARACTER    = 1;
static const unsigned itemType_FACTION      = 10;
static const unsigned TaskType_TASK_MELEE_FOCUSED = 5;
static const unsigned TaskType_TASK_DRAW_WEAPON = 6;
static const unsigned sword_SWORD_SWING     = 0;
static const unsigned sword_SWORD_GUARD           = 1;
static const unsigned sword_SWORD_REACT_GUARD  = 2;
static const unsigned sword_SWORD_STARTING   = 3;
static const unsigned sword_SWORD_STAGGER         = 8;
static const unsigned sword_SWORD_APPROACH_START = 10;
// the enumerators spawn.cpp and gamecalls.cpp name (asserted)
static const unsigned itemType_RECORD_NONE    = 11;
static const unsigned itemType_RECORD_ANIMAL = 76;
static const unsigned PoseState_POSE_STANDING  = 0;

// ---- TripleInt: three ints
static const unsigned TripleInt_size        = 0x0C;
static const unsigned TripleInt_value       = 0x00;

// ---- hand: the game's object handle. Polymorphic (virtual ==/!= operators), so +0 is the vtable pointer.
static const unsigned hand_size             = 0x20; // (serial 0x18 + 4, rounded to 8)
static const unsigned hand_type             = 0x08;
static const unsigned hand_container        = 0x0C;
static const unsigned hand_containerStamp  = 0x10;
static const unsigned hand_index            = 0x14;
static const unsigned hand_serial           = 0x18;
// hand's vtable is the address table's HandVt row (stage 7/9 - never an RVA here); tools/check_vtable_slots.py checks it by RTTI.
static const unsigned vt_hand_opEqualHand   = 0x08; // F360 names this slot

// ---- GameDataReference: one entry of a GameData reference list
static const unsigned GameDataReference_size   = 0x40;
static const unsigned GameDataReference_values = 0x00; // (TripleInt)
static const unsigned GameDataReference_sid    = 0x10; // (std::string)
static const unsigned GameDataReference_ptr    = 0x38; // (GameData*)

// ---- GameData: one record of the game's data (FCS) database. Polymorphic (virtual destructor).
static const unsigned GameData_size             = 0x300; // compiled size
static const unsigned GameData_aliveStamp         = 0x008;
static const unsigned GameData_ownerContainer  = 0x010;
static const unsigned GameData_isDetached     = 0x018;
static const unsigned GameData_id               = 0x01C;
static const unsigned GameData_readOnly         = 0x020;
static const unsigned GameData_name             = 0x028;
static const unsigned GameData_type             = 0x050;
static const unsigned GameData_stringID         = 0x058;
static const unsigned GameData_fromActiveMod = 0x080;
static const unsigned GameData_instances        = 0x088; // (std::map, Ogre allocator)
static const unsigned GameData_idCounter        = 0x0B0;
static const unsigned GameData_activeFlags     = 0x0B8;
static const unsigned GameData_boolFields            = 0x0F8;
static const unsigned GameData_stringFields            = 0x138;
static const unsigned GameData_intFields            = 0x178;
static const unsigned GameData_floatFields            = 0x1B8;
static const unsigned GameData_fileFields        = 0x1F8;
static const unsigned GameData_vectorFields          = 0x238;
static const unsigned GameData_rotationFields         = 0x278;
static const unsigned GameData_referenceLists = 0x2B8;
static const unsigned GameData_createOrder     = 0x2F8;

// ---- GameData::ObjectInstance: one placed instance inside a GameData (the value type of `instances`)
static const unsigned GameDataObjectInstance_size     = 0x68; // compiled size
static const unsigned GameDataObjectInstance_pos      = 0x00;
static const unsigned GameDataObjectInstance_rot      = 0x0C;
static const unsigned GameDataObjectInstance_templateRef    = 0x20;
static const unsigned GameDataObjectInstance_created  = 0x48;
static const unsigned GameDataObjectInstance_modified = 0x4A;
static const unsigned GameDataObjectInstance_stateRecordIds = 0x50; // (lektor<std::string>)

// ---- GameDataContainer / GameDataManager (the manager adds no fields). Polymorphic (virtual destructor).
static const unsigned GameDataContainer_size = 0x180; 
static const unsigned GameDataManager_size   = 0x180;

// ---- SavedObjectState: one object's saved state. NOT polymorphic (+0 is the first field).
static const unsigned SavedObjectState_size       = 0xA8; // compiled size
static const unsigned SavedObjectState_baseRecord   = 0x00;
static const unsigned SavedObjectState_sourceRecords = 0x08;
static const unsigned SavedObjectState_firstTime  = 0x10;
static const unsigned SavedObjectState_instance   = 0x18;
static const unsigned SavedObjectState_pos        = 0x20;
static const unsigned SavedObjectState_rot        = 0x2C;
static const unsigned SavedObjectState_instanceID = 0x40; // (std::string)
static const unsigned SavedObjectState_states     = 0x68; // (GameHashMap<itemType, GameData*>)

// ==== groups 2 and 3.
// "base_D_B" = byte offset of base B inside D (the pointer adjustment of static_cast<B*>(D*)), asserted as
// offsetof(D, m) - offsetof(B, m) over a field m of B: VS2010 never treats static_cast<B*>((D*)k) as a constant
// (C2057, tried 2026-09-28 in four spellings). So two kinds of base CANNOT be asserted by the compiler and have no
// constant here: Ogre::GeneralAllocatedObject (GAO - Ogre's EMPTY allocator base: no field at all) and
// NxUserControllerHitReport (only a vtable pointer). Their positions (Character +0xC0, Platoon +0x78,
// InventoryItemBase +0xC0, AbstractMovementBase +0x8 / NxUserControllerHitReport +0; RootObjectContainer: see
// game/RootObject.h); they are written in the headers' comments. An empty base holds no data and only static
// operator new/delete, so its position moves no field - every field offset and size below is asserted.
static const unsigned object_align8         = 8;    // the non-polymorphic group 2/3 classes are 8-byte aligned

static const unsigned BodySide_max         = 3;    // BOTH_SIDES
static const unsigned ItemFunction_max      = 17;   // FUNCTION_SEVERED_LIMB
static const unsigned PathPriority_max    = 2;    // PATH_PRIORITY_HIGH
static const unsigned PathPriority_PATH_PRIORITY_MEDIUM = 1; // mig4 G2 (second enumerator)
static const unsigned SteeringMode_max      = 2;    // STEER_BY_DIRECTION
static const unsigned HealthPartType_max    = 3;    // MedicalSystem::HealthPartStatus::BODY_PART_HEAD

// ---- RootObjectBase / RootObject (polymorphic: +0 is the vtable pointer)
static const unsigned RootObjectBase_size        = 0x78;
static const unsigned RootObjectBase_liveCheckKey    = 0x08;
static const unsigned RootObjectBase_owner       = 0x10;
static const unsigned RootObjectBase_shownName = 0x18;
static const unsigned RootObjectBase_data        = 0x40;
static const unsigned RootObjectBase_pos         = 0x48;
static const unsigned RootObjectBase_handle      = 0x58;
static const unsigned RootObject_size            = 0xC0;
static const unsigned base_RootObject_RootObjectBase = 0x0;

// ---- Character : RootObject, GAO
static const unsigned Character_size             = 0x6D8;
static const unsigned base_Character_RootObjectBase = 0x0; // through RootObject
static const unsigned Character_inventory        = 0x2E8;
static const unsigned Character_animation        = 0x448;
static const unsigned Character_stats            = 0x450;
static const unsigned Character_medical          = 0x458; // embedded MedicalSystem
static const unsigned Character_movement         = 0x640;
static const unsigned Character_body             = 0x648;
static const unsigned Character_ai               = 0x650;
static const unsigned Character_platoon          = 0x658;

// ---- movement: NxUserControllerHitReport (vtable pointer only); AbstractMovementBase : it, GAO; CharMovement
static const unsigned NxUserControllerHitReport_size = 0x8;
static const unsigned AbstractMovementBase_size  = 0x108;
static const unsigned AbstractMovementBase_speedCap = 0xB4;   // T-189 runboost
static const unsigned AbstractMovementBase_speedNow = 0xB8;
static const unsigned AbstractMovementBase_desiredSpeed = 0xBC;
static const unsigned CharMovement_size          = 0x3B8;
static const unsigned base_CharMovement_AbstractMovementBase = 0x0;

// ---- body, animation, AI, combat (single base each; see game/CharBody.h for the GAO note)
static const unsigned CharBody_size              = 0x78;
static const unsigned AnimationClassBase_size    = 0xE8;
static const unsigned AnimationClass_size        = 0x300;
static const unsigned AnimationClass_movementLimits = 0xF0;   // T-189 runboost
static const unsigned AnimationLimits_speedCap = 0xAC;   // T-189 (AnimationClass +0x19C, applySpeedCap's field)
static const unsigned CharStats_runSpeed        = 0x17C;   // T-189 (the run speed)
static const unsigned OrdersReceiver_size        = 0x210;
static const unsigned AITaskSytem_size           = 0x3B8;
static const unsigned CombatClass_size           = 0x2B8;

// ---- MedicalSystem (polymorphic) and MedicalSystem::HealthPartStatus (not polymorphic)
static const unsigned MedicalSystem_size             = 0x1B0;
static const unsigned MedicalSystem_hunger           = 0x60;
static const unsigned MedicalSystem_fed              = 0x64;
static const unsigned MedicalSystem_blood            = 0x70;
static const unsigned MedicalSystem_bleedRate = 0x78;
static const unsigned MedicalSystem_knockoutClock    = 0xA0;
static const unsigned MedicalSystem_nextKnockoutAt       = 0x158;
static const unsigned MedicalSystem_crippled         = 0x160;
static const unsigned MedicalSystem_unconcious       = 0x161;
static const unsigned MedicalSystem_lowHealthKnockout          = 0x162;
static const unsigned MedicalSystem_bloodLossShock  = 0x163;
static const unsigned MedicalSystem_dead             = 0x164;
static const unsigned MedicalSystem_rightArmUsable       = 0x165;
static const unsigned MedicalSystem_leftArmUsable        = 0x166;
static const unsigned HealthPartStatus_size          = 0x68;
static const unsigned HealthPartStatus_data          = 0x00;
static const unsigned HealthPartStatus_partKind       = 0x08;
static const unsigned HealthPartStatus_medical       = 0x10;
static const unsigned HealthPartStatus_me            = 0x18;
static const unsigned HealthPartStatus_side          = 0x20;
static const unsigned HealthPartStatus_prosthetic     = 0x28;
static const unsigned HealthPartStatus_regenerates   = 0x30;
static const unsigned HealthPartStatus_collapses     = 0x31;
static const unsigned HealthPartStatus_fatal         = 0x32;
static const unsigned HealthPartStatus_knockoutScale        = 0x34;
static const unsigned HealthPartStatus_hitWeight     = 0x38;
static const unsigned HealthPartStatus_hitWeightScale = 0x3C;
static const unsigned HealthPartStatus_flesh         = 0x40;
static const unsigned HealthPartStatus_stunDamage     = 0x44;
static const unsigned HealthPartStatus_bandageLevel     = 0x48;
static const unsigned HealthPartStatus_splintLevel   = 0x4C;
static const unsigned HealthPartStatus_limbWear    = 0x50;
static const unsigned HealthPartStatus_maxHealthBase    = 0x54;
static const unsigned HealthPartStatus_age           = 0x58;
static const unsigned HealthPartStatus_healthScale        = 0x5C;
static const unsigned HealthPartStatus_fleshHealthFraction = 0x60;

// ---- GameWorld (polymorphic, GAO)
static const unsigned GameWorld_size             = 0x8C8;
static const unsigned GameWorld_gamedata         = 0x20;  // embedded GameDataManager
static const unsigned GameWorld_objectFactory       = 0x4A0;
static const unsigned GameWorld_factionDirectory       = 0x4A8;
static const unsigned GameWorld_player           = 0x580;

// ---- factions
static const unsigned Faction_size               = 0x288;
static const unsigned Faction_relations          = 0x78;
static const unsigned FactionDirectory_size        = 0x50;  // NOT polymorphic
static const unsigned FactionRelations_size      = 0x68;

// ---- platoons and containers
static const unsigned Platoon_size               = 0x200;
static const unsigned base_Platoon_RootObjectBase = 0x0;
static const unsigned DataObjectContainer_size   = 0x48;
static const unsigned DataObjectContainer_isSaved = 0x08;
static const unsigned DataObjectContainer_objectRecords = 0x10;
static const unsigned DataObjectContainer_recordsFile = 0x18; // std::string: see game/RootObject.h for why it matters
static const unsigned DataObjectContainer_containerKind = 0x40;
static const unsigned DataObjectGroupType_max    = 5;    // DataObjectContainer::KIND_BUILDING_INTERIOR
static const unsigned RootObjectContainer_size   = 0x68;
static const unsigned base_RootObjectContainer_DataObjectContainer = 0x0;
static const unsigned RootObjectContainer_things = 0x50;
static const unsigned ActivePlatoon_size         = 0xF8;
static const unsigned base_ActivePlatoon_RootObjectContainer = 0x0;
static const unsigned RootObjectFactory_size     = 0x50;  // NOT polymorphic

// ---- inventory and items
static const unsigned InventorySection_size      = 0xD8;
static const unsigned InventorySection_name      = 0x08;
static const unsigned InventorySection_items     = 0x40;  // Ogre::vector<InventorySection::SectionItem>::type
static const unsigned InventorySection_notifyTarget = 0xC0;
static const unsigned SectionItem_size           = 0x10;
static const unsigned SectionItem_item           = 0x00;
static const unsigned SectionItem_x              = 0x08;
static const unsigned SectionItem_y              = 0x0A;
static const unsigned SectionItem_w              = 0x0C;
static const unsigned SectionItem_h              = 0x0E;
static const unsigned Inventory_size             = 0x98;
static const unsigned Inventory_notifyTarget   = 0x80;
static const unsigned Inventory_owner            = 0x88;
static const unsigned InventoryItemBase_size     = 0x190;
static const unsigned base_InventoryItemBase_RootObjectBase = 0x0; // through RootObject
static const unsigned InventoryItemBase_quality  = 0x11C;
static const unsigned InventoryItemBase_functionKind = 0x124;
static const unsigned InventoryItemBase_quantity = 0x12C;
static const unsigned Item_size                  = 0x1E8;
static const unsigned base_Item_InventoryItemBase = 0x0;

// ---- save manager, utility, options, appearance
static const unsigned SaveManager_size           = 0x120; // NOT polymorphic
static const unsigned UtilityT_size              = 0x60;  // NOT polymorphic
static const unsigned GameOptions_size         = 0xE8;  // NOT polymorphic
static const unsigned GameOptions_viewDistance = 0x18;
static const unsigned AppearanceBase_size        = 0x190;
static const unsigned AppearanceBase_attachments = 0x10;

// ---- virtual-function table slots (byte offset into the class's vtable). NOT compiler-checked: the inline
// wrappers in game/RootObjectBase.h, Character.h and Inventory.h (part F) call through these;
// tools/check_vtable_slots.py proves each against kenshi_x64.exe via RTTI.
static const unsigned vt_RootObjectBase_getRecord  = 0x18;  // planning read
static const unsigned vt_RootObjectBase_isUnconscious = 0x30;  // planning read
static const unsigned vt_RootObjectBase_getPosition  = 0x40;  // planning read
static const unsigned vt_RootObjectBase_getOwnerFaction   = 0x58;  // planning read
static const unsigned vt_Character_faceToward    = 0x3B0; // planning read
static const unsigned vt_InventorySection_addItem    = 0x10;  // planning read
static const unsigned vt_InventorySection__addItem   = 0x18;  // planning read
static const unsigned vt_Inventory_takeItemOut = 0x28; // planning read
static const unsigned vt_Inventory_redrawPanel        = 0x58;  // planning read
static const unsigned vt_Inventory_addItem           = 0x10;  // mig4 G2 (Inventory::addItem, 4 args)

} // namespace gamelayout
