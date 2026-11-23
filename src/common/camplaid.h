#pragma once
/* src/common/camplaid.h - T-1429 CAMP NOTE design (F1608, after review F1605; replaces design (b)'s version-keyed camp mark): WHICH
 * CAMPS ARE LAID IN AN AREA is written INSIDE THE AREA FILE (the camp note), and the pieces one game laid for a camp go to the others
 * as one small world-server record per area (the camp pieces record). Pure rules and formats the furniture step, the note, the record
 * and the world server share.
 *
 * WHY. The engine lays a camp's pieces (furniture, small buildings, boxes - the camp maker 0x92F250) at the furniture step 0x937570,
 * from a random sequence, on every visit (F1591). Design A1 keeps the laid pieces in the engine's own area save (the 'needs saving'
 * hook, campgame.h CampPieceSaveAnswer - main-thread saves only, so the pieces and the note written in the same save always agree).
 *
 * THE KEY of one nest entry (one furniture step): <town>|<template id>|<building>
 *   town      a camp on the world's camp list: its CampKey (campwire.h, kind@x,z - the same text on every game); any other town: the
 *             town's position key (items.h ObjectPositionKey - record id, area, position in tenths; F1608 Q5 - two towns of one type in
 *             one area square no longer share it)
 *   template  the camp template's record id (the queue item's +0x28, the step's 2nd argument)
 *   building  the nest building's position key (ObjectPositionKey), or "centre" for the town's own entry (no building)
 * Read path (decomp_927540:70-78): the runner takes the queue item and calls the town's table slot +0x388 = 0x937570 with (town,
 * item+0x28, &item+0x30, the building resolved from item+0x40) - the hooked step (camp.cpp detour_cpDebris) gets exactly those four.
 *
 * THE CAMP NOTE (store.cpp CampNoteBeforeSave / CampNoteAfterLoad): one record of type 0x1E in the area file beside the town note,
 * fields file ("1" once whole) / zone / n / k0..k<n-1> (the keys, each once). Its order against the step (step 0 of the build, Read):
 * the area file is read inside ZoneMapContent::_activate 0x9FEC00 (decomp_9fec00:147), which sets its done byte (:406-408); 0xA0D890
 * waits for that byte (a0d890:82) before state 3 sets up the towns - 'Town loaded' 0x92E390 (decomp_a0dbe0:216) queues the nest
 * entries (0x928110) that the runner 0x927540 later hands to the step; so the note is read BEFORE any step of that load.
 *   READ (CampNoteOnRead): every area file read REPLACES the area's note (no note in the file = empty: a base-game-data read, or a save
 *     made before the note); a read that finds no file changes nothing.
 *   ADD: a step that lays here, and a camp pieces record applied here, add the key.
 *   WRITE: every main-thread save of the area writes the note it holds (off-thread saves keep no camp piece and carry no note).
 *
 * THE STEP (CampNoteStepDecide): the key is in the note -> make nothing (the area copy loaded here carries the camp); else this game is
 *   the area's CAMP LAYER - single player, the world-server link down, or the world server's area-load answer named this game (areafresh.h
 *   kLayerYou; also this game's own fresh map naming it the holder when it asked nothing) -> the engine's step lays, the key goes into the
 *   note and (online) into the area's camp pieces record; else ('owed': the answer named another game, or no answer while the link is up -
 *   F1612 item 1) -> make nothing: the layer's record brings the pieces. A key that cannot be built (or a step off the main thread) makes
 *   nothing while the keep is on (F1612 item 5: no saved piece without its note entry).
 *
 * THE CAMP PIECES RECORD, id `camplaid.<sx>.<sy>` (the area's square, 0..63), bytes little-endian (a `.platoon` file on the world
 * server, message 28 RECORD on the wire):
 *   u32 marker 'CLD1' | u32 format (kClFormat 3) | u32 generation | i32 writer slot | i32 sx | i32 sy | u32 n (0..kClEntryMax) |
 *   n x { u32 length + key (1..kClKeyMax bytes, no control byte) | u32 m (0..kClPieceMax) |
 *         m x { u32 length + record id (1..kClSidMax) | f32 x, y, z | f32 qw, qx, qy, qz | u8 inside (format 3 only) } } - keys strictly
 *   rising (each once). T-1429 F1753 design (a), format 3: `inside` is what the camp maker did with the piece on the camp's game - 0 made
 *   outdoors (no parent), 1 made inside the camp's nest building (the building the key's building part names), 2 not known; read on the
 *   camp's game from the piece's own parent (CampPieceInsideFact). The decoder also takes format 2 (no byte): each piece reads 2 (not
 *   known), and only such an old record's piece is placed by the point lookup (the guess F1723 found wrong at a shack's edge).
 * A record cut short, with an unknown marker or format, a square out of 0..63, a bad / repeated / unsorted key, a piece count over the
 * cap, a position or rotation that is not a finite number, or bytes after the last field DECODES TO REFUSED. A key once in the record
 * keeps its pieces (CampLaidWithEntry: the first layer's pieces stand). Box loot inside camp pieces is NOT carried.
 *   APPLY (CampLaidApplyPick): a game applies an entry only while its area's note lacks that key - live (the record arrives while the
 *   area is loaded here) or after a load - making each piece as the camp maker does, then adds the key to its note.
 *   WORLD SERVER (CampLaidJudgeWrite): towntime.h's rule - the area's holder only, naming itself as writer, a rising generation, the same
 *   bytes again a repeat; a refusal is answered (BOX_REFUSE, the stored record with an older / same-generation one). Never removed; a
 *   world reset drops it with the area's other records (worldreset.h MOVE: a `.platoon` record file).
 *
 * Pure: no engine memory, no Windows, no globals. C++03 (VS2010 v100).
 */

#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "boxrec.h"   /* the byte helpers and the refusal reasons the record shares */
#include "poskey.h"   /* T-1429 F1671: a piece's square (the engine's own sector formula) */

namespace camplaid {

const unsigned int kProtocol = 177;            /* the world-server protocol that carries camplaid records (T-1429; the camp note design keeps 174 - unreleased; 176: record format 3, the inside byte - F1753, unreleased) */
const unsigned int kClMarker = 0x31444C43u;    /* 'C','L','D','1' little-endian */
const unsigned int kClFormat = 3u;             /* 3 (T-1429 F1753 design a): each piece carries its inside byte; 2: the camp pieces record without it (still read: inside 2); format 1, design (b)'s key -> seq mark, is refused */
const unsigned int kClFormatNoInside = 2u;     /* the last format without the inside byte (DecodeCampLaidRec takes it: every piece reads kClInsideUnknown) */
const int kClInsideOut = 0, kClInsideNest = 1, kClInsideUnknown = 2;   /* F1753: a piece's inside byte - outdoors / inside the camp's nest building / not known */
const unsigned int kClKeyMax = 240u;
const unsigned int kClPartMax = 160u;
const unsigned int kClSidMax = 96u;
const unsigned int kClEntryMax = 128u;
const unsigned int kClPieceMax = 64u;
const unsigned int kClMaxBytes = 262144u;
const int kCnNoteMax = 256;                    /* keys one area's note holds (the rest are not written; counted) */
const int kClOk = 0, kClShort = 1, kClBadMarker = 2, kClBadFormat = 3, kClBadRecord = 4, kClTrailing = 5, kClTooBig = 6;

struct CampPiece { std::string sid; float x, y, z, qw, qx, qy, qz; int inside; CampPiece() : x(0), y(0), z(0), qw(1), qx(0), qy(0), qz(0), inside(kClInsideUnknown) {} };
struct CampLaidEntry { std::string key; std::vector<CampPiece> pieces; };
struct CampLaidRec
{
    unsigned int generation;
    int writerSlot;
    int sx, sy;
    std::vector<CampLaidEntry> laid;   /* strictly rising by key */
    CampLaidRec() : generation(0), writerSlot(0), sx(0), sy(0) {}
};

/* ---- the id ---- */
inline bool ClSquareOk(int v) { return v >= 0 && v <= 63; }
inline std::string MakeCampLaidId(int sx, int sy)
{
    if (!ClSquareOk(sx) || !ClSquareOk(sy)) return std::string();
    char b[32];
    std::sprintf(b, "camplaid.%d.%d", sx, sy);
    return std::string(b);
}
/* any spelling of the prefix counts (file names ignore case); only the spelling MakeCampLaidId makes parses */
inline bool IsCampLaidId(const std::string& id)
{
    static const char kPrefix[] = "camplaid.";
    if (id.size() < 9) return false;
    for (size_t i = 0; i < 9; ++i) { char c = id[i]; if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); if (c != kPrefix[i]) return false; }
    return true;
}
/* 1 = exactly a spelling MakeCampLaidId makes (its square into the pointers that are not 0); 0 = not a camplaid id */
inline int CampLaidIdParse(const std::string& id, int* sx, int* sy)
{
    if (id.size() < 12 || id.compare(0, 9, "camplaid.") != 0) return 0;
    size_t at = 9;
    int v[2] = { 0, 0 };
    for (int k = 0; k < 2; ++k)
    {
        const size_t s = at;
        int n = 0;
        while (at < id.size() && id[at] >= '0' && id[at] <= '9' && at - s < 2) { n = n * 10 + (id[at] - '0'); ++at; }
        if (at == s) return 0;
        if (k == 0) { if (at >= id.size() || id[at] != '.') return 0; ++at; }
        v[k] = n;
    }
    if (at != id.size() || MakeCampLaidId(v[0], v[1]) != id) return 0;   /* no leading zero, square in range, nothing after */
    if (sx) *sx = v[0];
    if (sy) *sy = v[1];
    return 1;
}

