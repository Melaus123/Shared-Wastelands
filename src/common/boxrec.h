#pragma once
/* src/common/boxrec.h - ONE BOX'S CONTENTS AS ONE WORLD-SERVER RECORD (a storage box, a shop's stock or a cage's prisoner).
 *
 * One record per box, id `box.<sx>.<sy>.#<hash>.<x10>.<z10>`: the box's position key (poskey.h, `<sid>@<sx>,<sy>@<x10>,<z10>,<y10>`)
 * WITHOUT its height part, because the game matches boxes on sector, record, x and z only (items.cpp BoxKeyNearKey), and with the
 * record name ALWAYS in its hashed form ('#' + 8 lower-case hex digits of poskey::SidHash32; a key that already carries the hashed
 * form keeps it). Always hashing gives one box one id whatever its key's length (the wire hashes a name only when the key WITH its
 * height is long, so choosing by length would give one box two ids), and makes every id its own file name (no two ids differ only
 * in letter case or punctuation). Numbers are plain decimal with no leading zero; x10 and z10 may be negative. The record's bytes
 * are its payload (the `.platoon` file on the world server, message 28 RECORD on the wire), laid out as:
 *
 *   HEADER (little-endian):
 *     u32 marker 'BOX1' | u32 format (kBrFormat) | u32 generation | i32 writer slot | u8 kind (kBrKindBox / Shop / Cage) |
 *     i32 owner slot (-1 = nobody's; else the player whose game writes the box while it is loaded there, boxowner.h) |
 *     u32 length + the full position key (1..kBrKeyMax bytes, no control byte, height included as the writer had it) |
 *     u32 flags (kBrFlagSeed: built from the writer's own box, not from a stored record) | u32 body length
 *   BODY (body length bytes): one PARITY_BOX answer (paritywire.h EncodeParityBox) with status CONTENTS and the header's key -
 *     the item rows with their stolen marks, uniforms, crafters and backpacks' contents. At most kBrMaxRows items.
 *   Nothing may follow the body. The whole record is at most kBrMaxBytes.
 *
 * A record that is cut short, carries an unknown marker, format, flag, kind or slot, names a key that is not a position key, holds
 * a body that is not one whole CONTENTS answer for that key or whose digest is not its items' own, has more items than the cap,
 * or has bytes after its body DECODES TO REFUSED: the caller gets no header and no item - never part of the box. The encoder writes
 * a box whole or not at all (a box missing some of its items would be the loss this record exists to end).
 *
 * THE WORLD SERVER'S RULE (BoxAcceptRule): ONE WRITER at a time, as boxowner.h. While the box's owner player's game has the box's
 * area loaded, only that owner's game may write or remove the box; otherwise only the area's holder may. A SEED is taken only when
 * no record of that id exists, judged against the owner the seed names (so a game may name itself the owner only while its game has
 * the area loaded; otherwise only the holder seeds); a shop or a cage never has an owner, and no write changes a box's kind. Any
 * later write must be an update
 * (not a seed) from the one writer of the stored record (its stored owner is the one tested), with a generation higher than the
 * stored one - an equal generation is taken only as a harmless repeat of the very same bytes, and then nothing is written; only the
 * area's holder may change a record's owner. A removal is taken only from the one writer of the stored record. A refused write
 * is answered with message kMsgBoxRefuse {str id | u32 reason | u32 stored generation | u32 sent generation}.
 *
 * Pure: no engine memory, no Windows, no globals. C++03 (VS2010 v100).
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "poskey.h"      /* the record name's hash, its hashed form and the sid room */
#include "paritywire.h"  /* the PARITY_BOX item list the record carries as its body */

