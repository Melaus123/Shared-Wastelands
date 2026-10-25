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
const unsigned kProtocol = 108;   /* the world-server protocol that carries SHOW (101: SHOW KEYS follows each SHOW) */
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
   row's centre (RowsFor copies the centre bit for bit), with its name, size level, keys and cut flag, and (T-924) the handle words its
   owner's game said for it - an identity a game matches its copy by (MatchIdent), never a town on another game; the same five words
   the wire always carried (they were 0): no new field, no protocol change. A said row not valid for SHOW KEYS is left out (that row's copies are then not linked on the viewer's game, and none is unlinked) */
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
            for (int j = 0; j < 5; ++j) k.hand[j] = t.hand[j];   /* T-924: the owner's own town handle words (0 when its game said none) */
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
    int ownerHand[5];       /* T-924: the owner's own town handle words, from its key row (all 0: not known) */
    std::set<std::string> keys;   /* T-924: the keys its key row last named (a re-founded outpost keeps its pieces' keys) */
    Shown() : owner(-1), ours(0), level(0), going(0), drawn(0) { pos[0] = pos[1] = pos[2] = 0.0f; for (int i = 0; i < 5; ++i) { hand[i] = 0; ownerHand[i] = 0; } }
};
/* T-924 IDENTITY: the key row of a SHOW row - the one of the same owner at exactly its centre (KeyRowsFor copies it bit for bit;
   -1 none: the SHOW KEYS for it has not come yet) */
inline int KeyRowOfRow(const ShowRow& r, const std::vector<townkeep::TownRow>& keyRows)
{
    for (size_t k = 0; k < keyRows.size(); ++k)
        if (keyRows[k].owner == r.owner && keyRows[k].pos[0] == r.pos[0] && keyRows[k].pos[1] == r.pos[1] && keyRows[k].pos[2] == r.pos[2]) return (int)k;
    return -1;
}
/* a shown base takes its row's identity: the owner's town handle words when its key row carries them, its keys when it names any
   (a key row with neither leaves what the base knew) */
inline void ShownIdentity(Shown* s, const townkeep::TownRow& keyRow)
{
    if (townkeep::HandKnown(keyRow.hand)) for (int i = 0; i < 5; ++i) s->ownerHand[i] = keyRow.hand[i];
    if (!keyRow.keys.empty()) { s->keys.clear(); s->keys.insert(keyRow.keys.begin(), keyRow.keys.end()); }
}
inline int SharesKey(const Shown& s, const townkeep::TownRow& keyRow)
{
    for (size_t k = 0; k < keyRow.keys.size(); ++k) if (s.keys.count(keyRow.keys[k]) != 0) return 1;
    return 0;
}
/* each row's shown base (-1 none), of the same owner, not yet matched, in three passes: 1. the owner's town handle (the row's key
   row carries it and the base knows it, the same words); 2. a shared piece key (an outpost the owner's game founded again, with a new
   handle and a moved centre, still holds its pieces); 3. today's rule, the first within kSameBaseRadius of the row's centre - only
   for a pair whose identity is not known on both sides (a world server that sends zeros, a base taken up before its key row came).
   A matched row whose centre moved keeps its base (no second copy). *gone: the shown bases no row matched, in order */