/* ---- the key ---- */
inline bool ClTextOk(const std::string& s, size_t cap)
{
    if (s.empty() || s.size() > cap) return false;
    for (size_t i = 0; i < s.size(); ++i) if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] == 0x7F) return false;
    return true;
}
/* the whole key; town = a list camp's CampKey or the town's position key; building "" = the town's own entry ("centre"). "" when a
   part is missing / not clean or the key is too long */
inline std::string CampLaidKey(const std::string& town, const std::string& tpl, const std::string& building)
{
    if (!ClTextOk(town, kClPartMax) || !ClTextOk(tpl, kClPartMax)) return std::string();
    const std::string b = building.empty() ? std::string("centre") : building;
    if (!ClTextOk(b, kClPartMax)) return std::string();
    const std::string k = town + "|" + tpl + "|" + b;
    return k.size() <= kClKeyMax ? k : std::string();
}
/* the town part of a key ("" when the key has no '|') */
inline std::string CampLaidKeyTown(const std::string& key)
{
    const size_t b = key.find('|');
    return b == std::string::npos ? std::string() : key.substr(0, b);
}
/* T-1429 review fold F1612 item 6: the key's building part - the nest building's position key; "" for the town's own entry ("centre") */
inline std::string CampLaidKeyBuilding(const std::string& key)
{
    const size_t e = key.rfind('|');
    if (e == std::string::npos || key.find('|') == e) return std::string();
    const std::string b = key.substr(e + 1);
    return b == "centre" ? std::string() : b;
}

/* ---- the record ---- */
inline bool ClFinite(float f) { return f == f && f < 3.0e38f && f > -3.0e38f; }
inline bool CampPieceValid(const CampPiece& p)
{
    return ClTextOk(p.sid, kClSidMax) && ClFinite(p.x) && ClFinite(p.y) && ClFinite(p.z) && ClFinite(p.qw) && ClFinite(p.qx) && ClFinite(p.qy) && ClFinite(p.qz)
        && p.inside >= kClInsideOut && p.inside <= kClInsideUnknown;
}
/* T-1429 F1753 design (a): A PIECE'S INSIDE FACT ON THE CAMP'S GAME, read from the piece itself right after the camp maker made it. top: the
   piece's parent words (build.cpp BdBuildingTopLevelPod: 1 top level - no parent hand, no layout; 0 inside a building; -1 a fault);
   parentIsNest: 1 = the building its parent hand / layout names (items.cpp LayoutOuterBuilding) IS the step's nest building, 0 = another
   building, none or not read. The camp maker puts a piece only inside the step's nest building or loose (decomp_92f250:188, 260-270), so
   a piece inside any other building is a mismatch: not known (2), never guessed - the caller logs and counts it (*mismatch = 1). */
inline int CampPieceInsideFact(int top, int parentIsNest, int* mismatch)
{
    if (mismatch != 0) *mismatch = 0;
    if (top == 1) return kClInsideOut;
    if (top == 0 && parentIsNest == 1) return kClInsideNest;
    if (top == 0 && mismatch != 0) *mismatch = 1;
    return kClInsideUnknown;
}
/* F1753: how the apply makes one piece - kMakeOutdoors, kMakeInNest, kMakeWaitNest (inside, its nest building not here yet: the entry
   waits for it - never made outdoors, no timer), kMakeNestGone (inside, but its nest building is GONE by the one rule CampNestGone: NOT
   made - it lived in a building that no longer exists; the record only adds entries, so an old layout's entry stays for good, and furniture
   must not stand floating where the building was), kMakeOldGuess (an old record's piece, inside not known: the point lookup decides as
   before F1753, counted). nestHere: the key's nest building is standing here now; nestGone: CampNestGone said gone. */
const int kMakeOutdoors = 0, kMakeInNest = 1, kMakeWaitNest = 2, kMakeNestGone = 3, kMakeOldGuess = 4;
inline int CampPieceMakeRoad(int inside, int nestHere, int nestGone)
{
    if (inside == kClInsideOut) return kMakeOutdoors;
    if (inside == kClInsideNest) return nestHere ? kMakeInNest : (nestGone ? kMakeNestGone : kMakeWaitNest);
    return kMakeOldGuess;
}
/* T-1429 F1753 review fold (MEDIUM, re-check fold): THE ONE 'NEST GONE' RULE, the same whatever this game saw before - ended by an event, no
   history, no timer. The nest's square follows from its position key (CampNestSquareOfKey). A nest still not found is gone (destroyed,
   moved, or never in this world's copy) only when that square is LOADED here now (squareLoaded: its buildings are placed - a file read alone
   is not enough, the read can come before the placing), its file read belongs to this load (readThisLoad), and the entry's town has run a
   camp step AFTER that read (townStepSeq > readStepSeq: the town's last step number against the step count stamped at the square's read -
   a step of the town from before the square came is not enough). Anything less: the entry waits for the next event. 1 = gone. */
inline int CampNestGone(int nestHere, int squareLoaded, int readThisLoad, long townStepSeq, long readStepSeq)
{
    return (nestHere == 0 && squareLoaded != 0 && readThisLoad != 0 && townStepSeq > 0 && townStepSeq > readStepSeq) ? 1 : 0;
}
/* the square a nest building's position key names ("<sid>@<sx>,<sy>@<x10>,<z10>,<y10>", poskey.h); 1 = read */
inline int CampNestSquareOfKey(const std::string& bk, int* sx, int* sy)
{
    char sid[poskey::kSidRoom]; long x10 = 0, z10 = 0; int x = -1, y = -1;
    if (bk.empty() || poskey::ParseKeySpot(bk.c_str(), sid, (int)sizeof sid, &x, &y, &x10, &z10) != 1 || x < 0 || x > 63 || y < 0 || y > 63) return 0;
    *sx = x; *sy = y;
    return 1;
}
/* LOW 1 (review fold): an entry whose inside piece could not be made because its nest building did not read (CampPieceMake why 5) keeps its
   key OUT of the square's note - the square's next event (a camp step, an area read, a square settled here) tries it again; no timer. */
