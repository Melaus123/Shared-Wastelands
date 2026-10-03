// clothing.cpp - replicate worn items. See clothing.h for the contract and why it differs
// in shape from the appearance fix.

#include "clothing.h"
#include "spawn.h"
#include "../common/copykeep.h"   /* copy1: KitPlacement */
#include "items.h"   /* inv6 phase 2: ItemOwnerOf / ItemOwnerApplyTo - the worn kit's stolen marks */
#include "appearance.h"   /* LIMBS: CopyRebuildInFlightAny */
#include "hooks.h"   /* LIMBS: coop::AddHook - the amputate gate */
#include "net/session.h"   /* LIMBS: net::IsUidMine / IsUidMineAnyThread */
#include "medical.h"       /* StatePush: an owned character's limb change goes out at once */
#include <set>
#include <vector>
#include "../common/limbwire.h"   /* LIMBS: the limb block and the copy's plan per limb */
#include <map>   /* LIMBS: the copy limbs whose owner sid made no item here */
#include "store.h"   /* LIMBS: EngineWritesBlocked, StoreIsDeadPod */
#include "policy.h"   /* LIMBS: PolicyVtableRvaPod / PolicyClassNamePod - the made limb item's RTTI class */
#include "combat.h"   /* copy3: ReadAnimalAge01 - the animal test (CharacterAnimal getter offset) */

#include "coop_log.h"
#include <stdint.h>   /* mig3: what <core/Functions.h> also brought in */
#include "game/Character.h"
#include "addresses.h"   /* mig3: coop::GameWorldPtr() / OptionsPtr() - our rows for what `ou` / `options` imported */
#include "game/GameWorld.h"
#include "game/GameData.h"
#include "game/GameDataManager.h"
#include "game/RootObjectFactory.h"
#include "game/Inventory.h"
#include "game/Item.h"
#include "game/hand.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <sstream>
#include <locale>
#include <cstring>
#include <cstdio>   /* quality1: the summary's quality */

