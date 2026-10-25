#pragma once
/* src/common/piecerec.h - ONE BUILT PIECE AS ONE WORLD-SERVER RECORD (a building, a wall, a piece of furniture a player placed).
 *
 * One record per piece, id `piece.<sx>.<sy>.#<hash8>.<x10>.<z10>.<y10>`: the piece's position key (poskey.h,
 * `<sid>@<sx>,<sy>@<x10>,<z10>,<y10>`, the key the build messages use) WITH its height part (pieces stack), and its record name
 * always in the hashed form ('#' + 8 lower-case hex digits of poskey::SidHash32, as boxrec.h MakeBoxRecId does; a key that already
 * carries the hashed form keeps it, so both texts of one key give one id). A key with no height, or that is not a position key, makes
 * no id. Numbers are plain decimal with no leading zero; x10, z10 and y10 may be negative. The record's bytes (the `.platoon` file on
 * the world server, message 28 RECORD on the wire) are laid out as:
 *
 *   HEADER (little-endian):
 *     u32 marker 'PCE1' | u32 format (kPrFormat) | u32 generation | i32 writer slot | i32 owner slot (the piece's owner player) |
 *     u32 length + the full piece key (1..kPrKeyMax bytes, no control byte) | u32 placement number (the placement's nonce, 0 = not
 *     known) | u32 flags (kPrFlagSeed: the owner's first record of the piece; kPrFlagRemoved: a removal mark) | u32 body length
 *   BODY, a live piece: u32 length + the PLACE bytes | u32 length + the STATE bytes - one pp.build row (ownrec.h, buildwire.h
 *     EncodeBuild): the PLACE names the header's key and placement number, the STATE names the header's key (its helper tail kept).
 *   BODY, a removal mark: u32 reason (buildwire.h kBuildReasonDismantled / kBuildReasonDestroyed) and nothing else.
 *   Nothing may follow the body. The whole record is at most kPrMaxBytes.
 *
 * A record that is cut short, carries an unknown marker, format, flag or slot, names a key that makes no piece id, holds a body that
 * is not one whole PLACE + STATE pair for its key (or one known reason for a removal mark), or has bytes after its body DECODES TO
 * REFUSED: the caller gets no header and no body - never part of a piece. The encoder writes a record whole or not at all.
 *
 * THE WORLD SERVER'S RULE (PieceAcceptRule): ONE WRITER at a time, the box rule (boxrec.h BoxMayWrite): while the piece's owner
 * player's game has the piece's area loaded only that owner writes; otherwise only the area's holder. A SEED (the owner's first
 * record of its own piece) names the sender as the owner and is taken when no record of that id exists, or over a removal mark with a
 * higher generation (a new placement at the spot), and only while the owner's game has the piece's area loaded or nobody holds it
 * (PieceSeedPlaceOk; else kPrRefuseSeedNotLoaded). Any later write is an update from the one writer of
 * the stored record with a generation higher than the stored one - the very same bytes again at the stored generation are a harmless
 * repeat and nothing is written; only a seed replaces a removal mark; only the area's holder may change a record's owner. A record
 * is never removed: a removal is a mark. A refused write is answered with kMsgPieceRefuse {str id | u32 reason | u32 stored generation
 * | u32 sent generation}.
 *
 * Pure: no engine memory, no Windows, no globals. C++03 (VS2010 v100).
 */

#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include "poskey.h"     /* the record name's hash and its hashed form */
#include "boxrec.h"     /* the key reading, the number spelling, the byte helpers and the one-writer rule the piece shares */
#include "buildwire.h"  /* the PLACE and STATE bytes the body carries */

namespace piecerec {

const unsigned int kProtocol = 108;               /* the world-server protocol that carries piece records and PIECE_REFUSE */
const unsigned int kMsgPieceRefuse = 79u;         /* world server -> the writer: its piece record was not taken, and why */

const unsigned int kPrMarker = 0x31454350u;       /* 'P','C','E','1' little-endian */
const unsigned int kPrFormat = 1u;
const unsigned int kPrMaxBytes = 4096u;           /* one pp.build row is a few hundred bytes (ownrec.h kBuildRecMaxBytes) */
const unsigned int kPrFlagSeed = 1u, kPrFlagRemoved = 2u, kPrFlagsAll = 3u;
const unsigned int kPrKeyMax = coopbuild::kBuildMaxKey;   /* the build messages' key room */
const unsigned int kPrFixedBytes = 24u;           /* marker, format, generation, writer, owner, key length */
const unsigned int kPrHeaderMaxBytes = kPrFixedBytes + kPrKeyMax + 12u;   /* the longest header: what the server reads back from a file */
const unsigned int kPrIdMax = 128u;               /* longer than any id MakePieceRecId makes */
const int kPrMaxSlot = coopbrec::kBrMaxSlot;

/* decode results: anything but kPrOk is a refused record */
const int kPrOk = 0, kPrShort = 1, kPrBadMarker = 2, kPrBadFormat = 3, kPrBadHeader = 4, kPrBadBody = 5, kPrTrailing = 6, kPrTooBig = 7,
          kPrDecodeMax = 7;

/* the world server's answers */
const int kPrAccept = 0, kPrRefuseBadId = 1, kPrRefuseUnreadable = 2, kPrRefuseWrongPiece = 3, kPrRefuseWriter = 4,
          kPrRefuseSeedExists = 5, kPrRefuseNoRecord = 6, kPrRefuseNotWriter = 7, kPrRefuseOlder = 8, kPrRefusePosition = 9,
          kPrRefuseOwnerChange = 10, kPrRefuseSameGen = 11,
          kPrRepeat = 12,                     /* not a refusal: the very same bytes again at the stored generation - nothing is written */
          kPrRefuseSeedNotOwner = 13, kPrRefuseOverRemoval = 14, kPrRefuseGone = 15, kPrRefuseSeedNotLoaded = 16,
          kPrJudgeMax = 16;

struct PieceHeader
{
    unsigned int format, flags, generation, placement;
    int writerSlot, ownerSlot;
    std::string key;                 /* the full piece key, height included */
    unsigned int bodyBytes;          /* filled by the decoder */
    PieceHeader() : format(kPrFormat), flags(0), generation(0), placement(0), writerSlot(0), ownerSlot(0), bodyBytes(0) {}
};
struct PieceBody
{
    std::string place, state;        /* a live piece: the pp.build row's PLACE and STATE bytes */
    unsigned int reason;             /* a removal mark: why the piece went */
    PieceBody() : reason(0) {}
};

/* ---- the record id ---- */
/* any spelling of the prefix counts (file names ignore case); only the lower-case spelling MakePieceRecId makes parses */
inline bool IsPieceRecId(const std::string& id)
{
    static const char kPrefix[] = "piece.";
    if (id.size() < 6) return false;
    for (size_t i = 0; i < 6; ++i) { char c = id[i]; if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); if (c != kPrefix[i]) return false; }
    return true;
}
inline std::string PieceRecIdOf(const std::string& sid, int sx, int sy, long x10, long z10, long y10)
{
    return "piece." + coopbrec::BrLongText((long)sx) + "." + coopbrec::BrLongText((long)sy) + "." + sid + "." + coopbrec::BrLongText(x10)
         + "." + coopbrec::BrLongText(z10) + "." + coopbrec::BrLongText(y10);
}
/* 1 = the id is exactly a spelling MakePieceRecId makes (its parts into the outputs that are not 0); 0 = not a piece record id
   (outputs untouched). The sid field must be the hashed form, '#' + exactly 8 lower-case hex digits. */