namespace coopbrec {

const unsigned int kProtocol = 101;            /* the world-server protocol that carries box records and BOX_REFUSE */


const unsigned int kMsgBoxRefuse = 72u;       /* world server -> the writer: its box record was not taken, and why */

const unsigned int kBrMarker = 0x31584F42u;   /* 'B','O','X','1' little-endian */
const unsigned int kBrFormat = 1u;
const unsigned int kBrMaxRows = cooppar::kParityMaxItems;   /* 1024 items per box */
const unsigned int kBrMaxBytes = 256u << 10;  /* 256 KB per record */
const unsigned int kBrFlagSeed = 1u;
const unsigned int kBrKeyMax = (unsigned int)cooppar::kParityStrMax;   /* the body carries the key behind a u8 length */
const unsigned int kBrFixedBytes = 25u;       /* marker, format, generation, writer, kind, owner, key length */
const unsigned int kBrHeaderMaxBytes = kBrFixedBytes + kBrKeyMax + 8u;   /* the longest header: what the server reads back from a file */
const unsigned int kBrIdMax = 128u;           /* longer than any id MakeBoxRecId makes (at most 41 bytes) */
const int kBrMaxSector = 63;
const int kBrMaxSlot = 65535;
const int kBrKindBox = 0, kBrKindShop = 1, kBrKindCage = 2, kBrKindMax = 2;
const int kBrNumDigitsMax = 9;                /* a tenth of a world unit past 1e7 units is a bad read (poskey.h); 9 digits never overflow */

/* decode results: anything but kBrOk is a refused record */
const int kBrOk = 0, kBrShort = 1, kBrBadMarker = 2, kBrBadFormat = 3, kBrBadHeader = 4, kBrOverCap = 5, kBrBadBody = 6,
          kBrTrailing = 7, kBrTooBig = 8, kBrBadDigest = 9, kBrDecodeMax = 9;

/* the world server's answers */
const int kBrAccept = 0, kBrRefuseBadId = 1, kBrRefuseUnreadable = 2, kBrRefuseWrongBox = 3, kBrRefuseWriter = 4,
          kBrRefuseSeedExists = 5, kBrRefuseNoRecord = 6, kBrRefuseNotWriter = 7, kBrRefuseOlder = 8, kBrRefusePosition = 9,
          kBrRefuseOwnerChange = 10, kBrRefuseOwnerKind = 11, kBrRefuseSameGen = 12,
          kBrRepeat = 13,                  /* not a refusal: the very same bytes again at the stored generation - nothing is written */
          kBrRefuseKindChange = 14,
          kBrJudgeMax = 14;

struct BoxHeader
{
    unsigned int format, flags, generation;
    int writerSlot, kind, ownerSlot;
    std::string key;                 /* the full position key, height included */
    unsigned int bodyBytes;          /* filled by the decoder */
    BoxHeader() : format(kBrFormat), flags(0), generation(0), writerSlot(0), kind(kBrKindBox), ownerSlot(-1), bodyBytes(0) {}
};

/* the parts of a position key */
struct BoxKeyParts
{
    std::string sid;
    int sx, sy, haveY;
    long x10, z10, y10;
    BoxKeyParts() : sx(0), sy(0), haveY(0), x10(0), z10(0), y10(0) {}
};

/* ---- text and numbers ---- */
inline bool BrTextOk(const std::string& s, size_t max)
{
    if (s.empty() || s.size() > max) return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}
/* One optionally-signed decimal integer in s[*at, end), advancing *at past it. strict = only the spelling BrLongText makes (no
   leading zero, no "-0"). 1 read, 0 not a number. */
inline int BrParseLong(const std::string& s, size_t* at, size_t end, int strict, long* out)
{
    size_t i = *at;
    int neg = 0;
    if (i < end && s[i] == '-') { neg = 1; ++i; }
    const size_t first = i;
    long v = 0;
    while (i < end && s[i] >= '0' && s[i] <= '9')
    {
        if ((int)(i - first) >= kBrNumDigitsMax) return 0;
        v = v * 10 + (long)(s[i] - '0');
        ++i;
    }
    if (i == first) return 0;
    if (strict != 0 && ((s[first] == '0' && i - first > 1) || (neg != 0 && v == 0))) return 0;
    *at = i;
    *out = neg ? -v : v;
    return 1;
}
inline std::string BrLongText(long v)
{
    char b[32];
    std::sprintf(b, "%ld", v);
    return std::string(b);
}

/* ---- the position key ---- */
/* A key taken apart, reading the two '@' separators FROM THE RIGHT (a record name may hold an '@'), the height optional - the
   same reading as the game's resolver. 1 = parsed (sector 0..63, sid 1..poskey::kSidRoom-1 bytes), 0 = not a position key. */
inline int BoxKeyParse(const std::string& key, BoxKeyParts* out)
{
    const size_t at2 = key.rfind('@');
    if (at2 == std::string::npos || at2 == 0) return 0;
    const size_t at1 = key.rfind('@', at2 - 1);
    if (at1 == std::string::npos || at1 == 0) return 0;
    BoxKeyParts k;
    k.sid = key.substr(0, at1);
    if (!BrTextOk(k.sid, (size_t)poskey::kSidRoom - 1)) return 0;
    size_t p = at1 + 1;
    long a = 0, b = 0;
    if (BrParseLong(key, &p, at2, 0, &a) == 0 || p >= at2 || key[p] != ',') return 0;
    ++p;
    if (BrParseLong(key, &p, at2, 0, &b) == 0 || p != at2) return 0;
    if (a < 0 || a > kBrMaxSector || b < 0 || b > kBrMaxSector) return 0;
    k.sx = (int)a; k.sy = (int)b;
    p = at2 + 1;
    if (BrParseLong(key, &p, key.size(), 0, &k.x10) == 0 || p >= key.size() || key[p] != ',') return 0;
    ++p;
    if (BrParseLong(key, &p, key.size(), 0, &k.z10) == 0) return 0;
    if (p < key.size() && key[p] == ',') { ++p; if (BrParseLong(key, &p, key.size(), 0, &k.y10) == 0) return 0; k.haveY = 1; }
    if (p != key.size()) return 0;
    if (out) *out = k;
    return 1;
}

/* ---- the record id ---- */
/* any spelling of the prefix counts (file names ignore case, so "Box.3.4..." would land on box.3.4...'s files); only the
   lower-case spelling MakeBoxRecId makes parses, so another spelling meets the box rules and is refused by them */
inline bool IsBoxRecId(const std::string& id)
{
    static const char kPrefix[] = "box.";
    if (id.size() < 4) return false;
    for (size_t i = 0; i < 4; ++i) { char c = id[i]; if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); if (c != kPrefix[i]) return false; }
    return true;
}
inline std::string BoxRecIdOf(const std::string& sid, int sx, int sy, long x10, long z10)
{
    return "box." + BrLongText((long)sx) + "." + BrLongText((long)sy) + "." + sid + "." + BrLongText(x10) + "." + BrLongText(z10);
}
/* 1 = the id is exactly a spelling MakeBoxRecId makes (the sector, the sid field, x10 and z10 into the outputs that are not 0); 0 =
   not a box record id (outputs untouched). The sid field must be the hashed form, '#' + exactly 8 lower-case hex digits. */
