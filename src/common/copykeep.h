/* src/common/copykeep.h - copy1 (.modding/investigations/copies-health-and-animal-items.md COPY1 and COPY2): the two
 * pure decisions of the copy-side fixes. No engine memory, no Windows; the offline suite calls the same functions.
 * C++03 (VS2010 v100).
 *
 * COPY1 (stats.cpp StatsRecalcPod): MedicalSystem::updateStats 0x644840, which a copy's stats apply calls after
 * _recalculateStats 0x8857E0, sets MedicalSystem::blood (+0x70) to max blood (0x643610's value) - Read, decomp
 * (chaos-levers-and-brains.md). A wounded copy therefore showed full blood until the next STATE. The copy's blood is
 * read straight before that call and written back straight after: BloodRestoreWanted says whether to write it back,
 * BloodPushDelta how far the push had moved it.
 *
 * COPY2 (clothing.cpp ApplyGarmentsImpl): when the whole-inventory Inventory::addItem refuses a kit item (an animal's
 * `main` section is switched off, so "wherever it fits" finds nowhere), the item goes into the section the owner had it
 * in, through that section's own InventorySection::addItem; if that also refuses, the created item is destroyed (it is
 * in no inventory, so nothing else would ever free it). KitPlacement names the outcome.
 */
#pragma once

#include <cstring>

