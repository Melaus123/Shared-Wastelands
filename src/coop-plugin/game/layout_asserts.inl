// layout_asserts.inl - THE ONE LIST OF LAYOUT ASSERTIONS.
//
// game/decl_check.cpp includes this after OUR headers (game/*.h): every size and offset our declarations
// produce is asserted against its number in game/gamelayout.h. It is not linked into the DLL: build.bat step 1c
// compiles it with /c and throws the object away.
//
// Every name used here must exist, with the same spelling, in our headers.

#define GL_SIZE(T, k)      static_assert(sizeof(T) == gamelayout::k, "size of " #T " != gamelayout::" #k)
#define GL_OFF(T, m, k)    static_assert(offsetof(T, m) == gamelayout::k, "offset of " #T "::" #m " != gamelayout::" #k)
#define GL_ENUMMAX(E, v, k) GL_SIZE(E, enum_size); static_assert((unsigned)(v) == gamelayout::k, "last enumerator " #v " != gamelayout::" #k)
// Where base B sits inside D, measured through a field m of B (VS2010 will not
// evaluate static_cast<B*>(D*) at compile time - see gamelayout.h). GL_ALIGN8 only on non-polymorphic classes:
// VS2010's __alignof rejects an abstract class (C2259), and a polymorphic one is 8-aligned by its vtable pointer.
#define GL_BASE(D, B, m, k) static_assert(offsetof(D, m) - offsetof(B, m) == gamelayout::k, "base " #B " of " #D " != gamelayout::" #k)
#define GL_ALIGN8(T)       static_assert(__alignof(T) == gamelayout::object_align8, "alignment of " #T " != 8")

typedef lektor<int>                            gl_lektor_int;
typedef lektor<GameData*>                      gl_lektor_gamedata;
typedef GameHashMap<int, GameData*>::type gl_umap_int_gamedata;
typedef GameHashSet<GameData*>::type      gl_uset_gamedata;
typedef GameData::ObjectInstance               gl_objinst;
typedef MedicalSystem::HealthPartStatus        gl_hps;
typedef InventorySection::SectionItem          gl_sectionitem;

// lektor
GL_SIZE(gl_lektor_int, lektor_size);
GL_SIZE(gl_lektor_gamedata, lektor_size);
GL_OFF(gl_lektor_int, count, lektor_count);
GL_OFF(gl_lektor_int, maxSize, lektor_maxSize);
GL_OFF(gl_lektor_int, stuff, lektor_stuff);
GL_OFF(gl_lektor_gamedata, stuff, lektor_stuff);

// unordered wrappers
GL_SIZE(gl_umap_int_gamedata, GameHashMap_size);
GL_SIZE(gl_uset_gamedata, GameHashSet_size);

// library types
GL_SIZE(std::string, std_string_size);
GL_SIZE(Ogre::Vector3, Ogre_Vector3_size);
GL_SIZE(Ogre::Quaternion, Ogre_Quaternion_size);

