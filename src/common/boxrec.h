#pragma once
/* src/common/boxrec.h - ONE BOX'S CONTENTS AS ONE WORLD-SERVER RECORD (a storage box, a shop's stock or a cage's prisoner).
 *
 * One record per box, id `box.<sx>.<sy>.#<hash>.<x10>.<z10>`: the box's position key (poskey.h, `<sid>@<sx>,<sy>@<x10>,<z10>,<y10>`)
 * WITHOUT its height part, because the game matches boxes on sector, record, x and z only (items.cpp BoxKeyNearKey). A PLAYER'S BOX
 * (a storage box whose record names an owner slot) has id `box.<sx>.<sy>.#<hash>.<x10>.<z10>.<y10>` - its height included, as a
 * player-built piece's id is (piecerec.h), because a player's chests stack; a player box's key must carry a height. A player record
 * stored under the id without the height (written by protocol 113 games) is read as that box's record (BoxIntakeJudge kBiLegacy)
 * until the box's next write lands under the new id and the old id is removed. Every id has the
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
 *     u32 flags (kBrFlagSeed: built from the writer's own box, not from a stored record; kBrFlagRetired: a bit games of protocol 116
 *     and earlier set, read and ignored, never written; kBrFlagUndos: an undo list follows the body, a shop's record only;
 *     kBrFlagLedger: the box's ledger follows the body and any undo list) | u32 body length
 *   BODY (body length bytes): one PARITY_BOX answer (paritywire.h EncodeParityBox) with status CONTENTS and the header's key -
 *     the item rows with their stolen marks, uniforms, crafters and backpacks' contents. At most kBrMaxRows items.
 *   UNDO LIST (only with kBrFlagUndos): u32 count (1..kBrUndoMax) + count x {u32 asker | u32 epoch | u32 request id}, no key twice -
 *     the cancelled shop trades (shoprec.h) whose undo these contents already hold. It goes into the box's record in the same write
 *     as the undo's change to the box and is carried by every later write of the box until the shop record has the undo stored done,
 *     so a game that loads an older save (the box's stock then comes from this record) never applies the undo a second time.
 *   LEDGER (only with kBrFlagLedger): THE BOX'S LEDGER below ('BJLD' ...), after the undo list when both are there.
 *   Nothing else may follow. A record with neither is byte for byte the form written before either existed. The whole record is at
 *   most kBrMaxBytes.
 *
 * A record that is cut short, carries an unknown marker, format, flag, kind or slot, names a key that is not a position key, holds
 * a body that is not one whole CONTENTS answer for that key or whose digest is not its items' own, has more items than the cap,
 * carries an undo list that is empty, over its cap, names a key twice or is on a record not a shop's, carries a ledger flag with no
 * whole ledger, or has bytes after its body (after its undo list, after its ledger) DECODES TO REFUSED: the caller gets no header and no item - never part of the box. The encoder writes
 * a box whole or not at all (a box missing some of its items would be the loss this record exists to end).
 *
 * THE WORLD SERVER'S RULE (BoxAcceptRule): ONE WRITER at a time, as boxowner.h. While the box's owner player's game has the box's
 * area loaded, only that owner's game may write or remove the box; otherwise only the area's holder may. A SEED is taken only when
 * no record of that id exists, judged against the owner the seed names (so a game may name itself the owner only while its game has
 * the area loaded; otherwise only the holder seeds); a shop or a cage never has an owner, and no write changes a box's kind. Any
 * later write must be an update
 * (not a seed) from the one writer of the stored record (its stored owner is the one tested), with a generation higher than the
 * stored one - an equal generation is taken only as a harmless repeat of the very same bytes, and then nothing is written; only the
 * area's holder may change a record's owner. A removal is taken from the stored record's owner or the area's holder (BoxMayRemove). A refused write
 * is answered with message kMsgBoxRefuse {str id | u32 reason | u32 stored generation | u32 sent generation}; a taken write (and the very
 * same bytes again) with message kMsgBoxTaken {str id | u32 generation} - the writer treats its write as stored only once that arrives.
 *
 * Pure: no engine memory, no Windows, no globals. C++03 (VS2010 v100).
 */

#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "poskey.h"      /* the record name's hash, its hashed form and the sid room */
#include "paritywire.h"  /* the PARITY_BOX item list the record carries as its body */

