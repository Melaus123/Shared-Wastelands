/* src/common/statswire.h - S1 (read-stats, T260): A CHARACTER'S 44 SAVED STAT VALUES AS ONE 176-BYTE BLOCK.
 *
 * T260 (Confirmed): 290 of 363 characters had a different athletics on the owner and on its copy.  The read
 * (read-stats, Read) found why: CharStats::init 0x64C0A0 always re-rolls 32 fields (_randomiseStats 0x643260),
 * so each game rolled its own random stats for the same character, and no message carried them.
 *
 * The block is the set the engine's own save writes (CharStats::serialise 0x649E60) and its load path reads
 * back (CharStats::updateStats 0x64AC10), 44 values of 4 bytes each, in THIS order:
 *
 *   index  0..38   39 floats at CharStats +0x80 .. +0x118 (skills and attributes; athletics is +0x94 = index 5)
 *   index 39       attack          +0x120 (float)
 *   index 40       defence         +0x124 (float)
 *   index 41       warrior spirit  +0x13C (float)
 *   index 42       xp              +0x194 (float)
 *   index 43       free attribute points +0x198 (int)
 *
 * The values travel as their raw 4-byte patterns (little-endian, as both games are x64 Windows), so a float
 * arrives bit-for-bit.  Every float is checked on decode: NaN or an infinity (caught by its BITS - a `v != v`
 * test is not safe under /fp:fast) refuses the whole block, and the index of the first bad value is reported.
 *
 * Pure: no engine memory, no Windows.  The offline suite (src/coop-test/test_main.cpp) hits the same bytes the
 * plugin sends (net/session.cpp SendStats / OnStats, coopspawn's SPAWN tail) and applies (stats.cpp).
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no C++11 anything.
 */
#pragma once

#include <cstring>
#include <vector>

namespace coopstats {

const int kStatsCount = 44;
const size_t kStatsBlockBytes = 176;          /* 44 x 4 */
const size_t kStatsMsgBytes = 4 + 176;        /* MSG_STATS: uid u32 + block */
const int kStatsAthleticsIndex = 5;           /* CharStats +0x94 */
const int kStatsFreePointsIndex = 43;         /* the one int */

/* Decode outcomes. */
const int kStatsDecodeOk       = 0;
const int kStatsDecodeTooShort = 1;           /* fewer than the bytes the block needs */
const int kStatsDecodeBadFloat = 2;           /* a float was NaN or infinite - the whole block is refused */

/* The CharStats offset of value i (0..43), 0 for an index out of range. */
inline unsigned int StatsOffset(int i)
{
    if (i < 0 || i >= kStatsCount) return 0;
    if (i <= 38) return 0x80u + 4u * (unsigned int)i;
    switch (i)
    {
    case 39: return 0x120u;
    case 40: return 0x124u;
    case 41: return 0x13Cu;
    case 42: return 0x194u;
    default: return 0x198u;
    }
}

inline bool StatsIsFloat(int i) { return i >= 0 && i < kStatsFreePointsIndex; }

inline bool StatsBitsFinite(unsigned int bits) { return (bits & 0x7F800000u) != 0x7F800000u; }

inline float StatsBitsToFloat(unsigned int bits) { float f = 0.0f; std::memcpy(&f, &bits, 4); return f; }

/* -1 when every float in raw[44] is finite, else the index of the first that is not. */
inline int StatsFirstBadFloat(const unsigned int* raw)
{
    int i;
    for (i = 0; i < kStatsFreePointsIndex; ++i)
        if (!StatsBitsFinite(raw[i])) return i;
    return -1;
}

/* Appends the 176-byte block. */
inline void EncodeStatsBlock(std::vector<char>* b, const unsigned int* raw)
{
    const size_t at = b->size();
    b->resize(at + kStatsBlockBytes);
    std::memcpy(&(*b)[at], raw, kStatsBlockBytes);
}

/* Reads the block at p.  raw is written only when the block is whole; badIndex (optional) names the first
   non-finite float on kStatsDecodeBadFloat, -1 otherwise. */
inline int DecodeStatsBlock(const char* p, size_t size, unsigned int* raw, int* badIndex)
{
    if (badIndex) *badIndex = -1;
    if (p == 0 || size < kStatsBlockBytes) return kStatsDecodeTooShort;
    unsigned int tmp[kStatsCount];
    std::memcpy(tmp, p, kStatsBlockBytes);
    const int bad = StatsFirstBadFloat(tmp);
    if (bad >= 0) { if (badIndex) *badIndex = bad; return kStatsDecodeBadFloat; }
    if (raw) std::memcpy(raw, tmp, kStatsBlockBytes);
    return kStatsDecodeOk;
}

/* MSG_STATS: uid u32 | block (180 bytes). */
inline void EncodeStatsMsg(std::vector<char>* b, unsigned int uid, const unsigned int* raw)
{
    const size_t at = b->size();
    b->resize(at + 4);
    std::memcpy(&(*b)[at], &uid, 4);
    EncodeStatsBlock(b, raw);
}

inline int DecodeStatsMsg(const char* p, size_t size, unsigned int* uid, unsigned int* raw, int* badIndex)
{
    if (badIndex) *badIndex = -1;
    if (p == 0 || size < kStatsMsgBytes) return kStatsDecodeTooShort;
    unsigned int u = 0;
    std::memcpy(&u, p, 4);
    const int r = DecodeStatsBlock(p + 4, size - 4, raw, badIndex);
    if (r == kStatsDecodeOk && uid) *uid = u;
    return r;
}

/* The owner's send rule: true when the new values differ enough from the last ones sent - a float whose
   whole number changed or that moved by 0.05 or more, or any change of the int.  A float that is not finite
   on either side counts as changed only when its bits changed (the caller never sends a non-finite block). */
inline bool StatsWorthSending(const unsigned int* last, const unsigned int* now)
{
    int i;
    for (i = 0; i < kStatsCount; ++i)
    {
        if (last[i] == now[i]) continue;
        if (!StatsIsFloat(i)) return true;
        if (!StatsBitsFinite(last[i]) || !StatsBitsFinite(now[i])) return true;
        const float a = StatsBitsToFloat(last[i]), c = StatsBitsToFloat(now[i]);
        const double fa = (double)a, fc = (double)c;
        /* whole number: floor, done by hand so a negative value floors the right way */
        double wa = (double)(long long)fa; if (wa > fa) wa -= 1.0;
        double wc = (double)(long long)fc; if (wc > fc) wc -= 1.0;
        if (wa != wc) return true;
        const double d = fc - fa;
        if (d >= 0.05 || d <= -0.05) return true;
    }
    return false;
}

}   /* namespace coopstats */
