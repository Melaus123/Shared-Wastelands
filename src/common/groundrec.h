#pragma once
/* src/common/groundrec.h - AN AREA'S GROUND ITEMS AS ONE WORLD-SERVER RECORD.
 *
 * One record per area square, id `ground.<sx>.<sy>` (sectors 0..63, plain decimal, no leading zero). Its bytes are the record's
 * payload (the `.platoon` file on the world server, message 28 RECORD on the wire) and are laid out as:
 *
 *   HEADER (kGrHeaderBytes, little-endian):
 *     u32 marker 'GRD1' | u32 format (kGrFormat) | u32 flags (kGrFlagSeed: built from the writer's own ground, not from a stored
 *     record) | u32 generation | u32 writer slot | i32 sx | i32 sy | u32 rows | u32 cut (rows left out: over kGrMaxRows or
 *     kGrMaxBytes, or cut by the caller before encoding)
 *   ROWS (rows x):
 *     str name (the item's shared name, groundname.h: 1..kGroundKeyCap-1 bytes, no control byte) |
 *     str base sid | str company sid | str material sid | str colour sid   (the item fields a GROUND ADD carries; str = u32 len + bytes) |
 *     u32 quantity | f32 quality | f32 charges | i32 function | i32 level | u8 unique |
 *     f32 x | f32 y | f32 z | f32 rotation w | f32 rotation x | f32 rotation y | f32 rotation z |
 *     u8 marks (kGrHasOwner | kGrHasGear | kGrHasBag; any other bit refuses the record) |
 *     [OWN1 block, ownerwire.h - the stolen mark] [GEM1 block, gearwire.h - uniform and crafter]
 *     [u32 length + BAG1 block, bagwire.h - a backpack's contents with its OWNL / GEML / BAGN tails; never empty]
 *   Nothing may follow the last row.
 *
 * A record that is cut short, carries an unknown marker, format, flag or mark bit, names more rows than the cap, holds a row that is
 * not whole and in range, or has bytes after its last row DECODES TO REFUSED: the caller gets no header and no row - never part of
 * the list. The encoder leaves out (counts) a row it cannot write whole, and counts every row past the caps in the header's cut.
 *
 * THE WORLD SERVER'S RULE (GroundAcceptRule): a SEED is taken only when no record of that id exists and its sender holds the area;
 * any later write must come from the game that holds the area now, as an update (not a seed), with a generation not lower than the
 * stored one. A refused write is answered with message kMsgGroundRefuse {str id | u32 reason | u32 stored generation | u32 sent
 * generation}.
 *
 * Pure: no engine memory, no Windows, no globals. C++03 (VS2010 v100).
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "groundkey.h"   /* kGroundKeyCap: the room a shared name has */
#include "bagwire.h"     /* a backpack's rows (BAG1); it brings ownerwire.h (OWN1) and gearwire.h (GEM1) */

