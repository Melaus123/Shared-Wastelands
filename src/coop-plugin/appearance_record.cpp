// appearance_record.cpp - H010b. See appearance_record.h for why this replaces H010a.
//
// GameData's value storage - every map is declared with its real type in game/GameData.h, so they
// iterate directly rather than through raw offsets:
//   boolFields            +0x0F8  string -> bool
//   stringFields            +0x138  string -> string
//   intFields            +0x178  string -> int
//   floatFields            +0x1B8  string -> float
//   fileFields        +0x1F8  string -> string   (mesh / texture filenames)
//   vectorFields          +0x238  string -> Ogre::Vector3
//   rotationFields         +0x278  string -> Ogre::Quaternion   (counted, not replicated)
//   referenceLists +0x2B8  string -> vector<GameDataReference>{TripleInt, sid, ptr}
//
// The record is reached through the engine's own exported accessor rather than the +0x148
// offset, so nothing here depends on that field being where the header says it is - which
// matters, because F159 found that exact field returning a constant when read by name.

#include "appearance_record.h"
#include "appearance.h"

#include "coop_log.h"
#include <stdint.h>   /* mig3: what <core/Functions.h> also brought in */
#include "addresses.h"
/* These hook targets are our own address-table rows (coop::AddrAbs gives image base + RVA, or 0 when the
   row is unbound, which each call site below guards). */
static unsigned long long kMig3GetAppearanceData = 0; static coop::AddrReg kMig3GetAppearanceData_reg("Appearance_readLook", &kMig3GetAppearanceData);   /* Steam_1.0.65 0x5B0650 */
static unsigned long long kMig3RecUpdateAppearance = 0; static coop::AddrReg kMig3RecUpdateAppearance_reg("AppearanceHuman_rebuildLook", &kMig3RecUpdateAppearance);   /* Steam_1.0.65 0x532E60 */
static unsigned long long kMig3SetAppearanceData = 0; static coop::AddrReg kMig3SetAppearanceData_reg("Appearance_writeLook", &kMig3SetAppearanceData);   /* Steam_1.0.65 0x5378D0 */
#include "game/Character.h"
#include "game/GameWorld.h"
#include "game/GameData.h"
#include "game/GameDataManager.h"
#include "game/Appearance.h"
#include "game/AnimationClass.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <sstream>
#include <locale>
#include <cstring>

