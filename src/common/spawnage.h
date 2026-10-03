/* src/common/spawnage.h - P1 (animal age): THE MSG_SPAWN PAYLOAD, ENCODED AND DECODED BY ONE PURE PAIR.
 *
 * The user saw it in T244: every copied animal was newborn-sized.  build/read-parity3.md GAP 1
 * (decompile-Confirmed): an animal's drawn size is scaleMin + (scaleMax - scaleMin) x AGE, AGE is the
 * float at CharacterAnimal +0x700 (getAge0to1 0x5E1580 returns it; the constructor 0x5C76C0 stores its
 * `_age` argument there, and the factory 0x580CE0 passes create()'s last argument into it), and the
 * mirror created every copy with age 0.0.  Age travelled in no message.
 *
 * So the SPAWN payload grows ONE trailing f32 - the owner's age at the moment of sending - and the
 * whole payload is built and parsed here, so the offline suite (src/coop-test/test_main.cpp) hits the
 * SAME bytes net/session.cpp sends and reads.  SendSpawn and OnSpawn CALL these; neither carries a
 * second copy (lesson 11).
 *
 *   uid u32 | x y z f32 | nameLen u32 | name | facLen u32 | faction | keepContainer u8 | age f32 (P1)
 *       | statsHas u8 | [176-byte stats block when statsHas != 0] (S1, session 49: src/common/statswire.h)
 *       | flags u8 (T-303, session protocol 102: bit 0 = the owner's character is DEAD, bit 1 = knocked out)
 *
 * An older payload without the trailing age decodes to 0.0 (the value every copy had before) and says
 * so (ageAbsent), so a caller can count it.  A present age that is NaN, infinite or outside [0,1] is
 * clamped and says so (ageClamped).  Session protocol 45 marks the growth.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no C++11 anything.
 */
#pragma once

#include <cstring>
#include <string>
#include <vector>

#include "statswire.h"   /* S1 (read-stats): the SPAWN tail's 44-value stats block */