inline int PieceRecIdParse(const std::string& id, int* sx, int* sy, std::string* sid, long* x10, long* z10, long* y10)
{
    if (id.size() > kPrIdMax || id.compare(0, 6, "piece.") != 0) return 0;
    size_t at = 6;
    long a = 0, b = 0;
    if (coopbrec::BrParseLong(id, &at, id.size(), 1, &a) == 0 || at >= id.size() || id[at] != '.') return 0;
    ++at;
    if (coopbrec::BrParseLong(id, &at, id.size(), 1, &b) == 0 || at >= id.size() || id[at] != '.') return 0;
    ++at;
    if (a < 0 || a > coopbrec::kBrMaxSector || b < 0 || b > coopbrec::kBrMaxSector) return 0;
    if (id.size() - at < 10) return 0;
    const std::string s = id.substr(at, 9);
    if (poskey::IsHashedSidField(s.c_str()) == 0) return 0;
    at += 9;
    if (id[at] != '.') return 0;
    ++at;
    long x = 0, z = 0, y = 0;
    if (coopbrec::BrParseLong(id, &at, id.size(), 1, &x) == 0 || at >= id.size() || id[at] != '.') return 0;
    ++at;
    if (coopbrec::BrParseLong(id, &at, id.size(), 1, &z) == 0 || at >= id.size() || id[at] != '.') return 0;
    ++at;
    if (coopbrec::BrParseLong(id, &at, id.size(), 1, &y) == 0 || at != id.size()) return 0;
    if (sx) *sx = (int)a;
    if (sy) *sy = (int)b;
    if (sid) *sid = s;
    if (x10) *x10 = x;
    if (z10) *z10 = z;
    if (y10) *y10 = y;
    return 1;
}
/* The record id of the piece a key names: its record name always in the hashed form, its height kept. "" when the key is not a
   position key with a height (no id is ever guessed for it). */
inline std::string MakePieceRecId(const std::string& fullKey)
{
    if (!coopbrec::BrTextOk(fullKey, kPrKeyMax)) return std::string();
    coopbrec::BoxKeyParts k;
    if (coopbrec::BoxKeyParse(fullKey, &k) == 0 || k.haveY == 0) return std::string();
    std::string field = k.sid;
    if (poskey::IsHashedSidField(field.c_str()) == 0)
    {
        char hashed[16];
        int w = poskey::AppendStr(hashed, (int)sizeof hashed, 0, "#");
        w = poskey::AppendHex8(hashed, (int)sizeof hashed, w, poskey::SidHash32(field.c_str()));
        if (w < 0) return std::string();
        field = hashed;
    }
    const std::string id = PieceRecIdOf(field, k.sx, k.sy, k.x10, k.z10, k.y10);
    return PieceRecIdParse(id, 0, 0, 0, 0, 0, 0) != 0 ? id : std::string();
}
/* the piece's world position from its id (tenths of a unit back to units): the position a RECORD carries (the server's gate wants a
   real one). 1 = read, 0 = not a piece record id. */
inline int PieceRecPosOf(const std::string& id, float* x, float* y, float* z)
{
    long x10 = 0, z10 = 0, y10 = 0;
    if (PieceRecIdParse(id, 0, 0, 0, &x10, &z10, &y10) == 0) return 0;
    if (x) *x = (float)x10 / 10.0f;
    if (y) *y = (float)y10 / 10.0f;
    if (z) *z = (float)z10 / 10.0f;
    return 1;
}

/* ---- validity ---- */
inline bool PrSlotOk(int slot) { return slot >= 0 && slot <= kPrMaxSlot; }
inline bool PieceHeaderValid(const PieceHeader& h)
{
    return h.format == kPrFormat && (h.flags & ~kPrFlagsAll) == 0 && PrSlotOk(h.writerSlot) && PrSlotOk(h.ownerSlot) && !MakePieceRecId(h.key).empty();
}
/* the body's own test against its header: a removal mark carries one known reason; a live piece a PLACE of the header's key and
   placement number and a STATE of the same key, each decoding whole */
