// appearance.cpp - PROBE P019 (read) and H010a (replicate). See appearance.h for why.
//
// The path:
//   Character         +0x448 -> AnimationClass*
//   AnimationClass    +0x0E8 -> AppearanceBase*
//   AppearanceBase    +0x100 bodyFilename   +0x140 female    +0x141 updatedAttachments
//                     +0x142 updatedAppearanceData          +0x150 raceData
//                     +0x160 hairStyle      +0x168 shaved    +0x180 barefoot
//                     +0x184 characterHeight +0x188 heightSpeedMult +0x18C height_0to1
//   AppearanceHuman   +0x1A0 bulkMult +0x1A4 muscleMult +0x1A8 skinnyMult +0x1B0 beard
//
// Confirmed reading correctly in T051 (F156): `female` (+0x140) and `body` (+0x100) come
// from unrelated offsets and agreed on 8/8 lines - female=1 always beside human_female.mesh.
// That cross-check is what makes this a measurement rather than a plausible reading.
//
// AppearanceHuman's fields start past the base class's 0x190 bytes, so they are touched ONLY
// after the object is confirmed to BE an AppearanceHuman.
//
// F159 - HOW THAT TEST IS DONE, and why the obvious way was wrong. The first version compared
// vtable slot +0x40 (the header's stated offset for `isFlayed`) against the resolved address
// of AppearanceHuman::isFlayed. It read `human=0` on eight confirmed Greenlanders: the
// header's VTABLE OFFSETS are shifted just like its RVAs (F020). So the slot is now FOUND BY
// SCANNING the vtable for the resolved address, and the index that worked is logged once.
// That measures the layout instead of assuming it.
// T-293 (2026-09-30): the scan never matched either - human=0 on every character and genderSlot=-1 in every REPORT of
// T625/T628 (1.0.65) and T633/T634 (1.0.68). The vtable WAS AppearanceHuman's and setGender WAS in it, at slot 5 (+0x28,
// the header's own offset): the game is incrementally linked, so every slot holds a 5-byte jump stub (E9 rel32; the
// [P020] dump shows slot RVAs 0xA051-0x50911) and never a function's own address. The test is now the vtable pointer
// against the address-table row AppearanceHumanVt - see IsHumanAppearance.
//
// Everything the apply path calls is reached through an address-table row, so no vtable offset and no RVA is
// written down anywhere in this file.

#include "appearance.h"
#include "appearance_record.h"
#include "clothing.h"
#include "spawn.h"
#include "effect.h"   /* T-327: NearestOwnedAnimal */
#include "medical.h"   /* crash1b: CopyOwnerSaysKo */
#include "addresses.h" /* crash2: AddrReg (the createBody guard's row) */
#include "store.h"     /* crash2: StoreMainThreadId (the createBody guard skips on the main thread only) */
#include "../common/copybody.h"   /* crash2: the pure skip rule */
#include "../common/humantest.h"  /* T-293: the human class rule */
#include "../common/kolook.h"     /* a copy met while its owner says knocked out is dressed before it goes down */
#include "playerfaction.h"          /* IsStandInFaction: a copy of another player's character (lookKoArrival split) */
#include "net/session.h"

#include "coop_log.h"
#include <stdint.h>   /* mig3: what <core/Functions.h> also brought in */
/* These hook targets are our own address-table rows (coop::AddrAbs gives image base + RVA, or 0 when the
   row is unbound, which each call site below guards). */
static unsigned long long kMig3HumanIsFlayed = 0; static coop::AddrReg kMig3HumanIsFlayed_reg("AppearanceHuman_skinRemoved", &kMig3HumanIsFlayed);   /* Steam_1.0.65 0x544620 */
static unsigned long long kMig3HumanSetGender = 0; static coop::AddrReg kMig3HumanSetGender_reg("AppearanceHuman_chooseSex", &kMig3HumanSetGender);   /* Steam_1.0.65 0x52D530 */
static unsigned long long kMig3HumanUpdateAppearance = 0; static coop::AddrReg kMig3HumanUpdateAppearance_reg("AppearanceHuman_rebuildLook", &kMig3HumanUpdateAppearance);   /* Steam_1.0.65 0x532E60 */
static unsigned long long kMig3HumanUpdateProportions = 0; static coop::AddrReg kMig3HumanUpdateProportions_reg("AppearanceHuman_rebuildShape", &kMig3HumanUpdateProportions);   /* Steam_1.0.65 0x52DC30 */
static unsigned long long kHumanVtRva = 0; static coop::AddrReg kHumanVtRva_reg("AppearanceHumanVt", &kHumanVtRva);   /* T-293: Steam_1.0.65 0x16E5208 (RTTI .?AVAppearanceHuman@@) */
#include "hooks.h"   /* mig1: coop::AddHook (own MinHook) */
#include "game/Character.h"
#include "game/GameWorld.h"
#include "game/GameData.h"
#include "game/GameDataManager.h"
#include "game/Appearance.h"
#include "game/AnimationClass.h"
#include "game/Inventory.h"
#include "game/Item.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <map>
#include <vector>
#include <algorithm>
#include <sstream>
#include <locale>
#include <cstring>