namespace coopgrec {

const unsigned int kProtocol = 100;            /* the world-server protocol that carries ground records and GROUND_REFUSE */


const unsigned int kMsgGroundRefuse = 71u;    /* world server -> the writer: its ground record was not taken, and why */

const unsigned int kGrMarker = 0x31445247u;   /* 'G','R','D','1' little-endian */
const unsigned int kGrFormat = 1u;
const unsigned int kGrMaxRows = 1024u;        /* rows per area; the rest are counted in the header's cut */
const unsigned int kGrMaxBytes = 2u << 20;    /* 2 MB per record; rows that would pass it are counted in the cut */
const unsigned int kGrHeaderBytes = 36u;
const unsigned int kGrFlagSeed = 1u;
const int kGrMaxSector = 63;
const unsigned char kGrHasOwner = 1, kGrHasGear = 2, kGrHasBag = 4;
const float kGrMaxCoord = 1.0e7f;             /* a world coordinate past this is a bad read (poskey.h's test) */

/* decode results: anything but kGrOk is a refused record */
const int kGrOk = 0, kGrShort = 1, kGrBadMarker = 2, kGrBadFormat = 3, kGrBadHeader = 4, kGrOverCap = 5, kGrBadRow = 6,
          kGrTrailing = 7, kGrTooBig = 8;

/* the world server's answers */
const int kGrAccept = 0, kGrRefuseBadId = 1, kGrRefuseUnreadable = 2, kGrRefuseWrongArea = 3, kGrRefuseWriter = 4,
          kGrRefuseSeedExists = 5, kGrRefuseNoRecord = 6, kGrRefuseNotHolder = 7, kGrRefuseOlder = 8, kGrRefusePosition = 9;

struct GroundHeader
{
    unsigned int format, flags, generation, writerSlot;
    int sx, sy;
    unsigned int rows, cut;
    GroundHeader() : format(kGrFormat), flags(0), generation(0), writerSlot(0), sx(0), sy(0), rows(0), cut(0) {}
};

struct GroundRow
{
    std::string name;                                       /* the shared name (groundname.h) */
    std::string baseSid, companySid, materialSid, colorSid; /* the item type and the fields a GROUND ADD carries with it */
    unsigned int qty;
    float quality, charges;
    int functionKind, level;
    unsigned char unique;
    float pos[3];                                           /* exact world position x, y, z */
    float rot[4];                                           /* rotation w, x, y, z */
    coopmark::OwnerId owner;                                /* the stolen mark; kind 0 = none */
    gearwire::GearMarks gear;                               /* uniform and crafter; empty = none */
    std::vector<coopbag::BagRow> bag;                       /* a backpack's contents; empty = not a pack, or an empty one */
    GroundRow() : qty(1), quality(0.0f), charges(0.0f), functionKind(0), level(0), unique(0)
    { pos[0] = pos[1] = pos[2] = 0.0f; rot[0] = 1.0f; rot[1] = rot[2] = rot[3] = 0.0f; }
};

struct GrEncodeStats
{
    unsigned int written, cut, invalid, gearDropped;
    GrEncodeStats() : written(0), cut(0), invalid(0), gearDropped(0) {}
};

/* ---- the record id ---- */
/* any spelling of the prefix counts (file names ignore case, so "Ground.3.4" would land on ground.3.4's files); only the
   lower-case spelling GroundRecId makes parses, so another spelling meets the ground rules and is refused by them */
inline bool IsGroundRecId(const std::string& id)
{
    static const char kPrefix[] = "ground.";
    if (id.size() < 7) return false;
    for (size_t i = 0; i < 7; ++i) { char c = id[i]; if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); if (c != kPrefix[i]) return false; }
    return true;
}
/* "" when a sector is out of range */
inline std::string GroundRecId(int sx, int sy)
{
    if (sx < 0 || sx > kGrMaxSector || sy < 0 || sy > kGrMaxSector) return std::string();
    char b[32];
    std::sprintf(b, "ground.%d.%d", sx, sy);
    return std::string(b);
}
inline int GrParseSector(const std::string& s, size_t* at, char stop, int* out)
{
    size_t i = *at;
    int v = 0, digits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9')
    {
        if (digits > 0 && v == 0) return 0;   /* a leading zero: not the one spelling GroundRecId makes */
        v = v * 10 + (s[i] - '0');
        ++digits; ++i;
        if (v > kGrMaxSector) return 0;
    }
    if (digits == 0) return 0;
    if (stop != 0) { if (i >= s.size() || s[i] != stop) return 0; ++i; }
    else if (i != s.size()) return 0;
    *at = i;
    *out = v;
    return 1;
}
/* 1 = the id is exactly what GroundRecId makes for (*sx, *sy); 0 = not a ground record id (outputs untouched) */
inline int GroundRecIdParse(const std::string& id, int* sx, int* sy)
{
    if (id.compare(0, 7, "ground.") != 0) return 0;
    size_t at = 7;
    int a = 0, b = 0;
    if (GrParseSector(id, &at, '.', &a) == 0 || GrParseSector(id, &at, 0, &b) == 0) return 0;
    if (sx) *sx = a;
    if (sy) *sy = b;
    return 1;
}

/* ---- validity ---- */
inline bool GrFloatOk(float f) { return coopbag::BagFloatOk(f); }
inline bool GrCoordOk(float f) { return GrFloatOk(f) && f > -kGrMaxCoord && f < kGrMaxCoord; }
inline bool GrNameOk(const std::string& s)
{
    if (s.empty() || s.size() >= (size_t)coopground::kGroundKeyCap) return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}