inline bool PieceBodyValid(const PieceHeader& h, const PieceBody& b)
{
    if ((h.flags & kPrFlagRemoved) != 0)
        return b.reason == (unsigned int)coopbuild::kBuildReasonDismantled || b.reason == (unsigned int)coopbuild::kBuildReasonDestroyed;
    if (b.place.empty() || b.state.empty()) return false;
    coopbuild::BuildMsg m, st;
    if (coopbuild::DecodeBuildSaved(b.place.data(), b.place.size(), &m) != coopbuild::kBuildDecodeOk || m.kind != coopbuild::kBuildPlace) return false;
    if (m.key != h.key || m.nonce != h.placement) return false;
    if (coopbuild::DecodeBuild(b.state.data(), b.state.size(), &st) != coopbuild::kBuildDecodeOk || st.kind != coopbuild::kBuildState) return false;
    return st.key == h.key;
}

/* ---- bytes ---- */
/* The record for header `h` and body `b`, into *out (replaced). false = nothing written (*out emptied): the header is out of range,
   the body is not valid for it, or the record would pass kPrMaxBytes. */
inline bool EncodePieceRecord(std::vector<char>* out, const PieceHeader& h, const PieceBody& b)
{
    if (out == 0) return false;
    out->clear();
    PieceHeader hh = h;
    hh.format = kPrFormat;
    if (!PieceHeaderValid(hh) || !PieceBodyValid(hh, b)) return false;
    std::vector<char> body;
    if ((hh.flags & kPrFlagRemoved) != 0) coopbrec::BrPutU32(&body, b.reason);
    else { coopbrec::BrPutStr(&body, b.place); coopbrec::BrPutStr(&body, b.state); }
    std::vector<char> r;
    coopbrec::BrPutU32(&r, kPrMarker);
    coopbrec::BrPutU32(&r, hh.format);
    coopbrec::BrPutU32(&r, hh.generation);
    coopbrec::BrPutU32(&r, (unsigned int)hh.writerSlot);
    coopbrec::BrPutU32(&r, (unsigned int)hh.ownerSlot);
    coopbrec::BrPutStr(&r, hh.key);
    coopbrec::BrPutU32(&r, hh.placement);
    coopbrec::BrPutU32(&r, hh.flags);
    coopbrec::BrPutU32(&r, (unsigned int)body.size());
    if (r.size() + body.size() > (size_t)kPrMaxBytes) return false;
    r.insert(r.end(), body.begin(), body.end());
    out->swap(r);
    return true;
}
/* The header alone (through the body length) - what the world server reads back from a stored record's file, at most
   kPrHeaderMaxBytes bytes. */
inline int DecodePieceHeader(const char* p, size_t size, PieceHeader* out)
{
    if (p == 0) return kPrShort;
    size_t off = 0;
    unsigned int marker = 0;
    PieceHeader h;
    if (!coopbrec::BrGet4(p, size, &off, &marker)) return kPrShort;
    if (marker != kPrMarker) return kPrBadMarker;
    if (!coopbrec::BrGet4(p, size, &off, &h.format)) return kPrShort;
    if (h.format != kPrFormat) return kPrBadFormat;
    if (!coopbrec::BrGet4(p, size, &off, &h.generation) || !coopbrec::BrGet4(p, size, &off, &h.writerSlot) || !coopbrec::BrGet4(p, size, &off, &h.ownerSlot))
        return kPrShort;
    unsigned int len = 0;
    {
        size_t o = off;
        if (!coopbrec::BrGet4(p, size, &o, &len)) return kPrShort;
        if (len == 0 || len > kPrKeyMax) return kPrBadHeader;
    }
    if (!coopbrec::BrGetStr(p, size, &off, kPrKeyMax, &h.key)) return kPrShort;
    if (!coopbrec::BrGet4(p, size, &off, &h.placement) || !coopbrec::BrGet4(p, size, &off, &h.flags) || !coopbrec::BrGet4(p, size, &off, &h.bodyBytes))
        return kPrShort;
    if (!PieceHeaderValid(h)) return kPrBadHeader;
    if (h.bodyBytes > kPrMaxBytes) return kPrTooBig;
    if (out) *out = h;
    return kPrOk;
}
/* the length of a decoded header (the body starts there) */
inline size_t PieceHeaderBytes(const PieceHeader& h) { return (size_t)kPrFixedBytes + h.key.size() + 12u; }

/* The whole record. kPrOk: *hOut and *bodyOut hold it; anything else: refused, and neither output is touched. */
inline int DecodePieceRecord(const char* p, size_t size, PieceHeader* hOut, PieceBody* bodyOut)
{
    if (p == 0) return kPrShort;
    if (size > (size_t)kPrMaxBytes) return kPrTooBig;
    PieceHeader h;
    const int hr = DecodePieceHeader(p, size, &h);
    if (hr != kPrOk) return hr;
    const size_t off = PieceHeaderBytes(h);
    if (size - off < (size_t)h.bodyBytes) return kPrShort;
    if (size - off > (size_t)h.bodyBytes) return kPrTrailing;
    PieceBody b;
    size_t at = 0;
    const char* q = p + off;
    const size_t n = (size_t)h.bodyBytes;
    if ((h.flags & kPrFlagRemoved) != 0)
    {
        if (!coopbrec::BrGet4(q, n, &at, &b.reason) || at != n) return kPrBadBody;
    }
    else
    {
        if (!coopbrec::BrGetStr(q, n, &at, kPrMaxBytes, &b.place) || !coopbrec::BrGetStr(q, n, &at, kPrMaxBytes, &b.state) || at != n) return kPrBadBody;
    }
    if (!PieceBodyValid(h, b)) return kPrBadBody;
    if (hOut) *hOut = h;
    if (bodyOut) { bodyOut->place.swap(b.place); bodyOut->state.swap(b.state); bodyOut->reason = b.reason; }
    return kPrOk;
}
inline const char* PieceDecodeWhy(int why)
{
    return why == kPrOk ? "ok" : why == kPrShort ? "cut short" : why == kPrBadMarker ? "not a piece record (wrong marker)"
         : why == kPrBadFormat ? "an unknown format" : why == kPrBadHeader ? "a header out of range"
         : why == kPrBadBody ? "a body that is not one whole PLACE and STATE (or one removal reason) for its key"
         : why == kPrTrailing ? "bytes after the body" : why == kPrTooBig ? "larger than the byte cap" : "malformed";
}

