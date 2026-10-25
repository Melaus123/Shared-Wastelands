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

const unsigned int kProtocol = 108;            /* the world-server protocol that carries box records and BOX_REFUSE */


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

/* ---- a game's intake of an arriving record ----
   A record that reaches a game (the world server's forward or its feed) is kept in that game's box table only when its id is one
   MakeBoxRecId makes, it decodes whole, its key names that very id, and its generation is not lower than the copy already kept for
   that id (an equal generation replaces the copy: the world server stores one set of bytes per generation). Anything else is
   counted by its reason and never kept - never part of a box. */
const int kBiKeep = 0, kBiBadId = 1, kBiUnreadable = 2, kBiWrongBox = 3, kBiOlder = 4, kBiMax = 4;
/* 1 = an arriving record of generation newGen replaces the kept copy (none kept, or newGen not lower than copyGen) */
inline int BoxKeepArrived(int haveCopy, unsigned int copyGen, unsigned int newGen)
{
    if (haveCopy == 0) return 1;
    return newGen >= copyGen ? 1 : 0;
}
/* kBiKeep: *hOut and *itemsOut hold the record; kBiOlder: *hOut holds its header, *itemsOut untouched; anything else: neither is
   touched. *decodeWhy: the decoder's reason when kBiUnreadable, else kBrOk. */
inline int BoxIntakeJudge(const std::string& id, const char* p, size_t size, int haveCopy, unsigned int copyGen,
                          BoxHeader* hOut, std::vector<cooppar::ParityItem>* itemsOut, int* decodeWhy)
{
    if (decodeWhy) *decodeWhy = kBrOk;
    if (BoxRecIdParse(id, 0, 0, 0, 0, 0) == 0) return kBiBadId;
    BoxHeader h;
    std::vector<cooppar::ParityItem> items;
    const int d = DecodeBoxRecord(p, size, &h, &items);
    if (d != kBrOk) { if (decodeWhy) *decodeWhy = d; return kBiUnreadable; }
    if (MakeBoxRecId(h.key) != id) return kBiWrongBox;
    if (hOut) *hOut = h;
    if (BoxKeepArrived(haveCopy, copyGen, h.generation) == 0) return kBiOlder;
    if (itemsOut) itemsOut->swap(items);
    return kBiKeep;
}
/* ---- a game's own write of a box record ----
   The record a writer sends: the box read whole, its header naming this game as the writer. No record when the box has more than
   kBrMaxRows items or would pass kBrMaxBytes (a box written in part would lose the rest). */
const int kBwOk = 0, kBwOverCap = 1, kBwBadItem = 2, kBwBadHeader = 3;
/* The record for header h (its generation and flags as they are) and items, into *out (may be 0; emptied unless kBwOk). kBwOk: the
   record; kBwOverCap: more items than kBrMaxRows, or the record would not fit (kBrMaxBytes or the item list's own limits);
   kBwBadItem: an item is not valid (no base record, a quantity below 1, a number that is not finite); kBwBadHeader: the header is out
   of range. Encoding the same box twice with generation 0 and no flags gives the same bytes: the caller's "unchanged" test. */
inline int BoxWriteCheck(const BoxHeader& h, const std::vector<cooppar::ParityItem>& items, std::vector<char>* out)
{
    if (out) out->clear();
    if (items.size() > kBrMaxRows) return kBwOverCap;
    BoxHeader hh = h;
    hh.format = kBrFormat;
    if (!BoxHeaderValid(hh)) return kBwBadHeader;
    if (!BoxItemsValid(items)) return kBwBadItem;
    std::vector<char> b;
    if (!EncodeBoxRecord(&b, hh, items)) return kBwOverCap;
    if (out) out->swap(b);
    return kBwOk;
}
/* The generation of this game's next write. haveKept / keptGen: the record of the box this game holds (received from the world
   server); haveSent / sentGen: this game's own last write of it, or the stored generation a refusal named. 1 = a seed at generation
   1 (no record known); 0 = an update one above the higher of the two; -1 = no write (the generation cannot go higher). */