/* the one row rule both ends apply (the gear marks as they travel: empty, or GearValid) */
inline bool GroundRowValid(const GroundRow& r)
{
    if (!GrNameOk(r.name)) return false;
    if (r.baseSid.empty() || r.baseSid.size() > coopbag::kBagMaxSid) return false;
    if (r.companySid.size() > coopbag::kBagMaxSid || r.materialSid.size() > coopbag::kBagMaxSid || r.colorSid.size() > coopbag::kBagMaxSid) return false;
    if (r.qty < 1 || r.qty > coopbag::kBagMaxQty) return false;
    if (!GrFloatOk(r.quality) || !GrFloatOk(r.charges)) return false;
    for (int i = 0; i < 3; ++i) if (!GrCoordOk(r.pos[i])) return false;
    for (int i = 0; i < 4; ++i) if (!GrFloatOk(r.rot[i])) return false;
    if (!coopmark::OwnerValid(r.owner)) return false;
    if (!gearwire::GearEmpty(r.gear) && !gearwire::GearValid(r.gear)) return false;
    if (r.bag.size() > coopbag::kBagMaxRows) return false;
    for (size_t i = 0; i < r.bag.size(); ++i) if (!coopbag::BagRowValid(r.bag[i])) return false;
    return coopbag::BagNestValid(r.bag);
}
inline bool GroundHeaderValid(const GroundHeader& h)
{
    return h.format == kGrFormat && (h.flags & ~kGrFlagSeed) == 0 && h.sx >= 0 && h.sx <= kGrMaxSector && h.sy >= 0 && h.sy <= kGrMaxSector
        && h.rows <= kGrMaxRows;
}

/* ---- bytes ---- */
inline void GrPutU32(std::vector<char>* b, unsigned int v) { const size_t at = b->size(); b->resize(at + 4); std::memcpy(&(*b)[at], &v, 4); }
inline void GrPutF32(std::vector<char>* b, float v) { const size_t at = b->size(); b->resize(at + 4); std::memcpy(&(*b)[at], &v, 4); }
inline void GrPutStr(std::vector<char>* b, const std::string& s) { GrPutU32(b, (unsigned int)s.size()); b->insert(b->end(), s.begin(), s.end()); }
inline void GrSetU32(std::vector<char>* b, size_t at, unsigned int v) { std::memcpy(&(*b)[at], &v, 4); }
/* Every test is `size - off < n` with off already <= size. */
inline bool GrGet4(const char* p, size_t size, size_t* off, void* v)
{
    if (*off > size || size - *off < 4) return false;
    std::memcpy(v, p + *off, 4);
    *off += 4;
    return true;
}
inline bool GrGetStr(const char* p, size_t size, size_t* off, unsigned int cap, std::string* s)
{
    unsigned int len = 0;
    if (!GrGet4(p, size, off, &len)) return false;
    if (len > cap || size - *off < (size_t)len) return false;
    s->assign(p + *off, len);
    *off += len;
    return true;
}

/* One row appended whole; false = nothing appended (the row breaks GroundRowValid after its gear is cleaned, or its bag cannot be
   written). *gearDropped: the gear fields emptied by cleaning (gearwire.h GearClean). */