/* ---- the world server's rule ---- */
/* May this game write the piece? The box rule: while the piece's owner player's game has the piece's area loaded only that owner may;
   otherwise only the area's holder. */
inline int PieceMayWrite(int senderSlot, int holderSlot, int ownerSlot, int ownerLoaded)
{
    return coopbrec::BoxMayWrite(senderSlot, holderSlot, ownerSlot, ownerLoaded);
}
/* exists: a record of this id is stored; storedGen / storedOwner / storedRemoved: its generation, owner slot and removal mark;
   storedOwnerLoaded: 1 when the stored owner's game has the area loaded; sameBytes: 1 when the write's bytes are exactly the stored
   record's; seed / removed: the write's flags; gen / sentOwner: its generation and owner slot; sentOwnerLoaded: 1 when the owner the
   write names has the area loaded. */
/* An owner's seed is placed only while the owner's game has the area loaded (or is the area's holder), or nobody holds the area -
   the box rule, with an area nobody holds open to its owner */
inline int PieceSeedPlaceOk(int senderSlot, int holderSlot, int ownerSlot, int ownerLoaded)
{
    return (PieceMayWrite(senderSlot, holderSlot, ownerSlot, ownerLoaded) != 0 || holderSlot < 0) ? 1 : 0;
}
inline int PieceAcceptRule(int exists, unsigned int storedGen, int storedOwner, int storedOwnerLoaded, int storedRemoved, int sameBytes,
                           int seed, int removed, unsigned int gen, int sentOwner, int sentOwnerLoaded, int senderSlot, int holderSlot)
{
    if (exists != 0 && gen == storedGen && sameBytes != 0) return kPrRepeat;   /* the very same bytes again: nothing to write */
    if (seed != 0)
    {
        if (sentOwner != senderSlot || senderSlot < 0) return kPrRefuseSeedNotOwner;   /* no game seeds another player's piece */
        if (exists == 0) return PieceSeedPlaceOk(senderSlot, holderSlot, sentOwner, sentOwnerLoaded) != 0 ? kPrAccept : kPrRefuseSeedNotLoaded;
        if (storedRemoved == 0) return kPrRefuseSeedExists;
        if (gen <= storedGen) return kPrRefuseOlder;   /* over a removal mark: a higher generation only */
        return PieceSeedPlaceOk(senderSlot, holderSlot, sentOwner, sentOwnerLoaded) != 0 ? kPrAccept : kPrRefuseSeedNotLoaded;
    }
    if (exists == 0) return kPrRefuseNoRecord;
    if (PieceMayWrite(senderSlot, holderSlot, storedOwner, storedOwnerLoaded) == 0) return kPrRefuseNotWriter;
    if (gen < storedGen) return kPrRefuseOlder;
    if (gen == storedGen) return kPrRefuseSameGen;
    if (storedRemoved != 0 && removed == 0) return kPrRefuseOverRemoval;   /* only a seed (a new placement) replaces a removal mark */
    if (sentOwner != storedOwner && !(holderSlot >= 0 && senderSlot == holderSlot)) return kPrRefuseOwnerChange;
    return kPrAccept;
}
/* A whole write judged: the id must parse, the bytes decode, the header's key name this id's piece and its writer slot be the
   sender's, then PieceAcceptRule. loadedSlots: the slots whose games the area map shows with the piece's area loaded now. *hOut (may
   be 0): the decoded header (left default when the bytes do not decode). kPrAccept: store it; kPrRepeat: taken, nothing to store;
   anything else: refused. */
inline int PieceJudgeWrite(const std::string& id, const char* p, size_t size, int senderSlot, int holderSlot, const std::vector<int>& loadedSlots,
                           int exists, unsigned int storedGen, int storedOwner, int storedRemoved, int sameBytes, PieceHeader* hOut)
{
    if (PieceRecIdParse(id, 0, 0, 0, 0, 0, 0) == 0) return kPrRefuseBadId;
    PieceHeader h;
    if (DecodePieceRecord(p, size, &h, 0) != kPrOk) return kPrRefuseUnreadable;
    if (hOut) *hOut = h;
    if (MakePieceRecId(h.key) != id) return kPrRefuseWrongPiece;
    if (senderSlot < 0 || h.writerSlot != senderSlot) return kPrRefuseWriter;
    return PieceAcceptRule(exists, storedGen, storedOwner, coopbrec::BrSlotIn(loadedSlots, storedOwner), storedRemoved, sameBytes,
                           (h.flags & kPrFlagSeed) != 0 ? 1 : 0, (h.flags & kPrFlagRemoved) != 0 ? 1 : 0, h.generation, h.ownerSlot,
                           coopbrec::BrSlotIn(loadedSlots, h.ownerSlot), senderSlot, holderSlot);
}
inline const char* PieceRefuseWhy(int why)
{
    return why == kPrAccept ? "taken"
         : why == kPrRefuseBadId ? "the id is not piece.<sx>.<sy>.<sid>.<x10>.<z10>.<y10>"
         : why == kPrRefuseUnreadable ? "the record's bytes are not a whole piece record"
         : why == kPrRefuseWrongPiece ? "the record's key names another piece than its id"
         : why == kPrRefuseWriter ? "the record's writer slot is not the sending game's"
         : why == kPrRefuseSeedExists ? "a seed, and the piece already has a live record"
         : why == kPrRefuseNoRecord ? "an update, and the piece has no record to update (send a seed)"
         : why == kPrRefuseNotWriter ? "the sending game is not the piece's one writer (its owner while the owner's game has the area loaded, else the area's holder)"
         : why == kPrRefuseOlder ? "its generation is not above the stored record's"
         : why == kPrRefusePosition ? "a position-only update - a piece record is always written whole"
         : why == kPrRefuseOwnerChange ? "it changes the piece's owner, and only the area's holder may"
         : why == kPrRefuseSameGen ? "its generation equals the stored record's and its bytes differ"
         : why == kPrRepeat ? "the very same bytes again (taken, nothing written)"
         : why == kPrRefuseSeedNotOwner ? "a seed that does not name the sending game as the piece's owner"
         : why == kPrRefuseOverRemoval ? "an update over a removal mark - only a new placement's seed replaces it"
         : why == kPrRefuseSeedNotLoaded ? "a seed while another game holds the piece's area and the owner's game does not have it loaded"
         : why == kPrRefuseGone ? "a removal of the record - a piece record is never removed, a removal is a mark"
         : "refused";
}

