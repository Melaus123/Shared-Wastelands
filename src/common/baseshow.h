#pragma once
/* T-811: TEAMMATES' BASES ON THE MAP (SHOW, world-server kind 78, protocol 100; its keys TOWNS SHOW KEYS since 101). Compiled into the world server (store_main.cpp), the
   game (build.cpp, store.cpp) and the offline suite (test_main.cpp). Pure: no disk, no clock, no engine. Header-only. No C++11.

   Each game says its own outposts to the world server every 5 s (townkeep.h SAY); the world server keeps them per player in
   player-towns.txt, also while that player is offline. SHOW carries, to each game, the outposts of the OTHER players in its player's
   faction of players (the T-546 team: founder and members; a player in no team is sent an empty list) - each with the owner's
   player name. It is sent when a game is admitted, when player-towns.txt changes and when the team table changes, and only when
   the list a game would be sent differs from the last one it was sent.
   The game finds or founds each outpost once - a player town of the owner's stand-in faction at that centre (within
   kSameBaseRadius) is taken, else one is founded the engine's way (TownFoundPlayer) and given the stand-in faction - named
   "<outpost name> (<player name>)" (Label). The town is looked up by its handle words at every use (its address is never kept).
   A base whose row goes, while the list came whole, is taken out of this world the engine's way (TownDestroy) when this game
   founded it and no loaded piece names it as its town (GoneStep), and its model-piece store is empty (StoreKeep); the same before
   every game save, so the save does not hold it, and it is founded again at the next drain. One the store gate keeps through a save
   is in that save: after a reload it is taken up again by its mark (LeftoverMark) and taken out once its store is empty.
   WIRE (little-endian, as every world-server message): u32 flags (kFlagCut: the list was cut, so a missing row is not a removed
   one), u32 count 0..kMaxShowRows, count x ROW
     ROW  i32 owner slot, f32 x, f32 y (height), f32 z, str outpost name (0..128 bytes, no control characters),
          str owner's player name (0..128 bytes, no control characters; empty = not known), u32 size level 0..townkeep::kLevelMax
   THE ICON (owner 874 a): the engine draws the growing outpost icon only for a town of this game's own player faction (0x48CEE0:
   Faction+0x250 set -> its icon by Town+0x388, the size level; else by the town type Town+0xD8). build.cpp hooks Town vt+0x378 /
   +0x380 for the towns it shows for a teammate: the icon the engine gives a player outpost of the row's size level
   (IconNameOfLevel: the name 0x48CEE0 writes for that level, written into the call's answer - no town field is touched) and the
   player outposts' map layer (kOutpostMapLayer).
   THE MOD'S OWN BASES ARE KNOWN BY THE TOWN ITSELF (LeftoverMark): a player town owned by a stand-in faction (its record coop-p<slot>),
   named in the label form (LabelForm) and standing at no kept outpost's centre. A base that a save held, or that stood when the
   game was closed, is so taken up again after a reload: a row at its centre adopts it, else it is taken out.
     str = u32 length, the bytes.
   THE PIECES' TOWN (owner 936 / 937, 2026-10-17; protocol 101): each SHOW is followed by TOWNS SHOW KEYS (townkeep.h kOpShowKeys) -
   per shown row the said keys of the outpost's linked pieces (KeyRowsFor). The game links each loaded copy whose key is on a row of
   its owner to the town it shows for that row (LinkStep), re-checked at every look (a copy made later, a town made later, a link on
   the owner's game and a reload all converge); a copy in a shown town that is on no key row of it, while that row is not cut and
   said its key earlier this world (SeenRow: one the engine put there at creation and the row never said stays), is put back in the
   town building creation would choose with the shown towns left out (PutBackPick). Before a base founded here is
   taken out (its row went, or a save was asked for) its copies this game linked are put back the same way, so a save holds none
   linked to it; they are linked again after the save. So the copies of a teammate's
   outpost run their power grid on this game too (no power is ever sent between games: powerwire.h). */
#include <string>
#include <vector>
#include <set>
#include <map>
#include <cstring>
#include "townkeep.h"   /* the said outposts (TownRow), keptwire's PutU32 / PutStr / TextOk / FloatOk / NearXZ */