// enums (size + last enumerator)
GL_ENUMMAX(itemType, RECORD_TYPE_LAST, itemType_max);
GL_ENUMMAX(TaskType, TASK_BREAK_GATE_ORDER, TaskType_max);
GL_ENUMMAX(swordStateEnum, SWORD_APPROACH, swordStateEnum_max);
GL_ENUMMAX(SquadRole, ROLE_SLAVE, SquadRole_max);
GL_ENUMMAX(SpeedOrder, SPEED_UNCHANGED, SpeedOrder_max);
GL_ENUMMAX(PoseState, POSE_KNOCKED_OUT, PoseState_max);
// mig4 G1: named enumerators (value)
#define GL_ENUMVAL(v, k) static_assert((unsigned)(v) == gamelayout::k, "enumerator " #v " != gamelayout::" #k)
GL_ENUMVAL(RECORD_CHARACTER, itemType_RECORD_CHARACTER);
GL_ENUMVAL(FACTION, itemType_FACTION);
GL_ENUMVAL(TASK_MELEE_FOCUSED, TaskType_TASK_MELEE_FOCUSED);
GL_ENUMVAL(TASK_DRAW_WEAPON, TaskType_TASK_DRAW_WEAPON);
GL_ENUMVAL(SWORD_SWING, sword_SWORD_SWING);
GL_ENUMVAL(SWORD_GUARD, sword_SWORD_GUARD);
GL_ENUMVAL(SWORD_REACT_GUARD, sword_SWORD_REACT_GUARD);
GL_ENUMVAL(SWORD_STARTING, sword_SWORD_STARTING);
GL_ENUMVAL(SWORD_STAGGER, sword_SWORD_STAGGER);
GL_ENUMVAL(SWORD_APPROACH_START, sword_SWORD_APPROACH_START);
GL_ENUMVAL(RECORD_NONE, itemType_RECORD_NONE);                 // mig4 G3b
GL_ENUMVAL(RECORD_ANIMAL, itemType_RECORD_ANIMAL);   // mig4 G3b
GL_ENUMVAL(POSE_STANDING, PoseState_POSE_STANDING);               // mig4 G3b

// TripleInt
GL_SIZE(TripleInt, TripleInt_size);
GL_OFF(TripleInt, value, TripleInt_value);

// hand
GL_SIZE(hand, hand_size);
GL_OFF(hand, type, hand_type);
GL_OFF(hand, container, hand_container);
GL_OFF(hand, containerStamp, hand_containerStamp);
GL_OFF(hand, index, hand_index);
GL_OFF(hand, serial, hand_serial);

// GameDataReference
GL_SIZE(GameDataReference, GameDataReference_size);
GL_OFF(GameDataReference, values, GameDataReference_values);
GL_OFF(GameDataReference, sid, GameDataReference_sid);
GL_OFF(GameDataReference, ptr, GameDataReference_ptr);

// GameData
GL_SIZE(GameData, GameData_size);
GL_OFF(GameData, aliveStamp, GameData_aliveStamp);
GL_OFF(GameData, ownerContainer, GameData_ownerContainer);
GL_OFF(GameData, isDetached, GameData_isDetached);
GL_OFF(GameData, id, GameData_id);
GL_OFF(GameData, readOnly, GameData_readOnly);
GL_OFF(GameData, name, GameData_name);
GL_OFF(GameData, type, GameData_type);
GL_OFF(GameData, stringID, GameData_stringID);
GL_OFF(GameData, fromActiveMod, GameData_fromActiveMod);
GL_OFF(GameData, instances, GameData_instances);
GL_OFF(GameData, idCounter, GameData_idCounter);
GL_OFF(GameData, activeFlags, GameData_activeFlags);
GL_OFF(GameData, boolFields, GameData_boolFields);
GL_OFF(GameData, stringFields, GameData_stringFields);
GL_OFF(GameData, intFields, GameData_intFields);
GL_OFF(GameData, floatFields, GameData_floatFields);
GL_OFF(GameData, fileFields, GameData_fileFields);
GL_OFF(GameData, vectorFields, GameData_vectorFields);
GL_OFF(GameData, rotationFields, GameData_rotationFields);
GL_OFF(GameData, referenceLists, GameData_referenceLists);
GL_OFF(GameData, createOrder, GameData_createOrder);

// GameData::ObjectInstance
GL_SIZE(gl_objinst, GameDataObjectInstance_size);
GL_OFF(gl_objinst, pos, GameDataObjectInstance_pos);
GL_OFF(gl_objinst, rot, GameDataObjectInstance_rot);
GL_OFF(gl_objinst, templateRef, GameDataObjectInstance_templateRef);
GL_OFF(gl_objinst, created, GameDataObjectInstance_created);
GL_OFF(gl_objinst, modified, GameDataObjectInstance_modified);
GL_OFF(gl_objinst, stateRecordIds, GameDataObjectInstance_stateRecordIds);