inline int BoxRecIdParse(const std::string& id, int* sx, int* sy, std::string* sid, long* x10, long* z10)
{
    if (id.size() > kBrIdMax || id.compare(0, 4, "box.") != 0) return 0;
    size_t at = 4;
    long a = 0, b = 0;
    if (BrParseLong(id, &at, id.size(), 1, &a) == 0 || at >= id.size() || id[at] != '.') return 0;
    ++at;
    if (BrParseLong(id, &at, id.size(), 1, &b) == 0 || at >= id.size() || id[at] != '.') return 0;
    ++at;
    if (a < 0 || a > kBrMaxSector || b < 0 || b > kBrMaxSector) return 0;
    const size_t dz = id.rfind('.');
    if (dz == std::string::npos || dz <= at) return 0;
    const size_t dx = id.rfind('.', dz - 1);
    if (dx == std::string::npos || dx <= at) return 0;
    const std::string s = id.substr(at, dx - at);
    if (poskey::IsHashedSidField(s.c_str()) == 0) return 0;
    long x = 0, z = 0;
    size_t p = dx + 1;
    if (BrParseLong(id, &p, dz, 1, &x) == 0 || p != dz) return 0;
    p = dz + 1;
    if (BrParseLong(id, &p, id.size(), 1, &z) == 0 || p != id.size()) return 0;
    if (sx) *sx = (int)a;
    if (sy) *sy = (int)b;
    if (sid) *sid = s;
    if (x10) *x10 = x;
    if (z10) *z10 = z;
    return 1;
}

/* The record id of the box a position key names: its height left out and its record name always in the hashed form (kept as it is
   when the key already carries that form, so both texts of one key give one id). "" when the key is not a position key. */