namespace baseshow {

const unsigned kMsgShow = 78;
const unsigned kProtocol = 101;   /* the world-server protocol that carries SHOW (101: SHOW KEYS follows each SHOW) */
const unsigned kFlagCut = 1;
const size_t kMaxShowRows = 256;   /* teammate outposts in one SHOW */
const size_t kShowMsgBytes = townkeep::kTownsMsgBytes;   /* one SHOW at most: under the world-server road's 64 KiB */
const float kSameBaseRadius = townkeep::kTownSameRadius;   /* a town this close (x, z) to a row's centre is that row's base */

struct ShowRow
{
    int owner;            /* the owner's player slot */
    float pos[3];         /* the outpost's centre: x, height, z */
    std::string name;     /* the outpost's name as its owner's game reads it */
    std::string ownerName;   /* the owner's player name ("" = not known) */
    int level;            /* the outpost's size level 0..townkeep::kLevelMax */
    ShowRow() : owner(-1), level(0) { pos[0] = pos[1] = pos[2] = 0.0f; }
};

inline bool RowOk(const ShowRow& r)
{
    if (r.owner < 0 || r.owner > townkeep::kOwnerMax || r.level < 0 || r.level > townkeep::kLevelMax) return false;
    for (int j = 0; j < 3; ++j) if (!keptwire::FloatOk(r.pos[j])) return false;
    return keptwire::TextOk(r.name, keptwire::kStrMax, true) && keptwire::TextOk(r.ownerName, keptwire::kStrMax, true);
}
inline size_t RowBytes(const ShowRow& r) { return 4 + 12 + 4 + r.name.size() + 4 + r.ownerName.size() + 4; }

/* the rows one game is sent: the said outposts of every slot in `mates` (the viewer's teammates), never the viewer's own, each with
   its owner's name; a row that is not sendable is left out. At most kMaxShowRows rows and kShowMsgBytes bytes: *cut = 1 when a row
   was left out for room. */
inline std::vector<ShowRow> RowsFor(const std::vector<townkeep::TownRow>& said, int viewer, const std::set<int>& mates,
                                    const std::map<int, std::string>& names, int* cut)
{
    std::vector<ShowRow> out;
    size_t bytes = 8;
    if (cut != 0) *cut = 0;
    for (size_t i = 0; i < said.size(); ++i)
    {
        const townkeep::TownRow& t = said[i];
        if (t.owner == viewer || mates.count(t.owner) == 0) continue;
        ShowRow r;
        r.owner = t.owner;
        for (int j = 0; j < 3; ++j) r.pos[j] = t.pos[j];
        r.name = townkeep::NameFit(t.name);
        r.level = t.level;
        std::map<int, std::string>::const_iterator nm = names.find(t.owner);
        if (nm != names.end()) r.ownerName = townkeep::NameFit(nm->second);
        if (!RowOk(r)) continue;
        if (out.size() >= kMaxShowRows || bytes + RowBytes(r) > kShowMsgBytes) { if (cut != 0) *cut = 1; continue; }
        bytes += RowBytes(r);
        out.push_back(r);
    }
    return out;
}

/* SHOW KEYS (townkeep.h kOpShowKeys): one row per shown row, in its order - the first said outpost of the row's owner at exactly the
   row's centre (RowsFor copies the centre bit for bit), with its name, size level, keys and cut flag; handle words all 0. A said row
   not valid for SHOW KEYS is left out (that row's copies are then not linked on the viewer's game, and none is unlinked) */
inline std::vector<townkeep::TownRow> KeyRowsFor(const std::vector<townkeep::TownRow>& said, const std::vector<ShowRow>& shown)
{
    std::vector<townkeep::TownRow> out;
    for (size_t r = 0; r < shown.size() && out.size() < townkeep::kMaxShowKeyRows; ++r)
        for (size_t i = 0; i < said.size(); ++i)
        {
            const townkeep::TownRow& t = said[i];
            if (t.owner != shown[r].owner || t.pos[0] != shown[r].pos[0] || t.pos[1] != shown[r].pos[1] || t.pos[2] != shown[r].pos[2]) continue;
            townkeep::TownRow k;
            k.owner = t.owner;
            for (int j = 0; j < 3; ++j) k.pos[j] = t.pos[j];
            k.name = townkeep::NameFit(t.name);
            k.keys = t.keys;
            k.cut = t.cut != 0 ? 1 : 0;
            k.level = t.level;
            if (townkeep::RowOk(k, townkeep::kOpShowKeys)) out.push_back(k);
            break;
        }
    return out;
}
inline bool Encode(std::vector<char>* out, unsigned flags, const std::vector<ShowRow>& rows)
{
    if (flags > kFlagCut || rows.size() > kMaxShowRows) return false;
    std::vector<char> b;
    keptwire::PutU32(&b, flags);
    keptwire::PutU32(&b, (unsigned)rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const ShowRow& r = rows[i];
        if (!RowOk(r)) return false;
        unsigned u = 0;
        std::memcpy(&u, &r.owner, 4); keptwire::PutU32(&b, u);
        for (int j = 0; j < 3; ++j) { std::memcpy(&u, &r.pos[j], 4); keptwire::PutU32(&b, u); }
        keptwire::PutStr(&b, r.name);
        keptwire::PutStr(&b, r.ownerName);
        keptwire::PutU32(&b, (unsigned)r.level);
    }
    if (b.size() > kShowMsgBytes) return false;
    out->swap(b);
    return true;
}
/* 1 = read whole and every row valid; 0 = refused (nothing taken) */
inline int Decode(const char* p, size_t n, unsigned* flags, std::vector<ShowRow>* rows)
{
    townkeep::TkReader rd(p, n);
    const unsigned f = rd.U32();
    const unsigned count = rd.U32();
    if (rd.bad || f > kFlagCut || count > kMaxShowRows) return 0;
    std::vector<ShowRow> got;
    for (unsigned i = 0; i < count; ++i)
    {
        ShowRow r;
        r.owner = rd.I32();
        for (int j = 0; j < 3; ++j) r.pos[j] = rd.F32();
        const unsigned ln = rd.U32();
        if (rd.bad || ln > keptwire::kStrMax || n - rd.at < ln) return 0;
        r.name.assign(p + rd.at, ln); rd.at += ln;
        const unsigned lo = rd.U32();
        if (rd.bad || lo > keptwire::kStrMax || n - rd.at < lo) return 0;
        r.ownerName.assign(p + rd.at, lo); rd.at += lo;
        const unsigned lv = rd.U32();
        r.level = lv > (unsigned)townkeep::kLevelMax ? -1 : (int)lv;   /* -1: refused by RowOk */
        if (!RowOk(r)) return 0;
        got.push_back(r);
    }
    if (rd.bad || rd.at != n) return 0;
    *flags = f;
    rows->swap(got);
    return 1;
}

/* the town's name on the map: "<outpost name> (<player name>)" ("(<player name>)" for an outpost with no name; the outpost name
   alone when no player name is given - the game always gives one). At most keptwire::kStrMax bytes: the player's name is cut to
   kLabelPlayerMax bytes, then the outpost name is shortened, so the label form (LabelForm) is always kept. */
const size_t kLabelPlayerMax = 64;
inline std::string Label(const std::string& base, const std::string& player)
{
    const std::string b = townkeep::NameFit(base);
    std::string pl = townkeep::NameFit(player);
    if (pl.empty()) return b;
    if (pl.size() > kLabelPlayerMax) pl.resize(kLabelPlayerMax);
    const std::string suffix = (b.empty() ? "(" : " (") + pl + ")";
    const size_t room = keptwire::kStrMax - suffix.size();
    return (b.size() > room ? b.substr(0, room) : b) + suffix;
}
/* a name in the label form: it ends with ')' and holds a '(' before it */
inline bool LabelForm(const std::string& name)
{
    if (name.size() < 3 || name[name.size() - 1] != ')') return false;
    const std::string::size_type o = name.rfind('(', name.size() - 2);
    return o != std::string::npos && o + 2 < name.size();
}
/* 1 = a player town this mod made for a teammate: owned by a stand-in (standInSlot >= 0: its faction record is coop-p<slot>), named
   in the label form, and at no kept outpost's centre (the reset's own towns are never taken for one) */
inline int LeftoverMark(int standInSlot, int labelForm, int nearKept) { return (standInSlot >= 0 && labelForm != 0 && nearKept == 0) ? 1 : 0; }

/* ---- the game's side: what it shows ---- */
struct Shown
{
    int owner;
    float pos[3];
    int hand[5];   /* the town's handle words in THIS game (all 0: none standing now - found or taken at the next apply) */
    int ours;      /* 1 = this game founded the town for the row (it may take it out); 0 = a town that stood already (never taken out here) */
    int level;     /* the row's size level: the outpost icon the map hook draws for it */
    int going;     /* 1 = its row went and it is not taken out yet (a loaded piece names it, or the destroy faulted): tried again */
    int drawn;     /* T-811 log: 1 = the map made its marker (the icon hook ran for its town) */
    std::string markSaid;   /* T-811 log: the icon / colour last written in its [TEAMBASE] map marker line ("" = none yet) */
    Shown() : owner(-1), ours(0), level(0), going(0), drawn(0) { pos[0] = pos[1] = pos[2] = 0.0f; for (int i = 0; i < 5; ++i) hand[i] = 0; }
};
/* each row's shown base: the first not yet matched of the same owner within kSameBaseRadius of the row's centre (-1 none);
   *gone: the shown bases no row matched, in order */
inline void Match(const std::vector<Shown>& shown, const std::vector<ShowRow>& rows, std::vector<int>* rowTo, std::vector<size_t>* gone)
{
    std::vector<int> used(shown.size(), 0);
    rowTo->assign(rows.size(), -1);
    for (size_t r = 0; r < rows.size(); ++r)
        for (size_t s = 0; s < shown.size(); ++s)
            if (used[s] == 0 && shown[s].owner == rows[r].owner && keptwire::NearXZ(shown[s].pos[0], shown[s].pos[2], rows[r].pos[0], rows[r].pos[2], kSameBaseRadius) != 0)
            { used[s] = 1; (*rowTo)[r] = (int)s; break; }
    gone->clear();
    for (size_t s = 0; s < shown.size(); ++s) if (used[s] == 0) gone->push_back(s);
}
/* the key row of a shown base: the one of the same owner at exactly its centre (BdShowApply gives a base its row's centre bit for bit,
   and KeyRowsFor copies it the same way - so two outposts of one owner within kSameBaseRadius each find their own); else the first
   of the same owner within kSameBaseRadius of it (a base taken up at its town's own position) (-1 none) */
inline int KeyRowOf(const Shown& s, const std::vector<townkeep::TownRow>& keyRows)
{
    for (size_t k = 0; k < keyRows.size(); ++k)
        if (keyRows[k].owner == s.owner && keyRows[k].pos[0] == s.pos[0] && keyRows[k].pos[1] == s.pos[1] && keyRows[k].pos[2] == s.pos[2]) return (int)k;
    for (size_t k = 0; k < keyRows.size(); ++k)
        if (keyRows[k].owner == s.owner && keptwire::NearXZ(s.pos[0], s.pos[2], keyRows[k].pos[0], keyRows[k].pos[2], kSameBaseRadius) != 0) return (int)k;
    return -1;
}
/* THE KEYS A SHOWN ROW EVER SAID (T1342, 2026-10-18): building creation puts a new piece placed in a player outpost's area into that
   outpost at once - on the owner's game its own outpost, here the town shown for it - before the owner's next key walk says its key.
   So a copy in a shown town whose key that row never said is where the engine put it (left there: the owner's game has it in its
   outpost too); one whose key the row said and no longer says was taken out on the owner's game (put back). One entry per key row
   (owner and centre bit for bit, as KeyRowsFor copies it); a row on no whole list goes with its keys; cleared with the world. Past
   kSeenKeysCap keys a row is full: every key then counts as said (the put-back as before). */
const size_t kSeenKeysCap = 4096;
struct SeenRow
{
    int owner;
    float pos[3];
    int full;
    std::set<std::string> keys;
    SeenRow() : owner(-1), full(0) { pos[0] = pos[1] = pos[2] = 0.0f; }
};
inline bool SeenSameRow(const SeenRow& s, const townkeep::TownRow& r)
{
    return s.owner == r.owner && s.pos[0] == r.pos[0] && s.pos[1] == r.pos[1] && s.pos[2] == r.pos[2];
}
/* a whole SHOW KEYS list: held rows on none of its rows dropped, each row's keys added */
inline void SeenNote(std::vector<SeenRow>* seen, const std::vector<townkeep::TownRow>& rows)
{
    std::vector<SeenRow> kept;
    for (size_t i = 0; i < seen->size(); ++i)
    {
        bool on = false;
        for (size_t r = 0; r < rows.size() && !on; ++r) on = SeenSameRow((*seen)[i], rows[r]);
        if (on) { kept.push_back(SeenRow()); kept.back().owner = (*seen)[i].owner; for (int j = 0; j < 3; ++j) kept.back().pos[j] = (*seen)[i].pos[j];
                  kept.back().full = (*seen)[i].full; kept.back().keys.swap((*seen)[i].keys); }
    }
    seen->swap(kept);
    for (size_t r = 0; r < rows.size(); ++r)
    {
        size_t at = seen->size();
        for (size_t i = 0; i < seen->size(); ++i) if (SeenSameRow((*seen)[i], rows[r])) { at = i; break; }
        if (at == seen->size())
        {
            seen->push_back(SeenRow());
            seen->back().owner = rows[r].owner;
            for (int j = 0; j < 3; ++j) seen->back().pos[j] = rows[r].pos[j];
        }
        SeenRow& s = (*seen)[at];
        for (size_t k = 0; k < rows[r].keys.size() && s.full == 0; ++k)
        {
            if (s.keys.count(rows[r].keys[k]) != 0) continue;
            if (s.keys.size() >= kSeenKeysCap) { s.full = 1; s.keys.clear(); break; }
            s.keys.insert(rows[r].keys[k]);
        }
    }
}
/* 1 = key row `row` said `key` (wire form) at some list held this world, or the row is full */
inline int SeenHas(const std::vector<SeenRow>& seen, const townkeep::TownRow& row, const std::string& key)
{
    for (size_t i = 0; i < seen.size(); ++i)
        if (SeenSameRow(seen[i], row)) return (seen[i].full != 0 || seen[i].keys.count(key) != 0) ? 1 : 0;
    return 0;
}
/* 1 = a copy in a shown town that is on none of its keys may leave it: the base's row went (going), or its key row came (keyRowFound),
   is not cut, and said the copy's key earlier (keySaidOnce: the owner took it out of the outpost; never said = the engine put it
   there at creation, as on the owner's game - it stays) */
inline int UnlinkOk(int going, int keyRowFound, int keyRowCut, int keySaidOnce)
{
    return (going != 0 || (keyRowFound != 0 && keyRowCut == 0 && keySaidOnce != 0)) ? 1 : 0;
}
/* 1 = such a copy is left where the engine put it (counted engineLeft): not going, its key row came, is not cut, never said its key */
inline int EngineLeft(int going, int keyRowFound, int keyRowCut, int keySaidOnce)
{
    return (going == 0 && keyRowFound != 0 && keyRowCut == 0 && keySaidOnce == 0) ? 1 : 0;
}
/* A COPY OF A TEAMMATE'S PIECE AND THE TOWN SHOWN FOR ITS OUTPOST (re-checked at every look, never settled):
   onRow - its key is on the key row of a base shown for its owner; townStands - that base's town stands here; inRowTown - the copy's own
   town handle words are that town's; inShownTown - (not on a row) its words are those of a base shown for its owner; unlinkOk - that
   base's key row came, is not cut and said the copy's key once (UnlinkOk), or the base's row went. kLinkUndo puts it back in the town
   PutBackPick chooses */
const int kLinkNone = 0, kLinkAlready = 1, kLinkDo = 2, kLinkNoTown = 3, kLinkUndo = 4;
inline int LinkStep(int onRow, int townStands, int inRowTown, int inShownTown, int unlinkOk)
{
    if (onRow != 0)
    {
        if (townStands == 0) return kLinkNoTown;
        return inRowTown != 0 ? kLinkAlready : kLinkDo;
    }
    if (inShownTown == 0 || unlinkOk == 0) return kLinkNone;
    return kLinkUndo;
}
/* P47 PUT-BACK: the town a copy leaving a shown town is given - the first choice building creation 0x57C1E0 makes, the nearest town
   whose area holds the spot (0x9279C0: a town whose state +0xD8 is not 8, whose vt+0x270 answers 0, with the spot within its radius
   vt+0x2A0; nearest by x/z distance, strict - of two equal the first stays), here with the going town and every shown town excluded,
   and player towns excluded (0x9279C0's own filter when its flag is set: a teammate's copy is not put in this player's outpost).
   -1 = none: the caller gives the wilderness town (TownList+0x180F8), as 0x9279C0 does.
   WHERE IT DIFFERS FROM 0x9279C0 (read 2026-10-18): (a) the engine looks only at the towns listed in the 3x3 cells of its 64x64 town
   grid (TownList+0xC0, cell by 0xA07C50, handles resolved by 0x9F8230) around the spot; the caller here offers every town of the
   all-towns list - a town whose centre lies outside those cells but whose radius covers the spot can be chosen here and not by the
   engine (following the grid needs two more engine routines in the address table, not worth it for a put-back a load redoes);
   (b) the player-town filter is always on here, the engine's only when its flag is set; (c) the engine starts its best at
   0x7F7FFFFF (the largest float), here the first town in range - the same for any finite distance */
struct PutBackTown { int excluded; int dead; int busy; int playerTown; float d2; float radius; };
inline int PutBackPick(const PutBackTown* c, int n)
{
    int best = -1;
    for (int i = 0; i < n; ++i)
    {
        const PutBackTown& t = c[i];
        if (t.excluded != 0 || t.dead != 0 || t.busy != 0 || t.playerTown != 0) continue;
        if (!(t.d2 <= t.radius * t.radius)) continue;   /* the spot outside its area (or a value that is not a number) */
        if (best < 0 || t.d2 < c[best].d2) best = i;
    }
    return best;
}
/* the icon index the engine gives a player outpost of this size level (0x48CEE0: plevel 1 -> 3, 2 -> 1, 3 -> 2, else 9) - the same index
   it gives a town of that type; IconNameOfLevel is that index's name, which the map hook writes */
inline int IconIndexOfLevel(int level) { return level == 1 ? 3 : level == 2 ? 1 : level == 3 ? 2 : 9; }
const unsigned kOutpostMapLayer = 0;   /* the map layer the engine gives every player outpost (0x48B220) */
/* the icon name 0x48CEE0 writes for a player outpost of this size level (its jump table at 0x48D024: index 9 SmallPlace, 3 Village,
   1 Outpost, 2 Town) */
inline const char* IconNameOfLevel(int level) { return level == 1 ? "Village" : level == 2 ? "Outpost" : level == 3 ? "Town" : "SmallPlace"; }
/* T-811 log (decisions 874 a / 875): the engine's five map-marker colours - each a MyGUI colour (4 floats, 16 bytes), side by side
   from the MapMarkerColourSlots row (1.0.65 0x212E528, 1.0.68 0x212F538) in this order, filled by the map screen's set-up from the
   skin's MarkerAlly / MarkerNeutral / MarkerEnemy / MarkerPlayer / MarkerPlayerSelected (both executables read). The engine's colour
   pick (MapMarkerColour, 0x48E890 / 0x48F320) answers a pointer to one of them. */
const int kMarkerColours = 5;
const unsigned long long kMarkerColourBytes = 16;
const int kMarkerNeutral = 1;   /* the slot the marker refresh (0x48F280) draws when the town's name is not shown (Town vt+0x340 false) */
inline const char* MarkerColourName(int slot)
{
    return slot == 0 ? "ally" : slot == 1 ? "neutral" : slot == 2 ? "enemy" : slot == 3 ? "own" : slot == 4 ? "own-selected" : "?";
}
inline const char* MarkerSkinName(int slot)
{
    return slot == 0 ? "MarkerAlly" : slot == 1 ? "MarkerNeutral" : slot == 2 ? "MarkerEnemy" : slot == 3 ? "MarkerPlayer" : slot == 4 ? "MarkerPlayerSelected" : "?";
}
/* which of the five a colour pointer is (-1 = none of them, or no slots address) */
inline int MarkerColourSlotOf(unsigned long long colour, unsigned long long slots)
{
    if (slots == 0 || colour < slots) return -1;
    const unsigned long long d = colour - slots;
    if (d % kMarkerColourBytes != 0 || d / kMarkerColourBytes >= (unsigned long long)kMarkerColours) return -1;
    return (int)(d / kMarkerColourBytes);
}
/* the slot the map draws (0x48F280): the pick when the town's name is shown, else the neutral slot */
inline int MarkerDrawnSlot(int nameShown, int picked) { return nameShown != 0 ? picked : kMarkerNeutral; }
const unsigned long kShowRetryMs = 30000;   /* a base that could not be taken out is tried again this long after */
/* a shown base no row names is taken out only when the list came whole */
inline bool RemoveAllowed(unsigned flags) { return (flags & kFlagCut) == 0; }
const int kGoneLeave = 0, kGoneDestroy = 1;
/* a shown base to take out (its row went, or a save): destroyed only when this game founded it, no loaded piece names it as its town,
   the loaded pieces could all be read, and the destroy call is in the address table; else it is left standing */
inline int GoneStep(int ours, int linkedPieces, int linkedUnread, int canDestroy)
{
    return (ours != 0 && linkedPieces == 0 && linkedUnread == 0 && canDestroy != 0) ? kGoneDestroy : kGoneLeave;
}
/* P47 STORE GATE (2026-10-18): a town the mod would destroy (GoneStep said kGoneDestroy) is kept while its model-piece store
   (Town+0x250: the table keyed by building, count +0x28, and the sign-highlight pair list, count +0x90) holds anything, or could not
   be read. createPhysical 0x55FF50 points a building at the store of the town its words name (Building+0x1F8) and adds its pieces;
   the model teardown takes them out through that pointer. A copy re-towned by its words alone (as the engine's own founding 0x4D6300
   does) keeps its pieces in the old town's store until its models are torn down, and TownDestroy 0x92A4F0 visits no building - so a
   destroy now would leave live buildings pointing into a freed town. The store empties when the area's models are torn down (the
   area unloads); the take-out then runs at its next retry. 1 = keep (do not destroy now), 0 = the store is empty */
inline int StoreKeep(int read, unsigned long long keys, unsigned long long signs)
{
    return (read == 0 || keys != 0 || signs != 0) ? 1 : 0;
}
/* the towns a store-gate refusal was logged for (by handle words): 1 = the first time for this town (log it, and note it), 0 = logged
   already, or the list is full (cap: no more are noted; the refusal is still counted) */
struct StoreLogged { int h[5]; };
inline int StoreLogOnce(std::vector<StoreLogged>* seen, const int* h, size_t cap)
{
    for (size_t i = 0; i < seen->size(); ++i) if (std::memcmp((*seen)[i].h, h, sizeof((*seen)[i].h)) == 0) return 0;
    if (seen->size() >= cap) return 0;
    StoreLogged s;
    std::memcpy(s.h, h, sizeof(s.h));
    seen->push_back(s);
    return 1;
}
const int kRowNone = 0, kRowKeep = 1, kRowTake = 2, kRowFound = 3;
/* a row: its own town still stands - keep it; else a town of the owner's stand-in at its centre - take it; else found one (when
   TownFoundPlayer is in the address table) */
inline int RowStep(int ownTownStands, int otherStands, int canFound)
{
    if (ownTownStands != 0) return kRowKeep;
    if (otherStands != 0) return kRowTake;
    return canFound != 0 ? kRowFound : kRowNone;
}

}   /* namespace baseshow */