inline bool GrPutRow(std::vector<char>* out, const GroundRow& in, unsigned int* gearDropped)
{
    GroundRow r = in;
    const unsigned int dropped = gearwire::GearClean(in.gear, &r.gear);
    if (gearDropped) *gearDropped = dropped;
    if (!GroundRowValid(r)) return false;
    std::vector<char> b;
    GrPutStr(&b, r.name);
    GrPutStr(&b, r.baseSid); GrPutStr(&b, r.companySid); GrPutStr(&b, r.materialSid); GrPutStr(&b, r.colorSid);
    GrPutU32(&b, r.qty);
    GrPutF32(&b, r.quality); GrPutF32(&b, r.charges);
    GrPutU32(&b, (unsigned int)r.functionKind); GrPutU32(&b, (unsigned int)r.level);
    b.push_back((char)(r.unique ? 1 : 0));
    for (int i = 0; i < 3; ++i) GrPutF32(&b, r.pos[i]);
    for (int i = 0; i < 4; ++i) GrPutF32(&b, r.rot[i]);
    unsigned char marks = 0;
    if (r.owner.kind != coopmark::kOwnNone) marks |= kGrHasOwner;
    if (!gearwire::GearEmpty(r.gear)) marks |= kGrHasGear;
    if (!r.bag.empty()) marks |= kGrHasBag;
    b.push_back((char)marks);
    if ((marks & kGrHasOwner) != 0 && !coopmark::EncodeOwner(&b, r.owner)) return false;
    if ((marks & kGrHasGear) != 0 && gearwire::EncodeGear(&b, r.gear, 0) != 1) return false;
    if ((marks & kGrHasBag) != 0)
    {
        std::vector<char> bag;
        if (!coopbag::EncodeBagRows(&bag, r.bag)) return false;
        GrPutU32(&b, (unsigned int)bag.size());
        b.insert(b.end(), bag.begin(), bag.end());
    }
    out->insert(out->end(), b.begin(), b.end());
    return true;
}

/* The record for header `h` (its flags, generation, writer slot and sector; h.cut = rows the caller already left out) and `rows`,
   into *out (replaced). A row that cannot be written whole is left out and counted in st->invalid; rows past kGrMaxRows or
   kGrMaxBytes are counted in the header's cut. false = nothing written (*out emptied): the header is out of range. */
inline bool EncodeGroundRecord(std::vector<char>* out, const GroundHeader& h, const std::vector<GroundRow>& rows, GrEncodeStats* st)
{
    GrEncodeStats s;
    if (out == 0) return false;
    out->clear();
    GroundHeader hh = h;
    hh.format = kGrFormat; hh.rows = 0;
    if (!GroundHeaderValid(hh)) { if (st) *st = s; return false; }
    GrPutU32(out, kGrMarker);
    GrPutU32(out, hh.format);
    GrPutU32(out, hh.flags);
    GrPutU32(out, hh.generation);
    GrPutU32(out, hh.writerSlot);
    GrPutU32(out, (unsigned int)hh.sx);
    GrPutU32(out, (unsigned int)hh.sy);
    GrPutU32(out, 0u);
    GrPutU32(out, 0u);
    unsigned int cut = h.cut;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (s.written >= kGrMaxRows) { ++cut; ++s.cut; continue; }
        std::vector<char> one;
        unsigned int gd = 0;
        if (!GrPutRow(&one, rows[i], &gd)) { ++s.invalid; continue; }
        s.gearDropped += gd;
        if (out->size() + one.size() > (size_t)kGrMaxBytes) { ++cut; ++s.cut; continue; }
        out->insert(out->end(), one.begin(), one.end());
        ++s.written;
    }
    GrSetU32(out, 28, s.written);
    GrSetU32(out, 32, cut);
    if (st) *st = s;
    return true;
}

/* The header alone (the first kGrHeaderBytes bytes) - what the world server reads back from a stored record's file. */
inline int DecodeGroundHeader(const char* p, size_t size, GroundHeader* out)
{
    if (p == 0 || size < (size_t)kGrHeaderBytes) return kGrShort;
    size_t off = 0;
    unsigned int marker = 0;
    GroundHeader h;
    GrGet4(p, size, &off, &marker);
    if (marker != kGrMarker) return kGrBadMarker;
    GrGet4(p, size, &off, &h.format);
    if (h.format != kGrFormat) return kGrBadFormat;
    GrGet4(p, size, &off, &h.flags);
    GrGet4(p, size, &off, &h.generation);
    GrGet4(p, size, &off, &h.writerSlot);
    GrGet4(p, size, &off, &h.sx);
    GrGet4(p, size, &off, &h.sy);
    GrGet4(p, size, &off, &h.rows);
    GrGet4(p, size, &off, &h.cut);
    if (h.rows > kGrMaxRows) return kGrOverCap;
    if (!GroundHeaderValid(h)) return kGrBadHeader;
    if (out) *out = h;
    return kGrOk;
}