/* ---- PIECE_REFUSE (kMsgPieceRefuse): {str id | u32 reason | u32 stored generation | u32 sent generation} ---- */
inline std::vector<char> EncodePieceRefuse(const std::string& id, int reason, unsigned int storedGen, unsigned int sentGen)
{
    return coopbrec::EncodeBoxRefuse(id.size() > kPrIdMax ? id.substr(0, kPrIdMax) : id, reason, storedGen, sentGen);
}
/* 1 = read whole (nothing after it); 0 = refused, outputs untouched */
inline int DecodePieceRefuse(const std::vector<char>& b, std::string* id, int* reason, unsigned int* storedGen, unsigned int* sentGen)
{
    return coopbrec::DecodeBoxRefuse(b, id, reason, storedGen, sentGen);
}

/* ---- a game's intake of an arriving record ----
   Kept per id only when the id is one MakePieceRecId makes, the record decodes whole, its key names that very id and its generation
   is not lower than the copy kept (an equal generation replaces it). Anything else is counted by its reason and never kept. */
const int kPiKeep = 0, kPiBadId = 1, kPiUnreadable = 2, kPiWrongPiece = 3, kPiOlder = 4, kPiMax = 4;
inline int PieceIntakeJudge(const std::string& id, const char* p, size_t size, int haveCopy, unsigned int copyGen,
                            PieceHeader* hOut, PieceBody* bodyOut, int* decodeWhy)
{
    if (decodeWhy) *decodeWhy = kPrOk;
    if (PieceRecIdParse(id, 0, 0, 0, 0, 0, 0) == 0) return kPiBadId;
    PieceHeader h;
    PieceBody b;
    const int d = DecodePieceRecord(p, size, &h, &b);
    if (d != kPrOk) { if (decodeWhy) *decodeWhy = d; return kPiUnreadable; }
    if (MakePieceRecId(h.key) != id) return kPiWrongPiece;
    if (hOut) *hOut = h;
    if (haveCopy != 0 && h.generation < copyGen) return kPiOlder;
    if (bodyOut) { bodyOut->place.swap(b.place); bodyOut->state.swap(b.state); bodyOut->reason = b.reason; }
    return kPiKeep;
}
inline const char* PieceIntakeWhy(int why)
{
    return why == kPiKeep ? "kept" : why == kPiBadId ? "the id is not a piece record id" : why == kPiUnreadable ? "the record does not decode"
         : why == kPiWrongPiece ? "the record's key names another piece" : why == kPiOlder ? "older than the kept copy" : "unknown";
}

/* ---- a game's own write ---- */
/* The generation of this game's next write (boxrec.h BoxWriteGen): 1 = no record known - generation 1; 0 = one above the higher of
   the kept record and this game's last write (or the stored generation a refusal named); -1 = no write (cannot go higher). */
inline int PieceWriteGen(int haveKept, unsigned int keptGen, int haveSent, unsigned int sentGen, unsigned int* gen)
{
    return coopbrec::BoxWriteGen(haveKept, keptGen, haveSent, sentGen, gen);
}
/* What a PIECE_REFUSE changes in this game's own write state - it applies nothing to the piece. Older, the same generation with other
   bytes, or a seed where a live record exists: the stored generation is known, the piece is written again above it (kPwrRaise). No
   record to update: written again as a seed (kPwrSeed). Not the writer, or an owner change: written again once the games may read the
   writer alike (kPwrRetry). Any other reason: the mark is dropped (kPwrNone). */
const int kPwrNone = 0, kPwrRaise = 1, kPwrSeed = 2, kPwrRetry = 3;
inline int PieceRefuseNext(int reason, unsigned int storedGen, int* haveSent, unsigned int* sentGen)
{
    if (reason == kPrRefuseOlder || reason == kPrRefuseSameGen || reason == kPrRefuseSeedExists)
    {
        if (*haveSent == 0 || storedGen > *sentGen) *sentGen = storedGen;
        *haveSent = 1;
        return kPwrRaise;
    }
    if (reason == kPrRefuseNoRecord) { *haveSent = 0; *sentGen = 0; return kPwrSeed; }
    if (reason == kPrRefuseOverRemoval)
    {   /* the stored record is a removal mark: the live piece is a new placement, written as a seed above it */
        if (*haveSent == 0 || storedGen > *sentGen) *sentGen = storedGen;
        *haveSent = 1;
        return kPwrRaise;
    }
    if (reason == kPrRefuseNotWriter || reason == kPrRefuseOwnerChange || reason == kPrRefuseSeedNotLoaded) return kPwrRetry;
    return kPwrNone;
}

