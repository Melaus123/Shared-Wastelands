/* THE POSITION KEY of a building or furniture piece - the name both games and the world server give one piece:

       <base record sid>@<sector x>,<sector y>@<x*10>,<z*10>,<y*10>            (tenths of a world unit)

   The game builds it from a live building (src/coop-plugin/items.cpp ItBoxKeyBuildMode, which says why each part is what it is);
   the world server builds it from an area record's stored position (zonekeep.h). Both call the functions here, so the two keys are
   the same bytes by construction. Pure: no engine, no Windows call, no CRT locale (the numbers are laid out by hand, never by
   printf), safe on any thread. Header-only. No C++11. */
#ifndef COOP_POSKEY_H
#define COOP_POSKEY_H

#include <math.h>
#include <string.h>

namespace poskey {

const float kSectorSize   = 4608.0f;     /* the world is a 64 x 64 grid of 4608-unit sectors */
const float kSectorOrigin = 147456.0f;   /* 4608 * 32 */
const int kKeyCap = 48;                  /* the key's room on the wire, its closing zero included */
const int kSidRoom = 96;                 /* the room for a base record sid: a longer one gives no key at all */

/* World position -> sector: the engine's own formula (ZoneManager::sectorRecord 0x9B1EC0), clamped into the grid. */
inline void SectorOf(float x, float z, int* sx, int* sy)
{
    int a = (int)floorf((kSectorOrigin + x) / kSectorSize);
    int b = (int)floorf((kSectorOrigin + z) / kSectorSize);
    if (a < 0) a = 0; if (a > 63) a = 63; if (b < 0) b = 0; if (b > 63) b = 63;
    *sx = a; *sy = b;
}
/* Tenths of a world unit, rounded half away from zero. *okOut 0 means the float was not a plausible world coordinate - the sector
   constants put one inside +-147456, so +-1e7 is a bad-read test, not a rounding decision (a NaN fails both comparisons too). */
inline long TenthsOfAUnit(float v, int* okOut)
{
    if (!(v > -1.0e7f && v < 1.0e7f)) { *okOut = 0; return 0; }
    const double t = (double)v * 10.0;
    *okOut = 1;
    return (long)(t >= 0.0 ? (t + 0.5) : (t - 0.5));
}
/* FNV-1a, 32 bits, over the sid's own bytes. Used only when the whole key would not fit kKeyCap: the sid is then replaced by
   "#" + 8 hex digits. Every side runs this on the same record name and gets the same digits. It is not a security hash. */
inline unsigned int SidHash32(const char* s)
{
    unsigned int h = 2166136261u;
    for (; *s != 0; ++s) { h ^= (unsigned int)(unsigned char)*s; h *= 16777619u; }
    return h;
}
/* The three appenders. Each returns the new length, or -1 when the text would not fit - and -1 is carried forward by every later
   call, so one overflow test at the end covers a whole compose. */
inline int AppendStr(char* out, int cap, int at, const char* s)
{
    if (at < 0) return -1;
    while (*s != 0) { if (at >= cap - 1) return -1; out[at++] = *s++; }
    out[at] = 0;
    return at;
}
inline int AppendLong(char* out, int cap, int at, long v)
{
    if (at < 0) return -1;
    char tmp[24];
    int n = 0;
    unsigned long u = (v < 0) ? (unsigned long)(0 - (unsigned long)v) : (unsigned long)v;
    if (v < 0) { at = AppendStr(out, cap, at, "-"); if (at < 0) return -1; }
    do { tmp[n++] = (char)('0' + (int)(u % 10u)); u /= 10u; } while (u != 0 && n < 20);
    while (n > 0) { if (at >= cap - 1) return -1; out[at++] = tmp[--n]; }
    out[at] = 0;
    return at;
}
inline int AppendHex8(char* out, int cap, int at, unsigned int v)
{
    if (at < 0) return -1;
    const char* const hex = "0123456789abcdef";
    for (int i = 7; i >= 0; --i) { if (at >= cap - 1) return -1; out[at++] = hex[(v >> (i * 4)) & 0xFu]; }
    out[at] = 0;
    return at;
}
/* Lay the pieces out. 1 = it fitted, 0 = it did not (out then holds a partial string the caller must not use). */
inline int Compose(char* out, int cap, const char* sidField, int sx, int sy, long x10, long z10, long y10)
{
    int at = 0;
    out[0] = 0;
    at = AppendStr(out, cap, at, sidField);
    at = AppendStr(out, cap, at, "@");
    at = AppendLong(out, cap, at, (long)sx);
    at = AppendStr(out, cap, at, ",");
    at = AppendLong(out, cap, at, (long)sy);
    at = AppendStr(out, cap, at, "@");
    at = AppendLong(out, cap, at, x10);
    at = AppendStr(out, cap, at, ",");
    at = AppendLong(out, cap, at, z10);
    at = AppendStr(out, cap, at, ",");
    at = AppendLong(out, cap, at, y10);
    return (at < 0) ? 0 : 1;
}
/* The key with the sid as it is, or - when that does not fit cap - with the sid replaced by "#" + its 8-digit hash (at most 41
   characters at the edge of the map, so the hashed form fits kKeyCap by arithmetic). sidFieldOut (room sidRoom) gets the sid
   field the key was made with. 1 = composed as it is, 2 = composed with the sid hashed, 0 = it did not fit even hashed (out then
   holds ""). */
inline int ComposeOrHash(char* out, int cap, const char* sid, int sx, int sy, long x10, long z10, long y10, char* sidFieldOut, int sidRoom)
{
    int got = 1;
    char hashed[16];
    const char* field = sid;
    if (Compose(out, cap, sid, sx, sy, x10, z10, y10) == 0)
    {
        int hat = 0;
        hat = AppendStr(hashed, (int)sizeof hashed, hat, "#");
        hat = AppendHex8(hashed, (int)sizeof hashed, hat, SidHash32(sid));
        if (hat < 0 || Compose(out, cap, hashed, sx, sy, x10, z10, y10) == 0) { out[0] = 0; return 0; }
        field = hashed;
        got = 2;
    }
    if (sidFieldOut != 0 && sidRoom > 0) { strncpy(sidFieldOut, field, (size_t)(sidRoom - 1)); sidFieldOut[sidRoom - 1] = 0; }
    return got;
}

enum { kKeyOk = 1, kKeyHashed = 2, kKeyNoFit = 0, kKeyBadPos = -1, kKeySidTooLong = -2, kKeyNoSid = -3 };
/* THE KEY OF A STORED PIECE: base record sid and world position (x across, y up, z across), as a game builds it from the live
   building. kKeyOk / kKeyHashed = out holds it; any other answer = no key (out holds ""). */
inline int KeyOfPos(const char* sid, float x, float y, float z, char* out, int cap)
{
    out[0] = 0;
    if (sid == 0 || sid[0] == 0) return kKeyNoSid;
    if (strlen(sid) >= (size_t)kSidRoom) return kKeySidTooLong;
    int okx = 0, oky = 0, okz = 0;
    const long x10 = TenthsOfAUnit(x, &okx), y10 = TenthsOfAUnit(y, &oky), z10 = TenthsOfAUnit(z, &okz);
    if (okx == 0 || oky == 0 || okz == 0) return kKeyBadPos;
    int sx = 0, sy = 0;
    SectorOf(x, z, &sx, &sy);
    return ComposeOrHash(out, cap, sid, sx, sy, x10, z10, y10, 0, 0);
}

/* ONE PIECE, TWO KEY TEXTS. ComposeOrHash hashes the record name only when the whole key does not fit the room it is handed, so
   a caller with a 64-byte buffer (build.cpp's pp.build rows and PLACE keys, doors.cpp, farm.cpp) keeps the name verbatim in a key
   of kKeyCap..63 characters, where a caller at kKeyCap (the box road, the world server's area records) writes "#" + 8 hex digits.
   The functions below let one side read the other's text. */
/* 1 = f is a hashed sid field: "#" + exactly 8 lowercase hex digits */
inline int IsHashedSidField(const char* f)
{
    if (f == 0 || f[0] != '#') return 0;
    for (int i = 1; i <= 8; ++i)
    {
        const char c = f[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
    }
    return (f[9] == 0) ? 1 : 0;
}
/* 1 = the two sid fields name the same record: the same text, or one is the hashed field of the other's own bytes */
inline int SidFieldsSame(const char* a, const char* b)
{
    if (a == 0 || b == 0) return 0;
    if (strcmp(a, b) == 0) return 1;
    const int ha = IsHashedSidField(a), hb = IsHashedSidField(b);
    if (ha == hb) return 0;
    const char* hashed = (ha != 0) ? a : b;
    const char* plain = (ha != 0) ? b : a;
    char h[16];
    int at = AppendStr(h, (int)sizeof h, 0, "#");
    at = AppendHex8(h, (int)sizeof h, at, SidHash32(plain));
    return (at > 0 && strcmp(h, hashed) == 0) ? 1 : 0;
}
/* THE MATCH: a piece (have) answers to a key (want) when it is in the same sector, within `window` tenths on x and on z, and
   of the same record in either sid form. y is not compared (a resolver breaks ties with it). */
inline int SameSpotSameRecord(const char* haveSid, int hsx, int hsy, long hx10, long hz10,
                              const char* wantSid, int wsx, int wsy, long wx10, long wz10, long window)
{
    if (hsx != wsx || hsy != wsy) return 0;
    long dx = hx10 - wx10; if (dx < 0) dx = -dx;
    if (dx > window) return 0;
    long dz = hz10 - wz10; if (dz < 0) dz = -dz;
    if (dz > window) return 0;
    return SidFieldsSame(haveSid, wantSid);
}
/* THE WIRE FORM OF A KEY: the text a caller at kKeyCap composes for the same piece. A key shorter than kKeyCap is copied as it
   is; a longer one (composed in a bigger buffer, name verbatim) gets its sid field - everything left of the second '@' from the
   right - replaced by "#" + SidHash32, which is what ComposeOrHash gives at kKeyCap because the rest of the key is the same
   text. 1 = out holds the key unchanged, 2 = out holds the hashed form, 0 = no position key that fits out (out holds ""). */
inline int WireForm(const char* key, char* out, int cap)
{
    if (out == 0 || cap <= 0) return 0;
    out[0] = 0;
    if (key == 0 || key[0] == 0) return 0;
    const size_t n = strlen(key);
    if (n < (size_t)kKeyCap)
    {
        if (n >= (size_t)cap) return 0;
        memcpy(out, key, n + 1);
        return 1;
    }
    const char* at2 = strrchr(key, '@');
    if (at2 == 0 || at2 == key) return 0;
    const char* at1 = at2 - 1;
    while (at1 > key && *at1 != '@') --at1;
    if (*at1 != '@' || at1 == key) return 0;
    const size_t sidLen = (size_t)(at1 - key);
    if (sidLen >= (size_t)kSidRoom) return 0;
    char sid[kSidRoom];
    memcpy(sid, key, sidLen);
    sid[sidLen] = 0;
    if (IsHashedSidField(sid) != 0) return 0;   /* a key with a hashed field is never this long */
    int w = AppendStr(out, cap, 0, "#");
    w = AppendHex8(out, cap, w, SidHash32(sid));
    w = AppendStr(out, cap, w, at1);
    if (w < 0 || w >= kKeyCap) { out[0] = 0; return 0; }
    return 2;
}
/* ONE PIECE, EITHER TEXT: 1 = the two keys are the same text, or their wire forms are the same text (a 64-byte key of kKeyCap..63
   characters and the '#<hash>' key the kKeyCap road composes for the same record and spot) */
inline int KeysSamePiece(const char* a, const char* b)
{
    if (a == 0 || b == 0 || a[0] == 0 || b[0] == 0) return 0;
    if (strcmp(a, b) == 0) return 1;
    char wa[kKeyCap], wb[kKeyCap];
    if (WireForm(a, wa, kKeyCap) == 0 || WireForm(b, wb, kKeyCap) == 0) return 0;
    return strcmp(wa, wb) == 0 ? 1 : 0;
}
/* 1 = a key with two texts: kKeyCap..63 characters with the record name in plain text - a 64-byte buffer keeps it verbatim, the
   kKeyCap road hashes it */
inline int KeyHasTwoForms(const char* key)
{
    if (key == 0) return 0;
    const size_t n = strlen(key);
    if (n < (size_t)kKeyCap || n > 63) return 0;
    char w[kKeyCap];
    return WireForm(key, w, kKeyCap) == 2 ? 1 : 0;
}
/* a decimal (optional '-', 1..9 digits) at p, before end: the first byte after it, 0 = none */
inline const char* ReadKeyLong(const char* p, const char* end, long* v)
{
    int neg = 0;
    if (p < end && *p == '-') { neg = 1; ++p; }
    if (p >= end || *p < '0' || *p > '9') return 0;
    long r = 0;
    int digits = 0;
    while (p < end && *p >= '0' && *p <= '9') { if (++digits > 9) return 0; r = r * 10 + (long)(*p - '0'); ++p; }
    *v = neg != 0 ? -r : r;
    return p;
}
/* A KEY'S SPOT: "<sid field>@<sx>,<sy>@<x10>,<z10>,<y10>" read back - the sid field (everything left of the second '@' from the
   right, into sidOut of room sidRoom), the sector and the x / z tenths; ParseKeySpotH also the height's tenths into *y10 (may be 0).
   1 = read, 0 = not a position key */
inline int ParseKeySpotH(const char* key, char* sidOut, int sidRoom, int* sx, int* sy, long* x10, long* z10, long* y10)
{
    if (sidOut != 0 && sidRoom > 0) sidOut[0] = 0;
    if (key == 0 || key[0] == 0) return 0;
    const char* at2 = strrchr(key, '@');
    if (at2 == 0 || at2 == key) return 0;
    const char* at1 = at2 - 1;
    while (at1 > key && *at1 != '@') --at1;
    if (*at1 != '@' || at1 == key) return 0;
    const size_t sidLen = (size_t)(at1 - key);
    if (sidOut == 0 || sidRoom <= 0 || sidLen >= (size_t)sidRoom) return 0;
    long a = 0, b = 0, x = 0, z = 0, y = 0;
    const char* p = ReadKeyLong(at1 + 1, at2, &a);
    if (p == 0 || *p != ',') return 0;
    p = ReadKeyLong(p + 1, at2, &b);
    if (p != at2) return 0;
    const char* end = key + strlen(key);
    p = ReadKeyLong(at2 + 1, end, &x);
    if (p == 0 || *p != ',') return 0;
    p = ReadKeyLong(p + 1, end, &z);
    if (p == 0 || *p != ',') return 0;
    p = ReadKeyLong(p + 1, end, &y);
    if (p != end) return 0;
    memcpy(sidOut, key, sidLen);
    sidOut[sidLen] = 0;
    *sx = (int)a; *sy = (int)b; *x10 = x; *z10 = z;
    if (y10 != 0) *y10 = y;
    return 1;
}
inline int ParseKeySpot(const char* key, char* sidOut, int sidRoom, int* sx, int* sy, long* x10, long* z10)
{
    return ParseKeySpotH(key, sidOut, sidRoom, sx, sy, x10, z10, 0);
}
const long kTwinDrift = 1;   /* tenths of a unit on x and z: the match drift every key search allows (SameSpotSameRecord's window) */

}   /* namespace poskey */

#endif
