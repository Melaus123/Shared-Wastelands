/* src/common/treatwire.h - heal1 (user T305/T307: the guards bandaged the jailed character forever and the bandage never
 * took). A medic on one game treats its COPY of a character the other game drives; the owner's next STATE (every <= 5 s)
 * carries its own, untreated `bandageLevel` / `splintLevel` per part back onto the copy (spawn.cpp ApplyHealth), so the
 * treatment is erased and the medic starts again. The medic's game sends what the copy now holds, and the owner raises
 * its own character's values to it (never lowers them); its next STATE then carries the treatment to every copy.
 *
 *   uid u32 | n u8 (1..kTreatMaxParts) | n x (f32 bandageLevel, f32 splintLevel)
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <vector>

namespace cooptreat {

const unsigned int kTreatMaxParts = 32;   /* as spawn.cpp's STATE buffers */

const int kTreatDecodeOk       = 0;
const int kTreatDecodeTooShort = 1;
const int kTreatDecodeBadCount = 2;   /* n == 0 or above kTreatMaxParts */
const int kTreatDecodeBadValue = 3;   /* a value that is not finite, or below 0 */

struct TreatMsg
{
    unsigned int uid;
    unsigned int n;
    float bandageLevel[kTreatMaxParts];
    float splintLevel[kTreatMaxParts];
    TreatMsg() : uid(0), n(0) { for (unsigned int i = 0; i < kTreatMaxParts; ++i) { bandageLevel[i] = 0.0f; splintLevel[i] = 0.0f; } }
};

inline bool TreatValueOk(float v) { return v == v && v >= 0.0f && v < 1.0e30f; }

/* false (nothing appended) for a bad count or a bad value. */
inline bool EncodeTreat(std::vector<char>* b, const TreatMsg& m)
{
    if (b == 0 || m.n == 0 || m.n > kTreatMaxParts) return false;
    for (unsigned int i = 0; i < m.n; ++i)
        if (!TreatValueOk(m.bandageLevel[i]) || !TreatValueOk(m.splintLevel[i])) return false;
    const size_t at = b->size();
    b->resize(at + 4 + 1 + (size_t)m.n * 8);
    char* p = &(*b)[at];
    std::memcpy(p, &m.uid, 4); p += 4;
    *p++ = (char)(unsigned char)m.n;
    for (unsigned int i = 0; i < m.n; ++i)
    {
        std::memcpy(p, &m.bandageLevel[i], 4); p += 4;
        std::memcpy(p, &m.splintLevel[i], 4); p += 4;
    }
    return true;
}

inline int DecodeTreat(const char* p, size_t size, TreatMsg* m)
{
    if (p == 0 || m == 0 || size < 5) return kTreatDecodeTooShort;
    std::memcpy(&m->uid, p, 4);
    const unsigned int n = (unsigned int)(unsigned char)p[4];
    if (n == 0 || n > kTreatMaxParts) return kTreatDecodeBadCount;
    if (size < 5 + (size_t)n * 8) return kTreatDecodeTooShort;
    m->n = n;
    const char* q = p + 5;
    for (unsigned int i = 0; i < n; ++i)
    {
        std::memcpy(&m->bandageLevel[i], q, 4); q += 4;
        std::memcpy(&m->splintLevel[i], q, 4); q += 4;
        if (!TreatValueOk(m->bandageLevel[i]) || !TreatValueOk(m->splintLevel[i])) return kTreatDecodeBadValue;
    }
    return kTreatDecodeOk;
}

/* The medic's game keeps a sent treatment PENDING until the owner's STATE carries it (TreatConfirmedBy on every value it raised).
   Each owner STATE that does not show it is counted (an event, never a clock); after kTreatOwnerStates of them the same TREAT is
   sent again, and after kTreatSendsMax sends the treatment is given up (the owner's word stands). */
const int kTreatOwnerStates = 2;
const int kTreatSendsMax    = 3;

const int kTreatAskNone   = 0;   /* nothing pending */
const int kTreatAskWait   = 1;
const int kTreatAskResend = 2;
const int kTreatAskGiveUp = 3;

inline int TreatAskStep(bool pending, int ownerStatesSinceSend, int sends)
{
    if (!pending) return kTreatAskNone;
    if (ownerStatesSinceSend < kTreatOwnerStates) return kTreatAskWait;
    if (sends < kTreatSendsMax) return kTreatAskResend;
    return kTreatAskGiveUp;
}

/* The owner's value carries the held one (a held 0 is "not raised here" and always carried). */
inline bool TreatConfirmedBy(float owner, float held, float eps)
{
    return !(owner + eps < held);
}

} // namespace cooptreat