/* ---- when a write is due: ONE WRITE PER CHANGE, sent when the change happens (no clock) ---- */
const int kPeNone = 0, kPePlaced = 1, kPeFinished = 2, kPeDestroyed = 3, kPeBroken = 4, kPeRepaired = 5, kPeRemoved = 6, kPeMats = 7,
          kPeHelp = 8, kPeStarted = 9, kPeStopped = 10, kPeProgress = 11, kPeHit = 12, kPeSeed = 13, kPeFirst = 14, kPeAgain = 15, kPeMax = 15;
inline const char* PieceEventName(int e)
{
    static const char* const n[] = { "none", "placed", "finished", "destroyed", "broken", "repaired", "removed", "mats", "help",
                                     "started", "stopped", "progress", "hit", "seed", "first", "again" };
    return (e >= 0 && e <= kPeMax) ? n[e] : "unknown";
}
/* the tenth of the piece's needed work that progress has reached, 0..10 (a hair of rounding counts as reached) */
inline int PieceTenthOf(float progress, float needed)
{
    double t = (double)progress / (double)needed * 10.0 + 1e-4;
    if (!(t > 0.0)) return 0;
    if (t >= 10.0) return 10;
    return (int)std::floor(t);
}
/* 1 = a write is due. Every event but progress is a change by itself; progress is a change when it crosses a tenth of needed (0.1,
   0.2 .. of needed) - a smaller step is not written. A piece with no needed amount (0 or not a number) changes with any progress. */
inline int PieceWriteDue(float oldProgress, float newProgress, float needed, int event)
{
    if (event != kPeProgress) return (event > kPeNone && event <= kPeMax) ? 1 : 0;
    if (newProgress == oldProgress) return 0;
    if (!(needed > 0.0f) || !coopbuild::BuildFloatOk(needed) || !coopbuild::BuildFloatOk(oldProgress) || !coopbuild::BuildFloatOk(newProgress)) return 1;
    return PieceTenthOf(oldProgress, needed) != PieceTenthOf(newProgress, needed) ? 1 : 0;
}

/* ---- what a game knows the world server stores of a piece ----
   The world server never sends a game its own writes back, so a game's kept copy can lag what the server stores of the game's own
   writes. Each write taken (RECORD_SEQ) and each refusal's word teaches the stored record's generation and whether it is a removal
   mark; the newest of that and the kept copy decides seed or update. */
struct PieceSrvView { int have; unsigned int gen; int removed; PieceSrvView() : have(0), gen(0), removed(0) {} };
/* this game's write of generation gen (a removal mark or not) is stored: it is the stored record unless a newer one is known */
inline void PieceTakenNote(PieceSrvView* v, unsigned int gen, int removed)
{
    if (v == 0 || (v->have != 0 && gen < v->gen)) return;
    v->have = 1; v->gen = gen; v->removed = removed != 0 ? 1 : 0;
}
/* what a refusal says is stored: a live record (a seed where one exists), a removal mark (an update over one), no record, or only a
   generation (older / the same generation: whether it is a removal mark stays as known) */
inline void PieceRefuseLearn(int reason, unsigned int storedGen, PieceSrvView* v)
{
    if (v == 0) return;
    if (reason == kPrRefuseSeedExists) { v->have = 1; v->gen = storedGen; v->removed = 0; }
    else if (reason == kPrRefuseOverRemoval) { v->have = 1; v->gen = storedGen; v->removed = 1; }
    else if (reason == kPrRefuseNoRecord) { v->have = 0; v->gen = 0; v->removed = 0; }
    else if (reason == kPrRefuseOlder || reason == kPrRefuseSameGen)
    {
        if (v->have == 0) v->removed = 0;
        if (v->have == 0 || storedGen > v->gen) v->gen = storedGen;
        v->have = 1;
    }
}
/* 1 = an own live piece's next write is a seed: the newest stored record known (the kept copy, or the server view when its
   generation is higher) is a removal mark (a new placement over it), or none is known and this game has sent nothing */
inline int PieceSeedDecide(int haveKept, unsigned int keptGen, int keptRemoved, int haveSrv, unsigned int srvGen, int srvRemoved, int haveSent)
{
    int have = 0, removed = 0;
    if (haveKept != 0) { have = 1; removed = keptRemoved; }
    if (haveSrv != 0 && (have == 0 || srvGen > keptGen)) { have = 1; removed = srvRemoved; }
    if (have == 0) return haveSent != 0 ? 0 : 1;
    return removed != 0 ? 1 : 0;
}
/* 1 = the first look in this world at an own piece writes nothing (only its snapshot): the kept copy is the newest stored record
   known, live, this game's, and its body is the piece's own row byte for byte */
inline int PieceFirstLookSkip(int event, int haveKept, int keptRemoved, int keptNewest, int keptOwnerMine, int sameBody)
{
    return (event == kPeFirst && haveKept != 0 && keptRemoved == 0 && keptNewest != 0 && keptOwnerMine != 0 && sameBody != 0) ? 1 : 0;
}
/* BUILD STOPPED: 1 = an own piece whose progress moved since its last write in this world is written - asked when its area unloads
   here (PieceStopMoment; no clock) */
inline int PieceStoppedDue(int snapHave, float snapProgress, float progress)
{
    return (snapHave != 0 && progress != snapProgress) ? 1 : 0;
}
/* 1 = a write of this event ends 'the build is moving' (the next progress is 'started'); a progress step, a start, a material
   delivery, a helper confirmation mid-build or a write sent again after a link loss does not */
inline int PieceEventEndsMoving(int event)
{
    return (event == kPeProgress || event == kPeStarted || event == kPeMats || event == kPeHelp || event == kPeAgain) ? 0 : 1;
}
/* a write refused as not the writer's (or a seed while another game holds the area) is tried again on a new area map only when this
   game reads itself the writer then (its next change tries it too) */