/* One row at *off; false = refused (cut, an unknown mark bit, a block that is not whole, a row out of range). */
inline bool GrGetRow(const char* p, size_t size, size_t* off, GroundRow* out)
{
    GroundRow r;
    size_t o = *off;
    const unsigned int sidCap = coopbag::kBagMaxSid;
    if (!GrGetStr(p, size, &o, (unsigned int)coopground::kGroundKeyCap - 1u, &r.name)) return false;
    if (!GrGetStr(p, size, &o, sidCap, &r.baseSid) || !GrGetStr(p, size, &o, sidCap, &r.companySid)
        || !GrGetStr(p, size, &o, sidCap, &r.materialSid) || !GrGetStr(p, size, &o, sidCap, &r.colorSid)) return false;
    if (!GrGet4(p, size, &o, &r.qty) || !GrGet4(p, size, &o, &r.quality) || !GrGet4(p, size, &o, &r.charges)
        || !GrGet4(p, size, &o, &r.functionKind) || !GrGet4(p, size, &o, &r.level)) return false;
    if (o >= size) return false;
    r.unique = (unsigned char)(p[o] ? 1 : 0);
    o += 1;
    for (int i = 0; i < 3; ++i) if (!GrGet4(p, size, &o, &r.pos[i])) return false;
    for (int i = 0; i < 4; ++i) if (!GrGet4(p, size, &o, &r.rot[i])) return false;
    if (o >= size) return false;
    const unsigned char marks = (unsigned char)p[o];
    o += 1;
    if ((marks & ~(kGrHasOwner | kGrHasGear | kGrHasBag)) != 0) return false;
    if ((marks & kGrHasOwner) != 0)
    {
        size_t e = 0;
        if (coopmark::DecodeOwner(p, size, o, &r.owner, &e) != coopmark::kOwnOk) return false;
        o = e;
    }
    if ((marks & kGrHasGear) != 0)
    {
        size_t e = 0;
        if (gearwire::DecodeGear(p, size, o, &r.gear, &e) != gearwire::kGearOk) return false;
        o = e;
    }
    if ((marks & kGrHasBag) != 0)
    {
        unsigned int len = 0;
        if (!GrGet4(p, size, &o, &len) || len == 0 || size - o < (size_t)len) return false;
        size_t e = 0;
        if (coopbag::DecodeBagRows(p + o, (size_t)len, 0, &r.bag, &e) != coopbag::kBagOk || e != (size_t)len || r.bag.empty()) return false;
        o += (size_t)len;
    }
    if (!GroundRowValid(r)) return false;
    *off = o;
    *out = r;
    return true;
}

/* The whole record. kGrOk: *hOut and *rowsOut hold it; anything else: refused, and neither output is touched. */
inline int DecodeGroundRecord(const char* p, size_t size, GroundHeader* hOut, std::vector<GroundRow>* rowsOut)
{
    if (p == 0) return kGrShort;
    if (size > (size_t)kGrMaxBytes) return kGrTooBig;
    GroundHeader h;
    const int hr = DecodeGroundHeader(p, size, &h);
    if (hr != kGrOk) return hr;
    size_t off = kGrHeaderBytes;
    std::vector<GroundRow> rows;
    for (unsigned int i = 0; i < h.rows; ++i)
    {
        GroundRow r;
        if (!GrGetRow(p, size, &off, &r)) return kGrBadRow;   /* cut inside a row, or a row out of range */
        rows.push_back(r);
    }
    if (off != size) return kGrTrailing;
    if (hOut) *hOut = h;
    if (rowsOut) rowsOut->swap(rows);
    return kGrOk;
}
inline const char* GroundDecodeWhy(int why)
{
    return why == kGrOk ? "ok" : why == kGrShort ? "cut short" : why == kGrBadMarker ? "not a ground record (wrong marker)"
         : why == kGrBadFormat ? "an unknown format" : why == kGrBadHeader ? "a header out of range" : why == kGrOverCap ? "more rows than the cap"
         : why == kGrBadRow ? "a row that is not whole or out of range" : why == kGrTrailing ? "bytes after the last row"
         : why == kGrTooBig ? "larger than the byte cap" : "malformed";
}

/* ---- the world server's rule ---- */
/* exists: a record of this id is stored; storedGen: its generation; seed: the write is a seed (kGrFlagSeed); gen: the write's
   generation; senderSlot / holderSlot: the writing game's slot and the slot the area map names as the area's holder (-1 = none). */
