// decl_check.cpp - THE LAYOUT CHECK: our declarations against game/gamelayout.h.
//
// Includes ONLY our own headers (game/*.h, plus the Ogre/boost/std headers they pull in) and
// game/gamelayout.h, then runs game/layout_asserts.inl. Passing means our declarations lay the game's
// objects out at exactly the sizes and offsets game/gamelayout.h holds.
// NEVER linked into the DLL.
#include <stddef.h>
#include "forward.h"
#include "lektor.h"
#include "OgreUnordered.h"
#include "TripleInt.h"
#include "hand.h"
#include "Enums.h"
#include "GameData.h"
#include "GameDataManager.h"
#include "GameSaveState.h"
// mig4 E+F: groups 2 and 3
#include "RootObjectBase.h"
#include "RootObject.h"
#include "Character.h"
#include "CharMovement.h"
#include "CharBody.h"
#include "AnimationClass.h"
#include "CharStats.h"   /* T-189 runboost */
#include "AITaskSystem.h"
#include "CombatClass.h"
#include "MedicalSystem.h"
#include "GameWorld.h"
#include "Faction.h"
#include "FactionRelations.h"
#include "Platoon.h"
#include "RootObjectFactory.h"
#include "Inventory.h"
#include "Item.h"
#include "SaveManager.h"
#include "UtilityT.h"
#include "OptionsHolder.h"
#include "Appearance.h"
#include "TitleScreen.h"

#include "gamelayout.h"
#include "layout_asserts.inl"
