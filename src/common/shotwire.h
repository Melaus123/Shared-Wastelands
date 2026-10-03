/* src/common/shotwire.h - P104 fix (session protocol 104; T668 measured 2026-09-30: A's bolt struck B's character, a COPY on A,
 * whose engine damage C2-b skips there, and nothing reached B - B's character took no damage on either game).
 *
 * MSG_SHOT (63, RELIABLE, the shooter's owner -> the victim's owner): Character::iShotYou 0x4398F0 ran on the shooter's game
 * for a victim that game does not drive. The victim's owner plays the hit through the engine's own wound path,
 * MedicalSystem::addWound 0x64FE40 with attack type 6 and the Damages iShotYou builds (combat.cpp, P104 block).
 *
 *   victimUid u32 | shooterUid u32 | rec 4 x f32 (the bolt's damage record: Harpoon +0x40 +0x44 +0x48 +0x4C) | flags u8
 *   flags bit 0 = onPurpose (iShotYou's 4th argument); no other bit may be set.
 *
 * rec[3] (+0x4C) is the one field iShotYou puts into the Damages (its third float); GunClass::shoot 0x43A390 writes all four
 * (decomp_43a390.txt:108-118) and +0x40..+0x48 ride for the log.
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <vector>

namespace coopshot {

const size_t        kShotWireSize      = 25;
const unsigned char kShotFlagOnPurpose = 1;
const float         kShotDamageMax     = 100000.0f;   /* a bolt's +0x4C above this is garbage (T668 measured 69.4) */

/* The Damages' fifth float as iShotYou sets it: DAT_141681ad0 = 2.0f (.rdata; build/answers-wake1.md, decomp_8f8370). */
const float kShotDamagesFifth = 2.0f;

const int kShotDecodeOk        = 0;
const int kShotDecodeTooShort  = 1;
const int kShotDecodeBadUid    = 2;   /* a zero uid, or victim == shooter */
const int kShotDecodeBadDamage = 3;   /* rec[3] NaN, negative, infinite or above kShotDamageMax */
const int kShotDecodeBadFlags  = 4;   /* a flag bit other than kShotFlagOnPurpose */

struct ShotMsg
{
    unsigned int victimUid;
    unsigned int shooterUid;
    float        rec[4];      /* Harpoon +0x40, +0x44, +0x48, +0x4C (the damage) */
    bool         onPurpose;
};

inline bool ShotDamageOk(float d) { return d == d && d >= 0.0f && d <= kShotDamageMax; }   /* d == d is false for NaN */

inline bool ShotMsgOk(const ShotMsg& m)
{
    return m.victimUid != 0 && m.shooterUid != 0 && m.victimUid != m.shooterUid && ShotDamageOk(m.rec[3]);
}

/* false (nothing appended) for a message that would not decode. */
inline bool EncodeShot(std::vector<char>* b, const ShotMsg& m)
{
    if (b == 0 || !ShotMsgOk(m)) return false;
    const size_t at = b->size();
    b->resize(at + kShotWireSize);
    char* p = &(*b)[at];
    std::memcpy(p, &m.victimUid, 4);
    std::memcpy(p + 4, &m.shooterUid, 4);
    std::memcpy(p + 8, m.rec, 16);
    p[24] = (char)(m.onPurpose ? kShotFlagOnPurpose : 0);
    return true;
}

/* Nothing is written on a refusal. A longer payload is read up to its first 25 bytes. */
inline int DecodeShot(const char* p, size_t size, ShotMsg* out)
{
    if (p == 0 || out == 0 || size < kShotWireSize) return kShotDecodeTooShort;
    ShotMsg m;
    std::memcpy(&m.victimUid, p, 4);
    std::memcpy(&m.shooterUid, p + 4, 4);
    std::memcpy(m.rec, p + 8, 16);
    const unsigned char f = (unsigned char)p[24];
    if ((f & (unsigned char)~kShotFlagOnPurpose) != 0) return kShotDecodeBadFlags;
    m.onPurpose = (f & kShotFlagOnPurpose) != 0;
    if (m.victimUid == 0 || m.shooterUid == 0 || m.victimUid == m.shooterUid) return kShotDecodeBadUid;
    if (!ShotDamageOk(m.rec[3])) return kShotDecodeBadDamage;
    *out = m;
    return kShotDecodeOk;
}

/* The Damages (6 floats, F085) Character::iShotYou builds from a bolt (decomp_4398f0.txt:25-33): {0, 0, +0x4C, 0, 2.0f, 0}. */
inline void ShotDamages(float boltDamage, float out6[6])
{
    out6[0] = 0.0f; out6[1] = 0.0f; out6[2] = boltDamage; out6[3] = 0.0f; out6[4] = kShotDamagesFifth; out6[5] = 0.0f;
}

/* T-311 (pvp1): which victim-side reactions Character::iShotYou runs for a bolt (decomp_4398f0.txt:34-45, 83), so the victim's
 * owner plays the same ones after the MSG_SHOT wound.
 *   rememberCharacter(victim, attacker, ST_TEMPORARY_ENEMY)  only when the shot was on purpose;
 *   AI::underRangedAttack(victim's AI, attacker, 1)          on purpose, an attacker, the victim's combat class +0x130 clear
 *                                                            (0 = read clear; -1 = unread: not run) and the attacker's type 1;
 *   the hit reaction 0x438E50                                always.
 * attackerPresent false (no copy of the shooter): nothing - the owner applies no wound either. */
const int kShotReactRemember    = 1;
const int kShotReactUnderRanged = 2;
const int kShotReactHitReaction = 4;
inline int ShotReactions(bool onPurpose, bool attackerPresent, int combatFlag130, int attackerType)
{
    if (!attackerPresent) return 0;
    int r = kShotReactHitReaction;
    if (onPurpose) r |= kShotReactRemember;
    if (onPurpose && combatFlag130 == 0 && attackerType == 1) r |= kShotReactUnderRanged;
    return r;
}

/* T-311 fold 1: WHY underRangedAttack is not run (attacker present), one reason each, for the REPORT's reactSkip counters.
 * kShotUrRun exactly when ShotReactions chooses kShotReactUnderRanged. Order: not on purpose, +0x130 set, +0x130 unread,
 * then an attacker type other than 1 (an unread type, -1, included). */
const int kShotUrRun              = 0;
const int kShotUrSkipNotOnPurpose = 1;
const int kShotUrSkipFlag130      = 2;
const int kShotUrSkipFlagUnread   = 3;
const int kShotUrSkipNotChar      = 4;
inline int ShotUnderRangedSkip(bool onPurpose, int combatFlag130, int attackerType)
{
    if (!onPurpose) return kShotUrSkipNotOnPurpose;
    if (combatFlag130 == 1) return kShotUrSkipFlag130;
    if (combatFlag130 != 0) return kShotUrSkipFlagUnread;
    if (attackerType != 1) return kShotUrSkipNotChar;
    return kShotUrRun;
}

} // namespace coopshot
