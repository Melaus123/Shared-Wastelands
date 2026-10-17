#pragma once
/* THE PLAYERS' KEPT PIECES, LAID BACK INTO A RESET WORLD ONCE EACH (KEPT, world-server kind 69, protocol 95).
   A reset that keeps the players' buildings leaves kept-pieces.txt in the reset world (zonekeep.h): every piece a player owned, with
   its box contents. The world server keeps each row's state (kept-state.txt) and hands the work out; the games lay it. This header
   is the pure half both sides compile (the world server's store_main.cpp, the game's store.cpp and build code, the offline suite):
   the wire, the row's state bits and every decision about who may do what, so the two sides cannot hold different ideas of one row.

   - A ROW is one kept piece, numbered by its place in kept-pieces.txt (that file is never rewritten in a world). It has three PARTS:
     the PIECE (the building, wall or furniture itself), its ITEMS (the box contents; a piece that kept no items has this part done
     from the start) and the OWNER part (the owner's own game has found the piece and registered it as its own).
   - THE PIECE: only the game the world server's area map names as the holder of the row's sector may claim it.
   - THE ITEMS: the box writer lays them (boxowner.h: the owner's game while the owner is loaded there, else the area's holder). The
     GAME decides that; the world server gives the first claim to a game that holds the area or owns the piece.
   - THE OWNER PART: no claim. The owner's own game reports it once its own committed save holds the registration (LAID owner).
   - LAID: the claimer made the piece (made / present / handed back) or laid the items (laid), or could not (refused - the claim
     is handed back and the part is new again). A laid part is DONE when the world server holds a new record of that area holding it
     (zonekeep.h RecordHoldsPiece / RecordHoldsItems), or when the laying game says SETTLED: any save of that game after the lay has
     finished, a save after the lay wrote this area's record, and the world server's sequence echo of that write came back. The world
     server takes a SETTLED only for a lay it saw on its own run: a game on a new link reports its lay again (LAID) before its SETTLED.
   - A GAME LEAVES: its claims are handed back. A PIECE it laid that is not done yet is new again only when that game was the area's
     holder when it laid it (its save would have carried the record; the match before make keeps a second game from making it again);
     otherwise the piece is never offered again and only the area's record can close it. BOX CONTENTS a game laid are never offered
     again, whoever laid them (the player may empty the box before the lay is saved, and a second lay would then double the items):
     they stay laid until a record that holds them or that game's SETTLED closes them. Both marks (LaidNonWriter) are kept in
     kept-state.txt, with the game that laid the part (laidSeen), so a restart keeps them.
   - CLAIMED AGAIN: a laid part goes to its own layer once more (ClaimDecide kClaimAgain, under the same holder / owner rule) when
     that game asks: its laid-here record (written only once a save that holds a lay has finished) does not list the part, so the save
     it runs on does not hold it (a reload) and it lays the part once more. Handed back, refused or left, the part is laid again.
     Every LAID taken is written to kept-state.txt at once, with the piece's first how (a piece its first lay found standing is marked
     PRESENT in the row's flags, and no game lays its own state).
   - DOWN: ROWS (one sector's unfinished rows, at most 64, to a game whose AREAS report lists that sector for the first time on its
     link - never inside the WELCOME's opening push) and STATE (one row's state after any change, to every game; to the claimer alone
     for a refused claim).
   WIRE (little-endian, as every world-server message): u32 op, then
     CLAIM    u32 row, u32 part (piece / items)          RELEASE  u32 row, u32 part (piece / items)
     LAID     u32 row, u32 part, u32 how, str live key (0..47 bytes; the key of the live piece the game made or found, "" = none)
     SETTLED  u32 row, u32 part (piece / items)
     ROWS     i32 sector x, i32 sector y, u32 count 1..64, count x ROW
     STATE    u32 row, u32 flags, u32 claim piece, u32 claim items
     ROW      u32 row, u32 flags, u32 claim piece, u32 claim items, u32 owner slot, str key, str template, f32 x, f32 y (height), f32 z,
              f32 rot w, f32 rot x, f32 rot y, f32 rot z, i32 floor, str host key, f32 progress, u32 complete, u32 destroyed, u32 broken,
              OWN, u32 mats 0..64, mats x [i32 index, f32 count], u32 doors 0..32, doors x [i32 index, i32 state 0..3, u32 locked, u32
              wants lock, i32 lock level, i32 gate code, i32 owner slot (-1 = not a player's), f32 hardness (-1 = none kept), u32 lock
              broken], u32 items 0..512, items x ITEM
     OWN      the piece's own state: u32 has (zonekeep kHas* bits), str name, str interior layout name, str exterior layout name, u32
              pause, u32 gets battery, u32 power on, f32 battery, f32 production, i32 lock level, f32 lock hardness, u32 lock locked,
              f32 plant growth, f32 plant death, f32 plant start, i32 plants harvested, u32 inputs 0..16, inputs x [i32 index 0..15,
              f32 stock]
     ITEM     str template, str material, str company, str color, str section, str uniform, str crafter, u32 owner ticket kept
              (0 / 1), 5 x i32 the ticket's words (raw, as the record held them), f32 quality, f32 charges, i32 quantity, i32 level, i32 function, i32 unique, i32 slot x, i32 slot y, u32 inner 0..256, inner x ITEM (a pack's own
              items, each with its own in turn; an ITEM at depth zonekeep::kMaxPackDepth - a box item is depth 0 - has 0)
     A claim field is the holder's slot + 1 (0 = nobody); a laid part's is its layer's slot + 1 (0 = not known; ClaimFieldOf). str = u32 length, the bytes (no control characters): a key at most 47 bytes,
     any other text at most 128; the key and the templates are never empty.
   Header-only. No C++11. */
#include <string>
#include <vector>
#include <cstring>
#include "zonekeep.h"   /* the kept piece, its items, the position key and the sector */