namespace coop {

void InvNetNoteAnnounced(void* ch, unsigned int uid);   /* inv2a (items.cpp, declared in items.h) */
int KitApplyBusy(void* ch, unsigned int uid);            /* T-1 B2 fold (review F2; items.cpp, declared in items.h) */

namespace {

const size_t kCharAnimationOff  = 0x448;  // Character      -> AnimationClass*
const size_t kAnimAppearanceOff = 0x0E8;  // AnimationClass -> AppearanceBase*

// T240 - the two offsets the WOUND path reads, and they are Read, not Confirmed: they come
// from the decompile of MedicalSystem::addWound (kenshi_x64 RVA 0x64FE40,
// build/decomp_64fe40.txt lines 416-423), not from a header and not from a live measurement.
const size_t kAnimWoundGateOff  = 0x088;  // AnimationClass -> byte; 0 = addWound reads no node
const size_t kAppearanceEntOff  = 0x0D8;  // AppearanceBase -> Ogre::MovableObject* (the body)

// R1-a - the two back-references CharacterBuilt compares. Read from decompiles 0x5B8910 and
// 0x538590 (build/read-r1.md Q4), not Confirmed live.
const size_t kAnimEntityOff      = 0x0A8;  // AnimationClass -> the entity it animates (lookAt reads it)
const size_t kAppearanceAnimOff  = 0x138;  // AppearanceBase -> its AnimationClass (back-pointer)

// R1-a-b (review-r1a M1) - what setProneState (0x5C7390) and its two callees dereference, for
// CharacterProneSafe. Read, not Confirmed live: build/decomp_5c7390.txt lines 16-31 read
// Character +0x650 (param_1[0xca], `AI* ai`) unchecked; line 39 null-checks +0x638
// (param_1[199], `CharacterNameTag*`) before decomp_6e8ba0.txt; line 42 reads *(+0x640) +0x3B0
// (param_1[200], `CharMovement*`) unchecked, and decomp_660fe0.txt - reached only when that is
// non-zero - calls through *(+0x640 +0x3A8) unchecked. The +0x448 -> +0xE8 hop is read-r1 Q2.
// None of them reads the body (+0x648) or its scene node.
const size_t kCharNameTagOff     = 0x638;  // Character -> CharacterNameTag* (null-checked by the engine)
const size_t kCharMovementOff    = 0x640;  // Character -> CharMovement*
const size_t kCharAiOff          = 0x650;  // Character -> AI*
const size_t kMovementFlagOff    = 0x3B0;  // CharMovement -> non-zero: 0x660FE0 runs
const size_t kMovementObjOff     = 0x3A8;  // CharMovement -> the object 0x660FE0 calls through

const size_t kBodyFilenameOff = 0x100;
const size_t kFemaleOff       = 0x140;
const size_t kUpdAttachOff    = 0x141;
const size_t kUpdAppOff       = 0x142;
const size_t kRaceOff         = 0x150;
const size_t kHairOff         = 0x160;
const size_t kShavedOff       = 0x168;
const size_t kBarefootOff     = 0x180;
const size_t kHeightOff       = 0x184;
const size_t kHeight01Off     = 0x18C;
const size_t kIsCreatingOff   = 0x0E8;    // isCreatingBody - NOT a settle flag (F157)

// P021 - gear. Character -> Inventory -> the flat item list.
const size_t kInventoryOff = 0x2E8;       // Character  -> Inventory*
const size_t kAllItemsOff  = 0x010;       // Inventory  -> lektor<Item*> _allItems
const size_t kSectionsOff  = 0x028;       // Inventory  -> unordered_map<string, InventorySection*>

const size_t kBulkOff   = 0x1A0;          // AppearanceHuman only
const size_t kMuscleOff = 0x1A4;
const size_t kSkinnyOff = 0x1A8;
const size_t kBeardOff  = 0x1B0;

// ---- counters (F155: a zero must be explainable, so it must first be counted) ----------
struct Counters
{
    int watched;      // local uids registered for capture
    int sent;         // rolls sent to the peer
    int queued;       // rolls received from the peer
    int applied;      // rolls applied locally
    int applyFailed;  // rolls we could not apply
    int captureFailed;// owned uids whose record could not be read
    // F172: `notHuman` USED TO LIVE HERE and it was a DEAD COUNTER - declared, initialised,
    // printed in every REPORT line, and incremented by nothing after the apply path moved to
    // appearance_record.cpp. It read 0 for four runs and 0 reads as "nothing went wrong".
    // Removed rather than re-wired: the thing it counted is now measured directly by
    // `genderSlot`, and a counter with one honest source beats two with a dead one.
    Counters() : watched(0), sent(0), queued(0), applied(0), applyFailed(0),
                 captureFailed(0) {}
};
Counters g_c;

std::map<unsigned int, RecordCopy> g_pendingRemote;   // uid -> record waiting to apply
std::map<unsigned int, GarmentSet> g_pendingGarments; // uid -> worn items waiting to apply
std::map<unsigned int, bool>       g_appearanceDone;  // uids whose record has been applied
std::map<unsigned int, bool>           g_pendingLocal;    // uid -> still waiting to settle

// R1-a / R1-a-b (review-r1a LOW-3): remote APPEARANCE / CLOTHING entries that ENTERED a wait
// because the character had settled but CharacterBuilt said no - once per uid per wait, not once
// per frame. Cumulative; printed on the [M1] REPORT line.
long long g_appearanceWaitNotBuilt = 0;
// R1-a-b (review-r1a M3): remote CLOTHING entries that entered a wait because the same uid still
// had a remote APPEARANCE entry pending - once per uid per wait. Cumulative; [M1] REPORT line.
long long g_clothingWaitAppearance = 0;
// uid -> in that wait now. A uid leaves when it gets past the test, so a later wait counts again.
std::map<unsigned int, bool> g_waitNotBuiltAppearance;
std::map<unsigned int, bool> g_waitNotBuiltClothing;
std::map<unsigned int, bool> g_waitClothingForAppearance;

// crash1 (T293 / F912): remote APPEARANCE / CLOTHING entries that entered a wait because the copy was lying down or
// limp - once per uid per wait. Cumulative; the [P019] REPORT line, beside the uids waiting now.
long long g_appearanceWaitDowned = 0;
long long g_clothingWaitDowned = 0;
std::map<unsigned int, bool> g_waitDownedAppearance;
std::map<unsigned int, bool> g_waitDownedClothing;

static void NoteWaitEntered(std::map<unsigned int, bool>& waiting, unsigned int uid, long long* counter)
{
    if (waiting.insert(std::make_pair(uid, true)).second) ++*counter;
}

// A COPY MET WHILE ITS OWNER SAYS KNOCKED OUT (src/common/kolook.h): its first look - and its worn items after it - go
// through while it still stands and its knockdown is held for them; the hold then keeps the knockdown back while the rebuild
// runs. g_copyLookApplied = uids of copies that have taken a look (forgotten when the copy is removed and at world teardown);
// g_lookKoThrough = copies whose first look went through that way and whose worn items have not been applied yet (value
// true; false once its worn items were counted clothLate).
// lookKoArrival: [applied] first looks applied that way, [gaveUp] holds that gave up with the first look still pending
// (the knockdown then lands and the look waits for the copy to stand), [clothLate] copies whose first look went through
// that way but whose worn items, ready, found the hold gone or the copy down (they wait for it to stand; once per copy);
// the Player pair counts copies of a player's characters (the copy's faction is a player's stand-in).
std::map<unsigned int, bool> g_copyLookApplied;
std::map<unsigned int, bool> g_lookKoThrough;
long long g_lookKoApplied = 0, g_lookKoGaveUp = 0, g_lookKoAppliedPlayer = 0, g_lookKoGaveUpPlayer = 0;
long long g_lookKoClothLate = 0;

// 1 = the copy's faction is a player's stand-in (a copy of a player's character), 0 = not, -1 = no copy or a read faulted.
// No C++ object with a destructor in this frame (C2712).
static int CopyPlayerSidePod(::Character* c)
{
    if (c == 0) return -1;
    ::Faction* f = 0;
    __try { f = c->getOwnerFactionDirect(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    return (f != 0 && coop::IsStandInFaction(f)) ? 1 : 0;
}

// Local guards. spawn.cpp's live in its anonymous namespace by design (they are not an
// interface), so this file carries its own copies of the same two rules (F051).
bool Ptr(const void* p)
{
    uintptr_t v = (uintptr_t)p;
    if (v < 0x10000 || v >= 0x00007FFFFFFFFFFFull) return false;
    if (v & 0x7) return false;
    __try { volatile uintptr_t probe = *(uintptr_t*)v; (void)probe; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

bool Obj(const void* p)
{
    if (!Ptr(p)) return false;
    uintptr_t vtable = *(uintptr_t*)p;
    uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    return vtable > base && vtable < base + 0x4000000;
}

// C1-b (review 2026-09-22 H1): `Obj` asks for a vtable inside the GAME image, and the body at
// appearance +0xD8 is an Ogre::Entity whose vtable lives in OgreMain_x64.dll - so `Obj` called
// every body "not an object" and every gated hit read kHitNoEntity. The same guard with the
// module named: range and alignment (`Ptr`), then a vtable inside [base, base + SizeOfImage) of
// that module. Base and size come from the PE optional header and are cached once; a module
// that is not loaded answers false.
const char kOgreModule[] = "OgreMain_x64.dll";

static bool ObjInModule(const void* p, const char* module)
{
    static char      s_module[64] = { 0 };
    static uintptr_t s_base = 0;
    static uintptr_t s_size = 0;
    if (module == 0 || !Ptr(p)) return false;
    if (s_base == 0 || std::strcmp(s_module, module) != 0)
    {
        HMODULE h = ::GetModuleHandleA(module);
        if (h == 0) return false;
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)h;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        const IMAGE_NT_HEADERS64* nt =
            (const IMAGE_NT_HEADERS64*)((const char*)h + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
        std::strncpy(s_module, module, sizeof(s_module) - 1);
        s_module[sizeof(s_module) - 1] = 0;
        s_base = (uintptr_t)h;
        s_size = (uintptr_t)nt->OptionalHeader.SizeOfImage;
    }
    uintptr_t vtable = *(uintptr_t*)p;
    return vtable >= s_base && vtable < s_base + s_size;
}

// C1-b (review 2026-09-22 L4): "attached" is asked of Ogre itself - the exported
// Ogre::MovableObject::getParentSceneNode, the SAME call addWound makes - not of the inline
// header's `mParentNode` field, whose layout would be ours to get wrong. x64 thiscall passes
// `this` in rcx, so it is called as a plain one-argument function.
typedef void* (*GetParentSceneNodeFn)(const void*);
const char kGetParentSceneNodeSym[] =
    "?getParentSceneNode@MovableObject@Ogre@@QEBAPEAVSceneNode@2@XZ";

// SEH lives here, with NO C++ objects (C2712). A fault answers 0 = no node.
int CallGetParentSceneNode(GetParentSceneNodeFn fn, const void* ent, void** nodeOut)
{
    __try { *nodeOut = fn(ent); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *nodeOut = 0; return 0; }
}

std::string S(long long v)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << v;
    return ss.str();
}

std::string Hex(unsigned long long v)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "0x" << std::hex << std::uppercase << v;
    return ss.str();
}

std::string F2(float v)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss.setf(std::ios::fixed);
    ss.precision(2);
    ss << v;
    return ss.str();
}

// Resolved ONCE. On failure it logs once why, and the caller treats every gated body as
// detached (kHitDetached) - a play is refused rather than risked.
GetParentSceneNodeFn GetParentSceneNode()
{
    static bool s_tried = false;
    static GetParentSceneNodeFn s_fn = 0;
    if (s_tried) return s_fn;
    s_tried = true;
    HMODULE h = ::GetModuleHandleA(kOgreModule);
    if (h != 0) s_fn = (GetParentSceneNodeFn)::GetProcAddress(h, kGetParentSceneNodeSym);
    if (s_fn == 0)
        DebugLog(std::string("[M3] C1-b: Ogre::MovableObject::getParentSceneNode did NOT resolve (")
                 + (h == 0 ? "OgreMain_x64.dll is not loaded" : "the export is not in OgreMain_x64.dll")
                 + ") - every gated body now counts as skippedDetached and no such hit is played");
    else
        DebugLog("[M3] C1-b: resolved Ogre::MovableObject::getParentSceneNode = "
                 + Hex((unsigned long long)(uintptr_t)s_fn));
    return s_fn;
}

// Reach the appearance object, or 0. Both hops are validated.
void* GetAppearance(::Character* c)
{
    if (c == 0 || !Obj(c)) return 0;
    void* anim = *(void**)((char*)c + kCharAnimationOff);
    if (!Obj(anim)) return 0;
    void* app = *(void**)((char*)anim + kAnimAppearanceOff);
    if (!Obj(app)) return 0;
    return app;
}

// Name of a GameData record, or "-" for null / "?" for unreadable. A static-data record IS
// the identity of a rolled choice: two instances that picked different hair carry different
// names here, which is the entire point of this probe.
std::string DataName(void* base, size_t off)
{
    ::GameData* gd = *(::GameData**)((char*)base + off);
    if (gd == 0) return "-";
    if (!Obj(gd)) return "?";
    return gd->name;
}

// ---- resolved engine entry points ------------------------------------------------------
// SEH lives in helpers with NO C++ objects: MSVC refuses __try in a function that requires
// object unwinding (C2712).

uintptr_t ResolveHumanIsFlayed()
{
    __try { return (uintptr_t)coop::AddrAbs(kMig3HumanIsFlayed); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
uintptr_t ResolveHumanSetGender()
{
    __try { return (uintptr_t)coop::AddrAbs(kMig3HumanSetGender); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
uintptr_t ResolveHumanUpdateAppearance()
{
    __try { return (uintptr_t)coop::AddrAbs(kMig3HumanUpdateAppearance); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
uintptr_t ResolveHumanUpdateProportions()
{
    __try { return (uintptr_t)coop::AddrAbs(kMig3HumanUpdateProportions); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

uintptr_t g_isFlayed = 0, g_setGender = 0, g_updateApp = 0, g_updateProp = 0;
// T-293: the human test's answers, per CALL (the loops ask every tick): vtable == AppearanceHumanVt / another class /
// no row or an unreadable vtable. In the REPORT line as humanTest[yes,no,unreadable].
volatile LONG g_humanYes = 0, g_humanNo = 0, g_humanUnreadable = 0;
volatile LONG g_humanFirstNoted = 0, g_humanNoRowNoted = 0;
bool g_resolved = false;

void ResolveOnce()
{
    if (g_resolved) return;
    g_resolved = true;
    g_isFlayed   = ResolveHumanIsFlayed();
    g_setGender  = ResolveHumanSetGender();
    g_updateApp  = ResolveHumanUpdateAppearance();
    g_updateProp = ResolveHumanUpdateProportions();
    DebugLog("[P019] resolved AppearanceHuman: isFlayed=" + Hex(g_isFlayed)
             + " setGender=" + Hex(g_setGender)
             + " updateAppearance=" + Hex(g_updateApp)
             + " updateProportions=" + Hex(g_updateProp));
}

// Is this appearance object an AppearanceHuman?
//
// T-293 - THE TEST IS THE VTABLE POINTER. The game has three appearance classes (RTTI in both Steam exes: AppearanceBase,
// AppearanceHuman, AppearanceAnimal), each with one vtable, so "vtable == image base + row AppearanceHumanVt" IS the
// class (1.0.65 0x16E5208, 1.0.68 0x16E6338; the address table checks at start that its first slot points where RTTI
// says). The rule is coophuman::HumanClassDecide (src/common/humantest.h, offline-tested).
//
// WHY THE OLD TEST NEVER MATCHED (F159 / F161 / T-293). It looked for AppearanceHuman::setGender's ADDRESS in the vtable.
// This game is incrementally linked: every vtable slot holds a 5-byte jump stub (E9 rel32) at the front of .text, never
// the function itself. T625 [P020] (1.0.65, uid 4194305, a Greenlander): the vtable at image base + 0x16E5208 (RTTI
// .?AVAppearanceHuman@@) has 14 slots, RVAs 0xA051-0x50911; slot 5 (+0x28) = 0x4A3CC, which jumps to 0x52D530 =
// setGender (row AppearanceHuman_chooseSex); slot 8 (+0x40) = 0x3208D -> 0x544620 = isFlayed; slot 11 (+0x58) = 0xA051
// -> 0x539440 = createBody. T633 (1.0.68) the same with 0x4A408 -> 0x52DFC0. So the header's offsets were right after
// all, and F161's "isFlayed is 0x160 out" was not the address table's number.
// Lesson: a function address is never IN this game's vtables - compare the vtable POINTER, or follow the stub.
//
// It cannot fail silently: every answer is counted (g_humanYes / g_humanNo / g_humanUnreadable, REPORT humanTest[...]),
// the first human logs its RTTI class name and what slots 5 and 11 really call, and a missing row logs once.

// The vtable pointer, or 0 when the object or the pointer is unreadable. No C++ object here (C2712).
static uintptr_t ReadVtablePod(void* app)
{
    if (app == 0 || !Ptr(app)) return 0;
    __try { const uintptr_t vt = *(uintptr_t*)app; return Ptr((void*)vt) ? vt : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Where vtable slot `slot` really goes: the slot's value (*raw), then through up to four E9 jump stubs. 0 on a fault.
// No C++ object here (C2712).
static uintptr_t SlotTargetPod(uintptr_t vt, int slot, uintptr_t* raw)
{
    *raw = 0;
    __try
    {
        uintptr_t at = *(uintptr_t*)(vt + slot * sizeof(void*));
        *raw = at;
        for (int k = 0; k < 4; ++k)
        {
            const uintptr_t next = (uintptr_t)coophuman::ThunkTarget((unsigned long long)at, (const unsigned char*)at);
            if (next == at) break;
            at = next;
        }
        return at;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// The compiler's class name for this vtable (MSVC x64 RTTI: vt[-1] -> the complete object locator; its +12 is the type
// descriptor's RVA; the name is 16 bytes into that, e.g. ".?AVAppearanceHuman@@"). "" on a fault. No C++ object here.
static void RttiNamePod(uintptr_t vt, uintptr_t base, char* out, int cap)
{
    out[0] = 0;
    __try
    {
        const uintptr_t col = *(uintptr_t*)(vt - sizeof(void*));
        if (col <= base || col >= base + 0x4000000) return;
        const unsigned int td = *(unsigned int*)(col + 12);
        const char* nm = (const char*)(base + td + 16);
        int i = 0;
        for (; i < cap - 1 && nm[i] != 0; ++i) out[i] = nm[i];
        out[i] = 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
}

static std::string HumanRva(uintptr_t a, uintptr_t base)
{
    return a == 0 ? std::string("?") : Hex((unsigned long long)(a - base));
}

// Once, at the first human recognised: the class and what its setGender / createBody slots really call.
static void NoteFirstHuman(uintptr_t vt, uintptr_t base)
{
    ResolveOnce();
    char cls[64];
    RttiNamePod(vt, base, cls, (int)sizeof(cls));
    uintptr_t raw5 = 0, raw11 = 0;
    const uintptr_t t5 = SlotTargetPod(vt, 5, &raw5);
    const uintptr_t t11 = SlotTargetPod(vt, 11, &raw11);
    DebugLog("[P019] human test: first AppearanceHuman recognised - vtable " + Hex((unsigned long long)vt)
             + " = image base + " + Hex(kHumanVtRva) + " (row AppearanceHumanVt), RTTI class '" + std::string(cls) + "'"
             + "; slot 5 (+0x28) holds rva " + HumanRva(raw5, base) + " -> " + HumanRva(t5, base)
             + (t5 != 0 && t5 == g_setGender ? std::string(" = setGender (row AppearanceHuman_chooseSex)")
                                             : std::string(" - NOT setGender ") + HumanRva(g_setGender, base))
             + "; slot 11 (+0x58, createBody) holds rva " + HumanRva(raw11, base) + " -> " + HumanRva(t11, base));
}

// 1 human, 0 a readable appearance of another class, -1 no row / unreadable (never "human"). Counted every call.
int HumanClassOf(void* app)
{
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    const uintptr_t vt = ReadVtablePod(app);
    const int v = coophuman::HumanClassDecide((unsigned long long)vt, (unsigned long long)base, kHumanVtRva);
    if (v == coophuman::kHumanYes)
    {
        ::InterlockedIncrement(&g_humanYes);
        if (::InterlockedCompareExchange(&g_humanFirstNoted, 1, 0) == 0) NoteFirstHuman(vt, base);
    }
    else if (v == coophuman::kHumanNo)
    {
        ::InterlockedIncrement(&g_humanNo);
    }
    else
    {
        ::InterlockedIncrement(&g_humanUnreadable);
        if (kHumanVtRva == 0 && ::InterlockedCompareExchange(&g_humanNoRowNoted, 1, 0) == 0)
            DebugLog("[P019] human test: no address-table row AppearanceHumanVt - every character reads 'not human'"
                     " (counted as unreadable)");
    }
    return v;
}

bool IsHumanAppearance(void* app)
{
    return HumanClassOf(app) == coophuman::kHumanYes;
}

// Resolve a static-data record by its stringID. There is no HAIR itemType in this build's
// enum, so the name-plus-category lookup used everywhere else in this project has nothing to
// search for a haircut; the sid overload needs no category and is the record's canonical
// identity anyway. Empty sid = null, which is a legitimate value.
::GameData* FindDataBySid(const std::string& sid)
{
    if (sid.empty() || coop::GameWorldPtr() == 0) return 0;
    ::GameData* gd = coop::GameWorldPtr()->gamedata.getData(sid);
    return Obj(gd) ? gd : 0;
}

// crash1 (T293 / F912): Character -> PoseState (replicate.cpp kProneStateOff; 0 normal, 1 staying low, 2 crippled,
// 3 playing dead, 4 KO).
const size_t kCharProneStateOff = 0x0E0;
// crash1 fold (review-crash1 HIGH-1): MedicalSystem (Character +0x458) `unconcious` +0x161 and the wake-up clock
// `knockoutClock` +0xA0 (medical.cpp H029 note: Character +0x4F8). ApplyLatch writes both from the owner's STATE; with
// either set the copy's own engine knocks it out by itself (F439), so a copy that has them is treated as going down.
const size_t kCharUnconciousOff = 0x458 + 0x161;
const size_t kCharKoTimerOff    = 0x4F8;

// crash1 (T293 / F912, Confirmed crash; the cause Inferred): B applied a remote APPEARANCE to a Skimmer copy 7 ms after
// M4 knocked it out, and the next Character::update ran the body rebuild 0x539C50, which went through
// Character::_ragdollMode 0x5D0320 and read a body node whose transform was already null. CharacterBuilt asks whether the
// body is BUILT, not whether it is limp. 1 = lying down (PoseState 2..4), in ragdoll, or about to be (unconscious, or a
// wake-up clock running); 0 = standing (PoseState 0 or 1, none of those); -1 = unreadable, which the callers treat as lying down (fail closed). inRagdoll 0x7D08A0
// double-dereferences the AnimationClass (F357), so the pointer is checked first and the call SEH-wrapped. No C++ object
// with a destructor lives in this frame (C2712).
int CopyDownedPod(::Character* c)
{
    if (c == 0 || !Obj(c)) return -1;
    int prone = 0;
    void* anim = 0;
    unsigned char ko = 0;
    float koTimer = 0.0f;
    __try
    {
        prone = *(int*)((char*)c + kCharProneStateOff);
        anim = *(void**)((char*)c + kCharAnimationOff);
        ko = *((unsigned char*)c + kCharUnconciousOff);
        koTimer = *(float*)((char*)c + kCharKoTimerOff);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    if (prone < 0 || prone > 4) return -1;
    if (prone >= 2) return 1;
    if (ko != 0 || (koTimer > 0.0f && koTimer < 3600.0f)) return 1;   // about to go limp (review-crash1 HIGH-1)
    if (!Obj(anim)) return -1;
    __try { return c->inRagdoll() ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// crash1b (T323): 1 = ACTUALLY limp - PoseState 2..4 or in ragdoll; 0 = standing or unreadable. Unlike CopyDownedPod it
// ignores the unconscious flag and the wake-up clock: a copy flagged unconscious can still be standing with a rebuild queued,
// and KnockdownMustWait must keep holding then. No C++ object with a destructor lives in this frame (C2712).
int LimpPod(::Character* c)
{
    if (c == 0 || !Obj(c)) return 0;
    int prone = 0;
    void* anim = 0;
    __try
    {
        prone = *(int*)((char*)c + kCharProneStateOff);
        anim = *(void**)((char*)c + kCharAnimationOff);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (prone >= 2 && prone <= 4) return 1;
    if (!Obj(anim)) return 0;
    __try { return c->inRagdoll() ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// crash1: 1 = a body rebuild is queued (the updAttach / updApp flags) or the body is being created. That rebuild runs at
// the character's next update; knocking the copy down before it runs would be T293 the other way round.
int AppearanceRebuildInFlight(::Character* c)
{
    void* app = GetAppearance(c);
    if (app == 0) return 0;   // no appearance object: nothing to rebuild
    return (*((unsigned char*)app + kUpdAttachOff) != 0
            || *((unsigned char*)app + kUpdAppOff) != 0
            || *((unsigned char*)app + kIsCreatingOff) != 0) ? 1 : 0;
}

// crash1c (review-crash1b R2c): AppearanceRebuildInFlight for ANY thread (the puppet knockout gate runs on engine threads).
// It only reads three bytes behind two validated hops; a fault reads as 1 (in flight), the safe direction - the gate then
// refuses a knockout, and the owner's knockout still arrives by STATE. No C++ object with a destructor here (C2712).
int RebuildInFlightGuarded(::Character* c)
{
    __try { return AppearanceRebuildInFlight(c); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
}

} // namespace

// crash1c (R2c): CopyRebuildInFlightAny, the exported face of RebuildInFlightGuarded, is defined below the createBody guard
// (T-293 fold 2: a copy whose createBody the guard holds now does not count as a rebuild in flight).

// THE COPY BODY-REBUILD GUARD - the one gate every rebuild of a copy's body passes. AppearanceHuman::createBody and
// AppearanceAnimal::createBody (one argument, this = the appearance object; AppearanceBase::update calls them through vtable
// slot +0x58 while +0x143 is set) are the only appearance steps that replace the body entity, so every road that rebuilds a
// body - the APPEARANCE apply, a clothing apply that queues a rebuild, a limb or race change, the engine's own - arrives
// here. On the MAIN thread a replicated character with a body (app +0xD8) - a copy or this game's own: the body decides, not
// the owner - does not rebuild while it lies in
// ragdoll (anim +0x2E8, anim = app +0x138) or while the main thread is inside the engine's ragdoll pass (the K2 detour on
// ThreadSafeRagdollUpdates counts the depth: the get-up blend's last update reaches AppearanceBase::update from inside the
// pass, and a rebuild there deletes the running blend and leaves it reading the destroyed node). coopbody::CopyBodyDecide
// says why. The detour then returns without the original: createBody clears +0x143 as its first action, so the byte stays
// set and the engine calls again on its next update - nothing is destroyed and the rebuild is retried until it runs. A read
// that faults also waits. Off the main thread it never waits. While +0x143 stays set AppearanceBase::update calls createBody
// and RETURNS before its +0x142 step (vt+0x30/+0x68) and its +0x141 step, so those wait with it. CopyRebuildInFlightAny
// (the knockout / death gates) therefore reads a copy the guard holds NOW as NOT in flight - the hold is what keeps every
// step of that rebuild off the held body - counted copyBodyHeld[gateHeldPassed]. KnockdownWait (the knockdown hold) asks
// CopyRebuildInFlightAny too, so a copy the guard holds is not waited on there either.
static unsigned long long kAnimalCreateBodyRva = 0;
static AddrReg kAnimalCreateBodyRva_reg("AppearanceAnimalCreateBody", &kAnimalCreateBodyRva);   /* Steam_1.0.65 0x539C50 */
typedef void (*AnimalCreateBodyFn)(void* app);
static AnimalCreateBodyFn orig_animalCreateBody = 0;
static int g_animalBodyHook = 0;                        // 1 installed, -1 failed, 0 no row
static volatile LONG g_copyBodyDeferred = 0;            // createBody calls skipped (main thread)
static volatile LONG g_copyBodyDeferredUids = 0;        // distinct uids ever skipped
static volatile LONG g_copyBodyResumed = 0;             // a deferred uid's createBody later ran (once per episode)
static volatile LONG g_copyBodyOffMain = 0;             // calls not on the main thread (or before it is known) - never skipped
static volatile LONG g_copyBodyReadFault = 0;           // skips because a read faulted
static LONG g_copyBodyLines = 0;                        // main thread only
// T-293 fold 1 (review F2): the human createBody guard - its own row, hook and counters (the log line cap is shared).
static unsigned long long kHumanCreateBodyRva = 0;
static AddrReg kHumanCreateBodyRva_reg("AppearanceHumanCreateBody", &kHumanCreateBodyRva);   /* Steam_1.0.65 0x539440, 1.0.68 0x539ED0 */
static AnimalCreateBodyFn orig_humanCreateBody = 0;     // the same one-argument shape (this = the appearance object)
static int g_humanBodyHook = 0;                         // 1 installed, -1 failed, 0 no row
static volatile LONG g_copyBodyHumanDeferred = 0;
static volatile LONG g_copyBodyHumanDeferredUids = 0;
static volatile LONG g_copyBodyHumanResumed = 0;
static volatile LONG g_copyBodyHumanOffMain = 0;
static volatile LONG g_copyBodyHumanReadFault = 0;
static const LONG kCopyBodyLineCap = 200;
static const size_t kCopyBodyAppCharOff = 0x0F0;        // AppearanceBase::me
static const size_t kCopyBodyRagdollOff = 0x2E8;        // AnimationClass -> the ragdoll object (0 = not in ragdoll)
static volatile LONG g_ragdollPassDepth = 0;            // main thread: how deep it is inside the engine's ragdoll pass now
static volatile LONG g_ragdollPassOffMain = 0;          // ragdoll pass entries not on the main thread (or before it is known)
static volatile LONG g_copyBodyPassDeferred = 0;        // createBody calls held because they came from inside the pass (both classes)
static volatile LONG g_copyBodyPassEpisodes = 0;        // holds that began inside the pass (not the ragdoll, not a fault)
static volatile LONG g_copyBodyLongHolds = 0;           // holds still in place after kCopyBodyLongHoldMs, held inside the pass then
static volatile LONG g_copyBodyLongHoldsLimp = 0;       // ... held lying in ragdoll (a corpse stays held) or on a faulting read
static volatile LONG g_ragdollPassDepthResets = 0;      // ticks outside the pass that found the pass depth not 0 (reset to 0)
static const DWORD kCopyBodyLongHoldMs = 30000;

// PROBE-START: P091 - copy createBody / knockdown gate state at the crash precondition (F912/F938/F949, T421)
// READ AND LOG ONLY. Since crash2 the createBody hook is the guard above (final code); P091CreateBodyNote is called from
// inside it, just before the engine's createBody runs, so it sees only the calls the guard lets through (from crash2 on
// createBodyCopyRagdoll should read 0). AppearanceBase +0xF0 is its Character (`me`). The note may
// run on an engine thread: its counters are interlocked, the per-uid clock table is written on the main thread only (an
// engine thread only reads it), and every P091 DebugLog is claimed by compare-exchange. All P091 lines share one cap of
// 400 per process.
static volatile LONG g_p091CreateBodyCopy = 0;          // createBody entered on one of our copies
static volatile LONG g_p091CreateBodyCopyRagdoll = 0;   // ... with the copy in ragdoll at entry (the crash precondition)
static volatile LONG g_p091KnockLimpRebuild = 0;        // KnockdownMustWait calls taking limpEarly with any of the 4 bytes set
static volatile LONG g_p091Lines = 0;                   // P091 lines claimed (createBody + knock)
static volatile LONG g_p091LogBusy = 0;
static const LONG kP091LineCap = 400;
static const size_t kP091AppCharOff = 0x0F0;            // AppearanceBase::me
static const DWORD kP091KnockGapMs = 3000;              // a knockdown STATE after 3 s without one = a new knockdown
static const int kP091Rows = 128;
static const size_t kCreateBodyPendingOffP091 = 0x143;  // AppearanceBase byte: createBody runs next update (F939; kCreateBodyPendingOff is defined further down)

struct P091Row { volatile LONG uid; volatile LONG appAt; volatile LONG knockAt; DWORD lastCallAt; int lastBranch; };
static P091Row g_p091Rows[kP091Rows];
static LONG g_p091Next = 0;                             // main thread only

struct P091Snap { int ragdoll, prone, limp, b141, b142, b143, bE8; };

static int P091ClaimLog()
{
    for (int i = 0; i < 20000; ++i)
    {
        if (::InterlockedCompareExchange(&g_p091LogBusy, 1, 0) == 0) return 1;
        YieldProcessor();
    }
    return 0;
}
static void P091ReleaseLog() { ::InterlockedExchange(&g_p091LogBusy, 0); }

// ANY THREAD, read only.
static P091Row* P091Find(unsigned int uid)
{
    if (uid == 0) return 0;
    for (int i = 0; i < kP091Rows; ++i)
        if ((unsigned int)g_p091Rows[i].uid == uid) return &g_p091Rows[i];
    return 0;
}

// MAIN THREAD: the uid's row, taking the oldest slot when the table is full.
static P091Row* P091Claim(unsigned int uid)
{
    P091Row* r = P091Find(uid);
    if (r != 0) return r;
    r = &g_p091Rows[g_p091Next % kP091Rows];
    ++g_p091Next;
    r->uid = 0; r->appAt = 0; r->knockAt = 0; r->lastCallAt = 0; r->lastBranch = -9;
    r->uid = (LONG)uid;
    return r;
}

static LONG P091Now() { const DWORD t = ::GetTickCount(); return (LONG)(t != 0 ? t : 1); }
static long long P091MsSince(LONG at) { return at == 0 ? -1 : (long long)(DWORD)(::GetTickCount() - (DWORD)at); }

// MAIN THREAD (the APPEARANCE apply loop).
static void P091NoteAppearanceApplied(unsigned int uid) { P091Claim(uid)->appAt = P091Now(); }

// Reads only; -1 = unreadable. No C++ object with a destructor in this frame (C2712).
static void P091SnapPod(::Character* c, void* app, P091Snap* s)
{
    s->ragdoll = -1; s->prone = -1; s->b141 = -1; s->b142 = -1; s->b143 = -1; s->bE8 = -1;
    s->limp = LimpPod(c);
    __try { s->prone = *(int*)((char*)c + kCharProneStateOff); }
    __except (EXCEPTION_EXECUTE_HANDLER) { s->prone = -1; }
    if (app != 0)
    {
        __try
        {
            s->b141 = *((unsigned char*)app + kUpdAttachOff);
            s->b142 = *((unsigned char*)app + kUpdAppOff);
            s->b143 = *((unsigned char*)app + kCreateBodyPendingOffP091);
            s->bE8  = *((unsigned char*)app + kIsCreatingOff);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { }
    }
    __try
    {
        void* anim = *(void**)((char*)c + kCharAnimationOff);
        if (Obj(anim)) s->ragdoll = c->inRagdoll() ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { s->ragdoll = -1; }
}

// ANY THREAD, from the detour. Everything inside __try; no std::string here (C2712) - the line is a char buffer.
static void P091CreateBodyNote(void* app)
{
    int claimed = 0;
    __try
    {
        if (app == 0 || !Obj(app)) return;
        ::Character* c = *(::Character**)((char*)app + kP091AppCharOff);
        if (c == 0 || !Obj(c)) return;
        const unsigned int uid = FindSpawnedUid(c);
        if (uid == 0) return;
        P091Snap sn;
        P091SnapPod(c, app, &sn);
        ::InterlockedIncrement(&g_p091CreateBodyCopy);
        if (sn.ragdoll == 1) ::InterlockedIncrement(&g_p091CreateBodyCopyRagdoll);
        if (::InterlockedCompareExchange(&g_p091Lines, 0, 0) >= kP091LineCap) return;
        const unsigned long m = StoreMainThreadId();
        const int onMain = (m != 0 && ::GetCurrentThreadId() == (DWORD)m) ? 1 : 0;
        const P091Row* r = P091Find(uid);
        const long long msApp = (r != 0) ? P091MsSince(r->appAt) : -1;
        const long long msKnock = (r != 0) ? P091MsSince(r->knockAt) : -1;
        const int appIsCurrent = (GetAppearance(c) == app) ? 1 : 0;
        if (!P091ClaimLog()) return;
        claimed = 1;
        if (::InterlockedIncrement(&g_p091Lines) <= kP091LineCap)
        {
            char buf[384];
            _snprintf(buf, sizeof(buf) - 1,
                      "[P091] createBody copy uid=%u thread=%s isRagdoll=%d prone=%d limp=%d app[141,142,143,E8]=%d,%d,%d,%d"
                      " msSinceAppearance=%lld msSinceKnockdown=%lld appIsCurrent=%d n=%ld ragdollN=%ld",
                      uid, onMain ? "main" : "off", sn.ragdoll, sn.prone, sn.limp, sn.b141, sn.b142, sn.b143, sn.bE8,
                      msApp, msKnock, appIsCurrent, (long)g_p091CreateBodyCopy, (long)g_p091CreateBodyCopyRagdoll);
            buf[sizeof(buf) - 1] = 0;
            DebugLog(buf);
        }
        P091ReleaseLog();
        claimed = 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { if (claimed) P091ReleaseLog(); }
}

// MAIN THREAD, from KnockdownMustWait. One line per uid per knockdown and per change of branch within it.
void P091NoteKnock(unsigned int uid, ::Character* c, int branch)
{
    if (uid == 0 || c == 0) return;
    P091Row* r = P091Claim(uid);
    const DWORD now = ::GetTickCount();
    const bool fresh = (r->lastCallAt == 0 || now - r->lastCallAt > kP091KnockGapMs);
    if (fresh) r->knockAt = P091Now();
    const bool changed = fresh || r->lastBranch != branch;
    r->lastCallAt = (DWORD)P091Now();
    r->lastBranch = branch;
    P091Snap sn;
    P091SnapPod(c, GetAppearance(c), &sn);
    const bool pending = sn.b141 > 0 || sn.b142 > 0 || sn.b143 > 0 || sn.bE8 > 0;
    if (branch == -1 && pending) ::InterlockedIncrement(&g_p091KnockLimpRebuild);
    if (!changed) return;
    if (::InterlockedCompareExchange(&g_p091Lines, 0, 0) >= kP091LineCap) return;
    const char* name = (branch == -1) ? "limpEarly" : (branch == 0) ? "none" : (branch == 1) ? "wait1" : (branch == 2) ? "wait2" : "?";
    const std::string line = "[P091] knock uid=" + S((long long)uid) + " branch=" + name + (fresh ? " (new knockdown)" : " (changed)")
        + " isRagdoll=" + S((long long)sn.ragdoll) + " prone=" + S((long long)sn.prone) + " limp=" + S((long long)sn.limp)
        + " app[141,142,143,E8]=" + S((long long)sn.b141) + "," + S((long long)sn.b142) + "," + S((long long)sn.b143)
        + "," + S((long long)sn.bE8) + " msSinceAppearance=" + S(P091MsSince(r->appAt))
        + " knockLimpWithRebuildPending=" + S((long long)g_p091KnockLimpRebuild);
    if (!P091ClaimLog()) return;
    if (::InterlockedIncrement(&g_p091Lines) <= kP091LineCap) DebugLog(line);
    P091ReleaseLog();
}

static void P091ReportLine()
{
    DebugLog("[P091] REPORT createBodyCopy=" + S((long long)g_p091CreateBodyCopy)
             + " createBodyCopyRagdoll=" + S((long long)g_p091CreateBodyCopyRagdoll)
             + " knockLimpWithRebuildPending=" + S((long long)g_p091KnockLimpRebuild)
             + " lines=" + S((long long)g_p091Lines) + " hook=" + S((long long)g_animalBodyHook));
}
// PROBE-END: P091

// crash2: the guard's reads. POD only - no C++ object with a destructor in a frame with __try (C2712). MAIN THREAD.
struct CopyBodyRead { unsigned int uid; int replicated; int owned; int entity; int ragdoll; int fault; };

static void CopyBodyReadPod(void* app, CopyBodyRead* r)
{
    r->uid = 0; r->replicated = 0; r->owned = 0; r->entity = 0; r->ragdoll = 0; r->fault = 0;
    __try
    {
        if (app == 0) { r->fault = 1; return; }
        ::Character* c = *(::Character**)((char*)app + kCopyBodyAppCharOff);
        const unsigned int uid = FindSpawnedUid(c);
        r->uid = uid;
        // uid 0: a character the mod never replicated (a game's own creature the mod never touched) or a retired row - never held.
        // Every replicated uid has its body read, whoever owns it now: an owner change does not end a ragdoll.
        if (uid == 0) return;
        r->replicated = 1;
        r->owned = net::IsUidMineAnyThread(uid) ? 1 : 0;
        r->entity = (*(void**)((char*)app + kAppearanceEntOff) != 0) ? 1 : 0;
        if (r->entity == 0) return;   // manager fold: no old body = nothing destroyed, so a first build (anim not read) never waits
        void* anim = *(void**)((char*)app + kAppearanceAnimOff);
        if (anim == 0) { r->fault = 1; return; }   // (app +0x138) +0x2E8 cannot be read
        r->ragdoll = (*(void**)((char*)anim + kCopyBodyRagdollOff) != 0) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { r->fault = 1; }
}

// MAIN THREAD only: the uids whose rebuild is deferred now (the episode), and every uid ever deferred.
struct CopyBodyEpisode { DWORD since; long calls; bool longLogged; bool ownedSeen; };
static std::map<unsigned int, CopyBodyEpisode> g_copyBodyEpisodes;
static std::map<unsigned int, char> g_copyBodyUidsEver;
// The uids whose body rebuild the MOD queued (a look, kit or limb apply left +0x143 set on a replicated
// character) and whose createBody has not run since. The guard holds such a rebuild as it holds a carried-over one
// (heldBefore), so a rebuild queued on a copy that becomes this game's own before its createBody comes (an area hand-over, a
// RELEASE adopt, a hire) is still held while the body lies in ragdoll - without the mark the guard read "owned, no hold" and
// ran it on the ragdolled body (F986). Cleared when the guard lets that uid's createBody run, when the uid is forgotten, and at
// a world load / teardown. MAIN THREAD.
static std::map<unsigned int, char> g_copyBodyModQueued;
static LONG g_copyBodyModMarks = 0;                      // marks placed (main thread)
static volatile LONG g_copyBodyModMarkOffMain = 0;       // mark requests off the main thread: not placed (expected 0)
// createBody calls the guard RAN for a body this game owns, with a body, lying in ragdoll or inside the ragdoll pass, with no
// hold and no mod mark: the engine's own rebuilds of an owned body in the crash's state (are there any at all?). MAIN THREAD.
static LONG g_copyBodyOwnedRanInRagdoll = 0;
static LONG g_copyBodyOwnedRanLines = 0;
static const LONG kCopyBodyOwnedRanLineCap = 10;

// Holds (episodes, both classes) in which the guard held a body this game owns at that call - once per episode, also when the
// uid became this game's own while already held (an area hand-over). The first kCopyBodyOwnedLineCap are written. MAIN THREAD.
static volatile LONG g_copyBodyHeldOwned = 0;
static LONG g_copyBodyOwnedLines = 0;
static const LONG kCopyBodyOwnedLineCap = 20;
static void CopyBodyHeldOwnedNote(unsigned int uid, const char* cls, const char* why, CopyBodyEpisode* ep)
{
    ep->ownedSeen = true;
    ::InterlockedIncrement(&g_copyBodyHeldOwned);
    if (g_copyBodyOwnedLines >= kCopyBodyOwnedLineCap) return;
    ++g_copyBodyOwnedLines;
    DebugLog("[COPY] body rebuild of uid=" + S((long long)uid) + " held while this game owns it: " + why + cls);
}

// T-293 fold 2 (F2-A): the uids of the episode map (the copies whose createBody the guard holds now), mirrored for ANY thread -
// the knockout / death gates run on engine threads and the maps are main-thread only. One writer (the main thread); a slot
// reading 0 is free. A uid the table has no room for reads "not held", which is the gates' old answer (counted tableFull).
static const size_t kCopyBodyPendingOff = 0x143;         // AppearanceBase : byte, set = createBody runs next update
static const int kCopyBodyHeldSlots = 128;
static volatile LONG g_copyBodyHeldUid[kCopyBodyHeldSlots];
static volatile LONG g_copyBodyHeldFull = 0;             // episodes the held table had no room for
static volatile LONG g_gateHeldPassed = 0;               // gate checks that read "in flight" and passed: the guard holds createBody
static long g_copyBodyMapsGen = -1;                      // the world-load generation the maps belong to (written on the main thread)

static void CopyBodyHeldAdd(unsigned int uid)
{
    int freeSlot = -1;
    for (int i = 0; i < kCopyBodyHeldSlots; ++i)
    {
        const LONG v = g_copyBodyHeldUid[i];
        if (v == (LONG)uid) return;
        if (v == 0 && freeSlot < 0) freeSlot = i;
    }
    if (freeSlot < 0) { ::InterlockedIncrement(&g_copyBodyHeldFull); return; }
    ::InterlockedExchange(&g_copyBodyHeldUid[freeSlot], (LONG)uid);
}

static void CopyBodyHeldRemove(unsigned int uid)
{
    for (int i = 0; i < kCopyBodyHeldSlots; ++i)
        if (g_copyBodyHeldUid[i] == (LONG)uid) ::InterlockedExchange(&g_copyBodyHeldUid[i], 0);
}

// ANY THREAD.
static int CopyBodyHeldHas(unsigned int uid)
{
    if (uid == 0) return 0;
    for (int i = 0; i < kCopyBodyHeldSlots; ++i)
        if (g_copyBodyHeldUid[i] == (LONG)uid) return 1;
    return 0;
}

// ANY THREAD (the report line).
static long CopyBodyHeldCount()
{
    long n = 0;
    for (int i = 0; i < kCopyBodyHeldSlots; ++i)
        if (g_copyBodyHeldUid[i] != 0) ++n;
    return n;
}

// MAIN THREAD. T-293 fold 2 (F3-b): the copy is gone - its guard marks go with it.
static void CopyBodyForgetUid(unsigned int uid)
{
    g_copyBodyEpisodes.erase(uid);
    g_copyBodyUidsEver.erase(uid);
    g_copyBodyModQueued.erase(uid);   // a rebuild the mod queued on the gone body is gone with it
    CopyBodyHeldRemove(uid);
}

// MAIN THREAD. Every copy is gone (world teardown, a world load): every guard mark goes with them.
static void CopyBodyForgetAll()
{
    g_copyBodyEpisodes.clear();
    g_copyBodyUidsEver.clear();
    g_copyBodyModQueued.clear();   // the mod's queued-rebuild marks describe the old world's bodies
    for (int i = 0; i < kCopyBodyHeldSlots; ++i) ::InterlockedExchange(&g_copyBodyHeldUid[i], 0);
}

// MAIN THREAD. T-293 fold 2 (F3-b): a world load (StoreWorldGenNow moved) clears the guard's maps; the uids describe the old world.
static void CopyBodyWorldCheck()
{
    const long gen = StoreWorldGenNow();
    if (gen == g_copyBodyMapsGen) return;
    CopyBodyForgetAll();
    g_copyBodyMapsGen = gen;
}

// MAIN THREAD. the mod has just queued a body rebuild on `uid` - marked until that uid's createBody runs. The
// world check comes first, so a mark placed early in a new world is not wiped by the guard's first check of that world.
static void CopyBodyModMark(unsigned int uid)
{
    if (uid == 0) return;
    CopyBodyWorldCheck();
    if (g_copyBodyModQueued.insert(std::make_pair(uid, (char)1)).second) ++g_copyBodyModMarks;
}

// bodytest pending <copyUid> - TEST-ONLY lever on the game that holds the COPY: it forces the window in which a copy's queued
// body rebuild meets the engine's ragdoll pass. Armed, it waits for the copy's get-up blend (anim +0x2F0 set, its isAnimating
// byte +0x61 set); then, at the start of every ragdoll pass while that blend runs, it sets the appearance's rebuild-pending
// byte (+0x143) - only when the byte was clear - and clears it again after the pass if the blend is still running, so no
// ordinary update between passes consumes it. The pass in which the blend ends runs the blend's last update, which reaches
// AppearanceBase::update and so createBody with +0x143 set: the copy body-rebuild guard must hold it ("deferred: inside the
// engine's ragdoll pass") and the next ordinary update rebuild it. WITHOUT THE GUARD THIS LEVER REPRODUCES THE CRASH (the
// rebuild deletes the running blend, which then reads the old body's destroyed node). Every read is guarded; a uid this game
// drives, or one it has not spawned, is refused. One arming at a time; it gives up after kBodyTestWindowMs. [BODYTEST] lines.
// MAIN THREAD (the command, AppearanceTick, and the K2 detour around the pass).
static unsigned int g_bodyTestUid = 0;          // the armed copy (0 = not armed)
static DWORD        g_bodyTestArmedAt = 0;
static bool         g_bodyTestSetThisPass = false;   // this pass's start set +0x143
static long         g_bodyTestPassesSet = 0;     // passes of the current arming that started with +0x143 set by the lever
static LONG g_bodyTestArms = 0, g_bodyTestFired = 0, g_bodyTestTimeouts = 0, g_bodyTestRefused = 0;
static const DWORD kBodyTestWindowMs = 120000;
static const size_t kBodyTestBlendOff = 0x2F0;    // AnimationClass -> RagdollAnimation, the get-up blend (0 = none)
static const size_t kBodyTestRunningOff = 0x61;   // RagdollAnimation : byte, isAnimating
static const size_t kBodyTestTimerOff = 0x68;     // RagdollAnimation : float, time into the blend
static const size_t kBodyTestLengthOff = 0x64;    // RagdollAnimation : float, the blend's length

struct BodyTestRead { int ok; int blend; int running; int pending; float timer; float length; void* app; };

// No C++ object with a destructor in this frame (C2712).
static void BodyTestReadPod(::Character* c, BodyTestRead* r)
{
    r->ok = 0; r->blend = 0; r->running = 0; r->pending = 0; r->timer = 0.0f; r->length = 0.0f; r->app = 0;
    if (c == 0 || !Obj(c)) return;
    __try
    {
        void* anim = *(void**)((char*)c + kCharAnimationOff);
        if (anim == 0) return;
        void* app = *(void**)((char*)anim + kAnimAppearanceOff);
        if (app == 0) return;
        r->app = app;
        r->pending = *((unsigned char*)app + kCopyBodyPendingOff) != 0 ? 1 : 0;
        void* blend = *(void**)((char*)anim + kBodyTestBlendOff);
        if (blend != 0)
        {
            r->blend = 1;
            r->running = *((unsigned char*)blend + kBodyTestRunningOff) != 0 ? 1 : 0;
            r->timer = *(float*)((char*)blend + kBodyTestTimerOff);
            r->length = *(float*)((char*)blend + kBodyTestLengthOff);
        }
        r->ok = 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { r->ok = 0; }
}

static int BodyTestWritePendingPod(void* app, unsigned char v)
{
    __try { *((unsigned char*)app + kCopyBodyPendingOff) = v; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static std::string BodyTestF(float v)
{
    char b[32];
    _snprintf(b, sizeof(b) - 1, "%.2f", (double)v);
    b[sizeof(b) - 1] = 0;
    return std::string(b);
}

static std::string BodyTestCounts()
{
    return " bodytest[arms,fired,timeouts,refused]=" + S((long long)g_bodyTestArms) + "," + S((long long)g_bodyTestFired) + ","
         + S((long long)g_bodyTestTimeouts) + "," + S((long long)g_bodyTestRefused)
         + " copyBodyPassDeferred=" + S((long long)g_copyBodyPassDeferred);
}

std::string BodyTestLever(const std::string& sub, unsigned int uid)
{
    if (sub != "pending" || uid == 0) return "error bodytest usage: bodytest pending <copyUid>";
    std::string why;
    ::Character* c = FindSpawned(uid);
    if (net::IsUidMine(uid)) why = "this game drives it (arm it on the game that holds the copy)";
    else if (c == 0) why = "no character with that uid on this game";
    else
    {
        BodyTestRead r;
        BodyTestReadPod(c, &r);
        if (r.ok == 0) why = "its animation or appearance cannot be read";
    }
    if (!why.empty())
    {
        ::InterlockedIncrement(&g_bodyTestRefused);
        DebugLog("[BODYTEST] pending (TEST) uid=" + S((long long)uid) + " refused: " + why + BodyTestCounts());
        return "error bodytest pending: " + why;
    }
    if (g_bodyTestUid != 0 && g_bodyTestUid != uid)
        DebugLog("[BODYTEST] pending (TEST) uid=" + S((long long)g_bodyTestUid) + " disarmed: a new arming replaces it");
    g_bodyTestUid = uid; g_bodyTestArmedAt = ::GetTickCount(); g_bodyTestSetThisPass = false; g_bodyTestPassesSet = 0;
    ::InterlockedIncrement(&g_bodyTestArms);
    DebugLog("[BODYTEST] pending (TEST) uid=" + S((long long)uid) + " armed: +0x143 will be set inside each ragdoll pass while"
             " the copy's get-up blend runs, until the blend ends" + BodyTestCounts());
    return "ok bodytest pending uid=" + S((long long)uid) + " armed";
}

// MAIN THREAD, from AppearanceTick: the arming gives up when no get-up blend came in the window.
static void BodyTestTick()
{
    if (g_bodyTestUid == 0) return;
    if ((DWORD)(::GetTickCount() - g_bodyTestArmedAt) < kBodyTestWindowMs) return;
    ::InterlockedIncrement(&g_bodyTestTimeouts);
    DebugLog("[BODYTEST] pending (TEST) uid=" + S((long long)g_bodyTestUid) + " gave up: no get-up blend ended within "
             + S((long long)(kBodyTestWindowMs / 1000)) + " s (passes set=" + S((long long)g_bodyTestPassesSet) + ")" + BodyTestCounts());
    g_bodyTestUid = 0;
}

// MAIN THREAD, at the start of the outermost ragdoll pass.
static void BodyTestPassBefore()
{
    g_bodyTestSetThisPass = false;
    if (g_bodyTestUid == 0) return;
    if (net::IsUidMine(g_bodyTestUid))
    {
        DebugLog("[BODYTEST] pending (TEST) uid=" + S((long long)g_bodyTestUid) + " disarmed: this game drives it now (not a copy)"
                 + BodyTestCounts());
        g_bodyTestUid = 0;
        return;
    }
    ::Character* c = FindSpawned(g_bodyTestUid);
    BodyTestRead r;
    BodyTestReadPod(c, &r);
    if (r.ok == 0 || r.blend == 0 || r.running == 0 || r.pending != 0) return;
    if (BodyTestWritePendingPod(r.app, 1) == 0) return;
    CopyBodyModMark(g_bodyTestUid);   // the lever queued this rebuild - held as the mod's own if the uid changes owner first
    g_bodyTestSetThisPass = true;
    if (++g_bodyTestPassesSet == 1)
        DebugLog("[BODYTEST] pending (TEST) uid=" + S((long long)g_bodyTestUid) + " set +0x143 at a pass start: the get-up blend"
                 " runs (" + BodyTestF(r.timer) + " of " + BodyTestF(r.length) + " s)");
}

// MAIN THREAD, at the end of the outermost ragdoll pass.
static void BodyTestPassAfter()
{
    if (!g_bodyTestSetThisPass) return;
    g_bodyTestSetThisPass = false;
    ::Character* c = FindSpawned(g_bodyTestUid);
    BodyTestRead r;
    BodyTestReadPod(c, &r);
    if (r.ok == 0)
    {
        DebugLog("[BODYTEST] pending (TEST) uid=" + S((long long)g_bodyTestUid) + " dropped: the character can no longer be read"
                 " after the pass (not counted as fired)" + BodyTestCounts());
        g_bodyTestUid = 0;
        return;
    }
    if (r.blend != 0 && r.running != 0)
    {
        BodyTestWritePendingPod(r.app, 0);   // the blend runs on: no ordinary update may take the rebuild between passes
        return;
    }
    ::InterlockedIncrement(&g_bodyTestFired);
    DebugLog("[BODYTEST] pending (TEST) uid=" + S((long long)g_bodyTestUid) + " fired: the get-up blend's last update ran inside"
             " this pass with +0x143 set (passes set=" + S((long long)g_bodyTestPassesSet) + ", +0x143 now="
             + S((long long)r.pending) + ", blend now=" + S((long long)r.blend) + ")"
             + BodyTestCounts());
    g_bodyTestUid = 0;
}

// The K2 detour on ThreadSafeRagdollUpdates, around the engine's pass: returns the depth to restore afterwards. Only the main
// thread's depth is kept (the guard holds on the main thread only); an entry on another thread is counted.
long RagdollPassEnter()
{
    const unsigned long m = StoreMainThreadId();
    if (m == 0 || ::GetCurrentThreadId() != (DWORD)m) { ::InterlockedIncrement(&g_ragdollPassOffMain); return -1; }
    const long saved = (long)g_ragdollPassDepth;
    if (saved == 0) BodyTestPassBefore();   // TEST-ONLY lever: returns at once unless `bodytest pending` is armed
    ::InterlockedExchange(&g_ragdollPassDepth, (LONG)(saved + 1));
    return saved;
}

void RagdollPassLeave(long saved)
{
    if (saved < 0) return;
    ::InterlockedExchange(&g_ragdollPassDepth, (LONG)saved);
    if (saved == 0) BodyTestPassAfter();    // TEST-ONLY lever
}

// MAIN THREAD. true = skip this createBody call (the engine calls again next update). One rule for both classes; the
// counters are the class's own, the pass counters are shared.
static bool AnimalBodyMustWait(void* app, int human)
{
    volatile LONG* const cDeferred     = (human != 0) ? &g_copyBodyHumanDeferred     : &g_copyBodyDeferred;
    volatile LONG* const cDeferredUids = (human != 0) ? &g_copyBodyHumanDeferredUids : &g_copyBodyDeferredUids;
    volatile LONG* const cResumed      = (human != 0) ? &g_copyBodyHumanResumed      : &g_copyBodyResumed;
    volatile LONG* const cReadFault    = (human != 0) ? &g_copyBodyHumanReadFault    : &g_copyBodyReadFault;
    const char* const cls = (human != 0) ? " (human)" : "";
    CopyBodyWorldCheck();   // the maps below belong to this world load
    CopyBodyRead r;
    CopyBodyReadPod(app, &r);
    const int inPass = (g_ragdollPassDepth > 0) ? 1 : 0;
    const DWORD now = ::GetTickCount();
    std::map<unsigned int, CopyBodyEpisode>::iterator e = (r.uid != 0) ? g_copyBodyEpisodes.find(r.uid) : g_copyBodyEpisodes.end();
    const bool inEpisode = (e != g_copyBodyEpisodes.end());
    std::map<unsigned int, char>::iterator mk = (r.uid != 0) ? g_copyBodyModQueued.find(r.uid) : g_copyBodyModQueued.end();
    const bool modMarked = (mk != g_copyBodyModQueued.end());
    // held before = an open hold for this uid, or a rebuild the mod queued that has not run yet - whoever owns the
    // uid now, so a rebuild queued on a copy that became this game's own before this call still waits out the ragdoll
    const int heldBefore = (inEpisode || modMarked) ? 1 : 0;
    if (coopbody::CopyBodyDecide(1, r.replicated, r.owned, heldBefore, r.entity, r.ragdoll, inPass, r.fault) == coopbody::kCopyBodyRun)
    {
        if (modMarked) g_copyBodyModQueued.erase(mk);   // the rebuild the mod queued runs now
        if (!inEpisode)
        {
            if (!modMarked && r.owned != 0 && r.fault == 0 && r.entity != 0 && (r.ragdoll != 0 || inPass != 0))
            {
                ++g_copyBodyOwnedRanInRagdoll;
                if (g_copyBodyOwnedRanLines < kCopyBodyOwnedRanLineCap)
                {
                    ++g_copyBodyOwnedRanLines;
                    DebugLog("[COPY] body rebuild of uid=" + S((long long)r.uid) + cls + " RAN on a body this game owns "
                             + (r.ragdoll != 0 ? "lying in ragdoll" : "inside the engine's ragdoll pass")
                             + " - no hold, no mod mark: the engine queued it (ownedRanInRagdoll="
                             + S((long long)g_copyBodyOwnedRanInRagdoll) + ")");
                }
            }
            return false;
        }
        ::InterlockedIncrement(cResumed);
        if (g_copyBodyLines < kCopyBodyLineCap)
        {
            ++g_copyBodyLines;
            DebugLog("[COPY] body rebuild of uid=" + S((long long)r.uid) + cls + " rebuilt after deferral (ms="
                     + S((long long)(DWORD)(now - e->second.since)) + " deferredCalls=" + S((long long)e->second.calls) + ")");
        }
        CopyBodyHeldRemove(r.uid);
        g_copyBodyEpisodes.erase(e);
        return false;
    }
    ::InterlockedIncrement(cDeferred);
    if (r.fault != 0) ::InterlockedIncrement(cReadFault);
    const bool byPass = (r.fault == 0 && r.ragdoll == 0 && inPass != 0);
    if (byPass) ::InterlockedIncrement(&g_copyBodyPassDeferred);
    const char* why = (r.fault != 0) ? "a read faulted" : (r.ragdoll != 0) ? "lying in ragdoll" : "inside the engine's ragdoll pass";
    if (r.uid == 0) return true;
    if (inEpisode)
    {
        ++e->second.calls;
        if (r.owned != 0 && !e->second.ownedSeen) CopyBodyHeldOwnedNote(r.uid, cls, why, &e->second);
        if (!e->second.longLogged && (DWORD)(now - e->second.since) >= kCopyBodyLongHoldMs)
        {
            e->second.longLogged = true;
            ::InterlockedIncrement(byPass ? &g_copyBodyLongHolds : &g_copyBodyLongHoldsLimp);
            DebugLog("[COPY] body rebuild of uid=" + S((long long)r.uid) + cls + " still deferred after "
                     + S((long long)(DWORD)(now - e->second.since)) + " ms: " + why + " (deferredCalls="
                     + S((long long)e->second.calls) + ")");
        }
        return true;
    }
    if (g_copyBodyUidsEver.insert(std::make_pair(r.uid, (char)1)).second) ::InterlockedIncrement(cDeferredUids);
    CopyBodyEpisode ep; ep.since = now; ep.calls = 1; ep.longLogged = false; ep.ownedSeen = false;
    g_copyBodyEpisodes[r.uid] = ep;
    // a body this game owns opens a hold here only with a rebuild the mod queued (heldBefore from the mark)
    if (r.owned != 0) CopyBodyHeldOwnedNote(r.uid, cls, why, &g_copyBodyEpisodes[r.uid]);
    CopyBodyHeldAdd(r.uid);   // the gates' any-thread view of the episode
    if (byPass) ::InterlockedIncrement(&g_copyBodyPassEpisodes);
    if (g_copyBodyLines < kCopyBodyLineCap)
    {
        ++g_copyBodyLines;
        DebugLog("[COPY] body rebuild of uid=" + S((long long)r.uid) + cls + " deferred: " + why
                 + " - the engine retries each update");
    }
    return true;
}

// ANY THREAD. 1 = the guard holds this copy's createBody NOW: +0x143 is set, the body is not being created (+0xE8 clear), it is a
// copy with a body, and the guard held this uid on the main thread and has not let it through since (the held table, this world
// load) - so the engine's update calls createBody and either the guard holds it again or it runs on the main thread, and update
// returns before its +0x142 / +0x141 steps. A read that faults, or anything unproven, reads 0 (not held). needRagdoll = 1 also
// asks that the copy lies in ragdoll - a hold that lasts (a corpse); a hold inside the ragdoll pass ends at the next update.
// No C++ object with a destructor here (C2712).
static int CopyBodyHeldNowPod(::Character* c, int needRagdoll)
{
    __try
    {
        if (c == 0 || StoreWorldGenNow() != g_copyBodyMapsGen) return 0;
        void* app = GetAppearance(c);
        if (app == 0) return 0;
        if (*((unsigned char*)app + kCopyBodyPendingOff) == 0) return 0;
        if (*((unsigned char*)app + kIsCreatingOff) != 0) return 0;
        CopyBodyRead r;
        CopyBodyReadPod(app, &r);
        if (r.fault != 0 || r.uid == 0) return 0;
        if (r.replicated == 0 || r.owned != 0 || r.entity == 0) return 0;   // the gates ask about copies only (their old answer)
        if (needRagdoll != 0 && r.ragdoll == 0) return 0;
        return CopyBodyHeldHas(r.uid);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// crash1c (R2c): the exported face of RebuildInFlightGuarded. ANY THREAD. The one question the knockout / death gates ask
// (medical.cpp PuppetKoAllowed, CopyDeathAllowed, ApplyOwnerDeath). T-293 fold 2 (F2-A): a copy whose createBody the guard
// holds now is NOT a rebuild in flight - the hold is what keeps that rebuild off the limp body - counted gateHeldPassed.
int CopyRebuildInFlightAny(::Character* c)
{
    if (RebuildInFlightGuarded(c) == 0) return 0;
    if (CopyBodyHeldNowPod(c, 0) == 0) return 1;
    ::InterlockedIncrement(&g_gateHeldPassed);
    return 0;
}

static void detour_animalCreateBody(void* app)
{
    const unsigned long m = StoreMainThreadId();
    if (m == 0 || ::GetCurrentThreadId() != (DWORD)m) ::InterlockedIncrement(&g_copyBodyOffMain);   // never skipped
    else if (AnimalBodyMustWait(app, 0)) return;   // +0x143 stays set: the engine calls again on its next update
    // PROBE-START: P091 - the createBody calls the guard lets through
    P091CreateBodyNote(app);
    // PROBE-END: P091
    orig_animalCreateBody(app);   // the engine's createBody, unchanged and outside any __try
}

void InstallAnimalCreateBodyGuard()
{
    if (kAnimalCreateBodyRva == 0) { DebugLog("[COPY] no address-table row AppearanceAnimalCreateBody - the animal copy body-rebuild guard is OFF"); return; }
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    const coop::HookStatus h = coop::AddHook((void*)(base + (uintptr_t)kAnimalCreateBodyRva),
                                                       (void*)&detour_animalCreateBody, (void**)&orig_animalCreateBody);
    g_animalBodyHook = (h == coop::SUCCESS) ? 1 : -1;
    DebugLog(std::string("[COPY] AppearanceAnimal::createBody 0x539C50 body-rebuild guard AddHook ") + (g_animalBodyHook == 1 ? "SUCCESS" : "FAILED"));
}

// T-293 fold 1 (review F2): AppearanceHuman::createBody - the animal detour's rule and shape, the human counters, no P091 note
// (that probe describes the animal crash precondition).
static void detour_humanCreateBody(void* app)
{
    const unsigned long m = StoreMainThreadId();
    if (m == 0 || ::GetCurrentThreadId() != (DWORD)m) ::InterlockedIncrement(&g_copyBodyHumanOffMain);   // never skipped
    else if (AnimalBodyMustWait(app, 1)) return;   // +0x143 stays set: the engine calls again on its next update
    orig_humanCreateBody(app);   // the engine's createBody, unchanged and outside any __try
}

void InstallHumanCreateBodyGuard()
{
    if (kHumanCreateBodyRva == 0) { DebugLog("[COPY] no address-table row AppearanceHumanCreateBody - the human copy body-rebuild guard is OFF"); return; }
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    const coop::HookStatus h = coop::AddHook((void*)(base + (uintptr_t)kHumanCreateBodyRva),
                                             (void*)&detour_humanCreateBody, (void**)&orig_humanCreateBody);
    g_humanBodyHook = (h == coop::SUCCESS) ? 1 : -1;
    DebugLog(std::string("[COPY] AppearanceHuman::createBody (row AppearanceHumanCreateBody, rva ") + Hex(kHumanCreateBodyRva)
             + ") body-rebuild guard AddHook " + (g_humanBodyHook == 1 ? "SUCCESS" : "FAILED"));
}

// ---------------------------------------------------------------------------------------
// P019: read
// ---------------------------------------------------------------------------------------

bool AppearanceSettled(::Character* c)
{
    void* app = GetAppearance(c);
    if (app == 0) return false;
    return *((unsigned char*)app + kUpdAttachOff) == 0
        && *((unsigned char*)app + kUpdAppOff)    == 0;
}

// T240 / C1-b - CAN THE ENGINE ORIENT THIS BODY? Read (crash dump + decompile), not Confirmed.
//
// MedicalSystem::addWound (RVA 0x64FE40, build/decomp_64fe40.txt lines 416-435) does this,
// with NO null check on either node:
//     anim = *(victim + 0x448)
//     if (*(char*)(anim + 0x88) != 0) {
//         entity = *(MovableObject**)(*(anim + 0xE8) + 0xD8)
//         node   = entity ? entity->getParentSceneNode() : <garbage>
//         Ogre::Node::getOrientation(node)        <-- node == 0 is what killed instance B
//         if (no harpoon)                          <-- always, for melee
//             the SAME hops on the ATTACKER's +0x448, with NO gate on the attacker's own
//             +0x88, and getOrientation on that node too (review 2026-09-22 H4)
//     }
// The victim there had been re-spawned 0.5 s earlier with its appearance change still in
// flight: the entity existed and nothing had attached it to a scene node yet. So this walks
// the same hops first, and the caller declines to play a hit it cannot survive.
//
// The reads are guarded the way every other read in this file is (F051): `Obj` - range,
// alignment, and a vtable inside the game image - for the character, the AnimationClass and
// the appearance; `ObjInModule(.., "OgreMain_x64.dll")` for the entity, whose vtable is in
// Ogre's image (C1-b H1). "Attached" is Ogre's own exported getParentSceneNode (C1-b L4).
int AppearanceBodyChain(::Character* c, bool honourGate, void** animOut, void** appOut,
                        void** entOut, int* reasonOut, int* gateOut)
{
    if (animOut != 0) *animOut = 0;
    if (appOut  != 0) *appOut  = 0;
    if (entOut  != 0) *entOut  = 0;
    if (reasonOut != 0) *reasonOut = kHitBodyAttached;
    if (gateOut != 0) *gateOut = 0;

    void* anim = 0;
    if (c != 0 && Obj(c)) anim = *(void**)((char*)c + kCharAnimationOff);
    if (!Obj(anim)) { if (reasonOut != 0) *reasonOut = kHitNoAnim; return 0; }
    if (animOut != 0) *animOut = anim;

    // The engine's own gate on the whole node read. For the VICTIM (honourGate) a 0 gate means
    // the wound path never reaches the entity, so nothing below can fault and the hit is
    // playable whatever the rest says. For the ATTACKER the gate that matters is the victim's,
    // so this byte is reported and otherwise ignored. The rest is still READ either way, so the
    // report line can show what the body actually looked like.
    const bool gate = (*((unsigned char*)anim + kAnimWoundGateOff) != 0);
    if (gateOut != 0) *gateOut = gate ? 1 : 0;
    const bool nodeRead = gate || !honourGate;

    void* app = *(void**)((char*)anim + kAnimAppearanceOff);
    if (!Obj(app))
    {
        if (reasonOut != 0) *reasonOut = nodeRead ? kHitNoAppearance : kHitBodyNoNodeRead;
        return nodeRead ? 0 : 1;
    }
    if (appOut != 0) *appOut = app;

    // The same two flags AppearanceSettled reads: while either is set the engine still has an
    // appearance change to apply, which is precisely the window the crash landed in.
    if (*((unsigned char*)app + kUpdAttachOff) != 0 || *((unsigned char*)app + kUpdAppOff) != 0)
    {
        if (reasonOut != 0) *reasonOut = nodeRead ? kHitAppearancePending : kHitBodyNoNodeRead;
        return nodeRead ? 0 : 1;
    }

    // Null OR unreadable counts as "no entity": either way there is no body to take a node
    // from, and `ObjInModule` is the only honest way to ask the second question without faulting.
    void* ent = *(void**)((char*)app + kAppearanceEntOff);
    if (!ObjInModule(ent, kOgreModule))
    {
        if (reasonOut != 0) *reasonOut = nodeRead ? kHitNoEntity : kHitBodyNoNodeRead;
        return nodeRead ? 0 : 1;
    }
    if (entOut != 0) *entOut = ent;

    // An unresolved export or a faulting call both answer "no node": refuse, do not risk.
    GetParentSceneNodeFn getParent = GetParentSceneNode();
    void* node = 0;
    if (getParent == 0 || CallGetParentSceneNode(getParent, ent, &node) == 0 || node == 0)
    {
        if (reasonOut != 0) *reasonOut = nodeRead ? kHitDetached : kHitBodyNoNodeRead;
        return nodeRead ? 0 : 1;
    }

    if (reasonOut != 0) *reasonOut = kHitBodyAttached;
    return 1;
}

int AppearanceBodyAttached(::Character* c, void** animOut, void** appOut, void** entOut,
                           int* reasonOut, int* gateOut)
{
    return AppearanceBodyChain(c, true, animOut, appOut, entOut, reasonOut, gateOut);
}

// R1-a - HAS THE ENGINE FINISHED BUILDING THIS CHARACTER? Read (decompiles), not Confirmed live.
// Source: build/read-r1.md Q3/Q4.
//   * 0x5B8910 (AnimationClass "set body") stores the entity at anim +0xA8 and sets anim +0x88 = 1
//     LAST; called with entity 0 it clears +0xA8 and sets +0x88 = 0.
//   * 0x538590 is its only real-entity caller - set(app +0x138 = anim, app +0xD8) - and it writes
//     isCreatingBody (app +0xE8) = 0. The rebuild virtuals 0x539C50 / 0x539440 set it to 1, detach
//     the entity and zero +0xD8, and the build itself is a deferred work item (asynchronous).
// So "built" = the Chain is attached (anim, appearance, both settle flags clear, Ogre entity, scene
// node - with the gate NOT honoured, so a 0 gate cannot pass it), AND no rebuild is in flight, AND
// the gate byte is exactly 1, AND both sides agree on the body: anim +0xA8 == app +0xD8 (lookAt
// reads the first, addWound the second) and app +0x138 points back at this anim.
// F157 still holds: isCreatingBody == 0 is NOT "settled" on its own - it is one necessary condition
// here, beside the settle flags the Chain already reads.
int CharacterBuilt(::Character* c, int* reasonOut)
{
    if (reasonOut != 0) *reasonOut = kNotBuiltNoChain;
    void* anim = 0;
    void* app  = 0;
    void* ent  = 0;
    if (AppearanceBodyChain(c, false, &anim, &app, &ent, 0, 0) == 0
        || anim == 0 || app == 0 || ent == 0)
        return 0;

    // anim and app passed `Obj` inside the Chain, so their own fields are readable (F051 style).
    if (*((unsigned char*)app + kIsCreatingOff) != 0)
    { if (reasonOut != 0) *reasonOut = kNotBuiltCreating; return 0; }
    if (*((unsigned char*)anim + kAnimWoundGateOff) != 1)
    { if (reasonOut != 0) *reasonOut = kNotBuiltGate; return 0; }
    if (*(void**)((char*)anim + kAnimEntityOff) != ent
        || *(void**)((char*)app + kAppearanceAnimOff) != anim)
    { if (reasonOut != 0) *reasonOut = kNotBuiltMismatch; return 0; }

    if (reasonOut != 0) *reasonOut = kCharBuilt;
    return 1;
}

long long AppearanceWaitNotBuiltCount()
{
    return g_appearanceWaitNotBuilt;
}

long long ClothingWaitAppearanceCount()
{
    return g_clothingWaitAppearance;
}

// R1-a-b (review-r1a M1). Offsets and sources: the kCharNameTagOff block above (Read). Every hop
// setProneState takes unchecked must be an object; the two it guards itself (+0x638 null, and
// +0x3A8 only when +0x3B0 is set) must be readable when present. The scene node is NOT asked for,
// so a character with no body (0x5C8A00 unbinds it when Character +0x608 is cleared) passes.
int CharacterProneSafe(::Character* c, int* reasonOut)
{
    if (reasonOut != 0) *reasonOut = kProneUnsafeChar;
    if (c == 0 || !Obj(c)) return 0;
    void* anim = *(void**)((char*)c + kCharAnimationOff);
    if (!Obj(anim)) { if (reasonOut != 0) *reasonOut = kProneUnsafeAnim; return 0; }
    // anim passed `Obj`, so its own fields are readable (F051 style, as in CharacterBuilt).
    if (!Obj(*(void**)((char*)anim + kAnimAppearanceOff)))
    { if (reasonOut != 0) *reasonOut = kProneUnsafeAppearance; return 0; }
    void* movement = *(void**)((char*)c + kCharMovementOff);
    if (!Obj(movement)) { if (reasonOut != 0) *reasonOut = kProneUnsafeMovement; return 0; }
    if (!Obj(*(void**)((char*)c + kCharAiOff))) { if (reasonOut != 0) *reasonOut = kProneUnsafeAi; return 0; }
    /* R1-a-c (re-check-r1ab Q1): 0x660FE0 does not just read +0x3A8 - it calls three of its virtuals
       (vtable +0xD0/+0x368/+0x390) and walks ITS +0x448 -> +0xE8 chain (decomp_660fe0.txt:43,79-92). So it
       must be a real game object with that chain, not merely readable memory. Read, not Confirmed live. */
    if (*(void**)((char*)movement + kMovementFlagOff) != 0)
    {
        void* mo = *(void**)((char*)movement + kMovementObjOff);
        void* moAnim = Obj(mo) ? *(void**)((char*)mo + kCharAnimationOff) : 0;
        if (!Obj(mo) || !Obj(moAnim) || !Obj(*(void**)((char*)moAnim + kAnimAppearanceOff)))
        { if (reasonOut != 0) *reasonOut = kProneUnsafeMovementObj; return 0; }
    }
    void* tag = *(void**)((char*)c + kCharNameTagOff);
    if (tag != 0 && !Ptr(tag)) { if (reasonOut != 0) *reasonOut = kProneUnsafeNameTag; return 0; }
    if (reasonOut != 0) *reasonOut = kProneSafe;
    return 1;
}

std::string AppearanceString(::Character* c)
{
    if (c == 0 || !Obj(c)) return "look=UNREADABLE(char)";
    void* anim = *(void**)((char*)c + kCharAnimationOff);
    if (!Obj(anim)) return "look=UNREADABLE(anim)";
    void* app = *(void**)((char*)anim + kAnimAppearanceOff);
    if (!Obj(app)) return "look=UNREADABLE(appearance)";

    unsigned char female   = *((unsigned char*)app + kFemaleOff);
    unsigned char shaved   = *((unsigned char*)app + kShavedOff);
    unsigned char barefoot = *((unsigned char*)app + kBarefootOff);
    float height   = *(float*)((char*)app + kHeightOff);
    float height01 = *(float*)((char*)app + kHeight01Off);

    // The chosen body mesh - the field that most directly answers "is this the same person",
    // because chooseBodyMesh() picks it from the race's pool per instance.
    const std::string& body = *(const std::string*)((char*)app + kBodyFilenameOff);

    // Count of things hung on the character: clothing, weapons, hair meshes. A count, not a
    // list - iterating the engine's boost map from here is not worth the risk yet - but it
    // is enough to see "one is wearing a hat and the other is not", which is precisely what
    // the user reported and what nothing here could measure.
    const AppearanceBase* ab = (const AppearanceBase*)app;
    long long attached = (long long)ab->attachments.size();

    // F157: `building` is NOT the settle flag - it reads 0 while the values are still
    // defaults. updAttach/updApp are. All three are printed so a reading can never be
    // mistaken for a verdict; a probe that cannot say which kind of reading it took is
    // exactly the sort of number F155 is about.
    unsigned char building     = *((unsigned char*)app + kIsCreatingOff);
    unsigned char updAttach    = *((unsigned char*)app + kUpdAttachOff);
    unsigned char updApp       = *((unsigned char*)app + kUpdAppOff);

    std::string out = "female=" + S(female)
        + " race='"  + DataName(app, kRaceOff) + "'"
        + " body='"  + body                    + "'"
        + " hair='"  + DataName(app, kHairOff) + "'"
        + " shaved="   + S(shaved)
        + " barefoot=" + S(barefoot)
        + " height="   + F2(height)
        + " height01=" + F2(height01)
        + " attached=" + S(attached)
        + " settled="  + S((updAttach == 0 && updApp == 0) ? 1 : 0)
        + " building=" + S(building)
        + " updAttach=" + S(updAttach)
        + " updApp="    + S(updApp);

    // F159: `suid` (+0x144) and `appearanceData` (+0x148) were printed by the first version
    // and returned junk (0, 1, and 1065353216 - the bit pattern of 1.0f) and a constant '0'.
    // They are gone rather than left in: a field that reads as one thing and reports another
    // is worse than no field (lesson 1).
    const int humanClass = HumanClassOf(app);   // T-293 fold 1 (review F4): the three answers, not two
    if (humanClass == coophuman::kHumanYes)
    {
        out += std::string(" human=1")
            + " beard='" + DataName(app, kBeardOff) + "'"
            + " bulk=" + F2(*(float*)((char*)app + kBulkOff))
            + " musc=" + F2(*(float*)((char*)app + kMuscleOff))
            + " skin=" + F2(*(float*)((char*)app + kSkinnyOff));
    }
    else
    {
        out += (humanClass < 0) ? " human=?" : " human=0";   // ? = no AppearanceHumanVt row or an unreadable vtable
    }
    return out;
}

// PROBE P021 - WHAT THE CHARACTER IS WEARING AND CARRYING, by name.
//
// The last measured divergence after H010b (F165): `attached` matched 1/4. But a COUNT is not
// a diff - two characters can both be wearing four things and none of them the same. T051's
// whole lesson was that an instrument which cannot name what it sees cannot verify a fix, so
// the clothing work gets its instrument before it gets its fix.
//
// The names are SORTED, deliberately. The engine stores items in a lektor whose order comes
// out of per-process allocation, so an unsorted list would differ between two instances that
// are carrying exactly the same things - a false divergence, which in this project is the
// most expensive kind of number there is (lesson 1).
//
//   Character +0x2E8 -> Inventory*
//   Inventory +0x010 -> lektor<Item*> _allItems
//   Item : InventoryItemBase : RootObject, so getRecordDirect()->name is the item's identity.
// T055 FAILURE AND ITS CORRECTION. The first version of this printed `items=0 []` on all eight
// readings - on BOTH instances, including the authority, which no wire-format problem can
// cause. And it printed nothing else: a well-formed line, a zero, and no way to tell "the
// inventory is empty" from "the pointer is wrong" from its own output. **A probe that fails
// silently is the exact defect class this project keeps paying for** (F155), and I shipped one.
//
// So this version reports its own INPUTS beside its answer: the inventory pointer, the raw
// `_allItems` count, and a walk of the section map with each section's name and item count.
// Those distinguish every case that `items=0` alone conflates - and the section walk is also
// the likely place the items actually live, since `_allItems` may be a lazily-filled cache.
std::string GearString(::Character* c)
{
    if (c == 0 || !Obj(c)) return "gear=UNREADABLE(char)";

    void* inv = *(void**)((char*)c + kInventoryOff);
    if (!Obj(inv)) return "gear=UNREADABLE(inventory ptr=" + Hex((uintptr_t)inv) + ")";

    std::vector<std::string> names;

    // Source 1: the flat list. T055 measured this reading 0 on characters that were visibly
    // carrying things, so its count is reported whatever it says.
    const lektor<Item*>& all = *(const lektor<Item*>*)((char*)inv + kAllItemsOff);
    unsigned int flat = all.size();
    if (flat <= 512)
        for (unsigned int i = 0; i < flat; ++i)
        {
            Item* it = all.begin()[i];
            if (!Obj(it)) { names.push_back("?flat"); continue; }
            ::GameData* gd = it->getRecordDirect();
            names.push_back(Obj(gd) ? gd->name : std::string("?"));
        }

    // Source 2: the SECTIONS. Each holds its own item vector, and this is where equipped
    // clothing lives if the flat list is a cache. Section names are printed too - "Armour",
    // "Weapon" and so on tell us WHICH slots differ, not merely that something does.
    std::string sectionDump;
    long long sectionCount = 0, sectionItems = 0;
    {
        typedef boost::unordered::unordered_map<std::string, InventorySection*,
            boost::hash<std::string>, std::equal_to<std::string>,
            Ogre::STLAllocator<std::pair<std::string const, InventorySection*>,
            Ogre::GeneralAllocPolicy> > SecMap;
        const SecMap& secs = *(const SecMap*)((char*)inv + kSectionsOff);
        if (secs.size() <= 64)
        {
            for (SecMap::const_iterator it = secs.begin(); it != secs.end(); ++it)
            {
                ++sectionCount;
                InventorySection* sec = it->second;
                if (!Ptr(sec)) { sectionDump += " " + it->first + "=?"; continue; }
                const Ogre::vector<InventorySection::SectionItem>::type& si = sec->items;
                sectionDump += " " + it->first + "=" + S((long long)si.size());

                // F199 - WHY IS THIS SECTION ALWAYS EMPTY?
                //
                // `belt`, `main`, `backpack_attach`, `hip` and `flat` have read 0 in every run
                // from T056 to T062 - SEVEN runs - and I produced two theories about what would
                // fill them, both of which the game refuted. A third theory is not what is
                // missing; these fields are, and they were in the header the whole time:
                //
                //   enabled              (+0xD0) - a DISABLED section can never hold anything,
                //                                  which would make "always 0" correct behaviour
                //   isAnEquippedItemSection (+0xB4) - worn/equipped vs plain storage
                //   limitedSlot          (+0xB8) - the AttachSlot it accepts (0=WEAPON, 1=BACK,
                //                                  3=HAT, 6=LEGS, 9=BOOTS, 14=BELT, 7=NONE)
                //   itemsLimit           (+0xA8) - a limit of 0 also means "nothing fits"
                //
                // Printed only when the section is EMPTY: a section holding items has already
                // answered the question, and this keeps the line from doubling in length.
                if (si.empty())
                {
                    const char* sb = (const char*)sec;
                    sectionDump += "(en=" + S((int)*(const unsigned char*)(sb + 0xD0))
                                 + ",eq=" + S((int)*(const unsigned char*)(sb + 0xB4))
                                 + ",slot=" + S(*(const int*)(sb + 0xB8))
                                 + ",lim=" + S(*(const int*)(sb + 0xA8))
                                 + ",wh=" + S(*(const int*)(sb + 0x30))
                                 + "x" + S(*(const int*)(sb + 0x34)) + ")";
                }
                if (si.size() > 512) continue;
                for (size_t k = 0; k < si.size(); ++k)
                {
                    ++sectionItems;
                    Item* item = si[k].item;
                    if (!Obj(item)) { names.push_back("?sec"); continue; }
                    ::GameData* gd = item->getRecordDirect();
                    names.push_back(Obj(gd) ? gd->name : std::string("?"));
                }
            }
        }
        else sectionDump = " sections=IMPLAUSIBLE(" + S((long long)secs.size()) + ")";
    }

    // Sorted, deliberately: the engine's item order comes out of per-process allocation, so an
    // unsorted list would show a false divergence between two identically dressed characters.
    std::sort(names.begin(), names.end());
    std::string list;
    for (size_t i = 0; i < names.size(); ++i)
        list += (i ? "|" : "") + names[i];

    return "items=" + S((long long)names.size())
         + " flat=" + S(flat)
         + " sections=" + S(sectionCount)
         + " sectionItems=" + S(sectionItems)
         + " inv=" + Hex((uintptr_t)inv)
         + " [" + list + "]"
         + " secs:" + (sectionDump.empty() ? std::string(" none") : sectionDump);
}

bool ReportAppearance(unsigned int uid)
{
    ::Character* c = FindSpawned(uid);
    if (c == 0)
    {
        ErrorLog("[P019] look: no spawned character for uid " + S(uid));
        return false;
    }
    DebugLog("[P019] uid=" + S(uid) + " " + AppearanceString(c));
    DebugLog("[P021] uid=" + S(uid) + " " + GearString(c));
    return true;
}

// ---------------------------------------------------------------------------------------
// H010b: replicate the appearance RECORD. The apply lives in appearance_record.cpp; this
// file owns only the WHEN - which is the half F157 made non-obvious.
// ---------------------------------------------------------------------------------------

void QueueRemoteRecord(unsigned int uid, const RecordCopy& rec)
{
    g_pendingRemote[uid] = rec;
    ++g_c.queued;
}

void WatchLocalRoll(unsigned int uid)
{
    g_pendingLocal[uid] = true;
    ++g_c.watched;
}

// ---- deadlook1 (owner decision 2026-09-26, .modding/02-project-rules.md; F938 / F939; review-deadlook1 fold) ---------
// A DEAD HUMAN copy gets its owner's WORN CLOTHING only. The clothing apply re-attaches items (+0x141 -> buildAttachments,
// no createBody, no forced ragdoll OFF: answers-deadlook2 R1/R4a, Read), so the corpse stays lying. The APPEARANCE apply is
// never made on a corpse (setAppearanceData -> createBody -> ragdoll OFF: the corpse jumps, F939) - its gate refuses a dead
// copy outright (review D4a). Dead animals keep waiting (the owner's decision; T293 is the animal body rebuild).
// WORN SLOTS ONLY (fold 1): ApplyWornGarments (clothing.cpp) replaces shirt / legs / armour / head / boots and leaves every
// other item where it is - the engine drops a corpse's backpack and weapons after death (F530(d)); re-creating them would
// duplicate them.
// LOOTED = TOUCHED (fold 2): the item-sync layer moved an item onto or off this copy - an owner's ITEM_MOVE applied to it
// (items.cpp ApplyItemMove) or a move this game's player made on it, undone here and asked of the owner (items.cpp
// ItCrossTransfer). Set on the MAIN THREAD at any time, alive or dead: the clothing snapshot is the owner's spawn-settle
// one, so any later move makes it stale. A looted dead copy is held for good, never re-dressed. Forgotten, with every
// other per-uid mark here, when the copy is removed (DeadlookForgetCopy from spawn.cpp RemoveLocalCopy - review D3a).
// SETTLE (review D3b): a dead copy is dressed only once it has been seen dead for kDeadSettleMs, so the engine's own death
// drop (dropGearOnDeath 0x5CAD80, message-driven, F530(d)) has run first.
std::map<unsigned int, bool>  g_deadTouched;      // uid -> the item-sync layer moved an item onto / off this copy
std::map<unsigned int, DWORD> g_deadFirstSeen;    // uid -> GetTickCount() the first time it was seen dead
std::map<unsigned int, int>   g_deadHeldReason;   // uid -> the hold reason last counted (once per uid per reason)
// T-1 B2 fold (review F2): kits held because this game still holds one of the copy's items by pointer (KitApplyBusy) - once per
// uid per wait; by the first reason seen (window / cursor / request); the uids waiting now.
long long g_kitApplyDeferredOpen = 0;
long long g_kitDeferredWhy[3] = { 0, 0, 0 };
std::map<unsigned int, bool> g_waitKitOpen;
long long g_kitOwnedSkipped = 0;   // kit3 (kit2 review + T522, review D3): queued kits dropped - the uid is this game's own now
long long g_deadClothingApplied = 0;   // dead human copies dressed with the owner's worn clothing
long long g_deadLooted = 0;            // holds entered: a dead human copy whose items the item sync moved - never re-dressed
long long g_deadAnimalHeld = 0;        // holds entered: a dead copy whose appearance object is readable and NOT human
long long g_deadNotHuman = 0;          // holds entered: a dead copy that could not be PROVEN human (no appearance object,
                                       // no AppearanceHumanVt row, or an unreadable vtable - T-293)
long long g_deadCarriedHeld = 0;       // holds entered: a dead human copy carried (+0x3D4), in a bed or cage (+0x2F8), or
                                       // unreadable there
long long g_deadRebuildQueued = 0;     // review D1: createBody pending (+0x143) right after a dead copy's clothing apply
                                       // (expected 0 - the clothing path sets only +0x141)
long long g_kitStaleDropped = 0;       // T-293 fold 1 (review F1): waiting clothing lists dropped - the owner moved a worn item since
long long g_kitStaleHeldCorpse = 0;    // T-293 fold 2 (F2-B): waiting clothing lists dropped - the guard holds that corpse's createBody
long long g_kitStaleLines = 0;         // kitStale log lines, both reasons (50 at most)

const size_t kDeadCarriedOff       = 0x3D4;   // Character : bool _isBeingCarried (spawn.cpp kBeingCarriedOff)
const size_t kDeadInSomethingOff   = 0x2F8;   // Character : int inSomething, 1 bed, 2 cage (spawn.cpp kCarryInSomethingOff)
const size_t kCreateBodyPendingOff = 0x143;   // AppearanceBase : byte, set = createBody runs next update (answers-deadlook2 R1)
const DWORD  kDeadSettleMs         = 2000;    // review D3b

const int kDeadGo = 0, kDeadHeldAnimal = 1, kDeadHeldNotHuman = 2, kDeadHeldCarried = 3, kDeadHeldRebuild = 4;
const int kDeadHeldSettling = 5, kDeadLooted = 6, kDeadNotDead = 7;
const int kDeadHeldCorpse = 8;   // T-293 fold 2 (F2-B): the createBody guard holds this corpse's rebuild - for good (a corpse stays limp)

// 1 dead, 0 alive, -1 unreadable. hasDied 0x620B20 reads +0x5BC (answers-deadlook2, Confirmed bytes). No C++ object here (C2712).
static int DeadPod(::Character* c)
{
    if (c == 0 || !Obj(c)) return -1;
    __try { return c->hasDied() ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// THE HUMAN TEST: the class check this file already uses, HumanClassOf / IsHumanAppearance (T-293: the appearance
// object's vtable pointer is AppearanceHuman's, row AppearanceHumanVt; until T-293 it answered 0 for every human).
// 1 human, 0 a readable appearance of another class (an animal), -1 no appearance object / no row / an unreadable vtable
// (= not human). HumanClassOf guards its own reads.
static int DeadHumanPod(::Character* c)
{
    void* app = GetAppearance(c);
    if (app == 0) return -1;
    return HumanClassOf(app);
}

// 1 carried or in a bed / cage, 0 neither, -1 unreadable. No C++ object here (C2712).
static int DeadHeldPod(::Character* c)
{
    unsigned char carried = 0;
    int inSomething = 0;
    __try
    {
        carried = *((unsigned char*)c + kDeadCarriedOff);
        inSomething = *(int*)((char*)c + kDeadInSomethingOff);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    return (carried != 0 || inSomething != 0) ? 1 : 0;
}

// 1 = createBody is pending on the appearance object (+0x143), 0 = not, -1 = no appearance object / fault. No C++ object here.
static int CreateBodyPendingPod(::Character* c)
{
    void* app = GetAppearance(c);
    if (app == 0) return -1;
    __try { return *((unsigned char*)app + kCreateBodyPendingOff) != 0 ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// Starts the settle clock the first time this uid is seen dead (both look loops call it at their gate).
static void DeadNoteSeen(unsigned int uid, ::Character* c)
{
    if (g_deadFirstSeen.find(uid) != g_deadFirstSeen.end()) return;
    if (DeadPod(c) != 1) return;
    g_deadFirstSeen[uid] = ::GetTickCount();
    DebugLog("[P022] deadlook: uid=" + S(uid) + " first seen dead"
             + (g_deadTouched.find(uid) != g_deadTouched.end() ? " (its items were already moved by the item sync)" : ""));
}

// Asked of a copy DeadPod says is dead, live at the moment of the apply. kDeadGo = dress it now.
static int DeadClothingVerdict(unsigned int uid, ::Character* c)
{
    DeadNoteSeen(uid, c);
    const int human = DeadHumanPod(c);
    if (human == 0) return kDeadHeldAnimal;
    if (human != 1) return kDeadHeldNotHuman;
    if (DeadHeldPod(c) != 0) return kDeadHeldCarried;
    if (g_deadTouched.find(uid) != g_deadTouched.end()) return kDeadLooted;
    std::map<unsigned int, DWORD>::const_iterator fs = g_deadFirstSeen.find(uid);
    if (fs == g_deadFirstSeen.end()) return kDeadHeldSettling;
    if ((DWORD)(::GetTickCount() - fs->second) < kDeadSettleMs) return kDeadHeldSettling;
    // T-293 fold 2 (F2-B): a corpse whose createBody the guard holds is held for good (+0x143 never clears on it), so waiting for
    // its rebuild would keep the list forever - its own outcome: the caller drops the list.
    if (CopyBodyHeldNowPod(c, 1) != 0) return kDeadHeldCorpse;
    if (!CharacterBuilt(c, 0) || AppearanceRebuildInFlight(c) != 0) return kDeadHeldRebuild;
    return kDeadGo;
}

// Counts a hold once per uid per reason (a hold that changes reason counts again). Rebuild / settle holds are short and
// are not counted.
static void NoteDeadHeld(unsigned int uid, int why)
{
    std::map<unsigned int, int>::iterator h = g_deadHeldReason.find(uid);
    if (h != g_deadHeldReason.end() && h->second == why) return;
    g_deadHeldReason[uid] = why;
    if (why == kDeadHeldAnimal) ++g_deadAnimalHeld;
    else if (why == kDeadHeldNotHuman) ++g_deadNotHuman;
    else if (why == kDeadHeldCarried) ++g_deadCarriedHeld;
    else if (why == kDeadLooted)
    {
        ++g_deadLooted;
        DebugLog("[P022] deadlook: uid=" + S(uid) + " LOOTED (the item sync moved items on this copy) - never re-dressed");
    }
}

// MAIN THREAD (items.cpp ApplyItemMove, after the move was applied): an owner item move landed on this copy.
// Only a move in a WORN section of a copy that is already dead marks it looted (fold 3, re-check D-a/D-b):
// - a living copy's gear changes are the owner's own and are carried by the appearance sync;
// - the owner's own death drop (backpack, weapons) never touches a worn section, so it does not mark;
// - a player looting the corpse's clothing does: on this game the take is undone and asked of the owner, and the
//   owner's applied move comes back here through ApplyItemMove;
// - an unreadable dead flag is not proof of alive, so it marks (fails toward "never re-dressed").
void DeadlookNoteItemTouch(unsigned int uid, const std::string& section)
{
    if (uid == 0 || !ClothingIsWornSection(section)) return;
    ::Character* c = FindSpawned(uid);
    if (c != 0 && DeadPod(c) != 0) g_deadTouched[uid] = true;
}

// T-293 fold 1 (review F1). MAIN THREAD (items.cpp ApplyItemMove, before the copy is found or the move tried). The owner is the
// authority: its add / remove / split in a WORN section means a clothing list still waiting for this copy describes gear it no
// longer wears. The list is DROPPED - applied or not, dead or alive - not trimmed: the apply rebuilds every worn slot (clearAll /
// ClearWornSections), so a list without the moved slot would still undo the owner's move on the copy, and the owner never re-sends
// clothing after a live strip (only at first settle and after a wandering restock). The copy keeps what it wears plus the owner's
// moves. MSG_CLOTHING and MSG_ITEM_MOVE both ride CH_RELIABLE and are handled as they arrive (session.cpp OnClothing /
// OnItemMove), so a list queued AFTER this move was sent after it and is applied as usual - no separate "stale" mark is needed.
// Quantity (op 2) and charges (op 4) change no item's place and keep the list.
void GarmentsNoteOwnerMove(unsigned int uid, const std::string& section, int op)
{
    if (uid == 0 || (op != 0 && op != 1 && op != 3) || !ClothingIsWornSection(section)) return;
    std::map<unsigned int, GarmentSet>::iterator it = g_pendingGarments.find(uid);
    if (it == g_pendingGarments.end()) return;
    g_pendingGarments.erase(it);
    g_waitClothingForAppearance.erase(uid);
    g_waitNotBuiltClothing.erase(uid);
    g_waitDownedClothing.erase(uid);
    g_waitKitOpen.erase(uid);
    ++g_kitStaleDropped;
    if (++g_kitStaleLines <= 50)
        DebugLog("[P022] CLOTHING uid=" + S(uid) + " DROPPED before it was applied - the owner moved an item in its '" + section
                 + "' section (op " + S((long long)op) + ") since the list was sent; the copy keeps its gear plus the owner's"
                 " moves (T-293 fold 1, kitStale[droppedOnOwnerMove]; 50 lines at most)");
}

// MAIN THREAD (spawn.cpp RemoveLocalCopy): the copy is gone - its dead-copy marks describe that copy only (review D3a).
void DeadlookForgetCopy(unsigned int uid)
{
    g_deadTouched.erase(uid);
    g_deadFirstSeen.erase(uid);
    g_deadHeldReason.erase(uid);
    g_copyLookApplied.erase(uid);   // a re-spawned copy's first look is its own
    g_lookKoThrough.erase(uid);
    CopyBodyForgetUid(uid);   // T-293 fold 2 (F3-b): the createBody guard's marks describe this copy only
}

// MAIN THREAD (spawn.cpp RepeatSpawnAct, a stale copy made again): the new body has had no owner's record applied - the old
// body's mark goes, so its clothing waits for its own APPEARANCE as a fresh copy's does.
void AppearanceForgetCopyBody(unsigned int uid)
{
    g_appearanceDone.erase(uid);
}

// MAIN THREAD (off it: counted, never marked). The mod has just made an apply that may queue a body rebuild on
// `uid` (a look, kit or limb apply): when the rebuild-pending byte (+0x143) reads set now - or cannot be read - the uid is
// marked, so the createBody guard holds that rebuild in ragdoll even if the uid becomes this game's own before it runs.
void CopyBodyNoteModApply(unsigned int uid, ::Character* c)
{
    const unsigned long m = StoreMainThreadId();
    if (m == 0 || ::GetCurrentThreadId() != (DWORD)m) { ::InterlockedIncrement(&g_copyBodyModMarkOffMain); return; }
    if (uid == 0 || c == 0) return;
    if (CreateBodyPendingPod(c) != 0) CopyBodyModMark(uid);
}

// MAIN THREAD (spawn.cpp SpawnWorldTeardown): the marks above, for every copy at once - the world's copies all go with it.
void DeadlookForgetAllCopies()
{
    g_deadTouched.clear();
    g_deadFirstSeen.clear();
    g_deadHeldReason.clear();
    g_copyLookApplied.clear();   // a copy met in the next world with the same uid: its first look is its own
    g_lookKoThrough.clear();
    CopyBodyForgetAll();
}

// Defined below; declared here because the tick drives it. Ordering inside the tick is
// deliberate - see the comment on the definition.
void ApplyPendingGarments();

// P10 TEST-ONLY LEVER - `bodydown <anchorUid> [radiusM]` (final-form test code: nothing happens unless the test tool sends
// it). Proves crash2's createBody guard (F986) in one run by making the crash's situation on purpose. Sent to BOTH games;
// each does whichever half it can:
//  owner half - the living, standing ANIMAL this game drives nearest the anchor (<= radiusM, default 300 m) is knocked
//    out with the engine's own MedicalSystem::knockout and its STATE is pushed (medical.cpp KnockOutOwned), so the OTHER
//    game's copy of it is taken down by its own engine (ApplyLatch -> collapse -> ragdoll, the T424 road); the knockout
//    is checked again one lever tick and 2 s later (knockout only sets the wake-up clock; the medical update does the rest);
//  copy half - for 120 s, the first living ANIMAL copy within radiusM of the anchor that the guard would hold (a body and
//    a ragdoll object, CopyBodyReadPod) is handed its own appearance record again (ReassertAppearanceData): the
//    setAppearanceData call our APPEARANCE apply makes, which sets +0x143, so the engine's own AppearanceBase::update
//    calls createBody through vt+0x58 while the copy is down. Then it watches +0x143 until createBody has run.
// P10 fold 2: "an animal" is P10AnimalPod - the appearance object's vtable POINTER is AppearanceAnimal's (address-table
// row AppearanceAnimalVt). Fold 1 looked for AppearanceAnimal::createBody 0x539C50 INSIDE the vtable, which never
// matched: every vtable slot holds a 5-byte jump stub (E9 rel32), never the function's own address (T-293; slot 11 =
// 1.0.65 0x3151B -> 0x539C50, 1.0.68 0x31539 -> 0x53A6E0). (T633: the human test IsHumanAppearance read human=0 for a
// Greenlander on 1.0.68, so the old "not human" filter let both players' own characters through.) Never the anchor, never a player
// character (isPlayerControlled). Distances: 10 world units = 1 m (the project's scale, Inferred) - the first build's
// "100 m" was 100 units, about 10 m.
// Pass: copyBody deferred > 0, then resumed > 0 once the copy stands, and no crash. MAIN THREAD only.
static const DWORD kP10WindowMs = 120000;      // copy half: how long to wait for an animal copy in ragdoll
static const DWORD kP10WatchMs  = 180000;      // after the request: how long to watch for the rebuild
static const DWORD kP10TickMs   = 100;
static const DWORD kP10KoCheck2Ms = 2000;      // the second knockout check
static const float kP10UnitsPerM = 10.0f;      // world units per metre (Inferred scale, items.cpp traderspawn note)
static float        g_p10RadiusM  = 300.0f;
static float        g_p10RangeSq  = 3000.0f * 3000.0f;   // world units squared
static unsigned int g_p10Anchor   = 0;
static DWORD        g_p10ArmedAt  = 0;         // 0 = not armed
static unsigned int g_p10FiredUid = 0;         // the copy asked to rebuild (0 = none yet)
static DWORD        g_p10FiredAt  = 0;
static DWORD        g_p10LastTick = 0;
static unsigned int g_p10KoUid    = 0;         // the owned animal knocked out, still being checked (0 = none)
static DWORD        g_p10KoAt     = 0;
static int          g_p10KoChecks = 0;
static bool         g_p10KoTook   = false;
static float        g_p10CopyNearestSq  = -1.0f;   // copy half: the nearest living animal copy seen in this window
static unsigned int g_p10CopyNearestUid = 0;
static LONG g_p10ClassAns[3] = { 0, 0, 0 };    // the animal test's answers per call: [yes, no, unreadable/no row]
static bool g_p10ClassNoted = false, g_p10NoRowNoted = false, g_p10UnreadNoted = false;
static LONG g_p10Arms = 0, g_p10Knocks = 0, g_p10Requests = 0, g_p10Rebuilt = 0, g_p10Timeouts = 0;

static DWORD P10Now() { const DWORD t = ::GetTickCount(); return t != 0 ? t : 1; }

static std::string P10F(float v)
{
    char b[32];
    sprintf_s(b, sizeof(b), "%.2f", (double)v);
    return std::string(b);
}

static std::string P10M(float dsq) { return dsq < 0.0f ? std::string("-1") : P10F((float)::sqrt((double)dsq) / kP10UnitsPerM); }

// P10 fold 2: AppearanceAnimal's vtable (RTTI .?AVAppearanceAnimal@@; Steam_1.0.65 0x16E5468, Steam_1.0.68 0x16E6598).
static unsigned long long kAnimalVtRva = 0;
static AddrReg kAnimalVtRva_reg("AppearanceAnimalVt", &kAnimalVtRva);   /* Steam_1.0.65 0x16E5468 */

// The class identity. vt = the appearance object's vtable pointer (0 = unreadable), base = the game's image base,
// animalVtRva = the row AppearanceAnimalVt (0 = no row). 1 AppearanceAnimal, 0 another class, -1 cannot tell (never
// "animal"). The engine's appearance classes each have one vtable, so the pointer IS the class.
static int AnimalClassDecide(unsigned long long vt, unsigned long long base, unsigned long long animalVtRva)
{
    if (vt == 0 || base == 0 || animalVtRva == 0) return -1;
    return (vt == base + animalVtRva) ? 1 : 0;
}

// THE ANIMAL TEST. 1 = the appearance object's vtable pointer is AppearanceAnimal's (AnimalClassDecide); 0 = another
// class (a human: AppearanceHuman); -1 = no row, no appearance object, or an unreadable vtable - never an animal.
// *vtOut = the vtable pointer read (0 = none). No C++ object here (C2712).
static int P10AnimalPod(::Character* c, uintptr_t* vtOut)
{
    *vtOut = 0;
    if (c == 0) return -1;
    void* app = GetAppearance(c);
    if (app == 0) return -1;
    uintptr_t vt = 0;
    __try { vt = *(uintptr_t*)app; }
    __except (EXCEPTION_EXECUTE_HANDLER) { vt = 0; }
    *vtOut = vt;
    return AnimalClassDecide(vt, (uintptr_t)::GetModuleHandleA(0), kAnimalVtRva);
}

// The image RVA a vtable slot really calls: the slot holds an E9 rel32 jump stub; 0 = unreadable or not a stub.
// No C++ object here (C2712).
static unsigned long long P10SlotTargetRva(uintptr_t vt, int slot)
{
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    __try
    {
        const uintptr_t st = *(uintptr_t*)(vt + slot * sizeof(void*));
        if (!Ptr((void*)st) || *(unsigned char*)st != 0xE9) return 0;
        const uintptr_t t = st + 5 + (intptr_t)*(int*)(st + 1);
        return t > base ? (unsigned long long)(t - base) : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// 1 = a player character (Character::isPlayerControlled), 0 = not, -1 = a fault. No C++ object here (C2712).
static int P10PlayerPod(::Character* c)
{
    if (c == 0) return -1;
    __try { return c->isPlayerControlled() ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// The parts CopyDownedPod folds together, read separately (T633: its composite down=1 alone could not say why).
struct P10Down { int prone; int unc; float koTimer; int ragdoll; };

static void P10DownPod(::Character* c, P10Down* d)
{
    d->prone = -1; d->unc = -1; d->koTimer = -1.0f; d->ragdoll = -1;
    if (c == 0 || !Obj(c)) return;
    void* anim = 0;
    __try
    {
        d->prone = *(int*)((char*)c + kCharProneStateOff);
        d->unc = (*((unsigned char*)c + kCharUnconciousOff) != 0) ? 1 : 0;
        d->koTimer = *(float*)((char*)c + kCharKoTimerOff);
        anim = *(void**)((char*)c + kCharAnimationOff);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!Obj(anim)) return;
    __try { d->ragdoll = c->inRagdoll() ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { d->ragdoll = -1; }
}

static std::string P10DownLine(::Character* c, const P10Down& d)
{
    return " prone=" + S((long long)d.prone) + " unconcious=" + S((long long)d.unc) + " koTimer=" + P10F(d.koTimer)
         + " isRagdoll=" + S((long long)d.ragdoll) + " limp=" + S((long long)(c != 0 ? LimpPod(c) : -1))
         + " down=" + S((long long)(c != 0 ? CopyDownedPod(c) : -1));
}

// Counts every answer; logs once: no row, an unreadable vtable, and the first animal's class (its vtable and what its
// createBody slot 11 (+0x58, the guard's decomp) really jumps to).
static void P10NoteClass(int animal, uintptr_t vt)
{
    ++g_p10ClassAns[animal == 1 ? 0 : (animal == 0 ? 1 : 2)];
    if (kAnimalVtRva == 0)
    {
        if (!g_p10NoRowNoted)
        {
            g_p10NoRowNoted = true;
            DebugLog("[P10] no AppearanceAnimalVt row for this game build - no character is taken as an animal");
        }
        return;
    }
    if (animal < 0)
    {
        if (!g_p10UnreadNoted)
        {
            g_p10UnreadNoted = true;
            DebugLog("[P10] an appearance vtable could not be read (or no appearance object) - not taken as an animal"
                     " (logged once)");
        }
        return;
    }
    if (animal != 1 || g_p10ClassNoted) return;
    g_p10ClassNoted = true;
    const unsigned long long cb = P10SlotTargetRva(vt, 11);
    DebugLog("[P10] first animal recognised by class: vtable " + Hex((unsigned long long)vt) + " = image base + "
             + Hex(kAnimalVtRva) + " (row AppearanceAnimalVt, AppearanceAnimal); slot 11 (+0x58) jumps to "
             + Hex(cb) + (cb != 0 && cb == kAnimalCreateBodyRva
                          ? std::string(" = AppearanceAnimalCreateBody, the guard's function")
                          : std::string(" - NOT the AppearanceAnimalCreateBody row ") + Hex(kAnimalCreateBodyRva)));
}

// Squared horizontal distance in world units; false = the position could not be read.
static bool P10DistSq(::Character* c, const Ogre::Vector3& at, float* dsq)
{
    Ogre::Vector3 v;
    if (!SafeReadPosition(c, &v)) return false;
    const float dx = v.x - at.x, dz = v.z - at.z;
    *dsq = dx * dx + dz * dz;
    return true;
}

static bool P10AnchorPos(Ogre::Vector3* at)
{
    ::Character* a = FindSpawned(g_p10Anchor);
    return a != 0 && SafeReadPosition(a, at);
}

std::string BodyDownLever(unsigned int anchorUid, float radiusM)
{
    if (!(radiusM >= 1.0f && radiusM <= 5000.0f))
        return "error bodydown: radius " + P10F(radiusM) + " m is outside 1..5000";
    Ogre::Vector3 at;
    g_p10Anchor = anchorUid;
    if (!P10AnchorPos(&at))
    {
        g_p10Anchor = 0;
        return "error bodydown: no readable character for anchor uid " + S((long long)anchorUid);
    }
    g_p10RadiusM = radiusM;
    g_p10RangeSq = (radiusM * kP10UnitsPerM) * (radiusM * kP10UnitsPerM);
    // owner half: the nearest owned, living, standing animal that is neither the anchor nor a player character
    unsigned int nearest = 0;
    float nearestSq = -1.0f;
    int owned = 0, notAnimal = 0, players = 0, downOrDead = 0, noPos = 0;
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int u = 0; ::Character* c = 0; float d = 0.0f; uintptr_t avt = 0;
        if (!MirrorSlot(i, &u, &c) || u == 0 || c == 0 || u == anchorUid || !net::IsUidMine(u)) continue;
        ++owned;
        const int animal = P10AnimalPod(c, &avt);
        P10NoteClass(animal, avt);
        if (animal != 1) { ++notAnimal; continue; }
        if (P10PlayerPod(c) != 0) { ++players; continue; }
        if (DeadPod(c) != 0 || CopyDownedPod(c) != 0) { ++downOrDead; continue; }
        if (!P10DistSq(c, at, &d)) { ++noPos; continue; }
        if (nearestSq < 0.0f || d < nearestSq) { nearestSq = d; nearest = u; }
    }
    const unsigned int best = (nearest != 0 && nearestSq <= g_p10RangeSq) ? nearest : 0;
    const int ko = (best != 0) ? KnockOutOwned(best) : -2;   // -2 = no owned animal in range
    ::InterlockedIncrement(&g_p10Arms);
    const DWORD now = P10Now();
    g_p10ArmedAt = now; g_p10FiredUid = 0; g_p10FiredAt = 0; g_p10LastTick = 0;
    g_p10CopyNearestSq = -1.0f; g_p10CopyNearestUid = 0;
    g_p10KoUid = (ko >= 0) ? best : 0; g_p10KoAt = now; g_p10KoChecks = 0; g_p10KoTook = false;
    const std::string line = "anchor=" + S((long long)anchorUid) + " radiusM=" + P10F(radiusM)
        + " ownedAnimal=" + S((long long)best) + " knockout=" + S((long long)ko)
        + " nearestOwnedAnimal=" + S((long long)nearest) + " atM=" + P10M(nearestSq)
        + " owned[all,notAnimal,player,downOrDead,noPos]=" + S((long long)owned) + "," + S((long long)notAnimal) + ","
        + S((long long)players) + "," + S((long long)downOrDead) + "," + S((long long)noPos)
        + " animalTest[yes,no,unreadable]=" + S((long long)g_p10ClassAns[0]) + "," + S((long long)g_p10ClassAns[1]) + ","
        + S((long long)g_p10ClassAns[2]) + " animalVtRow=" + Hex(kAnimalVtRva) + " copyWatchS=" + S((long long)(kP10WindowMs / 1000));
    DebugLog("[P10] lever bodydown armed (TEST) " + line);
    return "ok bodydown " + line;
}

/* T-327 (the eatbite lever's `animal`): the nearest living, standing animal this game DRIVES - P10's animal test, not a player
   character, not the anchor - within radiusM of the anchor; 0 = none. *distSqUnits = its squared distance in world units. MAIN THREAD. */
unsigned int NearestOwnedAnimal(unsigned int anchorUid, float radiusM, float* distSqUnits)
{
    Ogre::Vector3 at;
    ::Character* ac = FindSpawned(anchorUid);
    if (ac == 0 || !SafeReadPosition(ac, &at)) return 0;
    unsigned int nearest = 0;
    float nearestSq = -1.0f;
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int u = 0; ::Character* c = 0; uintptr_t avt = 0;
        if (!MirrorSlot(i, &u, &c) || u == 0 || c == 0 || u == anchorUid || !net::IsUidMine(u)) continue;
        if (P10AnimalPod(c, &avt) != 1 || P10PlayerPod(c) != 0 || DeadPod(c) != 0 || CopyDownedPod(c) != 0) continue;
        Ogre::Vector3 p;
        if (!SafeReadPosition(c, &p)) continue;
        const float dx = p.x - at.x, dy = p.y - at.y, dz = p.z - at.z;
        const float d = dx * dx + dy * dy + dz * dz;
        if (nearestSq < 0.0f || d < nearestSq) { nearestSq = d; nearest = u; }
    }
    const float r = radiusM * kP10UnitsPerM;
    if (nearest == 0 || nearestSq > r * r) return 0;
    if (distSqUnits != 0) *distSqUnits = nearestSq;
    return nearest;
}

static std::string P10GuardCounts()
{
    return " copyBody[deferred,resumed]=" + S((long long)g_copyBodyDeferred) + "," + S((long long)g_copyBodyResumed)
         + " copyBodyHuman[deferred,resumed]=" + S((long long)g_copyBodyHumanDeferred) + "," + S((long long)g_copyBodyHumanResumed);
}

// owner half: did the knockout take? Checked one lever tick after the call and again 2 s after it.
static void P10KnockCheck(DWORD now)
{
    const DWORD due = (g_p10KoChecks == 0) ? kP10TickMs : kP10KoCheck2Ms;
    if (now - g_p10KoAt < due) return;
    ::Character* c = FindSpawned(g_p10KoUid);
    P10Down d;
    P10DownPod(c, &d);
    const bool took = d.unc == 1 || (d.prone >= 2 && d.prone <= 4) || d.ragdoll == 1;
    if (took && !g_p10KoTook) { g_p10KoTook = true; ::InterlockedIncrement(&g_p10Knocks); }
    DebugLog("[P10] lever bodydown knockout check owned uid=" + S((long long)g_p10KoUid) + " afterMs="
             + S((long long)(DWORD)(now - g_p10KoAt)) + (c == 0 ? std::string(" gone") : P10DownLine(c, d))
             + " took=" + S((long long)(took ? 1 : 0)));
    if (++g_p10KoChecks >= 2) g_p10KoUid = 0;
}

// P11 fold 1 (T652) TEST-ONLY lever: `koself <uid> [seconds]` - the game that DRIVES <uid> knocks its own character out
// with the engine's own knockout (medical.cpp KnockOutOwned, multiplier 1.0 - the P10 road that took in T641) and
// pushes its STATE; seconds > 0 then holds the wake-up clock at least that long (KnockClockAtLeastOwned). T652: `wound`
// with stun 300 left B's character standing (prone=0). The check runs one tick and 2 s after the call (KoSelfTick), like
// the P10 lever's. Acts only on this command. MAIN THREAD.
static unsigned int g_koSelfUid     = 0;   // the character still being checked (0 = none)
static unsigned int g_koSelfLastUid = 0;   // the last `koself` target on this game (capturetest's SUMMARY koSource)
static DWORD        g_koSelfAt      = 0;
static int          g_koSelfChecks  = 0;
static bool         g_koSelfTook    = false;
static LONG         g_koSelfCalls = 0, g_koSelfKnocks = 0;

std::string KoSelfLever(unsigned int uid, float seconds)
{
    if (uid == 0) return "error koself usage: koself <uid> [seconds]";
    if (!net::IsUidMine(uid))
    {
        DebugLog("[P11] koself (TEST) uid=" + S((long long)uid) + " refused: this game does not drive it (run it on the owner)");
        return "error koself: uid " + S((long long)uid) + " is not driven by this game";
    }
    ::Character* c = FindSpawned(uid);
    if (c == 0) return "error koself: no character with uid " + S((long long)uid) + " on this game";
    P10Down before;
    P10DownPod(c, &before);
    const std::string beforeLine = P10DownLine(c, before);
    const int ko = KnockOutOwned(uid);
    const float clock = (ko >= 0 && seconds > 0.0f) ? KnockClockAtLeastOwned(uid, seconds) : -1.0f;
    ::InterlockedIncrement(&g_koSelfCalls);
    g_koSelfLastUid = uid;
    g_koSelfUid = (ko >= 0) ? uid : 0; g_koSelfAt = P10Now(); g_koSelfChecks = 0; g_koSelfTook = false;
    const std::string line = "uid=" + S((long long)uid) + " knockout=" + S((long long)ko) + " holdS=" + P10F(seconds)
        + " clockAfterHold=" + P10F(clock) + " before[" + beforeLine + " ]"
        + " calls,knocks=" + S((long long)g_koSelfCalls) + "," + S((long long)g_koSelfKnocks);
    DebugLog("[P11] koself (TEST) " + line);
    return std::string(ko >= 0 ? "ok koself " : "error koself ") + line;
}

unsigned int KoSelfLastUid() { return g_koSelfLastUid; }

// owner half: did the knockout take? Checked one tick after the call and again 2 s after it (P10KnockCheck's rule).
static void KoSelfTick()
{
    if (g_koSelfUid == 0) return;
    const DWORD now = P10Now();
    const DWORD due = (g_koSelfChecks == 0) ? kP10TickMs : kP10KoCheck2Ms;
    if (now - g_koSelfAt < due) return;
    ::Character* c = FindSpawned(g_koSelfUid);
    P10Down d;
    P10DownPod(c, &d);
    const bool took = d.unc == 1 || (d.prone >= 2 && d.prone <= 4) || d.ragdoll == 1;
    if (took && !g_koSelfTook) { g_koSelfTook = true; ::InterlockedIncrement(&g_koSelfKnocks); }
    DebugLog("[P11] koself check (TEST) uid=" + S((long long)g_koSelfUid) + " afterMs="
             + S((long long)(DWORD)(now - g_koSelfAt)) + (c == 0 ? std::string(" gone") : P10DownLine(c, d))
             + " took=" + S((long long)(took ? 1 : 0)) + " calls,knocks=" + S((long long)g_koSelfCalls) + ","
             + S((long long)g_koSelfKnocks));
    if (++g_koSelfChecks >= 2) g_koSelfUid = 0;
}

// looktest <uid> - TEST-ONLY lever, run on the game that DRIVES <uid>: its look changes - one value of its appearance record
// (the first float key whose name holds "height", else the first float key) alternates between its original + 0.1 and its
// original, kept per character and never further than 0.1 from it - and the new APPEARANCE, then its CLOTHING, are sent to the
// other games at once, as the settle send does. looktest restore <uid> sends the original back and forgets it. This game
// applies the record to its own character only when the character stands and its body is not in ragdoll (LookTestTick, each
// tick, outside the engine's ragdoll pass), so a run can send a new look while the character is knocked out and the other games
// apply it while their copy gets up. The own apply also waits while the character's get-up blend runs. [LOOK] lines. MAIN THREAD.
struct LookTestOrig { std::string key; float value; bool raised; };
static std::map<unsigned int, LookTestOrig> g_lookTestOrig;   // uid -> the value before the first looktest, and which side is sent
static unsigned int g_lookTestUid    = 0;   // the own character whose own apply waits (0 = none)
static RecordCopy   g_lookTestRec;          // the record it waits to take
static DWORD        g_lookTestAt     = 0;   // when the lever ran
static LONG g_lookTestCalls = 0, g_lookTestSent = 0, g_lookTestSelfApplied = 0;

// The rule's body reads for this game's OWN character: the ragdoll object and a fault. No C++ object with a destructor (C2712).
static void OwnBodyHoldPod(::Character* c, int* ragdoll, int* fault)
{
    *ragdoll = 0; *fault = 0;
    if (c == 0 || !Obj(c)) { *fault = 1; return; }
    __try
    {
        void* anim = *(void**)((char*)c + kCharAnimationOff);
        if (anim == 0) { *fault = 1; return; }
        *ragdoll = (*(void**)((char*)anim + kCopyBodyRagdollOff) != 0) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *fault = 1; }
}

static int LookTestKey(const RecordCopy& rec, const std::string& want)
{
    int first = -1;
    for (size_t i = 0; i < rec.floatKeys.size(); ++i)
    {
        if (!want.empty()) { if (rec.floatKeys[i] == want) return (int)i; continue; }
        std::string k = rec.floatKeys[i];
        for (size_t j = 0; j < k.size(); ++j) if (k[j] >= 'A' && k[j] <= 'Z') k[j] = (char)(k[j] - 'A' + 'a');
        if (k.find("height") != std::string::npos) return (int)i;
        if (first < 0) first = (int)i;
    }
    return first;
}

std::string LookTestLever(unsigned int uid, bool restore)
{
    const char* verb = restore ? "looktest restore" : "looktest";
    if (uid == 0) return "error looktest usage: looktest <uid> | looktest restore <uid> | looktest show <uid>";
    if (!net::IsUidMine(uid))
    {
        DebugLog("[LOOK] " + std::string(verb) + " (TEST) uid=" + S((long long)uid) + " refused: this game does not drive it (run it on the owner)");
        return "error " + std::string(verb) + ": uid " + S((long long)uid) + " is not driven by this game";
    }
    ::Character* c = FindSpawned(uid);
    if (c == 0) return "error " + std::string(verb) + ": no character with uid " + S((long long)uid) + " on this game";
    std::map<unsigned int, LookTestOrig>::iterator o = g_lookTestOrig.find(uid);
    if (restore && o == g_lookTestOrig.end()) return "error looktest restore: uid " + S((long long)uid) + " has no changed look to restore";
    RecordCopy rec;
    if (g_lookTestUid == uid) rec = g_lookTestRec;   // its own apply still waits: build on the look already sent
    else if (!CaptureAppearanceRecord(c, &rec)) return "error " + std::string(verb) + ": the appearance record could not be read";
    const int k = LookTestKey(rec, o != g_lookTestOrig.end() ? o->second.key : std::string());
    if (k < 0) return "error " + std::string(verb) + ": the appearance record has no float value";
    if (o == g_lookTestOrig.end())
    {
        LookTestOrig n; n.key = rec.floatKeys[k]; n.value = rec.floatVals[k]; n.raised = false;
        o = g_lookTestOrig.insert(std::make_pair(uid, n)).first;
    }
    const float orig = o->second.value;
    const float before = rec.floatVals[k];
    float want = (restore || o->second.raised) ? orig : orig + 0.1f;
    if (want > orig + 0.1f) want = orig + 0.1f;
    if (want < orig - 0.1f) want = orig - 0.1f;
    rec.floatVals[k] = want;
    o->second.raised = (want != orig);
    ::InterlockedIncrement(&g_lookTestCalls);
    const bool sent = net::SendAppearance(uid, rec);
    bool clothes = false;
    if (sent)
    {
        ::InterlockedIncrement(&g_lookTestSent);
        GarmentSet worn;
        clothes = CaptureGarments(c, &worn) && net::SendClothing(uid, worn);
    }
    g_lookTestUid = uid; g_lookTestRec = rec; g_lookTestAt = P10Now();
    const std::string line = "uid=" + S((long long)uid) + " key='" + rec.floatKeys[k] + "' original=" + P10F(orig) + " "
        + P10F(before) + "->" + P10F(want) + " appearanceSent=" + S((long long)(sent ? 1 : 0)) + " clothingSent="
        + S((long long)(clothes ? 1 : 0)) + " ownApply=waits for a standing body not in ragdoll calls,sent,selfApplied="
        + S((long long)g_lookTestCalls) + "," + S((long long)g_lookTestSent) + "," + S((long long)g_lookTestSelfApplied);
    if (restore) g_lookTestOrig.erase(o);
    DebugLog("[LOOK] " + std::string(verb) + " (TEST) " + line);
    return std::string(sent ? "ok " : "error ") + verb + " " + line;
}

// looktest show <uid> - the same lever's read half, on any game: the character's look as this game shows it, read now.
std::string LookTestShow(unsigned int uid)
{
    ::Character* c = FindSpawned(uid);
    if (c == 0) return "error looktest show: no character with uid " + S((long long)uid) + " on this game";
    const std::string line = "uid=" + S((long long)uid) + " mine=" + S((long long)(net::IsUidMine(uid) ? 1 : 0)) + " "
        + AppearanceString(c) + " down=" + S((long long)CopyDownedPod(c));
    DebugLog("[LOOK] looktest show " + line);
    return "ok looktest show " + line;
}

static void LookTestTick()
{
    if (g_lookTestUid == 0) return;
    ::Character* c = FindSpawned(g_lookTestUid);
    if (c == 0)
    {
        DebugLog("[LOOK] looktest own apply (TEST) uid=" + S((long long)g_lookTestUid) + " dropped: the character is gone");
        g_lookTestUid = 0;
        return;
    }
    const DWORD now = P10Now();
    int ragdoll = 0, fault = 0;
    OwnBodyHoldPod(c, &ragdoll, &fault);
    if (coopbody::CopyBodyDecide(1, 1, 1, 1, 1, ragdoll, 0, fault) == coopbody::kCopyBodyWait) return;   // the tick is outside the pass
    // The guard holds this game's own character's rebuild only when the mod queued it (this apply marks it below) or a hold
    // carried over, and then only in ragdoll and inside the ragdoll pass: a rebuild queued while its get-up blend runs would be
    // held until the blend ends, so the own apply also waits for the blend to end.
    BodyTestRead br;
    BodyTestReadPod(c, &br);
    if (br.ok == 0 || (br.blend != 0 && br.running != 0)) return;
    if (CopyDownedPod(c) != 0 || !AppearanceSettled(c) || !CharacterBuilt(c, 0)) return;
    const bool ok = ApplyAppearanceRecord(c, g_lookTestRec);
    CopyBodyNoteModApply(g_lookTestUid, c);   // a rebuild this apply queued is held in ragdoll
    if (ok) ::InterlockedIncrement(&g_lookTestSelfApplied);
    DebugLog("[LOOK] looktest own apply (TEST) uid=" + S((long long)g_lookTestUid) + " waitedMs="
             + S((long long)(DWORD)(now - g_lookTestAt)) + (ok ? " applied" : " FAILED") + " calls,sent,selfApplied="
             + S((long long)g_lookTestCalls) + "," + S((long long)g_lookTestSent) + "," + S((long long)g_lookTestSelfApplied));
    g_lookTestUid = 0;
}

static void P10LeverTick()
{
    if (g_p10ArmedAt == 0 && g_p10KoUid == 0) return;
    const DWORD now = P10Now();
    if (g_p10LastTick != 0 && now - g_p10LastTick < kP10TickMs) return;
    g_p10LastTick = now;
    if (g_p10KoUid != 0) P10KnockCheck(now);
    if (g_p10ArmedAt == 0) return;
    if (g_p10FiredUid != 0)
    {
        ::Character* c = FindSpawned(g_p10FiredUid);
        const int pending = (c != 0) ? CreateBodyPendingPod(c) : -1;
        if (pending == 0)
        {
            ::InterlockedIncrement(&g_p10Rebuilt);
            DebugLog("[P10] lever bodydown copy uid=" + S((long long)g_p10FiredUid) + " body rebuilt (createBody ran) ms="
                     + S((long long)(DWORD)(now - g_p10FiredAt)) + " down=" + S((long long)CopyDownedPod(c)) + P10GuardCounts());
            g_p10ArmedAt = 0;
            return;
        }
        if (c == 0 || now - g_p10FiredAt > kP10WatchMs)
        {
            ::InterlockedIncrement(&g_p10Timeouts);
            DebugLog("[P10] lever bodydown copy uid=" + S((long long)g_p10FiredUid)
                     + (c == 0 ? std::string(" gone before its rebuild") : std::string(" not rebuilt after 180 s"))
                     + " pending=" + S((long long)pending) + " down=" + S((long long)(c != 0 ? CopyDownedPod(c) : -1))
                     + P10GuardCounts());
            g_p10ArmedAt = 0;
        }
        return;
    }
    if (now - g_p10ArmedAt > kP10WindowMs)
    {
        ::InterlockedIncrement(&g_p10Timeouts);
        DebugLog("[P10] lever bodydown window closed: no living animal copy in ragdoll within " + P10F(g_p10RadiusM)
                 + " m of anchor uid=" + S((long long)g_p10Anchor) + " in 120 s - nothing requested; nearest animal copy uid="
                 + S((long long)g_p10CopyNearestUid) + " atM=" + P10M(g_p10CopyNearestSq) + " (its closest; 0 / -1 = none seen)");
        g_p10ArmedAt = 0;
        return;
    }
    Ogre::Vector3 at;
    if (!P10AnchorPos(&at)) return;
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int u = 0; ::Character* c = 0; float d = 0.0f; uintptr_t avt = 0;
        if (!MirrorSlot(i, &u, &c) || u == 0 || c == 0 || u == g_p10Anchor || net::IsUidMine(u)) continue;
        const int animal = P10AnimalPod(c, &avt);
        P10NoteClass(animal, avt);
        if (animal != 1 || P10PlayerPod(c) != 0 || DeadPod(c) != 0) continue;   // a living animal copy, not a player's
        if (!P10DistSq(c, at, &d)) continue;
        if (g_p10CopyNearestSq < 0.0f || d < g_p10CopyNearestSq) { g_p10CopyNearestSq = d; g_p10CopyNearestUid = u; }
        if (d > g_p10RangeSq) continue;
        CopyBodyRead r;
        CopyBodyReadPod(GetAppearance(c), &r);
        if (r.replicated == 0 || r.owned != 0 || r.entity == 0 || r.ragdoll == 0 || r.fault != 0) continue;   // the guard's ragdoll hold, on a copy
        P10Down dn;
        P10DownPod(c, &dn);
        const int called = ReassertAppearanceData(c);
        CopyBodyNoteModApply(u, c);   // the lever queued this rebuild (TEST)
        const int pending = CreateBodyPendingPod(c);
        ::InterlockedIncrement(&g_p10Requests);
        DebugLog("[P10] lever bodydown createBody requested on animal copy uid=" + S((long long)u) + " atM=" + P10M(d)
                 + " animalVt=" + Hex((unsigned long long)avt) + P10DownLine(c, dn)
                 + " guardRead[copy,entity,ragdoll,fault]=" + S((long long)((r.replicated != 0 && r.owned == 0) ? 1 : 0)) + "," + S((long long)r.entity) + ","
                 + S((long long)r.ragdoll) + "," + S((long long)r.fault)
                 + " setAppearanceData=" + S((long long)called) + " pending143=" + S((long long)pending) + P10GuardCounts());
        if (called == 1) { g_p10FiredUid = u; g_p10FiredAt = now; }
        else g_p10ArmedAt = 0;
        return;
    }
}

/* M7a (T-197 piece 7a): THE CATCH-UP'S LOOKS. The settle below sends an owned character's looks ONCE; a game that comes to have
   the area later is sent them again here (worldsync.cpp WorldsyncCatchupAsk, the stream addressed to that game). A character
   still waiting to settle is skipped: its settle sends to everyone who has the area. MAIN THREAD. */
bool AppearanceResendOwned(unsigned int uid)
{
    if (g_pendingLocal.find(uid) != g_pendingLocal.end()) return false;
    ::Character* c = FindSpawned(uid);
    if (c == 0 || !AppearanceSettled(c)) return false;
    RecordCopy rec;
    if (!CaptureAppearanceRecord(c, &rec) || !net::SendAppearance(uid, rec)) return false;
    GarmentSet worn;
    if (CaptureGarments(c, &worn)) net::SendClothing(uid, worn);
    return true;
}

// T-556 (resurrect.cpp): a record this game applies to a character it DRIVES once its body can take it - the same gates as the
// looktest own apply (standing, not in ragdoll, no get-up blend, settled and built) - and then sends: APPEARANCE with that
// record, then CLOTHING with what it wears. The other games always get a look and the kit: when the apply fails, a send fails
// or the body has not passed the gates within kOwnLookWaitMs, the character is handed to the settle send (WatchLocalRoll),
// which sends its own look and what it wears (nothing, for a stripped character). A copy's CLOTHING waits for an APPEARANCE
// (ApplyPendingGarments), so the kit never goes alone. A record that missed the deadline stays queued (late): it is still
// applied, and APPEARANCE + CLOTHING sent again, when the body passes the gates; it leaves the queue only then, or when the
// character is gone, or at world teardown. Result per uid (given once, at the first of: applied, failed, deadline, gone):
// 0 waiting, 1 applied and both sent, 2 applied and a send failed, 3 not applied in time (still queued), 4 the apply failed
// (2-4: handed to the settle send), -1 the character is gone. MAIN THREAD.
struct OwnLookWait { RecordCopy rec; DWORD at; bool late; };
static std::map<unsigned int, OwnLookWait> g_ownLook;
static std::map<unsigned int, int> g_ownLookResult;
static const DWORD kOwnLookWaitMs = 60000;

void OwnLookQueue(unsigned int uid, const RecordCopy& rec)
{
    OwnLookWait& w = g_ownLook[uid];
    w.rec = rec;
    w.at = ::GetTickCount();
    w.late = false;
    g_ownLookResult[uid] = 0;
}

void OwnLookWorldTeardown()
{
    g_ownLook.clear();
    g_ownLookResult.clear();
}

int OwnLookResult(unsigned int uid)
{
    std::map<unsigned int, int>::iterator it = g_ownLookResult.find(uid);
    if (it == g_ownLookResult.end()) return -1;
    const int r = it->second;
    if (r != 0) g_ownLookResult.erase(it);
    return r;
}

static void OwnLookTick()
{
    std::map<unsigned int, OwnLookWait>::iterator it = g_ownLook.begin();
    while (it != g_ownLook.end())
    {
        const unsigned int uid = it->first;
        ::Character* c = FindSpawned(uid);
        const bool late = it->second.late;
        if (c == 0) { if (!late) g_ownLookResult[uid] = -1; g_ownLook.erase(it++); continue; }
        if (!late && (DWORD)(::GetTickCount() - it->second.at) >= kOwnLookWaitMs)
        {
            DebugLog("[P019] own record for uid=" + S(uid) + " NOT applied in " + S((long long)(kOwnLookWaitMs / 1000))
                     + " s - its own look and kit go out by the settle send now; the record stays queued for when the body passes the gates");
            WatchLocalRoll(uid);
            g_ownLookResult[uid] = 3;
            it->second.late = true;
            ++it;
            continue;
        }
        int ragdoll = 0, fault = 0;
        OwnBodyHoldPod(c, &ragdoll, &fault);
        BodyTestRead br;
        if (coopbody::CopyBodyDecide(1, 1, 1, 1, 1, ragdoll, 0, fault) == coopbody::kCopyBodyWait) { ++it; continue; }
        BodyTestReadPod(c, &br);
        if (br.ok == 0 || (br.blend != 0 && br.running != 0)) { ++it; continue; }
        if (CopyDownedPod(c) != 0 || !AppearanceSettled(c) || !CharacterBuilt(c, 0)) { ++it; continue; }
        int r = 4;
        const bool applied = ApplyAppearanceRecord(c, it->second.rec);
        CopyBodyNoteModApply(uid, c);   // a rebuild this apply queued is held in ragdoll
        if (applied)
        {
            r = net::SendAppearance(uid, it->second.rec) ? 1 : 2;
            GarmentSet worn;
            if (r == 1 && !(CaptureGarments(c, &worn) && net::SendClothing(uid, worn))) r = 2;
            if (r == 1) InvNetNoteAnnounced((void*)c, uid);
            DebugLog("[P019] -> APPEARANCE uid=" + S(uid) + (late ? " (own record applied LATE, after the deadline) " : " (own record applied first) ")
                     + RecordSummary(it->second.rec)
                     + (r == 1 ? " + CLOTHING " + GarmentSummary(worn) : std::string(" - a send FAILED: handed to the settle send")));
        }
        else DebugLog("[P019] own record for uid=" + S(uid) + " FAILED to apply - its own look and kit go out by the settle send");
        if (r != 1) WatchLocalRoll(uid);
        if (!late) g_ownLookResult[uid] = r;   // a late record's result (3) was given at the deadline
        g_ownLook.erase(it++);
    }
}

void AppearanceTick()
{
    // This tick runs outside the engine's ragdoll pass, so the pass depth must read 0 here; anything else would hold every copy's
    // rebuild for good. Said once, counted every time, reset.
    if (g_ragdollPassDepth != 0)
    {
        if (::InterlockedIncrement(&g_ragdollPassDepthResets) == 1)
            DebugLog("[COPY] the ragdoll pass depth read " + S((long long)g_ragdollPassDepth) + " outside the pass - reset to 0"
                     " (ragdollPassDepthResets on the [P019] REPORT)");
        ::InterlockedExchange(&g_ragdollPassDepth, 0);
    }
    P10LeverTick();   // P10 TEST-ONLY lever: returns at once unless `bodydown` armed it
    CopyBodyWorldCheck();   // T-293 fold 2 (F3-b): a world load clears the createBody guard's maps
    KoSelfTick();     // P11 fold 1 TEST-ONLY lever: returns at once unless a `koself` call is being checked
    LookTestTick();   // TEST-ONLY lever: returns at once unless a `looktest` own apply waits
    OwnLookTick();    // T-556: returns at once unless a brought-back character's record waits for its body
    BodyTestTick();   // TEST-ONLY lever: returns at once unless `bodytest pending` is armed
    // Send half: a uid we own, once its roll has settled. F157 is the whole reason this is a
    // tick and not a line inside the spawn path - at the moment the spawn returns the roll
    // has not been applied yet and reads as defaults on both sides, which would have
    // replicated "no appearance" and looked like a pass.
    if (!g_pendingLocal.empty())
    {
        std::map<unsigned int, bool>::iterator it = g_pendingLocal.begin();
        while (it != g_pendingLocal.end())
        {
            unsigned int uid = it->first;
            ::Character* c = FindSpawned(uid);
            if (c == 0) { g_pendingLocal.erase(it++); continue; }
            if (!AppearanceSettled(c)) { ++it; continue; }

            RecordCopy rec;
            if (CaptureAppearanceRecord(c, &rec) && net::SendAppearance(uid, rec))
            {
                ++g_c.sent;
                DebugLog("[P019] -> APPEARANCE uid=" + S(uid) + " " + RecordSummary(rec));

                // Clothing rides the same settle event but a SEPARATE message, sent second.
                // The order matters on the receiving side, not here (see AppearanceTick).
                GarmentSet worn;
                if (CaptureGarments(c, &worn) && net::SendClothing(uid, worn))
                {
                    DebugLog("[P022] -> CLOTHING uid=" + S(uid) + " " + GarmentSummary(worn));
                    InvNetNoteAnnounced((void*)c, uid);   /* inv2a: the safety net's baseline for this character is what was just sent */
                }
            }
            else
            {
                ++g_c.captureFailed;
                ErrorLog("[P019] capture FAILED for owned uid " + S(uid)
                         + " - the peer will keep its own roll");
            }
            g_pendingLocal.erase(it++);
        }
    }

    ApplyPendingGarments();

    // Apply half: only once OUR OWN copy has settled too, otherwise the engine's pending
    // appearance update runs afterwards and re-randomises straight over the top of us - and
    // (R1-a) only once its body is fully built.
    if (!g_pendingRemote.empty())
    {
        std::map<unsigned int, RecordCopy>::iterator it = g_pendingRemote.begin();
        while (it != g_pendingRemote.end())
        {
            unsigned int uid = it->first;
            ::Character* c = FindSpawned(uid);
            if (c == 0) { ++it; continue; }              // may arrive before the spawn does
            if (!AppearanceSettled(c)) { ++it; continue; }
            // R1-a: updateAppearance (0x532A40) reads the body's scene node unchecked, so a
            // settled character whose body is still being built waits too (read-r1 Q4).
            if (!CharacterBuilt(c, 0)) { NoteWaitEntered(g_waitNotBuiltAppearance, uid, &g_appearanceWaitNotBuilt); ++it; continue; }
            g_waitNotBuiltAppearance.erase(uid);
            // crash1 (T293 / F912): never rebuild a body that is lying down or limp; it applies once the copy stands.
            // This holds the whole apply (its +0x142 / +0x141 steps too) while the copy lies down; the body rebuild itself is
            // held by the copy body-rebuild guard (AnimalBodyMustWait) while the copy is in ragdoll or the engine's ragdoll pass runs.
            // crash1b (T323): ... nor while its OWNER says it is knocked out - a copy re-spawned under a KO'd owner stands for a
            // moment, and the knockdown that follows would land on the queued rebuild (T293 again)
            // deadlook1 (review D4a): the appearance is NEVER applied to a dead copy (the body rebuild makes a corpse jump,
            // F939) - refused here by hasDied itself, not only by "lying down"; an unreadable hasDied waits too. The settle
            // clock for its clothing starts here if this is where it is first seen dead.
            // T-303: nor while its OWNER says dead and the copy's own death has not landed yet (a SPAWN death still retrying).
            // T-303 fold 1 (owner decision 223): ... except its FIRST appearance - a copy its SPAWN said dead that is still ALIVE,
            // built and standing takes it once, then dies (medical.cpp SpawnDeathTry, at most kFirstLookWaitMs after its body is built). A copy
            // already dead never does: DeadPod refuses it first (deadlook1 unchanged).
            // A copy its OWNER says knocked out that still STANDS, alive, with its knockdown held for its looks right now
            // (spawn.cpp CopyKnockdownHeld) takes its FIRST look here: the setAppearanceData call sets +0x142, so the hold
            // keeps the knockdown back until the rebuild has run, and the copy goes down wearing its owner's look
            // (src/common/kolook.h LookGate). Every read is live, at the moment of the apply.
            const int lookDead = DeadPod(c);
            const int lookDowned = CopyDownedPod(c);
            const bool lookOwnerKo = coop::CopyOwnerSaysKo(uid);
            const bool lookOwnerDead = coop::CopyOwnerSaysDead(uid);
            const bool lookDeadWanted = lookOwnerDead && coop::SpawnDeathLookWanted(uid);
            const bool lookFirst = g_copyLookApplied.find(uid) == g_copyLookApplied.end();
            const bool lookHeld = lookOwnerKo && lookFirst && coop::CopyKnockdownHeld(uid);
            if (coopkolook::LookGate(lookDead, lookDowned, lookOwnerKo, lookOwnerDead, lookDeadWanted, lookFirst, lookHeld)
                != coopkolook::kLookApply)
            {
                DeadNoteSeen(uid, c);
                NoteWaitEntered(g_waitDownedAppearance, uid, &g_appearanceWaitDowned); ++it; continue;
            }
            g_waitDownedAppearance.erase(uid);
            const bool lookKoThrough = coopkolook::LookThroughKoHold(lookDead, lookDowned, lookOwnerKo, lookOwnerDead,
                                                                     lookDeadWanted, lookFirst, lookHeld);

            std::string before = AppearanceString(c);
            bool ok = ApplyAppearanceRecord(c, it->second);
            CopyBodyNoteModApply(uid, c);   // held in ragdoll even if the uid becomes this game's own first
            if (ok) { ++g_c.applied; g_appearanceDone[uid] = true; g_copyLookApplied[uid] = true; }
            else    ++g_c.applyFailed;
            if (ok && lookKoThrough)
            {
                g_lookKoThrough[uid] = true;
                ++g_lookKoApplied;
                if (CopyPlayerSidePod(c) == 1) ++g_lookKoAppliedPlayer;
            }
            coop::SpawnDeathNoteLookApplied(uid, ok);   /* T-303 fold 1 (decision 223): a SPAWN-dead copy's kill waited for this */
            // PROBE-START: P091 - the APPEARANCE apply clock
            if (ok) P091NoteAppearanceApplied(uid);
            // PROBE-END: P091
            DebugLog("[P019] <- APPEARANCE uid=" + S(uid) + (ok ? " applied" : " FAILED")
                     + (lookKoThrough ? " (owner says knocked out; standing, knockdown held - lookKoArrival)" : ""));
            DebugLog("[P019]   before " + before);
            DebugLog("[P019]   after  " + AppearanceString(c));
            g_pendingRemote.erase(it++);
        }
    }
}

// Clothing applies only AFTER this uid's appearance record has been applied. Applying the
// appearance rebuilds the character's attachments, so garments written first are undone -
// which would look exactly like the clothing fix not working. R1-a-b (review-r1a M3): nor while a
// NEWER appearance record for the same uid is still pending - it would rebuild over the garments.
// deadlook1: both rules except a DEAD HUMAN copy, whose appearance is never applied (owner decision 2026-09-26).
void QueueRemoteGarments(unsigned int uid, const GarmentSet& set)
{
    g_pendingGarments[uid] = set;
}

void ApplyPendingGarments()
{
    if (g_pendingGarments.empty()) return;
    std::map<unsigned int, GarmentSet>::iterator it = g_pendingGarments.begin();
    while (it != g_pendingGarments.end())
    {
        unsigned int uid = it->first;
        // kit3 (review D3): a kit is another game's dress for ITS character. One queued before this game took the uid for its own
        // (TakeLocalOwner) must never re-dress it: dropped, counted, said once.
        if (net::IsUidMine(uid))
        {
            if (++g_kitOwnedSkipped == 1)
                DebugLog("[P022] CLOTHING uid=" + S(uid) + " NOT applied - this game owns the character now (a kit queued before it"
                         " was taken; said once, counted as kit3[kitOwnedSkipped])");
            g_waitClothingForAppearance.erase(uid);
            g_waitNotBuiltClothing.erase(uid);
            g_waitDownedClothing.erase(uid);
            g_waitKitOpen.erase(uid);
            g_pendingGarments.erase(it++);
            continue;
        }
        // deadlook1 (owner decision 2026-09-26): a DEAD HUMAN copy takes its clothing without its appearance - the
        // appearance is never applied to a corpse, so "clothing waits for the appearance" would hold it for good. Only the
        // cheap half (dead + human) is asked here; the rest (carried, bed/cage, looted, rebuild) at the lying-down gate.
        const bool appDone = g_appearanceDone.find(uid) != g_appearanceDone.end();
        const bool appPending = g_pendingRemote.find(uid) != g_pendingRemote.end();
        if (!appDone || appPending)
        {
            ::Character* dc = FindSpawned(uid);
            const bool deadHuman = (dc != 0 && DeadPod(dc) == 1 && DeadHumanPod(dc) == 1);
            if (!deadHuman)
            {
                if (!appDone) { ++it; continue; }
                NoteWaitEntered(g_waitClothingForAppearance, uid, &g_clothingWaitAppearance); ++it; continue;
            }
        }
        g_waitClothingForAppearance.erase(uid);

        ::Character* c = FindSpawned(uid);
        if (c == 0) { ++it; continue; }
        if (!AppearanceSettled(c)) { ++it; continue; }
        // R1-a: the appearance apply just above rebuilds the body, so wait for it to be built.
        if (!CharacterBuilt(c, 0)) { NoteWaitEntered(g_waitNotBuiltClothing, uid, &g_appearanceWaitNotBuilt); ++it; continue; }
        g_waitNotBuiltClothing.erase(uid);
        // crash1 (T293 / F912): the garment apply rebuilds the attachments too - the same wait as the appearance apply.
        // deadlook1: a DEAD copy is asked the dead-copy questions whether or not it reads as lying down, live, here - the
        // moment of the apply; only a dead HUMAN copy that passes all of them goes on. A looted one is held (never
        // re-dressed); the entry stays so that a RE-SPAWNED copy - whose marks DeadlookForgetCopy cleared - can be dressed.
        const int deadNow = DeadPod(c);
        bool deadApply = false;
        // The copy whose first look went through while its owner says knocked out (g_lookKoThrough) takes its worn items the
        // same way: alive, standing and its knockdown still held for them, read live here (src/common/kolook.h).
        const int clothDowned = CopyDownedPod(c);
        const bool clothOwnerKo = coop::CopyOwnerSaysKo(uid);
        const bool clothOwnerDead = coop::CopyOwnerSaysDead(uid);
        const bool clothLookThrough = g_lookKoThrough.find(uid) != g_lookKoThrough.end();
        const bool clothHeld = clothLookThrough && clothOwnerKo && coop::CopyKnockdownHeld(uid);
        const bool clothKoThrough = coopkolook::ClothingThroughKoHold(deadNow, clothDowned, clothOwnerKo, clothOwnerDead,
                                                                      clothLookThrough, clothHeld);
        // Its first look went through on the held knockdown, but its worn items, ready now, found the hold gone (given up, past
        // its bound) or the copy down: they wait while the owner says knocked out (clothOwnerKo). Counted once per copy (lookKoArrival clothLate).
        if (coopkolook::ClothingMissedKoHold(deadNow, clothDowned, clothOwnerKo, clothOwnerDead, clothLookThrough, clothHeld))
        {
            std::map<unsigned int, bool>::iterator late = g_lookKoThrough.find(uid);
            if (late != g_lookKoThrough.end() && late->second)
            {
                late->second = false;
                ++g_lookKoClothLate;
                DebugLog("[P022] CLOTHING uid=" + S(uid) + " its first look went through on the held knockdown, but the hold is"
                         " gone or the copy is down now - the worn items wait for it to stand (lookKoArrival clothLate="
                         + S(g_lookKoClothLate) + ")");
            }
        }
        if (!clothKoThrough
            && (deadNow != 0 || clothDowned != 0 || clothOwnerKo   // crash1b
                || clothOwnerDead))   /* T-303: owner says dead, the copy's death not landed yet - held as not-dead */
        {
            const int v = (deadNow == 1) ? DeadClothingVerdict(uid, c) : kDeadNotDead;
            if (v == kDeadHeldCorpse)
            {
                // T-293 fold 2 (F2-B): never apply clothes to a held corpse - the list is dropped, counted kitStale[heldCorpse].
                g_waitNotBuiltClothing.erase(uid);
                g_waitDownedClothing.erase(uid);
                g_waitKitOpen.erase(uid);
                g_deadHeldReason.erase(uid);
                ++g_kitStaleHeldCorpse;
                if (++g_kitStaleLines <= 50)
                    DebugLog("[P022] CLOTHING uid=" + S(uid) + " DROPPED before it was applied - a dead copy whose body rebuild the"
                             " createBody guard holds for good (+0x143 set, lying in ragdoll); clothes are never applied to a held"
                             " corpse (T-293 fold 2, kitStale[heldCorpse]; 50 lines at most)");
                g_pendingGarments.erase(it++);
                continue;
            }
            if (v != kDeadGo)
            {
                if (deadNow == 1) NoteDeadHeld(uid, v);
                NoteWaitEntered(g_waitDownedClothing, uid, &g_clothingWaitDowned); ++it; continue;
            }
            deadApply = true;
            g_deadHeldReason.erase(uid);
        }
        g_waitDownedClothing.erase(uid);
        // T-1 B2 fold (review F2): the apply destroys and rebuilds every item the copy and its packs hold (clearAll). Since T-1 B2 a
        // LIVE pack wearer's kit is resent after the writer's wandering restock, so this can land mid-life: it waits while this game
        // still holds one of those items by pointer - an inventory window on it or one of its packs, an item from it on the cursor,
        // a request against it awaiting its answer (items.cpp KitApplyBusy). Re-asked every tick; the entry is never dropped here.
        const int busy = KitApplyBusy((void*)c, uid);
        if (busy != 0)
        {
            if (g_waitKitOpen.insert(std::make_pair(uid, true)).second)
            {
                ++g_kitApplyDeferredOpen;
                if (busy >= 1 && busy <= 3) ++g_kitDeferredWhy[busy - 1];
                if (g_kitApplyDeferredOpen == 1)
                    DebugLog("[P022] CLOTHING uid=" + S(uid) + " HELD - an inventory window, the cursor or a pending request still"
                             " holds one of its items (reason " + S((long long)busy) + "); applied once clear (T-1 B2 fold; said once,"
                             " counted as kitOpen[kitApplyDeferredOpen])");
            }
            ++it; continue;
        }
        g_waitKitOpen.erase(uid);

        std::string before = GearString(c);
        // deadlook1 fold 1: a dead copy gets its WORN slots only (clothing.cpp ApplyWornGarments); everything else stays.
        bool ok = deadApply ? ApplyWornGarments(c, it->second) : ApplyGarments(c, it->second);
        CopyBodyNoteModApply(uid, c);   // a rebuild the kit apply queued is held in ragdoll whoever owns it
        if (deadApply && ok)
        {
            ++g_deadClothingApplied;
            // fold 5 (review D1): the clothing path should queue only +0x141 (buildAttachments). A createBody queued here
            // would be the corpse-jump path; counted, expected 0.
            if (CreateBodyPendingPod(c) == 1)
            {
                ++g_deadRebuildQueued;
                DebugLog("[P022] deadlook: uid=" + S(uid) + " createBody PENDING (+0x143) after the dead-copy clothing apply"
                         " - not expected");
            }
        }
        if (ok) g_lookKoThrough.erase(uid);
        DebugLog("[P022] <- CLOTHING uid=" + S(uid) + (ok ? " applied" : " FAILED") + (deadApply ? " (dead copy)" : "")
                 + (clothKoThrough ? " (owner says knocked out; standing, knockdown held - lookKoArrival)" : ""));
        DebugLog("[P022]   before " + before);
        DebugLog("[P022]   after  " + GearString(c));
        g_pendingGarments.erase(it++);
    }
}

void NoteKnockLooksGaveUp(unsigned int uid)
{
    if (!coop::CopyOwnerSaysKo(uid)) return;
    if (g_pendingRemote.find(uid) == g_pendingRemote.end()) return;          // no look waiting
    if (g_copyLookApplied.find(uid) != g_copyLookApplied.end()) return;      // not its first look
    ++g_lookKoGaveUp;
    if (CopyPlayerSidePod(FindSpawned(uid)) == 1) ++g_lookKoGaveUpPlayer;
    DebugLog("[P019] APPEARANCE uid=" + S(uid) + " the knockdown held for its first look gave up after "
             + S((long long)coopkolook::kFirstLookWaitMs) + " ms - the copy goes down and the look waits until it stands"
             " (lookKoArrival gaveUp=" + S(g_lookKoGaveUp) + ")");
}

int KnockdownWait(unsigned int uid, ::Character* c)
{
    if (CopyRebuildInFlightAny(c)) return 1;   /* T-293 fold 2b: a copy the createBody guard holds right now is not waited on (its +0x141/+0x142 cannot clear while held) */
    if (g_pendingRemote.find(uid) != g_pendingRemote.end()) return 2;
    if (g_pendingGarments.find(uid) != g_pendingGarments.end()) return 2;
    return 0;
}

// crash1b: the exported face of LimpPod (MAIN THREAD).
int CopyLimpPod(::Character* c) { return LimpPod(c); }

int CopyDowned(::Character* c)
{
    return CopyDownedPod(c);
}

// crash1: of the uids in a lying-down wait now, how many are dead (a dead copy never stands, so its wait never ends).
// deadlook1: what is still counted is what still waits - dead animals, corpses held (carried, bed/cage, not provably human)
// plus every looted dead copy's clothing and every dead copy's APPEARANCE entry (never applied to a corpse). A dressed
// dead copy's clothing has left.
static long long DownedWaitDeadNow(const std::map<unsigned int, bool>& waiting)
{
    long long n = 0;
    for (std::map<unsigned int, bool>::const_iterator it = waiting.begin(); it != waiting.end(); ++it)
    {
        ::Character* c = FindSpawned(it->first);
        if (c != 0 && Obj(c) && c->hasDied()) ++n;
    }
    return n;
}

// crash1c (review-crash1b R3b): of the uids in a lying-down wait now, how many are alive copies whose owner's last STATE
// says knocked out - an owner that stays KO (or dead) keeps these looks waiting; downedDeadNow covers only dead copies.
static long long DownedWaitOwnerKoNow(const std::map<unsigned int, bool>& waiting)
{
    long long n = 0;
    for (std::map<unsigned int, bool>::const_iterator it = waiting.begin(); it != waiting.end(); ++it)
    {
        if (!coop::CopyOwnerSaysKo(it->first)) continue;
        ::Character* c = FindSpawned(it->first);
        if (c != 0 && Obj(c) && !c->hasDied()) ++n;
    }
    return n;
}

void ReportAppearanceCounters()
{
    DebugLog("[P019] REPORT watched=" + S(g_c.watched)
             + " sent=" + S(g_c.sent)
             + " queued=" + S(g_c.queued)
             + " applied=" + S(g_c.applied)
             + " applyFailed=" + S(g_c.applyFailed)
             + " captureFailed=" + S(g_c.captureFailed)
             + " pendingLocal=" + S((long long)g_pendingLocal.size())
             + " pendingRemote=" + S((long long)g_pendingRemote.size())
             // T-293: the human test's answers per call (vtable == AppearanceHumanVt / another class / no row or unreadable);
             // yes=0 while no > 0 in a run with human characters = the test is broken again.
             + " humanTest[yes,no,unreadable]=" + S((long long)g_humanYes) + "," + S((long long)g_humanNo) + ","
             + S((long long)g_humanUnreadable)
             // crash1 (T293 / F912): entries that entered the lying-down wait (cumulative), then the uids that entered it and
             // have not left (a copy removed while waiting stays in the count, as in the other wait maps here), then how many
             // of those are spawned and dead. deadlook1: downedDeadNow = what still waits (dead animals, held corpses,
             // looted corpses, and every dead copy's appearance entry, which is never applied to a corpse); a dressed dead
             // copy's clothing has left.
             + " crash1[appearanceWaitDowned,clothingWaitDowned,appearanceDownedNow,clothingDownedNow,downedDeadNow]="
             + S(g_appearanceWaitDowned) + "," + S(g_clothingWaitDowned)
             + "," + S((long long)g_waitDownedAppearance.size()) + "," + S((long long)g_waitDownedClothing.size())
             + "," + S(DownedWaitDeadNow(g_waitDownedAppearance) + DownedWaitDeadNow(g_waitDownedClothing))
             // crash1c (R3b): alive copies waiting for their looks while the owner says KO (no behaviour change).
             + " crash1c[lookWaitOwnerKo]="
             + S(DownedWaitOwnerKoNow(g_waitDownedAppearance) + DownedWaitOwnerKoNow(g_waitDownedClothing))
             // first looks applied to a standing copy whose owner says knocked out while its knockdown was held for them /
             // holds that gave up with the first look pending / worn items that then found the hold gone or the copy down;
             // then the first two for copies of a player's characters.
             + " lookKoArrival[applied,gaveUp,clothLate]=" + S(g_lookKoApplied) + "," + S(g_lookKoGaveUp) + "," + S(g_lookKoClothLate)
             + " lookKoArrivalPlayer[applied,gaveUp]=" + S(g_lookKoAppliedPlayer) + "," + S(g_lookKoGaveUpPlayer)
             // deadlook1 (owner decision 2026-09-26): dead HUMAN copies dressed with the owner's worn clothing (no body
             // rebuild); holds entered, once per uid per reason, for a looted dead copy (the item sync moved its items -
             // never re-dressed) / a dead animal / a dead copy not provably human / a dead human carried or in a bed or cage;
             // and createBody found pending after a dead copy's clothing apply (review D1, expected 0).
             + " deadlook[clothingDead,deadLooted,deadAnimalHeld,deadNotHuman,deadCarriedHeld,deadRebuildQueued]="
             + S(g_deadClothingApplied) + "," + S(g_deadLooted) + "," + S(g_deadAnimalHeld)
             + "," + S(g_deadNotHuman) + "," + S(g_deadCarriedHeld) + "," + S(g_deadRebuildQueued)
             // crash2: animal copy createBody calls skipped in ragdoll (per call / distinct uids), deferred uids later
             // rebuilt, calls off the main thread (never skipped), skips because a read faulted; holds of a body this game
             // owns (both classes, once per hold - also one that became this game's own while held).
             + " copyBody[deferred,deferredUids,resumed,offMain,readFault,heldOwned,ownedRanInRagdoll]="
             + S((long long)g_copyBodyDeferred) + "," + S((long long)g_copyBodyDeferredUids) + "," + S((long long)g_copyBodyResumed)
             + "," + S((long long)g_copyBodyOffMain) + "," + S((long long)g_copyBodyReadFault) + "," + S((long long)g_copyBodyHeldOwned)
             + "," + S((long long)g_copyBodyOwnedRanInRagdoll)
             // ownedRanInRagdoll = createBody calls run for an owned body in ragdoll / the pass with no hold and no
             // mod mark (the engine's own); mod marks placed, marks waiting now, mark requests off the main thread (not placed)
             + " copyBodyModMark[marks,now,offMain]=" + S((long long)g_copyBodyModMarks) + ","
             + S((long long)g_copyBodyModQueued.size()) + "," + S((long long)g_copyBodyModMarkOffMain)
             // T-293 fold 1 (review F2): the same for HUMAN copies (AppearanceHuman::createBody), plus that hook (1 on, -1 failed, 0 no row)
             + " copyBodyHuman[deferred,deferredUids,resumed,offMain,readFault,hook]="
             + S((long long)g_copyBodyHumanDeferred) + "," + S((long long)g_copyBodyHumanDeferredUids) + "," + S((long long)g_copyBodyHumanResumed)
             + "," + S((long long)g_copyBodyHumanOffMain) + "," + S((long long)g_copyBodyHumanReadFault) + "," + S((long long)g_humanBodyHook)
             // both classes: createBody calls held because they came from inside the engine's ragdoll pass, holds that began
             // there, copies whose rebuild waits now (0 once every deferred rebuild has run); holds logged as still in place
             // after 30 s; ragdoll pass entries off the main thread (expected 0)
             + " copyBodyPass[deferred,episodes,waitingNow]=" + S((long long)g_copyBodyPassDeferred) + ","
             + S((long long)g_copyBodyPassEpisodes) + "," + S((long long)g_copyBodyEpisodes.size())
             + " copyBodyLongHolds=" + S((long long)g_copyBodyLongHolds) + " copyBodyLongHoldsLimp=" + S((long long)g_copyBodyLongHoldsLimp)
             + " ragdollPass[offMain]=" + S((long long)g_ragdollPassOffMain) + " ragdollPassDepthResets=" + S((long long)g_ragdollPassDepthResets)
             // T-293 fold 2 (F2-A): knockout / death gate checks that read "rebuild in flight" and passed because the guard holds that
             // copy's createBody; copies held now; holds the held table had no room for (those gates keep the old answer)
             + " copyBodyHeld[gateHeldPassed,heldNow,tableFull]=" + S((long long)g_gateHeldPassed) + "," + S((long long)CopyBodyHeldCount())
             + "," + S((long long)g_copyBodyHeldFull)
             + " kit3[kitOwnedSkipped]=" + S(g_kitOwnedSkipped)
             // T-293 fold 1 (review F1): waiting clothing lists dropped because the owner moved a worn item after sending them
             + " kitStale[droppedOnOwnerMove,heldCorpse]=" + S(g_kitStaleDropped) + "," + S(g_kitStaleHeldCorpse)
             // T-1 B2 fold (review F2): kit applies held while the copy is open / on the cursor / under a request (once per uid per wait)
             + " kitOpen[kitApplyDeferredOpen,window,cursor,request,waitingNow]=" + S(g_kitApplyDeferredOpen)
             + "," + S(g_kitDeferredWhy[0]) + "," + S(g_kitDeferredWhy[1]) + "," + S(g_kitDeferredWhy[2])
             + "," + S((long long)g_waitKitOpen.size()));
    // PROBE-START: P091 - report line
    P091ReportLine();
    // PROBE-END: P091
    // P10 TEST-ONLY lever counters (all 0 unless `bodydown` was sent)
    DebugLog("[P10] REPORT bodydown[arms,knocks,requests,rebuilt,timeouts]=" + S((long long)g_p10Arms) + ","
             + S((long long)g_p10Knocks) + "," + S((long long)g_p10Requests) + "," + S((long long)g_p10Rebuilt) + ","
             + S((long long)g_p10Timeouts) + BodyTestCounts());
}

// ground5 fold 3 (T635): the ground batch's animal test for items.cpp's TEST lever `groundtest animaldrop` - the P10 lever's
// authoritative test (effort/p10lever P10AnimalPod, not on main yet): the appearance object's vtable holds
// AppearanceAnimal::createBody 0x539C50 (fold 3b: through the slot's E9 jump stub). IsHumanAppearance answers false for every character (T-293), so "not human" is never
// taken as "animal". 1 = an animal, 0 = a readable appearance of another class (a human: AppearanceHuman::createBody 0x539440),
// -1 = no address-table row / no appearance object / a fault. MAIN THREAD. No C++ object here (C2712).
int GroundAnimalAppPod(::Character* c)
{
    uintptr_t vt = 0;   /* ground5 fold 4 (T644): the P10 class test - the appearance object's vtable pointer == AppearanceAnimalVt */
    return P10AnimalPod(c, &vt);
}

// ground5 fold 3: the race record the [P019] lines print (appearance +kRaceOff), 0 = none / unreadable. No C++ object (C2712).
static ::GameData* GroundRacePod(::Character* c)
{
    void* app = GetAppearance(c);
    if (app == 0) return 0;
    __try
    {
        ::GameData* gd = *(::GameData**)((char*)app + kRaceOff);
        return (gd != 0 && Obj(gd)) ? gd : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

std::string GroundRaceName(::Character* c)
{
    ::GameData* gd = GroundRacePod(c);
    return gd != 0 ? gd->name : std::string("?");
}

} // namespace coop