inline std::string MakeBoxRecId(const std::string& fullKey)
{
    if (!BrTextOk(fullKey, kBrKeyMax)) return std::string();
    BoxKeyParts k;
    if (BoxKeyParse(fullKey, &k) == 0) return std::string();
    std::string field = k.sid;
    if (poskey::IsHashedSidField(field.c_str()) == 0)
    {
        char hashed[16];
        int at = poskey::AppendStr(hashed, (int)sizeof hashed, 0, "#");
        at = poskey::AppendHex8(hashed, (int)sizeof hashed, at, poskey::SidHash32(field.c_str()));
        if (at < 0) return std::string();
        field = hashed;
    }
    const std::string id = BoxRecIdOf(field, k.sx, k.sy, k.x10, k.z10);
    return BoxRecIdParse(id, 0, 0, 0, 0, 0) != 0 ? id : std::string();
}
/* ---- validity ---- */
inline bool BrSlotOk(int slot) { return slot >= 0 && slot <= kBrMaxSlot; }
inline bool BoxHeaderValid(const BoxHeader& h)
{
    return h.format == kBrFormat && (h.flags & ~kBrFlagSeed) == 0 && BrSlotOk(h.writerSlot) && (h.ownerSlot == -1 || BrSlotOk(h.ownerSlot))
        && h.kind >= 0 && h.kind <= kBrKindMax && !MakeBoxRecId(h.key).empty();
}
/* the items' own fields: a real quantity and finite numbers (the PARITY_BOX decoder already refuses an empty base sid) */
inline bool BoxItemsValid(const std::vector<cooppar::ParityItem>& items)
{
    if (items.size() > kBrMaxRows) return false;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const cooppar::ParityItem& it = items[i];
        if (it.baseSid.empty() || it.quantity < 1) return false;
        if (!coopbag::BagFloatOk(it.quality) || !coopbag::BagFloatOk(it.charges)) return false;
    }
    return true;
}

/* ---- bytes ---- */
inline void BrPutU32(std::vector<char>* b, unsigned int v) { const size_t at = b->size(); b->resize(at + 4); std::memcpy(&(*b)[at], &v, 4); }
inline void BrPutStr(std::vector<char>* b, const std::string& s) { BrPutU32(b, (unsigned int)s.size()); b->insert(b->end(), s.begin(), s.end()); }
/* Every test is `size - off < n` with off already <= size. */
inline bool BrGet4(const char* p, size_t size, size_t* off, void* v)
{
    if (*off > size || size - *off < 4) return false;
    std::memcpy(v, p + *off, 4);
    *off += 4;
    return true;
}
inline bool BrGetStr(const char* p, size_t size, size_t* off, unsigned int cap, std::string* s)
{
    unsigned int len = 0;
    if (!BrGet4(p, size, off, &len)) return false;
    if (len > cap || size - *off < (size_t)len) return false;
    s->assign(p + *off, len);
    *off += len;
    return true;
}

/* The record for header `h` (its flags, generation, slots, kind and key) and `items`, into *out (replaced). false = nothing written
   (*out emptied): the header is out of range, an item is not valid, there are more than kBrMaxRows items, the item list will not
   encode as a PARITY_BOX answer, or the record would pass kBrMaxBytes. */
inline bool EncodeBoxRecord(std::vector<char>* out, const BoxHeader& h, const std::vector<cooppar::ParityItem>& items)
{
    if (out == 0) return false;
    out->clear();
    BoxHeader hh = h;
    hh.format = kBrFormat;
    if (!BoxHeaderValid(hh) || !BoxItemsValid(items)) return false;
    cooppar::ParityBox m;
    m.status = cooppar::kParityStatusContents;
    m.key = hh.key;
    m.digest = cooppar::ParityDigestOf(items);
    m.items = items;
    std::vector<char> body;
    if (!cooppar::EncodeParityBox(&body, m)) return false;
    std::vector<char> b;
    BrPutU32(&b, kBrMarker);
    BrPutU32(&b, hh.format);
    BrPutU32(&b, hh.generation);
    BrPutU32(&b, (unsigned int)hh.writerSlot);
    b.push_back((char)(unsigned char)hh.kind);
    BrPutU32(&b, (unsigned int)hh.ownerSlot);
    BrPutStr(&b, hh.key);
    BrPutU32(&b, hh.flags);
    BrPutU32(&b, (unsigned int)body.size());
    if (b.size() + body.size() > (size_t)kBrMaxBytes) return false;
    b.insert(b.end(), body.begin(), body.end());
    out->swap(b);
    return true;
}

