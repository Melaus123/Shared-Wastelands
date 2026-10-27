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
 *     Format 2 (kPrFormatLive) adds a third part, u32 length + the LIVE bytes - a power piece's own live values:
 *       u32 flags (bit 0 switch on +0x3B5, bit 1 gets battery +0x3B4) | f32 battery level (+0x3C4) | u32 n 0..kPrLiveMaxStocks |
 *       n x f32 input stock (+0x488, stride 0x20, stock +0) - every value in [0, kPrLiveValueLimit] | T-1047 step 2b: with flags
 *       bit 2 (kPrLiveFlagBar) a production machine's f32 bar *(+0x448) in [0, kPrLiveBarLimit] | i32 work state +0x468 0..2 (2 working,
 *       1 blocked); a LIVE part without bit 2 leaves the bar NOT KNOWN (nothing is written to it). With flags bit 3
 *       (kPrLiveFlagDoors) a built piece's doors follow: u32 n 0..kPrLiveMaxDoors, then per door (its index in the piece's door
 *       list) f32 health (+0x164) in [0, kPrLiveValueLimit] | u32 lock level 0..100 | u32 flags (bit 0 door broken +0x3EC, bit 1
 *       lock broken +0x21, bit 2 has a lock); a part without bit 3 leaves the doors NOT KNOWN. Flags bit 4 (kPrLiveFlagNoPower) marks
 *       a part with NO power values (a piece with doors and no power: bits 0-2 clear, the battery and the stock count zero) - nothing
 *       is written to or owed to a piece's power for it; a part without bit 4 (every part written before it) carries the piece's
 *       power values (switch, gets battery, battery, stocks, bar). T-1047 step 5 (world-server protocol 116): flags bit 5
       (kPrLiveFlagTown) adds, after everything else, u32 outpost id 1..townkeep::kOutpostIdMax - the outpost (the world server's
       id, townkeep.h) its owner's game links the piece to; a part without bit 5 leaves the link NOT KNOWN (a new outpost's pieces
       are seeded before its first report). An older peer refuses a part with bit 5 (an unknown bit). A format-1 record carries no LIVE
 *       part: its live values are NOT KNOWN (never "off"). A removal mark is always format 1.
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

const unsigned int kProtocol = 121;               /* the world-server protocol that carries piece records and PIECE_REFUSE (116: the LIVE part's outpost link, bit 5) */

const unsigned int kMsgPieceRefuse = 79u;         /* world server -> the writer: its piece record was not taken, and why */

const unsigned int kPrMarker = 0x31454350u;       /* 'P','C','E','1' little-endian */
const unsigned int kPrFormat = 1u;                /* a record with no LIVE part */
const unsigned int kPrFormatLive = 2u;            /* a live piece's record with its LIVE part (switch, gets battery, battery, input stocks) */
const unsigned int kPrLiveMaxStocks = 16u;        /* a piece's input records carried (powerwire.h kPowerMaxStocks) */
const unsigned int kPrLiveFlagOn = 1u, kPrLiveFlagGets = 2u, kPrLiveFlagBar = 4u, kPrLiveFlagDoors = 8u, kPrLiveFlagNoPower = 16u, kPrLiveFlagTown = 32u, kPrLiveFlagsAll = 63u;   /* bit 2 = the bar and work state follow the stocks; bit 3 = the doors part follows them; bit 4 = no power values (else they are the piece's); bit 5 = the outpost link ends the part (T-1047 step 5) */
const unsigned int kPrLiveFlagsBeforeTown = 31u;  /* the bits a peer before T-1047 step 5 (protocol 115) reads: it refuses any other */
const unsigned int kPrTownIdMax = 0x7FFFFFFFu;    /* = townkeep::kOutpostIdMax: an outpost id 1..this */
const unsigned int kPrLiveMaxDoors = 16u;         /* a piece's doors carried, by index in its door list */
const unsigned int kPrLiveDoorBroken = 1u, kPrLiveDoorLockBroken = 2u, kPrLiveDoorHasLock = 4u, kPrLiveDoorFlagsAll = 7u;   /* a door's flags word */
const int kPrLiveLockLevelMax = 100;              /* a lock level carried: 0..100 */
const unsigned int kPrLiveMaxBytes = 12u + 4u * kPrLiveMaxStocks + 8u + 4u + 12u * kPrLiveMaxDoors + 4u;
const int kPieceLiveDoorBands = 20;               /* a door's health is written when it crosses a twentieth of its max */
const float kPrLiveBarLimit = 2.0f;               /* T-1047 step 2b: a production bar carried: [0, 2] */
const int kPrLiveStateMax = 2;                    /* T-1047 step 2b: a work state +0x468 carried: 0..2 (2 working, 1 blocked) */
const float kPrLiveValueLimit = 1000000.0f;       /* battery and stock: [0, 1e6] (powerwire.h's range) */
const float kPrLiveEps = 0.0001f;                 /* a level or stock change smaller than this is not a change */
const int kPieceLiveBatteryBands = 20;            /* the battery and each stock are written when they cross a twentieth (5%) of full */
const int kPieceLiveBarBandsWorked = 20;          /* T-1047 step 2b: the bar is written when it crosses a twentieth while a worker worked the
                                                     machine in the last coopfarm::kMineStateWorkedMs (the MINE_STATE window) */
const int kPieceLiveBarBandsIdle = 4;             /* T-1047 step 2b: else when it crosses a quarter */
const unsigned int kPieceLiveStateHoldMs = 1000u; /* T-1047 step 2b review fold 2: a work-state-only change is written once the state has
                                                     held this long - MINE_STATE's own gap (farmwire.h kMineStateGapMs) */
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
/* a piece's own live values (the LIVE part); have 0 = not known. hasPower 1 (the default) = the power values (on, gets, battery, stocks,
   bar) are the piece's; 0 = the part carries none (doors only - nothing is applied to the piece's power; written as flag bit 4). T-1047 step 2b: hasBar 1 = a production
   machine's bar and work state are known (bar, state); 0 = not known (a LIVE part without flag bit 2) */
struct PieceLive
{
    int have, hasPower, on, gets, n;
    float battery;
    float stock[kPrLiveMaxStocks];
    int hasBar, state;
    float bar;
    int hasDoors, nd;                         /* hasDoors 1 = the doors part is known: nd doors, by index in the piece's door list */
    float doorHealth[kPrLiveMaxDoors];
    int doorLevel[kPrLiveMaxDoors];
    unsigned int doorFlags[kPrLiveMaxDoors];  /* kPrLiveDoorBroken | kPrLiveDoorLockBroken | kPrLiveDoorHasLock */
    int hasTown;                              /* T-1047 step 5: 1 = the outpost link is known (flag bit 5) */
    unsigned int town;                        /* T-1047 step 5: the outpost id the piece is linked to (1..kPrTownIdMax; 0 with hasTown 0) */
    PieceLive() : have(0), hasPower(1), on(0), gets(0), n(0), battery(0.0f), hasBar(0), state(0), bar(0.0f), hasDoors(0), nd(0), hasTown(0), town(0u)
    {
        for (unsigned int i = 0; i < kPrLiveMaxStocks; ++i) stock[i] = 0.0f;
        for (unsigned int i = 0; i < kPrLiveMaxDoors; ++i) { doorHealth[i] = 0.0f; doorLevel[i] = 0; doorFlags[i] = 0u; }
    }
};
struct PieceBody
{
    std::string place, state;        /* a live piece: the pp.build row's PLACE and STATE bytes */
    unsigned int reason;             /* a removal mark: why the piece went */
    PieceLive live;                  /* a live piece's LIVE part (format 2); have 0 = none (format 1) */
    PieceBody() : reason(0) {}
};
/* 1 = the LIVE part carries the piece's power values (a part that exists without flag bit 4): only such a part is booked, queued, owed
   or written onto a piece's power - a doors-only part never is */
inline int PieceLivePowerKnown(const PieceLive& l) { return (l.have != 0 && l.hasPower != 0) ? 1 : 0; }

/* ---- the LIVE part's bytes ---- */
inline bool PrLiveValueOk(float v) { return v == v && v >= 0.0f && v <= kPrLiveValueLimit; }
/* T-1047 step 2b: a production bar in [0, kPrLiveBarLimit] (not a number, or infinite, refused) and a work state 0..kPrLiveStateMax */
inline bool PrLiveBarOk(float bar, int state) { return bar == bar && bar >= 0.0f && bar <= kPrLiveBarLimit && state >= 0 && state <= kPrLiveStateMax; }
/* a door carried: health in [0, kPrLiveValueLimit] (not a number refused), lock level 0..kPrLiveLockLevelMax, no unknown flag bit */
inline bool PrLiveDoorOk(float health, int level, unsigned int flags)
{
    return PrLiveValueOk(health) && level >= 0 && level <= kPrLiveLockLevelMax && (flags & ~kPrLiveDoorFlagsAll) == 0u;
}
/* not known is valid (no LIVE part); known: the switch and gets battery 0 / 1, at most kPrLiveMaxStocks stocks, every value in range;
   T-1047 step 2b: hasBar 0 / 1, and a known bar carries a bar and a work state in range (PrLiveBarOk). hasPower 0 / 1; a part with no
   power values carries them as zero (switch and gets battery off, battery 0, no stocks, no bar) */