inline int PieceRetryDue(int newMap, int writerNow) { return (newMap != 0 && writerNow != 0) ? 1 : 0; }
/* the moments 'build stopped' is asked: 1 = written at this moment. The piece's area unloading here is the build stopping; a game
   save is not - the build goes on, and a stop and a start would be written for no change. */
const int kPsmUnload = 0, kPsmSave = 1;
inline int PieceStopMoment(int moment) { return moment == kPsmUnload ? 1 : 0; }
/* another player's piece written here as its writer: only over its kept record when that is live and no removal mark at or above
   its generation is known (a refusal's word) - a write over a removal mark is refused at every hit */
inline int PieceHolderBaseOk(int haveKept, unsigned int keptGen, int keptRemoved, int haveSrv, unsigned int srvGen, int srvRemoved)
{
    if (haveKept == 0 || keptRemoved != 0) return 0;
    return (haveSrv != 0 && srvRemoved != 0 && srvGen >= keptGen) ? 0 : 1;
}

/* ---- the answers to a game's own writes ----
   Each write a game sent and has no answer for is remembered with its generation, whether it is a removal mark, and the stamp it
   was sent with. RECORD_SEQ (taken) carries that stamp back and no generation; PIECE_REFUSE names the generation. One piece's
   answers come in the order its writes were sent, so a write older than the one answered gets no answer (the world server
   answers a repeat of the stored record with nothing) and is dropped with it. A link loss drops them all, with what the game
   knew the server stores: the old link's answers never come, and the next link's feed sends the stored record again. */
struct PieceFly { unsigned int gen; int removed; long long stamp; PieceFly() : gen(0), removed(0), stamp(0) {} };
const size_t kPieceFlyCap = 16u;   /* unanswered writes remembered per piece (the oldest is dropped past it) */
inline void PieceFlyAdd(std::vector<PieceFly>* f, unsigned int gen, int removed, long long stamp)
{
    if (f == 0) return;
    if (f->size() >= kPieceFlyCap) f->erase(f->begin());
    PieceFly e; e.gen = gen; e.removed = removed != 0 ? 1 : 0; e.stamp = stamp;
    f->push_back(e);
}
/* RECORD_SEQ: the oldest unanswered write sent with this stamp is stored (an answer with no stamp: the oldest write); it and every
   older write are dropped. 1 = a write matched; 0 = none (the view is left alone). */
inline int PieceFlyTaken(std::vector<PieceFly>* f, int hasStamp, long long stamp, PieceSrvView* v)
{
    if (f == 0 || f->empty()) return 0;
    size_t k = 0;
    if (hasStamp != 0)
    {
        while (k < f->size() && (*f)[k].stamp != stamp) ++k;
        if (k == f->size()) return 0;
    }
    PieceTakenNote(v, (*f)[k].gen, (*f)[k].removed);
    f->erase(f->begin(), f->begin() + (std::vector<PieceFly>::difference_type)(k + 1));
    return 1;
}
/* PIECE_REFUSE: the refused write (the generation it names) and every older write are dropped. 1 = a write matched. */
inline int PieceFlyRefused(std::vector<PieceFly>* f, unsigned int sentGen)
{
    if (f == 0) return 0;
    size_t k = 0;
    while (k < f->size() && (*f)[k].gen != sentGen) ++k;
    if (k == f->size()) return 0;
    f->erase(f->begin(), f->begin() + (std::vector<PieceFly>::difference_type)(k + 1));
    return 1;
}
/* the world-server link went down or came up: no unanswered write is answered any more, and the stored record is learned again.
   1 = a write was unanswered - it may be lost with the link, so the piece is written again once with its current state (kPeAgain);
   the list is empty afterwards, so the next link change does not mark it again */
inline int PieceFlyLinkLost(std::vector<PieceFly>* f, PieceSrvView* v)
{
    const int lost = (f != 0 && !f->empty()) ? 1 : 0;
    if (f != 0) f->clear();
    if (v != 0) *v = PieceSrvView();
    return lost;
}
/* an own piece: this game is its writer while the piece's area is loaded here; a refused seed may also go while nobody holds the area */
inline int PieceOwnerWriterNow(int areaLoadedHere, int seedWait, int holderSlot)
{
    return (areaLoadedHere != 0 || (seedWait != 0 && holderSlot < 0)) ? 1 : 0;
}

/* ---- fold 3 (T1506): THE ONE WRITER QUESTION, answered alike by the game and the world server ----
   The world server's answer (PieceJudgeWrite -> PieceAcceptRule): a seed - the sender names itself the owner and PieceSeedPlaceOk
   holds (the owner with the area loaded, or the area's holder, or nobody holds the area); an update or a removal mark - PieceMayWrite
   (the owner while the owner's game has the area loaded, else only the area's holder). "Loaded" is the world server's reading: a
   loaded report from that game naming the area within kGraceSec (store_main.cpp PieceLoadedSlots). The game asks the very same rule
   with its own answers to those inputs - for its own piece, the loaded report it SENT (never the engine's active list, which runs
   ahead of the report through a load freeze). */
const double kPieceReportGraceSec = 10.0;   /* store_main.cpp kGraceSec: a game's loaded report counts this long there */
const double kPieceReportMarginSec = 2.0;   /* read this much shorter here: the write reaches the world server after it is sent */
/* 1 = the world server reads this game as having the area loaded: the area was in a loaded report this game sent reportAgeSec ago
   (< 0 = never), inside the server's window less the margin. The report leaves here before the server stamps it, so its age here is
   never below its age there. */
inline int PieceReportLoaded(double reportAgeSec)
{
    return (reportAgeSec >= 0.0 && reportAgeSec <= kPieceReportGraceSec - kPieceReportMarginSec) ? 1 : 0;
}
/* 1 = the game in mySlot is the piece's one writer now, for a write of this kind. ownerSlot: the owner the write names (for an
   update, the stored record's owner); ownerLoaded: the owner's game has the area loaded (for this game's own piece: its own sent
   report, PieceReportLoaded); holderSlot: the area map's holder (-1 nobody or not known). */