// GameDataContainer / GameDataManager
GL_SIZE(GameDataContainer, GameDataContainer_size);
GL_SIZE(GameDataManager, GameDataManager_size);

// SavedObjectState
GL_SIZE(SavedObjectState, SavedObjectState_size);
GL_OFF(SavedObjectState, baseRecord, SavedObjectState_baseRecord);
GL_OFF(SavedObjectState, sourceRecords, SavedObjectState_sourceRecords);
GL_OFF(SavedObjectState, firstTime, SavedObjectState_firstTime);
GL_OFF(SavedObjectState, instance, SavedObjectState_instance);
GL_OFF(SavedObjectState, pos, SavedObjectState_pos);
GL_OFF(SavedObjectState, rot, SavedObjectState_rot);
GL_OFF(SavedObjectState, instanceID, SavedObjectState_instanceID);
GL_OFF(SavedObjectState, states, SavedObjectState_states);

// ==== mig4 parts E+F: groups 2 and 3 =====================================================================
// enums
GL_ENUMMAX(BodySide, BOTH_SIDES, BodySide_max);
GL_ENUMMAX(ItemFunction, FUNCTION_SEVERED_LIMB, ItemFunction_max);
GL_ENUMMAX(PathPriority, PATH_PRIORITY_HIGH, PathPriority_max);
GL_ENUMVAL(PATH_PRIORITY_MEDIUM, PathPriority_PATH_PRIORITY_MEDIUM);   // mig4 G2
GL_ENUMMAX(SteeringMode, STEER_BY_DIRECTION, SteeringMode_max);
GL_ENUMMAX(gl_hps::BodyPartKind, gl_hps::BODY_PART_HEAD, HealthPartType_max);
GL_ENUMMAX(DataObjectContainer::ContainerKind, DataObjectContainer::KIND_BUILDING_INTERIOR, DataObjectGroupType_max);

// RootObjectBase / RootObject
GL_SIZE(RootObjectBase, RootObjectBase_size);
GL_OFF(RootObjectBase, liveCheckKey, RootObjectBase_liveCheckKey);
GL_OFF(RootObjectBase, owner, RootObjectBase_owner);
GL_OFF(RootObjectBase, shownName, RootObjectBase_shownName);
GL_OFF(RootObjectBase, data, RootObjectBase_data);
GL_OFF(RootObjectBase, pos, RootObjectBase_pos);
GL_OFF(RootObjectBase, handle, RootObjectBase_handle);
GL_SIZE(RootObject, RootObject_size);
GL_BASE(RootObject, RootObjectBase, liveCheckKey, base_RootObject_RootObjectBase);

// Character
GL_SIZE(Character, Character_size);
GL_BASE(Character, RootObjectBase, liveCheckKey, base_Character_RootObjectBase);
GL_OFF(Character, inventory, Character_inventory);
GL_OFF(Character, animation, Character_animation);
GL_OFF(Character, stats, Character_stats);
GL_OFF(Character, medical, Character_medical);
GL_OFF(Character, movement, Character_movement);
GL_OFF(Character, body, Character_body);
GL_OFF(Character, ai, Character_ai);
GL_OFF(Character, platoon, Character_platoon);

// movement
GL_SIZE(NxUserControllerHitReport, NxUserControllerHitReport_size);
GL_SIZE(AbstractMovementBase, AbstractMovementBase_size);
GL_OFF(AbstractMovementBase, speedCap, AbstractMovementBase_speedCap);   /* T-189 runboost */
GL_OFF(AbstractMovementBase, speedNow, AbstractMovementBase_speedNow);
GL_OFF(AbstractMovementBase, desiredSpeed, AbstractMovementBase_desiredSpeed);
GL_SIZE(CharMovement, CharMovement_size);
GL_BASE(CharMovement, AbstractMovementBase, speedNow, base_CharMovement_AbstractMovementBase);

