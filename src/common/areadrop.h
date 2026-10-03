/* src/common/areadrop.h - THE EFFECTIVE AREA MAP: WHAT EACH REPORTER IS TAKEN TO HOLD (W1-b).
 *
 * Same charter as areaclaim.h, storelink.h and clockmath.h: nothing in here reads a global, opens a file, or
 * includes a Windows, ENet or Ogre header. Every function is a pure function of its arguments, so
 * the rule that decides "another game has this sector loaded" is swept by the offline suite rather than only
 * by a run.
 *
 * THE RULE: a reporter LISTED in this map is taken exactly - its bits are this map's bits. A reporter ABSENT
 * from the whole map (its bit clear in every sector) keeps its last known bits until a session event says it is
 * gone (the session peer-gone path clears the whole effective map; a notebook restart does not, because the peer
 * did not go anywhere when the notebook did).
 *
 * WHY (T240, Confirmed; evidence/T240/READOUT.md "What the crash was"): the restarted notebook's first map knew
 * only A, so a peer not yet re-linked read as holding nothing: 241 UNLOADs then 241 re-spawns, and the other game
 * crashed rebuilding them in one frame. Review 2026-09-22 H3: W1's first answer (withdraw only on an OBSERVED
 * drop, through a queue) lost self-healing - a character walking out of a peer-held sector was never withdrawn,
 * and an absent reporter's history was overwritten by the next map. The effective map keeps the per-row
 * withdrawal test ("does the peer hold this sector?") and only changes what an absent reporter answers.
 *
 * THE PRE-PATCH EQUIVALENT (so a test can say what the old code would have answered): eff = newMask.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no brace-init.
 */
#ifndef COOP_COMMON_AREADROP_H
#define COOP_COMMON_AREADROP_H

#include <vector>   /* M9 (T-197): the seat book and the AREAMAP writer */
#include <cstddef>