inline int BoxWriteGen(int haveKept, unsigned int keptGen, int haveSent, unsigned int sentGen, unsigned int* gen)
{
    if (haveKept == 0 && haveSent == 0) { if (gen) *gen = 1u; return 1; }
    unsigned int base = 0;
    if (haveKept != 0) base = keptGen;
    if (haveSent != 0 && sentGen > base) base = sentGen;
    if (base == 0xFFFFFFFFu) return -1;
    if (gen) *gen = base + 1u;
    return 0;
}
/* 1 = nothing to write: this game's last write carried these very contents (sameContents: the same key, kind, owner, writer and
   items) and no record newer than that write has reached this game since */
inline int BoxWriteUnchanged(int haveSent, unsigned int sentGen, int haveKept, unsigned int keptGen, int sameContents)
{
    if (haveSent == 0 || sameContents == 0) return 0;
    if (haveKept != 0 && keptGen > sentGen) return 0;
    return 1;
}
/* What a BOX_REFUSE changes in this game's own write state - it applies nothing to the box. Older, the same generation with other
   bytes, or a seed where a record exists: the stored generation is known, so the next write is an update above it (kBwrRaise). No
   record to update: the next write is a seed (kBwrSeed). Not the writer, or an owner change: the games read the box's writer or owner
   differently right now - the write is tried again later, once their reads may agree (kBwrRetry; the write state is unchanged). Any
   other reason: nothing (kBwrNone). */
const int kBwrNone = 0, kBwrRaise = 1, kBwrSeed = 2, kBwrRetry = 3;
inline int BoxRefuseNext(int reason, unsigned int storedGen, int* haveSent, unsigned int* sentGen)
{
    if (reason == kBrRefuseOlder || reason == kBrRefuseSameGen || reason == kBrRefuseSeedExists)
    {
        if (*haveSent == 0 || storedGen > *sentGen) *sentGen = storedGen;
        *haveSent = 1;
        return kBwrRaise;
    }
    if (reason == kBrRefuseNoRecord) { *haveSent = 0; *sentGen = 0; return kBwrSeed; }
    if (reason == kBrRefuseNotWriter || reason == kBrRefuseOwnerChange) return kBwrRetry;
    return kBwrNone;
}

inline const char* BoxIntakeWhy(int why)
{
    switch (why)
    {
    case kBiKeep: return "kept";
    case kBiBadId: return "the id is not a box record id";
    case kBiUnreadable: return "the record does not decode";
    case kBiWrongBox: return "the record's key names another box";
    case kBiOlder: return "older than the kept copy";
    default: return "unknown";
    }
}

/* ---- the stored record put into the box at its area's load ----
   The box's ONE WRITER puts the world server's record into its box once per load of the box's area, after the engine has built the
   box from the area file (whichever copy the game loaded), so the world server's copy wins. A game that does not write the box puts
   nothing into it: its copy follows the writer's. The state of one box's apply at this load (reset when its area leaves the game's
   loaded ground, at world teardown and at a link loss): */
const int kBasNone = 0;       /* not decided yet at this load */
const int kBasWait = 1;       /* waiting: a move not published, items cannot be made yet, the cursor over the box's window, the
                                 writer not known yet, or the stored record a refusal named not here yet */
const int kBasDone = 2;       /* applied, or not needed (the box is that record or a newer one) */
const int kBasNotWriter = 3;  /* another game writes the box now: its moves are not held here (looked at again: a game that
                                 becomes the writer again applies afresh) */
const int kBasRefused = 4;    /* the record was not laid at this load (the swap twice did not give its contents, or the merged swap
                                 twice did not start): this game's box stands and is written as the next generation above that
                                 record - a newer stored record is tried afresh before that write */
const int kBasNoBox = 6;      /* the record's box is not found here (looked for again) */
const int kBasMax = 6;

/* 1 = the stored record must be put into this game's box: one is stored, and the box is not already that record or a newer one
   (haveMine 0: nothing put into or written from the box in this world - a game that loaded the world again always applies) */