namespace keptwire {

const unsigned kMsgKept = 69;
const unsigned kProtocol = 100;   /* the world-server protocol that carries KEPT */



const unsigned kOpClaim = 1, kOpRelease = 2, kOpLaid = 3, kOpSettled = 4, kOpRows = 5, kOpState = 6;
const unsigned kPartPiece = 1, kPartItems = 2, kPartOwner = 3;
const unsigned kHowMade = 1;              /* piece: made new */
const unsigned kHowPresent = 2;           /* piece: already there, owned as it should be - nothing made */
const unsigned kHowHandedBack = 3;        /* piece: an unowned matching town piece (a bought house) given back to its player, no charge */
const unsigned kHowItemsLaid = 4;         /* items: laid into the box */
const unsigned kHowItemsHas = 5;          /* items: the box already held an item of a kept template - nothing laid. The game does not
                                             report it (a box's kept items are laid in full once); HowFits still takes it */
const unsigned kHowOwnerRegistered = 6;   /* owner: the owner's game registered the piece as its own and a save of its own holds it */
const unsigned kHowRefused = 7;           /* piece / items: could not be done here - the claim is handed back */

/* a part's state: new (nobody works on it), claimed (one game may lay it), laid (laid, not yet in a saved record), done */
const unsigned kStNew = 0, kStClaimed = 1, kStLaid = 2, kStDone = 3;
/* FLAGS: bits 0-1 the piece's state, bits 2-3 the items' state, then the owner part and the two "never offered again" marks (a piece
   laid by a game that was not the area's holder; box contents laid by any game) */
const unsigned kFlagOwnerTook = 0x10, kFlagPieceLaidNonWriter = 0x20, kFlagItemsLaidNonWriter = 0x40;
const unsigned kFlagPiecePresent = 0x80;   /* the piece's first lay in this world found it standing: no game lays its own state */
const unsigned kFlagsKnown = 0xFF;

const size_t kMaxRows = 64;           /* rows in one ROWS */
const size_t kRowsBytes = 48 * 1024;  /* a ROWS stops growing past this many bytes (one row larger than it still goes alone) */
const size_t kStrMax = 128;
const size_t kKeyMax = (size_t)(poskey::kKeyCap - 1);
const size_t kMaxMats = 64;
const size_t kMaxItems = 512;
const size_t kMaxInner = 256;         /* a backpack's own items in one ITEM (the mod's pack cap, bagwire.h kBagMaxRows) */
typedef char KeptCapsAgree[(kMaxInner == (size_t)zonekeep::kMaxPackItems && kMaxItems == (size_t)zonekeep::kMaxBoxItems) ? 1 : -1];   /* zonekeep.h FitPiece fits rows to these */
const size_t kMaxDoors = (size_t)zonekeep::kMaxDoors;
const size_t kMaxInputs = (size_t)zonekeep::kMaxInputs;
const unsigned kClaimMax = 65536;     /* a claim field: slot + 1 */

inline const char* PartName(unsigned p) { return p == kPartPiece ? "piece" : (p == kPartItems ? "items" : (p == kPartOwner ? "owner" : "?")); }
inline const char* HowName(unsigned h)
{
    if (h == kHowMade) return "made";
    if (h == kHowPresent) return "present";
    if (h == kHowHandedBack) return "handed back";
    if (h == kHowItemsLaid) return "items laid";
    if (h == kHowItemsHas) return "items has";
    if (h == kHowOwnerRegistered) return "owner registered";
    if (h == kHowRefused) return "refused";
    return "?";
}
inline const char* StateName(unsigned s) { return s == kStNew ? "new" : (s == kStClaimed ? "claimed" : (s == kStLaid ? "laid" : "done")); }

/* ---- the state bits ---- */
inline unsigned PartState(unsigned flags, unsigned part)
{
    if (part == kPartPiece) return flags & 3u;
    if (part == kPartItems) return (flags >> 2) & 3u;
    return (flags & kFlagOwnerTook) != 0 ? kStDone : kStNew;
}
inline unsigned SetPartState(unsigned flags, unsigned part, unsigned st)
{
    if (part == kPartPiece) return (flags & ~3u) | (st & 3u);
    if (part == kPartItems) return (flags & ~0xCu) | ((st & 3u) << 2);
    return st == kStDone ? (flags | kFlagOwnerTook) : (flags & ~kFlagOwnerTook);
}
inline unsigned NonWriterFlag(unsigned part) { return part == kPartPiece ? kFlagPieceLaidNonWriter : (part == kPartItems ? kFlagItemsLaidNonWriter : 0u); }
/* a row is done when all three parts are */
inline int RowDone(unsigned flags)
{
    return (PartState(flags, kPartPiece) == kStDone && PartState(flags, kPartItems) == kStDone && (flags & kFlagOwnerTook) != 0) ? 1 : 0;
}
/* the flags a row starts a world-server run with, from its kept-state.txt line (a laid piece whose layer was the area's holder is
   not kept in the file: it is new again and the match before make keeps it from being made twice; laid box contents always are) */
inline unsigned FlagsOfState(const zonekeep::KeptState& s, int hasItems)
{
    unsigned f = 0;
    if (s.pieceDone != 0) f = SetPartState(f, kPartPiece, kStDone);
    else if (s.pieceLaidNonWriter != 0) f = SetPartState(f, kPartPiece, kStLaid) | kFlagPieceLaidNonWriter;
    if (s.itemsDone != 0 || hasItems == 0) f = SetPartState(f, kPartItems, kStDone);
    else if (s.itemsLaidNonWriter != 0) f = SetPartState(f, kPartItems, kStLaid) | kFlagItemsLaidNonWriter;
    if (s.ownerTook != 0) f |= kFlagOwnerTook;
    if (s.pieceFirstHow == (int)kHowPresent) f |= kFlagPiecePresent;
    return f;
}
inline zonekeep::KeptState StateOfFlags(unsigned f)
{
    zonekeep::KeptState s;
    s.pieceDone = PartState(f, kPartPiece) == kStDone ? 1 : 0;
    s.itemsDone = PartState(f, kPartItems) == kStDone ? 1 : 0;
    s.ownerTook = (f & kFlagOwnerTook) != 0 ? 1 : 0;
    s.pieceLaidNonWriter = (s.pieceDone == 0 && (f & kFlagPieceLaidNonWriter) != 0) ? 1 : 0;
    s.itemsLaidNonWriter = (s.itemsDone == 0 && (f & kFlagItemsLaidNonWriter) != 0) ? 1 : 0;
    return s;
}
/* the area record that holds a sector's pieces (recname.h: an area record's id) */
inline std::string AreaIdOfSector(int sx, int sy) { return "zone." + zonekeep::ZkNum(sx) + "." + zonekeep::ZkNum(sy); }
inline void SectorOfPiece(const zonekeep::KeptPiece& p, int* sx, int* sy) { poskey::SectorOf(p.pos[0], p.pos[2], sx, sy); }

/* ---- the world server's row (claims and who laid: memory only - every link ends with the process) ---- */
/* laidSeen: the game whose LAID for this part was taken (kept when that game's link goes, for its SETTLED on a new link, and in
   kept-state.txt while the part is kept laid: a laid part's claim field). again: the claim is its layer's claim AGAIN of its own lay
   (kClaimAgain) - handed back, refused or left, the part is laid again, never new */
struct Part { unsigned claim; int laidBy, laidSeen, wasWriter, again; unsigned how; Part() : claim(0), laidBy(0), laidSeen(0), wasWriter(0), again(0), how(0) {} };
/* pieceFirstHow: the how of the piece's first lay taken in this world (kept in kept-state.txt; 0 = none yet) */
struct ServerRow { unsigned flags, pieceFirstHow; Part piece, items; ServerRow() : flags(0), pieceFirstHow(0) {} };
inline Part& PartOf(ServerRow* r, unsigned part) { return part == kPartPiece ? r->piece : r->items; }
/* a row's kept-state.txt line: its flags' state, the piece's first how and the layer of each part kept laid */
inline zonekeep::KeptState StateOfRow(const ServerRow& r)
{
    zonekeep::KeptState s = StateOfFlags(r.flags);
    s.pieceFirstHow = (int)r.pieceFirstHow;
    s.pieceLaidBy = s.pieceLaidNonWriter != 0 ? r.piece.laidSeen : 0;
    s.itemsLaidBy = s.itemsLaidNonWriter != 0 ? r.items.laidSeen : 0;
    return s;
}
/* a row as a world-server run starts it from its kept-state.txt line: the flags (FlagsOfState), the piece's first how and the layer
   of each part kept laid */
inline void RowOfState(const zonekeep::KeptState& s, int hasItems, ServerRow* r)
{
    *r = ServerRow();
    r->flags = FlagsOfState(s, hasItems);
    r->pieceFirstHow = (unsigned)s.pieceFirstHow;
    if (PartState(r->flags, kPartPiece) == kStLaid) r->piece.laidSeen = s.pieceLaidBy;
    if (PartState(r->flags, kPartItems) == kStLaid) r->items.laidSeen = s.itemsLaidBy;
}
/* a part's claim field on the wire (ROWS / STATE): the claim's holder while claimed; while laid, its layer (laidSeen, 0 = not
   known), so that game tells its own lay from another's; 0 otherwise */
inline unsigned ClaimFieldOf(const ServerRow& r, unsigned part)
{
    const Part& p = part == kPartPiece ? r.piece : r.items;
    const unsigned st = PartState(r.flags, part);
    if (st == kStClaimed) return p.claim;
    if (st == kStLaid && p.laidSeen > 0 && (unsigned)p.laidSeen <= kClaimMax) return (unsigned)p.laidSeen;
    return 0u;
}

/* CLAIM from `who` (slot + 1). holderSlot = the slot the world server's area map names as the holder of the row's sector (-1 =
   none); ownerSlot = the row's owner. The piece: only the holder. The items: the holder or the owner (the game applies the box
   writer rule before it asks). Granted again to the game that already holds the claim. A LAID part goes AGAIN (kClaimAgain) only
   to its layer (laidSeen), under the same holder / owner rule: that game runs on a save that does not hold its lay. */
const int kClaimGrant = 1, kClaimNotNew = 2, kClaimHeld = 3, kClaimNotHolder = 4, kClaimBadPart = 5, kClaimAgain = 6;
inline int ClaimDecide(unsigned part, unsigned flags, unsigned claim, int who, int holderSlot, int ownerSlot, int laidSeen = 0)
{
    if (who <= 0 || (part != kPartPiece && part != kPartItems)) return kClaimBadPart;
    const unsigned st = PartState(flags, part);
    if (st == kStClaimed) return claim == (unsigned)who ? kClaimGrant : kClaimHeld;
    if (st == kStLaid && laidSeen == who)
    {
        const int me = who - 1;
        if (part == kPartPiece) return holderSlot == me ? kClaimAgain : kClaimNotHolder;
        return (holderSlot == me || ownerSlot == me) ? kClaimAgain : kClaimNotHolder;
    }
    if (st != kStNew) return kClaimNotNew;
    const int slot = who - 1;
    if (part == kPartPiece) return holderSlot == slot ? kClaimGrant : kClaimNotHolder;
    return (holderSlot == slot || ownerSlot == slot) ? kClaimGrant : kClaimNotHolder;
}
inline const char* ClaimName(int c)
{
    if (c == kClaimGrant) return "granted";
    if (c == kClaimNotNew) return "refused - the part is laid or done already";
    if (c == kClaimHeld) return "refused - another game holds the claim";
    if (c == kClaimNotHolder) return "refused - this game does not hold the area (nor own the piece, for its items)";
    if (c == kClaimAgain) return "granted again - its own lay, which the save it runs on does not hold";
    return "refused - not a part that is claimed";
}
/* applies ClaimDecide; true = the row changed */
inline bool Claim(ServerRow* r, unsigned part, int who, int holderSlot, int ownerSlot, int* result)
{
    const bool known = part == kPartPiece || part == kPartItems;
    *result = ClaimDecide(part, r->flags, known ? PartOf(r, part).claim : 0u, who, holderSlot, ownerSlot, known ? PartOf(r, part).laidSeen : 0);
    if (*result == kClaimAgain)
    {
        Part& p = PartOf(r, part);
        r->flags = SetPartState(r->flags, part, kStClaimed);   /* the never-offered marks stay: a restart keeps it laid */
        p.claim = (unsigned)who; p.again = 1; p.laidBy = 0;
        return true;
    }
    if (*result != kClaimGrant || PartState(r->flags, part) == kStClaimed) return false;
    r->flags = SetPartState(r->flags, part, kStClaimed);
    PartOf(r, part).claim = (unsigned)who; PartOf(r, part).again = 0;
    return true;
}
/* RELEASE: true = `who` held the claim; the part is new again (laid again for a claim again) */
inline bool Release(ServerRow* r, unsigned part, int who)
{
    if ((part != kPartPiece && part != kPartItems) || who <= 0) return false;
    Part& p = PartOf(r, part);
    if (PartState(r->flags, part) != kStClaimed || p.claim != (unsigned)who) return false;
    r->flags = SetPartState(r->flags, part, p.again != 0 ? kStLaid : kStNew);
    p.claim = 0; p.again = 0;
    return true;
}
/* the hows each part may report */
inline int HowFits(unsigned part, unsigned how)
{
    if (part == kPartPiece) return (how == kHowMade || how == kHowPresent || how == kHowHandedBack || how == kHowRefused) ? 1 : 0;
    if (part == kPartItems) return (how == kHowItemsLaid || how == kHowItemsHas || how == kHowRefused) ? 1 : 0;
    if (part == kPartOwner) return how == kHowOwnerRegistered ? 1 : 0;
    return 0;
}
/* LAID from `who`. writerNow = 1 when `who` holds the row's area in the world server's map at this moment.
     piece / items: only the claim holder; refused hands the claim back (new; laid again for a claim again); any other how makes the part laid, remembering who
                    laid it (also as laidSeen, for this run) and whether it held the area. A non-holder's piece and any game's box
                    contents are marked in the flags at once, so they are never offered again, even after a restart.
     owner:         only the owner's game; the part is done.
   A repeat of a lay already taken changes nothing. */
const int kLaidTaken = 1, kLaidRefusedBack = 2, kLaidOwnerTook = 3, kLaidRepeat = 4, kLaidNotHolder = 5, kLaidBad = 6;
inline int Laid(ServerRow* r, unsigned part, unsigned how, int who, int writerNow, int ownerSlot)
{
    if (who <= 0 || HowFits(part, how) == 0) return kLaidBad;
    if (part == kPartOwner)
    {
        if (who - 1 != ownerSlot) return kLaidNotHolder;
        if ((r->flags & kFlagOwnerTook) != 0) return kLaidRepeat;
        r->flags |= kFlagOwnerTook;
        return kLaidOwnerTook;
    }
    Part& p = PartOf(r, part);
    const unsigned st = PartState(r->flags, part);
    if (st == kStDone || (st == kStLaid && p.laidBy == who)) return kLaidRepeat;
    if (st != kStClaimed || p.claim != (unsigned)who) return kLaidNotHolder;
    p.claim = 0;
    if (how == kHowRefused)
    {
        r->flags = SetPartState(r->flags, part, p.again != 0 ? kStLaid : kStNew);   /* a claim again goes back to laid */
        p.again = 0;
        return kLaidRefusedBack;
    }
    r->flags = SetPartState(r->flags, part, kStLaid);
    p.laidBy = who; p.laidSeen = who; p.wasWriter = writerNow != 0 ? 1 : 0; p.how = how; p.again = 0;
    if (writerNow == 0 || part == kPartItems) r->flags |= NonWriterFlag(part);
    if (part == kPartPiece && r->pieceFirstHow == 0) r->pieceFirstHow = how;   /* the first lay in this world names what stood there */
    if (part == kPartPiece && r->pieceFirstHow == kHowPresent) r->flags |= kFlagPiecePresent;
    return kLaidTaken;
}
/* SETTLED from `who`: the part is done when `who` laid it, or when nobody is known to have laid it on this run (its layer's link went
   since, or a restart read it back as laid). A part that is new or claimed is closed only by the game whose LAID for it this run took
   (laidSeen): a game whose lay the world server does not know (a restart, a link that came back) reports it again first (LAID), so a
   SETTLED with no lay behind it on this run never closes a part. A part another connected game laid is not closed by this one. */
const int kSettledDone = 1, kSettledRepeat = 2, kSettledRefused = 3;
inline int Settled(ServerRow* r, unsigned part, int who)
{
    if (who <= 0 || (part != kPartPiece && part != kPartItems)) return kSettledRefused;
    Part& p = PartOf(r, part);
    const unsigned st = PartState(r->flags, part);
    if (st == kStDone) return kSettledRepeat;
    if (st == kStLaid && p.laidBy != 0 && p.laidBy != who) return kSettledRefused;
    if (st == kStClaimed && p.again != 0) return kSettledRefused;   /* a claim again: its game lays the part once more first */
    if (st == kStClaimed && p.claim != (unsigned)who) return kSettledRefused;
    if ((st == kStNew || st == kStClaimed) && p.laidSeen != who) return kSettledRefused;
    r->flags = SetPartState(r->flags, part, kStDone) & ~NonWriterFlag(part);
    p.claim = 0; p.laidBy = 0;
    return kSettledDone;
}
/* the area's new record holds a laid part: done */
inline bool MarkDone(ServerRow* r, unsigned part)
{
    if (part != kPartPiece && part != kPartItems) return false;
    if (PartState(r->flags, part) == kStDone) return false;
    r->flags = SetPartState(r->flags, part, kStDone) & ~NonWriterFlag(part);
    PartOf(r, part).claim = 0; PartOf(r, part).laidBy = 0; PartOf(r, part).again = 0;
    return true;
}
/* A GAME LEFT (`who`): its claims are handed back (a claim again of its own lay goes back to laid by nobody). A PIECE it laid that is not done is new again when it held the area as it laid it,
   and otherwise stays laid by nobody (never offered again - the area's record closes it). BOX CONTENTS it laid stay laid by nobody
   whoever laid them (never offered again: a record that holds them or the layer's SETTLED closes them - a lost lay can lose items, a
   second lay could double them). *reoffered counts the pieces made new. true = the row changed. */
inline bool PeerGone(ServerRow* r, int who, int* reoffered)
{
    if (who <= 0) return false;
    bool changed = false;
    for (unsigned part = kPartPiece; part <= kPartItems; ++part)
    {
        Part& p = PartOf(r, part);
        const unsigned st = PartState(r->flags, part);
        if (st == kStClaimed && p.claim == (unsigned)who)
        {
            r->flags = SetPartState(r->flags, part, p.again != 0 ? kStLaid : kStNew);
            if (p.again != 0) { p.laidBy = 0; p.wasWriter = 0; }
            p.claim = 0; p.again = 0; changed = true;
        }
        else if (st == kStLaid && p.laidBy == who)
        {
            if (p.wasWriter != 0 && part == kPartPiece) { r->flags = SetPartState(r->flags, part, kStNew) & ~NonWriterFlag(part); if (reoffered != 0) ++*reoffered; }
            p.laidBy = 0; p.wasWriter = 0;
            changed = true;
        }
    }
    return changed;
}

/* ---- WHERE A KEPT ITEM GOES (items.cpp ItKeptLayItems): never destroyed. A backpack's own item goes into its pack, else into the
   box, else onto the ground beside the box; an item of the box goes into the box, else onto the ground; after the ground, nowhere:
   the item waits alive in no inventory and is tried again (KeptWaitEnds). packMade = the pack itself was made here and is a pack. */
const int kPutLeft = 0, kPutPack = 1, kPutBox = 2, kPutGround = 3;
inline int KeptPutFirst(int isInner, int packMade) { return (isInner != 0 && packMade != 0) ? kPutPack : kPutBox; }
inline int KeptPutNext(int where) { return where == kPutPack ? kPutBox : (where == kPutBox ? kPutGround : kPutLeft); }

/* a kept item no place took waits in no inventory and is tried again by its layer on each later pass while its box is loaded there
   (items.cpp ItKeptRetryWaits); every kKeptItemWaitPasses passes the items still waiting are dropped at the box's position, and one
   that even that does not take waits on, named again - never forgotten while the world stands */
const int kKeptItemWaitPasses = 30;   /* Guess */
inline int KeptWaitEnds(int passes) { return passes >= kKeptItemWaitPasses ? 1 : 0; }

/* ---- wire ---- */
inline void PutU32(std::vector<char>* b, unsigned v) { char c[4]; std::memcpy(c, &v, 4); b->insert(b->end(), c, c + 4); }
inline void PutI32(std::vector<char>* b, int v) { char c[4]; std::memcpy(c, &v, 4); b->insert(b->end(), c, c + 4); }
inline void PutF32(std::vector<char>* b, float v) { char c[4]; std::memcpy(c, &v, 4); b->insert(b->end(), c, c + 4); }
inline void PutStr(std::vector<char>* b, const std::string& s) { PutU32(b, (unsigned)s.size()); b->insert(b->end(), s.begin(), s.end()); }
inline bool TextOk(const std::string& s, size_t cap, bool mayBeEmpty)
{
    if (s.size() > cap || (s.empty() && !mayBeEmpty)) return false;
    for (size_t i = 0; i < s.size(); ++i) { const unsigned char c = (unsigned char)s[i]; if (c < 0x20 || c == 0x7F) return false; }
    return true;
}
inline bool FloatOk(float v) { return v > -1.0e7f && v < 1.0e7f; }   /* NaN fails too */
inline bool FlagOk(int v) { return v == 0 || v == 1; }
inline bool ItemFieldsOk(const zonekeep::KeptItem& it)
{
    return TextOk(it.tmpl, kStrMax, false) && TextOk(it.material, kStrMax, true) && TextOk(it.company, kStrMax, true) && TextOk(it.color, kStrMax, true)
        && TextOk(it.section, kStrMax, true) && TextOk(it.uniform, kStrMax, true) && TextOk(it.crafter, kStrMax, true) && FlagOk(it.hasOwner) && FloatOk(it.quality) && FloatOk(it.charges);
}
/* an item and its own items at every depth: at most kMaxInner each, none at depth zonekeep::kMaxPackDepth holding items (a box item is
   depth 0) */
inline bool ItemOkAt(const zonekeep::KeptItem& it, int depth)
{
    if (!ItemFieldsOk(it) || it.inner.size() > kMaxInner || (depth >= zonekeep::kMaxPackDepth && !it.inner.empty())) return false;
    for (size_t k = 0; k < it.inner.size(); ++k) if (!ItemOkAt(it.inner[k], depth + 1)) return false;
    return true;
}
inline bool ItemOk(const zonekeep::KeptItem& it) { return ItemOkAt(it, 0); }
inline bool DoorOk(const zonekeep::KeptDoor& d)
{
    return d.index >= 0 && d.index < zonekeep::kMaxDoors && d.state >= 0 && d.state <= 3 && FlagOk(d.locked) && FlagOk(d.wantsLock)
        && d.ownerSlot >= -1 && d.ownerSlot <= coopslot::kSlotMax && FloatOk(d.hardness) && FlagOk(d.broken);
}
/* the piece's own state: known has bits, carried texts, flags 0 / 1, finite values, at most kMaxInputs stocks of an index below it */
inline bool OwnOk(const zonekeep::KeptPiece& p)
{
    if ((p.has & ~zonekeep::kHasKnown) != 0 || !TextOk(p.name, kStrMax, true) || !TextOk(p.interiorName, kStrMax, true) || !TextOk(p.exteriorName, kStrMax, true)) return false;
    if (!FlagOk(p.pause) || !FlagOk(p.getsBattery) || !FlagOk(p.powerOn) || !FlagOk(p.lockLocked)) return false;
    if (!FloatOk(p.battery) || !FloatOk(p.production) || !FloatOk(p.lockHardness) || !FloatOk(p.growth) || !FloatOk(p.death) || !FloatOk(p.start)) return false;
    if (p.inputs.size() > kMaxInputs) return false;
    for (size_t j = 0; j < p.inputs.size(); ++j) if (p.inputs[j].index < 0 || p.inputs[j].index >= zonekeep::kMaxInputs || !FloatOk(p.inputs[j].amount)) return false;
    return true;
}
struct Row { unsigned row, flags, claimPiece, claimItems; zonekeep::KeptPiece p; Row() : row(0), flags(0), claimPiece(0), claimItems(0) {} };
/* one ITEM, its own items after it */
inline void PutItem(std::vector<char>* b, const zonekeep::KeptItem& it)
{
    PutStr(b, it.tmpl); PutStr(b, it.material); PutStr(b, it.company); PutStr(b, it.color); PutStr(b, it.section);
    PutStr(b, it.uniform); PutStr(b, it.crafter);
    PutU32(b, (unsigned)it.hasOwner);
    for (int i = 0; i < 5; ++i) PutI32(b, it.owner[i]);
    PutF32(b, it.quality); PutF32(b, it.charges);
    PutI32(b, it.quantity); PutI32(b, it.level); PutI32(b, it.function); PutI32(b, it.unique); PutI32(b, it.slotX); PutI32(b, it.slotY);
    PutU32(b, (unsigned)it.inner.size());
    for (size_t k = 0; k < it.inner.size(); ++k) PutItem(b, it.inner[k]);
}
inline bool RowOk(const Row& r)
{
    const zonekeep::KeptPiece& p = r.p;
    if ((r.flags & ~kFlagsKnown) != 0 || r.claimPiece > kClaimMax || r.claimItems > kClaimMax) return false;
    if (p.ownerSlot < 0 || p.ownerSlot > coopslot::kSlotMax) return false;
    if (!TextOk(p.key, kKeyMax, false) || !TextOk(p.tmpl, kStrMax, false) || !TextOk(p.hostKey, kKeyMax, true)) return false;
    for (int j = 0; j < 3; ++j) if (!FloatOk(p.pos[j])) return false;
    for (int j = 0; j < 4; ++j) if (!FloatOk(p.rot[j])) return false;
    if (!FloatOk(p.progress) || !FlagOk(p.complete) || !FlagOk(p.destroyed) || !FlagOk(p.broken)) return false;
    if (!OwnOk(p)) return false;
    if (p.mats.size() > kMaxMats || p.items.size() > kMaxItems || p.doors.size() > kMaxDoors) return false;
    for (size_t j = 0; j < p.mats.size(); ++j) if (!FloatOk(p.mats[j].count)) return false;
    for (size_t j = 0; j < p.doors.size(); ++j) if (!DoorOk(p.doors[j])) return false;
    for (size_t j = 0; j < p.items.size(); ++j) if (!ItemOk(p.items[j])) return false;
    return true;
}
/* the bytes one ITEM takes, its own items with it */
inline size_t ItemWireSize(const zonekeep::KeptItem& it)
{
    size_t n = 4 * 7 + it.tmpl.size() + it.material.size() + it.company.size() + it.color.size() + it.section.size() + it.uniform.size()
               + it.crafter.size() + 4 * 6 + 4 * 8 + 4;
    for (size_t k = 0; k < it.inner.size(); ++k) n += ItemWireSize(it.inner[k]);
    return n;
}
/* the bytes one row takes in a ROWS */
inline size_t RowWireSize(const Row& r)
{
    size_t n = 4 * 5 + 4 + r.p.key.size() + 4 + r.p.tmpl.size() + 4 * 7 + 4 + 4 + r.p.hostKey.size() + 4 * 4
               + 4 + 4 + r.p.name.size() + 4 + r.p.interiorName.size() + 4 + r.p.exteriorName.size() + 4 * 3 + 4 * 2 + 4 * 3 + 4 * 4
               + 4 + r.p.inputs.size() * 8 + 4 + r.p.mats.size() * 8 + 4 + r.p.doors.size() * 36 + 4;
    for (size_t j = 0; j < r.p.items.size(); ++j) n += ItemWireSize(r.p.items[j]);
    return n;
}
inline void PutRow(std::vector<char>* b, const Row& r)
{
    const zonekeep::KeptPiece& p = r.p;
    PutU32(b, r.row); PutU32(b, r.flags); PutU32(b, r.claimPiece); PutU32(b, r.claimItems); PutU32(b, (unsigned)p.ownerSlot);
    PutStr(b, p.key); PutStr(b, p.tmpl);
    for (int j = 0; j < 3; ++j) PutF32(b, p.pos[j]);
    for (int j = 0; j < 4; ++j) PutF32(b, p.rot[j]);
    PutI32(b, p.floor); PutStr(b, p.hostKey); PutF32(b, p.progress);
    PutU32(b, (unsigned)p.complete); PutU32(b, (unsigned)p.destroyed); PutU32(b, (unsigned)p.broken);
    PutU32(b, (unsigned)p.has); PutStr(b, p.name); PutStr(b, p.interiorName); PutStr(b, p.exteriorName);
    PutU32(b, (unsigned)p.pause); PutU32(b, (unsigned)p.getsBattery); PutU32(b, (unsigned)p.powerOn); PutF32(b, p.battery); PutF32(b, p.production);
    PutI32(b, p.lockLevel); PutF32(b, p.lockHardness); PutU32(b, (unsigned)p.lockLocked);
    PutF32(b, p.growth); PutF32(b, p.death); PutF32(b, p.start); PutI32(b, p.harvested);
    PutU32(b, (unsigned)p.inputs.size());
    for (size_t j = 0; j < p.inputs.size(); ++j) { PutI32(b, p.inputs[j].index); PutF32(b, p.inputs[j].amount); }
    PutU32(b, (unsigned)p.mats.size());
    for (size_t j = 0; j < p.mats.size(); ++j) { PutI32(b, p.mats[j].index); PutF32(b, p.mats[j].count); }
    PutU32(b, (unsigned)p.doors.size());
    for (size_t j = 0; j < p.doors.size(); ++j)
    {
        const zonekeep::KeptDoor& d = p.doors[j];
        PutI32(b, d.index); PutI32(b, d.state); PutU32(b, (unsigned)d.locked); PutU32(b, (unsigned)d.wantsLock); PutI32(b, d.lockLevel);
        PutI32(b, d.gateCode); PutI32(b, d.ownerSlot); PutF32(b, d.hardness); PutU32(b, (unsigned)d.broken);
    }
    PutU32(b, (unsigned)p.items.size());
    for (size_t j = 0; j < p.items.size(); ++j) PutItem(b, p.items[j]);
}
struct Reader
{
    const char* p; size_t n, at; bool bad;
    Reader(const char* p0, size_t n0) : p(p0), n(n0), at(0), bad(p0 == 0 && n0 != 0) {}
    unsigned U32() { if (bad || n - at < 4) { bad = true; return 0; } unsigned v; std::memcpy(&v, p + at, 4); at += 4; return v; }
    int I32() { if (bad || n - at < 4) { bad = true; return 0; } int v; std::memcpy(&v, p + at, 4); at += 4; return v; }
    float F32() { if (bad || n - at < 4) { bad = true; return 0.0f; } float v; std::memcpy(&v, p + at, 4); at += 4; return v; }
    std::string Str(size_t cap, bool mayBeEmpty)
    {
        const unsigned len = U32();
        if (bad || len > cap || n - at < len) { bad = true; return std::string(); }
        std::string s(p + at, p + at + len); at += len;
        if (!TextOk(s, cap, mayBeEmpty)) bad = true;
        return s;
    }
    bool Done() const { return !bad && at == n; }
};
const size_t kItemWireMin = 88;   /* the fewest bytes an ITEM takes (a one-byte template, the rest empty, no own items) */
/* one ITEM; depth 0 = a box item, an ITEM at depth zonekeep::kMaxPackDepth must hold none of its own. false = malformed (rd.bad set) */
inline bool GetItem(Reader& rd, zonekeep::KeptItem* it, int depth)
{
    it->tmpl = rd.Str(kStrMax, false); it->material = rd.Str(kStrMax, true); it->company = rd.Str(kStrMax, true);
    it->color = rd.Str(kStrMax, true); it->section = rd.Str(kStrMax, true);
    it->uniform = rd.Str(kStrMax, true); it->crafter = rd.Str(kStrMax, true);
    it->hasOwner = (int)rd.U32();
    for (int i = 0; i < 5; ++i) it->owner[i] = rd.I32();
    if (!FlagOk(it->hasOwner)) rd.bad = true;
    it->quality = rd.F32(); it->charges = rd.F32();
    it->quantity = rd.I32(); it->level = rd.I32(); it->function = rd.I32(); it->unique = rd.I32(); it->slotX = rd.I32(); it->slotY = rd.I32();
    const unsigned nin = rd.U32();
    if (rd.bad || (depth >= zonekeep::kMaxPackDepth && nin != 0) || nin > kMaxInner || (size_t)nin > (rd.n - rd.at) / kItemWireMin) { rd.bad = true; return false; }
    for (unsigned k = 0; k < nin && !rd.bad; ++k)
    {
        zonekeep::KeptItem in;
        if (!GetItem(rd, &in, depth + 1)) return false;
        it->inner.push_back(in);
    }
    return !rd.bad;
}
inline bool GetRow(Reader& rd, Row* r)
{
    zonekeep::KeptPiece& p = r->p;
    r->row = rd.U32(); r->flags = rd.U32(); r->claimPiece = rd.U32(); r->claimItems = rd.U32();
    const unsigned owner = rd.U32();
    p.ownerSlot = owner > (unsigned)coopslot::kSlotMax ? -1 : (int)owner;
    p.key = rd.Str(kKeyMax, false); p.tmpl = rd.Str(kStrMax, false);
    for (int j = 0; j < 3; ++j) p.pos[j] = rd.F32();
    for (int j = 0; j < 4; ++j) p.rot[j] = rd.F32();
    p.floor = rd.I32(); p.hostKey = rd.Str(kKeyMax, true); p.progress = rd.F32();
    p.complete = (int)rd.U32(); p.destroyed = (int)rd.U32(); p.broken = (int)rd.U32();
    p.has = (int)rd.U32(); p.name = rd.Str(kStrMax, true); p.interiorName = rd.Str(kStrMax, true); p.exteriorName = rd.Str(kStrMax, true);
    p.pause = (int)rd.U32(); p.getsBattery = (int)rd.U32(); p.powerOn = (int)rd.U32(); p.battery = rd.F32(); p.production = rd.F32();
    p.lockLevel = rd.I32(); p.lockHardness = rd.F32(); p.lockLocked = (int)rd.U32();
    p.growth = rd.F32(); p.death = rd.F32(); p.start = rd.F32(); p.harvested = rd.I32();
    const unsigned nx = rd.U32();
    if (rd.bad || nx > kMaxInputs || (size_t)nx > (rd.n - rd.at) / 8) return false;
    for (unsigned j = 0; j < nx; ++j) { zonekeep::KeptStock s; s.index = rd.I32(); s.amount = rd.F32(); p.inputs.push_back(s); }
    const unsigned nm = rd.U32();
    if (rd.bad || nm > kMaxMats || (size_t)nm > (rd.n - rd.at) / 8) return false;
    for (unsigned j = 0; j < nm; ++j) { zonekeep::KeptMat m; m.index = rd.I32(); m.count = rd.F32(); p.mats.push_back(m); }
    const unsigned nd = rd.U32();
    if (rd.bad || nd > kMaxDoors || (size_t)nd > (rd.n - rd.at) / 36) return false;
    for (unsigned j = 0; j < nd; ++j)
    {
        zonekeep::KeptDoor d;
        d.index = rd.I32(); d.state = rd.I32(); d.locked = (int)rd.U32(); d.wantsLock = (int)rd.U32(); d.lockLevel = rd.I32();
        d.gateCode = rd.I32(); d.ownerSlot = rd.I32(); d.hardness = rd.F32(); d.broken = (int)rd.U32();
        p.doors.push_back(d);
    }
    const unsigned ni = rd.U32();
    if (rd.bad || ni > kMaxItems || (size_t)ni > (rd.n - rd.at) / kItemWireMin) return false;
    for (unsigned j = 0; j < ni && !rd.bad; ++j)
    {
        zonekeep::KeptItem it;
        if (!GetItem(rd, &it, 0)) return false;
        p.items.push_back(it);
    }
    return !rd.bad && RowOk(*r);
}

/* CLAIM / RELEASE / SETTLED (piece or items) */
inline bool EncodePart(std::vector<char>* out, unsigned op, unsigned row, unsigned part)
{
    out->clear();
    if ((op != kOpClaim && op != kOpRelease && op != kOpSettled) || (part != kPartPiece && part != kPartItems)) return false;
    PutU32(out, op); PutU32(out, row); PutU32(out, part);
    return true;
}
/* LAID: the how must fit the part (HowFits) */
inline bool EncodeLaid(std::vector<char>* out, unsigned row, unsigned part, unsigned how, const std::string& liveKey)
{
    out->clear();
    if (HowFits(part, how) == 0 || !TextOk(liveKey, kKeyMax, true)) return false;
    PutU32(out, kOpLaid); PutU32(out, row); PutU32(out, part); PutU32(out, how); PutStr(out, liveKey);
    return true;
}
/* ROWS: 1..64 rows, every one valid and in sector (sx, sy) */
inline bool EncodeRows(std::vector<char>* out, int sx, int sy, const std::vector<Row>& rows)
{
    out->clear();
    if (rows.empty() || rows.size() > kMaxRows || sx < 0 || sx > 63 || sy < 0 || sy > 63) return false;
    PutU32(out, kOpRows); PutI32(out, sx); PutI32(out, sy); PutU32(out, (unsigned)rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        int rx = -1, ry = -1;
        if (!RowOk(rows[i])) { out->clear(); return false; }
        SectorOfPiece(rows[i].p, &rx, &ry);
        if (rx != sx || ry != sy) { out->clear(); return false; }
        PutRow(out, rows[i]);
    }
    return true;
}
inline bool EncodeState(std::vector<char>* out, unsigned row, unsigned flags, unsigned claimPiece, unsigned claimItems)
{
    out->clear();
    if ((flags & ~kFlagsKnown) != 0 || claimPiece > kClaimMax || claimItems > kClaimMax) return false;
    PutU32(out, kOpState); PutU32(out, row); PutU32(out, flags); PutU32(out, claimPiece); PutU32(out, claimItems);
    return true;
}
struct Msg
{
    unsigned op, row, part, how, flags, claimPiece, claimItems; int sx, sy; std::string liveKey; std::vector<Row> rows;
    Msg() : op(0), row(0), part(0), how(0), flags(0), claimPiece(0), claimItems(0), sx(-1), sy(-1) {}
};
/* 1 = decoded (every field valid, nothing trailing), 0 = malformed (nothing is taken from it) */
inline int Decode(const char* p, size_t n, Msg* m)
{
    *m = Msg();
    Reader r(p, n);
    m->op = r.U32();
    if (r.bad) return 0;
    if (m->op == kOpClaim || m->op == kOpRelease || m->op == kOpSettled)
    {
        m->row = r.U32(); m->part = r.U32();
        return (r.Done() && (m->part == kPartPiece || m->part == kPartItems)) ? 1 : 0;
    }
    if (m->op == kOpLaid)
    {
        m->row = r.U32(); m->part = r.U32(); m->how = r.U32(); m->liveKey = r.Str(kKeyMax, true);
        return (r.Done() && HowFits(m->part, m->how) != 0) ? 1 : 0;
    }
    if (m->op == kOpState)
    {
        m->row = r.U32(); m->flags = r.U32(); m->claimPiece = r.U32(); m->claimItems = r.U32();
        return (r.Done() && (m->flags & ~kFlagsKnown) == 0 && m->claimPiece <= kClaimMax && m->claimItems <= kClaimMax) ? 1 : 0;
    }
    if (m->op == kOpRows)
    {
        m->sx = r.I32(); m->sy = r.I32();
        const unsigned c = r.U32();
        if (r.bad || c == 0 || c > kMaxRows || m->sx < 0 || m->sx > 63 || m->sy < 0 || m->sy > 63) return 0;
        for (unsigned i = 0; i < c; ++i)
        {
            Row w;
            if (!GetRow(r, &w)) { m->rows.clear(); return 0; }
            int rx = -1, ry = -1;
            SectorOfPiece(w.p, &rx, &ry);
            if (rx != m->sx || ry != m->sy) { m->rows.clear(); return 0; }
            m->rows.push_back(w);
        }
        if (!r.Done()) { m->rows.clear(); return 0; }
        return 1;
    }
    return 0;
}

/* ---- the game's side: the pure steps build.cpp's kept drain takes (offline-tested) ---- */
/* the order one sector's rows are laid in: the buildings other rows stand in first, then the other free-standing pieces, then the
   furniture (a building makes its own doors and fittings as it is made, so what stands inside it is looked for after it) */
const int kOrderHost = 0, kOrderFree = 1, kOrderFurniture = 2;
inline int LayOrder(int isFurniture, int isHost) { return isFurniture != 0 ? kOrderFurniture : (isHost != 0 ? kOrderHost : kOrderFree); }
/* what a game does next with a piece or items part. mayClaim = this game may hold the part now (the piece: it holds the area,
   MineHeldTS strict; the items: it is the box's writer); laidHere = this game already laid this part in this world.
     claim   - the part is new and this game may hold it; or it is laid by this game (its claim field), this game's record does not
               list it and the record is read (mayAgain) and this game may hold it: claimed AGAIN, laid once more
     work    - the claim is this game's: search, then lay
     report  - the claim is this game's and it laid the part already (the part became new again after a world-server restart, or the
               link dropped before LAID): LAID again with the same how, nothing touched
     release - the claim is this game's but it may no longer hold the part
     none    - anything else (another game's claim, laid, done) */
const int kStepNone = 0, kStepClaim = 1, kStepRelease = 2, kStepWork = 3, kStepReport = 4;
inline int PartStep(unsigned flags, unsigned part, unsigned claim, int mySlot, int mayClaim, int laidHere, int mayAgain = 0)
{
    if (mySlot < 0 || (part != kPartPiece && part != kPartItems)) return kStepNone;
    const unsigned st = PartState(flags, part);
    if (st == kStNew) return mayClaim != 0 ? kStepClaim : kStepNone;
    if (st == kStLaid) return (claim == (unsigned)mySlot + 1u && laidHere == 0 && mayAgain != 0 && mayClaim != 0) ? kStepClaim : kStepNone;
    if (st != kStClaimed || claim != (unsigned)mySlot + 1u) return kStepNone;
    if (laidHere != 0) return kStepReport;
    return mayClaim != 0 ? kStepWork : kStepRelease;
}
/* the restore road's outcome (build.cpp BdPending::rsOutcome: 1 present, 2 recreated, 3 refused - another owner's piece stands at
   the key, anything else failed) as the how its LAID reports. Nothing is made over a piece that stands there, so 3 is 'present'. */
inline unsigned HowOfRestore(int rsOutcome) { return rsOutcome == 2 ? kHowMade : ((rsOutcome == 1 || rsOutcome == 3) ? kHowPresent : kHowRefused); }
/* quaternions are (w, x, y, z). conj(a) * b: b's rotation seen from inside a's frame */
inline void QuatConjMul(const float* a, const float* b, float* out)
{
    const float pw = a[0], px = -a[1], py = -a[2], pz = -a[3];
    const float w = pw * b[0] - px * b[1] - py * b[2] - pz * b[3];
    const float x = pw * b[1] + px * b[0] + py * b[3] - pz * b[2];
    const float y = pw * b[2] - px * b[3] + py * b[0] + pz * b[1];
    const float z = pw * b[3] + px * b[2] - py * b[1] + pz * b[0];
    out[0] = w; out[1] = x; out[2] = y; out[3] = z;
}
/* v turned by the inverse of the unit quaternion q (the same arithmetic as build.cpp BdRotateInv) */
inline void RotateInv(const float* q, const float* v, float* out)
{
    const float w = q[0], x = -q[1], y = -q[2], z = -q[3];
    const float ux = y * v[2] - z * v[1], uy = z * v[0] - x * v[2], uz = x * v[1] - y * v[0];
    const float uux = y * uz - z * uy, uuy = z * ux - x * uz, uuz = x * uy - y * ux;
    out[0] = v[0] + 2.0f * (w * ux + uux);
    out[1] = v[1] + 2.0f * (w * uy + uuy);
    out[2] = v[2] + 2.0f * (w * uz + uuz);
}
/* FURNITURE IS MADE IN ITS HOST'S FRAME (createBuilding takes a furniture piece's position and rotation relative to the building it
   stands in): the kept world position and rotation turned into that frame, from the live host's position and rotation */
inline void LocalOfWorld(const float* hostPos, const float* hostQ, const float* worldPos, const float* worldQ, float* localPos, float* localQ)
{
    float d[3];
    for (int i = 0; i < 3; ++i) d[i] = worldPos[i] - hostPos[i];
    RotateInv(hostQ, d, localPos);
    QuatConjMul(hostQ, worldQ, localQ);
}
/* two pieces stand within r of each other on the ground (x and z; the height is not compared) */
inline int NearXZ(float ax, float az, float bx, float bz, float r) { const float dx = ax - bx, dz = az - bz; return (dx * dx + dz * dz <= r * r) ? 1 : 0; }
/* the delivered materials as the per-entry array a STATE carries: entry i = the count kept for index i, 0 where none was kept.
   Returns the entries written (the highest kept index + 1, at most cap); an index past cap is left out. */
inline int MatsArray(const std::vector<zonekeep::KeptMat>& mats, float* out, int cap)
{
    int n = 0;
    for (int i = 0; i < cap; ++i) out[i] = 0.0f;
    for (size_t j = 0; j < mats.size(); ++j)
    {
        const int k = mats[j].index;
        if (k < 0 || k >= cap) continue;
        out[k] = mats[j].count;
        if (k + 1 > n) n = k + 1;
    }
    return n;
}
/* the world server's sequence echo answers a lay's write. The echo carries the stamp (writtenAt) of the write it answers; it settles
   the lay when that write is the lay's own write or a later one of this game's (stamp >= the lay's write's - a later write of the
   area holds the lay too) and is newer than every write of that area this game had sent before the lay's write (an earlier write in
   the same second carries the same stamp, and the world server then refuses the lay's write as not newer). No stamp: never. */
inline int EchoAnswersWrite(int hasStamp, long long echoStamp, long long writeStamp, long long priorStamp)
{
    return (hasStamp != 0 && echoStamp >= writeStamp && echoStamp > priorStamp) ? 1 : 0;
}
/* ---- THE PIECE'S OWN STATE LAID BACK (build.cpp BdKeptExtras): one bit per part; a part the row holds nothing of is not wanted ---- */
const unsigned kXName = 1, kXLayout = 2, kXPause = 4, kXPower = 8, kXProd = 16, kXInputs = 32, kXFarm = 64, kXLock = 128, kXDoors = 256;
inline unsigned ExtrasWanted(const zonekeep::KeptPiece& p)
{
    unsigned m = 0;
    if ((p.has & zonekeep::kHasName) != 0) m |= kXName;
    if ((p.has & zonekeep::kHasLayout) != 0) m |= kXLayout;
    if ((p.has & zonekeep::kHasPause) != 0) m |= kXPause;
    if ((p.has & zonekeep::kHasPower) != 0) m |= kXPower;
    if ((p.has & zonekeep::kHasProduction) != 0) m |= kXProd;
    if (!p.inputs.empty()) m |= kXInputs;
    if ((p.has & zonekeep::kHasFarm) != 0) m |= kXFarm;
    if ((p.has & zonekeep::kHasOwnLock) != 0) m |= kXLock;
    if (!p.doors.empty() && p.destroyed == 0) m |= kXDoors;   /* a ruin has no doors */
    return m;
}
/* a kept lock level as the lock takes it */
inline int LockLevelClamp(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }
/* ---- THE CLASSES WHOSE OWN STATE IS VERIFIED, by the RTTI type name of the live piece's vtable (build.cpp: the kept lay's class
   checks and the TEST-ONLY bldset / bldstate gates use these and nothing else). Each set is a class whose vtable was read
   (build/vtable_<Class>.txt) and every class the game's RTTI base lists put under it whose vtable has the same function in the
   slots that matter and whose save / load is the parent's or calls it (build.cpp names the slots and functions):
     ClassBuilding   - the pause byte (the load at vt+0x380) and the piece's own lock (getDoorLock at vt+0x400): every building class;
     ClassUseable    - the power switch, gets battery and battery (setPowerOn at vt+0x318) and the broken flag (setBroken at
                       vt+0x310): UseableStuff and every class under it;
     ClassProduction - the production bar and the input records: ProductionBuilding and every class under it.
   Each set holds the next one. Any other name, an empty name and a null are not verified (0). ---- */
const char* const kClassProduction[] = { ".?AVProductionBuilding@@", ".?AVFarmBuilding@@", ".?AVGeneratorBuilding@@", ".?AVWindGeneratorBuilding@@",
                                         ".?AVCraftingBuilding@@", ".?AVFurnaceBuilding@@", ".?AVRainCollectorBuilding@@", ".?AVTortureBuilding@@" };
const char* const kClassUseableOnly[] = { ".?AVUseableStuff@@", ".?AVStorageBuilding@@", ".?AVLightBuilding@@", ".?AVResearchBuilding@@", ".?AVTurretBuilding@@" };
const char* const kClassBuildingOnly[] = { ".?AVBuilding@@", ".?AVGatewayBuilding@@", ".?AVDoorStuff@@", ".?AVWallBuilding@@" };
inline int ClassIn(const char* cls, const char* const* k, size_t n)
{
    if (cls == 0) return 0;
    for (size_t i = 0; i < n; ++i) if (std::strcmp(cls, k[i]) == 0) return 1;
    return 0;
}
inline int ClassProduction(const char* cls) { return ClassIn(cls, kClassProduction, sizeof(kClassProduction) / sizeof(kClassProduction[0])); }
inline int ClassUseable(const char* cls)
{
    return (ClassProduction(cls) != 0 || ClassIn(cls, kClassUseableOnly, sizeof(kClassUseableOnly) / sizeof(kClassUseableOnly[0])) != 0) ? 1 : 0;
}
inline int ClassBuilding(const char* cls)
{
    return (ClassUseable(cls) != 0 || ClassIn(cls, kClassBuildingOnly, sizeof(kClassBuildingOnly) / sizeof(kClassBuildingOnly[0])) != 0) ? 1 : 0;
}
/* A KEPT FARM'S PLANT AGES (farm.cpp FarmKeptLay). The farm saves no ages: its update (0xE5540) takes the lowest age as the growth, and
   a harvested plant is hidden at -10 (destroyAPlant 0xE47C0). So the first `harvested` plants (clamped to 0 .. n) are laid at -10 and
   the others from the kept growth upward by at most kFarmAgeSpread, the first of them at the growth itself. */
const float kFarmHarvestedAge = -10.0f;
const float kFarmAgeSpread = 0.02f;   /* Guess: how far above the kept growth the last growing plant is laid */
inline void KeptFarmAges(float growth, int harvested, int n, float* out)
{
    if (n <= 0) return;
    const int h = harvested < 0 ? 0 : (harvested > n ? n : harvested);
    const int grow = n - h;
    for (int i = 0; i < n; ++i)
    {
        if (i < h) { out[i] = kFarmHarvestedAge; continue; }
        out[i] = growth + (grow > 1 ? kFarmAgeSpread * (float)(i - h) / (float)(grow - 1) : 0.0f);
    }
}

/* ---- THE OWN STATE STEP (build.cpp BdKeptExtras): which parts are finished, their names, and whether this game lays any ---- */
inline int ExtrasFinished(unsigned want, unsigned done, unsigned ended) { return ((done | ended) & want) == want ? 1 : 0; }
/* A machine with no production bar here (the engine makes a bar only for a machine with an output item) and a kept bar of 0 - the
   value the engine's own save writes when the bar is missing - has nothing to lay: the bar part is done, as the engine's own load
   leaves it. A kept bar above 0 still waits for the bar. */
inline int ProdBarNothingToLay(int hasBar, float kept) { return (hasBar == 0 && kept == 0.0f) ? 1 : 0; }
/* no game lays the own state of a piece whose first lay found it standing (the row's present flag), nor of one this game found standing */
inline int ExtrasLayHere(unsigned flags, unsigned howHere) { return ((flags & kFlagPiecePresent) == 0 && howHere != kHowPresent) ? 1 : 0; }
inline std::string ExtrasNames(unsigned bits)
{
    static const char* const n[9] = { "name", "layout names", "pause", "power", "production bar", "input stocks", "farm", "own lock", "doors" };
    std::string o;
    for (int i = 0; i < 9; ++i) if ((bits & (1u << i)) != 0) { if (!o.empty()) o += ", "; o += n[i]; }
    return o.empty() ? std::string("none") : o;
}
/* the kept input stocks are laid only when the machine here has a record for every kept index (n = its input records; -1 = not read) */
inline int InputsAllFit(const std::vector<zonekeep::KeptStock>& in, int n)
{
    if (n < 0) return 0;
    for (size_t j = 0; j < in.size(); ++j) if (in[j].index < 0 || in[j].index >= n) return 0;
    return 1;
}
/* a kept door state (doorsync.h: 0 closed, 1 open, 2 opening, 3 closing) as the door is laid: opening is laid open, closing closed */
inline int KeptDoorOpen(int state) { return (state == 1 || state == 2) ? 1 : 0; }
/* ---- THIS GAME'S RECORD OF WHAT IT LAID IN A WORLD (build.cpp BdKeptHereSave / BdKeptHereCommit; a file in the player's records
   store). It says what this game's last FINISHED save holds: a change to what it laid is noted in memory with the engine's save
   request count then, and goes into the file only once a save requested after it has finished (HereFinishAction). A part it lists
   is never laid again by that game in that world; a part that game laid that it does not list is claimed again (a reload of a save
   that does not hold it) and laid once more.
   Line 1: "kept-laid-here<TAB>2<TAB><the world id, hex>". Then one line per row this game laid any part of:
     h <row> <piece how> <items how> <own state parts laid> <own state parts ended> <doors laid> <waiting items>
   TAB-separated, LF line ends (a CR before it is read as nothing), decimal. The piece how is 0 / made / present / handed back, the
   items how 0 / items laid / items has, the parts kX* bits, the doors one bit per door index. The waiting items are "-" or a comma
   list: an item path (ItemPathSplit) names a kept item the save does not hold (it waited in no inventory) - made again from the row
   after a reload; "x" and a path names an item of a waiting pack that is not inside it (left out when the pack is made again). A
   record of format 1 (no waiting items) is read too. ---- */
const char* const kHereHeader = "kept-laid-here";
const int kHereFormat = 2;
const unsigned kXAll = 0x1FF;
const size_t kHereWaitsMax = 16384;   /* the waiting items' text on one line */
struct HereRow
{
    unsigned pieceHow, itemsHow, extrasDone, extrasEnded, doorsDone;
    std::vector<std::string> waits;
    HereRow() : pieceHow(0), itemsHow(0), extrasDone(0), extrasEnded(0), doorsDone(0) {}
};
inline int HereRowEmpty(const HereRow& h) { return ((h.pieceHow | h.itemsHow | h.extrasDone | h.extrasEnded | h.doorsDone) == 0 && h.waits.empty()) ? 1 : 0; }
inline int HereRowSame(const HereRow& a, const HereRow& b)
{
    return (a.pieceHow == b.pieceHow && a.itemsHow == b.itemsHow && a.extrasDone == b.extrasDone && a.extrasEnded == b.extrasEnded
            && a.doorsDone == b.doorsDone && a.waits == b.waits) ? 1 : 0;
}
inline std::string HexOf(const std::string& s)
{
    static const char* const d = "0123456789abcdef";
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) { const unsigned char c = (unsigned char)s[i]; o += d[c >> 4]; o += d[c & 15]; }
    return o;
}
inline std::string UNum(unsigned v)
{
    char t[16];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v != 0 && n < 15);
    std::string o;
    while (n > 0) o += t[--n];
    return o;
}
inline bool UNumOf(const std::string& s, unsigned* v)
{
    if (s.empty() || s.size() > 10) return false;
    unsigned long long a = 0;
    for (size_t i = 0; i < s.size(); ++i) { if (s[i] < '0' || s[i] > '9') return false; a = a * 10u + (unsigned long long)(s[i] - '0'); }
    if (a > 0xFFFFFFFFull) return false;
    *v = (unsigned)a;
    return true;
}
/* A KEPT ITEM'S PATH in a row's items: "3" = the box's fourth item, "3.0" = the first item that pack held; 1 .. kMaxPackDepth + 1
   numbers, each below kMaxItems. false = not a path (*out empty) */