// body, animation, AI, combat
GL_SIZE(CharBody, CharBody_size);
GL_SIZE(AnimationClassBase, AnimationClassBase_size);
GL_SIZE(AnimationClass, AnimationClass_size);
GL_OFF(AnimationClass, movementLimits, AnimationClass_movementLimits);   /* T-189 runboost */
GL_OFF(AnimationLimits, speedCap, AnimationLimits_speedCap);   /* T-189 */
GL_OFF(CharStats, runSpeed, CharStats_runSpeed);   /* T-189 */
GL_SIZE(OrdersReceiver, OrdersReceiver_size);
GL_SIZE(AITaskSytem, AITaskSytem_size);
GL_SIZE(CombatClass, CombatClass_size);

// MedicalSystem
GL_SIZE(MedicalSystem, MedicalSystem_size);
GL_OFF(MedicalSystem, hunger, MedicalSystem_hunger);
GL_OFF(MedicalSystem, fed, MedicalSystem_fed);
GL_OFF(MedicalSystem, blood, MedicalSystem_blood);
GL_OFF(MedicalSystem, bleedRate, MedicalSystem_bleedRate);
GL_OFF(MedicalSystem, knockoutClock, MedicalSystem_knockoutClock);
GL_OFF(MedicalSystem, nextKnockoutAt, MedicalSystem_nextKnockoutAt);
GL_OFF(MedicalSystem, crippled, MedicalSystem_crippled);
GL_OFF(MedicalSystem, unconcious, MedicalSystem_unconcious);
GL_OFF(MedicalSystem, lowHealthKnockout, MedicalSystem_lowHealthKnockout);
GL_OFF(MedicalSystem, bloodLossShock, MedicalSystem_bloodLossShock);
GL_OFF(MedicalSystem, dead, MedicalSystem_dead);
GL_OFF(MedicalSystem, rightArmUsable, MedicalSystem_rightArmUsable);
GL_OFF(MedicalSystem, leftArmUsable, MedicalSystem_leftArmUsable);

// MedicalSystem::HealthPartStatus
GL_SIZE(gl_hps, HealthPartStatus_size);
GL_ALIGN8(gl_hps);
GL_OFF(gl_hps, data, HealthPartStatus_data);
GL_OFF(gl_hps, partKind, HealthPartStatus_partKind);
GL_OFF(gl_hps, medical, HealthPartStatus_medical);
GL_OFF(gl_hps, me, HealthPartStatus_me);
GL_OFF(gl_hps, side, HealthPartStatus_side);
GL_OFF(gl_hps, prosthetic, HealthPartStatus_prosthetic);
GL_OFF(gl_hps, regenerates, HealthPartStatus_regenerates);
GL_OFF(gl_hps, collapses, HealthPartStatus_collapses);
GL_OFF(gl_hps, fatal, HealthPartStatus_fatal);
GL_OFF(gl_hps, knockoutScale, HealthPartStatus_knockoutScale);
GL_OFF(gl_hps, hitWeight, HealthPartStatus_hitWeight);
GL_OFF(gl_hps, hitWeightScale, HealthPartStatus_hitWeightScale);
GL_OFF(gl_hps, flesh, HealthPartStatus_flesh);
GL_OFF(gl_hps, stunDamage, HealthPartStatus_stunDamage);
GL_OFF(gl_hps, bandageLevel, HealthPartStatus_bandageLevel);
GL_OFF(gl_hps, splintLevel, HealthPartStatus_splintLevel);
GL_OFF(gl_hps, limbWear, HealthPartStatus_limbWear);
GL_OFF(gl_hps, maxHealthBase, HealthPartStatus_maxHealthBase);
GL_OFF(gl_hps, age, HealthPartStatus_age);
GL_OFF(gl_hps, healthScale, HealthPartStatus_healthScale);
GL_OFF(gl_hps, fleshHealthFraction, HealthPartStatus_fleshHealthFraction);

// GameWorld
GL_SIZE(GameWorld, GameWorld_size);
GL_OFF(GameWorld, gamedata, GameWorld_gamedata);
GL_OFF(GameWorld, objectFactory, GameWorld_objectFactory);
GL_OFF(GameWorld, factionDirectory, GameWorld_factionDirectory);
GL_OFF(GameWorld, player, GameWorld_player);

