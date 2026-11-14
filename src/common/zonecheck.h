/* zonecheck.h - T-1243 (owner 1518 and 1520 d / e, 2026-11-05): AN AREA RECORD THAT CANNOT BE READ TO ITS END IS NEVER KEPT, SENT
   OR SERVED.

   An area record (zone.<x>.<y>) is the area file Kenshi saved. Run T2079: a world server stored one a game sent with 1,180 zero bytes
   inside a building record, and every game entering that area froze in Kenshi's own load; the earlier copy (.prev) loaded. The check
   is the duplicate report's reader, unchanged (dupsquad::ParseAll): it walks the file's layout to its exact last byte, and over 9,381
   area files in evidence it refused only the damaged one.
     at receipt (world server, OnRecordFrom): an unreadable area record is refused - nothing stored, rotated, numbered or forwarded;
     at start (world server, IndexRecordFromMeta): a stored area record that cannot be read is served from its earlier copy (.prev)
       when that copy validates and reads to its end; the unreadable file is kept aside, never deleted;
     before a send (game, ZoneSaveApply): the area file read back is not sent when it cannot be read.
   And the TEST-ONLY lever `zonedamage <zone id> before|after <offset> <length>`: the next send of that area record by this game has
   <length> bytes from <offset> set to zero - before the game's own check (which must refuse it) or after it (the world server's
   must). One use per arming.
   T-1243 (owner 1523, 2026-11-05): a third stage, `mirror` - at this game's next mirror write of that area record the mirror FILE
   has those bytes set to zero after the write (and after the send), and the kept record table is written again so its row has the
   damaged file's size and time: only the start's content check (store.cpp MirrorKeepLoad) can catch it. And SetAsideName: the name
   a game's unreadable kept area file is moved aside under at that check.

   PURE: no Windows calls, no file access. VS2010 C++03. */
#ifndef ZONECHECK_H
#define ZONECHECK_H

#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include "dupsquad.h"
#include "zonerec.h"

