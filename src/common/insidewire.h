/* src/common/insidewire.h - P25 fold 2 (T776 FAIL, 2026-10-01). The interior keep (replicate.cpp InteriorKeepTick) used to ask
 * the COPY's own engine which building it stands in (Character vt+0x1D8). On the copy's game that record is a lease (setter
 * vt+0x1C8 -> 0x792310 sets +0x69C = 4; counted down by 0x5C7BF0) and for copies it went stale: T776 counted B's copy "in the
 * bar" for 260+ s after it walked out. Under the authority rule the OWNER's engine knows where its own character is, so the
 * owner says it. MSG_INSIDE (66, reliable, the character stream's road) - sent on change and every kInsideRefreshSec:
 *
 *   uid u32 | flags u8 (bit0 = inside a building) | keyLen u8 (0..kInsideKeyMax) | key bytes (the building's P7n position key)
 *
 * The receiver keeps a building's inside loaded only while the owner's latest word is "inside, this key" and is at most
 * kInsideStaleSec old. An older peer sends nothing -> never heard -> no keep (the safe side).
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <string>
#include <vector>

namespace p25inside {

const size_t kInsideKeyMax = 64;          /* bytes; the P7n key is at most 47 (items.cpp kBoxKeyCap 48) */
const double kInsideRefreshSec = 5.0;     /* the owner re-says an unchanged word after this long (as MSG_INTENT) */
const double kInsideStaleSec = 15.0;      /* the receiver drops a word older than this (three refreshes missed) */

const int kInsideDecodeOk       = 0;
const int kInsideDecodeTooShort = 1;
const int kInsideDecodeTooLong  = 2;      /* keyLen above kInsideKeyMax */

/* false (nothing appended) for a key longer than kInsideKeyMax, or a null key with a length. */
inline bool EncodeInside(std::vector<char>* b, unsigned int uid, int inside, const char* key, size_t keyLen)
{
    if (b == 0 || keyLen > kInsideKeyMax || (keyLen != 0 && key == 0)) return false;
    const size_t at = b->size();
    b->resize(at + 6 + keyLen);
    char* p = &(*b)[at];
    std::memcpy(p, &uid, 4);
    p[4] = (char)(unsigned char)(inside ? 1 : 0);
    p[5] = (char)(unsigned char)keyLen;
    if (keyLen != 0) std::memcpy(p + 6, key, keyLen);
    return true;
}

/* Bits of flags other than bit0 are ignored (room for later). */
inline int DecodeInside(const char* p, size_t size, unsigned int* uid, int* inside, std::string* key)
{
    if (p == 0 || uid == 0 || inside == 0 || key == 0 || size < 6) return kInsideDecodeTooShort;
    const size_t len = (size_t)(unsigned char)p[5];
    if (len > kInsideKeyMax) return kInsideDecodeTooLong;
    if (size < 6 + len) return kInsideDecodeTooShort;
    std::memcpy(uid, p, 4);
    *inside = (((unsigned char)p[4]) & 1u) ? 1 : 0;
    key->assign(p + 6, len);
    return kInsideDecodeOk;
}

/* THE RECEIVER'S READING of the owner's latest word. heard: -1 never heard, 0 said outside, 1 said inside; keyLen the key it
   gave; heardAt / now wall-clock seconds. Only kSaidIn keeps a building's inside; every other answer releases it. A word older
   than kInsideStaleSec (or an unreadable clock) is stale. kSaidKeyMiss = the owner said inside but its game could not build the
   building's key. */
enum { kSaidNone = 0, kSaidIn = 1, kSaidOut = 2, kSaidStale = 3, kSaidKeyMiss = 4 };
inline int InsideVerdict(int heard, size_t keyLen, double heardAt, double now)
{
    if (heard < 0) return kSaidNone;
    if (!(now - heardAt <= kInsideStaleSec)) return kSaidStale;
    if (heard == 0) return kSaidOut;
    return keyLen != 0 ? (int)kSaidIn : (int)kSaidKeyMiss;
}

/* THE OWNER'S SEND RULE: the first word, any change of in/out or of the key, else a refresh after more than kInsideRefreshSec. */
inline bool InsideSendDue(bool everSent, int lastInside, const char* lastKey, int inside, const char* key, double lastAt, double now)
{
    if (!everSent) return true;
    if ((lastInside != 0) != (inside != 0)) return true;
    if (std::strcmp(lastKey != 0 ? lastKey : "", key != 0 ? key : "") != 0) return true;
    return !(now - lastAt <= kInsideRefreshSec);
}

} // namespace p25inside