inline bool ItemPathSplit(const std::string& path, std::vector<unsigned>* out)
{
    out->clear();
    size_t at = 0;
    for (;;)
    {
        const size_t e = path.find('.', at);
        const std::string part = path.substr(at, e == std::string::npos ? std::string::npos : e - at);
        unsigned v = 0;
        if (part.size() > 3 || !UNumOf(part, &v) || v >= (unsigned)kMaxItems || out->size() > (size_t)zonekeep::kMaxPackDepth) { out->clear(); return false; }
        out->push_back(v);
        if (e == std::string::npos) return true;
        at = e + 1;
    }
}
inline bool ItemPathOk(const std::string& path) { std::vector<unsigned> v; return ItemPathSplit(path, &v); }
inline std::string ItemPathChild(const std::string& parent, size_t k) { return parent.empty() ? UNum((unsigned)k) : parent + "." + UNum((unsigned)k); }
/* one name of the waiting items: a path, or "x" and a path */
inline bool HereTokenOk(const std::string& t) { return !t.empty() && ItemPathOk(t[0] == 'x' ? t.substr(1) : t); }
/* the kept item at path; 0 = none there */
inline const zonekeep::KeptItem* ItemAtPath(const std::vector<zonekeep::KeptItem>& items, const std::string& path)
{
    std::vector<unsigned> v;
    if (!ItemPathSplit(path, &v)) return 0;
    const std::vector<zonekeep::KeptItem>* level = &items;
    const zonekeep::KeptItem* it = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (v[i] >= level->size()) return 0;
        it = &(*level)[v[i]];
        level = &it->inner;
    }
    return it;
}
/* the record's names list `path` (as a waiting item or as one placed elsewhere): an item made again leaves it out of its own items */
inline bool HereNamed(const std::vector<std::string>& tokens, const std::string& path)
{
    for (size_t i = 0; i < tokens.size(); ++i)
        if (tokens[i] == path || (tokens[i].size() == path.size() + 1 && tokens[i][0] == 'x' && tokens[i].compare(1, std::string::npos, path) == 0)) return true;
    return false;
}
/* the names for one box: each waiting item's path, then "x" and the path of each item placed elsewhere that lies inside a waiting one */
inline std::vector<std::string> HereWaitTokens(const std::vector<std::string>& waiting, const std::vector<std::string>& elsewhere)
{
    std::vector<std::string> o(waiting);
    for (size_t i = 0; i < elsewhere.size(); ++i)
        for (size_t j = 0; j < waiting.size(); ++j)
        {
            const std::string& e = elsewhere[i];
            const std::string& w = waiting[j];
            if (e.size() > w.size() && e.compare(0, w.size(), w) == 0 && e[w.size()] == '.') { o.push_back("x" + e); break; }
        }
    return o;
}
/* WHEN A NOTED CHANGE GOES INTO THE RECORD. A change noted when the engine had accepted `seq` save requests is held by save request
   reqSeq when reqSeq > seq (that save was asked for after it). At a world teardown with `cut` save requests accepted, a change noted
   at or after cut is dropped (no save holds it: HereTeardownKeeps); one before it (beforeCut) waits for the save in flight - a
   finished save request at or below cut that holds it commits it, a later one (asked for after the teardown) drops it. */
