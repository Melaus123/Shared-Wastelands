// Enums.h - the game's enums our code names, PoseState among them. Each enum holds its last (largest)
// enumerator, so the type spans the same value range as the game's; game/layout_asserts.inl asserts that value
// and the 4-byte size. Code that names other enumerators adds them here with their values, and
// game/gamelayout.h keeps each value.
#pragma once

enum itemType        { RECORD_CHARACTER = 1, FACTION = 10, RECORD_NONE = 11, RECORD_ANIMAL = 76, RECORD_TYPE_LAST = 114 };   // G1: CHARACTER, FACTION; G3b: RECORD_NONE, RECORD_ANIMAL
enum TaskType        { TASK_MELEE_FOCUSED = 5, TASK_DRAW_WEAPON = 6, TASK_BREAK_GATE_ORDER = 290 };   // G1: combat.cpp
enum swordStateEnum  { SWORD_SWING = 0, SWORD_GUARD = 1, SWORD_REACT_GUARD = 2, SWORD_STARTING = 3, SWORD_STAGGER = 8,
                       SWORD_APPROACH_START = 10, SWORD_APPROACH = 11 };   // G1: combat.cpp
enum SquadRole { ROLE_SLAVE = 4 };
enum SpeedOrder       { SPEED_UNCHANGED = 4 };
enum PoseState      { POSE_STANDING = 0, POSE_KNOCKED_OUT = 4 };   // G3b: POSE_STANDING
enum BodySide       { BOTH_SIDES = 3 };          // mig4 group 2 (MedicalSystem::HealthPartStatus::side)
enum ItemFunction    { FUNCTION_SEVERED_LIMB = 17 }; // mig4 group 3 (InventoryItemBase::functionKind)
