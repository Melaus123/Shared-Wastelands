// forward.h - forward declarations of the game's classes.
//
// Every name is declared with the class-key `class`: MSVC puts the class-key into every mangled name that
// mentions the type (V = class, U = struct), so the key must be the same wherever the type is declared and a
// pointer parameter links to the one definition gamecalls.cpp gives. Global namespace, like the game's own
// RTTI names (.?AV<name>@@).
// Group 1 defines some of these (game/GameData.h etc.); groups 2-3 (mig4 part E) define most of the rest.
#pragma once

// group 1 (defined in game/)
class GameData;
class GameDataReference;
class GameDataContainer;
class GameDataManager;
class SavedObjectState;
class TripleInt;
class hand;

// defined by groups 2 and 3 (game/*.h; TitleScreen stays opaque - declared only)
class AbstractMovementBase;
class ActivePlatoon;
class AITaskSytem;
class AnimationClass;
class AnimationClassBase;
class AppearanceBase;
class CharBody;
class Character;
class CharMovement;
class CombatClass;
class DataObjectContainer;
class Faction;
class FactionDirectory;
class FactionRelations;
class GameWorld;
class Inventory;
class InventoryItemBase;
class InventorySection;
class Item;
class MedicalSystem;
class NxUserControllerHitReport;
class GameOptions;
class OrdersReceiver;
class Platoon;
class RootObject;
class RootObjectBase;
class RootObjectContainer;
class RootObjectFactory;
class SaveManager;
class TitleScreen;
class UtilityT;

// named only in signatures or pointer fields (never defined here)
class AI;
class AttachedEntity;
class Building;
class CharacterHuman;
class CharStats;
class FactoryCallbackInterface;
class InstanceID;
class PlayerInterface;
class PlacementTransform;
class RobotLimbItem;
class Serialisable;
class TownBase;

// T-186 (mig5): pointer types of the hook detours in worldgen.cpp and combat.cpp (never defined here)
class CombatClassAI;
class Town;
class ZoneManager;