inline int PieceWriterRule(int mySlot, int ownerSlot, int seed, int ownerLoaded, int holderSlot)
{
    if (mySlot < 0) return 0;
    if (seed != 0) return (ownerSlot == mySlot && PieceSeedPlaceOk(mySlot, holderSlot, ownerSlot, ownerLoaded) != 0) ? 1 : 0;
    return PieceMayWrite(mySlot, holderSlot, ownerSlot, ownerLoaded);
}
/* THE SEND GATE - every write of a marked piece, the first and any again, asks it. writerNow: PieceWriterRule now; *lastAnswer: the
   answer the last time it was asked (updated here); *edgeWait: a write was refused as not the writer's and the piece has not changed
   since (a new change clears it). kPsgHold: not the writer - the change is held (the piece stays marked; its latest state is what is
   written); kPsgSend: write now; kPsgWaitEdge: refused while this game read itself the writer, and the answer has not turned from no to
   yes since - nothing is sent until it does, or the piece changes again (no write again on a clock or on each area map). */
const int kPsgHold = 0, kPsgSend = 1, kPsgWaitEdge = 2;
inline int PieceSendGate(int writerNow, int* lastAnswer, int* edgeWait)
{
    const int was = *lastAnswer;
    *lastAnswer = writerNow != 0 ? 1 : 0;
    if (writerNow == 0) return kPsgHold;
    if (*edgeWait != 0)
    {
        if (was != 0) return kPsgWaitEdge;
        *edgeWait = 0;   /* the answer turned from no to yes: the held write goes, once */
    }
    return kPsgSend;
}
/* a write refused as not the writer's was not stored: the next write uses its generation again rather than one above it - unless a
   later write was sent meanwhile, or the refused generation is not above the stored one (the next write then goes above that) */
inline void PieceRetryGen(int haveSent, unsigned int* sentGen, unsigned int refusedGen, unsigned int storedGen)
{
    if (haveSent != 0 && *sentGen == refusedGen && refusedGen > storedGen && refusedGen > 0u) *sentGen = refusedGen - 1u;
}
/* ---- fold 4 (review): the inputs mirrored exactly, and turns only from events ---- */
/* no area map on this link: the holder is not known - never read as 'nobody holds it' (PieceSeedPlaceOk's holderSlot < 0) */
const int kPieceHolderUnknown = 0x7FFFFFFF;
/* the owner an update is judged against on the world server is the STORED record's (PieceJudgeWrite's storedOwner): the kept copy's
   when it is the newest stored record this game knows, else this game's own taken write (itself). Not known here: a record another
   game wrote after the newest one this game was fed. */
inline int PieceUpdateOwner(int mySlot, int haveKept, unsigned int keptGen, int keptOwner, int haveSrv, unsigned int srvGen)
{
    if (haveKept != 0 && keptOwner >= 0 && (haveSrv == 0 || keptGen >= srvGen)) return keptOwner;
    return mySlot;
}
/* a refusal as not the writer: the two sides disagreed, so this game's answer is NO from here - the next real turn to yes (read
   from an area map newer than the refusal, PieceConfirmReady) writes it once */
inline void PieceRefusedAsWriter(int* lastAnswer, int* edgeWait) { *lastAnswer = 0; *edgeWait = 1; }
/* after a refusal the answer is confirmed only from an area map that arrived after it AND whose inputs for the piece - the holder
   and the judged owner's loaded bit - differ from those saved at the refusal (fold 5 (re-check)): the same inputs were already
   refused, so only a real change counts (the piece changing, or a write of it taken, ends the wait elsewhere). No counter, no clock. */
inline int PieceConfirmReady(unsigned int mapSeqNow, unsigned int mapSeqAtRefusal, int holderNow, int bitNow, int holderAtRefusal, int bitAtRefusal)
{
    if (mapSeqNow == mapSeqAtRefusal) return 0;
    return (holderNow != holderAtRefusal || bitNow != bitAtRefusal) ? 1 : 0;
}
/* a held (or in-flight) piece of an area is asked again only on an EVENT: a new area map, or a loaded report the world server takes
   that names the area (areaReportSeq: the number of the last report naming it). Time passing is no event. */
inline int PieceHeldRecheckDue(unsigned int mapSeq, unsigned int mapSeqSeen, unsigned int areaReportSeq, unsigned int reportSeqSeen)
{
    return (mapSeq != mapSeqSeen || areaReportSeq > reportSeqSeen) ? 1 : 0;
}
/* a piece stays on its area's watch list while a held change waits for an event, or a write of it is unanswered (its writer answer
   is tracked meanwhile) */
inline int PieceWatchKeep(int marked, int waitEvent, int inFlight) { return ((marked != 0 && waitEvent != 0) || inFlight != 0) ? 1 : 0; }

/* ---- the first records of an existing world ----
   The owner's game seeds a record for each of its own live pieces that has none once the area's record list is complete; a lone game
   writes nothing; no game seeds another player's piece (its copy is the stale view); a removed piece is not seeded. */
const int kPsSeed = 0, kPsLone = 1, kPsNotOwn = 2, kPsRemoved = 3, kPsHasRecord = 4, kPsWait = 5;
inline int PieceSeedChoice(int own, int copy, int removed, int haveKept, int haveSent, int listDone, int lone)
{
    if (lone != 0) return kPsLone;
    if (own == 0 || copy != 0) return kPsNotOwn;
    if (removed != 0) return kPsRemoved;
    if (haveKept != 0 || haveSent != 0) return kPsHasRecord;
    if (listDone == 0) return kPsWait;
    return kPsSeed;
}

}   /* namespace piecerec */