inline int HereEventHeld(long seq, long reqSeq) { return reqSeq > seq ? 1 : 0; }
inline int HereTeardownKeeps(long seq, long cut) { return seq < cut ? 1 : 0; }
const int kHereKeep = 0, kHereCommit = 1, kHereDrop = 2;
inline int HereFinishAction(long seq, int beforeCut, long cut, long reqSeq)
{
    if (beforeCut != 0 && reqSeq > cut) return kHereDrop;
    return HereEventHeld(seq, reqSeq) != 0 ? kHereCommit : kHereKeep;
}
inline std::string EncodeHere(const std::string& worldId, const std::map<unsigned, HereRow>& m)
{
    std::string o = std::string(kHereHeader) + "\t" + zonekeep::ZkNum(kHereFormat) + "\t" + HexOf(worldId) + "\n";
    for (std::map<unsigned, HereRow>::const_iterator it = m.begin(); it != m.end(); ++it)
    {
        const HereRow& h = it->second;
        std::string w;
        for (size_t k = 0; k < h.waits.size(); ++k) { if (k != 0) w += ","; w += h.waits[k]; }
        o += "h\t" + UNum(it->first) + "\t" + UNum(h.pieceHow) + "\t" + UNum(h.itemsHow) + "\t" + UNum(h.extrasDone) + "\t" + UNum(h.extrasEnded) + "\t"
           + UNum(h.doorsDone) + "\t" + (w.empty() ? std::string("-") : w) + "\n";
    }
    return o;
}
/* the record back for world worldId. False = not a record of format 1 or 2, another world's (*otherWorld = 1), or a line this build
   cannot read (*why names it); *out is then empty */