namespace coopspawn {

/* Decode outcomes.  Only kSpawnDecodeOk carries a usable message. */
const int kSpawnDecodeOk            = 0;
const int kSpawnDecodeTooShort      = 1;   /* fewer than the 20 fixed bytes */
const int kSpawnDecodeNameTrunc     = 2;
const int kSpawnDecodeNoModifier    = 3;   /* no faction length */
const int kSpawnDecodeFactionTrunc  = 4;   /* faction or the keepContainer byte missing */

/* S1: what the tail after the age held.  A SPAWN still decodes whole in every case; only kSpawnStatsOk
   carries values. */
const int kSpawnStatsAbsent = 0;   /* nothing after the age: an older payload */
const int kSpawnStatsNone   = 1;   /* statsHas 0: the owner could not read its character */
const int kSpawnStatsOk     = 2;
const int kSpawnStatsBad    = 3;   /* statsHas set, but the block was truncated or held a NaN / infinite float */

/* T-303 (protocol 102): the owner's flags byte after the stats tail. A copy is created as the character is on its
   owner's game - a corpse is never announced standing (T665: 7.5 s alive until the owner's next STATE).
   T-303 fold 1: kept after the whole kSpawnStats* set. */
const unsigned int kSpawnFlagDead  = 1u;   /* Character::hasDied on the owner's game */
const unsigned int kSpawnFlagKo    = 2u;   /* unconscious, or the owner's wake-up clock running (the NoteOwnerKo test) */
const unsigned int kSpawnFlagsKnown = 3u;  /* any other bit is ignored on decode */

struct SpawnMsg
{
    unsigned int uid;
    float x, y, z;
    std::string templateName;
    std::string factionName;
    bool keepContainer;
    float age;          /* 0..1, always - clamped on decode */
    bool ageAbsent;     /* the payload ended before the age (a pre-45 sender): age reads 0.0 */
    bool ageClamped;    /* the age was present but NaN / infinite / outside [0,1] */
    int statsState;     /* S1: kSpawnStats* */
    unsigned int ownerFlags;   /* T-303: kSpawnFlag* (unknown bits masked off); 0 when absent */
    bool flagsAbsent;          /* T-303: the payload ended before the flags byte */
    int statsBadIndex;  /* S1: the first non-finite float of a refused block, -1 otherwise */
    unsigned int stats[coopstats::kStatsCount];   /* S1: the owner's 44 values when statsState == kSpawnStatsOk */
    SpawnMsg() : uid(0), x(0), y(0), z(0), keepContainer(true), age(0.0f), ageAbsent(false), ageClamped(false),
                 statsState(kSpawnStatsAbsent), ownerFlags(0), flagsAbsent(true), statsBadIndex(-1) { std::memset(stats, 0, sizeof(stats)); }
};

/* [0,1], with NaN and the infinities caught by their BITS (a `v != v` test is not safe under /fp:fast).
   A non-finite value reads 0.0; a finite one outside the range is pinned to the nearer end. */
inline float SpawnAgeClamp(float v, bool* clamped)
{
    unsigned int bits = 0;
    std::memcpy(&bits, &v, 4);
    if ((bits & 0x7F800000u) == 0x7F800000u) { if (clamped) *clamped = true; return 0.0f; }
    if (v < 0.0f) { if (clamped) *clamped = true; return 0.0f; }
    if (v > 1.0f) { if (clamped) *clamped = true; return 1.0f; }
    if (clamped) *clamped = false;
    return v;
}

inline void SpawnPutU32(std::vector<char>* b, unsigned int v)
{
    const size_t at = b->size();
    b->resize(at + 4);
    std::memcpy(&(*b)[at], &v, 4);
}

inline void SpawnPutF32(std::vector<char>* b, float v)
{
    const size_t at = b->size();
    b->resize(at + 4);
    std::memcpy(&(*b)[at], &v, 4);
}

inline void EncodeSpawn(std::vector<char>* b, unsigned int uid, float x, float y, float z,
                        const std::string& templateName, const std::string& factionName,
                        bool keepContainer, float age)
{
    SpawnPutU32(b, uid);
    SpawnPutF32(b, x);
    SpawnPutF32(b, y);
    SpawnPutF32(b, z);
    SpawnPutU32(b, (unsigned int)templateName.size());
    b->insert(b->end(), templateName.begin(), templateName.end());
    /* F115: modifiers travel with the spawn; faction goes by NAME. */
    SpawnPutU32(b, (unsigned int)factionName.size());
    b->insert(b->end(), factionName.begin(), factionName.end());
    b->push_back(keepContainer ? 1 : 0);
    SpawnPutF32(b, age);   /* P1: the owner's age, CharacterAnimal +0x700 (0.0 for a human / unreadable) */
}

/* S1 (read-stats): appended AFTER EncodeSpawn - statsHas u8, then the 176-byte block when raw44 is given
   (0 = the owner could not read its character's stats: statsHas 0, no block). */
inline void AppendSpawnStats(std::vector<char>* b, const unsigned int* raw44)
{
    b->push_back(raw44 != 0 ? 1 : 0);
    if (raw44 != 0) coopstats::EncodeStatsBlock(b, raw44);
}

/* Every length test is written as `size < at + n` on size_t with n small and `at` already bounded by
   size, so a hostile 32-bit length cannot wrap it (review-session S1). */
/* T-303 (protocol 102): appended AFTER AppendSpawnStats - the owner's kSpawnFlag* bits of the character. */
inline void AppendSpawnFlags(std::vector<char>* b, unsigned int flags)
{
    b->push_back((char)(unsigned char)(flags & kSpawnFlagsKnown));
}

inline int DecodeSpawn(const char* p, size_t size, SpawnMsg* out)
{
    SpawnMsg m;
    unsigned int nameLen = 0, facLen = 0;
    if (p == 0 || size < 20) return kSpawnDecodeTooShort;
    std::memcpy(&m.uid, p + 0, 4);
    std::memcpy(&m.x, p + 4, 4);
    std::memcpy(&m.y, p + 8, 4);
    std::memcpy(&m.z, p + 12, 4);
    std::memcpy(&nameLen, p + 16, 4);
    if (size - 20 < (size_t)nameLen) return kSpawnDecodeNameTrunc;
    m.templateName.assign(p + 20, nameLen);
    size_t off = (size_t)20 + (size_t)nameLen;
    if (size - off < 4) return kSpawnDecodeNoModifier;
    std::memcpy(&facLen, p + off, 4);
    off += 4;
    if (size - off < (size_t)facLen + 1) return kSpawnDecodeFactionTrunc;
    if (facLen > 0) m.factionName.assign(p + off, facLen);
    off += facLen;
    m.keepContainer = (p[off] != 0);
    off += 1;
    if (size - off >= 4)
    {
        float raw = 0.0f;
        std::memcpy(&raw, p + off, 4);
        m.age = SpawnAgeClamp(raw, &m.ageClamped);
        off += 4;
        if (size - off >= 1)   /* S1: statsHas, then the block */
        {
            const unsigned char has = (unsigned char)p[off];
            off += 1;
            if (has == 0) m.statsState = kSpawnStatsNone;
            else if (coopstats::DecodeStatsBlock(p + off, size - off, m.stats, &m.statsBadIndex) == coopstats::kStatsDecodeOk)
                m.statsState = kSpawnStatsOk;
            else m.statsState = kSpawnStatsBad;
            /* T-303 (protocol 102): the flags byte follows the stats tail. A block that is present is skipped whole (a
               NaN one too - its length is fixed); a truncated one leaves no byte to read, so the flags read absent. */
            if (has != 0) off = (size - off >= coopstats::kStatsBlockBytes) ? off + coopstats::kStatsBlockBytes : size;
            if (size - off >= 1)
            {
                m.ownerFlags = (unsigned int)(unsigned char)p[off] & kSpawnFlagsKnown;
                m.flagsAbsent = false;
                off += 1;
            }
        }
    }
    else
    {
        m.age = 0.0f;
        m.ageAbsent = true;
    }
    if (out) *out = m;
    return kSpawnDecodeOk;
}

}   /* namespace coopspawn */
