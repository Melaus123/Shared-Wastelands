// clothing.h - replicate what a character is WEARING.
//
// The last known visible divergence after H010b closed the character itself (F165) and F152
// closed its behaviour (F179). T050's third complaint was the clothes, and T056 measured it:
// **item-set parity 1/4**, concentrated in the `legs` slot, with `Rag Loincloth` on one
// instance and `Halfpants (ragged)` on the other for three of four characters. The probe
// prints the same garments the user named by eye.
//
// WHY THIS IS A DIFFERENT SHAPE OF PROBLEM FROM APPEARANCE. H010b worked because the
// appearance is DERIVED from a record, so replicating the record made both engines compute
// the same person. Clothing has no such record: the items ARE the state. There is nothing
// to copy that the engine will then re-derive from, so the items themselves must be rebuilt
// on the peer. That is not a band-aid by the register's definition - nothing here is
// corrected at intervals, and nothing the engine derives in between runs on a stale value.
//
// THE CONTRACT, read out of the binary before any of this was written (F183, lesson 3):
//   * `RootObjectFactory::createItem(GameData* itemState)` REFUSES null and returns 0.
//   * It reads exactly four values off the state record: `stringFields["base data sid"]`,
//     `stringFields["company sid"]`, `stringFields["material sid"]`, `intFields["level"]`.
//   * It builds the `hand` itself and forwards to the six-argument overload, which
//     null-checks every optional pointer and substitutes defaults.
//   * The item-state record type is **0x2A**, taken from `Item::serialiseInInventory`.
//
// SCOPE — WIDENED BY MEASUREMENT (F185). The first version sent only the BASE SID and said so
// explicitly. T059 measured the cost of that choice: item parity went 1/4 -> 3/4, and the ONE
// failure was a `Staff` — the run's only WEAPON.
//
// The decompile already in hand explains it exactly. The six-argument `createItem` opens with
// `if (baseRecord == 0 || baseRecord->type == 2 || baseRecord->type == 0x31) return 0;` and
// **itemType 2 is WEAPON**. The state-based overload handles that case by calling it with the
// arguments SWAPPED — company into the base slot, base into the weapon-mesh slot. So for a
// weapon the engine reads `company sid` into the very pointer it null-checks first. Sending
// only the base sid guaranteed a null there, and it returned 0.
//
// All four keys now travel: base sid, company sid, material sid, level. They are read from the
// engine's OWN serialiser rather than from guessed member offsets.

#pragma once

#include <string>
#include <vector>

#include "../common/garmentdetail.h"   /* quality1: per-item quality / charges / quantity / colour */
#include "../common/bagwire.h"   /* bag23 part 1: the kit's packs' contents (KitBag) */
#include "../common/kitcell.h"   /* kit2 (owner 102): each kit item's cell (KitCell) */

class Character;

namespace coop {

// What one character is wearing. Parallel arrays: `sections[i]` is where item `i` was found
// on the authority, `baseSids[i]` is which garment it is.
struct GarmentSet
{
    std::vector<std::string> sections;
    std::vector<std::string> baseSids;
    std::vector<std::string> companySids;   // REQUIRED for weapons (F185)
    std::vector<std::string> materialSids;
    std::vector<int>         levels;
    // quality1 (F907): one entry per item when the sender captured them (empty from an older sender): quality, charges,
    // stack size and colour, written onto the rebuilt item as the live item moves already do.
    std::vector<coopgarment::GarmentDetail> details;
    // bag23 part 1 (inv3 phase 2, protocol 81): what is inside each top-level pack of the kit - kit index + rows, rising by
    // index, only packs holding something (empty from an older sender: the copy's packs then stay empty as before).
    std::vector<coopbag::KitBag> bags;
    // kit2 (owner 102, protocol 83): each item's cell in its section on the owner, one per item (empty from an older sender:
    // the copy then places as before). The copy puts each item at exactly that section and cell.
    std::vector<coopkitcell::KitCell> cells;

    size_t Count() const { return baseSids.size(); }
};

// Read a character's worn items. Walks the inventory's SECTION map - not `_allItems`, which
// F173 measured reading 0 on characters that were visibly dressed.
bool CaptureGarments(::Character* c, GarmentSet* out);

// Clear the peer's own rolled clothing and rebuild the authority's. Returns false if the
// inventory could not be reached; individual items that fail to resolve are counted and
// reported rather than silently skipped.
bool ApplyGarments(::Character* c, const GarmentSet& set);

// deadlook1 fold (a DEAD copy): the same apply, but ONLY the worn sections - shirt, legs, armour, head, boots - are
// emptied and rebuilt from `set`; every other item (backpack, weapons, main, belt, ...) is left in place and every
// garment of `set` in another section is skipped. No clearAll.
bool ApplyWornGarments(::Character* c, const GarmentSet& set);

// deadlook1 fold 3: is `name` one of the worn sections ApplyWornGarments rebuilds (case-insensitive)?
bool ClothingIsWornSection(const std::string& name);

// One-line summary for the log.
std::string GarmentSummary(const GarmentSet& set);

// Wire format. Self-describing lengths so a truncated payload is REFUSED, not misparsed.
void SerialiseGarments(const GarmentSet& set, std::vector<char>* out);
bool DeserialiseGarments(const std::vector<char>& b, size_t at, GarmentSet* out);

// Counters for the report line. Every one of these is printed, because a zero that cannot be
// interrogated is not a measurement (F171).
void ReportClothingCounters();

} // namespace coop

namespace cooplimb { struct LimbsWire; }   /* LIMBS: src/common/limbwire.h */

namespace coop {

// LIMBS (parity P38 / P39). MAIN THREAD. The four limbs of a character this game drives - state, and the fitted robotic
// limb's sid and quality - for its next STATE; 0 = no character or unreadable (not carried). The pointer is to one buffer
// the next call overwrites.
const cooplimb::LimbsWire* LimbsOfOwned(unsigned int uid);

// LIMBS. MAIN THREAD (OnState). Records the owner's latest limb block for its copy here; nothing is changed in the engine
// from the STATE handler. has 0 = not carried: nothing is recorded.
void ApplyOwnerLimbs(unsigned int uid, int has, const cooplimb::LimbsWire& w, unsigned int fromPeer);

// LIMBS. MAIN THREAD, the K2 safe point (combat.cpp detour_tsRagdollUpdates, after the engine's ragdoll pass, AI worker
// paused): each recorded block onto its copy through the engine's own calls, a limb at a time and only where they differ;
// then the limbtest lever's one armed request.
void LimbSafePointDrain();

// LIMBS. MAIN THREAD. The copy `uid` is gone (removed, despawned, retired): its recorded block and per-limb marks go too.
void LimbsForgetUid(unsigned int uid);
// LIMBS. MAIN THREAD, world teardown: every recorded block, per-limb mark and the armed limbtest request go.
void LimbsWorldTeardown();
/* MAIN THREAD, every RestWatchTick: an owned character's limb change sends its STATE at once. */
void LimbOwnWatchTick();

// LIMBS. TEST-ONLY lever, MAIN THREAD (command channel). `cut <ownUid> <limb 0-3>` / `fit <ownUid> <limb 0-3> <sid>` arm one
// request for the K2 safe point; `show <uid>` logs any character's four limbs now. Returns the status line.
std::string LimbTestLever(const std::string& args);

// LIMBS. The amputate gate: a copy's own MedicalSystem::amputate is refused unless the mod's own limb apply (the K2 safe point) is making the call.
void InstallCopyAmputateGate();

} // namespace coop