namespace coop {

namespace {

const size_t kCharAnimationOff  = 0x448;
const size_t kAnimAppearanceOff = 0x0E8;

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

std::string S(long long v)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << v;
    return ss.str();
}

// The engine's own getter, through its address-table row. SEH lives in a helper with no C++
// objects (MSVC refuses __try in a function that needs object unwinding).
uintptr_t ResolveGetAppearanceData()
{
    __try { return (uintptr_t)coop::AddrAbs(kMig3GetAppearanceData); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
uintptr_t ResolveUpdateAppearance()
{
    __try { return (uintptr_t)coop::AddrAbs(kMig3RecUpdateAppearance); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

uintptr_t ResolveSetAppearanceData()
{
    __try { return (uintptr_t)coop::AddrAbs(kMig3SetAppearanceData); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

uintptr_t g_getAppData = 0, g_setAppData = 0, g_updateApp = 0;
bool g_resolved = false;

std::string Hex(unsigned long long v)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "0x" << std::hex << std::uppercase << v;
    return ss.str();
}

void ResolveOnce()
{
    if (g_resolved) return;
    g_resolved = true;
    g_getAppData = ResolveGetAppearanceData();
    g_setAppData = ResolveSetAppearanceData();
    g_updateApp  = ResolveUpdateAppearance();
    DebugLog("[P020] resolved getAppearanceData=" + Hex(g_getAppData)
             + " setAppearanceData=" + Hex(g_setAppData)
             + " updateAppearance=" + Hex(g_updateApp));
}

// F164 - a MEASUREMENT, shipped alongside the fix so a failed attempt still yields data
// (lesson 7). The human-class test has now failed three ways, and every explanation offered
// for it has been a guess. This prints what is ACTUALLY in the appearance object's vtable so
// the next decision is made from bytes rather than from a theory about COMDAT folding.
//
// Printed once. Slots that match a resolved address are marked, so the answer is readable
// straight off the line instead of requiring arithmetic on eight hex numbers.
void DumpVtableOnce(void* app)
{
    static bool s_done = false;
    if (s_done || app == 0) return;
    s_done = true;

    uintptr_t vt = *(uintptr_t*)app;
    if (!Ptr((void*)vt)) { DebugLog("[P020] vtable UNREADABLE"); return; }

    uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    std::string out = "[P020] appearance vtable @" + Hex(vt) + " imageBase=" + Hex(base);
    for (int i = 0; i < 24; ++i)
    {
        if (!Ptr((void*)(vt + i * sizeof(void*)))) break;
        uintptr_t slot = *(uintptr_t*)(vt + i * sizeof(void*));
        out += "\n  [" + S(i) + "] off=" + Hex((unsigned long long)(i * sizeof(void*)))
             + " " + Hex(slot) + " rva=" + Hex(slot - base);
        if (slot == g_getAppData) out += "  <== getAppearanceData";
        if (slot == g_setAppData) out += "  <== setAppearanceData";
        if (slot == g_updateApp)  out += "  <== updateAppearance";
    }
    DebugLog(out);
}

void* GetAppearanceObj(::Character* c)
{
    if (c == 0 || !Obj(c)) return 0;
    void* anim = *(void**)((char*)c + kCharAnimationOff);
    if (!Obj(anim)) return 0;
    void* app = *(void**)((char*)anim + kAnimAppearanceOff);
    if (!Obj(app)) return 0;
    return app;
}

::GameData* GetRecord(::Character* c)
{
    void* app = GetAppearanceObj(c);
    if (app == 0) return 0;
    ResolveOnce();
    if (g_getAppData == 0) return 0;
    typedef void* (*GetFn)(void*);
    void* rec = ((GetFn)g_getAppData)(app);
    return Obj(rec) ? (::GameData*)rec : 0;
}

// ---- wire helpers ----------------------------------------------------------------------

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

void PutF32(std::vector<char>* b, float v)
{
    size_t at = b->size();
    b->resize(at + 4);
    std::memcpy(&(*b)[at], &v, 4);
}

void PutI32(std::vector<char>* b, int v)
{
    size_t at = b->size();
    b->resize(at + 4);
    std::memcpy(&(*b)[at], &v, 4);
}

bool GetU32(const std::vector<char>& b, size_t* at, unsigned int* out)
{
    if (b.size() < *at + 4) return false;
    std::memcpy(out, &b[*at], 4);
    *at += 4;
    return true;
}

bool GetI32(const std::vector<char>& b, size_t* at, int* out)
{
    if (b.size() < *at + 4) return false;
    std::memcpy(out, &b[*at], 4);
    *at += 4;
    return true;
}

bool GetF32(const std::vector<char>& b, size_t* at, float* out)
{
    if (b.size() < *at + 4) return false;
    std::memcpy(out, &b[*at], 4);
    *at += 4;
    return true;
}

// A key length is bounded so a corrupt or hostile payload cannot make us allocate wildly.
// The bound is a guard on network-supplied data, not a belief about key names.
const unsigned int kMaxKeyLen   = 4096;
const unsigned int kMaxEntries  = 8192;

bool GetStr(const std::vector<char>& b, size_t* at, std::string* out)
{
    unsigned int len = 0;
    if (!GetU32(b, at, &len)) return false;
    if (len > kMaxKeyLen || b.size() < *at + len) return false;
    out->assign(len ? &b[*at] : "", len);
    *at += len;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------------------

bool CaptureAppearanceRecord(::Character* c, RecordCopy* out)
{
    ::GameData* gd = GetRecord(c);
    if (gd == 0 || out == 0) return false;

    {
        typedef boost::unordered::unordered_map<std::string, bool, boost::hash<std::string>,
            std::equal_to<std::string>,
            Ogre::STLAllocator<std::pair<std::string const, bool>, Ogre::GeneralAllocPolicy> > M;
        for (M::const_iterator it = gd->boolFields.begin(); it != gd->boolFields.end(); ++it)
        { out->boolKeys.push_back(it->first); out->boolVals.push_back(it->second ? 1 : 0); }
    }
    {
        typedef boost::unordered::unordered_map<std::string, int, boost::hash<std::string>,
            std::equal_to<std::string>,
            Ogre::STLAllocator<std::pair<std::string const, int>, Ogre::GeneralAllocPolicy> > M;
        for (M::const_iterator it = gd->intFields.begin(); it != gd->intFields.end(); ++it)
        { out->intKeys.push_back(it->first); out->intVals.push_back(it->second); }
    }
    {
        typedef boost::unordered::unordered_map<std::string, float, boost::hash<std::string>,
            std::equal_to<std::string>,
            Ogre::STLAllocator<std::pair<std::string const, float>, Ogre::GeneralAllocPolicy> > M;
        for (M::const_iterator it = gd->floatFields.begin(); it != gd->floatFields.end(); ++it)
        { out->floatKeys.push_back(it->first); out->floatVals.push_back(it->second); }
    }
    {
        typedef boost::unordered::unordered_map<std::string, std::string, boost::hash<std::string>,
            std::equal_to<std::string>,
            Ogre::STLAllocator<std::pair<std::string const, std::string>, Ogre::GeneralAllocPolicy> > M;
        for (M::const_iterator it = gd->stringFields.begin(); it != gd->stringFields.end(); ++it)
        { out->strKeys.push_back(it->first); out->strVals.push_back(it->second); }
        for (M::const_iterator it = gd->fileFields.begin(); it != gd->fileFields.end(); ++it)
        { out->fileKeys.push_back(it->first); out->fileVals.push_back(it->second); }
    }
    {
        typedef boost::unordered::unordered_map<std::string, Ogre::Vector3, boost::hash<std::string>,
            std::equal_to<std::string>,
            Ogre::STLAllocator<std::pair<std::string const, Ogre::Vector3>, Ogre::GeneralAllocPolicy> > M;
        for (M::const_iterator it = gd->vectorFields.begin(); it != gd->vectorFields.end(); ++it)
        {
            out->vecKeys.push_back(it->first);
            out->vecVals.push_back(it->second.x);
            out->vecVals.push_back(it->second.y);
            out->vecVals.push_back(it->second.z);
        }
    }
    {
        typedef boost::unordered::unordered_map<std::string, Ogre::vector<GameDataReference>::type,
            boost::hash<std::string>, std::equal_to<std::string>,
            Ogre::STLAllocator<std::pair<std::string const, Ogre::vector<GameDataReference>::type>,
            Ogre::GeneralAllocPolicy> > M;
        for (M::const_iterator it = gd->referenceLists.begin();
             it != gd->referenceLists.end(); ++it)
        {
            out->refKeys.push_back(it->first);
            out->refCounts.push_back((int)it->second.size());
            for (size_t i = 0; i < it->second.size(); ++i)
            {
                out->refSids.push_back(it->second[i].sid);
                out->refInts.push_back(it->second[i].values.value[0]);
                out->refInts.push_back(it->second[i].values.value[1]);
                out->refInts.push_back(it->second[i].values.value[2]);
            }
        }
    }
    {
        typedef boost::unordered::unordered_map<std::string, Ogre::Quaternion, boost::hash<std::string>,
            std::equal_to<std::string>,
            Ogre::STLAllocator<std::pair<std::string const, Ogre::Quaternion>, Ogre::GeneralAllocPolicy> > M;
        out->quatTotal = (int)gd->rotationFields.size();
    }
    return true;
}

bool ApplyAppearanceRecord(::Character* c, const RecordCopy& rec)
{
    ::GameData* gd = GetRecord(c);
    if (gd == 0) return false;

    // Overwrite in place, key by key. The peer's record already has the right SHAPE - it was
    // built from the same race by the same engine - so this replaces the rolled VALUES and
    // leaves everything else exactly as the engine made it.
    for (size_t i = 0; i < rec.boolKeys.size(); ++i)
        gd->boolFields[rec.boolKeys[i]] = rec.boolVals[i] != 0;
    for (size_t i = 0; i < rec.intKeys.size(); ++i)
        gd->intFields[rec.intKeys[i]] = rec.intVals[i];
    for (size_t i = 0; i < rec.floatKeys.size(); ++i)
        gd->floatFields[rec.floatKeys[i]] = rec.floatVals[i];
    for (size_t i = 0; i < rec.strKeys.size(); ++i)
        gd->stringFields[rec.strKeys[i]] = rec.strVals[i];
    for (size_t i = 0; i < rec.fileKeys.size(); ++i)
        gd->fileFields[rec.fileKeys[i]] = rec.fileVals[i];
    for (size_t i = 0; i < rec.vecKeys.size(); ++i)
        gd->vectorFields[rec.vecKeys[i]] =
            Ogre::Vector3(rec.vecVals[i * 3], rec.vecVals[i * 3 + 1], rec.vecVals[i * 3 + 2]);

    // References carry a target sid plus three ints. The sid is resolved against THIS
    // instance's own static data, which is the premise that already makes template-by-name
    // replication sound (F072/F115). A sid that does not resolve leaves ptr null, which is
    // the same state the engine uses for "no reference".
    {
        size_t sidAt = 0;
        for (size_t k = 0; k < rec.refKeys.size(); ++k)
        {
            Ogre::vector<GameDataReference>::type list;
            for (int j = 0; j < rec.refCounts[k]; ++j, ++sidAt)
            {
                if (sidAt >= rec.refSids.size()) break;
                GameDataReference r(rec.refSids[sidAt],
                                    TripleInt(rec.refInts[sidAt * 3],
                                              rec.refInts[sidAt * 3 + 1],
                                              rec.refInts[sidAt * 3 + 2]));
                r.ptr = (coop::GameWorldPtr() != 0) ? coop::GameWorldPtr()->gamedata.getData(rec.refSids[sidAt]) : 0;
                if (!Obj(r.ptr)) r.ptr = 0;
                list.push_back(r);
            }
            gd->referenceLists[rec.refKeys[k]] = list;
        }
    }

    // Now let the engine derive from what it has been given. This is the ONLY thing H010b
    // asks the engine to do, and `updateAppearance` is the same call that defeated H010a -
    // which is the point: it re-derives, and now it re-derives from the authority's inputs.
    ResolveOnce();
    void* app = GetAppearanceObj(c);
    if (app == 0) return false;

    DumpVtableOnce(app);

    // F163: `updateAppearance()` alone re-derives hair, height and physique but NOT sex -
    // T053 measured `female` byte-identical before and after on 4/4 characters, because the
    // engine sets it once at construction. `setAppearanceData` is the call that hands the
    // record back to the appearance system wholesale, and `AppearanceManager::Gender` has a
    // constructor taking a `GameData* appearanceData`, so gender IS derivable from this
    // record - nothing had yet asked for it to be re-derived.
    //
    // This stays inside the "write the source, let the engine derive" family. It is NOT a
    // return to writing `female` directly: that is refuted (F160) and left a flag lying about
    // the body mesh on screen.
    if (g_setAppData != 0)
    {
        typedef void (*SetFn)(void*, void*);
        ((SetFn)g_setAppData)(app, gd);
    }
    if (g_updateApp != 0)
    {
        typedef void (*VoidFn)(void*);
        ((VoidFn)g_updateApp)(app);
    }
    return true;
}

// P10 TEST-ONLY lever: see appearance_record.h. The same pointer ApplyAppearanceRecord hands over, unchanged.
int ReassertAppearanceData(::Character* c)
{
    ::GameData* gd = GetRecord(c);   // resolves the calls first
    if (gd == 0) return -1;
    void* app = GetAppearanceObj(c);
    if (app == 0) return -1;
    if (g_setAppData == 0) return 0;
    typedef void (*SetFn)(void*, void*);
    ((SetFn)g_setAppData)(app, gd);
    return 1;
}

void SerialiseRecord(const RecordCopy& rec, std::vector<char>* b)
{
    PutU32(b, (unsigned int)rec.boolKeys.size());
    for (size_t i = 0; i < rec.boolKeys.size(); ++i)
    { PutStr(b, rec.boolKeys[i]); b->push_back((char)rec.boolVals[i]); }

    PutU32(b, (unsigned int)rec.intKeys.size());
    for (size_t i = 0; i < rec.intKeys.size(); ++i)
    { PutStr(b, rec.intKeys[i]); PutI32(b, rec.intVals[i]); }

    PutU32(b, (unsigned int)rec.floatKeys.size());
    for (size_t i = 0; i < rec.floatKeys.size(); ++i)
    { PutStr(b, rec.floatKeys[i]); PutF32(b, rec.floatVals[i]); }

    PutU32(b, (unsigned int)rec.strKeys.size());
    for (size_t i = 0; i < rec.strKeys.size(); ++i)
    { PutStr(b, rec.strKeys[i]); PutStr(b, rec.strVals[i]); }

    PutU32(b, (unsigned int)rec.fileKeys.size());
    for (size_t i = 0; i < rec.fileKeys.size(); ++i)
    { PutStr(b, rec.fileKeys[i]); PutStr(b, rec.fileVals[i]); }

    PutU32(b, (unsigned int)rec.vecKeys.size());
    for (size_t i = 0; i < rec.vecKeys.size(); ++i)
    {
        PutStr(b, rec.vecKeys[i]);
        PutF32(b, rec.vecVals[i * 3]);
        PutF32(b, rec.vecVals[i * 3 + 1]);
        PutF32(b, rec.vecVals[i * 3 + 2]);
    }

    PutU32(b, (unsigned int)rec.refKeys.size());
    {
        size_t sidAt = 0;
        for (size_t k = 0; k < rec.refKeys.size(); ++k)
        {
            PutStr(b, rec.refKeys[k]);
            PutU32(b, (unsigned int)rec.refCounts[k]);
            for (int j = 0; j < rec.refCounts[k]; ++j, ++sidAt)
            {
                if (sidAt >= rec.refSids.size()) break;
                PutStr(b, rec.refSids[sidAt]);
                PutI32(b, rec.refInts[sidAt * 3]);
                PutI32(b, rec.refInts[sidAt * 3 + 1]);
                PutI32(b, rec.refInts[sidAt * 3 + 2]);
            }
        }
    }
    PutU32(b, (unsigned int)rec.quatTotal);
}

bool DeserialiseRecord(const std::vector<char>& b, size_t at, RecordCopy* out)
{
    unsigned int n = 0;

    if (!GetU32(b, &at, &n) || n > kMaxEntries) return false;
    for (unsigned int i = 0; i < n; ++i)
    {
        std::string k;
        if (!GetStr(b, &at, &k) || b.size() < at + 1) return false;
        out->boolKeys.push_back(k);
        out->boolVals.push_back((unsigned char)b[at]); ++at;
    }

    if (!GetU32(b, &at, &n) || n > kMaxEntries) return false;
    for (unsigned int i = 0; i < n; ++i)
    {
        std::string k; int v = 0;
        if (!GetStr(b, &at, &k) || !GetI32(b, &at, &v)) return false;
        out->intKeys.push_back(k); out->intVals.push_back(v);
    }

    if (!GetU32(b, &at, &n) || n > kMaxEntries) return false;
    for (unsigned int i = 0; i < n; ++i)
    {
        std::string k; float v = 0;
        if (!GetStr(b, &at, &k) || !GetF32(b, &at, &v)) return false;
        out->floatKeys.push_back(k); out->floatVals.push_back(v);
    }

    if (!GetU32(b, &at, &n) || n > kMaxEntries) return false;
    for (unsigned int i = 0; i < n; ++i)
    {
        std::string k, v;
        if (!GetStr(b, &at, &k) || !GetStr(b, &at, &v)) return false;
        out->strKeys.push_back(k); out->strVals.push_back(v);
    }

    if (!GetU32(b, &at, &n) || n > kMaxEntries) return false;
    for (unsigned int i = 0; i < n; ++i)
    {
        std::string k, v;
        if (!GetStr(b, &at, &k) || !GetStr(b, &at, &v)) return false;
        out->fileKeys.push_back(k); out->fileVals.push_back(v);
    }

    if (!GetU32(b, &at, &n) || n > kMaxEntries) return false;
    for (unsigned int i = 0; i < n; ++i)
    {
        std::string k; float x = 0, y = 0, z = 0;
        if (!GetStr(b, &at, &k) || !GetF32(b, &at, &x)
            || !GetF32(b, &at, &y) || !GetF32(b, &at, &z)) return false;
        out->vecKeys.push_back(k);
        out->vecVals.push_back(x); out->vecVals.push_back(y); out->vecVals.push_back(z);
    }

    if (!GetU32(b, &at, &n) || n > kMaxEntries) return false;
    for (unsigned int i = 0; i < n; ++i)
    {
        std::string k; unsigned int cnt = 0;
        if (!GetStr(b, &at, &k) || !GetU32(b, &at, &cnt) || cnt > kMaxEntries) return false;
        out->refKeys.push_back(k);
        out->refCounts.push_back((int)cnt);
        for (unsigned int j = 0; j < cnt; ++j)
        {
            std::string sid; int a = 0, bb = 0, cc = 0;
            if (!GetStr(b, &at, &sid) || !GetI32(b, &at, &a)
                || !GetI32(b, &at, &bb) || !GetI32(b, &at, &cc)) return false;
            out->refSids.push_back(sid);
            out->refInts.push_back(a); out->refInts.push_back(bb); out->refInts.push_back(cc);
        }
    }

    unsigned int quat = 0;
    if (!GetU32(b, &at, &quat)) return false;
    out->quatTotal = (int)quat;
    return true;
}

std::string RecordSummary(const RecordCopy& rec)
{
    return "bools=" + S((long long)rec.boolKeys.size())
         + " ints=" + S((long long)rec.intKeys.size())
         + " floats=" + S((long long)rec.floatKeys.size())
         + " strings=" + S((long long)rec.strKeys.size())
         + " files=" + S((long long)rec.fileKeys.size())
         + " vecs=" + S((long long)rec.vecKeys.size())
         + " refKeys=" + S((long long)rec.refKeys.size())
         + " refs=" + S((long long)rec.refSids.size())
         + " quatTOTAL=" + S(rec.quatTotal) + "(none replicated)";
}

} // namespace coop
