/* src/common/statewire.h - M4 / K1 (read-carry 2026-09-22): THE MSG_STATE PAYLOAD.
 *
 *   uid u32 | partCount u32 | parts (partCount * partFloats f32) | blood f32 | prone i32 | dead i32
 *     | latchBits u32 | nextKnockoutAt f32          (F147 - optional on decode: absent = no latch)
 *                                             (T-178 crawl1, session 86: latchBits bit 6 = the owner's own inRagdoll,
 *                                              getupcrawl.h kStateOwnerRagdollBit; bits 0-5 the medical latch)
 *                                             (latchBits bit 7 = the owner's stealth mode, sneakwire.h kStateOwnerSneakBit)
 *                                             (latchBits bits 8-13 = the owner's six stance buttons, bit 14 = they were read:
 *                                              WithStance below)
 *                                             (latchBits bits 15-16 = the owner's ranged mode, bit 16 = it was read:
 *                                              WithRangedMode below)
 *                                             (latchBits bits 17-18 = the owner's character aiming its crossbow / bow,
 *                                              bit 18 = it was read: WithAim below)
 *                                             (latchBits bits 19-20 = the owner's character reloading its crossbow / bow,
 *                                              bit 20 = it was read: WithReload below)
 *                                             (latchBits bits 21-22 = the owner's character has god mode (T-1205 /god),
 *                                              bit 22 = it was carried: WithGod below)
 *     | koTimer f32                             (H029 - optional: absent = -1, "not carried")
 *     | carryingUid u32                         (K1, session protocol 47 - optional: absent = 0, flagged)
 *     | restHas u8 | restX f32 | restY f32 | restZ f32   (R3, session protocol 48 - optional: absent = no rest)
 *     | poseKind u8 | actLen u8 | action bytes | keyLen u8 | bedKey bytes   (POSE, session 50 - optional: absent = no word)
 *     | outerLen u8 | outerKey bytes           (BED1, session 51 - optional: absent = no outer key)
 *     | hungerHas u8 (1) | hunger f32 | fed f32   (par5 (parity P5), session 79 - optional: written only when the
 *                                             owner read it; absent or cut = not carried; MedicalSystem +0x60/+0x64)
 *     | limb block (src/common/limbwire.h)     (LIMBS, session 133 - optional: written when the owner read its limbs.
 *                                             Without hunger a single 0 byte stands in the hunger block's place first,
 *                                             so the block's position never depends on hunger; absent or malformed =
 *                                             not carried, the copy's limbs are left as they are)
 *     | toolLen u8 | tool bytes                (T-890 WORK TOOL, session 190 - optional: written when the pose names the
 *                                             work tool the owner's machine job hangs in its hands. Without limbs a
 *                                             single 0 byte stands in the limb block's place first (after the hunger
 *                                             block or its own 0 byte), so the tool's position never depends on them;
 *                                             absent, cut or over kPoseStrMax = no tool)
 *
 * The ACTPOSE block (POSE, read-poses 2026-09-23, user T262) is the owner's IN-PLACE pose: kind 0 none, 1 an action the
 * engine replays every tick (sleep on floor, a seat, machine / crafting work - AnimationClass +0x210 name), 2 bed mode
 * (Character +0x2F8 == 1) with the bed's P7n building key; a bed may also carry the action as a fallback. Both strings
 * are at most kPoseStrMax bytes, no NUL on the wire. Always written from 50 on; a null `pose` writes kind 0.
 * BED1 (T263/T264, session 51): outerKey is the P7n key of the building whose interior LAYOUT made the bed
 * (Building+0x238 -> Layout+0x90), empty when the bed is not layout furniture. A layout piece is not on the zone
 * list the copy's resolver walks; the outer building is, and the copy walks ITS interior pieces for the bed key.
 * Always written from 51 on (an empty string is one 0 byte); a payload that ends after bedKey reads an empty
 * outerKey, and an outerLen that does not fit reads the whole block as absent.
 *
 * The rest block (R3, read-ragdoll 2026-09-22) is where the owner's ragdoll SETTLED (its getPosition, AnimationClass
 * +0x98), restHas 1; restHas 0 = the owner's body is not a settled, uncarried ragdoll. Always written from 48 on.
 *
 * carryingUid is the uid of the body THE CARRIER (this STATE's uid) carries on its owner's game, 0 = nothing.
 * Built and parsed here only, so the offline suite (src/coop-test/test_main.cpp) hits the SAME bytes
 * net/session.cpp sends and reads (lesson 11).  Trailing bytes after carryingUid are ignored.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once

#include <cstring>
#include <vector>
#include "limbwire.h"   /* LIMBS: the limb block after the hunger block */