inline int CampEntryNoteAfterMake(int nestUnread) { return nestUnread == 0 ? 1 : 0; }
inline bool CampLaidRecValid(const CampLaidRec& r)
{
    if (!ClSquareOk(r.sx) || !ClSquareOk(r.sy) || r.laid.size() > kClEntryMax) return false;
    for (size_t i = 0; i < r.laid.size(); ++i)
    {
        if (!ClTextOk(r.laid[i].key, kClKeyMax) || r.laid[i].pieces.size() > kClPieceMax) return false;
        if (i > 0 && !(r.laid[i - 1].key < r.laid[i].key)) return false;
        for (size_t j = 0; j < r.laid[i].pieces.size(); ++j) if (!CampPieceValid(r.laid[i].pieces[j])) return false;
    }
    return true;
}
inline void ClPutF(std::vector<char>* out, float f) { unsigned int u = 0; std::memcpy(&u, &f, 4); coopbrec::BrPutU32(out, u); }
inline bool ClGetF(const char* p, size_t size, size_t* off, float* f) { unsigned int u = 0; if (!coopbrec::BrGet4(p, size, off, &u)) return false; std::memcpy(f, &u, 4); return true; }
inline bool EncodeCampLaidRec(std::vector<char>* out, const CampLaidRec& r)
{
    if (out == 0) return false;
    out->clear();
    if (!CampLaidRecValid(r)) return false;
    coopbrec::BrPutU32(out, kClMarker);
    coopbrec::BrPutU32(out, kClFormat);
    coopbrec::BrPutU32(out, r.generation);
    coopbrec::BrPutU32(out, (unsigned int)r.writerSlot);
    coopbrec::BrPutU32(out, (unsigned int)r.sx);
    coopbrec::BrPutU32(out, (unsigned int)r.sy);
    coopbrec::BrPutU32(out, (unsigned int)r.laid.size());
    for (size_t i = 0; i < r.laid.size(); ++i)
    {
        coopbrec::BrPutStr(out, r.laid[i].key);
        coopbrec::BrPutU32(out, (unsigned int)r.laid[i].pieces.size());
        for (size_t j = 0; j < r.laid[i].pieces.size(); ++j)
        {
            const CampPiece& p = r.laid[i].pieces[j];
            coopbrec::BrPutStr(out, p.sid);
            ClPutF(out, p.x); ClPutF(out, p.y); ClPutF(out, p.z); ClPutF(out, p.qw); ClPutF(out, p.qx); ClPutF(out, p.qy); ClPutF(out, p.qz);
            out->push_back((char)(unsigned char)p.inside);   /* format 3: the inside byte (F1753) */
        }
    }
    if (out->size() > (size_t)kClMaxBytes) { out->clear(); return false; }
    return true;
}
inline bool ClGetStr(const char* p, size_t size, size_t* off, std::string* s, int* bad, unsigned int cap)
{
    unsigned int n = 0;
    if (!coopbrec::BrGet4(p, size, off, &n)) return false;
    if (n == 0 || n > cap) { *bad = 1; return false; }
    if (size - *off < n) return false;
    s->assign(p + *off, p + *off + n);
    *off += n;
    if (!ClTextOk(*s, cap)) { *bad = 1; return false; }
    return true;
}
/* kClOk: *out holds the record; anything else: refused, *out untouched */
inline int DecodeCampLaidRec(const char* p, size_t size, CampLaidRec* out)
{
    if (p == 0 || size < 8u) return kClShort;
    if (size > (size_t)kClMaxBytes) return kClTooBig;
    size_t off = 0;
    unsigned int marker = 0, format = 0, n = 0;
    CampLaidRec r;
    coopbrec::BrGet4(p, size, &off, &marker);
    if (marker != kClMarker) return kClBadMarker;
    coopbrec::BrGet4(p, size, &off, &format);
    if (format != kClFormat && format != kClFormatNoInside) return kClBadFormat;   /* F1753: format 2 still reads - every piece 'not known' */
    if (!coopbrec::BrGet4(p, size, &off, &r.generation) || !coopbrec::BrGet4(p, size, &off, &r.writerSlot)
        || !coopbrec::BrGet4(p, size, &off, &r.sx) || !coopbrec::BrGet4(p, size, &off, &r.sy) || !coopbrec::BrGet4(p, size, &off, &n))
        return kClShort;
    if (n > kClEntryMax) return kClBadRecord;
    for (unsigned int i = 0; i < n; ++i)
    {
        CampLaidEntry e; int bad = 0; unsigned int m = 0;
        if (!ClGetStr(p, size, &off, &e.key, &bad, kClKeyMax)) return bad ? kClBadRecord : kClShort;
        if (!coopbrec::BrGet4(p, size, &off, &m)) return kClShort;
        if (m > kClPieceMax) return kClBadRecord;
        for (unsigned int j = 0; j < m; ++j)
        {
            CampPiece c;
            if (!ClGetStr(p, size, &off, &c.sid, &bad, kClSidMax)) return bad ? kClBadRecord : kClShort;
            if (!ClGetF(p, size, &off, &c.x) || !ClGetF(p, size, &off, &c.y) || !ClGetF(p, size, &off, &c.z) || !ClGetF(p, size, &off, &c.qw)
                || !ClGetF(p, size, &off, &c.qx) || !ClGetF(p, size, &off, &c.qy) || !ClGetF(p, size, &off, &c.qz)) return kClShort;
            if (format == kClFormat)
            {   /* F1753: the inside byte - 0, 1 or 2; anything else is a field out of range */
                if (off >= size) return kClShort;
                const unsigned char in = (unsigned char)p[off++];
                if (in > (unsigned char)kClInsideUnknown) return kClBadRecord;
                c.inside = (int)in;
            }
            else c.inside = kClInsideUnknown;
            e.pieces.push_back(c);
        }
        r.laid.push_back(e);
    }
    if (off != size) return kClTrailing;
    if (!CampLaidRecValid(r)) return kClBadRecord;
    if (out) *out = r;
    return kClOk;
}
inline const char* CampLaidDecodeWhy(int why)
{
    return why == kClOk ? "ok" : why == kClShort ? "cut short" : why == kClBadMarker ? "not a camplaid record (wrong marker)"
         : why == kClBadFormat ? "an unknown format" : why == kClBadRecord ? "a field out of range" : why == kClTrailing ? "bytes after the last field"
         : why == kClTooBig ? "larger than the byte cap" : "malformed";
}
/* the entry index of `key`, -1 = not in the record */
inline int CampLaidFind(const CampLaidRec& r, const std::string& key)
{
    for (size_t i = 0; i < r.laid.size(); ++i) if (r.laid[i].key == key) return (int)i;
    return -1;
}
/* the record after adding entries `add` (key -> pieces): a key already in the record keeps its pieces (the first layer's stand); a new
   key is added while the record has room (*capped counts those left out) and its pieces cut to kClPieceMax. Returns the keys added. */