inline bool DecodeHere(const std::string& text, const std::string& worldId, std::map<unsigned, HereRow>* out, std::string* why, int* otherWorld = 0)
{
    out->clear();
    if (otherWorld != 0) *otherWorld = 0;
    std::map<unsigned, HereRow> m;
    std::vector<std::string> f;
    size_t at = 0;
    int lineNo = 0, format = 0;
    while (at < text.size())
    {
        size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(at, e - at);
        at = e + 1;
        ++lineNo;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        zonekeep::SplitTabs(line, &f);
        const std::string where = "line " + zonekeep::ZkNum(lineNo) + ": ";
        if (lineNo == 1)
        {
            if (f.size() != 3 || f[0] != kHereHeader || (f[1] != "1" && f[1] != zonekeep::ZkNum(kHereFormat))) { *why = where + "not a kept-laid-here record of format 1 or 2"; return false; }
            if (f[2] != HexOf(worldId)) { if (otherWorld != 0) *otherWorld = 1; *why = where + "the record of another world"; return false; }
            format = f[1] == "1" ? 1 : 2;
            continue;
        }
        if (line.empty()) continue;
        unsigned row = 0;
        HereRow h;
        bool ok = f.size() == (format == 1 ? 7u : 8u) && f[0] == "h" && UNumOf(f[1], &row) && UNumOf(f[2], &h.pieceHow) && UNumOf(f[3], &h.itemsHow)
                  && UNumOf(f[4], &h.extrasDone) && UNumOf(f[5], &h.extrasEnded) && UNumOf(f[6], &h.doorsDone);
        if (ok && format == 2 && f[7] != "-")
        {
            ok = !f[7].empty() && f[7].size() <= kHereWaitsMax;
            size_t a = 0;
            while (ok)
            {
                const size_t c = f[7].find(',', a);
                const std::string t = f[7].substr(a, c == std::string::npos ? std::string::npos : c - a);
                if (!HereTokenOk(t)) { ok = false; break; }
                h.waits.push_back(t);
                if (c == std::string::npos) break;
                a = c + 1;
            }
        }
        const bool fits = ok && (h.pieceHow == 0 || h.pieceHow == kHowMade || h.pieceHow == kHowPresent || h.pieceHow == kHowHandedBack)
                          && (h.itemsHow == 0 || h.itemsHow == kHowItemsLaid || h.itemsHow == kHowItemsHas) && (h.extrasDone & ~kXAll) == 0
                          && (h.extrasEnded & ~kXAll) == 0;
        if (!fits) { *why = where + "a line this build cannot read"; return false; }
        if (m.find(row) != m.end()) { *why = where + "row " + UNum(row) + " is named twice"; return false; }
        m[row] = h;
    }
    if (lineNo == 0) { *why = "the record is empty (no header line)"; return false; }
    out->swap(m);
    return true;
}
}   // namespace keptwire