inline void MatchIdent(const std::vector<Shown>& shown, const std::vector<ShowRow>& rows, const std::vector<townkeep::TownRow>& keyRows,
                       std::vector<int>* rowTo, std::vector<size_t>* gone)
{
    std::vector<int> used(shown.size(), 0);
    rowTo->assign(rows.size(), -1);
    std::vector<int> kr(rows.size(), -1);
    for (size_t r = 0; r < rows.size(); ++r) kr[r] = KeyRowOfRow(rows[r], keyRows);
    for (size_t r = 0; r < rows.size(); ++r)
    {
        if (kr[r] < 0 || !townkeep::HandKnown(keyRows[(size_t)kr[r]].hand)) continue;
        for (size_t s = 0; s < shown.size(); ++s)
            if (used[s] == 0 && shown[s].owner == rows[r].owner && townkeep::HandKnown(shown[s].ownerHand) && townkeep::HandSame(shown[s].ownerHand, keyRows[(size_t)kr[r]].hand))
            { used[s] = 1; (*rowTo)[r] = (int)s; break; }
    }
    for (size_t r = 0; r < rows.size(); ++r)
    {
        if ((*rowTo)[r] >= 0 || kr[r] < 0) continue;
        for (size_t s = 0; s < shown.size(); ++s)
            if (used[s] == 0 && shown[s].owner == rows[r].owner && SharesKey(shown[s], keyRows[(size_t)kr[r]]) != 0)
            { used[s] = 1; (*rowTo)[r] = (int)s; break; }
    }
    for (size_t r = 0; r < rows.size(); ++r)
    {
        if ((*rowTo)[r] >= 0) continue;
        const bool rowHand = kr[r] >= 0 && townkeep::HandKnown(keyRows[(size_t)kr[r]].hand);
        for (size_t s = 0; s < shown.size(); ++s)
            if (used[s] == 0 && shown[s].owner == rows[r].owner && (!rowHand || !townkeep::HandKnown(shown[s].ownerHand))
                && keptwire::NearXZ(shown[s].pos[0], shown[s].pos[2], rows[r].pos[0], rows[r].pos[2], kSameBaseRadius) != 0)
            { used[s] = 1; (*rowTo)[r] = (int)s; break; }
    }
    gone->clear();
    for (size_t s = 0; s < shown.size(); ++s) if (used[s] == 0) gone->push_back(s);
}
/* with no key rows: today's rule alone (the first not yet matched of the same owner within kSameBaseRadius of the row's centre) */
inline void Match(const std::vector<Shown>& shown, const std::vector<ShowRow>& rows, std::vector<int>* rowTo, std::vector<size_t>* gone)
{
    MatchIdent(shown, rows, std::vector<townkeep::TownRow>(), rowTo, gone);
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

/* ---- T-924 COPY LIST (2026-10-23, T1441): THIS GAME'S COPIES OF ITS TEAMMATES' OUTPOSTS, BY HANDLE WORDS ----
   The engine founds a copy with the local player's faction and its default name; the mod writes the stand-in faction and the label
   after. A copy a save held (its model-piece store kept it, StoreKeep) came back after a reload as THIS player's own town (T1441: the
   same handle, the player's faction, 0 keys), was said to the world server as its own, and a teammate's game founded a copy of it.
   So each game lists the copies it founded or took up - its slot, the copy's handle words and centre, the owner's town handle words
   and name when known - in its world folder: kCopyListFilePrefix + the player key + ".txt". A town whose handle is listed, standing
   within kSameBaseRadius of its listed centre (CopyAt: a handle the engine gave another town since is not taken), is that slot's copy
   whatever its faction or name: taken up again with the stand-in faction and the label written (TakeUpSlot), and never said as this
   player's own (SayOwnTown). T-924 FOLD 1: a row is never dropped when its copy is taken out or its town is gone - an older save
   reloaded can still hold that copy; a row whose town is gone is near harmless (a take-up needs the handle AND the centre) - and past
   kCopyListCap the oldest row goes. The list not read (CopyListState) holds this player's SAY. FILE: TAB-separated, LF line ends (a CR before it read as nothing); line 1 "<kCopyListHeader><TAB>1", then per copy
     C <owner slot> <5 x copy handle word> <x> <y> <z> <5 x owner's town handle word (0: not known)> <owner's outpost name>
   ints decimal, floats the 8 lower-case hex digits of their IEEE bits, the name escaped as zonekeep.h's lists. */
const char* const kCopyListFilePrefix = "shown-town-copies-";
const char* const kCopyListHeader = "shown-town-copies";
const int kCopyListFormat = 1;
const size_t kCopyListCap = 1024;   /* rows kept (kMaxShowRows bases at once, with room for the ones a save held) */
struct CopyRow
{
    int owner;          /* the owner's player slot */
    int hand[5];        /* the copy's town handle words in THIS game */
    float pos[3];       /* the copy town's own centre as read here */
    int ownerHand[5];   /* the owner's own town handle words (all 0: not known) */
    std::string name;   /* the owner's outpost name ("" = not known) */
    CopyRow() : owner(-1) { for (int i = 0; i < 5; ++i) { hand[i] = 0; ownerHand[i] = 0; } pos[0] = pos[1] = pos[2] = 0.0f; }
};
inline bool CopyRowOk(const CopyRow& r)
{
    if (r.owner < 0 || r.owner > townkeep::kOwnerMax || !townkeep::HandKnown(r.hand)) return false;
    for (int j = 0; j < 3; ++j) if (!keptwire::FloatOk(r.pos[j])) return false;
    return keptwire::TextOk(r.name, keptwire::kStrMax, true);
}
/* the row of a handle (-1 none, or no handle) */
inline int CopyFind(const std::vector<CopyRow>& l, const int* h)
{
    if (!townkeep::HandKnown(h)) return -1;
    for (size_t i = 0; i < l.size(); ++i) if (townkeep::HandSame(l[i].hand, h)) return (int)i;
    return -1;
}
/* the row listed for a town of handle h standing at lp: its index when the handle is listed and lp is within kSameBaseRadius of the
   listed centre; -1 none */
inline int CopyAt(const std::vector<CopyRow>& l, const int* h, const float* lp)
{
    const int i = CopyFind(l, h);
    if (i < 0) return -1;
    return keptwire::NearXZ(lp[0], lp[2], l[(size_t)i].pos[0], l[(size_t)i].pos[2], kSameBaseRadius) != 0 ? i : -1;
}
/* a copy noted (founded, taken up, or its identity learnt): 1 = the list changed. A row of the same handle is updated - an owner's
   handle or name once known is kept when the note has none; a row not valid (no handle) is not noted; a new row past kCopyListCap
   makes room by dropping the oldest. T-924 FOLD 2: a row noted again moves to the newest end (the list runs oldest to newest noted);
   at the cap the oldest row whose handle is not in live (zonekeep::TownHandText of the bases standing now) goes - a live copy's row
   never does; with every row live none goes (*allLive = 1). A move alone answers 0 (the file is written at the next change) */
inline int CopyNote(std::vector<CopyRow>* l, const CopyRow& r, const std::set<std::string>* live = 0, int* allLive = 0)
{
    if (allLive != 0) *allLive = 0;
    if (!CopyRowOk(r)) return 0;
    const int i = CopyFind(*l, r.hand);
    if (i < 0)
    {
        while (l->size() >= kCopyListCap)
        {
            size_t drop = l->size();
            for (size_t k = 0; k < l->size() && drop == l->size(); ++k)
                if (live == 0 || live->count(zonekeep::TownHandText((*l)[k].hand)) == 0) drop = k;
            if (drop == l->size()) { if (allLive != 0) *allLive = 1; break; }
            l->erase(l->begin() + (long)drop);
        }
        l->push_back(r);
        return 1;
    }
    const CopyRow o = (*l)[(size_t)i];
    CopyRow m = r;
    if (!townkeep::HandKnown(m.ownerHand)) for (int j = 0; j < 5; ++j) m.ownerHand[j] = o.ownerHand[j];
    if (m.name.empty()) m.name = o.name;
    const bool same = m.owner == o.owner && m.pos[0] == o.pos[0] && m.pos[1] == o.pos[1] && m.pos[2] == o.pos[2] && townkeep::HandSame(m.ownerHand, o.ownerHand) && m.name == o.name;
    l->erase(l->begin() + i);
    l->push_back(same ? o : m);
    return same ? 0 : 1;
}
/* one report when the list has stayed unread this long (logged = 1 once made) */
const unsigned long kCopyHoldReportMs = 60000;
inline int CopyHoldReport(unsigned long heldMs, int logged) { return (logged == 0 && heldMs >= kCopyHoldReportMs) ? 1 : 0; }
/* THE LIST'S STATE after a look at its file: in - read, or no file (an empty list); held - no file named (no world folder or player
   key yet), the file is there but would not open (locked), or a read error. While held this player's own outposts are not said
   (SayAllowed: a copy among them would be said as its own) and the shown bases are not applied; tried again every kCopyListRetryMs */
const int kCopyListHold = 0, kCopyListIn = 1;
const unsigned long kCopyListRetryMs = 2000;
inline int CopyListState(int pathKnown, int opened, int absent, int readOk)
{
    if (pathKnown == 0) return kCopyListHold;
    if (opened == 0) return absent != 0 ? kCopyListIn : kCopyListHold;
    return readOk != 0 ? kCopyListIn : kCopyListHold;
}
inline int SayAllowed(int listState) { return listState == kCopyListIn ? 1 : 0; }
/* T-924 FOLD 3 (T1442: the list's folder did not exist on a client, read as 'no file' and every write failed unseen): WHY the list is
   in or held - no file named (no data folder or player key yet), the world's data folder not made yet (held: it is made at the record
   index read), no file in the folder (an empty list), the file unreadable (locked, a read error: held), read */
const int kCopyWhyRead = 0, kCopyWhyNoFile = 1, kCopyWhyNoPath = 2, kCopyWhyNoFolder = 3, kCopyWhyUnreadable = 4;
inline int CopyListWhy(int pathKnown, int folderThere, int opened, int fileAbsent, int readOk)
{
    if (pathKnown == 0) return kCopyWhyNoPath;
    if (folderThere == 0) return kCopyWhyNoFolder;
    if (opened == 0) return fileAbsent != 0 ? kCopyWhyNoFile : kCopyWhyUnreadable;
    return readOk != 0 ? kCopyWhyRead : kCopyWhyUnreadable;
}
inline int CopyListStateOf(int why) { return (why == kCopyWhyRead || why == kCopyWhyNoFile) ? kCopyListIn : kCopyListHold; }
inline const char* CopyWhyName(int why)
{
    return why == kCopyWhyRead ? "read" : why == kCopyWhyNoFile ? "no file yet" : why == kCopyWhyNoPath ? "no file named (no data folder or player key yet)"
         : why == kCopyWhyNoFolder ? "the world's data folder is not made yet" : why == kCopyWhyUnreadable ? "the file is there but cannot be read" : "none";
}
/* the list may be written over its file: it was taken from it (readIn) and the file was read whole (or absent), or its bad lines were
   kept aside first (badKept) - never over content that could not be read */
inline int CopyWriteOk(int readIn, int whole, int badKept) { return (readIn != 0 && (whole != 0 || badKept != 0)) ? 1 : 0; }
/* where a file with bad lines is kept aside: <file>.bad, then .bad2 .. .bad<kCopyBadNames> (one already there is never written over) */
const int kCopyBadNames = 9;
inline std::string BadCopyName(const std::string& path, int n) { return n <= 1 ? path + ".bad" : path + ".bad" + zonekeep::ZkNum(n); }
/* a town standing here untracked, at a reload's take-up: the slot it is taken up as a copy of, -1 none. A listed town (listedOwner
   >= 0, CopyAt) is its listed slot's copy whatever its faction or name; else, once this world's kept list is in (markOk), one carrying
   the mod's mark (LeftoverMark) of a slot not this player's */
inline int TakeUpSlot(int listedOwner, int mySlot, int markOk, int standInSlot, int labelForm, int nearKept)
{
    if (listedOwner >= 0 && listedOwner != mySlot) return listedOwner;
    if (markOk != 0 && standInSlot != mySlot && LeftoverMark(standInSlot, labelForm, nearKept) != 0) return standInSlot;
    return -1;
}
/* SAY: 1 = a town of this player's faction that is not a listed copy (said as this player's own) */
inline int SayOwnTown(int ownFaction, int listedCopy) { return (ownFaction != 0 && listedCopy == 0) ? 1 : 0; }
/* THE FIRST APPLY (the SHOW KEYS that carries the rows' identity follows each SHOW, as its own message): a row that would have a copy
   founded for it while no whole SHOW KEYS came since its SHOW, and none held names it, waits - at most kShowKeysWaitMs (then founded
   as today) - so a moved outpost is matched to its copy, not founded twice. 1 = wait */
const unsigned long kShowKeysWaitMs = 10000;   /* Guess: the world server sends SHOW KEYS right after SHOW */
inline int FoundWait(int keysSinceShow, int rowHasKeyRow, unsigned long waitedMs)
{
    return (keysSinceShow == 0 && rowHasKeyRow == 0 && waitedMs < kShowKeysWaitMs) ? 1 : 0;
}
/* ONE APPLY'S PLAN (T-924 fold): rowTo - each row's shown base (MatchIdent); rowWait - 1 = a row that waits for its SHOW KEYS before a
   copy is founded for it (FoundWait); gone - the unmatched bases to take out; held - the unmatched bases of an owner with a row
   waiting (kept as they are: the identity match runs first once the keys come, at the latest when the wait ends) */
struct ApplyPlan
{
    std::vector<int> rowTo;
    std::vector<int> rowWait;
    std::vector<size_t> gone;
    std::vector<size_t> held;
};
inline void PlanApply(const std::vector<Shown>& shown, const std::vector<ShowRow>& rows, const std::vector<townkeep::TownRow>& keyRows,
                      int keysSinceShow, unsigned long waitedMs, ApplyPlan* p)
{
    std::vector<size_t> unmatched;
    MatchIdent(shown, rows, keyRows, &p->rowTo, &unmatched);
    p->rowWait.assign(rows.size(), 0);
    std::set<int> waitOwners;
    for (size_t r = 0; r < rows.size(); ++r)
        if (FoundWait(keysSinceShow, KeyRowOfRow(rows[r], keyRows) >= 0 ? 1 : 0, waitedMs) != 0) { p->rowWait[r] = 1; waitOwners.insert(rows[r].owner); }
    p->gone.clear();
    p->held.clear();
    for (size_t i = 0; i < unmatched.size(); ++i)
    {
        if (waitOwners.count(shown[unmatched[i]].owner) != 0) p->held.push_back(unmatched[i]);
        else p->gone.push_back(unmatched[i]);
    }
}
inline std::string EncodeCopyList(const std::vector<CopyRow>& l)
{
    std::string o = std::string(kCopyListHeader) + "\t" + zonekeep::ZkNum(kCopyListFormat) + "\n";
    for (size_t i = 0; i < l.size(); ++i)
    {
        const CopyRow& r = l[i];
        o += "C\t" + zonekeep::ZkNum(r.owner);
        for (int j = 0; j < 5; ++j) o += "\t" + zonekeep::ZkNum(r.hand[j]);
        for (int j = 0; j < 3; ++j) o += "\t" + zonekeep::FloatHex(r.pos[j]);
        for (int j = 0; j < 5; ++j) o += "\t" + zonekeep::ZkNum(r.ownerHand[j]);
        o += "\t" + zonekeep::Esc(r.name) + "\n";
    }
    return o;
}
/* the rows back, line by line: every good row kept (a handle twice: the later row wins; past kCopyListCap the oldest goes). The answer:
   the lines that could not be read (0 = read whole; an empty file reads as an empty list). A first line that is not this list's
   header of this format counts every line bad and keeps no row (*why names the first bad line) */
inline size_t DecodeCopyList(const std::string& text, std::vector<CopyRow>* out, std::string* why)
{
    out->clear();
    std::vector<CopyRow> rows;
    std::vector<std::string> f;
    size_t at = 0, bad = 0;
    int lineNo = 0;
    bool headerBad = false;
    while (at < text.size())
    {
        size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(at, e - at);
        at = e + 1;
        ++lineNo;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        const std::string where = "line " + zonekeep::ZkNum(lineNo) + ": ";
        zonekeep::SplitTabs(line, &f);
        if (lineNo == 1)
        {
            int v = 0;
            if (f.size() != 2 || f[0] != kCopyListHeader || !zonekeep::DecInt(f[1], &v) || v != kCopyListFormat) headerBad = true;
            if (headerBad) { ++bad; *why = where + "not a " + kCopyListHeader + " list of format " + zonekeep::ZkNum(kCopyListFormat); }
            continue;
        }
        if (line.empty()) continue;
        if (headerBad) { ++bad; continue; }
        CopyRow r;
        bool ok = f.size() == 16u && f[0] == "C" && zonekeep::DecInt(f[1], &r.owner);
        for (int j = 0; ok && j < 5; ++j) ok = zonekeep::DecInt(f[(size_t)(2 + j)], &r.hand[j]);
        for (int j = 0; ok && j < 3; ++j) ok = zonekeep::HexFloat(f[(size_t)(7 + j)], &r.pos[j]);
        for (int j = 0; ok && j < 5; ++j) ok = zonekeep::DecInt(f[(size_t)(10 + j)], &r.ownerHand[j]);
        ok = ok && zonekeep::Unesc(f[15], &r.name) && CopyRowOk(r);
        if (!ok) { if (bad == 0) *why = where + "a copy line this build cannot read"; ++bad; continue; }
        const int i = CopyFind(rows, r.hand);
        if (i >= 0) rows[(size_t)i] = r;
        else CopyNote(&rows, r);
    }
    out->swap(rows);
    return bad;
}

}   /* namespace baseshow */