inline int CampLaidWithEntries(const CampLaidRec& before, const std::map<std::string, std::vector<CampPiece> >& add, CampLaidRec* out, int* capped)
{
    std::map<std::string, std::vector<CampPiece> > m;
    for (size_t i = 0; i < before.laid.size(); ++i) m[before.laid[i].key] = before.laid[i].pieces;
    int added = 0, cut = 0;
    for (std::map<std::string, std::vector<CampPiece> >::const_iterator a = add.begin(); a != add.end(); ++a)
    {
        if (!ClTextOk(a->first, kClKeyMax) || m.count(a->first) != 0) continue;
        if (m.size() >= (size_t)kClEntryMax) { ++cut; continue; }
        std::vector<CampPiece> ps;
        for (size_t j = 0; j < a->second.size() && ps.size() < (size_t)kClPieceMax; ++j) if (CampPieceValid(a->second[j])) ps.push_back(a->second[j]);
        m[a->first] = ps; ++added;
    }
    if (out != 0)
    {
        CampLaidRec r = before;
        r.laid.clear();
        for (std::map<std::string, std::vector<CampPiece> >::const_iterator i = m.begin(); i != m.end(); ++i) { CampLaidEntry e; e.key = i->first; e.pieces = i->second; r.laid.push_back(e); }
        *out = r;
    }
    if (capped) *capped = cut;
    return added;
}
/* 1 = every key of `want` is in `rec` (the write that carries them is stored) */
inline int CampLaidCoversKeys(const CampLaidRec& rec, const std::map<std::string, std::vector<CampPiece> >& want)
{
    for (std::map<std::string, std::vector<CampPiece> >::const_iterator a = want.begin(); a != want.end(); ++a) if (CampLaidFind(rec, a->first) < 0) return 0;
    return 1;
}
/* the generation of this game's next write: one above the accepted one or the floor a refusal named, whichever is higher */
inline unsigned int CampLaidNextGen(int haveAcc, unsigned int accGen, unsigned int floorGen)
{
    const unsigned int base = (haveAcc != 0 && accGen > floorGen) ? accGen : floorGen;
    return base + 1u;
}
/* T-1429 review fold F1612 item 8: a piece of area (sx, sy)'s record is applied only in that square or a square beside it (a camp laid at a
   square's edge spills over by less than a square; anything further is a bad record) - psx / psy: the piece's square */
inline int CampPieceInBound(int sx, int sy, int psx, int psy)
{
    return (psx >= 0 && psx < 64 && psy >= 0 && psy < 64 && psx - sx <= 1 && sx - psx <= 1 && psy - sy <= 1 && sy - psy <= 1) ? 1 : 0;
}
/* T-1429 review fold F1612 item 8: ONE TICK'S SHARE of an apply - the picked entries (CampLaidApplyPick, in order) taken whole while their
   pieces stay within maxPieces (the first is always taken); the rest wait for the next tick */
const int kClApplyTickPieces = 64;
inline std::vector<int> CampLaidApplyShare(const CampLaidRec& rec, const std::vector<int>& pick, int maxPieces)
{
    std::vector<int> v; int n = 0;
    for (size_t i = 0; i < pick.size(); ++i)
    {
        const int m = (pick[i] >= 0 && pick[i] < (int)rec.laid.size()) ? (int)rec.laid[pick[i]].pieces.size() : 0;
        if (!v.empty() && n + m > maxPieces) break;
        v.push_back(pick[i]); n += m;
    }
    return v;
}
/* APPLY: the entries of `rec` whose key the area's note lacks, in record order */
inline std::vector<int> CampLaidApplyPick(const CampLaidRec& rec, const std::set<std::string>& note)
{
    std::vector<int> v;
    for (size_t i = 0; i < rec.laid.size(); ++i) if (note.count(rec.laid[i].key) == 0) v.push_back((int)i);
    return v;
}

/* ---- T-1429 F1671 (design decided 2026-11-20): EACH PLACED PIECE BELONGS TO THE SQUARE THAT HOLDS ITS OWN POSITION ----
   F1670: a camp laid across a square's edge has pieces in two squares; an area file carries only the pieces standing inside its own
   square, so 'the centre square's note lists the key' never meant 'every piece of the camp is here'. Now (1) a camp key goes into the note
   of EVERY square its pieces stand in (and the centre's, whose note the camp step reads), and (2) the record is applied PER SQUARE: when a
   square's area file has been read and the square is loaded here, each entry with pieces standing in that square whose key that SQUARE's
   note lacks has those pieces made, then its key goes into that square's note; a square whose note has the key trusts its own file
   (pieces destroyed there stay gone). The record stays keyed by the camp's centre square; a square finds the records with pieces in it
   through an index built as records arrive (CampLaidIndexAdd) - no sweep of every record. Square key: x * 64 + y (-1 off the grid). */
inline int CampSqKey(int x, int y) { return (x >= 0 && x < 64 && y >= 0 && y < 64) ? x * 64 + y : -1; }
inline int CampPieceSquare(const CampPiece& p) { int x = -1, y = -1; poskey::SectorOf(p.x, p.z, &x, &y); return CampSqKey(x, y); }
/* T-1429 F1753 review fold (MEDIUM): ONE ENTRY'S PLAN IN SQUARE sq (the apply, camp.cpp ClApplySquare) by each piece's road
   (CampPieceMakeRoad): a piece marked inside whose nest is neither here nor gone makes the WHOLE entry wait (returns 0: nothing made, key
   not noted); otherwise returns 1 - the entry applies and counts settled: outdoor pieces made, inside pieces made in the nest, inside pieces
   of a GONE nest dropped (not made), old-record pieces by the point lookup. Only pieces standing in sq are counted. */
struct CampNestPlan { int wait, outdoors, inNest, dropped, oldGuess; CampNestPlan() : wait(0), outdoors(0), inNest(0), dropped(0), oldGuess(0) {} };
inline int CampEntryNestPlan(const CampLaidEntry& e, int sq, int nestHere, int nestGone, CampNestPlan* p)
{
    CampNestPlan c;
    for (size_t j = 0; j < e.pieces.size(); ++j)
    {
        if (CampPieceSquare(e.pieces[j]) != sq) continue;
        const int r = CampPieceMakeRoad(e.pieces[j].inside, nestHere, nestGone);
        if (r == kMakeWaitNest) ++c.wait; else if (r == kMakeOutdoors) ++c.outdoors; else if (r == kMakeInNest) ++c.inNest; else if (r == kMakeNestGone) ++c.dropped; else ++c.oldGuess;
    }
    if (p != 0) *p = c;
    return c.wait == 0 ? 1 : 0;
}
/* the pieces of entry e standing in square sq */
inline int CampLaidPiecesInSquare(const CampLaidEntry& e, int sq)
{
    int n = 0;
    for (size_t i = 0; i < e.pieces.size(); ++i) if (sq >= 0 && CampPieceSquare(e.pieces[i]) == sq) ++n;
    return n;
}
/* the squares a list of pieces stands in (the laid step notes the key in each) */
inline void CampPiecesSquares(const std::vector<CampPiece>& ps, std::set<int>* out)
{
    for (size_t i = 0; i < ps.size(); ++i) { const int k = CampPieceSquare(ps[i]); if (k >= 0) out->insert(k); }
}
/* the squares a record touches: its own square (its note is the camp step's) and every square beside it (CampPieceInBound) that a piece of
   one of its entries stands in - a piece further away is a bad record, never applied (F1612 item 8) */
inline void CampLaidSquaresOf(const CampLaidRec& rec, std::set<int>* out)
{
    const int own = CampSqKey(rec.sx, rec.sy);
    if (own < 0) return;
    out->insert(own);
    for (size_t i = 0; i < rec.laid.size(); ++i)
        for (size_t j = 0; j < rec.laid[i].pieces.size(); ++j)
        {
            const int k = CampPieceSquare(rec.laid[i].pieces[j]);
            if (k >= 0 && CampPieceInBound(rec.sx, rec.sy, k / 64, k % 64)) out->insert(k);
        }
}
/* THE INDEX: square -> the record ids with pieces in it (and each record under its own square); add-only within a world */
inline void CampLaidIndexAdd(std::map<int, std::set<std::string> >* idx, const std::string& id, const CampLaidRec& rec)
{
    std::set<int> sq;
    CampLaidSquaresOf(rec, &sq);
    for (std::set<int>::const_iterator i = sq.begin(); i != sq.end(); ++i) (*idx)[*i].insert(id);
}
/* THE PER-SQUARE PICK: the entries of rec applied in square (qx, qy) given THAT square's note - an entry touches the square when a piece of it
   stands there (the square in bound of the record's own) or the square is the record's own (its key goes into the centre's note even with
   no piece there: the camp step reads that note); a touching entry whose key the note has is skipped (*skipped, in record order) */