namespace coopstate {

const int kStateDecodeOk        = 0;
const int kStateDecodeTooShort  = 1;   /* fewer than the 8 fixed bytes */
const int kStateDecodeBadCount  = 2;   /* partCount 0 or above the caller's maximum */
const int kStateDecodePartsTrunc = 3;  /* the payload ends before blood / prone / dead do */

/* POSE (read-poses, session 50): the ACTPOSE block. */
const unsigned char kPoseNone   = 0;
const unsigned char kPoseAction = 1;
const unsigned char kPoseBed    = 2;
const unsigned char kPoseCage   = 3;   /* arrest2 (session 56): in a cage - bedKey / outerKey name the cage as for a bed */
const unsigned int  kPoseStrMax = 48;

struct PoseWire
{
    unsigned char kind;                /* kPoseNone / kPoseAction / kPoseBed / kPoseCage */
    char action[kPoseStrMax + 1];      /* NUL-terminated; may be empty (a bed with no action) */
    char bedKey[kPoseStrMax + 1];      /* NUL-terminated; the bed's P7n building key, kind 2 only */
    char outerKey[kPoseStrMax + 1];    /* BED1 (51): NUL-terminated; the key of the building whose layout made the bed, or empty */
    char tool[kPoseStrMax + 1];        /* T-890 (190): NUL-terminated; the item sid of the work tool the owner's machine job hangs in its hands, or empty */
};

inline void PoseClear(PoseWire* p)
{
    if (p == 0) return;
    p->kind = kPoseNone; p->action[0] = 0; p->bedKey[0] = 0; p->outerKey[0] = 0; p->tool[0] = 0;
}

inline size_t PoseStrLen(const char* s)
{
    size_t n = 0;
    if (s == 0) return 0;
    while (n < kPoseStrMax && s[n] != 0) ++n;
    return n;
}

inline bool PoseEqual(const PoseWire& a, const PoseWire& b)
{
    return a.kind == b.kind && std::strcmp(a.action, b.action) == 0 && std::strcmp(a.bedKey, b.bedKey) == 0
        && std::strcmp(a.outerKey, b.outerKey) == 0 && std::strcmp(a.tool, b.tool) == 0;
}

/* T-890 WORK TOOL: true = an RTTI type name (".?AV<Class>@@", policy.h PolicyClassNamePod) names the engine's operate-machine
   task - the job whose start makes its building's "special tool" and keeps it at the task's +0x88 (decomp_35b3d0:75-89). The
   name is matched whole: another task class's +0x88 is not a tool. */
inline bool PoseToolTaskClass(const char* rttiName)
{
    return rttiName != 0 && std::strcmp(rttiName, ".?AVTask_OperateMachine@@") == 0;
}

/* T-890 review folds 1-3 (copy side): whether a put-on goes ahead now. ownJobTool = the tool the copy's own operate-machine job
   holds (1 a tool, 0 none, -1 not read); check = the copy's own pre-check that the job's start asks before it hangs its tool
   (vtable +0x30: 1 refuses, 0 allows, -1 not read). This mod never hangs its tool beside a job's own (the take-off goes by the
   slot's name and could not tell the two apart), nor where the job's start would not hang one. Both waits are tried again later. */
const int kPoseToolGo = 0, kPoseToolWaitJob = 1, kPoseToolWaitRefused = 2;
inline int PoseToolPutOnGate(int ownJobTool, int check)
{
    if (ownJobTool != 0) return kPoseToolWaitJob;
    if (check != 0) return kPoseToolWaitRefused;
    return kPoseToolGo;
}

/* the take-off goes by the slot's name, so it would remove a job's own tool: it is made unless such a tool is seen (an unread
   field does not stop it - a copy runs no job of its own, and a copy this game takes over has the tool taken off before its AI
   runs) */
inline bool PoseToolTakeOffAllowed(int ownJobTool) { return ownJobTool != 1; }

/* review fold 2: the tool hangs on copy c only when c is the address it was hung on AND c's handle ids read now (handNowRead)
   equal those read then (handSet) - an engine destroy and a later SPAWN can put another copy at the same address */
inline bool PoseToolSameCopy(const void* on, const void* c, bool handSet, const unsigned int* handThen, int handNowRead,
                             const unsigned int* handNow)
{
    return c != 0 && c == on && handSet && handNowRead != 0 && handThen != 0 && handNow != 0
        && std::memcmp(handThen, handNow, 5 * sizeof(unsigned int)) == 0;
}

struct StateMsg
{
    unsigned int uid;
    unsigned int partCount;
    float        blood;
    int          prone;
    int          dead;
    unsigned int latchBits;
    float        nextKnockoutAt;
    float        koTimer;
    unsigned int carryingUid;
    bool         carryAbsent;   /* the sender predates protocol 47 (cannot happen past HELLO; kept for the test) */
    unsigned char restHas;      /* R3: 1 = rest[] is where the owner's ragdoll settled; 0 = none, or absent (pre-48) */
    float        rest[3];
    bool         poseAbsent;    /* POSE: no (or a malformed) ACTPOSE block - the copy's pose is left as it is */
    PoseWire     pose;          /* POSE (50): the owner's in-place pose; kind 0 when absent */
    unsigned char hungerHas;    /* par5 (79): 1 = hunger / fed below are the owner's; 0 = not carried */
    float        hunger;        /* MedicalSystem::hunger +0x60 */
    float        fed;           /* MedicalSystem::fed +0x64 */
    unsigned char limbsHas;     /* LIMBS (133): 1 = limbs below are the owner's; 0 = not carried (absent or malformed) */
    cooplimb::LimbsWire limbs;  /* the owner's four limbs: state, and the fitted robotic limb's sid, quality and level */
};

inline void EncodeState(std::vector<char>* b, unsigned int uid, const float* parts, unsigned int partCount,
                        unsigned int partFloats, float blood, int prone, int dead, unsigned int latchBits,
                        float nextKnockoutAt, float koTimer, unsigned int carryingUid, const float* rest = 0,
                        const PoseWire* pose = 0, const float* hunger = 0, const cooplimb::LimbsWire* limbs = 0)
{
    if (b == 0) return;
    const unsigned int nf = partCount * partFloats;
    size_t at = b->size();
    b->resize(at + 8 + (size_t)nf * 4 + 41);
    std::memcpy(&(*b)[at], &uid, 4);
    std::memcpy(&(*b)[at + 4], &partCount, 4);
    at += 8;
    if (nf > 0 && parts != 0) std::memcpy(&(*b)[at], parts, (size_t)nf * 4);
    at += (size_t)nf * 4;
    std::memcpy(&(*b)[at],      &blood,       4);
    std::memcpy(&(*b)[at + 4],  &prone,       4);
    std::memcpy(&(*b)[at + 8],  &dead,        4);
    std::memcpy(&(*b)[at + 12], &latchBits,   4);
    std::memcpy(&(*b)[at + 16], &nextKnockoutAt,  4);
    std::memcpy(&(*b)[at + 20], &koTimer,     4);
    std::memcpy(&(*b)[at + 24], &carryingUid, 4);
    const unsigned char has = (rest != 0) ? 1 : 0;   /* R3 (session 48): the rest block, always written */
    const float zero[3] = { 0.0f, 0.0f, 0.0f };
    (*b)[at + 28] = (char)has;
    std::memcpy(&(*b)[at + 29], (rest != 0) ? rest : zero, 12);
    /* POSE (session 50): the ACTPOSE block, always written; a null `pose` is kind 0 with two empty strings. */
    PoseWire none;
    PoseClear(&none);
    const PoseWire* pw = (pose != 0) ? pose : &none;
    const size_t al = PoseStrLen(pw->action), kl = PoseStrLen(pw->bedKey), ol = PoseStrLen(pw->outerKey);
    const size_t q = b->size();
    b->resize(q + 4 + al + kl + ol);   /* BED1 (51): + outerLen u8 + outerKey */
    (*b)[q] = (char)pw->kind;
    (*b)[q + 1] = (char)(unsigned char)al;
    if (al > 0) std::memcpy(&(*b)[q + 2], pw->action, al);
    (*b)[q + 2 + al] = (char)(unsigned char)kl;
    if (kl > 0) std::memcpy(&(*b)[q + 3 + al], pw->bedKey, kl);
    (*b)[q + 3 + al + kl] = (char)(unsigned char)ol;   /* BED1 (51) */
    if (ol > 0) std::memcpy(&(*b)[q + 4 + al + kl], pw->outerKey, ol);
    /* par5 (session 79): the owner's {hunger, fed}, after the outer key; a null `hunger` writes nothing. */
    if (hunger != 0)
    {
        const size_t e = b->size();
        b->resize(e + 9);
        (*b)[e] = (char)1;
        std::memcpy(&(*b)[e + 1], &hunger[0], 4);
        std::memcpy(&(*b)[e + 5], &hunger[1], 4);
    }
    /* LIMBS (133): the limb block after the hunger block; without hunger one 0 byte stands in its place first. A null
       `limbs` writes nothing. */
    if (limbs != 0)
    {
        if (hunger == 0) b->push_back((char)0);
        cooplimb::EncodeLimbs(b, *limbs);
    }
    /* T-890 WORK TOOL (190): the pose's work tool after the limb block; without limbs one 0 byte stands in the limb block's
       place first (and, without hunger too, the hunger block's 0 byte before it). An empty tool writes nothing. */
    const size_t tl = PoseStrLen(pw->tool);
    if (tl > 0)
    {
        if (limbs == 0)
        {
            if (hunger == 0) b->push_back((char)0);
            b->push_back((char)0);
        }
        b->push_back((char)(unsigned char)tl);
        b->insert(b->end(), pw->tool, pw->tool + tl);
    }
}

/* par5: a hunger / fed pair a copy may take - finite and not absurd (NaN fails both compares). */
inline bool HungerPlausible(float hunger, float fed)
{
    return hunger > -100000.0f && hunger < 100000.0f && fed > -100000.0f && fed < 100000.0f;
}

/* par6b (attempt 2, T500 P6): MedicalSystem::dead (+0x164, the medical system at Character+0x458) IS Character::hasDied's
   byte (+0x5BC). medicalUpdate 0x651570 sets it and then calls declareDead, and reaches either call only inside
   `if (dead == 0)` (build/decomp_651570.txt:128) - so a declareDead whose return address is one of those two call sites
   began on a LIVE character. The RVAs are 1.0.65 Steam: the instruction after the calls at 0x651A10 and 0x651C69.
   The two return addresses are the address table's MedUpdateDeathRet1/2 rows (stage 7/9; on Steam 1.0.65 the instruction
   after the calls at 0x651A10 and 0x651C69), passed in by the caller. `base` is the exe's base; base 0 or a row 0 = not
   armed and nothing is recognised. */
inline bool MedicalUpdateDeathCall(unsigned long long ret, unsigned long long base, unsigned long long ret1Rva, unsigned long long ret2Rva)
{
    if (base == 0 || ret <= base || ret1Rva == 0 || ret2Rva == 0) return false;
    const unsigned long long rva = ret - base;
    return rva == ret1Rva || rva == ret2Rva;
}

/* `parts` receives partCount * partFloats floats and must hold maxParts * partFloats. Every test is
   `size - off < n` with `off` already bounded by `size`, so a hostile count cannot wrap. */
inline int DecodeState(const char* p, size_t size, unsigned int maxParts, unsigned int partFloats,
                       float* parts, StateMsg* m)
{
    if (p == 0 || m == 0 || size < 8) return kStateDecodeTooShort;
    unsigned int uid = 0, n = 0;
    std::memcpy(&uid, p, 4);
    std::memcpy(&n, p + 4, 4);
    if (n == 0 || n > maxParts) return kStateDecodeBadCount;
    const size_t nfb = (size_t)n * partFloats * 4;
    if (size - 8 < nfb + 12) return kStateDecodePartsTrunc;
    m->uid = uid;
    m->partCount = n;
    if (parts != 0) std::memcpy(parts, p + 8, nfb);
    const size_t at = 8 + nfb;
    std::memcpy(&m->blood, p + at,     4);
    std::memcpy(&m->prone, p + at + 4, 4);
    std::memcpy(&m->dead,  p + at + 8, 4);
    m->latchBits = 0; m->nextKnockoutAt = 0.0f; m->koTimer = -1.0f; m->carryingUid = 0; m->carryAbsent = true;
    m->restHas = 0; m->rest[0] = 0.0f; m->rest[1] = 0.0f; m->rest[2] = 0.0f;
    if (size - at >= 20)
    {
        std::memcpy(&m->latchBits,  p + at + 12, 4);
        std::memcpy(&m->nextKnockoutAt, p + at + 16, 4);
    }
    if (size - at >= 24) std::memcpy(&m->koTimer, p + at + 20, 4);
    if (size - at >= 28) { std::memcpy(&m->carryingUid, p + at + 24, 4); m->carryAbsent = false; }
    if (size - at >= 41 && p[at + 28] != 0) { m->restHas = 1; std::memcpy(m->rest, p + at + 29, 12); }   /* R3 (48) */
    /* POSE (50): optional; a kind above kPoseCage (arrest2, 56), a string above kPoseStrMax or a cut block reads as absent. */
    m->poseAbsent = true;
    PoseClear(&m->pose);
    m->hungerHas = 0; m->hunger = 0.0f; m->fed = 0.0f;   /* par5 (79) */
    m->limbsHas = 0; cooplimb::LimbsClear(&m->limbs);   /* LIMBS (133) */
    if (size - at >= 44)
    {
        const size_t q = at + 41;
        const unsigned char kind = (unsigned char)p[q];
        const size_t al = (unsigned char)p[q + 1];
        if (kind <= kPoseCage && al <= kPoseStrMax && size - q - 2 >= al + 1)
        {
            const size_t kl = (unsigned char)p[q + 2 + al];
            if (kl <= kPoseStrMax && size - q - 3 - al >= kl)
            {
                m->pose.kind = kind;
                if (al > 0) std::memcpy(m->pose.action, p + q + 2, al);
                m->pose.action[al] = 0;
                if (kl > 0) std::memcpy(m->pose.bedKey, p + q + 3 + al, kl);
                m->pose.bedKey[kl] = 0;
                m->poseAbsent = false;
                /* BED1 (51): the outer key. Nothing after bedKey = a 50 payload, no outer key; a length that
                   does not fit = a malformed block, read as absent like any other cut block. */
                const size_t r = q + 3 + al + kl;
                if (size - r >= 1)
                {
                    const size_t ol = (unsigned char)p[r];
                    if (ol <= kPoseStrMax && size - r - 1 >= ol)
                    {
                        if (ol > 0) std::memcpy(m->pose.outerKey, p + r + 1, ol);
                        m->pose.outerKey[ol] = 0;
                        /* par5 (79): the hunger block after the outer key; a cut block reads as not carried. */
                        const size_t e = r + 1 + ol;
                        if (size - e >= 9 && p[e] == 1)
                        {
                            m->hungerHas = 1;
                            std::memcpy(&m->hunger, p + e + 1, 4);
                            std::memcpy(&m->fed, p + e + 5, 4);
                        }
                        /* LIMBS (133): the limb block after a carried hunger block, or after the single 0 byte. */
                        size_t l = size;
                        if (m->hungerHas == 1) l = e + 9;
                        else if (size - e >= 1 && p[e] == 0) l = e + 1;
                        size_t le = size;
                        if (l < size && cooplimb::DecodeLimbs(p, size, l, &m->limbs, &le)) m->limbsHas = 1;
                        /* T-890 WORK TOOL (190): after a carried limb block, or after the limb block's single 0 byte; a
                           cut block or a length over kPoseStrMax reads as no tool. */
                        size_t t = size;
                        if (m->limbsHas == 1) t = le;
                        else if (l < size && p[l] == 0) t = l + 1;
                        if (t < size)
                        {
                            const size_t tl = (unsigned char)p[t];
                            if (tl > 0 && tl <= kPoseStrMax && size - t - 1 >= tl)
                            {
                                std::memcpy(m->pose.tool, p + t + 1, tl);
                                m->pose.tool[tl] = 0;
                            }
                        }
                    }
                    else { PoseClear(&m->pose); m->poseAbsent = true; }
                }
            }
        }
    }
    return kStateDecodeOk;
}

/* latchBits bits 8-13: the owner's six stance buttons in the order block, ranged, taunt, hold, passive, chase (combat.cpp
   kStanceName), 1 = on; bit 14 (kStanceReadBit): the six were read on the owner's game - clear = not carried, and the copy's
   buttons are left as they are. Bits 0-7 (the medical latch, the ragdoll and sneak bits) are not touched. */
const unsigned int kStanceShift   = 8;
const int          kStanceCount   = 6;
const unsigned int kStanceReadBit = 1u << 14;
const unsigned int kStanceMask    = (0x3Fu << kStanceShift) | kStanceReadBit;

/* latchBits with its stance bits replaced by six button bytes (0 off, anything else on); read false = not carried. */
inline unsigned int WithStance(unsigned int latchBits, const unsigned char* six, bool read)
{
    unsigned int b = latchBits & ~kStanceMask;
    if (!read || six == 0) return b;
    for (int i = 0; i < kStanceCount; ++i)
        if (six[i] != 0) b |= 1u << (kStanceShift + (unsigned int)i);
    return b | kStanceReadBit;
}
inline bool StanceCarried(unsigned int latchBits) { return (latchBits & kStanceReadBit) != 0; }
/* button i (0..5, kStanceName order): 1 on, 0 off or out of range */
inline int StanceOn(unsigned int latchBits, int i)
{
    return (i >= 0 && i < kStanceCount && (latchBits & (1u << (kStanceShift + (unsigned int)i))) != 0) ? 1 : 0;
}
/* T-1101 step 4: the stance bits read just before and just after one call of the engine's stance setter - true when the call
   changed a button (after read, and before unread or different in bits 8-13). An unreadable 'after' is false: a STATE would not
   carry the buttons either. */
inline bool StanceChanged(unsigned int before, unsigned int after)
{
    if (!StanceCarried(after)) return false;
    return !StanceCarried(before) || ((before ^ after) & kStanceMask) != 0;
}
/* latchBits bit 15: the owner's ranged mode (Character +0x2F0 -> RangedCombatClass +0x36, the flag the engine's per-frame ranged
   update tests before it fetches the gun); bit 16 (kRangedModeReadBit): it was read on the owner's game - clear = not carried, and
   the copy's ranged mode is left as it is. Bits 0-14 are not touched. */
const unsigned int kRangedModeBit     = 1u << 15;
const unsigned int kRangedModeReadBit = 1u << 16;
const unsigned int kRangedModeMask    = kRangedModeBit | kRangedModeReadBit;
/* latchBits with its ranged-mode bits replaced: mode 1 (any non-zero) on, 0 off, negative = not read (not carried). */
inline unsigned int WithRangedMode(unsigned int latchBits, int mode)
{
    const unsigned int b = latchBits & ~kRangedModeMask;
    if (mode < 0) return b;
    return b | kRangedModeReadBit | (mode != 0 ? kRangedModeBit : 0u);
}
/* the owner's ranged mode a STATE carries: 1 on, 0 off, -1 not carried */
inline int RangedModeCarried(unsigned int latchBits)
{
    if ((latchBits & kRangedModeReadBit) == 0) return -1;
    return (latchBits & kRangedModeBit) != 0 ? 1 : 0;
}
/* the owner's side: two samples of its own character's ranged mode (-1 unreadable) - true when both read and they differ. */
inline bool RangedModeChanged(int seen, int now)
{
    return seen >= 0 && now >= 0 && (seen != 0) != (now != 0);
}
/* the copy's side: the byte to write into the copy's ranged mode for the owner's word `want` (-1 none) when the copy's own reads
   `here` (-1 unreadable): 1 or 0, or -1 = write nothing (no word, unreadable, or already the same). */
inline int RangedModeWrite(int want, int here)
{
    if (want < 0 || here < 0) return -1;
    if ((want != 0) == (here != 0)) return -1;
    return want != 0 ? 1 : 0;
}
/* latchBits bit 17: the owner's character aims its crossbow / bow (aimstamp.h OwnDecide: its ranged mode on and the engine's
   aim code played its aim motion within the last 250 ms); bit 18 (kAimReadBit): it was known on the owner's game - clear = not
   carried, and the copy keeps its owner's last word. Bits 0-16 are not touched. */
const unsigned int kAimBit     = 1u << 17;
const unsigned int kAimReadBit = 1u << 18;
const unsigned int kAimMask    = kAimBit | kAimReadBit;
/* latchBits with its aim bits replaced: aim 1 (any positive) aiming, 0 not, negative = not known (not carried). */
inline unsigned int WithAim(unsigned int latchBits, int aim)
{
    const unsigned int b = latchBits & ~kAimMask;
    if (aim < 0) return b;
    return b | kAimReadBit | (aim != 0 ? kAimBit : 0u);
}
/* the owner's aim a STATE carries: 1 aiming, 0 not, -1 not carried */
inline int AimCarried(unsigned int latchBits)
{
    if ((latchBits & kAimReadBit) == 0) return -1;
    return (latchBits & kAimBit) != 0 ? 1 : 0;
}
/* latchBits bit 19: the owner's character reloads its crossbow / bow (aimstamp.h OwnReloadDecide: the aim code's reload branch's
   own test passes and the reload check counted its time down within the last 250 ms); bit 20 (kReloadReadBit): it was known on
   the owner's game - clear = not carried, and the copy keeps its owner's last word. Bits 0-18 are not touched. */
const unsigned int kReloadBit     = 1u << 19;
const unsigned int kReloadReadBit = 1u << 20;
const unsigned int kReloadMask    = kReloadBit | kReloadReadBit;
/* latchBits with its reload bits replaced: 1 (any positive) reloading, 0 not, negative = not known (not carried). */
inline unsigned int WithReload(unsigned int latchBits, int reloading)
{
    const unsigned int b = latchBits & ~kReloadMask;
    if (reloading < 0) return b;
    return b | kReloadReadBit | (reloading != 0 ? kReloadBit : 0u);
}
/* the owner's reload a STATE carries: 1 reloading, 0 not, -1 not carried */
inline int ReloadCarried(unsigned int latchBits)
{
    if ((latchBits & kReloadReadBit) == 0) return -1;
    return (latchBits & kReloadBit) != 0 ? 1 : 0;
}
/* latchBits bit 21: the owner's character has god mode (T-1205 /god, mockups-t1205.md section 12 - the owning game's memory,
   medical.cpp's god table); bit 22 (kGodReadBit): carried - clear = not carried, and the copy keeps its last word. Every other
   game's AI reads it to leave that character alone (peace.cpp). Bits 0-20 are not touched. */
const unsigned int kGodBit     = 1u << 21;
const unsigned int kGodReadBit = 1u << 22;
const unsigned int kGodMask    = kGodBit | kGodReadBit;
/* latchBits with its god bits replaced: 1 (any positive) god mode on, 0 off, negative = not known (not carried) */
inline unsigned int WithGod(unsigned int latchBits, int god)
{
    const unsigned int b = latchBits & ~kGodMask;
    if (god < 0) return b;
    return b | kGodReadBit | (god != 0 ? kGodBit : 0u);
}
/* the owner's god mode a STATE carries: 1 on, 0 off, -1 not carried */
inline int GodCarried(unsigned int latchBits)
{
    if ((latchBits & kGodReadBit) == 0) return -1;
    return (latchBits & kGodBit) != 0 ? 1 : 0;
}
/* the copy's side: a copy whose ranged mode was written ON in safe-point pass onPass (0 = none) fires only from a later pass
   passNow - the gun it fires with is the one the engine's ranged update stores after the write. */
inline bool RangedModeOnSettled(unsigned int onPass, unsigned int passNow)
{
    return onPass == 0u || passNow != onPass;
}
/* the safe-point pass counter's next value; it skips 0, which means 'none' above. */
inline unsigned int RangedModePassNext(unsigned int pass)
{
    return (pass + 1u == 0u) ? 1u : pass + 1u;
}
/* the copy's side: how many times one owner word is written back after the copy's own engine changed the byte back; after that
   the word is not written again until it changes. */
const int kRangedModeRevertCap = 20;
inline bool RangedModeRevertsLeft(int reverts)
{
    return reverts < kRangedModeRevertCap;
}
/* T-1101 step 4: a uid put in a small set waiting for one STATE each - several buttons in one frame give one push.
   1 added, 0 already in it (or uid 0), -1 the set is full. */
inline int DirtyAdd(unsigned int* set, int* n, int cap, unsigned int uid)
{
    if (uid == 0) return 0;
    for (int i = 0; i < *n; ++i) if (set[i] == uid) return 0;
    if (*n >= cap) return -1;
    set[(*n)++] = uid;
    return 1;
}

}   /* namespace coopstate */