/* The header alone (through the body length; the body is not needed) - what the world server reads back from a stored record's
   file, at most kBrHeaderMaxBytes bytes. */
inline int DecodeBoxHeader(const char* p, size_t size, BoxHeader* out)
{
    if (p == 0) return kBrShort;
    size_t off = 0;
    unsigned int marker = 0, kind = 0;
    BoxHeader h;
    if (!BrGet4(p, size, &off, &marker)) return kBrShort;
    if (marker != kBrMarker) return kBrBadMarker;
    if (!BrGet4(p, size, &off, &h.format)) return kBrShort;
    if (h.format != kBrFormat) return kBrBadFormat;
    if (!BrGet4(p, size, &off, &h.generation) || !BrGet4(p, size, &off, &h.writerSlot)) return kBrShort;
    if (off >= size) return kBrShort;
    kind = (unsigned char)p[off];
    off += 1;
    h.kind = (int)kind;
    if (!BrGet4(p, size, &off, &h.ownerSlot)) return kBrShort;
    unsigned int len = 0;
    {
        size_t o = off;
        if (!BrGet4(p, size, &o, &len)) return kBrShort;
        if (len == 0 || len > kBrKeyMax) return kBrBadHeader;
    }
    if (!BrGetStr(p, size, &off, kBrKeyMax, &h.key)) return kBrShort;
    if (!BrGet4(p, size, &off, &h.flags) || !BrGet4(p, size, &off, &h.bodyBytes)) return kBrShort;
    if (!BoxHeaderValid(h)) return kBrBadHeader;
    if (h.bodyBytes > kBrMaxBytes) return kBrTooBig;
    if (out) *out = h;
    return kBrOk;
}
/* the length of a decoded header (the body starts there) */
inline size_t BoxHeaderBytes(const BoxHeader& h) { return (size_t)kBrFixedBytes + h.key.size() + 8u; }

/* The whole record. kBrOk: *hOut and *itemsOut hold it; anything else: refused, and neither output is touched. */
inline int DecodeBoxRecord(const char* p, size_t size, BoxHeader* hOut, std::vector<cooppar::ParityItem>* itemsOut)
{
    if (p == 0) return kBrShort;
    if (size > (size_t)kBrMaxBytes) return kBrTooBig;
    BoxHeader h;
    const int hr = DecodeBoxHeader(p, size, &h);
    if (hr != kBrOk) return hr;
    const size_t off = BoxHeaderBytes(h);
    if (size - off < (size_t)h.bodyBytes) return kBrShort;
    if (size - off > (size_t)h.bodyBytes) return kBrTrailing;
    if (h.bodyBytes == 0) return kBrBadBody;
    cooppar::ParityBox m;
    const int br = cooppar::DecodeParityBox(p + off, (size_t)h.bodyBytes, &m);
    if (br == cooppar::kParityDecodeTooMany && p[off] == (char)cooppar::kParityStatusContents) return kBrOverCap;
    if (br != cooppar::kParityDecodeOk) return kBrBadBody;
    if (m.status != cooppar::kParityStatusContents || m.key != h.key || !BoxItemsValid(m.items)) return kBrBadBody;
    if (m.digest != cooppar::ParityDigestOf(m.items)) return kBrBadDigest;   /* the body's digest must be its own items' */
    if (hOut) *hOut = h;
    if (itemsOut) itemsOut->swap(m.items);
    return kBrOk;
}
inline const char* BoxDecodeWhy(int why)
{
    return why == kBrOk ? "ok" : why == kBrShort ? "cut short" : why == kBrBadMarker ? "not a box record (wrong marker)"
         : why == kBrBadFormat ? "an unknown format" : why == kBrBadHeader ? "a header out of range" : why == kBrOverCap ? "more items than the cap"
         : why == kBrBadBody ? "a body that is not one whole item list for its key" : why == kBrTrailing ? "bytes after the body"
         : why == kBrTooBig ? "larger than the byte cap" : why == kBrBadDigest ? "a body whose digest is not its items' own" : "malformed";
}