inline std::vector<int> CampLaidSquarePick(const CampLaidRec& rec, int qx, int qy, const std::set<std::string>& note, std::vector<int>* skipped)
{
    std::vector<int> v;
    const int sq = CampSqKey(qx, qy);
    if (sq < 0 || CampPieceInBound(rec.sx, rec.sy, qx, qy) == 0) return v;
    const int own = (qx == rec.sx && qy == rec.sy) ? 1 : 0;
    for (size_t i = 0; i < rec.laid.size(); ++i)
    {
        if (!own && CampLaidPiecesInSquare(rec.laid[i], sq) == 0) continue;
        if (note.count(rec.laid[i].key) != 0) { if (skipped != 0) skipped->push_back((int)i); continue; }
        v.push_back((int)i);
    }
    return v;
}
/* AN ENTRY'S READINESS in a square: its town must be here; in a square that is not the record's own, the record's own square must be loaded
   here too (its camp town and the nest building the maker puts pieces inside stand there) - else it waits (applied again at the next camp
   step or the next square applied here) */
enum { kSqReady = 0, kSqWaitTown = 1, kSqWaitOwn = 2 };
inline int CampLaidSquareReady(int townFound, int ownSquare, int ownSquareLoaded)
{
    if (townFound == 0) return kSqWaitTown;
    if (ownSquare == 0 && ownSquareLoaded == 0) return kSqWaitOwn;
    return kSqReady;
}
/* ONE TICK'S SHARE of a square's apply: items (each with its piece count in the square) taken whole while within maxPieces (the first always
   taken); returns how many are taken - the rest wait for the next tick */
inline int CampApplyShareCount(const std::vector<int>& counts, int maxPieces)
{
    int n = 0, t = 0;
    for (size_t i = 0; i < counts.size(); ++i) { if (t != 0 && n + counts[i] > maxPieces) break; n += counts[i]; ++t; }
    return t;
}

/* ---- the camp note ---- */
/* a read of the area file: fileOk = the engine read a file (0 = no such file: the note stays); keys = the note's keys (none: no note in
   the file - a base-game-data read or an older save - the note becomes empty). Bad keys are dropped. Returns the keys kept. */
inline int CampNoteOnRead(std::set<std::string>* note, int fileOk, const std::vector<std::string>& keys)
{
    if (note == 0 || fileOk == 0) return -1;
    note->clear();
    for (size_t i = 0; i < keys.size() && (int)note->size() < kCnNoteMax; ++i) if (ClTextOk(keys[i], kClKeyMax)) note->insert(keys[i]);
    return (int)note->size();
}
/* the note's "n" field: 0..kCnNoteMax, -1 = not a count */
inline int CampNoteCountParse(const std::string& t)
{
    if (t.empty() || t.size() > 4) return -1;
    int n = 0;
    for (size_t i = 0; i < t.size(); ++i) { if (t[i] < '0' || t[i] > '9') return -1; n = n * 10 + (t[i] - '0'); }
    if (t.size() > 1 && t[0] == '0') return -1;
    return n <= kCnNoteMax ? n : -1;
}

/* ---- the furniture step ---- */
enum { kStepKeepOff = 0, kStepNoteHad = 1, kStepLaid = 2, kStepOwed = 3, kStepNoKey = 4, kStepUnbuilt = 5 };
enum { kLayerUnknown = 0, kLayerYou = 1, kLayerOther = 2 };   /* areafresh.h's layer byte, as this load's answer */
/* keepOn: the camp keep is on; onMain: the main thread; keyOk: the entry's key was built; inNote: the key is in the area's note;
   single: single player; linkUp: the world-server link is up; layer: the world server's answer for this load of the area (kLayerYou also
   when this game's fresh area map names it the area's holder and it asked nothing - store.cpp AfDecide).
   T-1429 review fold F1612 THE ROOT RULE (item 5): every camp piece the keep saves has its key in the area's note - so while the keep is on
   a step that cannot note its key (it did not build, or the step ran off the main thread / did not read) MAKES NOTHING (kStepNoKey):
   pieces with no note entry would be saved and laid again at the next load. Item 1: with the link up, only an answer naming this game lays -
   no answer (kLayerUnknown) is owed, so two games never both lay while the link is up; single player or the link down lays here. */
/* T-1429 re-check fold F1617 item 3: a key that cannot be built ON THE MAIN THREAD (town / building position or record id unread) is
   kStepUnbuilt - the engine lays it and its pieces are left out of the save (camp.cpp g_cpUnsaved: the 'needs saving' check answers the
   engine's own 'no'), so nothing doubles at the next load and no game is left without a camp; off the main thread kStepNoKey stays (no
   piece can be read there). */
inline int CampNoteStepDecide(int keepOn, int onMain, int keyOk, int inNote, int single, int linkUp, int layer)
{
    if (!keepOn) return kStepKeepOff;
    if (!onMain) return kStepNoKey;
    if (!keyOk) return kStepUnbuilt;
    if (inNote) return kStepNoteHad;
    if (single || !linkUp || layer == kLayerYou) return kStepLaid;
    return kStepOwed;
}
/* 1 = the engine's camp maker makes pieces in this step (else its count is 0) */
inline int CampLaidStepMakes(int step) { return (step == kStepKeepOff || step == kStepLaid || step == kStepUnbuilt) ? 1 : 0; }
inline const char* CampLaidStepName(int step)
{
    return step == kStepNoteHad ? "note-had" : step == kStepLaid ? "laid" : step == kStepOwed ? "owed" : step == kStepNoKey ? "no-key" : step == kStepUnbuilt ? "unbuilt" : "keep-off";
}
/* T-1429 re-check fold F1617 item 1 (THE BLOCKER, design (b)): AN OWED STEP IS KEPT - its town, template record, point, building and key -
   and run later through the engine's own step (orig_cpDebris, the same arguments) on the event that makes this game the area's camp layer:
   the world server's HELD message (areafresh.h kHeld: this game became the area's holder and no other game is its layer) or a late answer
   naming this game. At that event: the key in the area's note or in the area's camp pieces record -> dropped (the layer's pieces are here or
   coming); the town or the building no longer the same live object (not on the engine's lists, or its position key changed) -> dropped;
   else run. inNote / inRecord / townSame / buildingSame: 1 = so. */
enum { kKeptRun = 0, kKeptDropKeyHere = 1, kKeptDropGone = 2 };
inline int CampKeptDecide(int inNote, int inRecord, int townSame, int buildingSame)
{
    if (inNote != 0 || inRecord != 0) return kKeptDropKeyHere;
    if (townSame == 0 || buildingSame == 0) return kKeptDropGone;
    return kKeptRun;
}
inline const char* CampKeptName(int d) { return d == kKeptRun ? "run" : d == kKeptDropKeyHere ? "dropped - its key is in the note or the record" : "dropped - its town or building is gone"; }
/* T-1429 re-check fold F1617 item 6: THE CAMP MAKER'S DUPLICATE CHECK (decomp_92f250:330-378, Read): a piece made where a loaded building of
   the same record already stands - squared distance under DAT_14167b308 (the same constant is the y of the maker's default up vector
   Vector3(0, it, 0), :224 - 1.0, Inferred) - is destroyed and the loaded one kept. The apply makes no piece there (the same outcome). */
const float kClDupDist2 = 1.0f;
inline int CampPieceDuplicate(int sameRecord, float dx, float dy, float dz) { return (sameRecord != 0 && dx * dx + dy * dy + dz * dz < kClDupDist2) ? 1 : 0; }

/* ---- the world server ---- */
/* One write of camplaid record `id` from slot senderSlot; holderSlot: the slot the area map names as the area's holder (-1 nobody);
   exists / storedGen / sameBytes: a stored record, its generation, the bytes equal the stored file. coopbrec::kBrAccept, kBrRepeat or a
   refusal reason. */