namespace coopkeep {

inline bool KeepFloatOk(float f)
{
    unsigned int b = 0;
    std::memcpy(&b, &f, 4);
    return (b & 0x7F800000u) != 0x7F800000u;   /* not NaN, not infinite (by the bits: /fp:fast) */
}

/* 1 = write `before` back after the medical push. Only for a COPY (never a character this game drives), only when the
   read succeeded, only for a finite value (blood may be negative: a character bleeding out reads below 0). */
inline int BloodRestoreWanted(int isCopy, int readOk, float before)
{
    return (isCopy != 0 && readOk != 0 && KeepFloatOk(before)) ? 1 : 0;
}

/* |afterPush - before|: how far the push moved blood; 0 when either value is not finite. */
inline float BloodPushDelta(float before, float afterPush)
{
    if (!KeepFloatOk(before) || !KeepFloatOk(afterPush)) return 0.0f;
    const float d = afterPush - before;
    return d < 0.0f ? -d : d;
}

enum KitPlace
{
    kKitPlacedByEngine  = 1,   /* the whole-inventory add took it (unchanged path) */
    kKitPlacedBySection = 2,   /* the owner's named section's own add took it */
    kKitDestroy         = 3,   /* both refused (or the named section does not exist here): destroy the created item */
    kKitLeaveFaulted    = 4    /* the section add FAULTED: the add is not atomic, the item's state is unknown - never destroyed */
};

/* engineAdd: 1 the whole-inventory add said yes. sectionAdd: 1 the named section took it, 0 it refused / the section was
   not found / not asked, -1 the section add faulted. */
inline int KitPlacement(int engineAdd, int sectionAdd)
{
    if (engineAdd != 0) return kKitPlacedByEngine;
    if (sectionAdd == 1) return kKitPlacedBySection;
    if (sectionAdd < 0) return kKitLeaveFaulted;
    return kKitDestroy;
}

/* copy2 (animal-copy-items.md A2/A4): InventorySection::addItem 0x74BD70 refuses a section whose `enabled` byte (+0xD0)
   is 0, and a living animal's `main` is off. The owner's section is switched ON for the one kit add only when it was
   OFF, and the value READ is what is put back afterwards - a section that was ON (a dead or knocked-out copy's `main`)
   is never switched, so it is never forced off. */
inline int KitSwitchOnForAdd(int wasEnabled) { return wasEnabled == 0 ? 1 : 0; }
inline int KitFlagToRestore(int wasEnabled) { return wasEnabled != 0 ? 1 : 0; }

/* copy2 (review copy1 MINOR): the guarded destroy's answer for a created item both adds refused -> which counter.
   rc: 1 the engine destroyed it, 0 the engine refused (GameWorld::destroy returned false), -1 the call faulted. */
enum KitDestroyCount
{
    kKitCountDestroyed      = 1,
    kKitCountDestroyRefused = 2,
    kKitCountDestroyFault   = 3
};
inline int KitDestroyCounter(int rc)
{
    if (rc == 1) return kKitCountDestroyed;
    if (rc < 0) return kKitCountDestroyFault;
    return kKitCountDestroyRefused;
}

/* copy3 (H-copy2; T390 probe P088; decomp_6296a0.txt:111-216, decomp_74a900.txt:13-19, decomp_74b0f0.txt:13-27): an
   animal's `main` is created with no very-limited-slot list, filled while it has none (fresh spawn: the death items
   through Character::giveItem 0x5CA970 -> tryAddItem -> InventorySection::addItem 0x74BD70; a save: Inventory::loadFrom
   0x74C260 -> 0x74B5A0, which places each item with the section's _addItem, vtable +0x18 = 0x748BE0, at a cell
   itemInArea 0x745BB0 finds free), and only THEN given addVeryLimitedSlot(main, 0) 0x74A900 - a list holding
   one null GameData, so the slot filter 0x74B0F0 refuses every real item from then on. The engine never adds to `main`
   with that filter in place; a copy's `main` already has it, so the section add 0x74BD70 refuses at its line 123. An
   ANIMAL copy therefore places the item as the engine's fill did: the first cell whose footprint no item overlaps
   (0x74BD70 lines 104-105 and 116-153: y outer, x inner, both from 0, footprint inside the grid, 0x745BB0 as the test;
   the filter is not consulted because at the engine's fill it did not exist yet), then the section's own _addItem
   (vtable +0x18). Any other copy keeps copy2's section add. */
enum KitSectionRouteKind
{
    kKitRouteSectionAdd   = 1,   /* InventorySection::addItem 0x74BD70 (copy2) */
    kKitRouteAnimalEngine = 2    /* free cell by 0x745BB0, then the section's _addItem vtable +0x18 (the engine's animal fill) */
};
inline int KitSectionRoute(int isAnimal) { return isAnimal != 0 ? kKitRouteAnimalEngine : kKitRouteSectionAdd; }

/* taken(ctx, x, y): non-zero when an item already in the section overlaps an item's footprint placed at x,y (the plugin
   passes the engine's 0x745BB0, which answers "free" for a footprint outside the grid - so the bounds are kept here).
   1 = found (*x,*y set), 0 = none (or bad sizes). */
typedef int (*KitCellTaken)(void* ctx, int x, int y);
inline int KitFirstFreeCell(int sw, int sh, int iw, int ih, KitCellTaken taken, void* ctx, int* x, int* y)
{
    *x = -1;
    *y = -1;
    if (taken == 0 || sw < 1 || sh < 1 || sw > 256 || sh > 256 || iw < 1 || ih < 1) return 0;
    if (iw > sw || ih > sh) return 0;                                  /* 0x74BD70 lines 104-105 */
    for (int cy = 0; cy <= sh - ih; ++cy)                              /* lines 116, 152-153 */
        for (int cx = 0; cx <= sw - iw; ++cx)                          /* lines 119, 149-150 */
            if (taken(ctx, cx, cy) == 0) { *x = cx; *y = cy; return 1; }   /* line 121 */
    return 0;
}

/* _addItem returns nothing, so the section's item count read before and after is the answer: exactly one more = placed
   (1); anything else = unknown (-1) - the call was made, so the item is never destroyed (KitPlacement -> kKitLeaveFaulted). */
inline int KitEngineRouteResult(long long countBefore, long long countAfter) { return countAfter == countBefore + 1 ? 1 : -1; }

} // namespace coopkeep
