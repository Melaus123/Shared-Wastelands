/* src/common/meleewire.h - MSG_MELEE_DECIDED (77, RELIABLE, the attacker's game -> the victim's owner) and the rule that says
 * which game decides one melee blow.
 *
 * A player character's melee blow on an NPC that another game runs is decided on the ATTACKER's game: the engine's hit
 * (Character::hitByMeleeAttack 0x439150) runs there on the NPC's copy - the block roll, the impact, the attacker's own
 * reactions; the copy's own damage is skipped there (medical.cpp detour_applyDamage) - and a blow that landed
 * (kMeleeHitLanded) travels to the NPC's game, which applies the wound through MedicalSystem::addWound 0x64FE40 and ignores its
 * own result for the swing of the attacker's copy. An NPC's blow on a player character and a blow between two player characters
 * keep the victim's game deciding.
 *
 *   victimUid u32 | attackerUid u32 | cutDir i32 | comboId i32 | damages 6 x f32 (the hit's Damages, 24 bytes) | techFlag u8
 *   little-endian, 41 bytes. techFlag is the byte the hit hands addWound as its 2nd argument (the technique's +0x31 byte, 0 / 1).
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <vector>

namespace meleewire {

const size_t kMeleeWireSize  = 41;
const float  kMeleeDamageMax = 100000.0f;   /* a Damages field beyond this either way is garbage */

/* Character::hitByMeleeAttack's return (decomp_439150.txt:58-60 and 207-226): the victim's combat class answers 0x665510 first;
   5 = it blocked the blow (the 'Parry' / impact sounds, no wound), 0 = no hit, anything else = the wound path ran (addWound),
   and the hit returns 2. Only kMeleeHitLanded is sent. */
const int kMeleeHitNone    = 0;
const int kMeleeHitLanded  = 2;
const int kMeleeHitBlocked = 5;

const int kMeleeDecodeOk        = 0;
const int kMeleeDecodeTooShort  = 1;
const int kMeleeDecodeBadUid    = 2;   /* a zero uid, or victim == attacker */
const int kMeleeDecodeBadDamage = 3;   /* a Damages field NaN, infinite or beyond kMeleeDamageMax */
const int kMeleeDecodeBadFlag   = 4;   /* techFlag other than 0 / 1 */

struct MeleeMsg
{
    unsigned int  victimUid;
    unsigned int  attackerUid;
    int           cutDir;
    int           comboId;
    float         damages[6];
    unsigned char techFlag;
};

inline bool MeleeDamageOk(float d) { return d == d && d >= -kMeleeDamageMax && d <= kMeleeDamageMax; }   /* d == d is false for NaN */

inline bool MeleeMsgOk(const MeleeMsg& m)
{
    if (m.victimUid == 0 || m.attackerUid == 0 || m.victimUid == m.attackerUid || m.techFlag > 1) return false;
    for (int i = 0; i < 6; ++i) if (!MeleeDamageOk(m.damages[i])) return false;
    return true;
}

/* false (nothing appended) for a message that would not decode. */
inline bool EncodeMelee(std::vector<char>* b, const MeleeMsg& m)
{
    if (b == 0 || !MeleeMsgOk(m)) return false;
    const size_t at = b->size();
    b->resize(at + kMeleeWireSize);
    char* p = &(*b)[at];
    std::memcpy(p, &m.victimUid, 4);
    std::memcpy(p + 4, &m.attackerUid, 4);
    std::memcpy(p + 8, &m.cutDir, 4);
    std::memcpy(p + 12, &m.comboId, 4);
    std::memcpy(p + 16, m.damages, 24);
    p[40] = (char)m.techFlag;
    return true;
}

/* Nothing is written on a refusal. A longer payload is read up to its first 41 bytes. */
inline int DecodeMelee(const char* p, size_t size, MeleeMsg* out)
{
    if (p == 0 || out == 0 || size < kMeleeWireSize) return kMeleeDecodeTooShort;
    MeleeMsg m;
    std::memcpy(&m.victimUid, p, 4);
    std::memcpy(&m.attackerUid, p + 4, 4);
    std::memcpy(&m.cutDir, p + 8, 4);
    std::memcpy(&m.comboId, p + 12, 4);
    std::memcpy(m.damages, p + 16, 24);
    m.techFlag = (unsigned char)p[40];
    if (m.techFlag > 1) return kMeleeDecodeBadFlag;
    if (m.victimUid == 0 || m.attackerUid == 0 || m.victimUid == m.attackerUid) return kMeleeDecodeBadUid;
    for (int i = 0; i < 6; ++i) if (!MeleeDamageOk(m.damages[i])) return kMeleeDecodeBadDamage;
    *out = m;
    return kMeleeDecodeOk;
}

/* WHICH GAME DECIDES ONE BLOW, asked inside the engine's hit on the game where it runs.
 *   attackerMine / victimMine: this game runs that character (false for a copy, or no uid);
 *   attackerPlayer / victimPlayer: 1 a player character (or a player-faction person), 0 an NPC, -1 not known (yet) - which keeps
 *   today's rule on THIS game. Each game classifies on its own sweep, so for up to one sweep the two games can answer differently
 *   for one swing: see MeleeReceiverApplies and MeleeKeptUnclassified below.
 * kMeleeKeep   - today's rule: the victim's game decides (a blow on a copy makes no result here; a blow on our own is ours);
 * kMeleeSend   - our player character struck the copy of an NPC another game runs: the hit runs here, a landed blow is sent;
 * kMeleeIgnore - the copy of another game's player character struck our NPC: that game decides, our result is not applied. */
const int kMeleeKeep   = 0;
const int kMeleeSend   = 1;
const int kMeleeIgnore = 2;
inline int MeleeDecide(bool attackerMine, int attackerPlayer, bool victimMine, int victimPlayer)
{
    if (attackerPlayer != 1 || victimPlayer != 0) return kMeleeKeep;
    if (attackerMine && !victimMine) return kMeleeSend;
    if (!attackerMine && victimMine) return kMeleeIgnore;
    return kMeleeKeep;
}

/* The NPC's game, before it applies a MELEE_DECIDED blow: only for a pair its own rule answers kMeleeIgnore for - the pair whose
 * own result it did not apply. Any other answer (the attacker or the victim not classified here yet, or a pair this game decides
 * itself) means this game's own result for that swing stands, and the blow is refused (why=rule) instead of counted twice. */
inline bool MeleeReceiverApplies(bool attackerMine, int attackerPlayer, bool victimMine, int victimPlayer)
{
    return MeleeDecide(attackerMine, attackerPlayer, victimMine, victimPlayer) == kMeleeIgnore;
}

/* The attacker's game: our character's blow on another game's copy kept today's rule (nothing sent) only because the attacker or
 * the copy is not classified here yet - with both known it could have been kMeleeSend. The NPC's game may already have classified
 * both and ignore its own result for that swing, and then the blow lands nowhere; counted (meleeKeptUnclassified) so a run shows
 * it. */
inline bool MeleeKeptUnclassified(bool attackerMine, int attackerPlayer, bool victimMine, int victimPlayer)
{
    if (!attackerMine || victimMine) return false;
    if (attackerPlayer == 0 || victimPlayer == 1) return false;
    return attackerPlayer == -1 || victimPlayer == -1;
}

}   /* namespace meleewire */
