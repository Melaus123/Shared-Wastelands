/* src/common/limbwire.h - THE LIMB BLOCK OF MSG_STATE: each character's four limbs as its owner's game sees them.
 *
 *   limbVer u8 (= kLimbBlockVer) | state[4] u8 | for each limb whose state is kLimbReplaced: sidLen u8 | sid bytes | quality f32
 *
 * Limb order is the engine's RobotLimbs::Limb: 0 left arm, 1 right arm, 2 left leg, 3 right leg. A state is the engine's
 * LimbState as RobotLimbs keeps it (+0x10, int[4]): 0 whole, 1 stump, 2 a robotic limb fitted, 3 crushed. For a fitted
 * limb the block names the limb item (its base sid, at most kLimbSidMax bytes, no NUL on the wire; 0 bytes = the owner
 * could not read it) and the item's quality (Item +0x11C). A fitted limb's damage is NOT here: while fitted it lives in
 * that limb's health part (flesh / stunDamage / limbWear / maxHealthBase), which every STATE already carries.
 *
 * Built and parsed here only, so the offline suite (src/coop-test/test_main.cpp) hits the same bytes net/session.cpp
 * sends and reads. PlanLimb is the copy's decision per limb, also tested offline.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace cooplimb {

const unsigned int  kLimbCount    = 4;
const unsigned char kLimbOriginal = 0;   /* LimbState LIMB_ORIGINAL */
const unsigned char kLimbStump    = 1;   /* LIMB_STUMP */
const unsigned char kLimbReplaced = 2;   /* LIMB_REPLACED - a robotic limb is fitted */
const unsigned char kLimbCrushed  = 3;   /* LIMB_CRUSHED */
const unsigned char kLimbBlockVer = 1;   /* the first byte of the block; any other value reads as no block */
const unsigned int  kLimbSidMax   = 63;

struct LimbsWire
{
    unsigned char state[kLimbCount];
    char          sid[kLimbCount][kLimbSidMax + 1];   /* NUL-terminated; meaningful for kLimbReplaced only */
    float         quality[kLimbCount];
};

inline void LimbsClear(LimbsWire* w)
{
    if (w == 0) return;
    for (unsigned int i = 0; i < kLimbCount; ++i) { w->state[i] = kLimbOriginal; w->sid[i][0] = 0; w->quality[i] = 0.0f; }
}

inline size_t LimbSidLen(const char* s)
{
    size_t n = 0;
    if (s == 0) return 0;
    while (n < kLimbSidMax && s[n] != 0) ++n;
    return n;
}

/* Copies at most kLimbSidMax bytes of `s` into limb i's sid (a longer sid is cut, and the copy then cannot resolve it). */
inline void LimbSetSid(LimbsWire* w, unsigned int i, const char* s)
{
    if (w == 0 || i >= kLimbCount) return;
    const size_t n = LimbSidLen(s);
    if (n > 0) std::memcpy(w->sid[i], s, n);
    w->sid[i][n] = 0;
}

inline bool LimbsEqual(const LimbsWire& a, const LimbsWire& b)
{
    for (unsigned int i = 0; i < kLimbCount; ++i)
    {
        if (a.state[i] != b.state[i]) return false;
        if (a.state[i] == kLimbReplaced && (std::strcmp(a.sid[i], b.sid[i]) != 0 || a.quality[i] != b.quality[i])) return false;
    }
    return true;
}

/* Appends the block. A state above kLimbCrushed is written as kLimbOriginal (never a byte the decoder refuses). */
inline void EncodeLimbs(std::vector<char>* b, const LimbsWire& w)
{
    if (b == 0) return;
    b->push_back((char)kLimbBlockVer);
    for (unsigned int i = 0; i < kLimbCount; ++i) b->push_back((char)(w.state[i] <= kLimbCrushed ? w.state[i] : kLimbOriginal));
    for (unsigned int i = 0; i < kLimbCount; ++i)
    {
        if (w.state[i] != kLimbReplaced) continue;
        const size_t n = LimbSidLen(w.sid[i]);
        const size_t at = b->size();
        b->resize(at + 1 + n + 4);
        (*b)[at] = (char)(unsigned char)n;
        if (n > 0) std::memcpy(&(*b)[at + 1], w.sid[i], n);
        std::memcpy(&(*b)[at + 1 + n], &w.quality[i], 4);
    }
}