inline int GroundAcceptRule(int exists, unsigned int storedGen, int seed, unsigned int gen, int senderSlot, int holderSlot)
{
    const int holds = (senderSlot >= 0 && holderSlot >= 0 && senderSlot == holderSlot) ? 1 : 0;
    if (exists == 0)
    {
        if (seed == 0) return kGrRefuseNoRecord;
        return holds != 0 ? kGrAccept : kGrRefuseNotHolder;
    }
    if (seed != 0) return kGrRefuseSeedExists;
    if (holds == 0) return kGrRefuseNotHolder;
    if (gen < storedGen) return kGrRefuseOlder;
    return kGrAccept;
}
/* A whole write judged: the id must parse, the bytes decode, the header's sector match the id and its writer slot be the sender's,
   then GroundAcceptRule. *hOut (may be 0): the decoded header (left default when the bytes do not decode). */
inline int GroundJudgeWrite(const std::string& id, const char* p, size_t size, int senderSlot, int holderSlot, int exists, unsigned int storedGen,
                            GroundHeader* hOut)
{
    int sx = 0, sy = 0;
    if (GroundRecIdParse(id, &sx, &sy) == 0) return kGrRefuseBadId;
    GroundHeader h;
    if (DecodeGroundRecord(p, size, &h, 0) != kGrOk) return kGrRefuseUnreadable;
    if (hOut) *hOut = h;
    if (h.sx != sx || h.sy != sy) return kGrRefuseWrongArea;
    if (senderSlot < 0 || h.writerSlot != (unsigned int)senderSlot) return kGrRefuseWriter;
    return GroundAcceptRule(exists, storedGen, (h.flags & kGrFlagSeed) != 0 ? 1 : 0, h.generation, senderSlot, holderSlot);
}
inline const char* GroundRefuseWhy(int why)
{
    return why == kGrAccept ? "taken"
         : why == kGrRefuseBadId ? "the id is not ground.<sx>.<sy>"
         : why == kGrRefuseUnreadable ? "the record's bytes are not a whole ground record"
         : why == kGrRefuseWrongArea ? "the record names another area than its id"
         : why == kGrRefuseWriter ? "the record's writer slot is not the sending game's"
         : why == kGrRefuseSeedExists ? "a seed, and the area already has a record (the first writer won)"
         : why == kGrRefuseNoRecord ? "an update, and the area has no record to update (send a seed)"
         : why == kGrRefuseNotHolder ? "the sending game does not hold the area"
         : why == kGrRefuseOlder ? "its generation is lower than the stored record's"
         : why == kGrRefusePosition ? "a position-only update - a ground record is always written whole"
         : "refused";
}

/* ---- GROUND_REFUSE (kMsgGroundRefuse): {str id | u32 reason | u32 stored generation | u32 sent generation} ---- */
inline std::vector<char> EncodeGroundRefuse(const std::string& id, int reason, unsigned int storedGen, unsigned int sentGen)
{
    std::vector<char> b;
    GrPutStr(&b, id);
    GrPutU32(&b, (unsigned int)reason);
    GrPutU32(&b, storedGen);
    GrPutU32(&b, sentGen);
    return b;
}
/* 1 = read whole (nothing after it); 0 = refused, outputs untouched */
inline int DecodeGroundRefuse(const std::vector<char>& b, std::string* id, int* reason, unsigned int* storedGen, unsigned int* sentGen)
{
    if (b.empty()) return 0;
    const char* p = &b[0];
    size_t off = 0;
    std::string s;
    unsigned int r = 0, sg = 0, ng = 0;
    if (!GrGetStr(p, b.size(), &off, (unsigned int)coopground::kGroundKeyCap, &s)) return 0;
    if (!GrGet4(p, b.size(), &off, &r) || !GrGet4(p, b.size(), &off, &sg) || !GrGet4(p, b.size(), &off, &ng) || off != b.size()) return 0;
    if (id) *id = s;
    if (reason) *reason = (int)r;
    if (storedGen) *storedGen = sg;
    if (sentGen) *sentGen = ng;
    return 1;
}

}   /* namespace coopgrec */