inline int CampLaidJudgeWrite(const std::string& id, const char* p, size_t size, int senderSlot, int holderSlot, int exists,
                              unsigned int storedGen, int sameBytes, CampLaidRec* out)
{
    int sx = 0, sy = 0;
    if (CampLaidIdParse(id, &sx, &sy) == 0) return coopbrec::kBrRefuseBadId;
    CampLaidRec r;
    if (DecodeCampLaidRec(p, size, &r) != kClOk) return coopbrec::kBrRefuseUnreadable;
    if (out != 0) *out = r;
    if (r.sx != sx || r.sy != sy) return coopbrec::kBrRefuseWrongBox;
    if (senderSlot < 0 || r.writerSlot != senderSlot) return coopbrec::kBrRefuseWriter;
    if (holderSlot < 0 || senderSlot != holderSlot) return coopbrec::kBrRefuseNotWriter;
    if (exists != 0)
    {
        if (r.generation < storedGen) return coopbrec::kBrRefuseOlder;
        if (r.generation == storedGen) return (sameBytes != 0) ? coopbrec::kBrRepeat : coopbrec::kBrRefuseSameGen;
    }
    return coopbrec::kBrAccept;
}
/* 1 = a refusal the world server answers with its stored record too (the writer's view is behind the stored one) */
inline int CampLaidRefusalSendsStored(int why) { return (why == coopbrec::kBrRefuseOlder || why == coopbrec::kBrRefuseSameGen) ? 1 : 0; }
/* T-1429 re-check fold F1627 item 1 (THE BLOCKER): A CAMP PIECES WRITE IS JUDGED BY WHETHER ITS SENDER WAS THE AREA'S HOLDER WHEN IT LAID -
   the world server records that fact when the write ARRIVES (the game sends each write when it lays, on the same ordered line as the area
   report that passes the holder), not when a write held behind an earlier one is judged (a hand-over in between would refuse it and the
   new holder's HELD would lay its keys again). arrivalFact: 1 = the sender was the holder at arrival, 0 = it was not, -1 = not recorded
   (the holder now is used). Returns the holder slot the judge (CampLaidJudgeWrite) takes. */
inline int CampLaidHolderForJudge(int senderSlot, int arrivalFact, int holderNow)
{
    if (arrivalFact > 0 && senderSlot >= 0) return senderSlot;
    if (arrivalFact == 0) return -1;
    return holderNow;
}
/* 1 = every key of `r` is in `have` */
inline int CampLaidRecHasAll(const CampLaidRec& have, const CampLaidRec& r)
{
    for (size_t i = 0; i < r.laid.size(); ++i) if (CampLaidFind(have, r.laid[i].key) < 0) return 0;
    return 1;
}
/* F1627 item 1: PER KEY THE FIRST WRITER WINS (append-only) - the world server's whole decision on one camp pieces write. The judge as
   CampLaidJudgeWrite (holderAtLay from CampLaidHolderForJudge); then, with a stored record readable (stored != 0): an accepted write is
   stored as the stored record plus the write's keys it lacks (a key already stored keeps its pieces - never replaced, never dropped), at
   the write's generation; a write refused as older / at the stored generation that carries keys the stored record lacks is TAKEN the same
   way at the stored generation + 1 (a late write of a game that laid as the holder is never lost); one that carries nothing new stays
   refused (the stored record goes back with the answer). *out = the record to store; *grown = 1 when it is not the write as sent (the
   world server sends the stored record back to the writer once it has landed). */
inline int CampLaidTakeWrite(const std::string& id, const char* p, size_t size, int senderSlot, int holderAtLay, int exists, const CampLaidRec* stored,
                             unsigned int storedGen, int sameBytes, CampLaidRec* out, int* grown)
{
    if (grown != 0) *grown = 0;
    CampLaidRec w;
    const int why = CampLaidJudgeWrite(id, p, size, senderSlot, holderAtLay, exists, storedGen, sameBytes, &w);
    if (out != 0) *out = w;
    if (exists == 0 || stored == 0) return why;
    if (why != coopbrec::kBrAccept && why != coopbrec::kBrRefuseOlder && why != coopbrec::kBrRefuseSameGen) return why;
    std::map<std::string, std::vector<CampPiece> > add;
    for (size_t i = 0; i < w.laid.size(); ++i) add[w.laid[i].key] = w.laid[i].pieces;
    CampLaidRec m; int capped = 0;
    const int added = CampLaidWithEntries(*stored, add, &m, &capped);
    if (why != coopbrec::kBrAccept && added == 0) return why;
    m.generation = why == coopbrec::kBrAccept ? w.generation : storedGen + 1u;
    m.writerSlot = senderSlot; m.sx = w.sx; m.sy = w.sy;
    int same = (why == coopbrec::kBrAccept && m.laid.size() == w.laid.size()) ? 1 : 0;
    for (size_t i = 0; same != 0 && i < m.laid.size(); ++i)
    {
        const CampLaidEntry& a = m.laid[i]; const CampLaidEntry& b = w.laid[i];
        if (a.key != b.key || a.pieces.size() != b.pieces.size()) { same = 0; break; }
        for (size_t j = 0; j < a.pieces.size(); ++j)
        {
            const CampPiece& x = a.pieces[j]; const CampPiece& y = b.pieces[j];
            if (x.sid != y.sid || x.x != y.x || x.y != y.y || x.z != y.z || x.qw != y.qw || x.qx != y.qx || x.qy != y.qy || x.qz != y.qz || x.inside != y.inside) { same = 0; break; }
        }
    }
    if (grown != 0) *grown = same ? 0 : 1;
    if (out != 0) *out = m;
    return coopbrec::kBrAccept;
}
/* T-1429 re-check fold F1627 items 2 / 5: THE LAYER A STEP USES - decided at the step from the live holding fact, never only from the ask
   (an answer naming this game is not taken back when the area passes to another game during a long load). layerAtLoad: this load's answer
   (kLayerYou after a HELD); holdsNow: 1 = this game's fresh area map names it the holder, 0 = another game, -1 = not known (no fresh map, link
   down); askedThisLoad: this load asked the world server; mapAfterRead (F1630 item 1): 1 = the map holdsNow came from ARRIVED AFTER this
   load's area-file read (CampMapAfterRead). kLayerYou only for the holder now - with its answer or a HELD naming it, or a holder's own load
   that asked nothing (no answer and no HELD will come) once an area map received after the load's read names it the holder. A map that
   arrived before the read is not enough, however fresh (up to 5 s): the area may have passed to another game since (F1630 item 1 - this
   game unloads the area, the world server hands it to another game, this game reloads it within 5 s: its map from before still names it).
   The next map (the world server sends one about every second) settles it: naming this game, the kept steps run at the tick. A holder now
   whose answer named another game waits for its HELD (sent once the previous layer's writes settled); not the holder (or not known) never lays. */
inline int CampLayerAtStep(int layerAtLoad, int holdsNow, int askedThisLoad, int mapAfterRead)
{
    if (holdsNow != 1) return layerAtLoad == kLayerUnknown ? kLayerUnknown : kLayerOther;
    if (layerAtLoad == kLayerYou) return kLayerYou;
    if (layerAtLoad == kLayerUnknown && askedThisLoad == 0 && mapAfterRead != 0) return kLayerYou;
    return layerAtLoad;
}
/* F1630 item 1: 1 = the area map numbered mapSeq (zones.cpp AreaMapApplySeqTS, one more per map that arrives) arrived after the read
   numbered readMapSeq (the map number when this load's area file was read; readThisLoad: that read belongs to this load) - an event, the
   next map, not a timer. Wraps. */
