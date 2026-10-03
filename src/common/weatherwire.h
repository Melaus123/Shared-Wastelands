/* src/common/weatherwire.h - P7s: THE MSG_WEATHER PAYLOAD, KEYED BY THE REGION'S NAME.
 *
 * F553 (disassembly-Confirmed, build/read-wxregions.md): the region INDEX that store protocol 38-43 carried
 * was this game's first-seen order of WeatherRegion::updateBT calls, and that order is the list order of a
 * boost::unordered_map keyed by a heap GameData* - so it DIFFERS between two processes loading the same
 * data, and the follower installed the authority's sky on the wrong region (T244: A had weather in three
 * regions, B in one; the user saw the dust-storm "wind in face" pose on B that A did not show).
 *
 * Protocol 44 carries the region's FCS stringID instead - the key the engine's own weather save uses
 * (AreaBiomeGroup::save 0x8F9E90 / ::load 0x8F9FA0). The other fields keep their order:
 *
 *   nameLen u32 (1..kWxNameMax) | name bytes | seasonIndex u32 | seasonEndDay i32 | weatherIndex u32 | 16 x u32
 *
 * The plugin's sender and receiver and the notebook's OnWeather all call these two functions, and the
 * offline suite round-trips them - one layout in one place (lesson 11).
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once

#include <cstring>
#include <vector>

namespace coopwx {

const unsigned int kWxNameMax   = 96;   /* FCS stringIDs are "<number>-<mod file>" - far shorter than this */
const size_t       kWxTailBytes = 76;   /* seasonIndex + endDay + weatherIndex + 16 words */

/* Decode outcomes. Only kWxDecodeOk carries a usable message. */
const int kWxDecodeOk           = 0;
const int kWxDecodeTooShort     = 1;   /* fewer than the 4 bytes of the length */
const int kWxDecodeNameEmpty    = 2;
const int kWxDecodeNameTooLong  = 3;   /* the hostile case: a length over kWxNameMax is refused before any read */
const int kWxDecodeSizeMismatch = 4;   /* the payload is not exactly 4 + nameLen + 76 bytes (a protocol-43 payload lands here) */
const int kWxDecodeNameBadByte  = 5;   /* a control byte in the name */

struct WxWire
{
    char name[kWxNameMax + 1];   /* NUL-terminated copy */
    unsigned int nameLen;
    unsigned int seasonIdx;
    int endDay;
    unsigned int weatherIdx;
    unsigned int w[16];
};

/* A stringID byte this layer will carry. Control bytes are refused so a name cannot break a log line; high
   bytes (a mod file named in another script) are allowed. */
inline bool WxNameByteOk(unsigned char c) { return c >= 0x20 && c != 0x7F; }

inline bool WxNameOk(const char* name, unsigned int nameLen)
{
    if (name == 0 || nameLen == 0 || nameLen > kWxNameMax) return false;
    for (unsigned int i = 0; i < nameLen; ++i) if (!WxNameByteOk((unsigned char)name[i])) return false;
    return true;
}

/* Appends nothing and returns false when the name would not decode on the other side. */
inline bool EncodeWeather(std::vector<char>* out, const char* name, unsigned int nameLen,
                          unsigned int seasonIdx, int endDay, unsigned int weatherIdx, const unsigned int w[16])
{
    if (out == 0 || !WxNameOk(name, nameLen)) return false;
    const size_t at = out->size();
    out->resize(at + 4 + nameLen + kWxTailBytes);
    char* p = &(*out)[at];
    std::memcpy(p, &nameLen, 4);            p += 4;
    std::memcpy(p, name, nameLen);          p += nameLen;
    std::memcpy(p, &seasonIdx, 4);          p += 4;
    std::memcpy(p, &endDay, 4);             p += 4;
    std::memcpy(p, &weatherIdx, 4);         p += 4;
    std::memcpy(p, w, 64);
    return true;
}

inline int DecodeWeather(const char* p, size_t n, WxWire* o)
{
    if (o == 0) return kWxDecodeTooShort;
    std::memset(o, 0, sizeof(*o));
    if (p == 0 || n < 4) return kWxDecodeTooShort;
    unsigned int len = 0;
    std::memcpy(&len, p, 4);
    if (len == 0) return kWxDecodeNameEmpty;
    if (len > kWxNameMax) return kWxDecodeNameTooLong;
    if (n != (size_t)4 + len + kWxTailBytes) return kWxDecodeSizeMismatch;
    for (unsigned int i = 0; i < len; ++i) if (!WxNameByteOk((unsigned char)p[4 + i])) return kWxDecodeNameBadByte;
    std::memcpy(o->name, p + 4, len);
    o->name[len] = 0;
    o->nameLen = len;
    {
        const char* t = p + 4 + len;
        std::memcpy(&o->seasonIdx, t, 4);
        std::memcpy(&o->endDay, t + 4, 4);
        std::memcpy(&o->weatherIdx, t + 8, 4);
        std::memcpy(&o->w[0], t + 12, 64);
    }
    return kWxDecodeOk;
}

inline const char* WeatherDecodeText(int r)
{
    switch (r)
    {
    case kWxDecodeOk:           return "ok";
    case kWxDecodeTooShort:     return "too short for the name length";
    case kWxDecodeNameEmpty:    return "empty region name";
    case kWxDecodeNameTooLong:  return "region name length over the cap";
    case kWxDecodeSizeMismatch: return "size does not match 4 + name + 76 (an older, index-keyed payload lands here)";
    case kWxDecodeNameBadByte:  return "a control byte in the region name";
    default:                    return "unknown";
    }
}

}   /* namespace coopwx */