namespace coopbrec {

const unsigned int kProtocol = 121;            /* the world-server protocol that carries box records and BOX_REFUSE (a record may carry its undo
                                                 list, kBrFlagUndos, and its ledger, kBrFlagLedger) */


const unsigned int kMsgBoxRefuse = 72u;       /* world server -> the writer: its box record was not taken, and why */
const unsigned int kMsgBoxTaken = 80u;        /* world server -> the writer: its box record of that generation is stored */
const unsigned int kMsgBoxAsk = 81u;          /* a game -> world server, after a link loss: for these boxes, my newest stored write? */
const unsigned int kMsgBoxAsked = 82u;        /* world server -> that game: {str id | u32 generation | u32 pending} - its slot's newest accepted write (0 none) */

const unsigned int kBrMarker = 0x31584F42u;   /* 'B','O','X','1' little-endian */
const unsigned int kBrFormat = 1u;
const unsigned int kBrMaxRows = cooppar::kParityMaxItems;   /* 1024 items per box */
const unsigned int kBrMaxBytes = 256u << 10;  /* 256 KB per record */
const unsigned int kBrFlagSeed = 1u;
const unsigned int kBrFlagRetired = 2u;       /* set by games of protocol 116 and earlier on a write after the writer role moved: a
                                                 stored record carrying it still reads; no write carries it */
const unsigned int kBrFlagUndos = 4u;         /* an undo list follows the body (a shop's record only); the encoder sets it from the list */
const unsigned int kBrUndoMax = 64u;          /* undo keys one record carries (twice the shop record's owed cap, shoprec kSrOwedCap) */
const unsigned int kBrFlagLedger = 8u;        /* the record ends with its ledger (THE BOX'S LEDGER below), after any undo list: set by the
                                                 encoder exactly when it is given one */
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
          kBrTrailing = 7, kBrTooBig = 8, kBrBadDigest = 9, kBrBadUndos = 10, kBrDecodeMax = 10;

/* the world server's answers */
const int kBrAccept = 0, kBrRefuseBadId = 1, kBrRefuseUnreadable = 2, kBrRefuseWrongBox = 3, kBrRefuseWriter = 4,
          kBrRefuseSeedExists = 5, kBrRefuseNoRecord = 6, kBrRefuseNotWriter = 7, kBrRefuseOlder = 8, kBrRefusePosition = 9,
          kBrRefuseOwnerChange = 10, kBrRefuseOwnerKind = 11, kBrRefuseSameGen = 12,
          kBrRepeat = 13,                  /* not a refusal: the very same bytes again at the stored generation - nothing is written */
          kBrRefuseKindChange = 14,
          kBrJudgeMax = 14;

/* one cancelled shop trade whose undo a shop's box contents hold: the asker's slot, its process epoch and the request id */
struct BoxUndoKey { unsigned int asker, epoch, id; };
inline int BoxUndoHas(const std::vector<BoxUndoKey>& v, unsigned int asker, unsigned int epoch, unsigned int id)
{
    for (size_t i = 0; i < v.size(); ++i) if (v[i].asker == asker && v[i].epoch == epoch && v[i].id == id) return 1;
    return 0;
}
struct BoxHeader
{
    unsigned int format, flags, generation;
    int writerSlot, kind, ownerSlot;
    std::string key;                 /* the full position key, height included */
    unsigned int bodyBytes;          /* filled by the decoder */
    std::vector<BoxUndoKey> undos;   /* the undo list (kBrFlagUndos), oldest first; empty = none */
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
/* 1 = the id is exactly a spelling MakeBoxRecIdFor makes (the sector, the sid field, x10 and z10 into the outputs that are not 0;
   *haveY 1 and *y10 the height when the id carries one - a player's box); 0 = not a box record id (outputs untouched). The sid field
   must be the hashed form, '#' + exactly 8 lower-case hex digits (it holds no '.'). */
inline int BoxRecIdParse(const std::string& id, int* sx, int* sy, std::string* sid, long* x10, long* z10, long* y10 = 0, int* haveY = 0)
{
    if (id.size() > kBrIdMax || id.compare(0, 4, "box.") != 0) return 0;
    size_t at = 4;
    long a = 0, b = 0;
    if (BrParseLong(id, &at, id.size(), 1, &a) == 0 || at >= id.size() || id[at] != '.') return 0;
    ++at;
    if (BrParseLong(id, &at, id.size(), 1, &b) == 0 || at >= id.size() || id[at] != '.') return 0;
    ++at;
    if (a < 0 || a > kBrMaxSector || b < 0 || b > kBrMaxSector) return 0;
    const size_t dx = id.find('.', at);
    if (dx == std::string::npos || dx <= at) return 0;
    const std::string s = id.substr(at, dx - at);
    if (poskey::IsHashedSidField(s.c_str()) == 0) return 0;
    long x = 0, z = 0, y = 0;
    int hy = 0;
    size_t p = dx + 1;
    if (BrParseLong(id, &p, id.size(), 1, &x) == 0 || p >= id.size() || id[p] != '.') return 0;
    ++p;
    if (BrParseLong(id, &p, id.size(), 1, &z) == 0) return 0;
    if (p < id.size())
    {
        if (id[p] != '.') return 0;
        ++p;
        if (BrParseLong(id, &p, id.size(), 1, &y) == 0 || p != id.size()) return 0;
        hy = 1;
    }
    if (sx) *sx = (int)a;
    if (sy) *sy = (int)b;
    if (sid) *sid = s;
    if (x10) *x10 = x;
    if (z10) *z10 = z;
    if (y10) *y10 = y;
    if (haveY) *haveY = hy;
    return 1;
}

/* The record id of the box a position key names, its record name always in the hashed form (kept as it is when the key already
   carries that form, so both texts of one key give one id). withHeight 0: the height left out (a box no player owns); 1: the height
   included (a player's box) - "" when the key carries no height. "" when the key is not a position key. */
inline std::string MakeBoxRecIdFor(const std::string& fullKey, int withHeight)
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
    if (withHeight != 0 && k.haveY == 0) return std::string();
    std::string id = BoxRecIdOf(field, k.sx, k.sy, k.x10, k.z10);
    if (withHeight != 0) id += "." + BrLongText(k.y10);
    return BoxRecIdParse(id, 0, 0, 0, 0, 0) != 0 ? id : std::string();
}
/* the id of a box no player owns (no height) */
inline std::string MakeBoxRecId(const std::string& fullKey) { return MakeBoxRecIdFor(fullKey, 0); }
/* 1 = a player's box: a storage box whose record names an owner slot */
inline int BoxIsPlayerRec(int kind, int ownerSlot) { return (kind == kBrKindBox && ownerSlot >= 0) ? 1 : 0; }
/* ---- validity ---- */
inline bool BrSlotOk(int slot) { return slot >= 0 && slot <= kBrMaxSlot; }
inline bool BoxHeaderValid(const BoxHeader& h)
{
    return h.format == kBrFormat && (h.flags & ~(kBrFlagSeed | kBrFlagRetired | kBrFlagUndos | kBrFlagLedger)) == 0 && BrSlotOk(h.writerSlot)
        && (h.ownerSlot == -1 || BrSlotOk(h.ownerSlot)) && h.kind >= 0 && h.kind <= kBrKindMax && ((h.flags & kBrFlagUndos) == 0 || h.kind == kBrKindShop)
        && !MakeBoxRecIdFor(h.key, BoxIsPlayerRec(h.kind, h.ownerSlot)).empty();
}
/* an undo list a record may carry: at most kBrUndoMax keys, none twice, and only on a shop's record */
inline bool BoxUndoListValid(const std::vector<BoxUndoKey>& v, int kind)
{
    if (v.empty()) return true;
    if (v.size() > kBrUndoMax || kind != kBrKindShop) return false;
    for (size_t i = 0; i < v.size(); ++i)
        for (size_t j = 0; j < i; ++j) if (v[j].asker == v[i].asker && v[j].epoch == v[i].epoch && v[j].id == v[i].id) return false;
    return true;
}
/* the id a record's header names: with the height for a player's box, without it otherwise */
inline std::string BoxRecIdOfHeader(const BoxHeader& h) { return MakeBoxRecIdFor(h.key, BoxIsPlayerRec(h.kind, h.ownerSlot)); }
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

/* ---- THE BOX'S LEDGER: WHICH CHANGES A RECORD HOLDS (kBrFlagLedger, after the body) ----
   Every change a game makes to a player's box has an id from the moment it is journalled, before any write carries it: a write carries
   the id of the change it writes (reserved at the box's first mark after this game's previous write or save; a save seals the reserved
   id with the box as it stood - BoxSeal: a change after the save takes a new id, and a later write carries the sealed id exactly only
   when it writes the box as it stood at that save), and the writer that serves another game's journalled ask records that ask's id in
   the very write that carries the change. A record's ledger is the ledger of the record it was written on with the writer's own entries laid
   on top, so a box's line of records carries every id that reached it, per slot (the slot that made the id): the newest kBlMaxPerSlot of
   each slot - an older id reads below the slot's floor. A write built on a record whose ledger this game does not have starts a new
   line, carrying that record's ids (if known) only as doubt; a seed starts a new line. The load of a save asks, for each change it holds
   unresolved: is the change's id in the stored record's ledger (stored), there as doubt (unsure), or absent from the very line the save
   named and above the slot's floor (not stored)?
   marker 'BJLD' | u64 line | u32 flags (kBlFlagCapped: a slot was left out at the slot cap - an absent id reads unsure) | u32 slots |
   per slot, ascending: u32 slot | u64 floor | u32 n | n x u64 id (ascending, each above the floor; the top bit marks doubt). */
const unsigned int kBlMarker = 0x444C4A42u;   /* 'BJLD' */
const unsigned int kBlMaxPerSlot = 64u, kBlMaxSlots = 256u;
const unsigned int kBlFlagCapped = 1u;
const unsigned long long kBlDoubt = 0x8000000000000000ULL;
const int kBlAbsent = 0, kBlExact = 1, kBlDoubtful = 2;
inline unsigned long long BlId(unsigned long long e) { return e & ~kBlDoubt; }
struct BoxLedgerSlot { int slot; unsigned long long floor; std::vector<unsigned long long> ids; BoxLedgerSlot() : slot(-1), floor(0ULL) {} };
struct BoxLedger { unsigned long long line; unsigned int flags; std::vector<BoxLedgerSlot> slots; BoxLedger() : line(0ULL), flags(0u) {} };
/* one id of a slot (its top bit: doubt) - a writer's entries not written yet, an ask's carried entries */
struct BoxLedgerEntry
{
    int slot; unsigned long long id;
    BoxLedgerEntry() : slot(-1), id(0ULL) {}
    BoxLedgerEntry(int s, unsigned long long i) : slot(s), id(i) {}
};
/* a new id: above every earlier one of this game (*last), from the wall clock (nowMs) so a later session's ids stay above an earlier
   session's */
inline unsigned long long BoxChangeIdNext(unsigned long long* last, unsigned long long nowMs)
{
    unsigned long long id = (nowMs << 16) & ~kBlDoubt;
    if (id <= *last) id = *last + 1ULL;
    *last = id;
    return id & ~kBlDoubt;
}
inline const BoxLedgerSlot* BoxLedgerSlotOf(const BoxLedger& l, int slot)
{
    for (size_t i = 0; i < l.slots.size(); ++i) if (l.slots[i].slot == slot) return &l.slots[i];
    return 0;
}
inline unsigned long long BoxLedgerFloor(const BoxLedger& l, int slot)
{
    const BoxLedgerSlot* s = BoxLedgerSlotOf(l, slot);
    return s != 0 ? s->floor : 0ULL;
}
/* kBlExact / kBlDoubtful / kBlAbsent */
inline int BoxLedgerFind(const BoxLedger& l, int slot, unsigned long long id)
{
    const BoxLedgerSlot* s = BoxLedgerSlotOf(l, slot);
    if (s == 0 || BlId(id) == 0ULL) return kBlAbsent;
    for (size_t i = 0; i < s->ids.size(); ++i)
        if (BlId(s->ids[i]) == BlId(id)) return (s->ids[i] & kBlDoubt) != 0 ? kBlDoubtful : kBlExact;
    return kBlAbsent;
}
/* e into the ledger (its top bit: doubt). Already there: exact when either is. At or below the slot's floor: nothing (it reads below the
   floor already). Over kBlMaxPerSlot: the oldest leave and the floor rises to them. A new slot past kBlMaxSlots is left out (flags). */
inline void BoxLedgerAdd(BoxLedger* l, int slot, unsigned long long e)
{
    const unsigned long long id = BlId(e);
    if (l == 0 || !BrSlotOk(slot) || id == 0ULL) return;
    size_t k = 0;
    while (k < l->slots.size() && l->slots[k].slot < slot) ++k;
    if (k == l->slots.size() || l->slots[k].slot != slot)
    {
        if (l->slots.size() >= kBlMaxSlots) { l->flags |= kBlFlagCapped; return; }
        BoxLedgerSlot ns;
        ns.slot = slot;
        l->slots.insert(l->slots.begin() + (long)k, ns);
    }
    BoxLedgerSlot& s = l->slots[k];
    if (id <= s.floor) return;
    size_t at = 0;
    while (at < s.ids.size() && BlId(s.ids[at]) < id) ++at;
    if (at < s.ids.size() && BlId(s.ids[at]) == id) { if ((e & kBlDoubt) == 0) s.ids[at] = id; return; }
    s.ids.insert(s.ids.begin() + (long)at, e);
    while (s.ids.size() > kBlMaxPerSlot)
    {
        if (BlId(s.ids[0]) > s.floor) s.floor = BlId(s.ids[0]);
        s.ids.erase(s.ids.begin());
    }
}
/* the id leaves the ledger (an ask served and then put back): it reads absent; the floor is unchanged */
inline void BoxLedgerDel(BoxLedger* l, int slot, unsigned long long id)
{
    if (l == 0) return;
    for (size_t k = 0; k < l->slots.size(); ++k)
    {
        if (l->slots[k].slot != slot) continue;
        std::vector<unsigned long long>& v = l->slots[k].ids;
        for (size_t i = 0; i < v.size(); ++i) if (BlId(v[i]) == BlId(id)) { v.erase(v.begin() + (long)i); return; }
        return;
    }
}
/* every id of the ledger as doubt */
inline void BoxLedgerAllDoubt(BoxLedger* l)
{
    for (size_t k = 0; k < l->slots.size(); ++k)
        for (size_t i = 0; i < l->slots[k].ids.size(); ++i) l->slots[k].ids[i] |= kBlDoubt;
}
/* THE LEDGER A WRITE CARRIES. baseHave: the ledger of the record of the generation just below the write is known (*base, line not 0) -
   the write is that ledger with the writer's entries on top; otherwise (a seed, or a base this game does not have) a new line (newLine)
   carrying the newest kept record's ledger (keptHave / kept) as doubt. Then the writer's removals (del) and additions (add). */
inline void BoxLedgerForWrite(int baseHave, const BoxLedger& base, int keptHave, const BoxLedger& kept, unsigned long long newLine,
                              const std::vector<BoxLedgerEntry>& add, const std::vector<BoxLedgerEntry>& del, BoxLedger* out)
{
    if (out == 0) return;
    if (baseHave != 0 && base.line != 0ULL) *out = base;
    else
    {
        *out = (keptHave != 0 && kept.line != 0ULL) ? kept : BoxLedger();
        BoxLedgerAllDoubt(out);
        out->line = newLine;
    }
    for (size_t i = 0; i < del.size(); ++i) BoxLedgerDel(out, del[i].slot, del[i].id);
    for (size_t i = 0; i < add.size(); ++i) BoxLedgerAdd(out, add[i].slot, add[i].id);
}
/* e into a list of entries (exact when either is). 1 = added */
inline int BoxLedgerEntrySet(std::vector<BoxLedgerEntry>* v, int slot, unsigned long long e)
{
    if (v == 0 || slot < 0 || BlId(e) == 0ULL) return 0;
    for (size_t i = 0; i < v->size(); ++i)
        if ((*v)[i].slot == slot && BlId((*v)[i].id) == BlId(e)) { if ((e & kBlDoubt) == 0) (*v)[i].id = BlId(e); return 0; }
    v->push_back(BoxLedgerEntry(slot, e));
    return 1;
}
/* 1 = the entry was in the list (it leaves) */
inline int BoxLedgerEntryDrop(std::vector<BoxLedgerEntry>* v, int slot, unsigned long long id)
{
    if (v == 0) return 0;
    for (size_t i = 0; i < v->size(); ++i)
        if ((*v)[i].slot == slot && BlId((*v)[i].id) == BlId(id)) { v->erase(v->begin() + (long)i); return 1; }
    return 0;
}
inline int BoxLedgerEntryHas(const std::vector<BoxLedgerEntry>& v, int slot, unsigned long long id)
{
    for (size_t i = 0; i < v.size(); ++i) if (v[i].slot == slot && BlId(v[i].id) == BlId(id)) return 1;
    return 0;
}
/* the entries of a list marked doubt (a change laid again with a part in conflict: its ids no longer say all of it is held) */
inline void BoxLedgerEntryDoubt(std::vector<BoxLedgerEntry>* v, const std::vector<BoxLedgerEntry>& which)
{
    if (v == 0) return;
    for (size_t i = 0; i < v->size(); ++i)
        if (BoxLedgerEntryHas(which, (*v)[i].slot, (*v)[i].id) != 0) (*v)[i].id |= kBlDoubt;
}
/* a kept record's ledger carries an entry as strongly (exact there, or doubt both): it leaves the writer's additions - every write built
   on that record carries it; a removal the record no longer carries leaves the removals once no write of this game waits for its answer
   (writesWaiting 0) - a write still waiting may hold the served ask, and the record its answer makes would carry it again */
inline void BoxLedgerPrune(std::vector<BoxLedgerEntry>* add, std::vector<BoxLedgerEntry>* del, const BoxLedger& kept, int writesWaiting = 0)
{
    if (kept.line == 0ULL) return;
    if (add != 0)
    {
        size_t k = 0;
        for (size_t i = 0; i < add->size(); ++i)
        {
            const int f = BoxLedgerFind(kept, (*add)[i].slot, (*add)[i].id);
            if (f == kBlExact || (f == kBlDoubtful && ((*add)[i].id & kBlDoubt) != 0)) continue;
            if (k != i) (*add)[k] = (*add)[i];
            ++k;
        }
        add->resize(k);
    }
    if (del != 0)
    {
        size_t k = 0;
        for (size_t i = 0; i < del->size(); ++i)
        {
            if (writesWaiting == 0 && BoxLedgerFind(kept, (*del)[i].slot, (*del)[i].id) == kBlAbsent) continue;
            if (k != i) (*del)[k] = (*del)[i];
            ++k;
        }
        del->resize(k);
    }
}
inline void BrPutU64(std::vector<char>* b, unsigned long long v) { BrPutU32(b, (unsigned int)(v & 0xFFFFFFFFULL)); BrPutU32(b, (unsigned int)(v >> 32)); }
inline bool BrGet8(const char* p, size_t size, size_t* off, unsigned long long* v)
{
    unsigned int lo = 0, hi = 0;
    if (!BrGet4(p, size, off, &lo) || !BrGet4(p, size, off, &hi)) return false;
    *v = ((unsigned long long)hi << 32) | (unsigned long long)lo;
    return true;
}
/* false = not encodable (no line, a slot or an id count out of range) */
inline bool BoxLedgerPut(std::vector<char>* b, const BoxLedger& l)
{
    if (b == 0 || l.line == 0ULL || l.slots.size() > kBlMaxSlots) return false;
    std::vector<char> t;
    BrPutU32(&t, kBlMarker); BrPutU64(&t, l.line); BrPutU32(&t, l.flags & kBlFlagCapped); BrPutU32(&t, (unsigned int)l.slots.size());
    for (size_t k = 0; k < l.slots.size(); ++k)
    {
        const BoxLedgerSlot& s = l.slots[k];
        if (!BrSlotOk(s.slot) || s.ids.size() > kBlMaxPerSlot) return false;
        BrPutU32(&t, (unsigned int)s.slot); BrPutU64(&t, s.floor); BrPutU32(&t, (unsigned int)s.ids.size());
        for (size_t i = 0; i < s.ids.size(); ++i) BrPutU64(&t, s.ids[i]);
    }
    b->insert(b->end(), t.begin(), t.end());
    return true;
}
/* true = *out holds the ledger at *off (moved past it); false = not one whole ledger in range (nothing kept) */
inline bool BoxLedgerGet(const char* p, size_t size, size_t* off, BoxLedger* out)
{
    BoxLedger l;
    unsigned int mk = 0, fl = 0, ns = 0;
    if (p == 0 || !BrGet4(p, size, off, &mk) || mk != kBlMarker || !BrGet8(p, size, off, &l.line) || l.line == 0ULL || !BrGet4(p, size, off, &fl)
        || (fl & ~kBlFlagCapped) != 0 || !BrGet4(p, size, off, &ns) || ns > kBlMaxSlots) return false;
    l.flags = fl;
    int prev = -1;
    for (unsigned int k = 0; k < ns; ++k)
    {
        BoxLedgerSlot s;
        unsigned int sl = 0, n = 0;
        if (!BrGet4(p, size, off, &sl) || sl > (unsigned int)kBrMaxSlot || (int)sl <= prev || !BrGet8(p, size, off, &s.floor) || !BrGet4(p, size, off, &n)
            || n > kBlMaxPerSlot) return false;
        s.slot = (int)sl; prev = s.slot;
        unsigned long long last = BlId(s.floor);
        for (unsigned int i = 0; i < n; ++i)
        {
            unsigned long long e = 0ULL;
            if (!BrGet8(p, size, off, &e) || BlId(e) <= last) return false;
            last = BlId(e);
            s.ids.push_back(e);
        }
        l.slots.push_back(s);
    }
    if (out != 0) *out = l;
    return true;
}
/* THE ASK'S ID ON THE ITEM REQUEST (game protocol 180). Every ITEM_REQUEST ends with an 'ASK1' trailer, after every other block (a HOLD /
   LAND's 'HLD1' included): n x (u32 slot | u64 id) | u32 n | u32 slot | u64 ask id | u32 'ASK1' - 20 + 12n bytes. ask id 0: not a
   journalled ask (nothing is recorded); slot: the asker's, the ledger slot the id is recorded under; the n entries (at most
   kAskCarryMax): other games' asks the asker had served into its own change of the box and not written when that change was handed
   over - the writer records them as doubt. */
const unsigned int kAskTailMarker = 0x314B5341u;   /* 'ASK1' */
const size_t kAskTailFixed = 20u;
const unsigned int kAskCarryMax = 32u;
struct AskTail { unsigned long long askId; int slot; std::vector<BoxLedgerEntry> carry; AskTail() : askId(0ULL), slot(-1) {} };
inline void PutAskTail(std::vector<char>* b, const AskTail& t)
{
    const size_t n = t.carry.size() < (size_t)kAskCarryMax ? t.carry.size() : (size_t)kAskCarryMax;
    for (size_t i = 0; i < n; ++i) { BrPutU32(b, (unsigned int)t.carry[i].slot); BrPutU64(b, t.carry[i].id); }
    BrPutU32(b, (unsigned int)n); BrPutU32(b, (unsigned int)t.slot); BrPutU64(b, t.askId); BrPutU32(b, kAskTailMarker);
}
/* the trailer at the end of a message of size bytes. 1 = read (*len: its bytes, to cut off the end); 0 = none there, or not whole */
inline int TakeAskTail(const char* p, size_t size, AskTail* t, size_t* len)
{
    if (p == 0 || size < kAskTailFixed) return 0;
    size_t off = size - kAskTailFixed;
    unsigned int n = 0, sl = 0, mk = 0;
    unsigned long long id = 0ULL;
    if (!BrGet4(p, size, &off, &n) || !BrGet4(p, size, &off, &sl) || !BrGet8(p, size, &off, &id) || !BrGet4(p, size, &off, &mk) || mk != kAskTailMarker)
        return 0;
    if (n > kAskCarryMax || size - kAskTailFixed < (size_t)n * 12u) return 0;
    AskTail r;
    r.askId = id; r.slot = (int)sl;
    off = size - kAskTailFixed - (size_t)n * 12u;
    for (unsigned int i = 0; i < n; ++i)
    {
        unsigned int cs = 0;
        unsigned long long ci = 0ULL;
        if (!BrGet4(p, size, &off, &cs) || !BrGet8(p, size, &off, &ci)) return 0;
        r.carry.push_back(BoxLedgerEntry((int)cs, ci));
    }
    if (t != 0) *t = r;
    if (len != 0) *len = kAskTailFixed + (size_t)n * 12u;
    return 1;
}

/* The record for header `h` (its flags, generation, slots, kind, key and undo list - kBrFlagUndos is set from the list) and `items`,
   into *out (replaced). false = nothing written (*out emptied): the header is out of range, an item is not valid, there are more than
   kBrMaxRows items, the item list will not encode as a PARITY_BOX answer, the undo list is not one a record may carry, or the record
   would pass kBrMaxBytes. led (may be 0): the record's ledger - the ledger flag is set exactly when one with a line is given. After
   the body: the undo list, then the ledger. */
inline bool EncodeBoxRecord(std::vector<char>* out, const BoxHeader& h, const std::vector<cooppar::ParityItem>& items, const BoxLedger* led = 0)
{
    if (out == 0) return false;
    out->clear();
    BoxHeader hh = h;
    hh.format = kBrFormat;
    hh.flags &= ~(kBrFlagLedger | kBrFlagUndos);
    if (led != 0 && led->line != 0ULL) hh.flags |= kBrFlagLedger;
    if (!hh.undos.empty()) hh.flags |= kBrFlagUndos;
    if ((hh.flags & kBrFlagRetired) != 0 || !BoxHeaderValid(hh) || !BoxItemsValid(items) || !BoxUndoListValid(hh.undos, hh.kind)) return false;
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
    std::vector<char> lb;
    if ((hh.flags & kBrFlagLedger) != 0 && !BoxLedgerPut(&lb, *led)) return false;
    if (b.size() + body.size() + lb.size() > (size_t)kBrMaxBytes) return false;
    b.insert(b.end(), body.begin(), body.end());
    if (!hh.undos.empty())
    {
        BrPutU32(&b, (unsigned int)hh.undos.size());
        for (size_t i = 0; i < hh.undos.size(); ++i) { BrPutU32(&b, hh.undos[i].asker); BrPutU32(&b, hh.undos[i].epoch); BrPutU32(&b, hh.undos[i].id); }
    }
    b.insert(b.end(), lb.begin(), lb.end());
    if (b.size() > (size_t)kBrMaxBytes) return false;
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

/* the undo list at *off (kBrFlagUndos): kBrOk with *out filled and *off past it; kBrShort cut short; kBrBadUndos empty, over the cap
   or a key twice */
inline int BoxUndoListRead(const char* p, size_t size, size_t* off, std::vector<BoxUndoKey>* out)
{
    out->clear();
    unsigned int n = 0;
    if (!BrGet4(p, size, off, &n)) return kBrShort;
    if (n == 0 || n > kBrUndoMax) return kBrBadUndos;
    if (size - *off < (size_t)n * 12u) return kBrShort;
    for (unsigned int i = 0; i < n; ++i)
    {
        BoxUndoKey k;
        BrGet4(p, size, off, &k.asker); BrGet4(p, size, off, &k.epoch); BrGet4(p, size, off, &k.id);
        if (BoxUndoHas(*out, k.asker, k.epoch, k.id) != 0) return kBrBadUndos;
        out->push_back(k);
    }
    return kBrOk;
}
/* The whole record. kBrOk: *hOut and *itemsOut hold it (*ledOut, may be 0: its ledger - line 0 when it carries none); anything else:
   refused, and no output is touched. After the body: the undo list (kBrFlagUndos), then one whole ledger (kBrFlagLedger), then
   nothing. */
inline int DecodeBoxRecord(const char* p, size_t size, BoxHeader* hOut, std::vector<cooppar::ParityItem>* itemsOut, BoxLedger* ledOut = 0)
{
    if (p == 0) return kBrShort;
    if (size > (size_t)kBrMaxBytes) return kBrTooBig;
    BoxHeader h;
    const int hr = DecodeBoxHeader(p, size, &h);
    if (hr != kBrOk) return hr;
    const size_t off = BoxHeaderBytes(h);
    if (size - off < (size_t)h.bodyBytes) return kBrShort;
    BoxLedger led;
    {
        size_t lo = off + (size_t)h.bodyBytes;
        if ((h.flags & kBrFlagUndos) != 0)
        {
            const int ur = BoxUndoListRead(p, size, &lo, &h.undos);
            if (ur != kBrOk) return ur;
        }
        if ((h.flags & kBrFlagLedger) != 0 && !BoxLedgerGet(p, size, &lo, &led)) return kBrTrailing;
        if (lo != size) return kBrTrailing;
    }
    if (h.bodyBytes == 0) return kBrBadBody;
    cooppar::ParityBox m;
    const int br = cooppar::DecodeParityBox(p + off, (size_t)h.bodyBytes, &m);
    if (br == cooppar::kParityDecodeTooMany && p[off] == (char)cooppar::kParityStatusContents) return kBrOverCap;
    if (br != cooppar::kParityDecodeOk) return kBrBadBody;
    if (m.status != cooppar::kParityStatusContents || m.key != h.key || !BoxItemsValid(m.items)) return kBrBadBody;
    if (m.digest != cooppar::ParityDigestOf(m.items)) return kBrBadDigest;   /* the body's digest must be its own items' */
    if (hOut) *hOut = h;
    if (itemsOut) itemsOut->swap(m.items);
    if (ledOut) *ledOut = led;
    return kBrOk;
}
inline const char* BoxDecodeWhy(int why)
{
    return why == kBrOk ? "ok" : why == kBrShort ? "cut short" : why == kBrBadMarker ? "not a box record (wrong marker)"
         : why == kBrBadFormat ? "an unknown format" : why == kBrBadHeader ? "a header out of range" : why == kBrOverCap ? "more items than the cap"
         : why == kBrBadBody ? "a body that is not one whole item list for its key" : why == kBrTrailing ? "bytes after the body (or a ledger that does not read whole)"
         : why == kBrTooBig ? "larger than the byte cap" : why == kBrBadDigest ? "a body whose digest is not its items' own"
         : why == kBrBadUndos ? "an undo list that is empty, over its cap or names a key twice" : "malformed";
}

/* THE UNDO LIST A WRITE OF A SHOP'S BOX CARRIES: the kept record's list, then the keys this game's own undos put into this box that
   it does not hold, less the keys in `done` (the shop record has their undo stored done, or no longer carries the trade), oldest
   first; past kBrUndoMax the oldest go (*dropped counts them). */
inline void BoxUndoCarry(const std::vector<BoxUndoKey>& kept, const std::vector<BoxUndoKey>& local, const std::vector<BoxUndoKey>& done,
                         std::vector<BoxUndoKey>* out, unsigned int* dropped)
{
    out->clear();
    if (dropped != 0) *dropped = 0;
    for (int pass = 0; pass < 2; ++pass)
    {
        const std::vector<BoxUndoKey>& src = (pass == 0) ? kept : local;
        for (size_t i = 0; i < src.size(); ++i)
            if (BoxUndoHas(done, src[i].asker, src[i].epoch, src[i].id) == 0 && BoxUndoHas(*out, src[i].asker, src[i].epoch, src[i].id) == 0)
                out->push_back(src[i]);
    }
    while (out->size() > kBrUndoMax) { out->erase(out->begin()); if (dropped != 0) ++*dropped; }
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
    if (BoxRecIdOfHeader(h) != id) return kBrRefuseWrongBox;
    if (senderSlot < 0 || h.writerSlot != senderSlot) return kBrRefuseWriter;
    return BoxAcceptRule(exists, storedGen, storedOwner, BrSlotIn(loadedSlots, storedOwner), sameBytes, (h.flags & kBrFlagSeed) != 0 ? 1 : 0,
                         h.generation, h.ownerSlot, BrSlotIn(loadedSlots, h.ownerSlot), h.kind, senderSlot, holderSlot, storedKind);
}
/* A removal judged: the id must parse and the sender be the stored record's owner or the area's holder - whichever of the two is the
   writer now. A removal is sent only when the box's piece is taken down (by its owner's game, perhaps queued while the link was down
   and sent after the writer role moved), when a box's player record is replaced by its record under another id (by the box's writer:
   the area's holder once the box reads as nobody's, while the old owner's game may still have the area loaded), or when a player
   record without the height is replaced: none of these is retried, so the two games that may write the box are both taken. */
inline int BoxMayRemove(int senderSlot, int holderSlot, int ownerSlot, int ownerLoaded)
{
    if (BoxMayWrite(senderSlot, holderSlot, ownerSlot, ownerLoaded) != 0) return 1;
    if (senderSlot < 0) return 0;
    return ((ownerSlot >= 0 && senderSlot == ownerSlot) || (holderSlot >= 0 && senderSlot == holderSlot)) ? 1 : 0;
}
inline int BoxJudgeRemove(const std::string& id, int senderSlot, int holderSlot, int storedOwner, const std::vector<int>& loadedSlots)
{
    if (BoxRecIdParse(id, 0, 0, 0, 0, 0) == 0) return kBrRefuseBadId;
    return BoxMayRemove(senderSlot, holderSlot, storedOwner, BrSlotIn(loadedSlots, storedOwner)) != 0 ? kBrAccept : kBrRefuseNotWriter;
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
/* ---- BOX_TAKEN (kMsgBoxTaken): {str id | u32 generation} - sent to the writer alone once a write the rule took (kBrAccept) is on the
   disk, and at once for the very same bytes again (kBrRepeat: that generation is stored already); never for a refused write ---- */
inline int BoxTakenNoteDue(int judge) { return (judge == kBrAccept || judge == kBrRepeat) ? 1 : 0; }
inline std::vector<char> EncodeBoxTaken(const std::string& id, unsigned int gen)
{
    std::vector<char> b;
    BrPutStr(&b, id.size() > kBrIdMax ? id.substr(0, kBrIdMax) : id);
    BrPutU32(&b, gen);
    return b;
}
/* 1 = read whole (nothing after it); 0 = refused, outputs untouched */
inline int DecodeBoxTaken(const std::vector<char>& b, std::string* id, unsigned int* gen)
{
    if (b.empty()) return 0;
    const char* p = &b[0];
    size_t off = 0;
    std::string s;
    unsigned int g = 0;
    if (!BrGetStr(p, b.size(), &off, kBrIdMax, &s) || !BrGet4(p, b.size(), &off, &g) || off != b.size()) return 0;
    if (id) *id = s;
    if (gen) *gen = g;
    return 1;
}

/* ---- BOX_ASKED (kMsgBoxAsked): {str id | u32 generation | u32 pending (1 = writes of the box were still queued on the world server)} */
inline std::vector<char> EncodeBoxAsked(const std::string& id, unsigned int gen, int pending)
{
    std::vector<char> b = EncodeBoxTaken(id, gen);
    BrPutU32(&b, pending != 0 ? 1u : 0u);
    return b;
}
/* 1 = read whole; 0 = refused (cut short, a pending other than 0 / 1, bytes after it), outputs untouched */
inline int DecodeBoxAsked(const std::vector<char>& b, std::string* id, unsigned int* gen, int* pending)
{
    if (b.size() < 4) return 0;
    unsigned int pd = 0;
    std::memcpy(&pd, &b[b.size() - 4], 4);
    if (pd > 1u) return 0;
    const std::vector<char> head(b.begin(), b.end() - 4);
    std::string s; unsigned int g = 0;
    if (DecodeBoxTaken(head, &s, &g) == 0) return 0;
    if (id) *id = s;
    if (gen) *gen = g;
    if (pending) *pending = (int)pd;
    return 1;
}
/* ---- BOX_ASK (kMsgBoxAsk): {u32 n | n x str id}, at most kBoxAskMaxIds ids ---- */
const size_t kBoxAskMaxIds = 256u;
inline std::vector<char> EncodeBoxAsk(const std::vector<std::string>& ids)
{
    std::vector<char> b;
    const size_t n = ids.size() > kBoxAskMaxIds ? kBoxAskMaxIds : ids.size();
    BrPutU32(&b, (unsigned int)n);
    for (size_t i = 0; i < n; ++i) BrPutStr(&b, ids[i].size() > kBrIdMax ? ids[i].substr(0, kBrIdMax) : ids[i]);
    return b;
}
/* 1 = read whole; 0 = refused (cut short, too many ids, bytes after it), *ids untouched */
inline int DecodeBoxAsk(const std::vector<char>& b, std::vector<std::string>* ids)
{
    if (b.empty()) return 0;
    const char* p = &b[0];
    size_t off = 0;
    unsigned int n = 0;
    if (!BrGet4(p, b.size(), &off, &n) || n > kBoxAskMaxIds) return 0;
    std::vector<std::string> v;
    for (unsigned int i = 0; i < n; ++i)
    {
        std::string s;
        if (!BrGetStr(p, b.size(), &off, kBrIdMax, &s)) return 0;
        v.push_back(s);
    }
    if (off != b.size()) return 0;
    if (ids) ids->swap(v);
    return 1;
}
/* THE WORLD SERVER'S MEMORY OF EACH WRITER'S STORED WRITES: per box record, each writer slot's newest accepted generation (kept beside the
   record's file, so a restart keeps it). A seed starts the record's line again (the record did not exist): the slots before it go. */
inline void BoxSlotGenNote(std::map<int, unsigned int>* m, int slot, unsigned int gen, int seed)
{
    if (m == 0) return;
    if (seed != 0) m->clear();
    if (slot < 0) return;
    std::map<int, unsigned int>::iterator it = m->find(slot);
    if (it == m->end()) (*m)[slot] = gen; else if (gen > it->second) it->second = gen;
}
inline unsigned int BoxSlotGenOf(const std::map<int, unsigned int>& m, int slot)
{
    std::map<int, unsigned int>::const_iterator it = m.find(slot);
    return it == m.end() ? 0u : it->second;
}
/* the text kept beside the record: one "slot generation" line per slot */
inline std::string EncodeSlotGens(const std::map<int, unsigned int>& m)
{
    std::string t;
    for (std::map<int, unsigned int>::const_iterator it = m.begin(); it != m.end(); ++it)
    { char b[48]; std::sprintf(b, "%d %u\n", it->first, it->second); t += b; }
    return t;
}
/* 1 = every line read (an empty text is no slot); 0 = a line that is not "slot generation" - *m untouched */
inline int DecodeSlotGens(const std::string& t, std::map<int, unsigned int>* m)
{
    std::map<int, unsigned int> r;
    size_t at = 0;
    while (at < t.size())
    {
        size_t e = t.find('\n', at);
        if (e == std::string::npos) e = t.size();
        const std::string line = t.substr(at, e - at);
        at = e + 1;
        if (line.empty()) continue;
        size_t p = 0;
        long a = 0, g = 0;
        if (BrParseLong(line, &p, line.size(), 0, &a) == 0 || p >= line.size() || line[p] != ' ') return 0;
        ++p;
        if (BrParseLong(line, &p, line.size(), 0, &g) == 0 || p != line.size() || a < 0 || a > kBrMaxSlot || g < 0) return 0;
        r[(int)a] = (unsigned int)g;
    }
    if (m) m->swap(r);
    return 1;
}
/* BOX_ASKED's answer g (this game's newest stored write of the box) against its writes the link carried and no answer came for
   (chain): the newest of them stored (0 = none) - every one is answered by it (those above g were not stored) */
inline unsigned int BoxAskedStored(const std::vector<unsigned int>& chain, unsigned int g)
{
    unsigned int best = 0;
    for (size_t i = 0; i < chain.size(); ++i) if (chain[i] <= g && chain[i] > best) best = chain[i];
    return best;
}

/* ---- a game's intake of an arriving record ----
   A record that reaches a game (the world server's forward or its feed) is kept in that game's box table only when its id is one
   MakeBoxRecId makes, it decodes whole, its key names that very id, and its generation is not lower than the copy already kept for
   that id (an equal generation replaces the copy: the world server stores one set of bytes per generation). Anything else is
   counted by its reason and never kept - never part of a box. */
const int kBiKeep = 0, kBiBadId = 1, kBiUnreadable = 2, kBiWrongBox = 3, kBiOlder = 4,
          kBiLegacy = 5,   /* a player's box stored under its id without the height: *hOut and *itemsOut hold it, and the caller keeps it
                              under BoxRecIdOfHeader's id unless a record of that id with a generation not lower is kept */
          kBiMax = 5;
/* 1 = an arriving record of generation newGen replaces the kept copy (none kept, or newGen not lower than copyGen) */
inline int BoxKeepArrived(int haveCopy, unsigned int copyGen, unsigned int newGen)
{
    if (haveCopy == 0) return 1;
    return newGen >= copyGen ? 1 : 0;
}
/* kBiKeep: *hOut and *itemsOut hold the record; kBiOlder: *hOut holds its header, *itemsOut untouched; anything else: neither is
   touched. *decodeWhy: the decoder's reason when kBiUnreadable, else kBrOk. */
inline int BoxIntakeJudge(const std::string& id, const char* p, size_t size, int haveCopy, unsigned int copyGen,
                          BoxHeader* hOut, std::vector<cooppar::ParityItem>* itemsOut, int* decodeWhy, BoxLedger* ledOut = 0)
{
    if (decodeWhy) *decodeWhy = kBrOk;
    if (BoxRecIdParse(id, 0, 0, 0, 0, 0) == 0) return kBiBadId;
    BoxHeader h;
    std::vector<cooppar::ParityItem> items;
    BoxLedger led;
    const int d = DecodeBoxRecord(p, size, &h, &items, &led);
    if (d != kBrOk) { if (decodeWhy) *decodeWhy = d; return kBiUnreadable; }
    if (BoxRecIdOfHeader(h) != id)
    {
        if (BoxIsPlayerRec(h.kind, h.ownerSlot) == 0 || MakeBoxRecId(h.key) != id || BoxRecIdOfHeader(h).empty()) return kBiWrongBox;
        if (hOut) *hOut = h;
        if (itemsOut) itemsOut->swap(items);
        if (ledOut) *ledOut = led;
        return kBiLegacy;
    }
    if (hOut) *hOut = h;
    if (BoxKeepArrived(haveCopy, copyGen, h.generation) == 0) return kBiOlder;
    if (itemsOut) itemsOut->swap(items);
    if (ledOut) *ledOut = led;
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
   server, or this game's own write kept until a refusal takes it back); haveSent / sentGen: this game's own last write of it, or the stored generation a refusal named. 1 = a seed at generation
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
    case kBiLegacy: return "a player's box under its id without the height (kept under its height id)";
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

/* ---- a player's box, live on every game ----
   A player's box takes its contents from its world-server record on every game that has its area loaded, at every load and whenever
   a newer record arrives - not only on its writer. Its contents never travel as item moves: the writer writes the whole box, every
   other game swaps the record in. A game that is not the writer shows its own take or put at once and asks the writer (undone on a
   refusal); the record is not swapped in while that ask is open, nor - after it closed - until a record of a generation above every
   record kept while the ask was open arrives (at most kBlvAfterRequestCapMs), so the shown change is not taken back by a record
   written before the ask was served. */
const int kBlvApply = 0, kBlvNotNeeded = 1, kBlvHoldRequest = 2, kBlvHoldPending = 3, kBlvHoldCursor = 4, kBlvHoldAfterRequest = 5,
          kBlvHoldMaker = 6, kBlvMax = 6;
const unsigned int kBlvAfterRequestCapMs = 10000u;  /* after an ask on the box closed: the longest a record not above the ask's waits */
/* 1 = the kept record waits after an ask on the box closed: an ask closed (closed), the kept record (keptGen) is not above the
   newest generation kept or in the box while the ask was open (openGen), and less than kBlvAfterRequestCapMs passed since (sinceMs) */
inline int BoxLiveAfterRequest(int closed, unsigned int keptGen, unsigned int openGen, unsigned int sinceMs)
{
    return (closed != 0 && keptGen <= openGen && sinceMs < kBlvAfterRequestCapMs) ? 1 : 0;
}
/* Is the box a player's box? A kept record decides, under either id (haveH / playerH / atH: the height id's record, kept, a player
   record, kept at; haveN / playerN / atN: the id without the height) - the later kept of two; the box's own owner read (localPlayer)
   only when neither is kept. So every game reads the box as the records say, and the writer and the others agree. */
inline int BoxPlayerDecide(int haveH, int playerH, unsigned int atH, int haveN, int playerN, unsigned int atN, int localPlayer)
{
    if (haveH != 0 && haveN != 0) return ((int)(atN - atH) > 0) ? (playerN != 0 ? 1 : 0) : (playerH != 0 ? 1 : 0);
    if (haveH != 0) return playerH != 0 ? 1 : 0;
    if (haveN != 0) return playerN != 0 ? 1 : 0;
    return localPlayer != 0 ? 1 : 0;
}
/* The live apply's choice on a game that does not write the box. haveMine / mineGen: the record generation this game's box is;
   keptGen: the newest record kept here; requestOpen: this game's own take or put on the box waits for the writer's answer;
   pendingMove: a change in the box not handled yet (the drain's ring, a split, a held remove); cursorBusy: an item is on the cursor
   while the box's window is open; afterRequest: BoxLiveAfterRequest; makerHold: items cannot be made yet. Only a generation above
   this game's box is applied. */
inline int BoxLiveChoice(int haveMine, unsigned int mineGen, unsigned int keptGen, int requestOpen, int pendingMove, int cursorBusy,
                         int afterRequest, int makerHold)
{
    if (BoxApplyNeeded(1, keptGen, haveMine, mineGen) == 0) return kBlvNotNeeded;
    if (requestOpen != 0) return kBlvHoldRequest;
    if (pendingMove != 0) return kBlvHoldPending;
    if (cursorBusy != 0) return kBlvHoldCursor;
    if (afterRequest != 0) return kBlvHoldAfterRequest;
    if (makerHold != 0) return kBlvHoldMaker;
    return kBlvApply;
}
inline const char* BoxLiveWhy(int c)
{
    return c == kBlvApply ? "apply" : c == kBlvNotNeeded ? "not-needed" : c == kBlvHoldRequest ? "request-open" : c == kBlvHoldPending ? "move-pending"
         : c == kBlvHoldCursor ? "cursor-over-window" : c == kBlvHoldAfterRequest ? "after-request" : c == kBlvHoldMaker ? "no-item-maker" : "?";
}
/* 1 = the writer of a player's box decides its stored record again: a record above its box arrived and this game has no change of
   its own waiting to be written - that write goes first, above the stored record */
inline int BoxWriterLiveDue(int haveMine, unsigned int mineGen, unsigned int keptGen, int marked)
{
    return (marked == 0 && BoxApplyNeeded(1, keptGen, haveMine, mineGen) != 0) ? 1 : 0;
}
/* A player's box marked (BoxRecMark): is the change made as the box's writer (its contents are then read at the mark, for the write
   made from that read once the box is gone)? writerNow: this game's writer reading now (1 this game, 0 another game, 2 cannot be told
   yet, -1 the box is not found); roleMine: the last reading (1 this game). Cannot be told: the last reading stands. A FRESH mark (no write owed before it) takes this change's answer whole; a mark over an owed change adds to it - a
   change as the writer sets markWriter, a change after the role moved sets markLater, and neither clears the other. 1 = as the writer. */
inline int BoxMarkRole(int writerNow, int roleMine, int fresh, int* markWriter, int* markLater)
{
    const int asWriter = (writerNow == 1 || (writerNow == 2 && roleMine == 1)) ? 1 : 0;
    if (fresh != 0) { *markWriter = asWriter; *markLater = (asWriter != 0) ? 0 : 1; }
    else if (asWriter != 0) *markWriter = 1;
    else *markLater = 1;
    return asWriter;
}
/* this game's own writes of one box waiting for the world server's answer (BOX_TAKEN or BOX_REFUSE), oldest first: at most this many */
const size_t kBrChainMax = 4u;
/* THE BOX RECORD GOES WITH ITS PIECE. At a player-built piece's removal mark the box records at the piece's position are removed: the
   height id (kBgNew) when a record of it is kept here or this game wrote one, the old id without the height (kBgLegacy) when a player
   record is kept under it. Never a box no player owns. */
const int kBgNew = 1, kBgLegacy = 2;
inline int BoxGoneAtDismantle(int haveNew, int sentNew, int haveLegacy)
{
    return ((haveNew != 0 || sentNew != 0) ? kBgNew : 0) | (haveLegacy != 0 ? kBgLegacy : 0);
}

/* ---- THE WRITER'S JOURNAL OF A PLAYER'S BOX ----
   While this game writes a player's box it keeps the newest record of the box known stored that its contents were built on (the BASE:
   the record put into the box, or this game's own write once BOX_TAKEN or BOX_ASKED says it is stored). Every change to the box -
   whatever made it: a character's move, a split, a merge, part of a stack, an NPC's job, a pack's contents, an item made or used up,
   another game's ask this game served - is the difference between the box's contents and that base. When this game stops writing the
   box, once no write of it waits for its answer, that difference (BoxJrnDiffAll against the box as this game last had it) becomes asks
   of the new writer over the item request road: units the box lost are a TAKE of that cell's item, units it gained a GIVE - a pack moved
   in or out whole with its contents as they are, and a change of the contents of a pack that stays in the box as asks addressed inside
   that pack. The move already happened here, so the answer only keeps or undoes it (BoxJrnEnding), on this game's characters that took
   part (its hints). */
const int kBjTake = 0, kBjPut = 1;
struct BoxJrnDelta
{
    int dir;                    /* kBjTake: units the box lost; kBjPut: units it gained */
    cooppar::ParityItem row;    /* the item: a take - the base's row (its cell is where the new writer's box holds it); a put - the row now */
    int quantity;               /* the units */
    int inPack;                 /* 1 = the item is inside a pack that stays in the box: row's section / x / y are the cell in the pack */
    std::string packSection;    /* that pack's cell in the box (as the base holds it) and its record */
    int packX, packY;
    std::string packSid;
    unsigned long long askId;   /* the id an ask of this part carries (reserved at a save or at the hand-over; 0 none yet) */
    BoxJrnDelta() : dir(0), quantity(0), inPack(0), packX(0), packY(0), askId(0ULL) {}
};
/* an item's identity: everything but its cell and quantity (its record, numbers, marks, uniform, crafter); withBag 1 also a pack's
   contents. The box's change compares items without their contents: a pack's contents are compared on their own (BoxJrnDiffAll) */
inline int BoxJrnKey(const cooppar::ParityItem& it, int withBag, std::string* out)
{
    cooppar::ParityBox m;
    m.status = cooppar::kParityStatusContents; m.key = "j"; m.items.push_back(it);
    m.items[0].section = "s"; m.items[0].x = 0; m.items[0].y = 0; m.items[0].quantity = 1;
    if (withBag == 0) m.items[0].bag.clear();
    m.digest = 0;
    std::vector<char> b;
    if (!cooppar::EncodeParityBox(&b, m) || b.empty()) return 0;
    out->assign(&b[0], b.size());
    return 1;
}
inline int BoxJrnIdentity(const cooppar::ParityItem& it, std::string* out) { return BoxJrnKey(it, 0, out); }
inline std::string BoxJrnCell(const cooppar::ParityItem& it)
{
    char n[48];
    std::sprintf(n, "\x01%d,%d", it.x, it.y);
    return it.section + n;
}
/* The box's change: base (the newest stored record it was built on) to now. Per identity, the units it lost are takes from the base's
   cells (first those cells that lost units, then any), the units it gained are puts at today's cells (first those that gained); an item
   only moved inside the box is no change. Takes first, then puts (a put may land where a take emptied). 1 = *out holds it; 0 = an item
   cannot be encoded (*out emptied). */
inline int BoxJrnDiff(const std::vector<cooppar::ParityItem>& base, const std::vector<cooppar::ParityItem>& now, std::vector<BoxJrnDelta>* out)
{
    if (out == 0) return 0;
    out->clear();
    std::vector<std::string> ib(base.size()), in(now.size());
    std::map<std::string, long long> sb, sn;
    std::map<std::string, std::map<std::string, long long> > cb, cn;   /* identity -> cell -> units */
    std::vector<std::string> order;                                   /* identities in the order first seen */
    std::set<std::string> seen;
    for (size_t i = 0; i < base.size(); ++i)
    {
        if (BoxJrnIdentity(base[i], &ib[i]) == 0) return 0;
        sb[ib[i]] += base[i].quantity; cb[ib[i]][BoxJrnCell(base[i])] += base[i].quantity;
        if (seen.insert(ib[i]).second) order.push_back(ib[i]);
    }
    for (size_t i = 0; i < now.size(); ++i)
    {
        if (BoxJrnIdentity(now[i], &in[i]) == 0) { out->clear(); return 0; }
        sn[in[i]] += now[i].quantity; cn[in[i]][BoxJrnCell(now[i])] += now[i].quantity;
        if (seen.insert(in[i]).second) order.push_back(in[i]);
    }
    std::vector<BoxJrnDelta> puts;
    for (size_t o = 0; o < order.size(); ++o)
    {
        const std::string& id = order[o];
        const long long b = sb[id], n = sn[id];
        if (n < b)
        {
            long long need = b - n;
            std::vector<long long> took(base.size(), 0);
            for (int pass = 0; pass < 2 && need > 0; ++pass)
                for (size_t i = 0; i < base.size() && need > 0; ++i)
                {
                    if (ib[i] != id) continue;
                    long long can = (long long)base[i].quantity - took[i];
                    if (pass == 0) { const long long left = cn[id][BoxJrnCell(base[i])]; can = (long long)base[i].quantity - left - took[i]; }
                    if (can <= 0) continue;
                    const long long t = can < need ? can : need;
                    took[i] += t; need -= t;
                }
            for (size_t i = 0; i < base.size(); ++i)
                if (took[i] > 0) { BoxJrnDelta d; d.dir = kBjTake; d.row = base[i]; d.quantity = (int)took[i]; out->push_back(d); }
        }
        else if (n > b)
        {
            long long need = n - b;
            std::vector<long long> got(now.size(), 0);
            for (int pass = 0; pass < 2 && need > 0; ++pass)
                for (size_t i = 0; i < now.size() && need > 0; ++i)
                {
                    if (in[i] != id) continue;
                    long long can = (long long)now[i].quantity - got[i];
                    if (pass == 0) { const long long had = cb[id][BoxJrnCell(now[i])]; can = (long long)now[i].quantity - had - got[i]; }
                    if (can <= 0) continue;
                    const long long t = can < need ? can : need;
                    got[i] += t; need -= t;
                }
            for (size_t i = 0; i < now.size(); ++i)
                if (got[i] > 0) { BoxJrnDelta d; d.dir = kBjPut; d.row = now[i]; d.quantity = (int)got[i]; puts.push_back(d); }
        }
    }
    out->insert(out->end(), puts.begin(), puts.end());
    return 1;
}
/* a row of a pack as an item of its own (its cell inside the pack) */
inline cooppar::ParityItem BoxJrnRowItem(const coopbag::BagRow& r)
{
    cooppar::ParityItem p;
    p.section = r.section; p.x = (int)r.x; p.y = (int)r.y; p.quantity = (int)r.qty;
    p.baseSid = r.baseSid; p.companySid = r.companySid; p.materialSid = r.materialSid; p.colorSid = r.colorSid;
    p.quality = r.quality; p.charges = r.charges; p.functionKind = r.functionKind; p.level = r.level; p.unique = (int)r.unique;
    p.owner = r.owner; p.gear = r.gear;
    return p;
}
/* THE BOX'S WHOLE CHANGE. A pack in both base and now with the very same contents (anywhere in the box) is no change and leaves both
   lists first, so packs of one kind only moved or swapped ask nothing and a pack taken or put whole is the one whose contents went or
   came. Then BoxJrnDiff of the items left (a pack moved in or out whole, with its contents as they are), then, for each pack left in
   both (paired by identity without contents: the same cell first, then in order; a pack taken or put whole is not paired), the change
   of its contents as asks inside it (inPack, at the base pack's cell). Takes first, then puts. A pack whose contents hold a pack and
   changed cannot be addressed one level down: counted in *nested and left out. 1 = *out holds it; 0 = an item cannot be encoded. */
inline int BoxJrnDiffAll(const std::vector<cooppar::ParityItem>& base0, const std::vector<cooppar::ParityItem>& now0, std::vector<BoxJrnDelta>* out, int* nested)
{
    if (nested != 0) *nested = 0;
    if (out == 0) return 0;
    std::vector<cooppar::ParityItem> base, now;
    {   /* the packs with the very same contents on both sides leave (the same cell first, then any) */
        std::vector<std::string> kb(base0.size()), kn(now0.size());
        for (size_t i = 0; i < base0.size(); ++i) if (BoxJrnKey(base0[i], 1, &kb[i]) == 0) { out->clear(); return 0; }
        for (size_t j = 0; j < now0.size(); ++j) if (BoxJrnKey(now0[j], 1, &kn[j]) == 0) { out->clear(); return 0; }
        std::vector<int> gb(base0.size(), 0), gn(now0.size(), 0);
        for (int pass = 0; pass < 2; ++pass)
            for (size_t i = 0; i < base0.size(); ++i)
            {
                if (gb[i] != 0 || base0[i].bag.empty() || base0[i].quantity != 1) continue;
                for (size_t j = 0; j < now0.size(); ++j)
                {
                    if (gn[j] != 0 || now0[j].quantity != 1 || kn[j] != kb[i]) continue;
                    if (pass == 0 && (base0[i].section != now0[j].section || base0[i].x != now0[j].x || base0[i].y != now0[j].y)) continue;
                    gb[i] = 1; gn[j] = 1;
                    break;
                }
            }
        for (size_t i = 0; i < base0.size(); ++i) if (gb[i] == 0) base.push_back(base0[i]);
        for (size_t j = 0; j < now0.size(); ++j) if (gn[j] == 0) now.push_back(now0[j]);
    }
    std::vector<BoxJrnDelta> top;
    if (BoxJrnDiff(base, now, &top) == 0) { out->clear(); return 0; }
    std::vector<std::string> ib(base.size()), in(now.size());
    for (size_t i = 0; i < base.size(); ++i) if (BoxJrnIdentity(base[i], &ib[i]) == 0) { out->clear(); return 0; }
    for (size_t i = 0; i < now.size(); ++i) if (BoxJrnIdentity(now[i], &in[i]) == 0) { out->clear(); return 0; }
    std::vector<int> bOut(base.size(), 0), nOut(now.size(), 0);   /* taken / put whole: not paired */
    for (size_t t = 0; t < top.size(); ++t)
    {
        const cooppar::ParityItem& r = top[t].row;
        if (r.quantity != top[t].quantity) continue;   /* part of a stack: the row stays */
        const std::vector<cooppar::ParityItem>& v = (top[t].dir == kBjTake) ? base : now;
        std::vector<int>& o = (top[t].dir == kBjTake) ? bOut : nOut;
        for (size_t i = 0; i < v.size(); ++i)
            if (o[i] == 0 && v[i].section == r.section && v[i].x == r.x && v[i].y == r.y && v[i].quantity == r.quantity && v[i].baseSid == r.baseSid) { o[i] = 1; break; }
    }
    std::vector<BoxJrnDelta> takes, puts;
    for (size_t t = 0; t < top.size(); ++t) (top[t].dir == kBjTake ? takes : puts).push_back(top[t]);
    std::vector<int> used(now.size(), 0);
    std::vector<std::string> fb(base.size()), fn(now.size());   /* each item with its contents */
    for (size_t i = 0; i < base.size(); ++i) if (BoxJrnKey(base[i], 1, &fb[i]) == 0) { out->clear(); return 0; }
    for (size_t j = 0; j < now.size(); ++j) if (BoxJrnKey(now[j], 1, &fn[j]) == 0) { out->clear(); return 0; }
    /* pass 0 the same contents at the same cell, 1 the same contents anywhere (packs only moved or swapped: no change), 2 the same cell,
       3 in order */
    for (int pass = 0; pass < 4; ++pass)
        for (size_t i = 0; i < base.size(); ++i)
        {
            if (bOut[i] != 0) continue;
            for (size_t j = 0; j < now.size(); ++j)
            {
                if (used[j] != 0 || nOut[j] != 0 || in[j] != ib[i]) continue;
                if (base[i].bag.empty() && now[j].bag.empty()) continue;
                const int sameCell = (base[i].section == now[j].section && base[i].x == now[j].x && base[i].y == now[j].y) ? 1 : 0;
                if (pass <= 1 && fb[i] != fn[j]) continue;
                if ((pass == 0 || pass == 2) && sameCell == 0) continue;
                used[j] = 1; bOut[i] = 2;
                if (fb[i] == fn[j]) break;
                int deep = 0;
                for (size_t r = 0; r < base[i].bag.size(); ++r) if (base[i].bag[r].parent != 0) deep = 1;
                for (size_t r = 0; r < now[j].bag.size(); ++r) if (now[j].bag[r].parent != 0) deep = 1;
                if (deep != 0) { if (nested != 0) ++*nested; break; }
                std::vector<cooppar::ParityItem> rb, rn;
                for (size_t r = 0; r < base[i].bag.size(); ++r) rb.push_back(BoxJrnRowItem(base[i].bag[r]));
                for (size_t r = 0; r < now[j].bag.size(); ++r) rn.push_back(BoxJrnRowItem(now[j].bag[r]));
                std::vector<BoxJrnDelta> dd;
                if (BoxJrnDiff(rb, rn, &dd) == 0) { out->clear(); return 0; }
                for (size_t k = 0; k < dd.size(); ++k)
                {
                    dd[k].inPack = 1; dd[k].packSection = base[i].section; dd[k].packX = base[i].x; dd[k].packY = base[i].y; dd[k].packSid = base[i].baseSid;
                    (dd[k].dir == kBjTake ? takes : puts).push_back(dd[k]);
                }
                break;
            }
        }
    out->clear();
    out->insert(out->end(), takes.begin(), takes.end());
    out->insert(out->end(), puts.begin(), puts.end());
    return 1;
}
/* 1 = the box's change becomes asks of its writer now. waiting: a write of the box waits for its answer (BOX_TAKEN / BOX_REFUSE, or
   BOX_ASKED after a link loss) - its answer moves the base first; writerNow: this game's writer reading of the box now (1 this game,
   0 another game, 2 cannot be told yet, -1 the box is not loaded here); lastMine: the last reading (1 this game); writeOwed: a write of
   the box this game may still send as its writer (a mark with the box's contents read at it). Another game writes the box: asked. Not
   loaded here: asked unless this game was its writer at the last reading and still owes that write. This game, or not told yet: none. */
inline int BoxJrnAskDue(int waiting, int writerNow, int lastMine, int writeOwed)
{
    if (waiting != 0) return 0;
    if (writerNow == 0) return 1;
    if (writerNow != -1) return 0;
    return (lastMine != 1 || writeOwed == 0) ? 1 : 0;
}
/* 1 = a game that has just taken the writer role (roleWas: its reading before, 0 another game) puts the newest stored record into its
   box before its first write: one is kept above what its box is - an old writer's write it never laid */
inline int BoxRoleTakeApply(int roleWas, int haveKept, unsigned int keptGen, int haveMine, unsigned int mineGen)
{
    return (roleWas == 0 && haveKept != 0 && BoxApplyNeeded(1, keptGen, haveMine, mineGen) != 0) ? 1 : 0;
}
/* the asks one box's change may make (a box holds at most kBrMaxRows items, so its change at most twice that) */
const size_t kBjAskCap = 2u * kBrMaxRows;
/* THE ENDING OF A JOURNALLED ASK. answer: kBjaAccept the writer's yes, kBjaRefuse its no, kBjaTimeout no answer in the request's time,
   kBjaLateYes / kBjaLateNo an answer after that time, kBjaUnsent the ask could not be sent. The move already happened here, so:
   TAKE  yes: kept, and the writer is told to let its removed item go (PLACED ok); no / unsent: the units are removed from this game's
         characters that took part; timeout: removed, and the writer is told PLACED no (a writer that served it puts its removed item
         back at once; one that never did ignores it); a late yes: PLACED no again.
   PUT   yes: kept (the item is in the writer's box); no / unsent: the item is made again on the character (first free cell); timeout:
         made again, and the writer is told to drop any copy it built (REVOKE); a late yes: that REVOKE again (its row exists now).
   A late no changes nothing. */
const int kBjaAccept = 0, kBjaRefuse = 1, kBjaTimeout = 2, kBjaLateYes = 3, kBjaLateNo = 4, kBjaUnsent = 5;
const int kBjDoNothing = 0, kBjDoRemoveChar = 1, kBjDoRemake = 2, kBjDoPlacedOk = 4, kBjDoPlacedNo = 8, kBjDoRevoke = 16;
inline int BoxJrnEnding(int dir, int answer)
{
    if (dir == kBjTake)
    {
        if (answer == kBjaAccept) return kBjDoPlacedOk;
        if (answer == kBjaRefuse || answer == kBjaUnsent) return kBjDoRemoveChar;
        if (answer == kBjaTimeout) return kBjDoRemoveChar | kBjDoPlacedNo;
        if (answer == kBjaLateYes) return kBjDoPlacedNo;
        return kBjDoNothing;
    }
    if (dir != kBjPut) return kBjDoNothing;
    if (answer == kBjaRefuse || answer == kBjaUnsent) return kBjDoRemake;
    if (answer == kBjaTimeout) return kBjDoRemake | kBjDoRevoke;
    if (answer == kBjaLateYes) return kBjDoRevoke;
    return kBjDoNothing;
}
/* THE JOURNAL'S STATE OF ONE BOX (the game keeps one per player's box; the offline tests drive it the same way).
   open: the base is known. waiting: this game's writes of the box sent and not answered yet, oldest first (at most kBrChainMax).
   lost: the link carried a waiting write and went down - BOX_ASKED answers it after the next WELCOME. askTop: the newest waiting write
   when that BOX_ASK went (BOX_ASKED answers only writes up to it). done: writes no longer waiting whose BOX_TAKEN or feed echo may still
   come (one BOX_ASKED said was not stored, one dropped at the cap) - kept so such a late answer still moves the base. */
struct BoxJrnWrite
{
    unsigned int gen;
    std::vector<cooppar::ParityItem> items;
    unsigned int at;          /* when it was sent (a tick count) */
    unsigned long long chg;   /* the change id it carries in the record's ledger (0 none) */
    BoxJrnWrite() : gen(0), at(0), chg(0ULL) {}
};
struct BoxJrnState
{
    int open;
    unsigned int baseGen;
    std::vector<cooppar::ParityItem> base;
    std::vector<BoxJrnWrite> waiting;
    int lost;
    unsigned int askTop;
    std::vector<BoxJrnWrite> done;
    BoxJrnState() : open(0), baseGen(0), lost(0), askTop(0) {}
};
/* a write no longer waiting is kept in done (at most kBrChainMax, the oldest goes) */
inline void BoxJrnDoneKeep(BoxJrnState* s, const BoxJrnWrite& w)
{
    if (s->done.size() >= kBrChainMax) s->done.erase(s->done.begin());
    s->done.push_back(w);
}
/* the box is the stored record of generation gen (laid here, or this game's own write stored): the base; the writes up to it are
   answered by it */
inline void BoxJrnOpen(BoxJrnState* s, const std::vector<cooppar::ParityItem>& base, unsigned int gen)
{
    if (s == 0) return;
    s->open = 1; s->base = base; s->baseGen = gen;
    size_t k = 0;
    for (size_t i = 0; i < s->waiting.size(); ++i) if (s->waiting[i].gen > gen) { if (k != i) s->waiting[k] = s->waiting[i]; ++k; }
    s->waiting.resize(k);
    if (s->waiting.empty()) { s->lost = 0; s->askTop = 0; }
    k = 0;
    for (size_t i = 0; i < s->done.size(); ++i) if (s->done[i].gen > gen) { if (k != i) s->done[k] = s->done[i]; ++k; }
    s->done.resize(k);
}
/* this game sent a write of the box. 1 = the oldest waiting write had to go to make room (it is never answered here) */
inline int BoxJrnWrote(BoxJrnState* s, unsigned int gen, const std::vector<cooppar::ParityItem>& items, unsigned int at, unsigned long long chg = 0ULL)
{
    if (s == 0) return 0;
    int dropped = 0;
    if (s->waiting.size() >= kBrChainMax) { BoxJrnDoneKeep(s, s->waiting[0]); s->waiting.erase(s->waiting.begin()); dropped = 1; }
    BoxJrnWrite w;
    w.gen = gen; w.items = items; w.at = at; w.chg = chg;
    s->waiting.push_back(w);
    return dropped;
}
/* BOX_TAKEN (or the feed bringing it back): this game's write of generation gen is stored. Found among the waiting or the done writes
   and above the base (or with no base): it is the base; the writes up to it are answered either way. 1 = found (*atOut: when it was
   sent) */
inline int BoxJrnTaken(BoxJrnState* s, unsigned int gen, unsigned int* atOut)
{
    if (s == 0) return 0;
    const BoxJrnWrite* f = 0;
    for (size_t i = 0; i < s->waiting.size() && f == 0; ++i) if (s->waiting[i].gen == gen) f = &s->waiting[i];
    for (size_t i = 0; i < s->done.size() && f == 0; ++i) if (s->done[i].gen == gen) f = &s->done[i];
    if (f == 0) return 0;
    const BoxJrnWrite w = *f;
    if (atOut != 0) *atOut = w.at;
    if (s->open == 0 || gen > s->baseGen) { BoxJrnOpen(s, w.items, gen); return 1; }
    size_t k = 0;   /* not above the base: the base stays; the writes up to it are answered */
    for (size_t i = 0; i < s->waiting.size(); ++i) if (s->waiting[i].gen > gen) { if (k != i) s->waiting[k] = s->waiting[i]; ++k; }
    s->waiting.resize(k);
    if (s->waiting.empty()) { s->lost = 0; s->askTop = 0; }
    return 1;
}
/* BOX_REFUSE: the write of generation gen is not stored - its change is still the box's change against the base. 1 = it was waiting */
inline int BoxJrnRefusedW(BoxJrnState* s, unsigned int gen)
{
    if (s == 0) return 0;
    for (size_t i = 0; i < s->waiting.size(); ++i)
        if (s->waiting[i].gen == gen) { s->waiting.erase(s->waiting.begin() + (long)i); if (s->waiting.empty()) s->lost = 0; return 1; }
    return 0;
}
/* the link went down: its waiting writes are answered by BOX_ASKED */
inline void BoxJrnLinkLost(BoxJrnState* s)
{
    if (s != 0 && !s->waiting.empty()) s->lost = 1;
}
/* a waiting write with no answer for ms (now - its send): treated as one the link lost. 1 = it is lost now */
inline int BoxJrnStale(BoxJrnState* s, unsigned int now, unsigned int ms)
{
    if (s == 0 || s->waiting.empty() || s->lost != 0) return 0;
    if (now - s->waiting[0].at < ms) return 0;
    s->lost = 1;
    return 1;
}
/* a BOX_ASK about the box goes now: the writes it answers are those waiting when the first unanswered ask went (up to the newest then);
   an ask sent again keeps that cut-off, so a write sent between two asks is never answered by them. The cut-off (0 = none) */
inline unsigned int BoxJrnAskSent(BoxJrnState* s)
{
    if (s == 0) return 0;
    if (s->askTop != 0) return s->askTop;
    unsigned int top = 0;
    for (size_t i = 0; i < s->waiting.size(); ++i) if (s->waiting[i].gen > top) top = s->waiting[i].gen;
    s->askTop = top;
    return top;
}
/* BOX_ASKED: g, this game's newest stored write of the box. Of the writes waiting when the ask went (up to askTop), the newest at or
   below g is stored (the base); the others were not stored (kept in done, so a late echo still counts) - unless pending: the world
   server still had writes of the box queued, so those above g keep waiting for their own echo (the feed, or BOX_TAKEN). A write sent
   after the ask keeps waiting for its own BOX_TAKEN. The stored generation (0 = none of them); *atOut: when it was sent */
inline unsigned int BoxJrnAsked(BoxJrnState* s, unsigned int g, unsigned int* atOut, int pending)
{
    if (s == 0) return 0;
    std::vector<unsigned int> gens;
    for (size_t i = 0; i < s->waiting.size(); ++i) if (s->waiting[i].gen <= s->askTop) gens.push_back(s->waiting[i].gen);
    const unsigned int st = BoxAskedStored(gens, g);
    if (st != 0) BoxJrnTaken(s, st, atOut);
    size_t k = 0;
    for (size_t i = 0; i < s->waiting.size(); ++i)
    {
        if (s->waiting[i].gen <= s->askTop && pending == 0) { BoxJrnDoneKeep(s, s->waiting[i]); continue; }
        if (k != i) s->waiting[k] = s->waiting[i];
        ++k;
    }
    s->waiting.resize(k);
    s->lost = 0; s->askTop = 0;
    return st;
}
/* one undo's units still owed after a pass that removed `removed` of them: never below 0, never more than were owed. The units taken */
inline int BoxJrnOwe(int* owed, int removed)
{
    if (owed == 0 || removed <= 0 || *owed <= 0) return 0;
    const int t = removed < *owed ? removed : *owed;
    *owed -= t;
    return t;
}
/* 1 = a hint (a character's part in a change, at hintAt) stays after a stored write sent at writeAt: it came at or after the write's read
   (the same millisecond stays: the hint may be of a change the write did not read) */
inline int BoxJrnHintStays(unsigned int hintAt, unsigned int writeAt) { return ((int)(hintAt - writeAt) >= 0) ? 1 : 0; }
/* A STORED RECORD LAID INTO THE BOX OVER A CHANGE NOT YET STORED (how: 0 the box already held it, 1 it replaced the box, 2 it was laid
   with this game's held moves on top). pending: the change against the old base is not empty; recordHolds: the record already holds
   the box as this game had it. kRbNone: nothing pending; kRbHandOver: another game writes the box - the change goes to it as asks;
   kRbReapply: this game writes the box - its change is laid again on top of the record as its own writes (BoxJrnMergeOnto), and only
   what conflicts is undone on the character; kRbKeep: the held moves are the change, read again from the box. kRbUndo: the character
   side undone whole - only when the change cannot be laid again (the box not here, no item maker). */
const int kRbNone = 0, kRbHandOver = 1, kRbUndo = 2, kRbKeep = 3, kRbReapply = 4;
/* THE GATES BEFORE A SWAP OF A BOX'S CONTENTS (the stored record's apply and the journal's lay again share them), in this order:
   kSwBlocked (engine writes blocked, the world tearing down), kSwPending (a move of the box not published yet), kSwCursor (an item on
   the cursor while the box's window or shop is open), kSwNoMaker (items cannot be made), kSwLoot (this game's research copies still
   in it). kSwGo = swap now; anything else = wait and try again. */
const int kSwGo = 0, kSwBlocked = 1, kSwPending = 2, kSwCursor = 3, kSwNoMaker = 4, kSwLoot = 5;
inline int BoxSwapGate(int blocked, int pendingMoves, int cursorBusy, int viewOpen, int makerHold, int lootBusy)
{
    if (blocked != 0) return kSwBlocked;
    if (pendingMoves != 0) return kSwPending;
    if (cursorBusy != 0 && viewOpen != 0) return kSwCursor;
    if (makerHold != 0) return kSwNoMaker;
    if (lootBusy != 0) return kSwLoot;
    return kSwGo;
}
/* THE LAY AGAIN OF THIS GAME'S CHANGE (items.cpp BoxJrnReapply): wait, or undo it on the character now. Only a passing state waits: a box
   fill running, or a swap gate holding (kSwBlocked .. kSwLoot). The box not loaded here, its contents unreadable (or the change not
   mergeable onto them), or a swap that did not start (more sections than a swap takes, a cell unreadable) do not pass by waiting - a park
   dropped at teardown would leave the items on the character and in the stored box - so the change is undone on the character at once.
   swapRan: 1 until the swap is tried. kRlLay = lay (or laid), kRlWait = wait and try again, kRlUndo = undo on the character now */
const int kRlUndo = 0, kRlLay = 1, kRlWait = -1;
inline int BoxJrnReapplyStep(int fillRunning, int boxHere, int readable, int gate, int swapRan)
{
    if (fillRunning != 0) return kRlWait;
    if (boxHere == 0 || readable == 0) return kRlUndo;
    if (gate != kSwGo) return kRlWait;
    if (swapRan == 0) return kRlUndo;
    return kRlLay;
}
/* A SAVE ABOUT TO BE WRITTEN over a parked change: 1 = undo it on the character now - no write of it waits and it is to be laid again, so
   only the swap's gates hold it, and the save would hold it on the character while the box's stored record lacks it; 0 = leave it (no
   park, its writes' answers still decide it, or it goes to another writer) */
inline int BoxJrnParkSaveUndo(int parked, int writesWaiting, int act)
{
    return (parked != 0 && writesWaiting == 0 && act == kRbReapply) ? 1 : 0;
}
/* THE STORED RECORD BECOMES THE JOURNAL'S BASE (how: 0 the box already held the record, 1 laid over the box, 2 laid with this game's held
   moves on top). how 0: nothing - the box as it is, this game's change in it, is the stored record, so laying the change again or asking
   another writer for it would make it twice. Otherwise a change not yet stored (pending) that the record does not hold is laid again as
   the writer, or handed to the record's writer as asks */
inline int BoxJrnRebaseDo(int how, int roleMine, int pending, int recordHolds)
{
    if (how == 2) return kRbKeep;
    if (how == 0) return kRbNone;
    if (pending == 0 || recordHolds != 0) return kRbNone;
    return (roleMine == 1) ? kRbReapply : kRbHandOver;
}
/* THE BOXES THIS GAME'S OWN GUARDED WRITES CHANGED (a refused move put back, a rollback, a held take returned: the item hooks ring none
   of them, so nothing marks the box). Each building is noted (at most kBoxGuardMax; past that every box counts as noted) and the box's
   journal reads its latest again from the box as it is (items.cpp BoxJrnGuardReread), so the journal never keeps a move the game undid.
   No allocation: the hooks note into it. */
const int kBoxGuardMax = 32;
struct BoxGuardNotes { void* at[kBoxGuardMax]; int n; int all; BoxGuardNotes() : n(0), all(0) { for (int i = 0; i < kBoxGuardMax; ++i) at[i] = 0; } };
inline void BoxGuardNote(BoxGuardNotes* g, void* p)
{
    if (g == 0 || p == 0 || g->all != 0) return;
    for (int i = 0; i < g->n; ++i) if (g->at[i] == p) return;
    if (g->n >= kBoxGuardMax) { g->all = 1; return; }
    g->at[g->n++] = p;
}
inline int BoxGuardHas(const BoxGuardNotes& g, const void* p)
{
    if (p == 0) return 0;
    if (g.all != 0) return 1;
    for (int i = 0; i < g.n; ++i) if (g.at[i] == p) return 1;
    return 0;
}
inline int BoxGuardAny(const BoxGuardNotes& g) { return (g.n > 0 || g.all != 0) ? 1 : 0; }
/* THE JOURNAL'S LATEST READ AGAIN FROM THE BOX AS IT IS (items.cpp BoxJrnReadAgain): for a box this game holds a change of journalled
   (latestHave) and that is loaded here - whatever changed it since its mark, by any road, is then in it - whichever game writes it now:
   the role can leave before the change does (a record laid over it, a write refused as not the writer's). Never when this game marked it
   as not its writer since its last read as the writer (askMix), whatever role is noted: the box then shows this game's own asks at once,
   which are not the journal's change, and the noted role can lag the mark (BoxJrnTick reads before it notes the role). 1 = read it again */
inline int BoxJrnReadAgainDue(int latestHave, int loaded, int askMix)
{
    return (latestHave != 0 && loaded != 0 && askMix == 0) ? 1 : 0;
}
/* a change of a box a guarded write noted (changed since its last read) is unsure when it is held (held) and was not read again (readOk
   0; a parked change is never read again). 1 = unsure */
inline int BoxJrnUnsureAfterRead(int noted, int held, int readOk)
{
    return (noted != 0 && held != 0 && readOk == 0) ? 1 : 0;
}
/* at a save, before any row: is a box with a held change noted? Notes pending (anyNotes) and the box found noted (has) - or the boxes
   cannot be looked up now (canResolve 0: a fill runs, the world is torn down), when any pending note may be its own. 1 = noted */
inline int BoxJrnSaveNoted(int anyNotes, int canResolve, int has)
{
    return (anyNotes != 0 && (canResolve == 0 || has != 0)) ? 1 : 0;
}
/* a box's note consumed (a parked change takes the box as it was read: a later note is a later write). Past the notes' cap every box
   stays noted */
inline void BoxGuardForget(BoxGuardNotes* g, const void* p)
{
    if (g == 0 || p == 0) return;
    for (int i = 0; i < g->n; ++i)
        if (g->at[i] == p) { g->at[i] = g->at[g->n - 1]; g->at[g->n - 1] = 0; --g->n; return; }
}
/* AN UNSURE CHANGE: a guarded write noted its box after the box was last read and it could not be read again, so a part of the change
   may already be put back. Its takes are dropped - left on the character, not laid or asked again (a take put back and taken from the
   box again would be gone from both) - and its puts kept: made again at worst, a copy too many, never a loss (as coopbrec::BoxOwedAct's
   unsure). The count dropped */
inline int BoxJrnUnsureParts(std::vector<BoxJrnDelta>* d)
{
    if (d == 0) return 0;
    int dropped = 0;
    for (size_t i = 0; i < d->size();)
    {
        if ((*d)[i].dir == kBjTake) { d->erase(d->begin() + (long)i); ++dropped; }
        else ++i;
    }
    return dropped;
}
inline void BoxGuardClear(BoxGuardNotes* g) { if (g != 0) { g->n = 0; g->all = 0; } }
/* THIS GAME'S CHANGE LAID AGAIN ON TOP OF A NEWER RECORD, as the writer lays an ask (onto: the box as it is now, the record laid; d: the
   change against the old base). A take removes its units from the box where they still are (the same cell first, then any item of
   that identity) - units no longer there (another game took them) go to *undo with exactly that quantity. A put adds its item at its
   own cell (*putAt: the index in *merged of the item it was laid as, so a refused place undoes it). A whole pack is matched with its
   contents (the same cell first). Inside a pack likewise, in the pack
   of that record at the pack's cell (else any pack of that record): a put into a cell of the pack already used, or with no such pack,
   goes to *undo. 1 = *merged holds the box to lay; 0 = an item cannot be encoded. */
inline int BoxJrnPackAt(const std::vector<cooppar::ParityItem>& v, const BoxJrnDelta& d)
{
    int any = -1;
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (v[i].baseSid != d.packSid) continue;
        if (v[i].section == d.packSection && v[i].x == d.packX && v[i].y == d.packY) return (int)i;
        if (any < 0) any = (int)i;
    }
    return any;
}
inline int BoxJrnMergeOnto(const std::vector<cooppar::ParityItem>& onto, const std::vector<BoxJrnDelta>& d, std::vector<cooppar::ParityItem>* merged,
                           std::vector<BoxJrnDelta>* undo, std::vector<int>* putAt)
{
    if (merged == 0 || undo == 0 || putAt == 0) return 0;
    *merged = onto; undo->clear(); putAt->assign(d.size(), -1);
    for (size_t k = 0; k < d.size(); ++k)
    {
        std::string id, full;
        if (BoxJrnIdentity(d[k].row, &id) == 0 || BoxJrnKey(d[k].row, 1, &full) == 0) return 0;
        if (d[k].dir == kBjTake)
        {
            int need = d[k].quantity;
            for (int pass = 0; pass < 2 && need > 0; ++pass)
            {
                if (d[k].inPack == 0)
                {   /* the item with its contents (a pack another game emptied or filled is not the pack taken here) */
                    for (size_t i = 0; i < merged->size() && need > 0; ++i)
                    {
                        cooppar::ParityItem& m = (*merged)[i];
                        if (pass == 0 && (m.section != d[k].row.section || m.x != d[k].row.x || m.y != d[k].row.y)) continue;
                        std::string mi;
                        if (BoxJrnKey(m, 1, &mi) == 0) return 0;
                        if (mi != full || m.quantity <= 0) continue;
                        const int t = m.quantity < need ? m.quantity : need;
                        m.quantity -= t; need -= t;
                    }
                }
                else
                {
                    const int p = BoxJrnPackAt(*merged, d[k]);
                    if (p < 0) break;
                    std::vector<coopbag::BagRow>& bag = (*merged)[p].bag;
                    for (size_t r = 0; r < bag.size() && need > 0; ++r)
                    {
                        if (bag[r].parent != 0) continue;
                        if (pass == 0 && (bag[r].section != d[k].row.section || (int)bag[r].x != d[k].row.x || (int)bag[r].y != d[k].row.y)) continue;
                        std::string ri;
                        if (BoxJrnIdentity(BoxJrnRowItem(bag[r]), &ri) == 0) return 0;
                        if (ri != id || bag[r].qty == 0) continue;
                        const int t = (int)bag[r].qty < need ? (int)bag[r].qty : need;
                        bag[r].qty -= (unsigned int)t; need -= t;
                    }
                }
            }
            if (need > 0) { BoxJrnDelta u = d[k]; u.quantity = need; undo->push_back(u); }
            continue;
        }
        if (d[k].inPack == 0)
        {
            cooppar::ParityItem it = d[k].row;
            it.quantity = d[k].quantity;
            merged->push_back(it);
            (*putAt)[k] = (int)merged->size() - 1;
            continue;
        }
        const int p = BoxJrnPackAt(*merged, d[k]);
        int used = (p < 0) ? 1 : 0;
        if (p >= 0)
            for (size_t r = 0; r < (*merged)[p].bag.size(); ++r)
            {
                const coopbag::BagRow& b = (*merged)[p].bag[r];
                if (b.parent == 0 && b.qty > 0 && b.section == d[k].row.section && (int)b.x == d[k].row.x && (int)b.y == d[k].row.y) used = 1;
            }
        if (used != 0) { undo->push_back(d[k]); continue; }
        coopbag::BagRow b;
        b.section = d[k].row.section; b.x = (unsigned int)d[k].row.x; b.y = (unsigned int)d[k].row.y; b.qty = (unsigned int)d[k].quantity;
        b.baseSid = d[k].row.baseSid; b.companySid = d[k].row.companySid; b.materialSid = d[k].row.materialSid; b.colorSid = d[k].row.colorSid;
        b.quality = d[k].row.quality; b.charges = d[k].row.charges; b.functionKind = d[k].row.functionKind; b.level = d[k].row.level;
        b.unique = (unsigned char)d[k].row.unique; b.owner = d[k].row.owner; b.gear = d[k].row.gear; b.parent = 0;
        (*merged)[p].bag.push_back(b);
        (*putAt)[k] = p;
    }
    /* stacks and pack rows taken to nothing leave */
    std::vector<cooppar::ParityItem> keep;
    std::vector<int> newIdx(merged->size(), -1);
    for (size_t i = 0; i < merged->size(); ++i)
    {
        cooppar::ParityItem m = (*merged)[i];
        if (m.quantity <= 0) continue;
        std::vector<coopbag::BagRow> rows;
        for (size_t r = 0; r < m.bag.size(); ++r) if (m.bag[r].qty > 0 || m.bag[r].parent != 0) rows.push_back(m.bag[r]);
        m.bag.swap(rows);
        newIdx[i] = (int)keep.size();
        keep.push_back(m);
    }
    for (size_t k = 0; k < putAt->size(); ++k) if ((*putAt)[k] >= 0) (*putAt)[k] = newIdx[(*putAt)[k]];
    merged->swap(keep);
    return 1;
}
inline int BoxJrnWaiting(const BoxJrnState& s) { return s.waiting.empty() ? 0 : 1; }
/* the box's change as asks: latest (the box as this game last had it as its writer) against the base. 1 = *out holds it (empty: no
   change); 0 = no base, or an item cannot be encoded */
inline int BoxJrnPlan(const BoxJrnState& s, const std::vector<cooppar::ParityItem>& latest, std::vector<BoxJrnDelta>* out, int* nested = 0)
{
    if (nested != 0) *nested = 0;
    if (out == 0) return 0;
    out->clear();
    if (s.open == 0) return 0;
    return BoxJrnDiffAll(s.base, latest, out, nested);
}
/* the change has gone to the new writer: the base is the new writer's to make */
inline void BoxJrnClose(BoxJrnState* s)
{
    if (s == 0) return;
    s->open = 0; s->base.clear(); s->baseGen = 0;
}

/* ---- THE BOX CHANGES A SAVE HOLDS UNRESOLVED (kBoxOwedFile, beside the save) ----
   A save can hold this game's change of a player's box on the character while the box's stored record may not hold it: a change this
   game wrote and has no answer for, or has not written yet (the journal, a parked change), an ask of a change handed to another writer
   not sent or not answered, an undo on a character not applied yet. Each is an owed row written beside that save, and the next load of
   that save settles it by the ids the change already has (THE BOX'S LEDGER): the newest of this game's writes of it the stored record's
   ledger holds gives the change left after it; each part left is then stored when its ask's id is there, not stored only when the
   ledger is of the very line the row names and every id that could hold the part is absent from it and above the slot's floor (an ask's
   id once the grace for an ask still on its way is over), and unsure otherwise: a seed's new line, a record with no ledger, an id the
   cap left out, a doubt. Nothing is settled from the box's stack counts. Not stored: undone exactly (a take's units leave the character,
   a put is made again); unsure: a take is left on the character and a put is made again - at worst a copy too many, never a loss -
   counted and logged. decided: an undo the game had already decided while its character was not loaded - done as it is.
   who: the characters that took part, by their save-stable ids (hovrec::CharWhere) - a uid names nothing after a load; a row is undone
   only on those characters. */
const char* const kBoxOwedFile = "multiplayer.boxowed";
const unsigned int kBoxOwedMarker = 0x574F4A42u;   /* 'BJOW' */
const unsigned int kBoxOwedFormat = 2u;
const unsigned int kBoxOwedMaxRows = 1024u, kBoxOwedMaxWho = 64u, kBoxOwedMaxIds = 64u, kBoxOwedMaxDeltas = 4096u, kBoxOwedMaxStr = 1024u;
struct BoxOwedWho { std::string where; std::string section; };
struct BoxOwedRow
{
    std::string id, key;                              /* the box's record id and key */
    float pos[3];                                     /* where the box was read (0,0,0 unknown) */
    std::vector<BoxOwedWho> who;
    int slot;                                         /* the ledger slot this game's ids are recorded under (-1 unknown) */
    unsigned long long line;                          /* the ledger line those ids go to (0 unknown: an absent id reads unsure) */
    int decided;                                      /* 1 = an undo already decided (ifNone): done as it is */
    std::vector<unsigned long long> ids;              /* this game's writes that may hold the change, oldest first - the last may be the id
                                                         the save sealed for the box as it stood (BoxSeal), no write of it sent yet */
    std::vector<std::vector<BoxJrnDelta> > ifStored;  /* by ids: the change left on the character when that write is the newest stored */
    std::vector<BoxJrnDelta> ifNone;                  /* the change left when none of them is (each part with its ask's id, 0 none) */
    BoxOwedRow() : slot(-1), line(0ULL), decided(0) { pos[0] = 0; pos[1] = 0; pos[2] = 0; }
};
/* an unsure change's row beside a save (BoxJrnUnsureParts on each of its lists): its takes never undone or asked at the load, its puts
   kept. The count dropped */
inline int BoxOwedRowUnsure(BoxOwedRow* r)
{
    if (r == 0) return 0;
    int n = BoxJrnUnsureParts(&r->ifNone);
    for (size_t g = 0; g < r->ifStored.size(); ++g) n += BoxJrnUnsureParts(&r->ifStored[g]);
    return n;
}
/* a journal's change (its state and the box as this game last had it) as an owed row: its writes - waiting, and those answered not
   stored whose echo may still come - oldest first, each by its id with the change left when it is the newest stored; then the change
   not written yet, under the id the save sealed for the box as it stands (pendId, BoxSeal; 0 none), with nothing left. 1 = made; 0 = no base, an item cannot be
   encoded, or a write with no id (that row could never be settled) */
inline int BoxOwedFromPark(const BoxJrnState& s, const std::vector<cooppar::ParityItem>& latest, unsigned long long pendId, BoxOwedRow* r)
{
    if (r == 0) return 0;
    r->ids.clear(); r->ifStored.clear(); r->ifNone.clear();
    if (BoxJrnPlan(s, latest, &r->ifNone) == 0) return 0;
    std::vector<const BoxJrnWrite*> ws;
    for (int pass = 0; pass < 2; ++pass)
    {
        const std::vector<BoxJrnWrite>& v = (pass == 0) ? s.waiting : s.done;
        for (size_t i = 0; i < v.size(); ++i)
        {
            size_t at = ws.size();
            while (at > 0 && ws[at - 1]->gen > v[i].gen) --at;
            ws.insert(ws.begin() + (long)at, &v[i]);
        }
    }
    for (size_t i = 0; i < ws.size(); ++i)
    {
        std::vector<BoxJrnDelta> left;
        if (ws[i]->chg == 0ULL || BoxJrnDiffAll(ws[i]->items, latest, &left, 0) == 0) { r->ids.clear(); r->ifStored.clear(); r->ifNone.clear(); return 0; }
        r->ids.push_back(ws[i]->chg);
        r->ifStored.push_back(left);
    }
    if (pendId != 0ULL) { r->ids.push_back(pendId); r->ifStored.push_back(std::vector<BoxJrnDelta>()); }
    return 1;
}
/* which parked change a save holds as an owed row: this game's own (it is laid again as the writer) with a write of it still waiting (one
   with none was undone on the character before the save: BoxJrnParkSaveUndo), and every change handed to another writer as asks (after a
   quit those asks are never sent: the stored record settles it) */
inline int BoxOwedParkRow(int act, int writesWaiting)
{
    if (act == kRbHandOver) return 1;
    return (act == kRbReapply && writesWaiting != 0) ? 1 : 0;
}
/* the box's journal at a save (this game the writer): its change against its base as an owed row (BoxOwedFromPark). 1 = made; 0 = no
   change, no base, or a row that cannot be made */
inline int BoxOwedFromJournal(const BoxJrnState& s, int latestHave, const std::vector<cooppar::ParityItem>& latest, unsigned long long pendId, BoxOwedRow* r)
{
    if (latestHave == 0 || s.open == 0 || r == 0) return 0;
    if (BoxOwedFromPark(s, latest, pendId, r) == 0) return 0;
    return r->ifNone.empty() ? 0 : 1;
}
/* A SAVE SEALS THE ID ITS ROW NAMES. A row names the change not written yet under one id, with nothing left when that id is stored; the
   box may change again before any write (the link down, a write skipped as unchanged), so the save seals that id with the box as it
   stood (latest) and the id it was reserved as is released - a change after the save takes a new id. A write carries a seal's id
   exactly only when it writes the box as it stood at that save (the save's change is then all of the record's), and as doubt otherwise
   (BoxSealCarry); a doubt stays doubt. A seal leaves once the kept record's ledger carries its id (its line holds it from then on); at
   kBoxSealMax the oldest leaves, carried as doubt. Never written, the id reads absent: the change is not stored. */
struct BoxSeal { unsigned long long id; std::vector<cooppar::ParityItem> items; BoxSeal() : id(0ULL) {} };
const unsigned int kBoxSealMax = 8u;
/* 1 = the two contents are the same box (no part between them) */
inline int BoxSealSame(const std::vector<cooppar::ParityItem>& a, const std::vector<cooppar::ParityItem>& b)
{
    std::vector<BoxJrnDelta> d;
    return (BoxJrnDiffAll(a, b, &d, 0) != 0 && d.empty()) ? 1 : 0;
}
/* the id a seal holds for the box as it stands (latest) that a save may name again; 0 none. Never an id the kept record's ledger
   already carries (keptHave / kept; slot: this game's ledger slot) or one a write still waiting carries as doubt (add): such an id is
   settled, or doubt for good (a doubt stays doubt), so a later change of the same contents named by it would read as unsure at its load
   and its puts be made again on top of the stored ones. The save takes a new id instead (the one reserved at the change's mark) */
inline unsigned long long BoxSealMatch(const std::vector<BoxSeal>& seals, const std::vector<cooppar::ParityItem>& latest, int keptHave,
                                       const BoxLedger& kept, int slot, const std::vector<BoxLedgerEntry>& add)
{
    for (size_t i = seals.size(); i-- > 0;)
    {
        if (BoxSealSame(seals[i].items, latest) == 0) continue;
        const unsigned long long id = seals[i].id;
        if (keptHave != 0 && kept.line != 0ULL && slot >= 0 && BoxLedgerFind(kept, slot, id) != kBlAbsent) continue;
        int doubt = 0;
        for (size_t j = 0; j < add.size(); ++j)
            if (add[j].slot == slot && BlId(add[j].id) == id && (add[j].id & kBlDoubt) != 0) doubt = 1;
        if (doubt == 0) return id;
    }
    return 0ULL;
}
/* id sealed with the box as it stood at the save (items). 1 = a new seal; 0 = already sealed, or no id. At the cap the oldest seal leaves
   into *gone (its id is carried as doubt) */
inline int BoxSealAdd(std::vector<BoxSeal>* seals, unsigned long long id, const std::vector<cooppar::ParityItem>& items, std::vector<unsigned long long>* gone)
{
    if (seals == 0 || BlId(id) == 0ULL) return 0;
    for (size_t i = 0; i < seals->size(); ++i) if ((*seals)[i].id == BlId(id)) return 0;
    while (seals->size() >= kBoxSealMax) { if (gone != 0) gone->push_back(seals->front().id); seals->erase(seals->begin()); }
    BoxSeal n; n.id = BlId(id); n.items = items;
    seals->push_back(n);
    return 1;
}
/* id into the write's entries as doubt (an exact entry of it is taken back) */
inline void BoxSealDoubt(std::vector<BoxLedgerEntry>* add, int slot, unsigned long long id)
{
    if (add == 0 || slot < 0 || BlId(id) == 0ULL) return;
    (void)BoxLedgerEntryDrop(add, slot, id);
    (void)BoxLedgerEntrySet(add, slot, BlId(id) | kBlDoubt);
}
/* THE SEALS A WRITE CARRIES (written: the box the write carries; slot: this game's ledger slot; keptHave / kept: the kept record's
   ledger). A seal the kept ledger carries leaves (its entry too: that line holds it as it is). Every other is in the write's entries -
   exact when the write is the box as it stood at its save, doubt otherwise; an entry already doubt stays doubt. */
inline void BoxSealCarry(std::vector<BoxSeal>* seals, const std::vector<cooppar::ParityItem>& written, int keptHave, const BoxLedger& kept, int slot,
                         std::vector<BoxLedgerEntry>* add)
{
    if (seals == 0 || add == 0 || slot < 0) return;
    size_t k = 0;
    for (size_t i = 0; i < seals->size(); ++i)
    {
        const unsigned long long id = (*seals)[i].id;
        if (keptHave != 0 && kept.line != 0ULL && BoxLedgerFind(kept, slot, id) != kBlAbsent) { (void)BoxLedgerEntryDrop(add, slot, id); continue; }
        int doubtNow = 0;
        for (size_t j = 0; j < add->size(); ++j)
            if ((*add)[j].slot == slot && BlId((*add)[j].id) == id && ((*add)[j].id & kBlDoubt) != 0) doubtNow = 1;
        if (doubtNow == 0)
        {
            if (BoxSealSame((*seals)[i].items, written) != 0) (void)BoxLedgerEntrySet(add, slot, id);
            else BoxSealDoubt(add, slot, id);
        }
        if (k != i) (*seals)[k] = (*seals)[i];
        ++k;
    }
    seals->resize(k);
}
/* a part's ask signature: everything that names it but its quantity, so an ask of the same part handed over later (with more or fewer
   units) carries the id a save already holds. "" = an item that cannot be encoded */
inline std::string BoxJrnAskSig(const BoxJrnDelta& d)
{
    std::string k;
    if (BoxJrnKey(d.row, d.inPack == 0 ? 1 : 0, &k) == 0) return std::string();
    char n[96];
    std::sprintf(n, "\x01%d\x01%d,%d\x01%d\x01%d,%d\x01", d.dir, d.row.x, d.row.y, d.inPack, d.packX, d.packY);
    return k + n + d.row.section + "\x01" + d.packSection + "\x01" + d.packSid;
}
/* each part with no ask id takes its signature's (*idOf, one per box's change), a new one (BoxChangeIdNext) when it has none */
inline void BoxAskIdsAssign(std::map<std::string, unsigned long long>* idOf, std::vector<BoxJrnDelta>* d, unsigned long long* last, unsigned long long nowMs)
{
    if (idOf == 0 || d == 0) return;
    for (size_t i = 0; i < d->size(); ++i)
    {
        if ((*d)[i].askId != 0ULL) continue;
        const std::string sig = BoxJrnAskSig((*d)[i]);
        if (sig.empty()) continue;
        std::map<std::string, unsigned long long>::const_iterator f = idOf->find(sig);
        if (f != idOf->end()) { (*d)[i].askId = f->second; continue; }
        const unsigned long long id = BoxChangeIdNext(last, nowMs);
        (*idOf)[sig] = id;
        (*d)[i].askId = id;
    }
}
/* a row's characters at the load (named: its save-stable ids; mapped: those loaded now; waited: the row's wait for them is over):
   kWhoGo - undo on the mapped ones; kWhoWait - wait for the others; kWhoDrop - none of its own characters (never another character):
   dropped, counted and logged */
const int kWhoWait = 0, kWhoGo = 1, kWhoDrop = 2;
inline int BoxOwedWhoUse(int named, int mapped, int waited)
{
    if (named <= 0) return kWhoDrop;
    if (mapped >= named) return kWhoGo;
    if (waited == 0) return kWhoWait;
    return (mapped > 0) ? kWhoGo : kWhoDrop;
}
/* THE ROW SETTLED BY THE STORED RECORD'S LEDGER (ledHave 0: no record known, or one without a ledger; graceOver: an ask of this game
   still on its way would have been served by now). *left: the parts of the change left on the character; *how, by part: kOpStored (the
   record holds it), kOpUndo (it does not: undone exactly), kOpUnsure (cannot be known). 1 = settled; 0 = wait (a part's ask may still be
   served) */
const int kOpStored = 0, kOpUndo = 1, kOpUnsure = 2;
inline int BoxOwedResolve(const BoxOwedRow& r, int ledHave, const BoxLedger& led, int graceOver, std::vector<BoxJrnDelta>* left, std::vector<int>* how)
{
    if (left == 0 || how == 0) return 0;
    left->clear(); how->clear();
    if (r.decided != 0) { *left = r.ifNone; how->assign(left->size(), kOpUndo); return 1; }
    const int have = (ledHave != 0 && led.line != 0ULL) ? 1 : 0;
    const unsigned long long fl = (have != 0) ? BoxLedgerFloor(led, r.slot) : 0ULL;
    int sure = (have != 0 && r.slot >= 0 && r.line != 0ULL && led.line == r.line && (led.flags & kBlFlagCapped) == 0) ? 1 : 0;
    int found = -1, doubt = 0;
    for (size_t i = r.ids.size(); i-- > 0;)
    {
        const int f = (have != 0) ? BoxLedgerFind(led, r.slot, r.ids[i]) : kBlAbsent;
        if (f == kBlExact) { found = (int)i; break; }
        if (f == kBlDoubtful) doubt = 1;
        if (BlId(r.ids[i]) <= fl) sure = 0;
    }
    const std::vector<BoxJrnDelta>& d = (found >= 0 && (size_t)found < r.ifStored.size()) ? r.ifStored[(size_t)found] : r.ifNone;
    int wait = 0;
    for (size_t k = 0; k < d.size(); ++k)
    {
        const unsigned long long a = d[k].askId;
        const int fa = (have != 0 && a != 0ULL) ? BoxLedgerFind(led, r.slot, a) : kBlAbsent;
        int o = kOpUndo;
        if (fa == kBlExact) o = kOpStored;
        else if (fa == kBlDoubtful || doubt != 0 || sure == 0 || (a == 0ULL && r.ids.empty()) || (a != 0ULL && BlId(a) <= fl)) o = kOpUnsure;
        else if (a != 0ULL && graceOver == 0) wait = 1;
        left->push_back(d[k]);
        how->push_back(o);
    }
    if (wait != 0) { left->clear(); how->clear(); return 0; }
    return 1;
}
/* what a settled part does on the character (coopbrec::BoxJrnEnding's act): not stored - as an unsent ask's ending; unsure - a take
   nothing, a put made again; stored - nothing */
inline int BoxOwedAct(int dir, int how)
{
    if (how == kOpUndo) return BoxJrnEnding(dir, kBjaUnsent);
    if (how == kOpUnsure) return (dir == kBjPut) ? kBjDoRemake : kBjDoNothing;
    return kBjDoNothing;
}
/* does the stored record (rec: the record the row was settled against) show this put? Its item at the put's cell - the same item, at least
   as many units as the box held there at the save. A take, or a part inside a pack, is never judged from the contents: 0 */
inline int BoxOwedRecordShows(const BoxJrnDelta& d, const std::vector<cooppar::ParityItem>& rec)
{
    if (d.dir != kBjPut || d.inPack != 0) return 0;
    std::string want, have;
    if (BoxJrnIdentity(d.row, &want) == 0) return 0;
    for (size_t i = 0; i < rec.size(); ++i)
    {
        const cooppar::ParityItem& r = rec[i];
        if (r.section != d.row.section || r.x != d.row.x || r.y != d.row.y || r.quantity < d.row.quantity) continue;
        if (BoxJrnIdentity(r, &have) != 0 && have == want) return 1;
    }
    return 0;
}
/* UNSURE IS JUDGED PER PART. A row reads unsure as a whole when its id is doubt (a later write of the box differed from the box at the
   save) - but only some parts may differ. Each unsure put the stored record shows at its cell (BoxOwedRecordShows) is stored: the box
   holds it, so it is not made again on the character; an unsure put the record does not show is still made again, a take still left on
   the character (never taken back on a guess). The count made stored */
inline int BoxOwedByPart(const std::vector<BoxJrnDelta>& left, std::vector<int>* how, const std::vector<cooppar::ParityItem>& rec)
{
    if (how == 0 || how->size() != left.size()) return 0;
    int n = 0;
    for (size_t k = 0; k < left.size(); ++k)
        if ((*how)[k] == kOpUnsure && BoxOwedRecordShows(left[k], rec) != 0) { (*how)[k] = kOpStored; ++n; }
    return n;
}
inline void BoxOwedPutF(std::vector<char>* b, float f) { unsigned int u = 0; std::memcpy(&u, &f, 4); BrPutU32(b, u); }
inline bool BoxOwedPutDeltas(std::vector<char>* b, const std::vector<BoxJrnDelta>& d)
{
    if (d.size() > kBoxOwedMaxDeltas) return false;
    BrPutU32(b, (unsigned int)d.size());
    for (size_t k = 0; k < d.size(); ++k)
    {
        if (d[k].packSection.size() > kBoxOwedMaxStr || d[k].packSid.size() > kBoxOwedMaxStr) return false;
        cooppar::ParityBox m;
        m.status = cooppar::kParityStatusContents; m.key = "d"; m.items.push_back(d[k].row); m.digest = cooppar::ParityDigestOf(m.items);
        std::vector<char> body;
        if (!cooppar::EncodeParityBox(&body, m)) return false;
        BrPutU64(b, d[k].askId);
        BrPutU32(b, (unsigned int)d[k].dir); BrPutU32(b, (unsigned int)d[k].quantity); BrPutU32(b, (unsigned int)d[k].inPack);
        BrPutStr(b, d[k].packSection); BrPutU32(b, (unsigned int)d[k].packX); BrPutU32(b, (unsigned int)d[k].packY); BrPutStr(b, d[k].packSid);
        BrPutU32(b, (unsigned int)body.size());
        b->insert(b->end(), body.begin(), body.end());
    }
    return true;
}
inline bool BoxOwedGetDeltas(const char* p, size_t size, size_t* off, std::vector<BoxJrnDelta>* out)
{
    unsigned int n = 0;
    if (!BrGet4(p, size, off, &n) || n > kBoxOwedMaxDeltas) return false;
    out->clear();
    for (unsigned int k = 0; k < n; ++k)
    {
        BoxJrnDelta d;
        unsigned int dir = 0, q = 0, ip = 0, px = 0, py = 0, len = 0;
        if (!BrGet8(p, size, off, &d.askId) || !BrGet4(p, size, off, &dir) || !BrGet4(p, size, off, &q) || !BrGet4(p, size, off, &ip)
            || !BrGetStr(p, size, off, kBoxOwedMaxStr, &d.packSection) || !BrGet4(p, size, off, &px) || !BrGet4(p, size, off, &py)
            || !BrGetStr(p, size, off, kBoxOwedMaxStr, &d.packSid) || !BrGet4(p, size, off, &len))
            return false;
        if ((dir != (unsigned int)kBjTake && dir != (unsigned int)kBjPut) || ip > 1u || len > size - *off) return false;
        cooppar::ParityBox m;
        if (cooppar::DecodeParityBox(p + *off, len, &m) != cooppar::kParityDecodeOk || m.items.size() != 1) return false;
        *off += len;
        d.dir = (int)dir; d.quantity = (int)q; d.inPack = (int)ip; d.packX = (int)px; d.packY = (int)py; d.row = m.items[0];
        out->push_back(d);
    }
    return true;
}
/* marker | format | u32 rows | per row: str id | str key | 3 x f32 pos | u32 n | n x (str where | str section) | u32 slot | u64 line |
   u32 decided | u32 g | g x (u64 id | deltas) | deltas (ifNone); deltas: u32 n | n x (u64 ask id | u32 dir | u32 quantity | u32 inPack |
   str packSection | u32 packX | u32 packY | str packSid | u32 len | the item as a one-item PARITY_BOX). false = nothing usable (a cap
   passed, an item not encodable) */
inline bool EncodeBoxOwed(const std::vector<BoxOwedRow>& rows, std::vector<char>* out)
{
    if (out == 0) return false;
    out->clear();
    if (rows.size() > kBoxOwedMaxRows) return false;
    std::vector<char> b;
    BrPutU32(&b, kBoxOwedMarker); BrPutU32(&b, kBoxOwedFormat); BrPutU32(&b, (unsigned int)rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const BoxOwedRow& r = rows[i];
        if (r.id.size() > kBoxOwedMaxStr || r.key.size() > kBoxOwedMaxStr || r.who.size() > kBoxOwedMaxWho || r.ids.size() > kBoxOwedMaxIds
            || r.ifStored.size() != r.ids.size()) return false;
        BrPutStr(&b, r.id); BrPutStr(&b, r.key);
        BoxOwedPutF(&b, r.pos[0]); BoxOwedPutF(&b, r.pos[1]); BoxOwedPutF(&b, r.pos[2]);
        BrPutU32(&b, (unsigned int)r.who.size());
        for (size_t w = 0; w < r.who.size(); ++w)
        {
            if (r.who[w].where.size() > kBoxOwedMaxStr || r.who[w].section.size() > kBoxOwedMaxStr) return false;
            BrPutStr(&b, r.who[w].where); BrPutStr(&b, r.who[w].section);
        }
        BrPutU32(&b, (unsigned int)r.slot); BrPutU64(&b, r.line); BrPutU32(&b, r.decided != 0 ? 1u : 0u);
        BrPutU32(&b, (unsigned int)r.ids.size());
        for (size_t g = 0; g < r.ids.size(); ++g)
        {
            BrPutU64(&b, r.ids[g]);
            if (!BoxOwedPutDeltas(&b, r.ifStored[g])) return false;
        }
        if (!BoxOwedPutDeltas(&b, r.ifNone)) return false;
    }
    out->swap(b);
    return true;
}
/* 1 = *out holds the rows; 0 = not a file of this form (nothing kept) */
inline int DecodeBoxOwed(const char* p, size_t size, std::vector<BoxOwedRow>* out)
{
    if (out == 0) return 0;
    out->clear();
    if (p == 0) return 0;
    size_t off = 0;
    unsigned int mk = 0, fm = 0, n = 0;
    if (!BrGet4(p, size, &off, &mk) || mk != kBoxOwedMarker || !BrGet4(p, size, &off, &fm) || fm != kBoxOwedFormat || !BrGet4(p, size, &off, &n)
        || n > kBoxOwedMaxRows) return 0;
    std::vector<BoxOwedRow> rows;
    for (unsigned int i = 0; i < n; ++i)
    {
        BoxOwedRow r;
        unsigned int pu[3] = { 0, 0, 0 }, nw = 0, ng = 0, sl = 0, dc = 0;
        if (!BrGetStr(p, size, &off, kBoxOwedMaxStr, &r.id) || !BrGetStr(p, size, &off, kBoxOwedMaxStr, &r.key)
            || !BrGet4(p, size, &off, &pu[0]) || !BrGet4(p, size, &off, &pu[1]) || !BrGet4(p, size, &off, &pu[2]) || !BrGet4(p, size, &off, &nw)
            || nw > kBoxOwedMaxWho) return 0;
        for (int k = 0; k < 3; ++k) std::memcpy(&r.pos[k], &pu[k], 4);
        for (unsigned int w = 0; w < nw; ++w)
        {
            BoxOwedWho o;
            if (!BrGetStr(p, size, &off, kBoxOwedMaxStr, &o.where) || !BrGetStr(p, size, &off, kBoxOwedMaxStr, &o.section)) return 0;
            r.who.push_back(o);
        }
        if (!BrGet4(p, size, &off, &sl) || !BrGet8(p, size, &off, &r.line) || !BrGet4(p, size, &off, &dc) || dc > 1u || !BrGet4(p, size, &off, &ng)
            || ng > kBoxOwedMaxIds) return 0;
        r.slot = (int)sl; r.decided = (int)dc;
        for (unsigned int g = 0; g < ng; ++g)
        {
            unsigned long long id = 0ULL;
            std::vector<BoxJrnDelta> d;
            if (!BrGet8(p, size, &off, &id) || !BoxOwedGetDeltas(p, size, &off, &d)) return 0;
            r.ids.push_back(id);
            r.ifStored.push_back(d);
        }
        if (!BoxOwedGetDeltas(p, size, &off, &r.ifNone)) return 0;
        rows.push_back(r);
    }
    if (off != size) return 0;
    out->swap(rows);
    return 1;
}
/* 1 = the file beside a save is that save's: written (fileFt, FILETIME) at or after its quick.save (saveFt); 0 = none, or an earlier
   save's left behind */
inline int BoxOwedFileCurrent(unsigned long long fileFt, unsigned long long saveFt)
{
    return (fileFt != 0ULL && saveFt != 0ULL && fileFt >= saveFt) ? 1 : 0;
}
/* THE COPY IN THE RECORDS STORE, by the save's folder and name: written at the save's request and again as its saveGame pre-hook notes
   its rows (before anything of the save is written), replaced by every request of that save, and removed once the file beside the save
   is written - so a game that ends between the save
   and that file (a crash) still has its rows. It carries the request's time (reqFt, FILETIME): the copy is that save's when the save's
   quick.save was written at or after it (a later request of the save would have replaced it). marker 'BJOC' | u32 lo | u32 hi | the
   rows' bytes (none: no rows) */
const unsigned int kBoxOwedCopyMarker = 0x434F4A42u;   /* 'BJOC' */
inline std::string EncodeBoxOwedCopy(unsigned long long reqFt, const std::string& rows)
{
    std::vector<char> b;
    BrPutU32(&b, kBoxOwedCopyMarker); BrPutU32(&b, (unsigned int)(reqFt & 0xFFFFFFFFULL)); BrPutU32(&b, (unsigned int)(reqFt >> 32));
    std::string t(b.begin(), b.end());
    t += rows;
    return t;
}
inline int DecodeBoxOwedCopy(const char* p, size_t size, unsigned long long* reqFt, std::string* rows)
{
    size_t off = 0;
    unsigned int mk = 0, lo = 0, hi = 0;
    if (p == 0 || !BrGet4(p, size, &off, &mk) || mk != kBoxOwedCopyMarker || !BrGet4(p, size, &off, &lo) || !BrGet4(p, size, &off, &hi)) return 0;
    *reqFt = ((unsigned long long)hi << 32) | (unsigned long long)lo;
    rows->assign(p + off, size - off);
    return 1;
}
/* which file a loading save's rows come from: the file beside it when current, else the copy of its request when the save landed after
   that request, else none */
const int kOwUseNone = 0, kOwUseBeside = 1, kOwUseCopy = 2;
inline int BoxOwedChoose(unsigned long long besideFt, unsigned long long saveFt, int copyHave, unsigned long long copyReqFt)
{
    if (BoxOwedFileCurrent(besideFt, saveFt) != 0) return kOwUseBeside;
    if (copyHave != 0 && saveFt != 0ULL && copyReqFt != 0ULL && copyReqFt <= saveFt) return kOwUseCopy;
    return kOwUseNone;
}
/* the copy's key text: the save's folder as a full path, lower case (one save, however its folder was spelt) */
inline std::string BoxOwedCopyKey(const std::string& fullDir)
{
    std::string low = fullDir;
    for (size_t i = 0; i < low.size(); ++i)
    {
        const char c = low[i];
        if (c >= 'A' && c <= 'Z') low[i] = (char)(c - 'A' + 'a');
        else if (c == '/') low[i] = '\\';
    }
    while (low.size() > 3 && low[low.size() - 1] == '\\') low.erase(low.size() - 1);
    return low;
}
/* EACH WAITING SAVE'S ROWS: taken at its request (the notes so far, *n0), noted at each saveGame pre-hook under the next number (the
   state that save is written with), and at the landing the first note after its request not claimed by an earlier save - two saves
   waiting at once never share one note; none noted since its request: its request's rows stand. 1 = *bytes is the note */
struct BoxOwedGuard { std::map<long long, std::string> hist; long long n; BoxOwedGuard() : n(0) {} };
inline long long BoxOwedGuardAt(const BoxOwedGuard& g) { return g.n; }
inline void BoxOwedGuardNote(BoxOwedGuard* g, const std::string& bytes, size_t cap)
{
    ++g->n;
    g->hist[g->n] = bytes;
    while (g->hist.size() > cap) g->hist.erase(g->hist.begin());
}
/* the note a save waiting since n0 would claim now, without claiming it (its copy in the records store). 1 = *n / *bytes are it */
inline int BoxOwedGuardPeek(const BoxOwedGuard& g, long long n0, long long* n, std::string* bytes)
{
    std::map<long long, std::string>::const_iterator h = g.hist.upper_bound(n0);
    if (h == g.hist.end()) return 0;
    *n = h->first; *bytes = h->second;
    return 1;
}
inline int BoxOwedGuardClaim(BoxOwedGuard* g, long long n0, std::string* bytes)
{
    std::map<long long, std::string>::iterator h = g->hist.upper_bound(n0);
    if (h == g->hist.end()) return 0;
    bytes->swap(h->second);
    g->hist.erase(h);
    return 1;
}
/* THE LOADING SAVE'S ROWS: armed at the load's prepare, bound at the load call's end to the world it made (the world generation moves
   inside the engine's load); a load that made no world drops them. Taken only by the world they are bound to. */
struct BoxOwedArm { int armed; long gen; BoxOwedArm() : armed(0), gen(-1) {} };   /* gen -1: the load call still running */
inline void BoxOwedArmAtPrepare(BoxOwedArm* a) { a->armed = 1; a->gen = -1; }
/* 1 = dropped (the load made no world) */
inline int BoxOwedArmAtLoadEnd(BoxOwedArm* a, long genAtPrepare, long genNow)
{
    if (a->armed == 0 || a->gen != -1) return 0;
    if (genNow == genAtPrepare) { a->armed = 0; return 1; }
    a->gen = genNow;
    return 0;
}
/* 1 = still to be taken by world worldGen (a save written before the take carries them) */
inline int BoxOwedArmBound(const BoxOwedArm& a, long worldGen) { return (a.armed != 0 && a.gen != -1 && a.gen == worldGen) ? 1 : 0; }
/* 1 = world worldGen takes them now (then disarmed); rows bound to another world are dropped */
inline int BoxOwedArmTake(BoxOwedArm* a, long worldGen)
{
    if (a->armed == 0 || a->gen == -1) return 0;
    const int take = (a->gen == worldGen) ? 1 : 0;
    a->armed = 0; a->gen = -1;
    return take;
}

}   /* namespace coopbrec */