/* Reads the block at p[at]. true = *w holds it and *end (when given) is the first byte after it; false = no block (a
   payload that ends before it, another version byte, a state above kLimbCrushed, a sid over kLimbSidMax or a cut block)
   and *w is cleared. Every test is `size - off < n` with `off` already bounded by `size`, so a hostile length cannot wrap. */
inline bool DecodeLimbs(const char* p, size_t size, size_t at, LimbsWire* w, size_t* end)
{
    if (w == 0) return false;
    LimbsClear(w);
    if (p == 0 || at > size || size - at < 1 + kLimbCount || (unsigned char)p[at] != kLimbBlockVer) return false;
    size_t q = at + 1;
    LimbsWire r;
    LimbsClear(&r);
    for (unsigned int i = 0; i < kLimbCount; ++i)
    {
        const unsigned char s = (unsigned char)p[q + i];
        if (s > kLimbCrushed) return false;
        r.state[i] = s;
    }
    q += kLimbCount;
    for (unsigned int i = 0; i < kLimbCount; ++i)
    {
        if (r.state[i] != kLimbReplaced) continue;
        if (size - q < 1) return false;
        const size_t n = (unsigned char)p[q];
        if (n > kLimbSidMax || size - q - 1 < n + 4) return false;
        if (n > 0) std::memcpy(r.sid[i], p + q + 1, n);
        r.sid[i][n] = 0;
        std::memcpy(&r.quality[i], p + q + 1 + n, 4);
        q += 1 + n + 4;
    }
    *w = r;
    if (end != 0) *end = q;
    return true;
}

/* What the copy's game does to one limb, from its own limb and its owner's. */
const int kLimbStepNone          = 0;   /* already the same */
const int kLimbStepAmputate      = 1;   /* whole (or crushed) here, a stump on the owner: MedicalSystem::amputate, no severed item */
const int kLimbStepUnfit         = 2;   /* a robotic limb here, a stump on the owner: setRobotLimbItem(limb, 0), the old item destroyed */
const int kLimbStepFit           = 3;   /* the owner has a robotic limb fitted, none here: make the item, setRobotLimbItem(limb, item) */
const int kLimbStepRefit         = 4;   /* both fitted, different items: unfit then fit */
const int kLimbStepCannotRegrow  = 5;   /* whole on the owner, not here: the engine has no road back to flesh - left, counted */
const int kLimbStepNoCrushRoad   = 6;   /* crushed on the owner: the engine's crushLimb is an empty function - left, counted */
const int kLimbStepUnnamed       = 7;   /* fitted on the owner, but its item has no name on the wire - left, counted */

inline int PlanLimb(unsigned char copyState, const char* copySid, unsigned char ownerState, const char* ownerSid)
{
    const bool ownerNamed = ownerSid != 0 && ownerSid[0] != 0;
    if (ownerState > kLimbCrushed) return kLimbStepNone;
    if (copyState == ownerState)
    {
        if (ownerState != kLimbReplaced || !ownerNamed) return kLimbStepNone;
        if (copySid != 0 && std::strcmp(copySid, ownerSid) == 0) return kLimbStepNone;
        return kLimbStepRefit;
    }
    if (ownerState == kLimbOriginal) return kLimbStepCannotRegrow;
    if (ownerState == kLimbCrushed) return kLimbStepNoCrushRoad;
    if (ownerState == kLimbStump) return (copyState == kLimbReplaced) ? kLimbStepUnfit : kLimbStepAmputate;
    /* ownerState == kLimbReplaced */
    if (!ownerNamed) return kLimbStepUnnamed;
    return (copyState == kLimbReplaced) ? kLimbStepRefit : kLimbStepFit;
}