inline bool PieceLiveValid(const PieceLive& l)
{
    if (l.have == 0) return true;
    if (l.hasPower != 0 && l.hasPower != 1) return false;
    if (l.hasPower == 0 && (l.on != 0 || l.gets != 0 || l.battery != 0.0f || l.n != 0 || l.hasBar != 0)) return false;
    if ((l.on != 0 && l.on != 1) || (l.gets != 0 && l.gets != 1) || (l.hasBar != 0 && l.hasBar != 1)) return false;
    if (l.n < 0 || l.n > (int)kPrLiveMaxStocks || !PrLiveValueOk(l.battery)) return false;
    for (int i = 0; i < l.n; ++i) if (!PrLiveValueOk(l.stock[i])) return false;
    if (l.hasBar != 0 && !PrLiveBarOk(l.bar, l.state)) return false;
    if (l.hasDoors != 0 && l.hasDoors != 1) return false;
    if (l.hasDoors != 0)
    {   /* known doors: at most kPrLiveMaxDoors, each in range */
        if (l.nd < 0 || l.nd > (int)kPrLiveMaxDoors) return false;
        for (int i = 0; i < l.nd; ++i) if (!PrLiveDoorOk(l.doorHealth[i], l.doorLevel[i], l.doorFlags[i])) return false;
    }
    if (l.hasTown != 0 && l.hasTown != 1) return false;
    if (l.hasTown != 0 ? (l.town == 0u || l.town > kPrTownIdMax) : (l.town != 0u)) return false;   /* T-1047 step 5: a known link names an outpost */
    return true;
}
inline void PrPutF32(std::vector<char>* b, float v) { unsigned int u = 0; std::memcpy(&u, &v, 4); coopbrec::BrPutU32(b, u); }
inline void PieceLivePut(std::vector<char>* out, const PieceLive& l)
{
    coopbrec::BrPutU32(out, (l.on != 0 ? kPrLiveFlagOn : 0u) | (l.gets != 0 ? kPrLiveFlagGets : 0u) | (l.hasBar != 0 ? kPrLiveFlagBar : 0u)
                            | (l.hasDoors != 0 ? kPrLiveFlagDoors : 0u) | (l.hasPower == 0 ? kPrLiveFlagNoPower : 0u) | (l.hasTown != 0 ? kPrLiveFlagTown : 0u));
    PrPutF32(out, l.battery);
    coopbrec::BrPutU32(out, (unsigned int)l.n);
    for (int i = 0; i < l.n; ++i) PrPutF32(out, l.stock[i]);
    if (l.hasBar != 0) { PrPutF32(out, l.bar); coopbrec::BrPutU32(out, (unsigned int)l.state); }   /* T-1047 step 2b: the bar and the work state */
    if (l.hasDoors != 0)
    {   /* the doors: their count, then each door's health, lock level and flags */
        const int nd = (l.nd < 0) ? 0 : (l.nd > (int)kPrLiveMaxDoors ? (int)kPrLiveMaxDoors : l.nd);   /* PrPutDoorsCount: the count written is the doors written */
        coopbrec::BrPutU32(out, (unsigned int)nd);
        for (int i = 0; i < nd; ++i)
        {
            PrPutF32(out, l.doorHealth[i]); coopbrec::BrPutU32(out, (unsigned int)l.doorLevel[i]); coopbrec::BrPutU32(out, l.doorFlags[i]);
        }
    }
    if (l.hasTown != 0) coopbrec::BrPutU32(out, l.town);   /* T-1047 step 5: the outpost link ends the part */
}
/* 1 = the bytes are exactly one LIVE part in range (into *out, have 1); 0 = refused, *out untouched. T-1047 step 2b: flag bit 2 adds the
   bar (f32) and the work state (i32) after the stocks; a part without it reads with the bar not known (hasBar 0); any other unknown bit,
   a missing or extra byte, or a bar or state out of range is refused. Flag bit 4 reads as hasPower 0, and such a part must carry no
   power values (PieceLiveValid); a part without it (an older part among them) reads as hasPower 1 */
inline int PieceLiveGet(const char* p, size_t size, PieceLive* out)
{
    if (p == 0) return 0;
    size_t at = 0;
    unsigned int fl = 0, n = 0;
    PieceLive l;
    if (!coopbrec::BrGet4(p, size, &at, &fl) || !coopbrec::BrGet4(p, size, &at, &l.battery) || !coopbrec::BrGet4(p, size, &at, &n)) return 0;
    const size_t tail = ((fl & kPrLiveFlagBar) != 0) ? 8u : 0u;
    const size_t doorsAt = ((fl & kPrLiveFlagDoors) != 0) ? 4u : 0u;   /* the doors' count; the doors themselves are measured once it is read */
    const size_t townAt = ((fl & kPrLiveFlagTown) != 0) ? 4u : 0u;     /* T-1047 step 5: the outpost link, last */
    if ((fl & ~kPrLiveFlagsAll) != 0 || n > kPrLiveMaxStocks) return 0;
    if (doorsAt == 0 ? (size - at != (size_t)n * 4u + tail + townAt) : (size - at < (size_t)n * 4u + tail + doorsAt + townAt)) return 0;
    for (unsigned int i = 0; i < n; ++i) if (!coopbrec::BrGet4(p, size, &at, &l.stock[i])) return 0;
    if (tail != 0)
    {
        unsigned int st = 0;
        if (!coopbrec::BrGet4(p, size, &at, &l.bar) || !coopbrec::BrGet4(p, size, &at, &st)) return 0;
        l.hasBar = 1; l.state = (st <= (unsigned int)kPrLiveStateMax) ? (int)st : -1;   /* -1: refused by PieceLiveValid below */
    }
    if (doorsAt != 0)
    {   /* the doors: exactly nd x (health, lock level, flags) to the end */
        unsigned int nd = 0;
        if (!coopbrec::BrGet4(p, size, &at, &nd) || nd > kPrLiveMaxDoors || size - at != (size_t)nd * 12u + townAt) return 0;
        for (unsigned int i = 0; i < nd; ++i)
        {
            unsigned int lv = 0;
            if (!coopbrec::BrGet4(p, size, &at, &l.doorHealth[i]) || !coopbrec::BrGet4(p, size, &at, &lv) || !coopbrec::BrGet4(p, size, &at, &l.doorFlags[i])) return 0;
            l.doorLevel[i] = (lv <= (unsigned int)kPrLiveLockLevelMax) ? (int)lv : -1;   /* -1: refused by PieceLiveValid below */
        }
        l.hasDoors = 1; l.nd = (int)nd;
    }
    if (townAt != 0)
    {   /* T-1047 step 5: the outpost link (0 or past kPrTownIdMax refused by PieceLiveValid below) */
        if (!coopbrec::BrGet4(p, size, &at, &l.town)) return 0;
        l.hasTown = 1;
    }
    if (at != size) return 0;
    l.have = 1; l.hasPower = (fl & kPrLiveFlagNoPower) != 0 ? 0 : 1; l.on = (fl & kPrLiveFlagOn) != 0 ? 1 : 0; l.gets = (fl & kPrLiveFlagGets) != 0 ? 1 : 0; l.n = (int)n;
    if (!PieceLiveValid(l)) return 0;
    if (out) *out = l;
    return 1;
}

/* 1 = the doors parts are the same: both not known, or both known with the same doors - lock levels and flags equal, health equal
   (eps 1: within kPrLiveEps) */
inline int PieceDoorsSameOf(const PieceLive& a, const PieceLive& b, int eps)
{
    if ((a.hasDoors != 0) != (b.hasDoors != 0)) return 0;
    if (a.hasDoors == 0) return 1;
    if (a.nd != b.nd) return 0;
    for (int i = 0; i < a.nd && i < (int)kPrLiveMaxDoors; ++i)
    {
        if (a.doorLevel[i] != b.doorLevel[i] || a.doorFlags[i] != b.doorFlags[i]) return 0;
        if (eps != 0 ? (std::fabs(a.doorHealth[i] - b.doorHealth[i]) > kPrLiveEps) : (a.doorHealth[i] != b.doorHealth[i])) return 0;
    }
    return 1;
}

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
    return (h.format == kPrFormat || (h.format == kPrFormatLive && (h.flags & kPrFlagRemoved) == 0)) && (h.flags & ~kPrFlagsAll) == 0 && PrSlotOk(h.writerSlot) && PrSlotOk(h.ownerSlot) && !MakePieceRecId(h.key).empty();
}
/* the body's own test against its header: a removal mark carries one known reason; a live piece a PLACE of the header's key and
   placement number and a STATE of the same key, each decoding whole */