inline int BoxApplyNeeded(int haveStored, unsigned int storedGen, int haveMine, unsigned int mineGen)
{
    if (haveStored == 0) return 0;
    if (haveMine == 0) return 1;
    return storedGen > mineGen ? 1 : 0;
}
/* A refusal of this game's write named a stored generation (knownGen) above this game's box: the apply waits for that record (or a
   newer one) to arrive here (keptHave / keptGen: the copy this game keeps). No clock: the box table's arrival of the record ends the
   wait; a link loss or the box's area leaving forgets the named generation. */
const int kBrWaitNo = 0, kBrWaitYes = 1;
inline int BoxStoredWait(int knownHave, unsigned int knownGen, int haveMine, unsigned int mineGen, int keptHave, unsigned int keptGen)
{
    if (knownHave == 0 || knownGen <= (haveMine != 0 ? mineGen : 0u)) return kBrWaitNo;
    return (keptHave != 0 && keptGen >= knownGen) ? kBrWaitNo : kBrWaitYes;
}
/* What the swap's answer makes of this load's apply. swapped: the swap's answer (1 the box reads as the record, 0 it does not, -1 held:
   items cannot be made and nothing was removed); tries: the swaps at this load that did not match before this one. A mismatch is
   tried once more; the second ends the apply: this game's box stands and is written as the next generation above the record (it is the
   writer, and its box is what its player sees). */
const unsigned int kBrApplyTries = 2u;
const int kBarApplied = 0, kBarHeld = 1, kBarAgain = 2, kBarRefused = 3;
inline int BoxApplyAfterSwap(int swapped, unsigned int tries)
{
    if (swapped > 0) return kBarApplied;
    if (swapped < 0) return kBarHeld;
    return (tries + 1u < kBrApplyTries) ? kBarAgain : kBarRefused;
}
/* MAY THE WRITER WRITE THE BOX NOW? A writer never writes before its box's apply of the stored record is decided at this load, and
   never writes - least of all a seed - from a box whose contents it never received or over a stored record a refusal named and it
   never saw. A box whose apply ended with its own copy standing (kBasRefused) is written: it becomes the next generation - unless a
   refusal named a newer stored record, which goes in first.
   applyState: this load's kBas*; haveKept: a stored record of the box reached this game; knownAbove: a refusal named a stored
   generation above this game's box; neverRecv: this game holds the box but never received its contents. */
const int kBwgGo = 0, kBwgWaitApply = 1, kBwgNeverRecv = 2;
inline int BoxWriteGate(int applyState, int haveKept, int knownAbove, int neverRecv)
{
    if (neverRecv != 0) return kBwgNeverRecv;
    if (haveKept == 0 && knownAbove == 0) return kBwgGo;   /* no record stored: nothing to put in first (the first write is a seed) */
    if (applyState == kBasRefused) return (knownAbove == 0) ? kBwgGo : kBwgWaitApply;   /* a newer stored record a refusal named goes in first */
    return (applyState == kBasDone) ? kBwgGo : kBwgWaitApply;
}
/* BoxWriteGate's knownAbove (T-823 fold 3): 1 = a refusal named a stored generation (knownGen) above this game's box (mineGen) that
   has not been tried here yet. A box whose apply ended with its own copy standing (kBasRefused) at the stored record refusedGen has
   tried every record up to refusedGen, so a named generation at or below it is spent: the box is written, as refusedGen + 1 or
   above (BoxWriteGen). Without this the named generation stayed above the box's own and the write waited for ever. */
inline int BoxKnownAbove(int knownHave, unsigned int knownGen, int haveMine, unsigned int mineGen, int applyState, unsigned int refusedGen)
{
    if (knownHave == 0 || knownGen <= (haveMine != 0 ? mineGen : 0u)) return 0;
    return (applyState == kBasRefused && knownGen <= refusedGen) ? 0 : 1;
}
/* 1 = a shop restock in the box's area waits: the stock's stored record is not decided yet at this load (a restock before the apply
   would be undone by it; the engine asks for the restock again on its next pass) */
inline int BoxApplyHoldsRestock(int applyState) { return (applyState == kBasNone || applyState == kBasWait) ? 1 : 0; }
/* 1 = a refusal answers this game's LATEST write of the box (sentGen is the box's generation): the box is then what it was before
   that write */