inline int CampMapAfterRead(int readThisLoad, unsigned int readMapSeq, unsigned int mapSeq) { return (readThisLoad != 0 && (int)(mapSeq - readMapSeq) > 0) ? 1 : 0; }
/* T-1429 re-check fold F1634: THE MAP NUMBER A LOAD'S 'AFTER' IS COUNTED FROM - every load has one, file read or not (a never-saved area hands
   the engine no file, so its load records no read; a failed file read records none either): this load's area-file read (readThisLoad) when
   it has one, else the map number at this load's start (startThisLoad: the engine's 'load decided' for the area, store.cpp
   detour_afZoneMapActivate, main thread), else the map number at the area's last unload (unloadSeq: 0 when the area was never unloaded this
   session - then a map that arrived before the load cannot name this game for an earlier load of it). Returns which (kMark*). */
enum { kMarkRead = 1, kMarkStart = 2, kMarkUnload = 3 };
inline int CampLoadMark(int readThisLoad, unsigned int readSeq, int startThisLoad, unsigned int startSeq, unsigned int unloadSeq, unsigned int* mark)
{
    if (readThisLoad != 0) { if (mark != 0) *mark = readSeq; return kMarkRead; }
    if (startThisLoad != 0) { if (mark != 0) *mark = startSeq; return kMarkStart; }
    if (mark != 0) *mark = unloadSeq;
    return kMarkUnload;
}
/* F1634: THE LAYER A FILE READ LEAVES ON THE LOAD - a load may hold its layer before its read now (its start, a HELD, an answer taken for
   this load): waiting = the answer to this load's ask kept for the read (-1 none); keepOld = the load's layer so far belongs to THIS load
   (oldLayer). A HELD (kLayerYou) of this load stands; else this load's answer; else the layer so far; else not known. */
inline int CampLayerAtRead(int waiting, int keepOld, int oldLayer)
{
    if (keepOld != 0 && oldLayer == kLayerYou) return kLayerYou;
    if (waiting >= 0) return waiting;
    return keepOld != 0 ? oldLayer : (int)kLayerUnknown;
}
/* T-1429 re-check fold F1627 item 4: this game's camp pieces writes out to the world server, by generation (each write's own: a refusal or
   an answer ends only its write) - the next write's generation: CampLaidNextGen, and above every generation this game has sent for the
   record (haveSent / hiSent: the highest) - a refusal of one write while another is out never lets a generation be used twice */
inline unsigned int CampLaidPipedGen(int haveSent, unsigned int hiSent, int haveAcc, unsigned int accGen, unsigned int floorGen)
{
    const unsigned int next = CampLaidNextGen(haveAcc, accGen, floorGen);
    if (haveSent == 0) return next;
    return hiSent + 1u > next ? hiSent + 1u : next;
}

/* T-1429 F1638 SHOULD: 'after' counted from the load's mark (CampLoadMark) - STRICT when the mark is the unload fallback and the area was
   never unloaded this session (unloadedEver 0: no unload recorded, the mark is 0): no map is 'after' it, so a map from before the load
   never makes this game the layer (a stale map with another game the real holder would lay the camp twice); the HELD, or a read / start
   of a later load, decides instead. The step counts it (campF1638[markNever]). */
inline int CampMapAfterMark(int which, int unloadedEver, unsigned int mark, unsigned int mapSeq)
{
    if (which == kMarkUnload && unloadedEver == 0) return 0;
    return CampMapAfterRead(1, mark, mapSeq);
}
/* T-1429 F1645 (3): THE FORM OF A KEY'S TOWN PART, told by its real shape - a camp on the world's camp list: its CampKey
   "<kind>@<x>,<z>" (campwire.h: one '@', two whole numbers after it); any other town: its position key
   "<sid>@<sx>,<sy>@<x10>,<z10>,<y10>" (poskey.h: two '@', three numbers after the last). Each goes to its own lookup (camp.cpp
   CpTownOfKeyPart); anything else names no town. Both forms hold '@' - the old test 'an @ in it = a list key' sent every position key to
   the list lookup, so a town not on the camp list was never found (F1645 (3)). */
enum { kTownKeyNone = 0, kTownKeyList = 1, kTownKeyPosition = 2 };
inline int CampTownKeyForm(const std::string& tp)
{
    if (tp.empty()) return kTownKeyNone;
    char sid[poskey::kSidRoom]; int sx = 0, sy = 0; long x10 = 0, z10 = 0, y10 = 0;
    if (poskey::ParseKeySpotH(tp.c_str(), sid, (int)sizeof sid, &sx, &sy, &x10, &z10, &y10) == 1) return kTownKeyPosition;
    const size_t at = tp.rfind('@');
    if (at == std::string::npos || at == 0 || tp.find('@') != at) return kTownKeyNone;
    const char* const end = tp.c_str() + tp.size();
    long x = 0, z = 0;
    const char* p = poskey::ReadKeyLong(tp.c_str() + at + 1, end, &x);
    if (p == 0 || p >= end || *p != ',') return kTownKeyNone;
    p = poskey::ReadKeyLong(p + 1, end, &z);
    return p == end ? kTownKeyList : kTownKeyNone;
}
/* T-1429 F1688 design D1: THE TOWN OF A POSITION KEY OVER THE ENGINE'S EVERY-TOWN LIST. A step's town part is a position key when its town is
   an ordinary town (not a world camp-list camp); the engine's camp array never holds such a town, so the old lookup over it never found it
   and every held-back step of that town was dropped 'town same 0' (T2706). The walk (camp.cpp CpTownOfPosKeyAll) feeds each live town here:
   only a town whose record id fits the key's id field has its own position key built, and only an EXACT key match counts; two exact matches
   name no town (never a guess between twins); a key that is not a position key names none. */
enum { kAllTownNone = -1, kAllTownTwins = -2, kAllTownNotKey = -3 };
struct CampAllTownFind
{
    char sid[poskey::kSidRoom];   /* the key's record-id field (the plain id, or '#' + its hash when the key had to hash it) */
    int ok;                       /* 1 = the key parsed as a position key */
    int idHits;                   /* towns whose record id fits the key's */
    int matches;                  /* towns whose own position key equals the key exactly */
    int first;                    /* the first exact match's row; -1 = none */
};
inline int CampAllTownFindStart(const std::string& tp, CampAllTownFind* f)
{
    f->sid[0] = 0; f->ok = 0; f->idHits = 0; f->matches = 0; f->first = -1;
    int sx = 0, sy = 0; long x10 = 0, z10 = 0, y10 = 0;
    f->ok = poskey::ParseKeySpotH(tp.c_str(), f->sid, (int)sizeof f->sid, &sx, &sy, &x10, &z10, &y10) == 1 ? 1 : 0;
    return f->ok;
}
/* does a town of record id townSid fit the key's id field? equal, or the key's hashed form of that id (poskey::ComposeOrHash) */
inline int CampAllTownIdFits(const CampAllTownFind& f, const char* townSid)
{
    if (f.ok == 0 || townSid == 0 || townSid[0] == 0) return 0;
    if (std::strcmp(f.sid, townSid) == 0) return 1;
    if (f.sid[0] != '#') return 0;
    char hashed[16]; int at = 0;
    at = poskey::AppendStr(hashed, (int)sizeof hashed, at, "#");
    at = poskey::AppendHex8(hashed, (int)sizeof hashed, at, poskey::SidHash32(townSid));
    return at >= 0 && std::strcmp(f.sid, hashed) == 0 ? 1 : 0;
}
/* one id-fitting town at row: its own position key (0 = did not build) against the key, exactly */
inline void CampAllTownSee(CampAllTownFind* f, int row, const char* townKey, const std::string& tp)
{
    ++f->idHits;
    if (townKey == 0 || townKey[0] == 0 || tp != townKey) return;
    if (f->matches == 0) f->first = row;
    ++f->matches;
}
/* the walk's answer: the one exact match's row, or kAllTownNone / kAllTownTwins / kAllTownNotKey */
inline int CampAllTownResult(const CampAllTownFind& f)
{
    if (f.ok == 0) return kAllTownNotKey;
    if (f.matches == 0) return kAllTownNone;
    if (f.matches > 1) return kAllTownTwins;
    return f.first;
}
/* T-1429 F1645 (2): a layer answer's text - the world server names 'other' also when NO game holds the area (store_main.cpp AfLayerFor:
   askerHolds is 0 with no holder); this game's own fresh area map tells the two apart (holderSlot -1 = no holder). Log text only: the
   decision is the answer's layer, unchanged. */