namespace coopdrop {

/* WHICH SLOTS REPORTED ANYTHING IN THIS MAP - the OR of every sector's loadedMask. A slot whose bit is clear
   here said nothing to the notebook inside its grace window: it is ABSENT, not empty-handed. `masks` is the
   map's per-sector masks, `count` how many; count <= 0 (or a null pointer) is an empty map and answers 0. */
inline unsigned AreaDropReportersPresent(const unsigned* masks, int count)
{
    unsigned present = 0;
    int i;
    if (masks == 0) return 0;
    for (i = 0; i < count; ++i) present |= masks[i];
    return present;
}

/* ONE SECTOR'S EFFECTIVE MASK. `prevEff` is this sector's effective mask before the map, `newMask` its
   loadedMask in this map (0 when the map omits the sector), `present` AreaDropReportersPresent over the map.
     newMask             - a present reporter is taken exactly: its set bits are set, its clear bits clear;
     | (prevEff & ~present) - an absent reporter keeps whatever it was last known to hold here. */
inline unsigned AreaEffectiveMask(unsigned prevEff, unsigned newMask, unsigned present)
{
    return newMask | (prevEff & ~present);
}

/* ===== M9 (T-197; world-server protocol 65; design-many M9, manager decision D6 (a)) - "WHO HAS IT LOADED", 256 WIDE =====
   The loaded-by mask indexes a SEAT (0..255): the world server's own index for a player that has a loaded mark anywhere in its
   map (an AREAS report inside the 10 s grace). A seat is not a slot (slots run 0..65519 for a world's life, areaclaim.h
   kSlotLifetimeMax) and not the uid seat (0..1023, uidblock.h). Each AREAMAP carries its seat table, so a game maps a bit back
   to a player, and a world-server restart that re-seats players is followed by AreaEffectiveRekey on every game.
   WIRE (AREAMAP, message 35), little-endian:
       u16 seatCount, seatCount * (u8 seat, u16 slot), u16 rowCount,
       rowCount * (u8 x, u8 y, u16 owner slot (0xFFFF = nobody), u8 n, then n x u8 seat - or n = 0xFF, then a 32-byte seat mask =
       8 u32 words, bit k of word w = seat 32w+k). M9f1 (M9 review M2): a row is VARIABLE LENGTH - 5 + n bytes for n <= 31 seats,
       37 bytes (the mask) for 32 or more; it was a flat 36 bytes.
   THE LIMIT (owner rule S2-67): the world server links at most 256 games at once (store_main.cpp kMaxConnected; the 257th is
   refused in words), so 256 seats hold every CONNECTED player. A player who left keeps its marks for the 10 s grace, so under
   full churn more than 256 slots can want a seat for up to 10 s: the newcomer waits (AreaSeatAssign counts it unseated; the
   world server counts areaSeats[seatsFull], M9f1) and its bits are missing from the maps until a departed player's marks age out -
   the other games read it as not having those sectors loaded for at most those seconds (Inferred). Nothing overflows. */
const int kAreaSeats = 256;
const int kAreaMaskWords = 8;              /* 256 / 32 */
const int kAreaRowMinBytes = 5;           /* M9f1 (T-197; M9 review M2): u8 x, u8 y, u16 owner, u8 n = 0 */
const int kAreaRowMaxBytes = 37;          /* M9f1: u8 x, u8 y, u16 owner, u8 0xFF, 32-byte mask */
const int kAreaRowListMax = 31;           /* M9f1: a row naming at most 31 seats lists them (5 + n bytes); 32 or more goes as the mask */
const unsigned kAreaRowBitmap = 0xFFu;    /* M9f1: the n that says "the 32-byte mask follows" */
const int kPlayerSectorsMaxRows = 512;    /* M9f1 (M9 review LOW): PLAYERSECTORS (37) rows, both ends - twice the 256 connected games */
const double kAreaPendingSec = 15.0;      /* M9f1 (M9 review M1): a slot named gone with no column waits at most this long for a map */
const unsigned kAreaOwnerNone = 0xFFFFu;   /* the owner column of a row nobody holds; also the one u16 no slot can take */
const int kAreaMaxRows = 4096;             /* 64 x 64 sectors */

inline void AreaMaskClear(unsigned* m) { int w; for (w = 0; w < kAreaMaskWords; ++w) m[w] = 0u; }
inline void AreaMaskSet(unsigned* m, int seat) { if (seat >= 0 && seat < kAreaSeats) m[seat >> 5] |= (1u << (seat & 31)); }
inline int AreaMaskTest(const unsigned* m, int seat) { return (seat >= 0 && seat < kAreaSeats) ? (int)((m[seat >> 5] >> (seat & 31)) & 1u) : 0; }
inline int AreaMaskAny(const unsigned* m) { int w; for (w = 0; w < kAreaMaskWords; ++w) if (m[w] != 0u) return 1; return 0; }

/* ONE SECTOR'S EFFECTIVE MASK, 256 WIDE: AreaEffectiveMask word by word - `new | (prev & ~present)` has no carry between bits,
   so it is word-independent. eff (updated in place), newMask and present are kAreaMaskWords words each. */
inline void AreaEffectiveMaskWords(unsigned* eff, const unsigned* newMask, const unsigned* present)
{
    int w;
    for (w = 0; w < kAreaMaskWords; ++w) eff[w] = AreaEffectiveMask(eff[w], newMask[w], present[w]);
}

/* the seat `slot` holds in a seat book (slotOfSeat[kAreaSeats], -1 = free), or -1 */
inline int AreaSeatOfSlot(const int* slotOfSeat, int slot)
{
    int j;
    if (slot < 0) return -1;
    for (j = 0; j < kAreaSeats; ++j) if (slotOfSeat[j] == slot) return j;
    return -1;
}

/* THE WORLD SERVER'S SEAT BOOK, once a map. `listed` = every slot with a fresh loaded mark (distinct). A seat whose slot is not
   listed is freed; a listed slot without a seat takes the LOWEST free one; a slot keeps its seat for as long as it stays listed.
   Returns how many listed slots found no seat (every seat held). */
inline int AreaSeatAssign(int* slotOfSeat, const std::vector<int>& listed, int* took, int* freed)
{
    int j, unseated = 0, t = 0, f = 0;
    size_t k;
    for (j = 0; j < kAreaSeats; ++j)
    {
        if (slotOfSeat[j] < 0) continue;
        bool keep = false;
        for (k = 0; k < listed.size(); ++k) if (listed[k] == slotOfSeat[j]) { keep = true; break; }
        if (!keep) { slotOfSeat[j] = -1; ++f; }
    }
    for (k = 0; k < listed.size(); ++k)
    {
        if (listed[k] < 0 || AreaSeatOfSlot(slotOfSeat, listed[k]) >= 0) continue;
        for (j = 0; j < kAreaSeats && slotOfSeat[j] >= 0; ++j) {}
        if (j >= kAreaSeats) { ++unseated; continue; }
        slotOfSeat[j] = listed[k]; ++t;
    }
    if (took != 0) *took = t;
    if (freed != 0) *freed = f;
    return unseated;
}

inline void AreaPutU16(std::vector<char>* b, unsigned v) { b->push_back((char)(unsigned char)(v & 0xFFu)); b->push_back((char)(unsigned char)((v >> 8) & 0xFFu)); }
inline unsigned AreaGetU16(const unsigned char* p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }

/* the WRITER (world server): the seat table from its seat book; returns how many seats went out */
inline int AreaMapPutSeats(std::vector<char>* b, const int* slotOfSeat)
{
    const size_t at = b->size();
    int n = 0, j;
    AreaPutU16(b, 0u);
    for (j = 0; j < kAreaSeats; ++j)
    {
        if (slotOfSeat[j] < 0 || slotOfSeat[j] >= (int)kAreaOwnerNone) continue;
        b->push_back((char)(unsigned char)j);
        AreaPutU16(b, (unsigned)slotOfSeat[j]);
        ++n;
    }
    (*b)[at] = (char)(unsigned char)(n & 0xFF); (*b)[at + 1] = (char)(unsigned char)((n >> 8) & 0xFF);
    return n;
}
/* ... and one row (owner < 0, or one no u16 can name, goes out as nobody). M9f1 (T-197; M9 review M2, D7 (a)): the seat list is
   VARIABLE LENGTH - u8 n, then the n seats ascending (n <= kAreaRowListMax: 5 + n bytes), or n = kAreaRowBitmap (0xFF) then the
   32-byte mask (32 seats or more: 37 bytes; 256 seats fit, which a plain u8 count could not name). */
inline void AreaMapPutRow(std::vector<char>* b, int x, int y, int owner, const unsigned* mask)
{
    int w, j, n = 0;
    b->push_back((char)(unsigned char)x); b->push_back((char)(unsigned char)y);
    AreaPutU16(b, (owner >= 0 && owner < (int)kAreaOwnerNone) ? (unsigned)owner : kAreaOwnerNone);
    for (j = 0; j < kAreaSeats; ++j) if (AreaMaskTest(mask, j)) ++n;
    if (n <= kAreaRowListMax)
    {
        b->push_back((char)(unsigned char)n);
        for (j = 0; j < kAreaSeats; ++j) if (AreaMaskTest(mask, j)) b->push_back((char)(unsigned char)j);
        return;
    }
    b->push_back((char)(unsigned char)kAreaRowBitmap);
    for (w = 0; w < kAreaMaskWords; ++w)
    {
        const unsigned v = mask[w];
        b->push_back((char)(unsigned char)(v & 0xFFu)); b->push_back((char)(unsigned char)((v >> 8) & 0xFFu));
        b->push_back((char)(unsigned char)((v >> 16) & 0xFFu)); b->push_back((char)(unsigned char)((v >> 24) & 0xFFu));
    }
}

/* one row of a map p[0..n) starting at *at (M9f1): x, y, owner (-1 = nobody) and its 8-word seat mask; *at moves past the row.
   Returns 0 - and writes nothing - when the row runs past n. Every read is bounds-checked against n. */
inline int AreaMapNextRow(const unsigned char* p, size_t n, size_t* at, int* x, int* y, int* owner, unsigned* mask)
{
    const size_t a = *at;
    unsigned k, o, i;
    int w;
    if (p == 0 || a > n || n - a < (size_t)kAreaRowMinBytes) return 0;
    k = (unsigned)p[a + 4];
    if (k == kAreaRowBitmap) { if (n - a < (size_t)kAreaRowMaxBytes) return 0; }
    else if (n - a < (size_t)kAreaRowMinBytes + (size_t)k) return 0;
    *x = (int)p[a]; *y = (int)p[a + 1];
    o = AreaGetU16(p + a + 2);
    *owner = (o == kAreaOwnerNone) ? -1 : (int)o;
    AreaMaskClear(mask);
    if (k == kAreaRowBitmap)
    {
        const unsigned char* r = p + a + 5;
        for (w = 0; w < kAreaMaskWords; ++w)
            mask[w] = (unsigned)r[w * 4] | ((unsigned)r[w * 4 + 1] << 8) | ((unsigned)r[w * 4 + 2] << 16) | ((unsigned)r[w * 4 + 3] << 24);
        *at = a + (size_t)kAreaRowMaxBytes;
    }
    else
    {
        for (i = 0; i < k; ++i) AreaMaskSet(mask, (int)p[a + 5 + i]);
        *at = a + (size_t)kAreaRowMinBytes + (size_t)k;
    }
    return 1;
}

/* the READER (game): checks the layout and fills listed[kAreaSeats] (slot of each seat in this map's table, -1 = not listed).
   Returns 0 - and the caller applies nothing - on a malformed map: a length short of its own counts (M9f1: every row is WALKED
   here, so a seat list running past the end is caught before anything is applied), over 256 seats or 4096 rows, a seat or a slot
   listed twice, or a listed slot of 0xFFFF. Trailing bytes are allowed (a later field). *rowsAt = where row 0 starts: read the rows
   with AreaMapNextRow from there. */
inline int AreaMapLayout(const unsigned char* p, size_t n, int* listed, int* seatCount, size_t* rowsAt, int* rowCount)
{
    int j, x, y, o;
    unsigned sc, rc, scratch[kAreaMaskWords];
    size_t at, first;
    for (j = 0; j < kAreaSeats; ++j) listed[j] = -1;
    if (p == 0 || n < 4) return 0;
    sc = AreaGetU16(p);
    if (sc > (unsigned)kAreaSeats || n < 2 + (size_t)sc * 3 + 2) return 0;
    for (j = 0; j < (int)sc; ++j)
    {
        const int seat = (int)p[2 + j * 3];
        const int slot = (int)AreaGetU16(p + 3 + j * 3);
        if (slot == (int)kAreaOwnerNone || listed[seat] >= 0 || AreaSeatOfSlot(listed, slot) >= 0) return 0;
        listed[seat] = slot;
    }
    at = 2 + (size_t)sc * 3;
    rc = AreaGetU16(p + at);
    at += 2;
    if (rc > (unsigned)kAreaMaxRows) return 0;
    first = at;
    for (j = 0; j < (int)rc; ++j) if (!AreaMapNextRow(p, n, &at, &x, &y, &o, scratch)) return 0;
    if (seatCount != 0) *seatCount = (int)sc;
    if (rowsAt != 0) *rowsAt = first;
    if (rowCount != 0) *rowCount = (int)rc;
    return 1;
}

/* THE REKEY (a world-server restart re-seats players). `book` = the game's seat book before this map (the slot each column of its
   effective map belongs to, -1 = none): the previous map's seat table plus the absent slots whose bits it still carries.
   `listed` = this map's seat table. Fills src[j] = the old column that becomes column j (-1 = an empty column) and out[] = the
   book after this map:
     - a listed slot's column follows it to the seat this map gives it;
     - an absent (unlisted) slot keeps its column where it was when that seat is not listed now;
     - an absent slot whose seat a listed slot now holds moves to the lowest seat nobody holds; with none left its bits are
       dropped (*dropped) - which needs more than 256 players with bits at once.
   Returns how many columns moved. The caller applies src to every cell (AreaEffectiveRekeyCell) BEFORE the map is applied. */
inline int AreaEffectiveRekey(const int* book, const int* listed, int* src, int* out, int* dropped)
{
    int j, i, moved = 0, lost = 0;
    int used[kAreaSeats];
    for (j = 0; j < kAreaSeats; ++j) { src[j] = -1; out[j] = -1; used[j] = 0; }
    for (j = 0; j < kAreaSeats; ++j)
    {
        if (listed[j] < 0) continue;
        out[j] = listed[j];
        i = AreaSeatOfSlot(book, listed[j]);
        if (i >= 0) { src[j] = i; used[i] = 1; if (i != j) ++moved; }
    }
    for (i = 0; i < kAreaSeats; ++i)   /* absent slots whose seat is still free stay put */
        if (book[i] >= 0 && !used[i] && out[i] < 0) { out[i] = book[i]; src[i] = i; used[i] = 1; }
    for (i = 0; i < kAreaSeats; ++i)   /* absent slots whose seat a listed slot took move to a free one */
    {
        if (book[i] < 0 || used[i]) continue;
        for (j = 0; j < kAreaSeats && out[j] >= 0; ++j) {}
        if (j >= kAreaSeats) { ++lost; continue; }
        out[j] = book[i]; src[j] = i; used[i] = 1; ++moved;
    }
    if (dropped != 0) *dropped = lost;
    return moved;
}
/* one cell (or any 8-word mask) through a rekey plan: bit src[j] of `in` becomes bit j of `out` */
inline void AreaEffectiveRekeyCell(const unsigned* in, const int* src, unsigned* out)
{
    int j;
    AreaMaskClear(out);
    for (j = 0; j < kAreaSeats; ++j) if (src[j] >= 0 && AreaMaskTest(in, src[j])) AreaMaskSet(out, j);
}

/* ===== M9f1 (T-197; M9 review M1 + LOW "the apply core"): THE GAME'S APPLY CORE, PURE =====
   zones.cpp ApplyRelayAreaMap keeps the grids and the lock; everything that decides WHICH BITS a map leaves in the effective map is
   here, so the offline suite runs the same code on a world-server restart and on the pending cases.
   AreaBook = the slot each seat COLUMN of the effective map belongs to (-1 none: the latest map's seat table plus the absent slots
   whose bits are still carried), the ARMED seats (W1-c: their next absence drops their bits instead of carrying them) and the
   PENDING slots (named gone before any map gave them a column).
   M9 review M1: a pending entry used to wait for a map that listed its slot, for ever - so when that player rejoined the old news
   armed its NEW seat and its next absence (a notebook blip) dropped its bits: townspeople twice, then a withdraw storm for that one
   player. Now the first non-empty map that does not list the slot drops the entry (this game holds no bits of it to drop), and
   an entry is dropped after kAreaPendingSec whatever comes (empty maps, a link outage). */
struct AreaBook
{
    int slotOfSeat[kAreaSeats];
    unsigned dropOnAbsence[kAreaMaskWords];
    int pendingSlot[kAreaSeats];
    double pendingAt[kAreaSeats];
    int pendingCount;
};
inline void AreaBookDisarm(AreaBook* b) { AreaMaskClear(b->dropOnAbsence); b->pendingCount = 0; }
inline void AreaBookInit(AreaBook* b)
{
    int j;
    for (j = 0; j < kAreaSeats; ++j) { b->slotOfSeat[j] = -1; b->pendingSlot[j] = -1; b->pendingAt[j] = 0.0; }
    AreaBookDisarm(b);
}
/* drops every pending entry older than kAreaPendingSec at `now`; returns how many */
inline int AreaBookExpirePending(AreaBook* b, double now)
{
    int k, kept = 0, gone = 0;
    for (k = 0; k < b->pendingCount; ++k)
    {
        if (now - b->pendingAt[k] > kAreaPendingSec) { ++gone; continue; }
        b->pendingSlot[kept] = b->pendingSlot[k]; b->pendingAt[kept] = b->pendingAt[k]; ++kept;
    }
    b->pendingCount = kept;
    return gone;
}
/* a slot named gone (PLAYER_GONE or the session peer-gone). seat = its column (-1 none), bits = cooppg::PeerGoneMaskBits's answer
   (the caller clears them from its cells). The bits are armed; a known slot with no column goes pending (named again: its clock
   restarts; at 256 entries the oldest goes). Returns 1 when the slot is pending. */
inline int AreaBookForget(AreaBook* b, int slot, int seat, const unsigned* bits, double now)
{
    int w, k;
    if (bits != 0) for (w = 0; w < kAreaMaskWords; ++w) b->dropOnAbsence[w] |= bits[w];
    if (slot < 0 || seat >= 0) return 0;
    for (k = 0; k < b->pendingCount; ++k) if (b->pendingSlot[k] == slot) { b->pendingAt[k] = now; return 1; }
    if (b->pendingCount >= kAreaSeats)
    {
        for (k = 1; k < b->pendingCount; ++k) { b->pendingSlot[k - 1] = b->pendingSlot[k]; b->pendingAt[k - 1] = b->pendingAt[k]; }
        --b->pendingCount;
    }
    b->pendingSlot[b->pendingCount] = slot; b->pendingAt[b->pendingCount] = now; ++b->pendingCount;
    return 1;
}
struct AreaApplyTally
{
    int moved, lost;                                   /* the rekey: columns moved, columns dropped (no free seat) */
    int pendingArmed, pendingDropped, pendingExpired;  /* pending slots: armed at the seat this map gave them, not listed, too old */
    int absentOther, droppedArmed;                     /* 1 when some other absent column was carried / an armed one dropped */
    int carried;                                       /* cells whose effective mask differs from this map's own */
    int freed;                                         /* absent columns empty everywhere, given back */
};
const unsigned char kAreaCellMineBefore = 1, kAreaCellMineNow = 2, kAreaCellOther = 4, kAreaCellCarried = 8;
/* ONE NON-EMPTY CHECKED MAP. listed = AreaMapLayout's seat table; cells = cellCount effective masks of kAreaMaskWords words,
   updated in place; newMasks = this map's row mask per cell (all zero for a cell the map omits); cellFlags = cellCount bytes out
   (kAreaCell*; may be 0). In order:
     1. pending entries older than kAreaPendingSec go;
     2. THE REKEY: every column follows its slot to the seat this map gives it (AreaEffectiveRekey) - cells and arms move with it;
     3. every pending slot is settled: this map gave it a column -> its seat is armed; it did not -> the entry is dropped;
     4. my seat (whatever my slot's number; none -> every bit is somebody else) and `present` = the seat table;
     5. every cell: eff = (new & present) | (eff & ~(present | armed)) - a row bit for a seat the table does not list is ignored;
     6. an armed seat absent from this map has been dropped, its arm is spent; an absent column empty everywhere leaves the book. */
inline void AreaApplyMapCore(AreaBook* b, const int* listed, int mySlot, double now, unsigned* cells, const unsigned* newMasks,
                             int cellCount, unsigned char* cellFlags, AreaApplyTally* t)
{
    int j, c, w, k;
    int src[kAreaSeats], book[kAreaSeats];
    unsigned mine[kAreaMaskWords], present[kAreaMaskWords], pd[kAreaMaskWords], oldAny[kAreaMaskWords], newAny[kAreaMaskWords], tmp[kAreaMaskWords];
    t->moved = 0; t->lost = 0; t->pendingArmed = 0; t->pendingDropped = 0; t->pendingExpired = 0;
    t->absentOther = 0; t->droppedArmed = 0; t->carried = 0; t->freed = 0;
    t->pendingExpired = AreaBookExpirePending(b, now);
    t->moved = AreaEffectiveRekey(b->slotOfSeat, listed, src, book, &t->lost);
    if (t->moved + t->lost > 0)
    {
        for (c = 0; c < cellCount; ++c)
        {
            unsigned* e = cells + (size_t)c * (size_t)kAreaMaskWords;
            AreaEffectiveRekeyCell(e, src, tmp);
            for (w = 0; w < kAreaMaskWords; ++w) e[w] = tmp[w];
        }
        AreaEffectiveRekeyCell(b->dropOnAbsence, src, tmp);
        for (w = 0; w < kAreaMaskWords; ++w) b->dropOnAbsence[w] = tmp[w];
    }
    for (j = 0; j < kAreaSeats; ++j) b->slotOfSeat[j] = book[j];
    for (k = 0; k < b->pendingCount; ++k)
    {
        const int st = AreaSeatOfSlot(b->slotOfSeat, b->pendingSlot[k]);
        if (st >= 0) { AreaMaskSet(b->dropOnAbsence, st); ++t->pendingArmed; }
        else ++t->pendingDropped;
    }
    b->pendingCount = 0;
    AreaMaskClear(mine); AreaMaskSet(mine, AreaSeatOfSlot(b->slotOfSeat, mySlot));
    AreaMaskClear(present);
    for (j = 0; j < kAreaSeats; ++j) if (listed[j] >= 0) AreaMaskSet(present, j);
    for (w = 0; w < kAreaMaskWords; ++w) { pd[w] = present[w] | b->dropOnAbsence[w]; oldAny[w] = 0u; newAny[w] = 0u; }
    for (c = 0; c < cellCount; ++c)
    {
        unsigned* e = cells + (size_t)c * (size_t)kAreaMaskWords;
        const unsigned* nm = newMasks + (size_t)c * (size_t)kAreaMaskWords;
        unsigned char f = 0;
        for (w = 0; w < kAreaMaskWords; ++w) { oldAny[w] |= e[w]; if ((e[w] & mine[w]) != 0u) f = (unsigned char)(f | kAreaCellMineBefore); }
        for (w = 0; w < kAreaMaskWords; ++w)
        {
            const unsigned nw = nm[w] & present[w];
            e[w] = AreaEffectiveMask(e[w], nw, pd[w]);
            if ((e[w] & mine[w]) != 0u) f = (unsigned char)(f | kAreaCellMineNow);
            if ((e[w] & ~mine[w]) != 0u) f = (unsigned char)(f | kAreaCellOther);
            if (e[w] != nw) f = (unsigned char)(f | kAreaCellCarried);
            newAny[w] |= e[w];
        }
        if ((f & kAreaCellCarried) != 0) ++t->carried;
        if (cellFlags != 0) cellFlags[c] = f;
    }
    for (w = 0; w < kAreaMaskWords; ++w)
    {
        if ((oldAny[w] & ~present[w] & ~mine[w] & ~b->dropOnAbsence[w]) != 0u) t->absentOther = 1;
        if ((oldAny[w] & ~present[w] & b->dropOnAbsence[w]) != 0u) t->droppedArmed = 1;
        b->dropOnAbsence[w] &= present[w];
    }
    for (j = 0; j < kAreaSeats; ++j)
        if (b->slotOfSeat[j] >= 0 && listed[j] < 0 && !AreaMaskTest(newAny, j)) { b->slotOfSeat[j] = -1; ++t->freed; }
}

}   /* namespace coopdrop */

#endif