inline int BoxRefuseLatest(int haveMine, unsigned int mineGen, unsigned int sentGen) { return (haveMine != 0 && sentGen == mineGen) ? 1 : 0; }
/* THE MOVES ON A BOX WAIT FOR ITS STORED RECORD. From its area's load until the stored record is decided at that load, a box this game
   writes and that is not already that record (or a newer one) answers "not now" to every move site - the answer a box gets while this
   game's hand-back of it waits: a player's move between a character and the box is undone and refused, an engine task's take from it
   is refused (the task finds nothing and tries again later), a move inside the box stands and is laid on the baseline saved for the
   wait, and the apply lays the stored record with those moves on top. No clock: the hold ends when the apply is decided (the record
   applied, the box already that record, or the swap twice refused), or when the area leaves; through a link loss the box holds back
   and the wait goes on once the link and its records are back.
   haveKept: a stored record of the box is here; areaLoaded: its area is loaded here; applyState: this load's kBas*; needed:
   BoxApplyNeeded. A box another game writes (kBasNotWriter) is not held here: its copy follows that writer's, and when this game
   becomes its writer again a fresh apply decides it (T-823 fold 3). */
const int kBahNo = 0, kBahHold = 1;
inline int BoxApplyHold(int haveKept, int areaLoaded, int applyState, int needed)
{
    if (haveKept == 0 || areaLoaded == 0 || needed == 0) return kBahNo;
    return (applyState == kBasNone || applyState == kBasWait || applyState == kBasNoBox) ? kBahHold : kBahNo;
}
/* 1 = the moves on a box in a loaded area with no stored record here wait too: the world server's records for this world have not all
   been applied here (feedDone 0: the feed's last page not applied yet, or no link at all - a game with no world server holds back as
   a game with friends does), and one of them may be this box's. Once they have all been applied with none for the box, the hold ends
   and the box's first write (when it has changed) is a seed. */
inline int BoxApplyHoldUnknown(int feedDone) { return (feedDone == 0) ? 1 : 0; }
/* 1 = a change to a box in a loaded area was made while this game could not know the box's stored record (none here and no link).
   Counted only: the stored record still wins at the apply - a change that was not held as a move is not laid on top of it */
inline int BoxChangeUnknown(int haveKept, int linked) { return (haveKept == 0 && linked == 0) ? 1 : 0; }
/* 1 = a box whose apply ended with its own copy standing (kBasRefused, at the stored record refusedGen) must take a newer stored record
   that arrived since (keptGen above refusedGen) before it is written: the write would otherwise replace that newer record */
inline int BoxRefusedStale(int applyState, int haveKept, unsigned int keptGen, unsigned int refusedGen)
{
    return (applyState == kBasRefused && haveKept != 0 && keptGen > refusedGen) ? 1 : 0;
}
/* A write refused as not the writer's, or as an owner change, waits until this game reads the box differently from that write: 1 = try
   it again now. sentOwner: the owner slot the refused write named (-1 none); nowMine: this game's writer decision now (1 this game,
   0 another game - the write path then drops the mark, -1 the box is not here - the write path drops it, 2 the writer cannot be told
   yet - no answer, nothing changed); nowOwner: the owner slot read now. */
inline int BoxRetryChanged(int sentOwner, int nowMine, int nowOwner)
{
    if (nowMine == 2) return 0;
    if (nowMine != 1) return 1;
    return (nowOwner != sentOwner) ? 1 : 0;
}
/* T-823 fold 3: 1 = a write refused as not the writer's (or as an owner change) is tried again now: this game reads the box
   differently from that write (BoxRetryChanged), or a new area map arrived since the refusal (newMap) - the world server judges
   the writer from the area maps, and its view may lag this game's, so each new map tries the write once more. */
inline int BoxRetryDue(int sentOwner, int nowMine, int nowOwner, int newMap)
{
    if (newMap != 0) return 1;
    return BoxRetryChanged(sentOwner, nowMine, nowOwner);
}

}   /* namespace coopbrec */