/* ---- the world server's rule ---- */
/* 1 = slot is one of slots (a slot below 0 never is) */
inline int BrSlotIn(const std::vector<int>& slots, int slot)
{
    if (slot < 0) return 0;
    for (size_t i = 0; i < slots.size(); ++i) if (slots[i] == slot) return 1;
    return 0;
}
/* May this game write (or remove) the box? ONE WRITER, as boxowner.h: while the box's owner player's game has the box's area loaded
   only that owner may; otherwise only the area's holder. senderSlot: the writing game's slot; holderSlot: the slot the area map
   names as the box's area's holder (-1 = none); ownerSlot: the box's owner player's slot (-1 = nobody's); ownerLoaded: 1 when the
   area map shows the OWNER's game with the box's area loaded now. */
inline int BoxMayWrite(int senderSlot, int holderSlot, int ownerSlot, int ownerLoaded)
{
    if (senderSlot < 0) return 0;
    if (ownerSlot >= 0 && ownerLoaded != 0) return senderSlot == ownerSlot ? 1 : 0;
    return (holderSlot >= 0 && senderSlot == holderSlot) ? 1 : 0;
}
/* exists: a record of this id is stored; storedGen / storedOwner / storedKind: its generation, owner slot and kind (storedKind -1 =
   not known: its header could not be read, so any kind may replace it); storedOwnerLoaded: 1 when the stored
   owner's game has the area loaded; sameBytes: 1 when the write's bytes are exactly the stored record's; seed: the write is a seed
   (kBrFlagSeed); gen / sentOwner / kind: the write's generation, owner slot and kind; sentOwnerLoaded: 1 when the owner the write
   names has the area loaded. A seed is judged against the owner it names, an update against the stored owner; a shop or a cage
   never has an owner and no update changes the kind; an update must be newer, except the very same bytes again (kBrRepeat:
   nothing to write); only the holder may change the owner. */
inline int BoxAcceptRule(int exists, unsigned int storedGen, int storedOwner, int storedOwnerLoaded, int sameBytes, int seed, unsigned int gen,
                         int sentOwner, int sentOwnerLoaded, int kind, int senderSlot, int holderSlot, int storedKind)
{
    if (exists == 0)
    {
        if (seed == 0) return kBrRefuseNoRecord;
        if (sentOwner >= 0 && kind != kBrKindBox) return kBrRefuseOwnerKind;
        return BoxMayWrite(senderSlot, holderSlot, sentOwner, sentOwnerLoaded) != 0 ? kBrAccept : kBrRefuseNotWriter;
    }
    if (seed != 0) return kBrRefuseSeedExists;
    if (BoxMayWrite(senderSlot, holderSlot, storedOwner, storedOwnerLoaded) == 0) return kBrRefuseNotWriter;
    if (gen < storedGen) return kBrRefuseOlder;
    if (gen == storedGen) return sameBytes != 0 ? kBrRepeat : kBrRefuseSameGen;
    if (storedKind >= 0 && kind != storedKind) return kBrRefuseKindChange;
    if (sentOwner >= 0 && kind != kBrKindBox) return kBrRefuseOwnerKind;
    if (sentOwner != storedOwner && !(holderSlot >= 0 && senderSlot == holderSlot)) return kBrRefuseOwnerChange;
    return kBrAccept;
}
/* A whole write judged: the id must parse, the bytes decode, the header's key name this id's box and its writer slot be the
   sender's, then BoxAcceptRule. loadedSlots: the slots whose games the area map shows with the box's area loaded now. storedOwner /
   storedKind: the STORED record's owner slot (-1 = nobody's, or no record) and kind (-1 = not known, or no record). sameBytes: 1 when the bytes are exactly the stored record's (the
   server tests it when the generations are equal). *hOut (may be 0): the decoded header (left default when the bytes do not
   decode). kBrAccept: store it; kBrRepeat: taken, nothing to store; anything else: refused. */
