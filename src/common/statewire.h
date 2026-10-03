/* src/common/statewire.h - M4 / K1 (read-carry 2026-09-22): THE MSG_STATE PAYLOAD.
 *
 *   uid u32 | partCount u32 | parts (partCount * partFloats f32) | blood f32 | prone i32 | dead i32
 *     | latchBits u32 | nextKnockoutAt f32          (F147 - optional on decode: absent = no latch)
 *                                             (T-178 crawl1, session 86: latchBits bit 6 = the owner's own inRagdoll,
 *                                              getupcrawl.h kStateOwnerRagdollBit; bits 0-5 the medical latch)
 *                                             (latchBits bit 7 = the owner's stealth mode, sneakwire.h kStateOwnerSneakBit)
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
};

inline void PoseClear(PoseWire* p)
{
    if (p == 0) return;
    p->kind = kPoseNone; p->action[0] = 0; p->bedKey[0] = 0; p->outerKey[0] = 0;
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
        && std::strcmp(a.outerKey, b.outerKey) == 0;
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
    cooplimb::LimbsWire limbs;  /* the owner's four limbs: state, and the fitted robotic limb's sid and quality */
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
                        if (l < size && cooplimb::DecodeLimbs(p, size, l, &m->limbs, 0)) m->limbsHas = 1;
                    }
                    else { PoseClear(&m->pose); m->poseAbsent = true; }
                }
            }
        }
    }
    return kStateDecodeOk;
}

}   /* namespace coopstate */