inline const char* CampAnswerLayerText(int layer, int mapFresh, int holderSlot, int mySlot)
{
    if (layer == kLayerYou) return "this game";
    if (layer != kLayerOther) return "not said";
    if (mapFresh == 0) return "another game or no holder yet (this game's area map is not fresh)";
    if (holderSlot == -1) return "no holder yet";
    if (mySlot >= 0 && holderSlot == mySlot) return "not this game when asked (this game's area map names it the holder now - its HELD decides)";   /* F1654 NIT */
    return "another game";
}
/* T-1429 F1648 design (B), review fold F1654: THE RE-LINK - a game that loaded another game's copy of an area keeps that game's camp pieces
   from the file; each piece's saved town link holds the saving game's own town numbers (F1646), so the engine linked it to its default town
   ('Nowhere', TownList+0x180F8). Each such piece of key K is moved to THIS game's camp town of K through the engine's own set-town road
   (build.cpp BdBuildingSetTownPod). The pieces of K are the camp pieces record's own list for K (record id + spot: CampPieceDuplicate, the
   maker's own duplicate test) - the note is not changed. F1654 item 1: NO HOLDER GATE - the holder may have loaded a copy another game saved
   (A lays and leaves, B alone holds and reads A's file); each piece's own town is the fact (CampRelinkPiece). GATE: never in single player,
   never without the area's record or while the area is not loaded here. */
enum { kRlGateNo = 0, kRlGateRun = 1 };
inline int CampRelinkGate(int single, int haveRecord, int loaded)
{
    return (single != 0 || haveRecord == 0 || loaded == 0) ? (int)kRlGateNo : (int)kRlGateRun;
}
/* the record's entries a pass looks at: those whose key is in the area's note (the pieces came with the area file - an entry the note lacks
   is made here by the apply, with this game's town) and that are not settled this load (done) */
inline std::vector<int> CampRelinkPick(const CampLaidRec& rec, const std::set<std::string>& note, const std::set<std::string>& done)
{
    std::vector<int> r;
    for (size_t i = 0; i < rec.laid.size(); ++i) if (note.count(rec.laid[i].key) != 0 && done.count(rec.laid[i].key) == 0) r.push_back((int)i);
    return r;
}
/* ONE PIECE (F1654 item 3): not found here; already linked to this game's town of K (left alone - the engine's circle fallback found it, or
   this game's own maker made it); LEFT ALONE when its town is any other real town (only a piece the load left in the engine's default town,
   or whose link names no live town, is the file's orphan) or a player owns it; else moved - or, standing inside a building, it follows its
   host's move */
enum { kRlNotFound = 0, kRlRight = 1, kRlMove = 2, kRlInside = 3, kRlLeave = 4, kRlUnread = 5 };   /* kRlUnread: F1659 B1 (CampRelinkPieceOfLink) */
inline int CampRelinkPiece(int found, int linkedToTown, int townIsFallback, int playerOwned, int topLevel)
{
    if (found == 0) return kRlNotFound;
    if (linkedToTown != 0) return kRlRight;
    if (townIsFallback == 0 || playerOwned != 0) return kRlLeave;
    return topLevel == 0 ? kRlInside : kRlMove;
}
/* ONE KEY after a pass (F1654 item 2): its town not here - waits for the town; a piece not found or the building read cut - waits for the
   pieces (the next event: the area's pieces listed again, a town built, a camp step); else settled for this load. Every event runs the same
   pass on the facts of that moment, so the order of 'pieces loaded' and 'town here' never matters - only the last state counts. */
/* REVIEW FOLD F1659 N1: a key with a piece whose move faulted or whose town words did not read (faultOrUnread) is not settled either - it
   waits (kRlKeyWaitFault) and is passed again at the next event; a key that faulted on kRlFaultTriesMax passes in this world gives up
   (kRlKeyGaveUp: settled for this load, counted and said once) - a piece that always fails never loops. faultPassesBefore: this key's
   passes with a fault or an unread piece before this one, in this world. The give-up comes before the pieces' wait (bounded either way). */
enum { kRlKeyWaitTown = 0, kRlKeyWaitPieces = 1, kRlKeyDone = 2, kRlKeyWaitFault = 3, kRlKeyGaveUp = 4 };
const int kRlFaultTriesMax = 5;
inline int CampRelinkKeyAfter(int townFound, long long notFound, int readCut, long long faultOrUnread, int faultPassesBefore)
{
    if (townFound == 0) return kRlKeyWaitTown;
    if (faultOrUnread > 0 && faultPassesBefore + 1 >= kRlFaultTriesMax) return kRlKeyGaveUp;
    if (notFound > 0 || readCut != 0) return kRlKeyWaitPieces;
    return faultOrUnread > 0 ? (int)kRlKeyWaitFault : (int)kRlKeyDone;
}
/* REVIEW FOLD F1659 B1: A PIECE'S STORED TOWN LINK - its own town words (Building +0x1D8, what the engine and the load set), NEVER the
   engine's name lookup (0xF6BE0: the building's link only when it names a live town, else the NEAREST town by position - an orphan near this
   camp's town read 'already right' and was never moved, one near another town was left alone). pieceRead / townRead: the piece's words and
   this game's camp town's own words read (0 = a fault); pieceHand / townHand: the five words (type first, 0xB = none); kind: the town lists
   for the piece's words (build.cpp BdTownOfWordsPod: 1 the wilderness town 'Nowhere', 2 a player's town, 3 a world town, 0 none or a dead
   town, -1 the lists did not read). RIGHT = the piece's words ARE this town's own words (all five, exact - no list needed); WILD = none /
   empty words, the wilderness town, no live town or a dead one; OTHER = another live town; UNREAD = a fault, the town's words unknown or the
   lists did not read (never moved, never 'right' - the key waits) */
enum { kRlLinkUnread = -1, kRlLinkOther = 0, kRlLinkRight = 1, kRlLinkWild = 2 };
inline int CampRelinkHandKnown(const int* h)
{
    if (h[0] == 0xB) return 0;
    for (int i = 0; i < 5; ++i) if (h[i] != 0) return 1;
    return 0;
}
inline int CampRelinkLinkOf(int pieceRead, const int* pieceHand, int townRead, const int* townHand, int kind)
{
    if (pieceRead == 0 || townRead == 0 || CampRelinkHandKnown(townHand) == 0) return kRlLinkUnread;
    if (CampRelinkHandKnown(pieceHand) == 0) return kRlLinkWild;
    if (std::memcmp(pieceHand, townHand, 5 * sizeof(int)) == 0) return kRlLinkRight;
    if (kind < 0) return kRlLinkUnread;
    return (kind == 0 || kind == 1) ? (int)kRlLinkWild : (int)kRlLinkOther;
}
/* ONE PIECE FROM ITS STORED LINK (F1659 B1): not found; its words not read - kRlUnread (not moved, not 'right', counted; the key waits);
   else CampRelinkPiece with 'linked to this town' = RIGHT and 'the load's fallback' = WILD */
inline int CampRelinkPieceOfLink(int found, int link, int playerOwned, int topLevel)
{
    if (found == 0) return kRlNotFound;
    if (link == kRlLinkUnread) return kRlUnread;
    return CampRelinkPiece(1, link == kRlLinkRight ? 1 : 0, link == kRlLinkWild ? 1 : 0, playerOwned, topLevel);
}

}   // namespace camplaid