inline bool PieceBodyValid(const PieceHeader& h, const PieceBody& b)
{
    if ((h.flags & kPrFlagRemoved) != 0)
        return b.reason == (unsigned int)coopbuild::kBuildReasonDismantled || b.reason == (unsigned int)coopbuild::kBuildReasonDestroyed;
    if (b.place.empty() || b.state.empty() || !PieceLiveValid(b.live)) return false;
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
    hh.format = ((hh.flags & kPrFlagRemoved) == 0 && b.live.have != 0) ? kPrFormatLive : kPrFormat;   /* a removal mark never carries LIVE */
    if (!PieceHeaderValid(hh) || !PieceBodyValid(hh, b)) return false;
    std::vector<char> body;
    if ((hh.flags & kPrFlagRemoved) != 0) coopbrec::BrPutU32(&body, b.reason);
    else
    {
        coopbrec::BrPutStr(&body, b.place); coopbrec::BrPutStr(&body, b.state);
        if (hh.format == kPrFormatLive)
        {
            std::vector<char> lv;
            PieceLivePut(&lv, b.live);
            coopbrec::BrPutStr(&body, std::string(lv.begin(), lv.end()));
        }
    }
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
    if (h.format != kPrFormat && h.format != kPrFormatLive) return kPrBadFormat;
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
        if (!coopbrec::BrGetStr(q, n, &at, kPrMaxBytes, &b.place) || !coopbrec::BrGetStr(q, n, &at, kPrMaxBytes, &b.state)) return kPrBadBody;
        if (h.format == kPrFormatLive)
        {   /* the LIVE part: whole and in range */
            std::string lv;
            if (!coopbrec::BrGetStr(q, n, &at, kPrLiveMaxBytes, &lv) || PieceLiveGet(lv.data(), lv.size(), &b.live) == 0) return kPrBadBody;
        }
        if (at != n) return kPrBadBody;
    }
    if (!PieceBodyValid(h, b)) return kPrBadBody;
    if (hOut) *hOut = h;
    if (bodyOut) { bodyOut->place.swap(b.place); bodyOut->state.swap(b.state); bodyOut->reason = b.reason; bodyOut->live = b.live; }
    return kPrOk;
}
inline const char* PieceDecodeWhy(int why)
{
    return why == kPrOk ? "ok" : why == kPrShort ? "cut short" : why == kPrBadMarker ? "not a piece record (wrong marker)"
         : why == kPrBadFormat ? "an unknown format" : why == kPrBadHeader ? "a header out of range"
         : why == kPrBadBody ? "a body that is not one whole PLACE and STATE (with one whole LIVE part in format 2, or one removal reason) for its key"
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
    if (bodyOut) { bodyOut->place.swap(b.place); bodyOut->state.swap(b.state); bodyOut->reason = b.reason; bodyOut->live = b.live; }
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
          kPeHelp = 8, kPeStarted = 9, kPeStopped = 10, kPeProgress = 11, kPeHit = 12, kPeSeed = 13, kPeFirst = 14, kPeAgain = 15, kPeLive = 16, kPeMax = 16;
inline const char* PieceEventName(int e)
{
    static const char* const n[] = { "none", "placed", "finished", "destroyed", "broken", "repaired", "removed", "mats", "help",
                                     "started", "stopped", "progress", "hit", "seed", "first", "again", "live" };
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
    return (event == kPeProgress || event == kPeStarted || event == kPeMats || event == kPeHelp || event == kPeAgain || event == kPeLive) ? 0 : 1;
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
/* another player's piece written here: 1 = the write's STATE part comes from the newest write this game sent (newestGen, newestRemoved:
   the last in the unanswered list) - a write carrying a hit's state is unanswered (flyHit, PieceFlyHit) and that newest write is a live
   record above the kept one. The newest write carries the hit's state or one made from it; the kept record's is the state before the
   hit, and a write taken after the hit's would put the hit back. 0 = from the kept record. */
inline int PieceHolderStateFromFly(int flyHit, int newestRemoved, unsigned int newestGen, unsigned int keptGen)
{
    return (flyHit != 0 && newestRemoved == 0 && newestGen > keptGen) ? 1 : 0;
}

/* ---- the answers to a game's own writes ----
   Each write a game sent and has no answer for is remembered with its generation, whether it is a removal mark, and the stamp it
   was sent with. RECORD_SEQ (taken) carries that stamp back and no generation; PIECE_REFUSE names the generation. One piece's
   answers come in the order its writes were sent, so a write older than the one answered gets no answer (the world server
   answers a repeat of the stored record with nothing) and is dropped with it. A link loss drops them all, with what the game
   knew the server stores: the old link's answers never come, and the next link's feed sends the stored record again. */
/* hit: the write is another player's piece's and carries this game's state after a hit (sent again after a link loss only while
   such a write is unanswered - PieceFlyHit) */
/* carry: a hit's state carried from a write dropped at the cap (carryGen / carryStamp: the newest such dropped write), answered by that
   write's own late answer (PieceFlyCarryAnswered) */
struct PieceFly
{
    unsigned int gen; int removed; long long stamp; int hit;
    int carry; unsigned int carryGen; long long carryStamp;
    PieceFly() : gen(0), removed(0), stamp(0), hit(0), carry(0), carryGen(0), carryStamp(0) {}
};
const size_t kPieceFlyCap = 16u;   /* unanswered writes remembered per piece (the oldest is dropped past it) */
inline void PieceFlyAdd(std::vector<PieceFly>* f, unsigned int gen, int removed, long long stamp, int hit = 0)
{
    if (f == 0) return;
    PieceFly e; e.gen = gen; e.removed = removed != 0 ? 1 : 0; e.stamp = stamp; e.hit = hit != 0 ? 1 : 0;
    if (f->size() >= kPieceFlyCap)
    {   /* the oldest goes; a hit's state it carried (its own or one carried to it) stays in flight on the next oldest, which names the
           dropped write so that write's own answer, arriving after it left the list, still lets the state go */
        const PieceFly old = f->front();
        f->erase(f->begin());
        if (old.hit != 0 || old.carry != 0)
        {
            PieceFly& to = f->empty() ? e : f->front();
            to.carry = 1; to.carryGen = old.gen; to.carryStamp = old.stamp;
        }
    }
    f->push_back(e);
}
/* 1 = an unanswered write carries a hit's state: the state after the hit is the one to send again if the link drops now. A hit's write
   answered (taken or refused) leaves the list, so a later LIVE-only write never sends that old state over a repair made since */
inline int PieceFlyHit(const std::vector<PieceFly>& f)
{
    for (size_t i = 0; i < f.size(); ++i) if (f[i].hit != 0 || f[i].carry != 0) return 1;
    return 0;
}
/* an answer that matched no write in the list (PieceFlyTaken / PieceFlyRefused) answers a write dropped at the cap: a hit's state carried
   from a dropped write at or below the answered one is answered with it, so a repair made since is never overwritten by the old hit
   after a link loss. A taken answer names its stamp (the carried write's own; a stamp is a second, so a dropped write of that same
   second counts as it), a refusal its generation (below every write still listed). */
inline void PieceFlyCarryAnswered(std::vector<PieceFly>* f, int byStamp, long long stamp, unsigned int gen)
{
    if (f == 0 || f->empty()) return;
    const unsigned int oldest = f->front().gen;
    for (size_t i = 0; i < f->size(); ++i)
    {
        PieceFly& e = (*f)[i];
        if (e.carry == 0) continue;
        const int answered = (byStamp != 0) ? (e.carryStamp == stamp ? 1 : 0) : ((gen >= e.carryGen && gen < oldest) ? 1 : 0);
        if (answered != 0) { e.carry = 0; e.carryGen = 0u; e.carryStamp = 0; }
    }
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
        if (k == f->size()) { PieceFlyCarryAnswered(f, 1, stamp, 0u); return 0; }   /* a dropped write's answer */
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
    if (k == f->size()) { PieceFlyCarryAnswered(f, 0, 0, sentGen); return 0; }   /* a dropped write's answer */
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

/* ---- reading the records: THE APPLY ----
   Once a piece's area is loaded here AND the world server has said the area's record list is complete, each kept record of the area
   is applied (and each record that arrives while the area stays loaded): for a piece's existence, its build state and its removal
   the record wins over the area snapshot and the game's own save. No record leaves the piece as loaded (never a removal). */
const unsigned int kGameProtocolRecordFeed = 173u;   /* the game-to-game protocol from which these three facts travel only in the records */
const unsigned int kGameProtocolLiveRecord = 174u;   /* the game-to-game protocol from which a player's power piece's live values travel only in its record (no POWER_STATE) */
const unsigned int kGameProtocolLiveBar = 175u;      /* T-1047 step 2b: the game-to-game protocol from which a player's power piece's production bar and work state travel in its record (no MINE_STATE for it) */
const unsigned int kGameProtocolDoors = 176u;        /* the game-to-game protocol from which a HIT names a door of its piece, or asks the piece's writer to add a door's repair */
const int kPaWait = 0, kPaMake = 1, kPaState = 2, kPaRemove = 3, kPaReown = 4, kPaNone = 5, kPaUnreadable = 6, kPaMax = 6;
inline const char* PieceApplyName(int a)
{
    static const char* const n[] = { "wait", "made", "state", "removed", "reowned", "none", "unreadable" };
    return (a >= 0 && a <= kPaMax) ? n[a] : "unknown";
}
/* areaLoaded: the area is loaded and walked here; listDone: its record list is complete; haveRecord / recordOk: a record is kept and
   decodes whole; removed: it is a removal mark; present: 1 the piece stands here (found at its key), 0 it does not, -1 not known (the
   make and remove roads search again before they act); pending: a placement of the key waits on the make road; ownerRight: the
   standing piece's owner is the record's; sameState: the standing piece already shows the record's state; samePlacement: the
   standing piece is the placement the removal mark ends (a different placement number = rebuilt since, left standing) */
inline int PieceApplyChoice(int areaLoaded, int listDone, int haveRecord, int recordOk, int removed, int present, int pending, int ownerRight,
                            int sameState, int samePlacement)
{
    if (areaLoaded == 0 || listDone == 0) return kPaWait;
    if (haveRecord == 0) return kPaNone;          /* no record: left as loaded - never a removal */
    if (recordOk == 0) return kPaUnreadable;      /* nothing is done with a record that does not read */
    if (removed != 0)
    {
        if (present == 1) return samePlacement != 0 ? kPaRemove : kPaNone;
        return (present < 0 || pending != 0) ? kPaRemove : kPaNone;   /* nothing stands or waits: nothing to take down */
    }
    if (present != 1) return pending != 0 ? kPaNone : kPaMake;   /* a placement (or re-make) of the key already waits: never a second one */
    if (ownerRight == 0) return kPaReown;
    return sameState != 0 ? kPaNone : kPaState;   /* only a real difference goes to a road */
}
/* 1 = the standing piece shows the record's state: complete, destroyed and broken alike, progress within a thousandth of needed, and
   the same delivered materials (materials not read here = not known to be the same) */
inline int PieceStateSame(float recP, float recN, int recC, int recD, int recB, int recNm, const float* recM,
                          float liveP, float liveN, int liveC, int liveD, int liveB, int liveNm, const float* liveM)
{
    if ((recC != 0) != (liveC != 0) || (recD != 0) != (liveD != 0) || (recB != 0) != (liveB != 0)) return 0;
    const float big = recN > liveN ? recN : liveN;
    const float tol = 0.001f * (big > 1.0f ? big : 1.0f);
    if (std::fabs(recP - liveP) > tol) return 0;
    if (liveNm < 0 || recNm != liveNm) return 0;
    for (int i = 0; i < recNm; ++i) if (recM == 0 || liveM == 0 || std::fabs(recM[i] - liveM[i]) > 0.001f) return 0;
    return 1;
}
/* 1 = an own piece's first look in this world writes nothing because a kept record outranks the save: one the area's holder wrote (its
   state is being applied), or a removal mark of this very placement (the piece is being taken down) - never a seed or a first-look
   update over it. Later real changes are written as usual. */
inline int PieceFirstLookYield(int event, int haveKept, int keptWriterMine, int keptRemoved, int samePlacement)
{
    if (event != kPeFirst || haveKept == 0) return 0;
    if (keptRemoved != 0) return samePlacement != 0 ? 1 : 0;
    return keptWriterMine == 0 ? 1 : 0;
}
/* the record's state is written onto every standing or made piece, a finished one included (its delivered materials); its build
   progress only onto an unfinished one - a finished piece's progress is its own (a wall's health) and is never written from here */
inline int PieceApplyStateGoes(int present, int made) { return (present == 1 || made != 0) ? 1 : 0; }
inline int PieceApplyProgressGoes(int recordComplete, int liveComplete) { return (recordComplete == 0 && liveComplete == 0) ? 1 : 0; }
/* an own pp.build row at a world load: a lone game lays it at once (unchanged); online it is held until its area's record list is
   complete, then its record decides - a live record makes the piece from the record (kPpFromRecord), a removal mark drops the row
   (kPpDrop), no record lays the row (kPpLay; the piece then seeds its record). A row whose key makes no record id is laid. */
const int kPpLay = 0, kPpHold = 1, kPpFromRecord = 2, kPpDrop = 3;
inline int PiecePpRowChoice(int lone, int haveId, int listDone, int haveRecord, int recordRemoved)
{
    if (lone != 0 || haveId == 0) return kPpLay;
    if (listDone == 0) return kPpHold;
    if (haveRecord == 0) return kPpLay;
    return recordRemoved != 0 ? kPpDrop : kPpFromRecord;
}

/* PcaApplyOne's inputs. present: an own piece - its current own row (the area's walk registered every own piece standing there);
   another player's piece with a copy row, or with none under a removal mark - the lookup of its key (found 1, a complete empty answer 0,
   else -1); otherwise -1 (the make road searches before it makes) */
inline int PieceApplyPresent(int own, int ownRow, int copyRow, int removed, int lookFound, int lookNone)
{
    if (own != 0) return ownRow != 0 ? 1 : 0;
    if (copyRow != 0 || removed != 0) return lookFound != 0 ? 1 : (lookNone != 0 ? 0 : -1);
    return -1;
}
/* is the standing (or waiting) piece the placement a removal mark ends? An own piece only when both placement numbers are known and
   equal (it is taken down by this game's engine); another player's piece unless both are known and differ (its REMOVE road checks again) */
inline int PieceApplySamePlacement(int own, unsigned int recPlacement, unsigned int livePlacement)
{
    if (own != 0) return (recPlacement != 0u && livePlacement != 0u && recPlacement == livePlacement) ? 1 : 0;
    return (recPlacement == 0u || livePlacement == 0u || recPlacement == livePlacement) ? 1 : 0;
}
/* a held pp.build row's last word: a live record whose body does not decode lays the row (nothing could be made from it); a removal
   mark of another (or an unknown) placement lays it too - only the row of the very placement it ends is retired */
inline int PiecePpRowFinal(int choice, int bodyOk, int samePlacement)
{
    if (choice == kPpFromRecord && bodyOk == 0) return kPpLay;
    if (choice == kPpDrop && samePlacement == 0) return kPpLay;
    return choice;
}

/* THE OWN-PIECE STATE RULE (owner decision 1067 a). ahead: the record is ahead of the standing piece - more progress, or finished while
   the piece is not (a finished piece is never behind a record). */
inline int PieceRecordAhead(float recProgress, int recComplete, float liveProgress, int liveComplete, float needed)
{
    if (liveComplete != 0) return 0;
    if (recComplete != 0) return 1;
    const float tol = 0.001f * (needed > 1.0f ? needed : 1.0f);
    return (recProgress > liveProgress + tol) ? 1 : 0;
}
/* an own standing piece: the area holder's record is written whole where it differs; this game's own record only at the area's first
   apply since the world loaded and only where it is ahead - its progress / finished part, the save's materials kept (the save rolled the
   inventory back: no item is duplicated). After a reconnect in the same world, or a record behind or equal, the live piece wins (its newer
   state is written by the normal change road). */
const int kPoNone = 0, kPoWhole = 1, kPoAheadKeepMats = 2;
inline int PieceOwnStateRule(int writerMine, int firstAfterLoad, int recordAhead, int stateSame)
{
    if (writerMine == 0) return stateSame != 0 ? kPoNone : kPoWhole;
    if (firstAfterLoad == 0 || recordAhead == 0) return kPoNone;
    return kPoAheadKeepMats;
}
/* the first look also yields to this game's own record where it is ahead of the save (the apply lays it) */
inline int PieceFirstLookYieldAhead(int event, int haveKept, int keptWriterMine, int keptRemoved, int samePlacement, int recordAhead)
{
    if (PieceFirstLookYield(event, haveKept, keptWriterMine, keptRemoved, samePlacement) != 0) return 1;
    return (event == kPeFirst && haveKept != 0 && keptRemoved == 0 && keptWriterMine != 0 && recordAhead != 0) ? 1 : 0;
}

/* THE SEEN GENERATION: per piece, the highest generation this game applied or wrote in this world. Every apply but an area's first
   since the world loaded takes only a record above it - a reconnect's feed re-sends records this game is already past. */
inline unsigned int PieceSeenGenNote(unsigned int seen, unsigned int gen) { return gen > seen ? gen : seen; }
inline int PieceSeenGenSkip(int firstAfterLoad, int haveSeen, unsigned int seenGen, unsigned int recGen)
{
    return (firstAfterLoad == 0 && haveSeen != 0 && recGen <= seenGen) ? 1 : 0;
}

/* the seen generation (kept only while the piece's area stays loaded here) skips only a state on a present piece: a missing piece is
   made and a removal mark over a present piece removes it whatever was seen */
inline int PieceSeenGenChoice(int choice, int seenSkip) { return (seenSkip != 0 && choice == kPaState) ? kPaNone : choice; }

/* a seen generation counts only within the load of its area it was noted in (each load of an area is counted, link or not) */
inline int PieceSeenGenValid(int haveSeen, unsigned int seenLoad, unsigned int loadNow) { return (haveSeen != 0 && seenLoad == loadNow) ? 1 : 0; }
/* a record that finds a placement of its key waiting is noted as seen only when the waiting entry now carries its state */
inline int PieceWaitNoteSeen(int carriedUpdated) { return carriedUpdated != 0 ? 1 : 0; }

/* ---- this game's own records, and an own piece made from its record ----
   The world server never sends a game its own writes back: the write an accept (RECORD_SEQ) answers - PieceFlyTaken's own match, the
   one with its stamp, else the oldest - is this game's own record of the piece, kept like an arriving one. -1 = no write matches. */
inline int PieceFlyMatch(const std::vector<PieceFly>& f, int hasStamp, long long stamp)
{
    if (f.empty()) return -1;
    if (hasStamp == 0) return 0;
    for (size_t k = 0; k < f.size(); ++k) if (f[k].stamp == stamp) return (int)k;
    return -1;
}
/* present, with an own piece looked up too: its row outlives an area unload, so a reloaded area that lacks the piece reads 0 (made from
   its record) - found 1, a complete empty answer 0, else -1 (the restore road searches again before it makes) */
inline int PieceApplyPresentLive(int own, int ownRow, int copyRow, int removed, int lookFound, int lookNone)
{
    if (own != 0) return ownRow == 0 ? 0 : (lookFound != 0 ? 1 : (lookNone != 0 ? 0 : -1));
    return PieceApplyPresent(own, ownRow, copyRow, removed, lookFound, lookNone);
}
/* an own piece made from its record (decision 1067). At the area's first apply after a world load the player's items came back from the
   mod's own continuous records (pp.squad), the same kind of snapshot as the piece's pp.build row: with that row at hand, a record ahead
   of it gives its progress / finished part with the row's materials (kPmRecordSaveMats), a record behind or equal gives the row's state
   (kPmSave); own rows read and none for the piece (placed and gone within the write gap - its materials left the inventory snapshot
   too): the record whole (kPmRecord). Own rows not read (own records off, or the row record unreadable): the items came from the game
   save - the record's progress / state with no materials, so no item is doubled (kPmRecordNoMats). A reload in the same session (no
   world load since the write): the record whole */
const int kPmRecord = 0, kPmRecordSaveMats = 1, kPmSave = 2, kPmRecordNoMats = 3;
inline int PieceOwnMakeMats(int firstAfterLoad, int rowsRead, int haveSaveRow, int recordAhead)
{
    if (firstAfterLoad == 0) return kPmRecord;
    if (rowsRead == 0) return kPmRecordNoMats;
    if (haveSaveRow == 0) return kPmRecord;
    return recordAhead != 0 ? kPmRecordSaveMats : kPmSave;
}
/* the held pp.build row is the piece's when its key matches - unless both placement numbers are known and differ (an older row carries
   no number: it is still the piece's row, and its materials are the snapshot's) */
inline int PieceHeldRowMatches(const std::string& rowKey, const std::string& key, unsigned int rowPlacement, unsigned int recPlacement)
{
    if (key.empty() || rowKey != key) return 0;
    return (rowPlacement != 0u && recPlacement != 0u && rowPlacement != recPlacement) ? 0 : 1;
}
/* why a make is asked again, and the event that asks it: own rows not handed yet - the restore point; an unfinished live search - the
   area's next walk (an arriving record of the piece asks it too) */
const int kPlNone = 0, kPlSearch = 1, kPlRows = 2;
const int kPlEvEdge = 1, kPlEvWalk = 2;
inline int PieceLookWhy(int present, int rowsState) { return rowsState < 0 ? kPlRows : (present < 0 ? kPlSearch : kPlNone); }
inline int PieceLookDue(int why, int ev) { return ((why == kPlRows && ev == kPlEvEdge) || (why == kPlSearch && ev == kPlEvWalk)) ? 1 : 0; }
/* an unfinished live search asks its area for one more walk (the walk's end asks the make again), at most kPieceLookRewalkMax times
   for one waiting make: a walk that moves is already walked again by the zone scan itself, so a search still unfinished after that
   many finished walks is not the area still loading - the make then waits for its record's next arrival or the area's next load */
const int kPieceLookRewalkMax = 3;
inline int PieceLookRewalk(int why, int rewalks) { return (why == kPlSearch && rewalks < kPieceLookRewalkMax) ? 1 : 0; }
/* a make on a live search that did not finish (present -1), or at a world load whose own rows are not handed yet (rowsState -1): not
   made on a guess - asked again */
inline int PieceApplyLookUnknown(int own, int choice, int present, int rowsState)
{   /* this game's own pieces only: another player's -1 means "not looked up here" and its placement road searches before it makes */
    if (own == 0) return 0;
    return (choice == kPaMake && (present < 0 || rowsState < 0)) ? 1 : 0;
}
/* an own record's state done on the STATE road: 1 = it landed - the piece is marked for a look (its first look when no baseline is
   booked); 2 = dropped - no look (the piece's old state would be written over a newer record); 0 = another game's copy */
inline int PieceStateDoneNote(int ownRec, int landed) { return ownRec == 0 ? 0 : (landed != 0 ? 1 : 2); }
/* kPmRecordNoMats on a record's STATE: every carried material written as 0, progress and the finished part kept */
inline void PieceOwnMakeNoMats(coopbuild::BuildMsg* st)
{
    for (unsigned int i = 0; i < coopbuild::kBuildMaxMats; ++i) st->mats[i] = 0.0f;
}
/* the state of a piece made from its record goes on the STATE road (every kind, walls included); a restore from the save keeps its own
   write (not for walls) */
const int kMsStateRoad = 0, kMsInline = 1, kMsNone = 2;
inline int PieceMadeStateRoad(int fromRecord, int hasState, int wall)
{
    if (hasState == 0) return kMsNone;
    if (fromRecord != 0) return kMsStateRoad;
    return wall != 0 ? kMsNone : kMsInline;
}
/* the first look yields only while the apply it waits for is still to come or still running (the area not applied yet, or a state,
   placement or removal of the piece queued): afterwards it books its baseline from the piece itself and writes where it differs */
inline int PieceFirstLookYieldPending(int yieldWanted, int areaApplied, int applyQueued)
{
    return (yieldWanted != 0 && (areaApplied == 0 || applyQueued != 0)) ? 1 : 0;
}

/* ---- THE LIVE PART: a power piece's own values, ONE WRITE PER CHANGE (no clock) ---- */
/* the band a value is in against full, of `bands` bands (under 1 counts as 1): 0 empty, 1..bands its parts, bands + 1 full (a full of 0
   or not a number counts as 1) - reaching full or empty is a band of its own. T-1047 step 2b: the battery and the stocks have
   kPieceLiveBatteryBands (PieceLiveBand); a production bar (full 1) its worked or idle count (PieceLiveBarBands) */
inline int PieceLiveBandOf(float v, float full, int bands)
{
    if (bands < 1) bands = 1;
    if (!(full > 0.0f) || !(full <= kPrLiveValueLimit)) full = 1.0f;
    if (!(v > kPrLiveEps)) return 0;
    if ((double)v >= (double)full * (1.0 - 1e-6)) return bands + 1;
    int b = 1 + (int)std::floor((double)v / (double)full * (double)bands);
    if (b > bands) b = bands;
    if (b < 1) b = 1;
    return b;
}
inline int PieceLiveBand(float v, float full) { return PieceLiveBandOf(v, full, kPieceLiveBatteryBands); }
/* 1 = v has left the band s was written in, by the band rule WITH HYSTERESIS: reaching empty or full from another band counts at once;
   any other move counts only once v is half a band (a fortieth of full) past the written band's edges - a level hovering on an edge,
   or a battery at full under a flickering load, writes nothing. Leaving full or empty takes the same half-band margin, and a value
   written at full leaves it as the top twentieth does (below full less one and a half twentieths), so a refilled stock's first fall
   out of full costs no write of its own. */
inline int PieceLiveBandLeftOf(float s, float v, float full, int bands)
{
    if (bands < 1) bands = 1;
    if (!(full > 0.0f) || !(full <= kPrLiveValueLimit)) full = 1.0f;
    const int bs = PieceLiveBandOf(s, full, bands), bv = PieceLiveBandOf(v, full, bands);
    if (bs == bv) return 0;
    if (bv == 0 || bv == bands + 1) return 1;   /* reached empty or full */
    const double w = (double)full / (double)bands, x = (double)v;
    double lo = 0.0, hi = 0.0;   /* the written band's edges (empty: 0 to 0) */
    if (bs >= bands) { lo = w * (double)(bands - 1); hi = (double)full; }
    else if (bs >= 1) { lo = w * (double)(bs - 1); hi = w * (double)bs; }
    return (x < lo - 0.5 * w || x >= hi + 0.5 * w) ? 1 : 0;
}
inline int PieceLiveBandLeft(float s, float v, float full) { return PieceLiveBandLeftOf(s, v, full, kPieceLiveBatteryBands); }
/* T-1047 step 2b: the production bar's band count - fine (kPieceLiveBarBandsWorked) while a worker worked the machine in the MINE_STATE
   window (farm.cpp MineWorkedRecently), else coarse (kPieceLiveBarBandsIdle) */
inline int PieceLiveBarBands(int worked) { return worked != 0 ? kPieceLiveBarBandsWorked : kPieceLiveBarBandsIdle; }
/* 1 = now is a change to write against what was written (sent): the switch or gets battery changed; the town grid's on-battery flag
   flipped (sentGrid / nowGrid -1 = not read); the battery left its written band of its capacity (PieceLiveBandLeft: full and empty at
   once, any other band half a band past its edge); the number of input records changed; a stock rose (a fuel item taken), reached 0,
   or left its written band of its peak the same way (peak[i]: the stock's value at its last rise; 0 = not known, full 1). Nothing
   known now (no power values: hasPower 0): no write; nothing written yet (or a write without power values): a write. */
inline int PieceLiveDue(const PieceLive& sent, const float* peak, float capacity, int sentGrid, const PieceLive& now, int nowGrid,
                        int barBands = kPieceLiveBarBandsIdle, unsigned int stateHeldMs = kPieceLiveStateHoldMs)
{
    if (now.have == 0 || now.hasPower == 0) return 0;
    if (sent.have == 0 || sent.hasPower == 0) return 1;
    if ((sent.on != 0) != (now.on != 0) || (sent.gets != 0) != (now.gets != 0)) return 1;
    if (sentGrid >= 0 && nowGrid >= 0 && (sentGrid != 0) != (nowGrid != 0)) return 1;
    if (now.hasBar != 0)
    {   /* T-1047 step 2b: the bar known now - its first known value, a work state change, or the bar leaving its written band of barBands
           (full 1; empty - an item made - and full at once, any other band half a band past its edge). A bar not known now writes nothing.
           T-1047 step 2b review fold 2: a work state change only once now.state has held kPieceLiveStateHoldMs (stateHeldMs: how long it
           has, PieceLiveStateHeld) - a flapping state writes nothing of its own */
        if (sent.hasBar == 0) return 1;
        if (sent.state != now.state && stateHeldMs >= kPieceLiveStateHoldMs) return 1;
        if (PieceLiveBandLeftOf(sent.bar, now.bar, 1.0f, barBands) != 0) return 1;
    }
    if (PieceLiveBandLeft(sent.battery, now.battery, capacity) != 0) return 1;
    if (sent.n != now.n) return 1;
    for (int i = 0; i < now.n && i < (int)kPrLiveMaxStocks; ++i)
    {
        const float s = sent.stock[i], v = now.stock[i];
        if (v - s > kPrLiveEps) return 1;
        if (!(v > kPrLiveEps) && s > kPrLiveEps) return 1;
        const float full = (peak != 0) ? peak[i] : 0.0f;
        if (PieceLiveBandLeft(s, v, full) != 0) return 1;
    }
    return 0;
}
/* each stock's peak after a write of now over prev: a stock new, risen, or with no peak takes its value; one that fell keeps its peak */
inline void PieceLivePeakNote(float* peak, const PieceLive& prev, const PieceLive& now)
{
    for (int i = 0; i < (int)kPrLiveMaxStocks; ++i)
    {
        if (i >= now.n) { peak[i] = 0.0f; continue; }
        if (prev.have == 0 || prev.hasPower == 0 || i >= prev.n || now.stock[i] - prev.stock[i] > kPrLiveEps || !(peak[i] > kPrLiveEps)) peak[i] = now.stock[i];
    }
}
/* 1 = both known and the same within kPrLiveEps (not known is never the same) */
inline int PieceLiveSame(const PieceLive& a, const PieceLive& b)
{
    if (a.have == 0 || b.have == 0) return 0;
    if ((a.hasPower != 0) != (b.hasPower != 0)) return 0;   /* the power values known on one side only */
    if ((a.on != 0) != (b.on != 0) || (a.gets != 0) != (b.gets != 0) || a.n != b.n) return 0;
    if (std::fabs(a.battery - b.battery) > kPrLiveEps) return 0;
    for (int i = 0; i < a.n && i < (int)kPrLiveMaxStocks; ++i) if (std::fabs(a.stock[i] - b.stock[i]) > kPrLiveEps) return 0;
    if ((a.hasBar != 0) != (b.hasBar != 0)) return 0;   /* T-1047 step 2b: the bar known on one side only */
    if (a.hasBar != 0 && (a.state != b.state || std::fabs(a.bar - b.bar) > kPrLiveEps)) return 0;
    if (PieceDoorsSameOf(a, b, 1) == 0) return 0;   /* the doors known on one side only, or different */
    return 1;
}
/* 1 = exactly the same LIVE part (both not known counts as the same) - the bytes a record would carry */
inline int PieceLiveEqual(const PieceLive& a, const PieceLive& b)
{
    if ((a.have != 0) != (b.have != 0)) return 0;
    if (a.have == 0) return 1;
    if ((a.hasPower != 0) != (b.hasPower != 0)) return 0;
    if (a.on != b.on || a.gets != b.gets || a.n != b.n || a.battery != b.battery) return 0;
    for (int i = 0; i < a.n && i < (int)kPrLiveMaxStocks; ++i) if (a.stock[i] != b.stock[i]) return 0;
    if ((a.hasBar != 0) != (b.hasBar != 0)) return 0;   /* T-1047 step 2b */
    if (a.hasBar != 0 && (a.bar != b.bar || a.state != b.state)) return 0;
    if (PieceDoorsSameOf(a, b, 0) == 0) return 0;
    return 1;
}
/* THE WRITER SIDE, at each power look. engineHere: this game runs the piece's engine steps (items.cpp ProdWriterDecide) - only it notes
   the values (another game puts its own changes back and asks); the record's one writer is still the send gate's (PieceWriterRule), so a
   mark this game may not write is held there. areaApplied 0: the area's records are not applied here yet - what is noted is forgotten
   (the record outranks the area save's values; its apply writes them first). buildBooked 0: an own piece whose first build look is not
   booked yet - nothing is marked (that first look writes the record whole, the LIVE part with it). No baseline yet: booked from the kept
   record when it holds LIVE values the band rule finds no change from, else marked. Then marked when the band rule finds a change since
   the last write (dueSnap) and since the last mark (dueMarked: a mark that was not written is made again only on a new change), and
   nothing is marked already (a marked write carries the latest values). */
const int kPlnNone = 0, kPlnForget = 1, kPlnBook = 2, kPlnMark = 3;
inline int PieceLiveNoteChoice(int engineHere, int areaApplied, int buildBooked, int haveSnap, int keptLive, int dueKept, int dueSnap, int dueMarked, int marked)
{
    if (engineHere == 0) return kPlnNone;
    if (areaApplied == 0) return kPlnForget;
    if (haveSnap == 0 && keptLive != 0 && dueKept == 0) return kPlnBook;
    if (buildBooked == 0 || marked != 0 || dueMarked == 0) return kPlnNone;
    if (haveSnap != 0 && dueSnap == 0) return kPlnNone;
    return kPlnMark;
}
/* THE APPLY SIDE: a record's LIVE part onto the piece - written now where it stands (kPlaNow; the engine writes only where it differs),
   owed to the piece a make brings (kPlaOwe), or nothing: a removal mark, an unreadable body, no power values (format 1, or a doors-only LIVE part: not known, never
   "off"), a generation this game is already past (seenSkip), or this game's own write while the area stayed loaded since the world
   loaded (the live piece is newer). A LIVE-only difference applies whatever the build state choice is. */
const int kPlaNone = 0, kPlaNow = 1, kPlaOwe = 2;
inline int PieceLiveApplyChoice(int removed, int bodyOk, int haveLive, int seenSkip, int writerMine, int firstAfterLoad, int choice)
{
    if (removed != 0 || bodyOk == 0 || haveLive == 0 || seenSkip != 0) return kPlaNone;   /* haveLive: the part carries power values */
    if (writerMine != 0 && firstAfterLoad == 0) return kPlaNone;
    if (choice == kPaUnreadable || choice == kPaRemove) return kPlaNone;
    return choice == kPaMake ? kPlaOwe : kPlaNow;
}
/* THE APPLY SIDE'S STATE of a player's piece. landed = the area's records are applied here (areaDone) and the record's LIVE values do not
   wait for the piece: not queued or owed (waits), not dropped at a full queue (dropped: still to come - the piece shows the area save's
   values until an apply lands or the area reloads), and the apply not given up (failed). Only a landed piece takes its switch from the
   record, so an ask is never made from the save's switch. */
inline int PieceLiveLandedRule(int areaDone, int waits, int dropped, int failed)
{
    return (areaDone != 0 && waits == 0 && dropped == 0 && failed == 0) ? 1 : 0;
}
/* 1 = a POWER_STATE naming the piece is dropped here: not landed. The record outranks: its apply writes the values, and a given-up apply
   is tried again at the piece's next record (PieceLiveNewRecord), so the record stays the piece's one road - the writer, which cannot
   know this game gave up, keeps writing records and never sends POWER_STATE for it */
inline int PieceLiveStateDropRule(int landed)
{
    return landed == 0 ? 1 : 0;
}
/* 1 = this game's notes of the piece's values are forgotten (PieceLiveNoteChoice's areaApplied 0): the record's LIVE values wait for the
   piece (waits) or were dropped at a full queue (dropped) - they are still to be written over it. A given-up apply (failed) never blocks
   the notes: nothing is written over the piece until its next record, so what the engine shows is what this game runs */
inline int PieceLivePendingRule(int waits, int dropped, int failed)
{
    (void)failed;
    return (waits != 0 || dropped != 0) ? 1 : 0;
}
/* a new record's LIVE part for the piece is queued (a generation this game has not applied): a given-up apply is tried again with a fresh
   run of faults (the run ended at the give-up); a run still under the cap goes on, so a piece that keeps faulting is given up again
   within the cap whatever its writer's pace. 1 = it had been given up */
inline int PieceLiveNewRecord(int* failed)
{
    const int was = (failed != 0 && *failed != 0) ? 1 : 0;
    if (failed != 0) *failed = 0;
    return was;
}
/* faulted LIVE applies in a row before the piece's apply is given up for this load */
const int kPieceLiveFaultCap = 5;
/* one LIVE apply's result (PwLiveApply: 1 written, 2 nothing to write, 0 no piece here now, -1 a fault) on the piece's run of faults *n:
   a landed apply ends the run, no piece leaves it, a fault adds one. 1 = the run is at cap: the apply is given up */
inline int PieceLiveFaultStep(int* n, int result, int cap)
{
    if (n == 0) return 0;
    if (result > 0) { *n = 0; return 0; }
    if (result == 0) return 0;
    if (*n < cap) ++*n;
    return (*n >= cap) ? 1 : 0;
}

/* ---- T-1047 step 2b: A PRODUCTION MACHINE'S BAR AND WORK STATE ON ITS RECORD ----
   The apply (PwLiveApply): 1 = a record's bar and work state are written onto the piece - the record carries them (recHasBar), the piece
   has an output record here (pieceHasBar), and this game does not run the machine's step (runnerHere 0: the runner's own tick moves its
   bar, never fought). */
inline int PieceLiveBarWrite(int recHasBar, int pieceHasBar, int runnerHere)
{
    return (recHasBar != 0 && pieceHasBar != 0 && runnerHere == 0) ? 1 : 0;
}
/* MINE_STATE's sender (farm.cpp MsSendRound): 1 = not sent - the piece's record carries its bar (carried: a power and a production class)
   and its live values ride that record from this game (recordRoad: build.cpp PwLiveRoad's record road). T-1047 step 2b review fold 4:
   and the record does carry it - the piece's row is on the record road and its last read had a bar (barRead: build.cpp PwRow.barRead);
   a bar refused at its read, or a piece with no row (the table full), keeps MINE_STATE, as nothing else delivers its bar */
inline int PieceBarStateSkip(int carried, int recordRoad, int barRead)
{
    return (carried != 0 && recordRoad != 0 && barRead != 0) ? 1 : 0;
}
/* MINE_STATE's receiver (farm.cpp FarmDrain): 1 = dropped - the POWER_STATE receiver's rule exactly (stateDrop: build.cpp PwStateDrop, a
   player's piece, this game linked, and its record's LIVE values not landed here - PieceLiveStateDropRule) for a piece whose record
   carries the bar; once landed, a MINE_STATE comes only from a runner whose record cannot carry the bar, and is applied */
inline int PieceBarStateDrop(int carried, int stateDrop)
{
    return (carried != 0 && stateDrop != 0) ? 1 : 0;
}

/* ---- T-1047 step 2b review folds ---- */
/* fold 2: the work state's hold clock - *have / *seen / *at: a state seen yet, the state last seen, and the tick it was first seen at.
   Returns how long `state` has held at `tick` (0 when it is new); unsigned arithmetic, so a tick count's wrap is a plain difference */
inline unsigned int PieceLiveStateHeld(int* have, int* seen, unsigned int* at, int state, unsigned int tick)
{
    if (have == 0 || seen == 0 || at == 0) return kPieceLiveStateHoldMs;
    if (*have == 0 || *seen != state) { *have = 1; *seen = state; *at = tick; }
    return tick - *at;
}
/* fold 3: 1 = the worker stopped at this look - a worker worked the machine in the MINE_STATE window at the last look (wasWorked) and
   not at this one */
inline int PieceLiveWorkStopped(int wasWorked, int worked)
{
    return (wasWorked != 0 && worked == 0) ? 1 : 0;
}
/* fold 3: 1 = the worker's stop writes now: its bar is known and the values differ from those written (sent; PieceLiveSame - not
   known counts as different), whatever the bands say */
inline int PieceLiveFinalDue(int stopped, const PieceLive& sent, const PieceLive& now)
{
    return (stopped != 0 && now.have != 0 && now.hasPower != 0 && now.hasBar != 0 && PieceLiveSame(sent, now) == 0) ? 1 : 0;
}
/* fold 1: 1 = the kept record's generation gen is newer than this game's own last write of the piece (haveSent / sentGen: moved by each
   write this game sends) and than the generation this game last took the bar from (takenGen) */
inline int PieceLiveNewerThanMine(int haveSent, unsigned int sentGen, unsigned int takenGen, unsigned int gen)
{
    return ((haveSent == 0 || gen > sentGen) && gen > takenGen) ? 1 : 0;
}
/* fold 1: 1 = a game that runs the machine (runnerHere) takes the record's bar and work state ONCE at a moment (moment: its runner answer
   turned 'here', or the piece's first apply in this load of its area) - the record carries the bar (recHasBar), the piece has an output
   record here (pieceHasBar), another game wrote the record (writerOther) and it is newer than this game's last write
   (PieceLiveNewerThanMine). Outside a moment the runner's own tick moves its bar - never fought (PieceLiveBarWrite) */
inline int PieceLiveBarTake(int moment, int runnerHere, int recHasBar, int pieceHasBar, int writerOther, int newerThanMine)
{
    return (moment != 0 && runnerHere != 0 && recHasBar != 0 && pieceHasBar != 0 && writerOther != 0 && newerThanMine != 0) ? 1 : 0;
}
/* fold 1: 1 = another player's piece's LIVE write goes on the box rule's answer at its last LIVE book or mark (wallLast) instead of asking
   it now - its area unloaded here (unload, no piece here to ask), or the writing left this game (handOff: asked now the box rule names the
   next runner, so the last values run here would never go). Never a hit's write (hit), and only a LIVE write (liveHolder). The world
   server's writer rule still judges the write */
inline int PieceLiveWallLast(int pieceHere, int hit, int liveHolder, int unload, int handOff, int wallLast)
{
    if (hit != 0 || liveHolder == 0 || wallLast == 0) return 0;
    return ((unload != 0 && pieceHere == 0) || handOff != 0) ? 1 : 0;
}

/* ---- A BUILT PIECE'S DOORS ON ITS RECORD ----
   Each door of a player's piece (its index in the piece's door list, filled from the template in order) carries its health, its lock's
   level, and its door broken / lock broken / has-a-lock flags in the LIVE part. The record's one writer writes them at each event that
   changes a door (its own hit, a repair, a hit or repair asked of it); every other game writes them onto its doors. */
typedef char PrDoorsMatchHit[(kPrLiveMaxDoors == (unsigned int)coopbuild::kBuildHitMaxDoor + 1u) ? 1 : -1];   /* a HIT names any door the record carries */
inline void PieceDoorsClear(PieceLive* l)
{
    if (l == 0) return;
    l->hasDoors = 0; l->nd = 0;
    for (unsigned int i = 0; i < kPrLiveMaxDoors; ++i) { l->doorHealth[i] = 0.0f; l->doorLevel[i] = 0; l->doorFlags[i] = 0u; }
}
/* the doors part of from into *to (the power values of *to kept) */
inline void PieceDoorsCopy(PieceLive* to, const PieceLive& from)
{
    if (to == 0) return;
    PieceDoorsClear(to);
    if (from.hasDoors == 0) return;
    to->hasDoors = 1; to->nd = (from.nd < 0) ? 0 : (from.nd > (int)kPrLiveMaxDoors ? (int)kPrLiveMaxDoors : from.nd);
    for (int i = 0; i < to->nd; ++i) { to->doorHealth[i] = from.doorHealth[i]; to->doorLevel[i] = from.doorLevel[i]; to->doorFlags[i] = from.doorFlags[i]; }
}
/* a door's health band against its max (0 or not a number counts as 1): 0 empty, 1..20 its twentieths, 21 full */
inline int PieceDoorBand(float health, float maxHealth) { return PieceLiveBandOf(health, maxHealth, kPieceLiveDoorBands); }
/* 1 = the doors now are a change to write against the doors last written (sent - the writer's own last write, or the stored record
   it booked): nothing known written; a door more or fewer; a door's broken, lock-broken or has-a-lock flag or its lock level changed;
   or a door's health crossed a twentieth of its max (maxHealth[i]). Doors not known now: no write. */
inline int PieceDoorsDue(const PieceLive& sent, const PieceLive& now, const float* maxHealth)
{
    if (now.hasDoors == 0) return 0;
    if (sent.hasDoors == 0 || sent.nd != now.nd) return 1;
    for (int i = 0; i < now.nd && i < (int)kPrLiveMaxDoors; ++i)
    {
        if (sent.doorFlags[i] != now.doorFlags[i] || sent.doorLevel[i] != now.doorLevel[i]) return 1;
        const float full = (maxHealth != 0) ? maxHealth[i] : 0.0f;
        if (PieceDoorBand(sent.doorHealth[i], full) != PieceDoorBand(now.doorHealth[i], full)) return 1;
    }
    return 0;
}
/* 1 = exactly the same doors (both not known counts as the same) */
inline int PieceDoorsSame(const PieceLive& a, const PieceLive& b) { return PieceDoorsSameOf(a, b, 0); }

/* ---- T-1047 STEP 5: THE OUTPOST LINK ON THE RECORD (owner 1043 / 1102 a) ----
   Every top-level piece's record names the outpost its owner's game links it to, by the world server's outpost id (townkeep.h; never a
   town handle - they differ between games - nor a centre). The writer is the record's one writer (the box rule); the owner's game
   writes its engine's link, any other writer keeps the stored one. Every other game links its copy to its own copy town of that
   outpost (baseshow.h: every game has one) - never over the owner's own engine link on the owner's game - BEFORE the record's LIVE
   power values are written onto the piece, and the relink's power cycle (a switched-on power piece switched off and on, to leave the
   old town's grid and join the new one) is never taken for a player's switch change. PieceLiveSame / PieceLiveEqual compare the power
   and doors values only: the link is written and compared by the steps below. */
inline void PieceTownSet(PieceLive* l, unsigned int id)
{
    if (l == 0) return;
    if (id == 0u || id > kPrTownIdMax) { l->hasTown = 0; l->town = 0u; return; }
    l->hasTown = 1; l->town = id;
}
inline unsigned int PieceTownOf(const PieceLive& l) { return (l.have != 0 && l.hasTown != 0) ? l.town : 0u; }
/* the link a write carries: on the owner's game (writerIsOwner) its engine's link when its outpost id is known (engineId != 0); else -
   another writer, or an outpost whose id is not known here yet - the stored record's (keptId; 0 = not known: none carried) */
inline unsigned int PieceTownWriteId(int writerIsOwner, unsigned int engineId, unsigned int keptId)
{
    return (writerIsOwner != 0 && engineId != 0u) ? engineId : keptId;
}
/* 1 = a write is marked for the link: this is the owner's game, it is the piece's writer now, its engine links the piece to a known
   outpost the stored record does not name (keptId) and no write of that link is marked already (markedId) */
inline int PieceTownMarkDue(int ownerHere, int writerHere, unsigned int engineId, unsigned int keptId, unsigned int markedId)
{
    return (ownerHere != 0 && writerHere != 0 && engineId != 0u && engineId != keptId && engineId != markedId) ? 1 : 0;
}
/* THE APPLY STEP for one piece's record on one game */
const int kTlNone = 0, kTlUnknown = 1, kTlOwn = 2, kTlInside = 3, kTlNoTown = 4, kTlSame = 5, kTlRelink = 6, kTlStepMax = 6;
inline const char* PieceTownStepName(int s)
{
    static const char* const n[] = { "none", "link not known", "own piece", "inside a building", "no copy town", "same", "relink" };
    return (s >= 0 && s <= kTlStepMax) ? n[s] : "unknown";
}
/* removed: a removal mark; ownerHere: the record's owner is this game's player (its engine's own link is the truth there); link: the
   record's outpost id (0 = not known); topLevel: the piece stands at the top level (1), inside a building (0: its host's town);
   townHere: this game holds a town for that outpost; inThatTown: the piece's town words are that town's */
inline int PieceTownApplyStep(int removed, int ownerHere, unsigned int link, int topLevel, int townHere, int inThatTown)
{
    if (removed != 0 || link == 0u) return kTlUnknown;
    if (ownerHere != 0) return kTlOwn;
    if (topLevel == 0) return kTlInside;
    if (townHere == 0) return kTlNoTown;
    return inThatTown != 0 ? kTlSame : kTlRelink;
}
/* T-1047 step 5 fold 1: the marked link stands only while its write is owed or sent and not answered (writeOwed / writeFlying); once
   the write is settled - taken, refused or lost - the mark is dropped (0), so the next look marks it again while the stored record
   still does not name the engine's link (a retry covered by the recurring look, not a one-shot mark) */
inline unsigned int PieceTownMarkStands(unsigned int markedId, int writeOwed, int writeFlying)
{
    return (writeOwed != 0 || writeFlying != 0) ? markedId : 0u;
}
/* T-1047 step 5 fold 1: THE RELINK'S POWER CYCLE, AS AN EVENT. BdBuildingSetTownPod reports what it did to a switched-on power piece:
   kCycleNone (not switched on, not a power piece, or off the network: nothing done), kCycleBack (switched off and on again inside the
   one call: the switch is where it was, so the power look sees no change and nothing is reset - a player's own flip noted before the
   relink keeps its ask), kCycleFailed (switched off, and switching it on again failed: it stays off). The step after it: */
const int kCycleNone = 0, kCycleBack = 1, kCycleFailed = 2;
const int kCycleStepNothing = 0, kCycleStepRecordSwitch = 1, kCycleStepLeftOff = 2;
/* recordPowerKnown: the piece's record carries its LIVE power values. A failed switch-on gets the record's switch written once
   (kCycleStepRecordSwitch: the record's LIVE values queued for the K2 drain, or written right after on that drain); with no record
   values it is left off and counted (kCycleStepLeftOff: the power look then sees the off as any switch change) */
inline int PieceTownCycleStep(int cycle, int recordPowerKnown)
{
    if (cycle != kCycleFailed) return kCycleStepNothing;
    return recordPowerKnown != 0 ? kCycleStepRecordSwitch : kCycleStepLeftOff;
}

}   /* namespace piecerec */