// factions
GL_SIZE(Faction, Faction_size);
GL_OFF(Faction, relations, Faction_relations);
GL_SIZE(FactionDirectory, FactionDirectory_size);
GL_ALIGN8(FactionDirectory);
GL_SIZE(FactionRelations, FactionRelations_size);

// platoons and containers
GL_SIZE(Platoon, Platoon_size);
GL_BASE(Platoon, RootObjectBase, liveCheckKey, base_Platoon_RootObjectBase);
GL_SIZE(DataObjectContainer, DataObjectContainer_size);
GL_OFF(DataObjectContainer, isSaved, DataObjectContainer_isSaved);
GL_OFF(DataObjectContainer, objectRecords, DataObjectContainer_objectRecords);
GL_OFF(DataObjectContainer, recordsFile, DataObjectContainer_recordsFile);
GL_OFF(DataObjectContainer, containerKind, DataObjectContainer_containerKind);
GL_SIZE(RootObjectContainer, RootObjectContainer_size);
GL_BASE(RootObjectContainer, DataObjectContainer, isSaved, base_RootObjectContainer_DataObjectContainer);
GL_OFF(RootObjectContainer, things, RootObjectContainer_things);
GL_SIZE(ActivePlatoon, ActivePlatoon_size);
GL_BASE(ActivePlatoon, RootObjectContainer, things, base_ActivePlatoon_RootObjectContainer);
GL_SIZE(RootObjectFactory, RootObjectFactory_size);
GL_ALIGN8(RootObjectFactory);

// inventory and items
GL_SIZE(InventorySection, InventorySection_size);
GL_OFF(InventorySection, name, InventorySection_name);
GL_OFF(InventorySection, items, InventorySection_items);
GL_OFF(InventorySection, notifyTarget, InventorySection_notifyTarget);
GL_SIZE(gl_sectionitem, SectionItem_size);
GL_ALIGN8(gl_sectionitem);
GL_OFF(gl_sectionitem, item, SectionItem_item);
GL_OFF(gl_sectionitem, x, SectionItem_x);
GL_OFF(gl_sectionitem, y, SectionItem_y);
GL_OFF(gl_sectionitem, w, SectionItem_w);
GL_OFF(gl_sectionitem, h, SectionItem_h);
GL_SIZE(Inventory, Inventory_size);
GL_OFF(Inventory, notifyTarget, Inventory_notifyTarget);
GL_OFF(Inventory, owner, Inventory_owner);
GL_SIZE(InventoryItemBase, InventoryItemBase_size);
GL_BASE(InventoryItemBase, RootObjectBase, liveCheckKey, base_InventoryItemBase_RootObjectBase);
GL_OFF(InventoryItemBase, quality, InventoryItemBase_quality);
GL_OFF(InventoryItemBase, functionKind, InventoryItemBase_functionKind);
GL_OFF(InventoryItemBase, quantity, InventoryItemBase_quantity);
GL_SIZE(Item, Item_size);
GL_BASE(Item, InventoryItemBase, quantity, base_Item_InventoryItemBase);

// save manager, utility, options, appearance
GL_SIZE(SaveManager, SaveManager_size);
GL_ALIGN8(SaveManager);
GL_SIZE(UtilityT, UtilityT_size);
GL_ALIGN8(UtilityT);
GL_SIZE(GameOptions, GameOptions_size);
GL_ALIGN8(GameOptions);
GL_OFF(GameOptions, viewDistance, GameOptions_viewDistance);
GL_SIZE(AppearanceBase, AppearanceBase_size);
GL_OFF(AppearanceBase, attachments, AppearanceBase_attachments);

#undef GL_SIZE
#undef GL_OFF
#undef GL_ENUMMAX
#undef GL_ENUMVAL
#undef GL_BASE
#undef GL_ALIGN8