namespace coop {

namespace {

const size_t kInventoryOff = 0x2E8;   // Character -> Inventory*
const size_t kSectionsOff  = 0x028;   // Inventory -> unordered_map<string, InventorySection*>
const size_t kPlatoonContainerOff = 0x10;   // ActivePlatoon -> GameDataContainer*

// F183: the item-state record type, taken from Item::serialiseInInventory, which is a
// one-line forwarder calling the virtual at vtable+0x258 with this value.
const int kItemStateType = 0x2A;

// F183: the only key that has to be set. The other three (`company sid`, `material sid`,
// `level`) are read by createItem too, and every one of them is null-checked downstream with
// a default substituted - verified in the decompile, not assumed.
const char* kBaseSidKey     = "base data sid";
const char* kCompanySidKey  = "company sid";
const char* kMaterialSidKey = "material sid";
const char* kLevelKey       = "level";
const char* kColorSidKey    = "color sid";   /* quality1: the colour the live item moves carry too */

// Read a string value out of a GameData's `stringFields`, or "" if absent. `find` rather than
// operator[] so a missing key is not silently INSERTED into the engine's own record.
std::string SData(::GameData* gd, const char* key)
{
    typedef boost::unordered::unordered_map<std::string, std::string,
        boost::hash<std::string>, std::equal_to<std::string>,
        Ogre::STLAllocator<std::pair<std::string const, std::string>,
        Ogre::GeneralAllocPolicy> > M;
    const M& m = gd->stringFields;
    M::const_iterator it = m.find(std::string(key));
    return it == m.end() ? std::string() : it->second;
}

int IData(::GameData* gd, const char* key)
{
    typedef boost::unordered::unordered_map<std::string, int,
        boost::hash<std::string>, std::equal_to<std::string>,
        Ogre::STLAllocator<std::pair<std::string const, int>,
        Ogre::GeneralAllocPolicy> > M;
    const M& m = gd->intFields;
    M::const_iterator it = m.find(std::string(key));
    return it == m.end() ? 0 : it->second;
}

struct Counters
{
    int captured;      // characters whose garments were read
    int itemsCaptured;
    int applied;       // characters rebuilt
    int itemsCreated;
    int itemsUnresolved;   // base sid did not resolve on this instance
    int itemsAddFailed;    // created but the inventory refused it
    int noContainer;       // could not obtain a data container
    int detailsRead, detailsReadFault, detailsWritten, detailsWriteFault, detailsAbsent, detailsBad;   // quality1
    int addPlacedBySection, addFailedDestroyed, addSectionFault;   // copy1 (COPY2): after a whole-inventory refusal
    int addEnabledForAdd, addDestroyRefused, addDestroyFault, addEnabledRestoreFault;   // copy2: section switched on for the add; the guarded destroy's answer
    int addPlacedEngineRoute, addEngineRouteNoCell;   // copy3: animal copies - placed through the engine's own animal route / no free cell
    int kitPlacedExact, kitRelocated, kitLost;   // kit2: at the owner's section and cell / that section's first free cell / no room there
    Counters() : captured(0), itemsCaptured(0), applied(0), itemsCreated(0),
                 itemsUnresolved(0), itemsAddFailed(0), noContainer(0),
                 detailsRead(0), detailsReadFault(0), detailsWritten(0), detailsWriteFault(0), detailsAbsent(0), detailsBad(0),
                 addPlacedBySection(0), addFailedDestroyed(0), addSectionFault(0),
                 addEnabledForAdd(0), addDestroyRefused(0), addDestroyFault(0), addEnabledRestoreFault(0),
                 addPlacedEngineRoute(0), addEngineRouteNoCell(0), kitPlacedExact(0), kitRelocated(0), kitLost(0) {}
};
Counters g_c;
/* bag23 part 1 (inv3 phase 2): the packs' contents on the kit - bagRows[captured,applied,relocated,lost] on the REPORT line */
long long g_bagRowsCaptured = 0, g_bagRowsApplied = 0, g_bagRowsRelocated = 0, g_bagRowsLost = 0;
long long g_bagPacksRefused = 0, g_bagKitBad = 0, g_bagKitEncodeRefused = 0;
/* kit2 (owner 102, protocol 83): the kit's cells block - kitCells[absent,bad,encodeRefused] on the REPORT line */
long long g_kitCellsAbsent = 0, g_kitCellsBad = 0, g_kitCellsEncodeRefused = 0;

bool Ptr(const void* p)
{
    uintptr_t v = (uintptr_t)p;
    if (v < 0x10000 || v >= 0x00007FFFFFFFFFFFull) return false;
    if (v & 0x7) return false;
    __try { volatile uintptr_t probe = *(uintptr_t*)v; (void)probe; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

/* quality1: the same Item fields the live item moves read and write (items.cpp kItemCharges 0x118 / kItemQuality 0x11C /
   kItemQuantity 0x12C). POD helpers: no object with a destructor in a __try function (C2712). */
const size_t kItemCharges  = 0x118;   // chargesLeft (float)
const size_t kItemQuality  = 0x11C;   // quality (float)
const size_t kItemQuantity = 0x12C;   // quantity (int)
const size_t kItemFunction = 0x124;   // functionKind (int)
const size_t kItemUnique   = 0x12A;   // isUnique (bool)
int ReadItemDetailPod(const void* item, float* quality, float* charges, int* quantity, int* function, unsigned char* unique)
{
    __try
    {
        *quality = *(const float*)((const char*)item + kItemQuality);
        *charges = *(const float*)((const char*)item + kItemCharges);
        *quantity = *(const int*)((const char*)item + kItemQuantity);
        *function = *(const int*)((const char*)item + kItemFunction);
        *unique = *(const unsigned char*)((const char*)item + kItemUnique);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int WriteItemDetailPod(void* item, float quality, float charges, int quantity, int function, unsigned char unique, float* readBack)
{
    __try
    {
        *(int*)((char*)item + kItemQuantity) = quantity;
        *(float*)((char*)item + kItemQuality) = quality;
        *(float*)((char*)item + kItemCharges) = charges;
        *(int*)((char*)item + kItemFunction) = function;
        *(unsigned char*)((char*)item + kItemUnique) = unique ? 1 : 0;
        *readBack = *(const float*)((const char*)item + kItemQuality);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

bool Obj(const void* p)
{
    if (!Ptr(p)) return false;
    uintptr_t vtable = *(uintptr_t*)p;
    uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    return vtable > base && vtable < base + 0x4000000;
}

std::string S(long long v)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << v;
    return ss.str();
}

typedef boost::unordered::unordered_map<std::string, InventorySection*,
    boost::hash<std::string>, std::equal_to<std::string>,
    Ogre::STLAllocator<std::pair<std::string const, InventorySection*>,
    Ogre::GeneralAllocPolicy> > SecMap;

Inventory* GetInventory(::Character* c)
{
    if (c == 0 || !Obj(c)) return 0;
    void* inv = *(void**)((char*)c + kInventoryOff);
    return Obj(inv) ? (Inventory*)inv : 0;
}

void PutU32(std::vector<char>* b, unsigned int v)
{
    size_t at = b->size();
    b->resize(at + 4);
    std::memcpy(&(*b)[at], &v, 4);
}

void PutStr(std::vector<char>* b, const std::string& s)
{
    PutU32(b, (unsigned int)s.size());
    b->insert(b->end(), s.begin(), s.end());
}

bool GetU32(const std::vector<char>& b, size_t* at, unsigned int* out)
{
    if (b.size() < *at + 4) return false;
    std::memcpy(out, &b[*at], 4);
    *at += 4;
    return true;
}

const unsigned int kMaxStrLen = 4096;
const unsigned int kMaxItems  = 256;

bool GetStr(const std::vector<char>& b, size_t* at, std::string* out)
{
    unsigned int len = 0;
    if (!GetU32(b, at, &len)) return false;
    if (len > kMaxStrLen || b.size() < *at + len) return false;
    out->assign(len ? &b[*at] : "", len);
    *at += len;
    return true;
}

// A container to create the item-state record in.
//
// F065's caveat still stands and is repeated here rather than quietly inherited: the P007
// spike parked its records in the PLATOON's container, and those are dropped whenever the
// engine next serialises that platoon. That is acceptable for a record we consume
// immediately - createItem reads it and we never look at it again - and it is NOT acceptable
// as a long-term home. A real sync layer must own its own container.
GameDataContainer* GetScratchContainer(::Character* c)
{
    ActivePlatoon* ap = c->getSquad();
    if (!Obj(ap)) return 0;
    GameDataContainer* container = *(GameDataContainer**)((char*)ap + kPlatoonContainerOff);
    return Obj(container) ? container : 0;
}

} // namespace

/* quality1: the item's quality / charges / quantity (live fields) and its colour (from the engine's own item-state record). */
static coopgarment::GarmentDetail CaptureDetail(Item* item, const std::string& colorSid)
{
    coopgarment::GarmentDetail d;
    float q = 0.0f, ch = 0.0f; int n = 1, fn = 0; unsigned char un = 0;
    if (ReadItemDetailPod(item, &q, &ch, &n, &fn, &un))
    {
        ++g_c.detailsRead;
        d.quality = q; d.charges = ch; d.quantity = (n >= 1) ? (unsigned int)n : 1u;
        d.functionKind = fn; d.unique = un ? 1 : 0;
    }
    else ++g_c.detailsReadFault;
    d.colorSid = colorSid;
    coop::ItemOwnerOf((void*)item, &d.owner);   /* inv6 phase 2 (R6): MAIN THREAD (appearance.cpp's settle send) */
    return d;
}

/* bag23 part 1 (inv3 phase 2, D3(b)): the contents of the kit item just pushed, when it is a pack - depth 1, read by items.cpp's own
   pack reader (ItBagRowsOf: the +0x290 back-pointer test, the ItReadFields fields, each row's stolen mark). MAIN THREAD (the
   settle send). A pack holding nothing is not listed. A pack whose contents cannot travel WHOLE (unreadable, over the 256-row
   cap, a pack inside it) is not listed either - counted and logged: that copy's pack is empty, never partly filled. */
static void CaptureBag(Item* item, GarmentSet* out)
{
    if (out->Count() == 0) return;
    coopbag::KitBag kb;
    kb.kitIndex = (unsigned int)(out->Count() - 1);
    int why = 0;
    const int r = coop::ItemBagRowsOf((void*)item, &kb.rows, &why);
    if (r == 0 || (r == 1 && kb.rows.empty())) return;
    if (r < 0)
    {
        ++g_bagPacksRefused;
        ErrorLog("[P022] kit pack " + out->baseSids.back() + " in '" + out->sections.back() + "': its contents cannot ride the kit ("
                 + coop::ItemBagWhyName(why) + ") - the copy's pack will be EMPTY (bagPacksRefused)");
        return;
    }
    g_bagRowsCaptured += (long long)kb.rows.size();
    out->bags.push_back(kb);
}

bool CaptureGarments(::Character* c, GarmentSet* out)
{
    Inventory* inv = GetInventory(c);
    if (inv == 0 || out == 0) return false;

    // F185: the four keys are read from the ENGINE'S OWN serialiser, not from guessed member
    // offsets on Item. `serialiseInInventory` is a one-line forwarder to the virtual that
    // writes an item-state record (type 0x2A), so whatever the engine considers the item's
    // identity is exactly what comes back - including the `company sid` a weapon needs.
    GameDataContainer* container = GetScratchContainer(c);
    if (container == 0) { ++g_c.noContainer; return false; }

    const SecMap& secs = *(const SecMap*)((char*)inv + kSectionsOff);
    if (secs.size() > 64) return false;

    for (SecMap::const_iterator it = secs.begin(); it != secs.end(); ++it)
    {
        InventorySection* sec = it->second;
        if (!Ptr(sec)) continue;
        const Ogre::vector<InventorySection::SectionItem>::type& si = sec->items;
        if (si.size() > 256) continue;
        for (size_t k = 0; k < si.size(); ++k)
        {
            Item* item = si[k].item;
            if (!Obj(item)) continue;

            ::GameData* st = item->saveIntoInventoryRecord(container, 0);
            if (!Obj(st))
            {
                // Fall back to the base record alone rather than dropping the item silently.
                // A garment that arrives without its company sid still appears; a garment that
                // is never sent cannot.
                ::GameData* gd = item->getRecordDirect();
                if (!Obj(gd)) continue;
                out->sections.push_back(it->first);
                out->cells.push_back(coopkitcell::KitCell((unsigned int)si[k].x, (unsigned int)si[k].y));   /* kit2: its cell */
                out->baseSids.push_back(gd->stringID);
                out->companySids.push_back(std::string());
                out->materialSids.push_back(std::string());
                out->levels.push_back(0);
                out->details.push_back(CaptureDetail(item, std::string()));
                CaptureBag(item, out);   /* bag23 part 1 */
                continue;
            }

            out->sections.push_back(it->first);
            out->cells.push_back(coopkitcell::KitCell((unsigned int)si[k].x, (unsigned int)si[k].y));   /* kit2 (owner 102): the owner's cell */
            out->baseSids.push_back(SData(st, kBaseSidKey));
            out->companySids.push_back(SData(st, kCompanySidKey));
            out->materialSids.push_back(SData(st, kMaterialSidKey));
            out->levels.push_back(IData(st, kLevelKey));
            out->details.push_back(CaptureDetail(item, SData(st, kColorSidKey)));
            CaptureBag(item, out);   /* bag23 part 1 */
        }
    }
    ++g_c.captured;
    g_c.itemsCaptured += (int)out->Count();
    return true;
}

// deadlook1 fold (manager decision 2026-09-25): the WORN sections a dead copy's apply replaces. KEPT: shirt, legs, armour,
// head, boots (the section names MSG_CLOTHING carries - the owner's own inventory section names, seen in the P021/P022 lines).
// SKIPPED, left exactly as they are: every other section - backpack_attach, the weapon sections, main, belt, hip, flat,
// and any name not in this list. The engine drops a corpse's backpack and equipped weapons after death (F530(d));
// re-creating them would duplicate them. Compared without case.
static bool IsWornSection(const std::string& name)
{
    static const char* const kWorn[] = { "shirt", "legs", "armour", "head", "boots" };
    for (size_t k = 0; k < sizeof(kWorn) / sizeof(kWorn[0]); ++k)
        if (_stricmp(name.c_str(), kWorn[k]) == 0) return true;
    return false;
}

// deadlook1 fold 3: the same list, for appearance.cpp's looted mark (only a move in a worn section counts).
bool ClothingIsWornSection(const std::string& name) { return IsWornSection(name); }

// Removes and destroys every item in the WORN sections only, through the same two calls items.cpp's ITEM_MOVE remove
// makes on a ghost (takeItemOut(item, -1, true), then the engine's destroy). The items are listed
// first (each once) and removed after, so no section vector is walked while it changes. Returns how many were removed.
static int ClearWornSections(Inventory* inv)
{
    std::vector<Item*> doomed;
    const SecMap& secs = *(const SecMap*)((char*)inv + kSectionsOff);
    if (secs.size() > 64) return 0;
    for (SecMap::const_iterator it = secs.begin(); it != secs.end(); ++it)
    {
        if (!IsWornSection(it->first)) continue;
        InventorySection* sec = it->second;
        if (!Ptr(sec)) continue;
        const Ogre::vector<InventorySection::SectionItem>::type& si = sec->items;
        if (si.size() > 256) continue;
        for (size_t k = 0; k < si.size(); ++k)
        {
            Item* item = si[k].item;
            if (!Obj(item)) continue;
            bool seen = false;
            for (size_t j = 0; j < doomed.size(); ++j) if (doomed[j] == item) { seen = true; break; }
            if (!seen) doomed.push_back(item);
        }
    }
    int removed = 0;
    for (size_t k = 0; k < doomed.size(); ++k)
    {
        Item* gone = inv->takeItemOut(doomed[k], -1, true);
        if (Obj(gone) && coop::GameWorldPtr() != 0) { coop::GameWorldPtr()->destroy(gone, false, "coop dead-copy clothing (worn slot)"); ++removed; }
    }
    return removed;
}

// copy1 (COPY2): the section of `inv` with exactly this name, or 0. Walked the way ClearWornSections walks the map; the
// names are the ones the owner's CaptureGarments read from the same map on its side.
static InventorySection* SectionNamed(Inventory* inv, const std::string& name)
{
    const SecMap& secs = *(const SecMap*)((char*)inv + kSectionsOff);
    if (secs.size() > 64) return 0;
    for (SecMap::const_iterator it = secs.begin(); it != secs.end(); ++it)
        if (it->first == name) return Ptr(it->second) ? it->second : 0;
    return 0;
}

// copy2: InventorySection +0xD0 `enabled` - the byte InventorySection::addItem 0x74BD70 refuses on at its first line and
// InventorySection::setEnabled 0x745360 writes (animal-copy-items.md A2; Inventory.h `bool enabled; // 0xD0`).
static const int kSectionEnabledOff = 0xD0;


// copy1 (COPY2) + copy2: that section's own add. `sec->addItem` is the virtual call (vtable +0x10), which lands at
// InventorySection::addItem 0x74BD70 (Steam_1.0.65.br slot 4218) - the call items.cpp
// ItPlace makes to put an item anywhere in one named section. It may merge a stackable item into a stack there and
// destroy the incoming object, so the item is never touched after a yes. 0x74BD70 refuses a section whose `enabled`
// byte is 0, and a living animal's `main` is off, so the section is switched ON for this one add with the engine's own
// InventorySection::setEnabled 0x745360 (Steam_1.0.65.br slot 4251; the engine fills `main` first and sets the flag after,
// CharacterAnimal::setupInventorySections 0x6296A0) and the value READ is put back afterwards - never forced off.
// 1 placed, 0 refused, -1 faulted (the add is not atomic: the caller never destroys a faulted item). *switchedOn = 1
// when the flag was switched for the add; the restore runs after a fault too, in its own guard (*restoreFault = 1 when
// it faulted). No C++ object with a destructor in this frame (C2712).
static int SectionAddEnabledPod(InventorySection* sec, Item* item, int* switchedOn, int* restoreFault)
{
    int was = 1, rc = -1;
    *switchedOn = 0;
    *restoreFault = 0;
    __try
    {
        was = *((const unsigned char*)sec + kSectionEnabledOff) != 0 ? 1 : 0;
        if (coopkeep::KitSwitchOnForAdd(was) != 0) { *switchedOn = 1; sec->setEnabled(true); }
        rc = sec->addItem(item, 1) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { rc = -1; }
    if (*switchedOn != 0)
    {
        __try { sec->setEnabled(coopkeep::KitFlagToRestore(was) != 0); }
        __except (EXCEPTION_EXECUTE_HANDLER) { *restoreFault = 1; }
    }
    return rc;
}

// copy2 (review copy1 MINOR): the destroy of a created item both adds refused, guarded the way items.cpp ItDestroyPod
// guards it (that one has internal linkage in items.cpp) and with the engine's answer read: GameWorld::destroy returns
// false and frees nothing while the item is still marked in an inventory (F536(b)). 1 destroyed, 0 the engine refused
// (or no GameWorld), -1 the call faulted. No C++ object with a destructor in this frame (C2712).
// copy3 (H-copy2; copykeep.h KitSectionRoute): an ANIMAL copy's kit item goes into the owner's section the way the engine
// filled that section before its slot filter existed (CharacterAnimal::setupInventorySections 0x6296A0 adds the filter
// only after the fill, decomp_6296a0.txt:216). The section is switched ON as copy2 does (the engine's fill ran with `main`
// ON; the value read is put back), the first free cell is found with the engine's own
// InventorySection::itemInArea 0x745BB0 (Steam_1.0.65.br slot 4237) in 0x74BD70's order (coopkeep::KitFirstFreeCell),
// and the item is placed with `sec->_addItem(item, x, y)` - the virtual call (vtable +0x18; Steam_1.0.65.br slot 4219 =
// 0x748BE0) that 0x74BD70:146 and Inventory::loadFrom's 0x74B5A0 make. Nothing is written by hand. 1 placed, 0 no free
// cell (*noCell = 1; the add was not called), -1 faulted or unknown (never destroyed). No C++ object with a destructor
// in these frames (C2712).
struct KitCellCtx { InventorySection* sec; Item* item; };

static int KitCellTakenEngine(void* ctx, int x, int y)
{
    const KitCellCtx* k = (const KitCellCtx*)ctx;
    return k->sec->itemInArea(k->item, x, y) ? 1 : 0;
}

// The section's item rows: begin +0x40, end +0x48, 16 bytes a row (decomp_745bb0.txt:19-21). A read only.
static long long KitSectionRowCount(const InventorySection* sec)
{
    const char* b = (const char*)sec;
    return (long long)(*(const char* const*)(b + 0x48) - *(const char* const*)(b + 0x40)) >> 4;
}

// kit2 (owner 102): the owner's cell (px,py) is tried first (coopkitcell::KitPlaceCell; -1,-1 = no cell known: the first free
// cell, as before). Placed = coopkitcell::kKitCellExact (1) or kKitCellRelocated (2).
static int SectionAddAnimalEnginePod(InventorySection* sec, Item* item, int px, int py, int* switchedOn, int* restoreFault, int* noCell)
{
    int was = 1, rc = -1;
    *switchedOn = 0;
    *restoreFault = 0;
    *noCell = 0;
    __try
    {
        was = *((const unsigned char*)sec + kSectionEnabledOff) != 0 ? 1 : 0;
        if (coopkeep::KitSwitchOnForAdd(was) != 0) { *switchedOn = 1; sec->setEnabled(true); }
        const char* sb = (const char*)sec;
        const char* ib = (const char*)item;
        KitCellCtx ctx;
        ctx.sec = sec;
        ctx.item = item;
        int x = -1, y = -1;
        // section grid +0x30 w / +0x34 h, item footprint +0x130 w / +0x134 h (0x74BD70 lines 104-105, 0x745BB0 lines 14-17)
        const int kind = coopkitcell::KitPlaceCell(*(const int*)(sb + 0x30), *(const int*)(sb + 0x34), *(const int*)(ib + 0x130),
                                                   *(const int*)(ib + 0x134), px, py, &KitCellTakenEngine, &ctx, &x, &y);   /* kit2 */
        if (kind == coopkitcell::kKitCellNone)
        {
            *noCell = 1;
            rc = 0;
        }
        else
        {
            const long long before = KitSectionRowCount(sec);
            sec->_addItem(item, x, y);
            rc = (coopkeep::KitEngineRouteResult(before, KitSectionRowCount(sec)) == 1) ? kind : -1;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { rc = -1; }
    if (*switchedOn != 0)
    {
        __try { sec->setEnabled(coopkeep::KitFlagToRestore(was) != 0); }
        __except (EXCEPTION_EXECUTE_HANDLER) { *restoreFault = 1; }
    }
    return rc;
}

/* kit2 (owner 102): a kit item at the owner's cell in the owner's section - items.cpp's placement (ItPlace: cell1's one cell test,
   ItCellAccepts, then that same section's first free cell, never a merge: the object passed is the object placed). The section is
   switched ON for the add as copy2 does and the value read is put back. coopkitcell::kKitCellExact 1, kKitCellRelocated 2, 0 no
   room in the section, -1 faulted (never destroyed). No C++ object with a destructor in this frame (C2712). */
static int SectionPlaceExactPod(InventorySection* sec, Item* item, int x, int y, int* switchedOn, int* restoreFault)
{
    int was = 1, rc = -1;
    *switchedOn = 0;
    *restoreFault = 0;
    __try
    {
        was = *((const unsigned char*)sec + kSectionEnabledOff) != 0 ? 1 : 0;
        if (coopkeep::KitSwitchOnForAdd(was) != 0) { *switchedOn = 1; sec->setEnabled(true); }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { rc = -2; }
    if (rc != -2)
    {
        const int r = coop::ItemPlaceInSection((void*)sec, (void*)item, x, y);   /* 1 exact, 2 same section elsewhere, 0 none, -1 fault */
        rc = (r == 1) ? coopkitcell::kKitCellExact : (r == 2 ? coopkitcell::kKitCellRelocated : (r == 0 ? coopkitcell::kKitCellNone : -1));
    }
    else rc = -1;
    if (*switchedOn != 0)
    {
        __try { sec->setEnabled(coopkeep::KitFlagToRestore(was) != 0); }
        __except (EXCEPTION_EXECUTE_HANDLER) { *restoreFault = 1; }
    }
    return rc;
}

static int KitDestroyPod(Item* item, const char* why)
{
    __try { return (coop::GameWorldPtr() != 0 && coop::GameWorldPtr()->destroy(item, false, why)) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* bag23 part 1 (inv3 phase 2): the kit's rows for kit item i, or 0 when it is not a pack holding anything. */
static const coopbag::KitBag* KitBagFor(const GarmentSet& set, size_t i)
{
    for (size_t k = 0; k < set.bags.size(); ++k) if ((size_t)set.bags[k].kitIndex == i) return &set.bags[k];
    return 0;
}

/* bag23 part 1: kit item i could not be made here - a pack's rows go with it, counted and said. */
static int KitBagLostWith(const GarmentSet& set, size_t i)
{
    const coopbag::KitBag* kb = KitBagFor(set, i);
    if (kb == 0) return 0;
    ErrorLog("[P022] kit pack " + set.baseSids[i] + " could not be created here - its " + S((long long)kb->rows.size())
             + " row(s) are LOST on this copy (bagRows lost)");
    return (int)kb->rows.size();
}

/* bag23 part 1 (inv3 phase 2, D3(b)): FILL BEFORE PLACE - the fresh pack is in no inventory yet (items.cpp ItemBagFillCopy). */
static void FillKitBag(Item* item, const GarmentSet& set, size_t i, ::Character* c, int* applied, int* relocated, int* lost)
{
    *applied = 0; *relocated = 0; *lost = 0;
    const coopbag::KitBag* kb = KitBagFor(set, i);
    if (kb == 0) return;
    coop::ItemBagFillCopy((void*)item, kb->rows, (void*)c, applied, relocated, lost, "kit");
}

static __declspec(noinline) bool ApplyGarmentsBody(::Character* c, const GarmentSet& set, bool wornOnly)
{
    Inventory* inv = GetInventory(c);
    if (inv == 0) return false;

    GameDataContainer* container = GetScratchContainer(c);
    if (container == 0)
    {
        ++g_c.noContainer;
        ErrorLog("[P022] apply refused: no data container reachable for this character");
        return false;
    }
    // RootObjectFactory is NON-polymorphic (F073), so the vtable guard must not be used
    // on it - that exact mistake made T018 stage 1 refuse a valid factory.
    if (coop::GameWorldPtr() == 0 || !Ptr(coop::GameWorldPtr()->objectFactory)) return false;
    RootObjectFactory* factory = coop::GameWorldPtr()->objectFactory;

    // Remove what this instance rolled for itself. `destroy=true` because these items exist
    // nowhere else and leaving them would leak; `skipUnique=false` because a freshly spawned
    // NPC has nothing unique worth preserving.
    // deadlook1 fold: a dead copy empties its WORN sections only; everything else stays where it is.
    int wornCleared = 0, skippedNotWorn = 0;
    /* kit2 (owner 102): THE KIT APPLY IS THE MOD'S OWN WRITE. The clear (clearAll -> InventorySection::removeItem, hooked) and
       the refill (_addItem, hooked) run with items.cpp's applying-thread marker up, so neither is booked as an unpaired request
       nor published. The marker READ is put back at the end (a caller may already hold it); it is raised again after every
       call into items.cpp that may lower it (ClearWornSections, FillKitBag). */
    const long kitPrevApplying = coop::ItemApplyingEnter();
    if (!wornOnly) inv->clearAll(true, false);
    else wornCleared = ClearWornSections(inv);
    coop::ItemApplyingEnter();

    int created = 0, unresolved = 0, addFailed = 0, placedBySection = 0, destroyed = 0, sectionFault = 0;
    int enabledForAdd = 0, destroyRefused = 0, destroyFault = 0, restoreFault = 0;   /* copy2 */
    int placedEngineRoute = 0, engineRouteNoCell = 0;   /* copy3 */
    int bagApplied = 0, bagRelocated = 0, bagLost = 0;   /* bag23 part 1 */
    int kitExact = 0, kitRelocated = 0, kitLost = 0;   /* kit2 */
    const bool haveCells = set.cells.size() == set.Count() && set.sections.size() == set.Count();   /* kit2: the owner's cells rode the kit */
    float animalAge = 0.0f;
    const bool isAnimal = ReadAnimalAge01(c, &animalAge);   /* copy3: CharacterAnimal (getter offset +0x708), read-only */
    for (size_t i = 0; i < set.Count(); ++i)
    {
        if (wornOnly && (i >= set.sections.size() || !IsWornSection(set.sections[i]))) { ++skippedNotWorn; continue; }
        // F183: the state record needs one key. Everything else createItem reads is
        // null-checked downstream with a default substituted.
        ::GameData* state = container->newRecord((itemType)kItemStateType, "", "coopitem");
        if (!Obj(state)) { ++unresolved; bagLost += KitBagLostWith(set, i); continue; }
        // All four keys the engine reads. A weapon (itemType 2) is refused outright unless
        // `company sid` resolves - that is F185, and it cost exactly one Staff to find.
        state->stringFields[kBaseSidKey] = set.baseSids[i];
        if (i < set.companySids.size() && !set.companySids[i].empty())
            state->stringFields[kCompanySidKey] = set.companySids[i];
        if (i < set.materialSids.size() && !set.materialSids[i].empty())
            state->stringFields[kMaterialSidKey] = set.materialSids[i];
        if (i < set.levels.size())
            state->intFields[kLevelKey] = set.levels[i];
        const bool haveDetail = set.details.size() == set.Count();
        if (haveDetail && !set.details[i].colorSid.empty())
            state->stringFields[kColorSidKey] = set.details[i].colorSid;

        Item* item = factory->createItem(state);
        if (!Obj(item))
        {
            // createItem returns 0 when the base sid does not resolve on this instance -
            // which is a real possibility worth counting rather than assuming away, since it
            // would mean the two installs do not carry identical static data.
            ++unresolved;
            bagLost += KitBagLostWith(set, i);   /* bag23 part 1 */
            continue;
        }
        ++created;

        /* quality1: the owner's quality, charges, stack size, item function and unique flag, written onto the fresh item
           before it enters the inventory (the fields ItCreateItem writes for a live move with setFields=1). The add below is
           the whole-inventory addItem(item, 1, ...) this path always used - NOT the live path's exact-slot _addItem - so
           whether the engine keeps a written stack size through that add, or merges two stacks correctly, is not measured
           yet (review-quality1 1/2). */
        if (haveDetail)
        {
            float back = 0.0f;
            const coopgarment::GarmentDetail& d = set.details[i];
            if (WriteItemDetailPod(item, d.quality, d.charges, (int)d.quantity, d.functionKind, d.unique, &back) && back == d.quality) ++g_c.detailsWritten;
            else ++g_c.detailsWriteFault;
        }
        /* inv6 phase 2 (R6): the owner's stolen mark on the copy's item, before it enters the inventory (ItOwnerApply's rule). */
        if (haveDetail && set.details[i].owner.kind != coopmark::kOwnNone) coop::ItemOwnerApplyTo((void*)item, set.details[i].owner, set.baseSids[i]);
        /* bag23 part 1 (inv3 phase 2, D3(b)): a pack is FILLED BEFORE IT IS PLACED, from the kit's rows for this index - after its
           own mark above, so rows that arrived clean or with their own exact marks keep them (review-inv6p2 MEDIUM's rule). A dead
           copy (wornOnly) skips pack rows: its pack is left where it is (the backpack section is skipped above anyway). */
        int packA = 0, packR = 0, packL = 0;
        if (!wornOnly) FillKitBag(item, set, i, c, &packA, &packR, &packL);
        coop::ItemApplyingEnter();   /* kit2: the pack fill called into items.cpp - the marker is up again for the placement */

        // Placement is left to the engine first: `addItem` finds the slot the garment belongs in.
        // copy1 (COPY2): when it refuses (an animal's only item section `main` is switched off, so "wherever it
        // fits" finds nowhere - T378 Skimmer/Garru addFailed), the item goes into the section the owner had it in,
        // through that section's own add (the kit carries no slot, so the section picks the cell). If that refuses
        // too, or the section does not exist here, the created item is in no inventory and is destroyed.
        /* kit2 (owner 102): EXACT PLACEMENT. When the kit carries the owner's cells, the item goes into the OWNER'S section at the
           OWNER'S cell - never the engine's whole-inventory addItem, which picks its own section and cell. That cell refused here
           (taken, off the grid, a slot the item does not fit) = that same section's first free cell, counted kitRelocated; no
           room in that section, or no such section on this copy = kitLost (the created item is destroyed, as copy1 does) - never
           another section, never a merge. Animals keep copy3's engine route (their `main` has a slot filter the fill predates). */
        int engineAdd = 0;
        if (haveCells)
        {
            engineAdd = 1;   /* the whole-inventory add is not asked: the rest of this iteration is the exact path */
            InventorySection* own = SectionNamed(inv, set.sections[i]);
            int switchedOn = 0, restoreFailed = 0, noCell = 0, got = coopkitcell::kKitCellNone;
            const int px = (int)set.cells[i].x, py = (int)set.cells[i].y;
            if (own != 0)
            {
                if (coopkeep::KitSectionRoute(isAnimal ? 1 : 0) == coopkeep::kKitRouteAnimalEngine)
                    got = SectionAddAnimalEnginePod(own, item, px, py, &switchedOn, &restoreFailed, &noCell);
                else
                    got = SectionPlaceExactPod(own, item, px, py, &switchedOn, &restoreFailed);
            }
            if (switchedOn != 0) ++enabledForAdd;
            if (restoreFailed != 0) ++restoreFault;
            if (got == coopkitcell::kKitCellExact) ++kitExact;
            else if (got == coopkitcell::kKitCellRelocated) ++kitRelocated;
            else if (got < 0) ++sectionFault;   /* the add faulted or its answer is unknown: the item is never destroyed */
            else
            {
                ++kitLost;
                ErrorLog("[P022] kit item " + set.baseSids[i] + " has no room in '" + set.sections[i] + "'"
                         + (own == 0 ? std::string(" (no such section on this copy)") : std::string()) + " - LOST on this copy (kitLost)");
                const int gone = KitDestroyPod(item, "coop copy kit item: no room in the owner's section (kit2)");
                const int which = coopkeep::KitDestroyCounter(gone);
                if (packA > 0)
                {
                    ErrorLog("[P022] kit pack " + set.baseSids[i] + " found no place on this copy - its " + S((long long)packA)
                             + " row(s) are LOST with it (bagRows lost)");
                    packL += packA; packA = 0; packR = 0;
                }
                if (which == coopkeep::kKitCountDestroyed) ++destroyed;
                else if (which == coopkeep::kKitCountDestroyFault) ++destroyFault;
                else ++destroyRefused;
            }
        }
        else engineAdd = inv->addItem(item, 1, false, false) ? 1 : 0;
        if (!haveCells && engineAdd == 0)
        {
            ++addFailed;
            InventorySection* own = i < set.sections.size() ? SectionNamed(inv, set.sections[i]) : 0;
            int switchedOn = 0, restoreFailed = 0;   /* copy2: the section switched ON for the add, flag restored */
            int secAdd = 0, noCell = 0;
            if (own != 0)
            {
                if (coopkeep::KitSectionRoute(isAnimal ? 1 : 0) == coopkeep::kKitRouteAnimalEngine)
                {
                    secAdd = SectionAddAnimalEnginePod(own, item, -1, -1, &switchedOn, &restoreFailed, &noCell);   /* copy3; kit2: no cell known */
                    if (secAdd > 1) secAdd = 1;   /* kit2: placed (the first free cell - no owner cell was known) */
                    if (secAdd == 1) ++placedEngineRoute;
                    if (noCell != 0) ++engineRouteNoCell;
                }
                else secAdd = SectionAddEnabledPod(own, item, &switchedOn, &restoreFailed);
            }
            if (switchedOn != 0) ++enabledForAdd;
            if (restoreFailed != 0) ++restoreFault;
            const int place = coopkeep::KitPlacement(engineAdd, secAdd);
            if (place == coopkeep::kKitPlacedBySection) ++placedBySection;
            else if (place == coopkeep::kKitLeaveFaulted) ++sectionFault;
            else if (place == coopkeep::kKitDestroy)
            {
                const int gone = KitDestroyPod(item, "coop copy kit item refused by the inventory and its own section (copy1)");
                const int which = coopkeep::KitDestroyCounter(gone);   /* copy2: a refused or faulted destroy is counted */
                if (packA > 0)   /* bag23 part 1: the rows inside a pack that found no place went with it */
                {
                    ErrorLog("[P022] kit pack " + set.baseSids[i] + " found no place on this copy - its " + S((long long)packA)
                             + " row(s) are LOST with it (bagRows lost)");
                    packL += packA; packA = 0; packR = 0;
                }
                if (which == coopkeep::kKitCountDestroyed) ++destroyed;
                else if (which == coopkeep::kKitCountDestroyFault) ++destroyFault;
                else ++destroyRefused;
            }
        }
        bagApplied += packA; bagRelocated += packR; bagLost += packL;   /* bag23 part 1 */
    }

    g_c.itemsCreated    += created;
    g_bagRowsApplied += bagApplied; g_bagRowsRelocated += bagRelocated; g_bagRowsLost += bagLost;   /* bag23 part 1 */
    g_c.itemsUnresolved += unresolved;
    g_c.itemsAddFailed  += addFailed;
    g_c.addPlacedBySection += placedBySection;   /* copy1 */
    g_c.addFailedDestroyed += destroyed;
    g_c.addSectionFault    += sectionFault;
    g_c.addEnabledForAdd   += enabledForAdd;    /* copy2 */
    g_c.addDestroyRefused  += destroyRefused;
    g_c.addDestroyFault    += destroyFault;
    g_c.addEnabledRestoreFault += restoreFault;
    g_c.addPlacedEngineRoute += placedEngineRoute;   /* copy3 */
    g_c.addEngineRouteNoCell += engineRouteNoCell;
    g_c.kitPlacedExact += kitExact; g_c.kitRelocated += kitRelocated; g_c.kitLost += kitLost;   /* kit2 */
    ++g_c.applied;
    coop::ItemApplyingLeave(kitPrevApplying);   /* kit2: the marker as it was before this apply */

    DebugLog("[P022] applied garments: wanted=" + S((long long)set.Count())
             + " created=" + S(created)
             + " unresolved=" + S(unresolved)
             + " addFailed=" + S(addFailed)
             + " addPlacedBySection=" + S(placedBySection) + " addFailedDestroyed=" + S(destroyed)   /* copy1 */
             + (sectionFault != 0 ? " addSectionFault=" + S(sectionFault) : std::string())
             + " addEnabledForAdd=" + S(enabledForAdd)   /* copy2 */
             + (isAnimal ? " animal=1 addPlacedEngineRoute=" + S(placedEngineRoute) + " addEngineRouteNoCell=" + S(engineRouteNoCell)
                         : std::string())   /* copy3 */
             + (destroyRefused != 0 || destroyFault != 0 || restoreFault != 0
                    ? " addDestroyRefused=" + S(destroyRefused) + " addDestroyFault=" + S(destroyFault) + " addEnabledRestoreFault=" + S(restoreFault)
                    : std::string())
             + (bagApplied != 0 || bagLost != 0 ? " bagRows[applied,relocated,lost]=" + S(bagApplied) + "," + S(bagRelocated) + "," + S(bagLost)
                                                : std::string())   /* bag23 part 1 */
             + (haveCells ? " kit[exact,relocated,lost]=" + S(kitExact) + "," + S(kitRelocated) + "," + S(kitLost)
                          : std::string(" kitCells=absent"))   /* kit2 */
             + (wornOnly ? " wornOnly=1 wornCleared=" + S(wornCleared) + " skippedNotWorn=" + S(skippedNotWorn)
                         : std::string()));
    return true;
}

/* kit3 (kit2 review + T522, review D4): THE KIT APPLY ALWAYS PUTS THE APPLYING MARKER BACK. ApplyGarmentsBody raises items.cpp's
   applying marker around the clear and the refill and lowers it at its end; an engine call faulting in between and caught by a
   handler further out would leave it raised, and every later main-thread item change would be taken for the mod's own write and
   never published. The marker as it was on entry is put back on EVERY exit (__finally; no object with a destructor here, C2712). */
static bool ApplyGarmentsImpl(::Character* c, const GarmentSet& set, bool wornOnly)
{
    const long entryApplying = coop::ItemApplyingPeek();
    bool ok = false;
    __try { ok = ApplyGarmentsBody(c, set, wornOnly); }
    __finally { coop::ItemApplyingLeave(entryApplying); }
    return ok;
}

bool ApplyGarments(::Character* c, const GarmentSet& set) { return ApplyGarmentsImpl(c, set, false); }

bool ApplyWornGarments(::Character* c, const GarmentSet& set) { return ApplyGarmentsImpl(c, set, true); }

std::string GarmentSummary(const GarmentSet& set)
{
    std::string out = "garments=" + S((long long)set.Count()) + " [";
    for (size_t i = 0; i < set.Count(); ++i)
    {
        out += (i ? "|" : "") + set.sections[i] + ":" + set.baseSids[i];
        // The company sid is printed because it is the field a weapon fails without (F185).
        // A blank one on a weapon is the signature of that failure and must be readable.
        if (i < set.companySids.size() && !set.companySids[i].empty())
            out += "+co:" + set.companySids[i];
        if (set.details.size() == set.Count())   /* quality1 */
        {
            char b[64];
            std::sprintf(b, "+q:%.2f", set.details[i].quality);
            out += b;
            if (set.details[i].quantity > 1) out += "x" + S((long long)set.details[i].quantity);
        }
    }
    return out + "]";
}

void SerialiseGarments(const GarmentSet& set, std::vector<char>* b)
{
    PutU32(b, (unsigned int)set.Count());
    for (size_t i = 0; i < set.Count(); ++i)
    {
        PutStr(b, set.sections[i]);
        PutStr(b, set.baseSids[i]);
        PutStr(b, i < set.companySids.size()  ? set.companySids[i]  : std::string());
        PutStr(b, i < set.materialSids.size() ? set.materialSids[i] : std::string());
        PutU32(b, (unsigned int)(i < set.levels.size() ? set.levels[i] : 0));
    }
    if (set.details.size() == set.Count()) coopgarment::EncodeGarmentDetails(b, set.details);   /* quality1: after the list */
    /* bag23 part 1 (inv3 phase 2, protocol 81): the packs' contents, LAST - after the details and their OWNL tail. Nothing when no
       pack holds anything. The capture already refused any pack that cannot travel whole, so an encode that still refuses is a
       defect: said and counted, and the kit goes without it (its garments still dress the copy). */
    if (!set.bags.empty() && !coopbag::EncodeKitBags(b, set.bags, set.Count()))
    {
        ++g_bagKitEncodeRefused;
        ErrorLog("[P022] the kit's packs block would not encode (" + S((long long)set.bags.size()) + " pack(s)) - the copy's packs"
                 " will be EMPTY (bagKitEncodeRefused, a defect)");
    }
    /* kit2 (owner 102, protocol 83): each item's cell, LAST - after the packs block. Only when every item's cell was read; an encode
       that refuses (a cell out of range) sends none: the copy then places as from an older sender (said and counted). */
    if (set.Count() != 0 && set.cells.size() == set.Count() && !coopkitcell::EncodeKitCells(b, set.cells, set.Count()))
    {
        ++g_kitCellsEncodeRefused;
        ErrorLog("[P022] the kit's cells block would not encode (" + S((long long)set.Count()) + " item(s)) - the copy places them"
                 " where its engine picks (kitCellsEncodeRefused)");
    }
    /* review-inv6p2 LOW: the kit's marked rows are counted by SendClothing after a successful send - not while serialising */
}

bool DeserialiseGarments(const std::vector<char>& b, size_t at, GarmentSet* out)
{
    unsigned int n = 0;
    if (!GetU32(b, &at, &n) || n > kMaxItems) return false;
    for (unsigned int i = 0; i < n; ++i)
    {
        std::string sec, sid, co, mat;
        unsigned int lvl = 0;
        if (!GetStr(b, &at, &sec) || !GetStr(b, &at, &sid)
            || !GetStr(b, &at, &co) || !GetStr(b, &at, &mat)
            || !GetU32(b, &at, &lvl)) return false;
        out->sections.push_back(sec);
        out->baseSids.push_back(sid);
        out->companySids.push_back(co);
        out->materialSids.push_back(mat);
        out->levels.push_back((int)lvl);
    }
    /* quality1: the optional details block after the list. Absent = an older sender (the copy keeps factory values); a
       malformed block is dropped whole and counted - the list itself is still good. */
    size_t detailEnd = at;   /* bag23 part 1: where the packs block starts */
    const int dr = b.empty() ? coopgarment::kDetailAbsent
                             : coopgarment::DecodeGarmentDetailsEnd(&b[0], b.size(), at, (size_t)n, &out->details, &detailEnd);
    if (dr == coopgarment::kDetailAbsent) ++g_c.detailsAbsent;
    else if (dr == coopgarment::kDetailBad) { ++g_c.detailsBad; out->details.clear(); }
    else if (dr == coopgarment::kDetailBadOwner) coop::ItemOwnerNoteBadRecv();   /* inv6 phase 2: details kept, the marks dropped */
    /* bag23 part 1 (inv3 phase 2, protocol 81): the packs' contents after the details and their OWNL tail. Absent = an older
       sender, or no pack holding anything. Malformed: the garments above still apply and every pack stays EMPTY - dropped whole,
       counted (bagRows lost +1, the block's own row count cannot be trusted; bagKitBad) and logged, never a crash. When the
       details block itself could not be read its end is unknown, so a packs block after it cannot be found: said, not guessed. */
    if (dr == coopgarment::kDetailOk || dr == coopgarment::kDetailAbsent)
    {
        int rowWhy = coopbag::kBagOk;
        size_t kitEnd = 0;
        const bool cellsNext = coopkitcell::KitCellsAt(b.empty() ? 0 : &b[0], b.size(), detailEnd);   /* kit2: no packs block */
        const int kr = cellsNext ? coopbag::kKitBagAbsent
                                 : coopbag::DecodeKitBags(b.empty() ? 0 : &b[0], b.size(), detailEnd, (size_t)n, &out->bags, &kitEnd, &rowWhy);
        if (kr == coopbag::kKitBagBad)
        {
            out->bags.clear();
            ++g_bagKitBad; ++g_bagRowsLost;
            if (rowWhy == coopbag::kBagBadOwner) coop::ItemOwnerNoteBadRecv();
            ErrorLog("[P022] CLOTHING packs block malformed (" + std::string(rowWhy == coopbag::kBagOk ? "its wrapper" : coopbag::BagDecodeWhy(rowWhy))
                     + ") - the garments apply, the copy's packs stay EMPTY (bagRows lost, bagKitBad)");
        }
        /* kit2 (owner 102, protocol 83): the cells block, LAST - right after the details when the kit has no packs, else after the
           packs block. Absent = an older sender; malformed = dropped whole (the garments place as from an older sender). When the
           packs block itself was malformed its end is unknown, so the cells are not looked for. */
        if (cellsNext || kr != coopbag::kKitBagBad)
        {
            const size_t cellAt = cellsNext ? detailEnd : (kr == coopbag::kKitBagOk ? kitEnd : b.size());
            const int cr = coopkitcell::DecodeKitCells(b.empty() ? 0 : &b[0], b.size(), cellAt, (size_t)n, &out->cells, 0);
            if (cr == coopkitcell::kKitCellAbsent) ++g_kitCellsAbsent;
            else if (cr == coopkitcell::kKitCellBad)
            {
                out->cells.clear();
                ++g_kitCellsBad;
                ErrorLog("[P022] CLOTHING cells block malformed - the garments apply where this copy's engine places them (kitCellsBad)");
            }
        }
    }
    else ErrorLog("[P022] CLOTHING details block unreadable - a packs block after it cannot be located; the copy's packs stay EMPTY");
    return true;
}

/* LIMBS (parity P38 / P39): A CHARACTER'S FOUR LIMBS - LOST, CRUSHED, OR A ROBOTIC LIMB FITTED - ARE ITS OWNER'S.
   Where the engine keeps them (read from the 1.0.65 exe's own bytes): MedicalSystem (Character +0x458) +0xC8 is a
   RobotLimbs*, made on first need by amputate / setRobotLimbItem (0 = all four whole); RobotLimbs +0x10 is LimbState[4]
   and +0x20 the fitted Item*[4] (RobotLimbs::setLimb 0xCFD90 writes both). The owner reads them for every STATE
   (LimbsOfOwned). The copy's game compares and changes only a limb that differs, through the engine's own calls
   (ApplyOwnerLimbs):
     a stump          - MedicalSystem::amputate 0x64E330 (limb, no severed item, no force). amputate does nothing to a limb
                        with a robotic limb fitted, so that one goes through setRobotLimbItem(limb, 0) and the item that
                        was fitted (now in no inventory) is destroyed;
     a robotic limb   - the item is made from its sid the way the kit makes worn items (factory createItem), its quality
                        written, then MedicalSystem::setRobotLimbItem 0x644C00(limb, item, 1) - the call RobotLimbs::load
                        makes for a saved fitted limb, straight after createItem, with the same last argument.
   The fitted limb's damage then comes from that limb's health part, which the same STATE carries.
   setRobotLimbItem(limb, 0) (build/decomp_644c00.txt) leaves the old item alive and points nothing at it any more:
   RobotLimbs::setLimb overwrites the slot (it destroys only a stump's item) and the health part's item pointer (+0x28) is
   cleared after the part's damage is written into the item. It touches no inventory. So the mod destroys the old item.
   Nothing is called from the STATE handler: OnState records the owner's latest block per copy, and LimbSafePointDrain
   applies it at the K2 safe point with the AI worker paused - amputate grows the wound list (MedicalSystem +0x178) and
   setRobotLimbItem swaps the limb pointers that the worker's medical tick reads.
   Left alone and counted: whole on the owner but not here (the engine has no road back to flesh), crushed (crushLimb
   0x6430C0 is an empty function in this build and nothing calls setLimb with crushed), a fitted item with no name.
   A copy's own amputation is refused by the gate below unless the mod's own limb apply (the K2 safe point) is making the call. */
const size_t kLimbMedical     = 0x458;   // Character -> MedicalSystem (medical.cpp kMedicalOffset)
const size_t kLimbMedRobot    = 0xC8;    // MedicalSystem -> RobotLimbs*
const size_t kLimbMedOwner    = 0xE0;    // MedicalSystem -> its Character
const size_t kLimbRobotStates = 0x10;    // RobotLimbs -> LimbState[4] (int)
const size_t kLimbRobotItems  = 0x20;    // RobotLimbs -> Item*[4]

unsigned long long kLimbAmputateRva = 0; static AddrReg kLimbAmputateRva_reg("MedicalSystem_amputate", &kLimbAmputateRva);   /* Steam_1.0.65 0x64E330 */
unsigned long long kLimbSetRobotRva = 0; static AddrReg kLimbSetRobotRva_reg("MedicalSystem_setRobotLimbItem", &kLimbSetRobotRva);   /* Steam_1.0.65 0x644C00 */

typedef void (*LimbAmputateFn)(void* med, int limb, bool createSeveredItem, const float* force);
typedef void (*LimbSetRobotFn)(void* med, int limb, void* item, bool keepDamage);
LimbAmputateFn orig_limbAmputate = 0;
volatile LONG  g_limbApplyThread = 0;   // the thread inside the mod's own amputate / setRobotLimbItem call (0 = none)

volatile LONG64 g_copyAmputateRefused  = 0;   // a copy's own amputate refused: its limbs come from its owner
volatile LONG64 g_copyAmputateApplying = 0;   // a copy's amputate let through: ApplyOwnerLimbs is applying the owner's word
volatile LONG64 g_copyAmputatePassed   = 0;   // amputate on a character this game drives, or not replicated: original ran
long long g_limbCarried = 0, g_limbReadFault = 0, g_limbAbsent = 0, g_limbRefusedMine = 0, g_limbNoCopy = 0, g_limbDeferredRebuild = 0;
long long g_limbDeferredCarry = 0, g_limbWriterChanged = 0, g_limbFitFaultKept = 0;
long long g_limbAmputated = 0, g_limbUnfitted = 0, g_limbFitted = 0, g_limbFitUnresolved = 0, g_limbFitKnownBad = 0;
long long g_limbCallFault = 0, g_limbNoRow = 0, g_limbNoEffect = 0, g_limbItemDestroyFault = 0, g_limbNoEffectKept = 0;
long long g_limbCannotRegrow = 0, g_limbNoCrushRoad = 0, g_limbUnnamed = 0, g_limbLogLines = 0;
long long g_limbQueued = 0, g_limbQueueFull = 0, g_limbDrainBlocked = 0, g_limbNotBuilt = 0, g_limbDeadSkip = 0;
long long g_limbImplausible = 0, g_limbFitNotRobot = 0, g_limbQualityRefit = 0, g_limbForgot = 0, g_limbTestRuns = 0;

/* 1 = states / items read; 0 = no RobotLimbs yet (all four whole); -1 = the read faulted. */
static int ReadLimbsPod(const void* c, int* states, void** items)
{
    for (unsigned int i = 0; i < cooplimb::kLimbCount; ++i) { states[i] = 0; items[i] = 0; }
    __try
    {
        const char* med = (const char*)c + kLimbMedical;
        const char* rl = *(const char* const*)(med + kLimbMedRobot);
        if (rl == 0) return 0;
        for (unsigned int i = 0; i < cooplimb::kLimbCount; ++i)
        {
            states[i] = *(const int*)(rl + kLimbRobotStates + 4 * i);
            items[i] = *(void* const*)(rl + kLimbRobotItems + 8 * i);
        }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static unsigned char LimbStateByte(int s)
{
    return (s >= (int)cooplimb::kLimbOriginal && s <= (int)cooplimb::kLimbCrushed) ? (unsigned char)s : cooplimb::kLimbOriginal;
}

/* The fitted item's base sid into out[kLimbSidMax + 1]; false (out empty) = unreadable or longer than the wire takes. */
static bool LimbItemSid(void* item, char* out)
{
    out[0] = 0;
    if (!Obj(item)) return false;
    ::GameData* gd = ((Item*)item)->getRecordDirect();
    if (!Obj(gd)) return false;
    const std::string& sid = gd->stringID;
    if (sid.empty() || sid.size() > cooplimb::kLimbSidMax) return false;
    std::memcpy(out, sid.c_str(), sid.size());
    out[sid.size()] = 0;
    return true;
}

/* MedicalSystem::amputate(limb, severed, no force). 1 = called; -1 = faulted; -2 = no address row. */
static int LimbAmputatePod(void* med, int limb, bool severed)
{
    const unsigned long long fn = AddrAbs(kLimbAmputateRva);
    if (kLimbAmputateRva == 0 || fn == 0) return -2;
    const float noForce[3] = { 0.0f, 0.0f, 0.0f };
    __try { ((LimbAmputateFn)fn)(med, limb, severed, noForce); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* setRobotLimbItem(limb, item, 1): item 0 = take the fitted limb off (a stump). The last argument 1 keeps the health part's
   own damage values (0 would load them from the item, or zero them on removal). 1 = called; -1 = faulted; -2 = no row. */
static int LimbSetRobotPod(void* med, int limb, void* item)
{
    const unsigned long long fn = AddrAbs(kLimbSetRobotRva);
    if (kLimbSetRobotRva == 0 || fn == 0) return -2;
    __try { ((LimbSetRobotFn)fn)(med, limb, item, true); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* The mod's own engine limb calls. The marker (g_limbApplyThread = this thread) lets a copy's amputate through the gate;
   with `guard` the item sync neither books nor publishes what the call makes or moves (ItemApplyingEnter). */
static int LimbAmputateCall(void* med, int limb, bool severed, bool guard)
{
    const long prevItems = guard ? ItemApplyingEnter() : 0;
    const LONG prevThread = InterlockedExchange(&g_limbApplyThread, (LONG)::GetCurrentThreadId());
    const int rc = LimbAmputatePod(med, limb, severed);
    InterlockedExchange(&g_limbApplyThread, prevThread);
    if (guard) ItemApplyingLeave(prevItems);
    return rc;
}

static int LimbSetRobotCall(void* med, int limb, void* item)
{
    const long prevItems = ItemApplyingEnter();
    const LONG prevThread = InterlockedExchange(&g_limbApplyThread, (LONG)::GetCurrentThreadId());
    const int rc = LimbSetRobotPod(med, limb, item);
    InterlockedExchange(&g_limbApplyThread, prevThread);
    ItemApplyingLeave(prevItems);
    return rc;
}

/* A robotic limb item from its sid, made as the kit makes a worn item, with the owner's quality. 0 = not made. */
static Item* LimbMakeItem(::Character* c, const char* sid, float quality)
{
    GameDataContainer* container = GetScratchContainer(c);
    if (container == 0 || coop::GameWorldPtr() == 0 || !Ptr(coop::GameWorldPtr()->objectFactory)) return 0;
    ::GameData* state = container->newRecord((itemType)kItemStateType, "", "coopitem");
    if (!Obj(state)) return 0;
    state->stringFields[kBaseSidKey] = sid;
    Item* item = coop::GameWorldPtr()->objectFactory->createItem(state);
    if (!Obj(item)) return 0;
    float q = 0.0f, ch = 0.0f, back = 0.0f; int n = 1, fnc = 0; unsigned char un = 0;
    if (quality == quality && quality >= 0.0f && quality < 100000.0f && ReadItemDetailPod(item, &q, &ch, &n, &fnc, &un))
        WriteItemDetailPod(item, quality, ch, n, fnc, un, &back);
    return item;
}

static void LimbLog(unsigned int uid, unsigned int limb, int step, unsigned char here, unsigned char owner, const char* sid,
                    unsigned int fromPeer, const char* outcome)
{
    ++g_limbLogLines;
    if (g_limbLogLines > 20 && g_limbLogLines % 50 != 0) return;
    DebugLog("[LIMBS] uid=" + S((long long)uid) + " " + cooplimb::LimbName(limb) + " " + cooplimb::LimbStepName(step)
             + " here=" + S((long long)here) + " owner=" + S((long long)owner)
             + ((sid != 0 && sid[0] != 0) ? std::string(" item=") + sid : std::string())
             + " from peer " + S((long long)fromPeer) + " -> " + outcome + " (line " + S(g_limbLogLines) + ")");
}

const cooplimb::LimbsWire* LimbsOfOwned(unsigned int uid)
{
    static cooplimb::LimbsWire s_w;   /* MAIN THREAD only (StateTick / StatePush): the next send reads it */
    ::Character* c = FindSpawned(uid);
    if (c == 0) return 0;
    int st[cooplimb::kLimbCount]; void* it[cooplimb::kLimbCount];
    const int r = ReadLimbsPod(c, st, it);
    if (r < 0) { ++g_limbReadFault; return 0; }
    cooplimb::LimbsClear(&s_w);
    for (unsigned int i = 0; r == 1 && i < cooplimb::kLimbCount; ++i)
    {
        s_w.state[i] = LimbStateByte(st[i]);
        if (s_w.state[i] != cooplimb::kLimbReplaced) continue;
        LimbItemSid(it[i], s_w.sid[i]);   /* unreadable = an empty name: the copy leaves that limb alone (unnamed) */
        float q = 0.0f, ch = 0.0f; int n = 1, fnc = 0; unsigned char un = 0;
        if (Obj(it[i]) && ReadItemDetailPod(it[i], &q, &ch, &n, &fnc, &un)) s_w.quality[i] = q;
    }
    ++g_limbCarried;
    return &s_w;
}

/* OWNER SIDE: a limb lost or a robotic limb fitted / taken off on one of this game's own characters sends its STATE at once
   (as a change of prone, pose or stealth does), so a copy never waits for the round-robin refresh. Read every call for at most
   kLimbOwnPerCall owned characters, round-robin; the first sight of a character only records it. MAIN THREAD. */
static std::map<unsigned int, unsigned long long> s_limbOwnSig;   /* owned uid -> its four limb states and items, as last seen */
static size_t s_limbOwnCursor = 0;
long long g_limbOwnPushed = 0, g_limbOwnLogged = 0;
const size_t kLimbOwnPerCall = 64;
static unsigned long long LimbSignature(const int* st, void* const* it)
{
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned int i = 0; i < cooplimb::kLimbCount; ++i)
    {
        h = (h ^ (unsigned long long)(unsigned int)st[i]) * 1099511628211ULL;
        h = (h ^ (unsigned long long)(uintptr_t)it[i]) * 1099511628211ULL;
    }
    return h;
}
void LimbOwnWatchTick()
{
    std::vector<unsigned int> mine;
    net::OwnedUidsSnapshot(&mine);
    if (mine.empty()) { s_limbOwnSig.clear(); return; }
    if (s_limbOwnSig.size() > mine.size() * 2 + 64)   /* characters no longer owned: drop them */
    {
        std::set<unsigned int> keep(mine.begin(), mine.end());
        for (std::map<unsigned int, unsigned long long>::iterator k = s_limbOwnSig.begin(); k != s_limbOwnSig.end(); )
            if (keep.count(k->first) == 0) s_limbOwnSig.erase(k++); else ++k;
    }
    const size_t n = mine.size() < kLimbOwnPerCall ? mine.size() : kLimbOwnPerCall;
    for (size_t j = 0; j < n; ++j)
    {
        const unsigned int uid = mine[(s_limbOwnCursor + j) % mine.size()];
        ::Character* c = FindSpawned(uid);
        if (c == 0) continue;
        int st[cooplimb::kLimbCount]; void* it[cooplimb::kLimbCount];
        if (ReadLimbsPod(c, st, it) < 0) continue;
        const unsigned long long sig = LimbSignature(st, it);
        std::map<unsigned int, unsigned long long>::iterator k = s_limbOwnSig.find(uid);
        if (k == s_limbOwnSig.end()) { s_limbOwnSig[uid] = sig; continue; }
        if (k->second == sig) continue;
        k->second = sig;
        const bool sent = StatePush(uid);
        if (sent) ++g_limbOwnPushed;
        if (++g_limbOwnLogged <= 16)
            DebugLog("[LIMBS] owned uid=" + S((long long)uid) + " limbs changed - STATE " + (sent ? "pushed" : "NOT pushed (no road)")
                     + " (first 16 logged)");
    }
    s_limbOwnCursor = (s_limbOwnCursor + n) % mine.size();
}

/* The owner's latest limb block per copy uid, recorded by OnState and applied by LimbSafePointDrain. A newer STATE
   replaces an older one (STATE carries no sequence number; the latest to arrive wins). MAIN THREAD only. */
struct LimbWant { cooplimb::LimbsWire w; unsigned int fromPeer; };
static std::map<unsigned int, LimbWant> s_limbWant;
const size_t kLimbWantMax = 512;

/* Per copy limb (key uid << 2 | limb): the owner sid that made no item here, or made one that is not a robotic limb - not
   asked again until the owner's sid changes; and the owner quality a same-item refit was last tried for. MAIN THREAD. */
static std::map<unsigned long long, std::string> s_limbBadSid;
static std::map<unsigned long long, float> s_limbQualityTried;
/* Per copy limb: the owner state and sid whose step the engine took without changing the limb - not tried again until the
   owner's word for that limb changes. MAIN THREAD. */
static std::map<unsigned long long, std::string> s_limbNoEffectTried;
static std::string LimbWord(unsigned char state, const char* sid) { return std::string(1, (char)('0' + state)) + sid; }

/* The limbtest lever's one armed request (MAIN THREAD). */
struct LimbTest { bool armed; int kind; unsigned int uid; int limb; char sid[cooplimb::kLimbSidMax + 1]; };
static LimbTest s_limbTest = { false, 0, 0, 0, { 0 } };

void ApplyOwnerLimbs(unsigned int uid, int has, const cooplimb::LimbsWire& w, unsigned int fromPeer)
{
    if (has == 0) { ++g_limbAbsent; return; }
    if (net::IsUidMine(uid)) { ++g_limbRefusedMine; return; }
    std::map<unsigned int, LimbWant>::iterator at = s_limbWant.find(uid);
    if (at == s_limbWant.end())
    {
        if (s_limbWant.size() >= kLimbWantMax) { ++g_limbQueueFull; return; }   /* the next STATE asks again */
        at = s_limbWant.insert(std::make_pair(uid, LimbWant())).first;
    }
    at->second.w = w;
    at->second.fromPeer = fromPeer;
    ++g_limbQueued;
}

void LimbsForgetUid(unsigned int uid)
{
    if (s_limbWant.erase(uid) != 0) ++g_limbForgot;
    for (unsigned int i = 0; i < cooplimb::kLimbCount; ++i)
    {
        const unsigned long long key = ((unsigned long long)uid << 2) | i;
        s_limbBadSid.erase(key);
        s_limbQualityTried.erase(key);
        s_limbNoEffectTried.erase(key);
    }
}

void LimbsWorldTeardown()
{
    s_limbOwnSig.clear();
    s_limbOwnCursor = 0;
    g_limbForgot += (long long)s_limbWant.size();
    s_limbWant.clear();
    s_limbBadSid.clear();
    s_limbQualityTried.clear();
    s_limbNoEffectTried.clear();
    s_limbTest.armed = false;
}

/* The mod's own item destroy; the item sync neither books nor publishes it. */
static void LimbDestroyItem(void* item, const char* why)
{
    if (!Obj(item)) return;
    const long prev = ItemApplyingEnter();
    if (KitDestroyPod((Item*)item, why) != 1) ++g_limbItemDestroyFault;
    ItemApplyingLeave(prev);
}

/* true = the item's complete-object class is the engine's robotic limb item; `cls` gets the RTTI name ("" = unreadable). */
static bool LimbItemIsRobot(void* item, char* cls, int cap)
{
    cls[0] = 0;
    const unsigned int vt = PolicyVtableRvaPod(item);
    if (vt == 0) return false;
    if (PolicyClassNamePod(vt, cls, cap) == 0) return false;
    return cooplimb::LimbIsRobotItemClass(cls);
}

/* MAIN THREAD, K2 safe point (worker paused): the owner's limbs onto its copy, a limb at a time and only where they differ.
   Everything is looked up again here - the copy may have gone, become this game's own, or died since its STATE came. */
/* Character +0x348: is it carrying a body. 1 yes, 0 no, -1 unreadable. */
static int LimbCarryingPod(const void* c)
{
    __try { return *((const unsigned char*)c + 0x348) != 0 ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static void ApplyOwnerLimbsNow(unsigned int uid, const cooplimb::LimbsWire& w, unsigned int fromPeer)
{
    if (net::IsUidMine(uid)) { ++g_limbRefusedMine; return; }
    if (!net::RemoteMayWriteStill(uid, fromPeer)) { ++g_limbWriterChanged; return; }   /* another game drives it since the STATE */
    ::Character* c = FindSpawned(uid);
    if (c == 0) { ++g_limbNoCopy; return; }
    if (CharacterBuilt(c, 0) != 1) { ++g_limbNotBuilt; return; }   /* the next STATE asks again */
    if (CopyRebuildInFlightAny(c) != 0) { ++g_limbDeferredRebuild; return; }
    int st[cooplimb::kLimbCount]; void* it[cooplimb::kLimbCount];
    if (ReadLimbsPod(c, st, it) < 0) { ++g_limbReadFault; return; }
    for (unsigned int i = 0; i < cooplimb::kLimbCount; ++i)
        if (st[i] < (int)cooplimb::kLimbOriginal || st[i] > (int)cooplimb::kLimbCrushed) { ++g_limbImplausible; return; }
    const bool dead = StoreIsDeadPod(c) != 0;   /* 1 dead, -1 unreadable: either way no amputation */
    void* med = (char*)c + kLimbMedical;
    for (unsigned int i = 0; i < cooplimb::kLimbCount; ++i)
    {
        char here[cooplimb::kLimbSidMax + 1];
        here[0] = 0;
        const unsigned char cs = (unsigned char)st[i];
        float hereQ = -1.0f;
        if (cs == cooplimb::kLimbReplaced)
        {
            LimbItemSid(it[i], here);
            float q = 0.0f, ch = 0.0f; int n = 1, fnc = 0; unsigned char un = 0;
            if (Obj(it[i]) && ReadItemDetailPod(it[i], &q, &ch, &n, &fnc, &un)) hereQ = q;
        }
        const unsigned long long key = ((unsigned long long)uid << 2) | i;
        const int step = cooplimb::PlanLimbQ(cs, here, hereQ, w.state[i], w.sid[i], w.quality[i]);
        if (step == cooplimb::kLimbStepNone) { s_limbQualityTried.erase(key); s_limbNoEffectTried.erase(key); continue; }
        if (step == cooplimb::kLimbStepCannotRegrow) { ++g_limbCannotRegrow; continue; }
        if (step == cooplimb::kLimbStepNoCrushRoad) { ++g_limbNoCrushRoad; continue; }
        if (step == cooplimb::kLimbStepUnnamed) { ++g_limbUnnamed; continue; }
        const bool sameItem = step == cooplimb::kLimbStepRefit && here[0] != 0 && std::strcmp(here, w.sid[i]) == 0;
        if (sameItem)
        {
            std::map<unsigned long long, float>::iterator tried = s_limbQualityTried.find(key);
            if (tried != s_limbQualityTried.end() && tried->second == w.quality[i]) { ++g_limbFitKnownBad; continue; }
            s_limbQualityTried[key] = w.quality[i];   /* one try per owner quality: a quality that does not stick is not retried */
            ++g_limbQualityRefit;
        }
        std::map<unsigned long long, std::string>::iterator bad = s_limbBadSid.find(key);
        if ((step == cooplimb::kLimbStepFit || step == cooplimb::kLimbStepRefit) && bad != s_limbBadSid.end() && bad->second == w.sid[i])
        {
            ++g_limbFitKnownBad;
            continue;
        }
        std::map<unsigned long long, std::string>::iterator ne = s_limbNoEffectTried.find(key);
        if (ne != s_limbNoEffectTried.end() && ne->second == LimbWord(w.state[i], w.sid[i])) { ++g_limbNoEffectKept; continue; }
        /* setRobotLimbItem(left arm, none) makes the engine drop what the character carries (decomp_644c00), and the carry is
           driven by its own sync: the arm waits until the copy carries nothing; the next STATE asks again */
        if (i == 0 && (step == cooplimb::kLimbStepUnfit || step == cooplimb::kLimbStepRefit) && LimbCarryingPod(c) != 0)
        {
            ++g_limbDeferredCarry;
            if (sameItem) s_limbQualityTried.erase(key);
            continue;
        }
        const char* outcome = "done";
        bool clear = true;   /* nothing failed: the limb should now be the owner's */
        if (step == cooplimb::kLimbStepAmputate)
        {
            if (dead) { clear = false; ++g_limbDeadSkip; outcome = "left: the copy is dead (or its dead flag is unreadable)"; }
            else
            {
                const int rc = LimbAmputateCall(med, (int)i, false, true);
                if (rc == 1) { ++g_limbAmputated; outcome = "amputated"; }
                else { clear = false; if (rc == -2) { ++g_limbNoRow; outcome = "no MedicalSystem_amputate row"; } else { ++g_limbCallFault; outcome = "amputate faulted"; } }
            }
        }
        if (step == cooplimb::kLimbStepUnfit)
        {
            void* old = it[i];
            const int rc = LimbSetRobotCall(med, (int)i, 0);
            if (rc == 1)
            {
                ++g_limbUnfitted; outcome = "unfitted";
                LimbDestroyItem(old, "copy robotic limb taken off: its owner's limb changed (LIMBS)");
            }
            else { clear = false; if (rc == -2) { ++g_limbNoRow; outcome = "no MedicalSystem_setRobotLimbItem row"; } else { ++g_limbCallFault; outcome = "setRobotLimbItem (off) faulted"; } }
        }
        if (step == cooplimb::kLimbStepFit || step == cooplimb::kLimbStepRefit)
        {
            if (kLimbSetRobotRva == 0) { clear = false; ++g_limbNoRow; outcome = "no MedicalSystem_setRobotLimbItem row"; }
            else
            {
                /* the new item first: the fitted limb comes off only once its replacement exists and is a robotic limb */
                const long prev = ItemApplyingEnter();
                Item* item = LimbMakeItem(c, w.sid[i], w.quality[i]);
                ItemApplyingLeave(prev);
                char cls[96];
                cls[0] = 0;
                if (item == 0) { clear = false; ++g_limbFitUnresolved; s_limbBadSid[key] = w.sid[i]; outcome = "the item sid does not make an item here"; }
                else if (!LimbItemIsRobot(item, cls, (int)sizeof cls))
                {
                    clear = false; ++g_limbFitNotRobot; s_limbBadSid[key] = w.sid[i]; outcome = "the item sid is not a robotic limb here";
                    LimbDestroyItem(item, "copy limb item is not a robotic limb (LIMBS)");
                }
                else
                {
                    bool off = true;
                    if (step == cooplimb::kLimbStepRefit)
                    {
                        void* old = it[i];
                        const int rc = LimbSetRobotCall(med, (int)i, 0);
                        if (rc == 1)
                        {
                            ++g_limbUnfitted;
                            LimbDestroyItem(old, "copy robotic limb taken off: its owner's limb changed (LIMBS)");
                        }
                        else
                        {
                            off = false; clear = false; ++g_limbCallFault; outcome = "setRobotLimbItem (off) faulted";
                            LimbDestroyItem(item, "copy robotic limb not fitted (LIMBS)");
                        }
                    }
                    if (off)
                    {
                        const int rc = LimbSetRobotCall(med, (int)i, item);
                        if (rc == 1) { ++g_limbFitted; outcome = sameItem ? "refitted at the owner's quality" : "fitted"; }
                        else
                        {
                            clear = false; ++g_limbCallFault; outcome = "setRobotLimbItem (on) faulted";
                            /* the engine may already hold the item in the limb slot (setLimb stores it before the part is
                               attached): destroyed only when the slot does not hold it */
                            int st2[cooplimb::kLimbCount]; void* it2[cooplimb::kLimbCount];
                            if (ReadLimbsPod(c, st2, it2) >= 0 && it2[i] != (void*)item) LimbDestroyItem(item, "copy robotic limb not fitted (LIMBS)");
                            else { ++g_limbFitFaultKept; outcome = "setRobotLimbItem (on) faulted - the slot holds the item, left in place"; }
                        }
                    }
                }
            }
        }
        if (clear)
        {
            s_limbBadSid.erase(key);
            int after[cooplimb::kLimbCount]; void* afterItems[cooplimb::kLimbCount];
            if (ReadLimbsPod(c, after, afterItems) < 0 || LimbStateByte(after[i]) != w.state[i])
            {
                ++g_limbNoEffect;
                s_limbNoEffectTried[key] = LimbWord(w.state[i], w.sid[i]);
                outcome = "called, but the limb did not change - not tried again until the owner's limb changes";
            }
        }
        LimbLog(uid, i, step, cs, w.state[i], w.sid[i], fromPeer, outcome);
    }
}

static std::string LimbF(float v)
{
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << v;
    return o.str();
}

/* "leftArm=0 rightArm=2(95748-Newwworld.mod q=0.5) ..." - the four limb states and each fitted item's sid and quality. */
static std::string LimbStateText(::Character* c)
{
    int st[cooplimb::kLimbCount]; void* it[cooplimb::kLimbCount];
    const int r = ReadLimbsPod(c, st, it);
    if (r < 0) return "unreadable";
    std::string out = (r == 0) ? std::string("noRobotLimbs") : std::string();
    for (unsigned int i = 0; i < cooplimb::kLimbCount; ++i)
    {
        if (!out.empty()) out += " ";
        out += std::string(cooplimb::LimbName(i)) + "=" + S((long long)st[i]);
        if (st[i] != (int)cooplimb::kLimbReplaced) continue;
        char sid[cooplimb::kLimbSidMax + 1];
        LimbItemSid(it[i], sid);
        float q = 0.0f, ch = 0.0f; int n = 1, fnc = 0; unsigned char un = 0;
        const bool haveQ = Obj(it[i]) && ReadItemDetailPod(it[i], &q, &ch, &n, &fnc, &un);
        out += "(" + std::string(sid[0] != 0 ? sid : "?") + " q=" + (haveQ ? LimbF(q) : std::string("?")) + ")";
    }
    return out;
}

/* The limbtest lever's armed request, at the K2 safe point (worker paused). */
static void LimbTestDrain()
{
    if (!s_limbTest.armed) return;
    const LimbTest t = s_limbTest;
    s_limbTest.armed = false;
    ++g_limbTestRuns;
    const std::string head = std::string("[LIMB] limbtest ") + (t.kind == cooplimb::kLimbTestCut ? "cut" : "fit")
                             + " uid=" + S((long long)t.uid) + " " + cooplimb::LimbName((unsigned int)t.limb)
                             + (t.kind == cooplimb::kLimbTestFit ? std::string(" item=") + t.sid : std::string());
    ::Character* c = 0;
    const char* why = 0;
    int st[cooplimb::kLimbCount]; void* it[cooplimb::kLimbCount];
    if (EngineWritesBlocked()) why = "a world is loading or tearing down";
    else if (!net::IsUidMine(t.uid)) why = "not this game's own character";
    else if ((c = FindSpawned(t.uid)) == 0) why = "no character for that uid";
    else if (CharacterBuilt(c, 0) != 1) why = "the character is not built yet";
    else if (StoreIsDeadPod(c) != 0) why = "the character is dead (or its dead flag is unreadable)";
    else if (ReadLimbsPod(c, st, it) < 0) why = "its limbs are unreadable";
    else if (t.kind == cooplimb::kLimbTestCut && (st[t.limb] == (int)cooplimb::kLimbStump || st[t.limb] == (int)cooplimb::kLimbReplaced))
        why = "that limb is already a stump or has a robotic limb fitted";
    else if (t.kind == cooplimb::kLimbTestFit && st[t.limb] != (int)cooplimb::kLimbStump) why = "that limb is not a stump";
    else if (t.kind == cooplimb::kLimbTestFit && kLimbSetRobotRva == 0) why = "no MedicalSystem_setRobotLimbItem row";
    if (why != 0)
    {
        DebugLog(head + " refused: " + why);
        return;
    }
    const std::string before = LimbStateText(c);
    void* med = (char*)c + kLimbMedical;
    std::string outcome;
    if (t.kind == cooplimb::kLimbTestCut)
    {
        /* as combat does: the severed limb item is made, and the item sync sees it as it sees any combat loss */
        const int rc = LimbAmputateCall(med, t.limb, true, false);
        outcome = (rc == 1) ? "amputate called" : (rc == -2 ? "no MedicalSystem_amputate row" : "amputate faulted");
    }
    else
    {
        const long prev = ItemApplyingEnter();
        Item* item = LimbMakeItem(c, t.sid, -1.0f);   /* the item's own default quality */
        ItemApplyingLeave(prev);
        char cls[96];
        cls[0] = 0;
        if (item == 0) outcome = "the sid makes no item here";
        else if (!LimbItemIsRobot(item, cls, (int)sizeof cls))
        {
            outcome = std::string("the item is not a robotic limb (class ") + (cls[0] != 0 ? cls : "unreadable") + ") - destroyed";
            LimbDestroyItem(item, "limbtest item is not a robotic limb");
        }
        else
        {
            const int rc = LimbSetRobotCall(med, t.limb, item);
            if (rc == 1) outcome = std::string("setRobotLimbItem called (class ") + cls + ")";
            else
            {
                int st2[cooplimb::kLimbCount]; void* it2[cooplimb::kLimbCount];
                if (ReadLimbsPod(c, st2, it2) >= 0 && it2[t.limb] != (void*)item)
                {
                    outcome = "setRobotLimbItem faulted - item destroyed";
                    LimbDestroyItem(item, "limbtest robotic limb not fitted");
                }
                else outcome = "setRobotLimbItem faulted - the slot holds the item, left in place";
            }
        }
    }
    DebugLog(head + " before=[" + before + "] after=[" + LimbStateText(c) + "] -> " + outcome);
}

void LimbSafePointDrain()
{
    LimbTestDrain();
    if (s_limbWant.empty()) return;
    if (EngineWritesBlocked())
    {
        g_limbDrainBlocked += (long long)s_limbWant.size();   /* dropped: the next STATE records the block again */
        s_limbWant.clear();
        return;
    }
    std::map<unsigned int, LimbWant> q;
    q.swap(s_limbWant);
    for (std::map<unsigned int, LimbWant>::const_iterator i = q.begin(); i != q.end(); ++i)
        ApplyOwnerLimbsNow(i->first, i->second.w, i->second.fromPeer);
}

std::string LimbTestLever(const std::string& args)
{
    unsigned int uid = 0;
    int limb = -1;
    std::string sid;
    const int kind = cooplimb::LimbTestParse(args, &uid, &limb, &sid);
    if (kind == cooplimb::kLimbTestBad)
    {
        DebugLog("[cmd] usage: limbtest cut <ownUid> <limb 0-3> | limbtest fit <ownUid> <limb 0-3> <robotLimbSid> | limbtest show <uid>"
                 " (limb 0 left arm, 1 right arm, 2 left leg, 3 right leg)");
        return "error limbtest usage";
    }
    if (kind == cooplimb::kLimbTestShow)
    {
        ::Character* c = FindSpawned(uid);
        if (c == 0)
        {
            DebugLog("[LIMB] limbtest show uid=" + S((long long)uid) + " refused: no character for that uid");
            return "error limbtest show: no character for that uid";
        }
        const std::string text = LimbStateText(c);
        DebugLog("[LIMB] limbtest show uid=" + S((long long)uid) + (net::IsUidMine(uid) ? " own" : " copy") + " [" + text + "]");
        return "ok limbtest show " + text;
    }
    const char* what = (kind == cooplimb::kLimbTestCut) ? "cut" : "fit";
    if (!net::IsUidMine(uid))
    {
        DebugLog(std::string("[LIMB] limbtest ") + what + " uid=" + S((long long)uid) + " refused: not this game's own character");
        return "error limbtest: not this game's own character";
    }
    if (s_limbTest.armed)
    {
        DebugLog(std::string("[LIMB] limbtest ") + what + " uid=" + S((long long)uid) + " refused: one request is already waiting for the safe point");
        return "error limbtest: one request is already waiting";
    }
    s_limbTest.kind = kind;
    s_limbTest.uid = uid;
    s_limbTest.limb = limb;
    cooplimb::LimbsWire scratch;
    cooplimb::LimbsClear(&scratch);
    cooplimb::LimbSetSid(&scratch, 0, sid.c_str());
    std::memcpy(s_limbTest.sid, scratch.sid[0], sizeof s_limbTest.sid);
    s_limbTest.armed = true;
    DebugLog(std::string("[LIMB] limbtest ") + what + " uid=" + S((long long)uid) + " " + cooplimb::LimbName((unsigned int)limb)
             + (kind == cooplimb::kLimbTestFit ? " item=" + sid : std::string()) + " armed for the K2 safe point");
    return std::string("ok limbtest ") + what + " armed";
}

/* DETOUR-THREAD RULES: amputate runs inside applyDamage, which can run on an AI worker - no allocation, no lock, no log;
   interlocked counters only, printed on the [LIMBS] REPORT line. */
void detour_limbAmputate(void* med, int limb, bool createSeveredItem, const float* force)
{
    const void* who = (med != 0) ? *(void* const*)((const char*)med + kLimbMedOwner) : 0;
    const unsigned int uid = (who != 0) ? FindSpawnedUid(who) : 0;
    if (uid != 0 && !net::IsUidMineAnyThread(uid))
    {
        if (g_limbApplyThread == 0 || g_limbApplyThread != (LONG)::GetCurrentThreadId())
        {
            InterlockedIncrement64(&g_copyAmputateRefused);   /* the owner's STATE brings the stump if the owner lost the limb */
            return;
        }
        InterlockedIncrement64(&g_copyAmputateApplying);
    }
    else InterlockedIncrement64(&g_copyAmputatePassed);
    orig_limbAmputate(med, limb, createSeveredItem, force);
}

void InstallCopyAmputateGate()
{
    const unsigned long long fn = AddrAbs(kLimbAmputateRva);
    if (kLimbAmputateRva == 0 || fn == 0)
    {
        ErrorLog("[LIMBS] MedicalSystem_amputate is not in this address table - the copy amputation gate is NOT installed");
        return;
    }
    const HookStatus st = AddHook((void*)(uintptr_t)fn, (void*)&detour_limbAmputate, (void**)&orig_limbAmputate);
    DebugLog(std::string("[LIMBS] copy amputation gate: MedicalSystem::amputate AddHook ") + (st == SUCCESS ? "SUCCESS" : "FAILED")
             + " (a copy loses a limb only when its owner's STATE says so)");
}

static void ReportLimbCounters()
{
    DebugLog("[LIMBS] REPORT carried=" + S(g_limbCarried) + " readFault=" + S(g_limbReadFault)
             + " recv[absent,refusedMine,noCopy,deferredRebuild]=" + S(g_limbAbsent) + "," + S(g_limbRefusedMine) + ","
             + S(g_limbNoCopy) + "," + S(g_limbDeferredRebuild)
             + " deferred[carrying,writerChanged]=" + S(g_limbDeferredCarry) + "," + S(g_limbWriterChanged) + " fitFaultKept=" + S(g_limbFitFaultKept)
             + " ownPushed=" + S(g_limbOwnPushed)
             + " applied[amputated,unfitted,fitted]=" + S(g_limbAmputated) + "," + S(g_limbUnfitted) + "," + S(g_limbFitted)
             + " failed[fitUnresolved,fitKnownBad,callFault,noRow,noEffect,itemDestroyFault]=" + S(g_limbFitUnresolved) + ","
             + S(g_limbFitKnownBad) + "," + S(g_limbCallFault) + "," + S(g_limbNoRow) + "," + S(g_limbNoEffect) + "," + S(g_limbItemDestroyFault)
             + " leftAlonePerState[cannotRegrow,noCrushRoad,unnamed]=" + S(g_limbCannotRegrow) + "," + S(g_limbNoCrushRoad) + "," + S(g_limbUnnamed)
             + " safePoint[queued,queueFull,blockedDropped,notBuilt,deadSkip,implausible,forgot]=" + S(g_limbQueued) + "," + S(g_limbQueueFull) + ","
             + S(g_limbDrainBlocked) + "," + S(g_limbNotBuilt) + "," + S(g_limbDeadSkip) + "," + S(g_limbImplausible) + "," + S(g_limbForgot)
             + " fitNotRobot=" + S(g_limbFitNotRobot) + " noEffectKept=" + S(g_limbNoEffectKept) + " qualityRefit=" + S(g_limbQualityRefit) + " limbtestRuns=" + S(g_limbTestRuns)
             + " gate[refused,applying,passed]=" + S((long long)g_copyAmputateRefused) + "," + S((long long)g_copyAmputateApplying) + ","
             + S((long long)g_copyAmputatePassed));
}

void ReportClothingCounters()
{
    DebugLog("[P022] REPORT captured=" + S(g_c.captured)
             + " itemsCaptured=" + S(g_c.itemsCaptured)
             + " applied=" + S(g_c.applied)
             + " itemsCreated=" + S(g_c.itemsCreated)
             + " itemsUnresolved=" + S(g_c.itemsUnresolved)
             + " itemsAddFailed=" + S(g_c.itemsAddFailed)
             + " addPlacedBySection=" + S(g_c.addPlacedBySection)   /* copy1 */
             + " addFailedDestroyed=" + S(g_c.addFailedDestroyed)
             + " addSectionFault=" + S(g_c.addSectionFault)
             + " addEnabledForAdd=" + S(g_c.addEnabledForAdd) + " addDestroyRefused=" + S(g_c.addDestroyRefused)   /* copy2 */
             + " addDestroyFault=" + S(g_c.addDestroyFault) + " addEnabledRestoreFault=" + S(g_c.addEnabledRestoreFault)
             + " addPlacedEngineRoute=" + S(g_c.addPlacedEngineRoute) + " addEngineRouteNoCell=" + S(g_c.addEngineRouteNoCell)   /* copy3 */
             + " noContainer=" + S(g_c.noContainer)
             + " details[read,readFault,written,writeFault,absent,bad]=" + S(g_c.detailsRead) + "," + S(g_c.detailsReadFault) + ","
             + S(g_c.detailsWritten) + "," + S(g_c.detailsWriteFault) + "," + S(g_c.detailsAbsent) + "," + S(g_c.detailsBad)
             + " bagRows[captured,applied,relocated,lost]=" + S(g_bagRowsCaptured) + "," + S(g_bagRowsApplied) + ","   /* bag23 part 1 */
             + S(g_bagRowsRelocated) + "," + S(g_bagRowsLost)
             + " bagPacksRefused=" + S(g_bagPacksRefused) + " bagKitBad=" + S(g_bagKitBad) + " bagKitEncodeRefused=" + S(g_bagKitEncodeRefused)
             + " kit[exact,relocated,lost]=" + S(g_c.kitPlacedExact) + "," + S(g_c.kitRelocated) + "," + S(g_c.kitLost)   /* kit2 */
             + " kitCells[absent,bad,encodeRefused]=" + S(g_kitCellsAbsent) + "," + S(g_kitCellsBad) + "," + S(g_kitCellsEncodeRefused));
    ReportLimbCounters();   /* LIMBS: its own [LIMBS] REPORT line */
}

} // namespace coop