namespace zonecheck
{

/* 1 = the bytes read to their end. *stop (when given): where reading ended - n for bytes read to their end, else the first byte of
   the field that could not be read or the first byte left over; 0 for no bytes. */
inline int Readable(const unsigned char* b, size_t n, size_t* stop)
{
    if (zonerec::ZoneEmptyIsRaw(b, n) != 0) { if (stop != 0) *stop = n; return 1; }   /* T-1329: an EMPTY area record (zonerec.h) reads to its end */
    size_t at = 0;
    int ok = 0;
    if (b != 0 && n > 0)
    {
        std::vector<dupsquad::Rec> recs;
        ok = dupsquad::ParseAllAt(b, n, &recs, &at) ? 1 : 0;
    }
    if (stop != 0) *stop = ok != 0 ? n : at;
    return ok;
}
inline int ReadableBytes(const std::vector<char>& v, size_t* stop)
{
    if (v.empty()) { if (stop != 0) *stop = 0; return 0; }
    return Readable((const unsigned char*)&v[0], v.size(), stop);
}

/* An area record's id, zone.<x>.<y> (zonerec.h). Only these are checked - no other kind of record. */
inline int IsAreaId(const std::string& id) { int x = 0, y = 0; return zonerec::ZoneRecIdParse(id, &x, &y) != 0 ? 1 : 0; }

/* AT RECEIPT: an area record carrying bytes that cannot be read to its end is refused; anything else is left to its own rules. Asked
   only for an area record that carries bytes (OnRecordFrom's area branch), so readable is the whole question. */
enum { kReceiptTake = 0, kReceiptRefuseUnreadable = 1 };
inline int ReceiptJudge(int readable)
{
    return readable == 0 ? kReceiptRefuseUnreadable : kReceiptTake;
}

/* AT START, for an area record whose current pair validates (its length and checksum match its index line): which copy is served.
   prevPair: the previous pair (<id>.meta.prev with <id>.platoon.prev) validates; prevReadable: and <id>.platoon.prev reads to its end.
   Only kStartPrev changes anything; the other three serve the current copy, as before. */
enum { kStartCurrent = 0, kStartPrev = 1, kStartCurrentNoPrev = 2, kStartCurrentPrevUnreadable = 3 };
inline int StartChoose(int currentReadable, int prevPair, int prevReadable)
{
    if (currentReadable != 0) return kStartCurrent;
    if (prevPair == 0) return kStartCurrentNoPrev;
    if (prevReadable == 0) return kStartCurrentPrevUnreadable;
    return kStartPrev;
}

/* ===== THE TEST-ONLY LEVER: zonedamage <zone id> before|after|mirror <offset> <length> ===== */
enum { kDamageNone = 0, kDamageBefore = 1, kDamageAfter = 2, kDamageMirror = 3 };   /* kDamageMirror: T-1243 (owner 1523) */
const long long kDamageMaxOffset = 1LL << 30;
const long long kDamageMaxLength = 1LL << 24;
struct DamageArm
{
    int when;            /* kDamageNone = not armed */
    std::string id;
    long long offset, length;
    DamageArm() : when(kDamageNone), offset(0), length(0) {}
};
inline const char* WhenWord(int when) { return when == kDamageBefore ? "before" : when == kDamageAfter ? "after" : when == kDamageMirror ? "mirror" : "none"; }
/* digits only (no sign, no letters), at most 12 */
inline int WholeNumber(const std::string& s, long long* v)
{
    if (s.empty() || s.size() > 12) return 0;
    long long n = 0;
    for (size_t k = 0; k < s.size(); ++k)
    {
        if (s[k] < '0' || s[k] > '9') return 0;
        n = n * 10 + (long long)(s[k] - '0');
    }
    *v = n;
    return 1;
}
/* 1 = parsed into *out: an area record's id (at most 64 characters), before, after or mirror, an offset 0..2^30 and a length 1..2^24, and
   nothing after them. */
inline int ParseDamage(const std::string& args, DamageArm* out)
{
    std::istringstream is(args);
    std::string id, when, offs, lens, extra;
    if (!(is >> id >> when >> offs >> lens)) return 0;
    if (is >> extra) return 0;
    if (id.size() > 64 || IsAreaId(id) == 0) return 0;
    const int w = when == "before" ? kDamageBefore : when == "after" ? kDamageAfter : when == "mirror" ? kDamageMirror : kDamageNone;
    if (w == kDamageNone) return 0;
    long long o = 0, l = 0;
    if (!WholeNumber(offs, &o) || !WholeNumber(lens, &l)) return 0;
    if (o > kDamageMaxOffset || l < 1 || l > kDamageMaxLength) return 0;
    out->when = w; out->id = id; out->offset = o; out->length = l;
    return 1;
}
inline std::string UsageReply()
{
    return "error zonedamage usage: zonedamage <zone.<x>.<y>> before|after|mirror <offset 0..1073741824> <length 1..16777216>";
}
inline std::string ArmReply(const DamageArm& a)
{
    char b[96];
    std::sprintf(b, " offset=%lld length=%lld", a.offset, a.length);
    return "ok zonedamage armed " + a.id + " " + WhenWord(a.when) + b;
}
/* The arming applies to this send: it is armed, for this stage, for this record. */
inline int DamageTakes(const DamageArm& a, int when, const std::string& id)
{
    return (a.when != kDamageNone && a.when == when && a.id == id) ? 1 : 0;
}
/* Sets bytes [offset, offset + length) to zero, cut at the end of the bytes; returns how many were set. */
inline long long DamageApply(std::vector<char>* bytes, long long offset, long long length)
{
    if (bytes == 0 || offset < 0 || length <= 0 || (unsigned long long)offset >= (unsigned long long)bytes->size()) return 0;
    const size_t from = (size_t)offset;
    const size_t room = bytes->size() - from;
    const size_t k = ((unsigned long long)length < (unsigned long long)room) ? (size_t)length : room;
    for (size_t i = 0; i < k; ++i) (*bytes)[from + i] = 0;
    return (long long)k;
}

/* T-1243 (owner 1523, 2026-11-05): the name a game's kept area file that cannot be read to its end is moved aside under -
   <file>.unreadable, or <file>.unreadable-<unix seconds> when that name is taken already (plainTaken). The move is made without
   replacing, so a taken second name fails the move rather than writing over a file; nothing is ever deleted. */
inline std::string SetAsideName(const std::string& file, int plainTaken, long long unixSeconds)
{
    if (plainTaken == 0) return file + ".unreadable";
    char b[32];
    std::sprintf(b, "%lld", unixSeconds);
    return file + ".unreadable-" + b;
}

}   /* namespace zonecheck */

#endif