inline int BoxJudgeWrite(const std::string& id, const char* p, size_t size, int senderSlot, int holderSlot, const std::vector<int>& loadedSlots,
                         int exists, unsigned int storedGen, int storedOwner, int storedKind, int sameBytes, BoxHeader* hOut)
{
    if (BoxRecIdParse(id, 0, 0, 0, 0, 0) == 0) return kBrRefuseBadId;
    BoxHeader h;
    if (DecodeBoxRecord(p, size, &h, 0) != kBrOk) return kBrRefuseUnreadable;
    if (hOut) *hOut = h;
    if (MakeBoxRecId(h.key) != id) return kBrRefuseWrongBox;
    if (senderSlot < 0 || h.writerSlot != senderSlot) return kBrRefuseWriter;
    return BoxAcceptRule(exists, storedGen, storedOwner, BrSlotIn(loadedSlots, storedOwner), sameBytes, (h.flags & kBrFlagSeed) != 0 ? 1 : 0,
                         h.generation, h.ownerSlot, BrSlotIn(loadedSlots, h.ownerSlot), h.kind, senderSlot, holderSlot, storedKind);
}
/* A removal judged: the id must parse and the sender be the one writer of the stored record (the same test as a write). */
inline int BoxJudgeRemove(const std::string& id, int senderSlot, int holderSlot, int storedOwner, const std::vector<int>& loadedSlots)
{
    if (BoxRecIdParse(id, 0, 0, 0, 0, 0) == 0) return kBrRefuseBadId;
    return BoxMayWrite(senderSlot, holderSlot, storedOwner, BrSlotIn(loadedSlots, storedOwner)) != 0 ? kBrAccept : kBrRefuseNotWriter;
}
inline const char* BoxRefuseWhy(int why)
{
    return why == kBrAccept ? "taken"
         : why == kBrRefuseBadId ? "the id is not box.<sx>.<sy>.<sid>.<x10>.<z10>"
         : why == kBrRefuseUnreadable ? "the record's bytes are not a whole box record"
         : why == kBrRefuseWrongBox ? "the record's key names another box than its id"
         : why == kBrRefuseWriter ? "the record's writer slot is not the sending game's"
         : why == kBrRefuseSeedExists ? "a seed, and the box already has a record (the first writer won)"
         : why == kBrRefuseNoRecord ? "an update, and the box has no record to update (send a seed)"
         : why == kBrRefuseNotWriter ? "the sending game is not the box's one writer (its owner while the owner's game has the area loaded, else the area's holder)"
         : why == kBrRefuseOlder ? "its generation is lower than the stored record's"
         : why == kBrRefusePosition ? "a position-only update - a box record is always written whole"
         : why == kBrRefuseOwnerChange ? "it changes the box's owner, and only the area's holder may"
         : why == kBrRefuseOwnerKind ? "it gives a shop or a cage an owner, and only a storage box has one"
         : why == kBrRefuseSameGen ? "its generation equals the stored record's and its bytes differ"
         : why == kBrRepeat ? "the very same bytes again (taken, nothing written)"
         : why == kBrRefuseKindChange ? "it changes the box's kind (storage box, shop or cage)"
         : "refused";
}

/* ---- BOX_REFUSE (kMsgBoxRefuse): {str id | u32 reason | u32 stored generation | u32 sent generation} ---- */
inline std::vector<char> EncodeBoxRefuse(const std::string& id, int reason, unsigned int storedGen, unsigned int sentGen)
{
    std::vector<char> b;
    BrPutStr(&b, id.size() > kBrIdMax ? id.substr(0, kBrIdMax) : id);
    BrPutU32(&b, (unsigned int)reason);
    BrPutU32(&b, storedGen);
    BrPutU32(&b, sentGen);
    return b;
}
/* 1 = read whole (nothing after it); 0 = refused, outputs untouched */
inline int DecodeBoxRefuse(const std::vector<char>& b, std::string* id, int* reason, unsigned int* storedGen, unsigned int* sentGen)
{
    if (b.empty()) return 0;
    const char* p = &b[0];
    size_t off = 0;
    std::string s;
    unsigned int r = 0, sg = 0, ng = 0;
    if (!BrGetStr(p, b.size(), &off, kBrIdMax, &s)) return 0;
    if (!BrGet4(p, b.size(), &off, &r) || !BrGet4(p, b.size(), &off, &sg) || !BrGet4(p, b.size(), &off, &ng) || off != b.size()) return 0;
    if (id) *id = s;
    if (reason) *reason = (int)r;
    if (storedGen) *storedGen = sg;
    if (sentGen) *sentGen = ng;
    return 1;
}

}   /* namespace coopbrec */