/* A same-item quality difference worth a refit: both qualities read (a number, 0 or more, under 100000) and more than
   0.001 apart. An unreadable quality on either side is no difference - the plan never refits on a guess. */
inline bool LimbQualityDiffers(float copyQuality, float ownerQuality)
{
    if (!(copyQuality == copyQuality) || copyQuality < 0.0f || copyQuality >= 100000.0f) return false;
    if (!(ownerQuality == ownerQuality) || ownerQuality < 0.0f || ownerQuality >= 100000.0f) return false;
    const float d = copyQuality - ownerQuality;
    return d > 0.001f || d < -0.001f;
}

/* PlanLimb, and a refit when both sides have the same named robotic limb fitted at different qualities (the copy's item is
   remade with the owner's quality). */
inline int PlanLimbQ(unsigned char copyState, const char* copySid, float copyQuality,
                     unsigned char ownerState, const char* ownerSid, float ownerQuality)
{
    const int step = PlanLimb(copyState, copySid, ownerState, ownerSid);
    if (step == kLimbStepNone && copyState == kLimbReplaced && ownerState == kLimbReplaced
        && ownerSid != 0 && ownerSid[0] != 0 && LimbQualityDiffers(copyQuality, ownerQuality))
        return kLimbStepRefit;
    return step;
}

/* true = an RTTI type name (".?AV<Class>@@") names the engine's robotic limb item class. */
inline bool LimbIsRobotItemClass(const char* rttiName)
{
    return rttiName != 0 && std::strstr(rttiName, "RobotLimbItem") != 0;
}

/* The limbtest lever's arguments: `cut <uid> <limb 0-3>`, `fit <uid> <limb 0-3> <sid>`, `show <uid>`. Anything else, a uid
   of 0, a limb out of range, a sid longer than kLimbSidMax or a trailing word is kLimbTestBad. */
const int kLimbTestBad  = 0;
const int kLimbTestCut  = 1;
const int kLimbTestFit  = 2;
const int kLimbTestShow = 3;

inline int LimbTestParse(const std::string& args, unsigned int* uid, int* limb, std::string* sid)
{
    std::istringstream is(args);
    std::string what, extra, s;
    unsigned int u = 0;
    int l = -1;
    if (!(is >> what >> u) || u == 0) return kLimbTestBad;
    if (what == "show")
    {
        if (is >> extra) return kLimbTestBad;
        if (uid != 0) *uid = u;
        return kLimbTestShow;
    }
    if (what != "cut" && what != "fit") return kLimbTestBad;
    if (!(is >> l) || l < 0 || l >= (int)kLimbCount) return kLimbTestBad;
    if (what == "fit" && (!(is >> s) || s.empty() || s.size() > kLimbSidMax)) return kLimbTestBad;
    if (is >> extra) return kLimbTestBad;
    if (uid != 0) *uid = u;
    if (limb != 0) *limb = l;
    if (sid != 0) *sid = s;
    return (what == "cut") ? kLimbTestCut : kLimbTestFit;
}

inline const char* LimbStepName(int step)
{
    switch (step)
    {
    case kLimbStepNone:         return "none";
    case kLimbStepAmputate:     return "amputate";
    case kLimbStepUnfit:        return "unfit";
    case kLimbStepFit:          return "fit";
    case kLimbStepRefit:        return "refit";
    case kLimbStepCannotRegrow: return "cannotRegrow";
    case kLimbStepNoCrushRoad:  return "noCrushRoad";
    case kLimbStepUnnamed:      return "unnamed";
    default:                    return "?";
    }
}

inline const char* LimbName(unsigned int i)
{
    static const char* const k[kLimbCount] = { "leftArm", "rightArm", "leftLeg", "rightLeg" };
    return i < kLimbCount ? k[i] : "?";
}

}   /* namespace cooplimb */
