#pragma once
/* THE PLAYERS' OUTPOSTS CARRIED INTO A RESET WORLD (TOWNS, world-server kind 70). Compiled into the world server (store_main.cpp), the
   game (build.cpp, store.cpp) and the offline suite (test_main.cpp). Pure: no disk, no clock, no engine. Header-only. No C++11.

   A player's outpost is a Town the game founds when the player places a building away from any town (0x4DA6F0 -> 0x933F40); each
   placed top-level building is linked to it (the town's handle words at Building +0x1D8) and its area record names that town by five handle words (townTYPE, townC, townCS,
   townI, townS). The world server holds no save of the games, so:
   - EACH GAME SAYS ITS OWN OUTPOSTS (SAY): name, centre and handle words of every player town of its player's faction, and the
     position keys of the registered pieces linked to it (the engine's BuildingGetTown reads that town). A player town's handle words as the game
     reads them are not the words the engine's save writes for that town, so a kept piece is tied to its outpost by their keys, not
     by a town handle. The world server keeps the said outposts per player slot in player-towns.txt (ReplaceSlotTowns: a SAY replaces
     that slot's rows; a row whose key list is cut keeps the keys its outpost had on file, and a SAY whose town list was not read
     whole keeps the slot's outposts it does not name).
   - A RESET THAT KEEPS THE BUILDINGS gives each kept piece to the said outpost of its owner one of whose keys is the same piece
     (KeptTownsOf; TownKeysSame: the same text, the two texts of one key, or the same record in the same sector within a tenth of a
     unit). Never by a town handle and never by distance alone (a bought house in a world town is on no player town's key list). An
     owner with no row in player-towns.txt is matched against the outposts the world's own earlier reset carried, with their keys
     (its kept-towns.txt; SaidWithCarried), and each of those carries on the kept pieces of its owner its keys name that no outpost
     took (CarryKeptKeys). The matched outposts, each with the keys of its kept pieces, are written to the reset world's
     kept-towns.txt. A kept piece no key names goes to the nearest said outpost of its owner within kOutpostTakeRadius of its
     centre (NearOutpostsTake); the owner's pieces still left with no outpost of theirs near get outposts made from them, by town and
     distance (OutpostsFromPieces). Neither step touches a piece in a world town (a town a building no player owns names: a bought
     house and its furniture; InWorldTown). A said outpost with no kept piece is not carried.
   - THE WORLD SERVER SENDS THE KEPT OUTPOSTS (LIST) to each game at its link's first AREAS report, before its first kept rows (an
     empty list too: the game drops any list it holds). Every game takes or founds the outpost (TownLayStep) for each kept piece of it
     loaded there and links the piece to it, whatever town the engine gave it (a bought or handed-back house is on no outpost's key
     list); a piece standing inside a building keeps its host's town. A SAY with
     no outposts, while that slot has rows on file, is taken only as the kEmptyWholeToClear-th SAY in a row on one link with no
     outposts and its town list read whole (EmptyWholeRun, EmptySayIgnored); the game marks no SAY whole before its player has been
     in the world it loaded for a while.
   WIRE (little-endian, as every world-server message): u32 op (SAY 1 up, LIST 2 down, SHOW KEYS 3 down - protocol 101), u32 flags,
   u32 count 0..kMaxTownsMsg, count x TOWN
     flags LIST: 1 = the list starts again with this message (0 or 1). SAY: kSayFirst (this message starts a SAY), kSayMore (more
           messages of this SAY follow), kSayWhole (the game read its whole town list); a SAY that would pass kTownsMsgBytes is
           split between its rows (EncodeTownsSay). SHOW KEYS: kSayFirst (the key list starts again with this message), kSayMore
           (more messages of it follow); split as a SAY is (EncodeShowKeys)
     SHOW KEYS (owner 936 / 937, 2026-10-17): sent right after every SHOW (baseshow.h) - per teammate outpost the SHOW carries, its
           owner slot, centre, name, size level and the said keys of its linked pieces with the cut flag (handle words all 0: the
           owner's game's words mean nothing on another game); the game links its copies of those pieces to the town it shows for
           the outpost (baseshow.h KeyRowsFor / LinkStep)
     TOWN  i32 owner slot (SAY: -1, the sender), 5 x i32 handle words, f32 x, f32 y (height), f32 z, str name (0..128 bytes, no control
           characters), u32 keys 0..kMaxTownKeys (with kKeysCut set: the outpost has pieces the list does not name), keys x str
           (1..47 bytes, each once), u32 size level 0..kLevelMax (T-811, protocol 100: the outpost's plevel, Town+0x388 - which outpost
           icon the map draws)
     str = u32 length, the bytes.
   FILES (player-towns.txt, kept-towns.txt): TAB-separated, LF line ends (a CR before it is read as nothing); line 1 "<header><TAB>3"
   (player-towns / kept-towns), then per outpost
     T <owner slot> <type> <container> <container stamp> <index> <serial> <x> <y> <z> <name> <cut 0 or 1> <size level 0..3>
     K <key>     (after its outpost's T line: one key of that outpost - a said piece in player-towns, a kept piece in kept-towns)
   Formats 1 (no cut field) and 2 (no size level: read as 0) are still read. A float is the 8 lower-case hex digits of its IEEE bits, an int decimal, text escaped as
   zonekeep.h's list (\\ \t \n \r). */
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cstring>
#include "keptwire.h"   /* PutU32 / PutStr / TextOk / FloatOk / NearXZ, the key and text caps; zonekeep.h: the kept piece, the file helpers */

namespace townkeep {

const unsigned kMsgTowns = 70;
const unsigned kOpSay = 1, kOpList = 2, kOpShowKeys = 3;   /* SHOW KEYS (3): world-server protocol 101 */
const size_t kMaxTownsMsg = 32;        /* outposts in one TOWNS */
const size_t kMaxTownsPerSlot = 32;    /* outposts one player's SAY is kept with */
const size_t kMaxTownKeys = 512;       /* kept pieces one outpost row names */
const int kOwnerMax = (int)keptwire::kClaimMax - 1;   /* an owner slot: a claim field is slot + 1 */
const char* const kPlayerTownsFile = "player-towns.txt";
const char* const kPlayerTownsHeader = "player-towns";
const char* const kKeptTownsFile = "kept-towns.txt";
const char* const kKeptTownsHeader = "kept-towns";
const int kTownsFormat = 3;   /* the outpost list files' format this build writes (3: with the size level, T-811) */
const int kTownsFormatCut = 2;   /* the format before it (no size level): still read, as level 0 */
const int kLevelMax = 3;   /* an outpost's size level (Town+0x388 'plevel', set by the engine 0x48B770 from the buildings near it): 0..3 */
const int kTownsFormatKeyless = 1;   /* the format before it (no cut field): still read */
const unsigned kSayFirst = 1, kSayMore = 2, kSayWhole = 4;   /* a SAY's flags word */
const unsigned kKeysCut = 0x80000000u;   /* on a TOWN's key count: the outpost has pieces its list does not name */
const size_t kTownsMsgBytes = 60000;   /* one TOWNS message at most: under the world-server road's 64 KiB (liveenvelope.h kLiveInnerMax) with room for its headers */
const float kTownSameRadius = 50.0f;    /* Guess: a town of the owner's faction whose centre is this close (x, z) to a kept outpost's centre is that outpost */
const float kOutpostTakeRadius = 1000.0f;   /* Guess: a kept piece no key names this close (x, z) to an outpost's centre is of that outpost (NearOutpostsTake, OutpostsFromPieces) */

struct TownRow
{
    int owner;                       /* the player's slot; -1 in a SAY (the sender) */
    int hand[5];                     /* type, container, container stamp, index, serial */
    float pos[3];                    /* the centre: x, height, z */
    std::string name;
    std::vector<std::string> keys;   /* SAY and player-towns.txt: the pieces linked to the outpost; LIST and kept-towns.txt: its kept pieces */
    int cut;                         /* 1 = the outpost has pieces keys does not name (its game's list was cut, or not every piece could be read) */
    int level;                       /* T-811: the outpost's size level 0..kLevelMax as its owner's game reads it (0 in a LIST and kept-towns.txt) */
    TownRow() : owner(-1), cut(0), level(0) { for (int i = 0; i < 5; ++i) hand[i] = 0; pos[0] = pos[1] = pos[2] = 0.0f; }
};

inline bool HandSame(const int* a, const int* b) { for (int i = 0; i < 5; ++i) if (a[i] != b[i]) return false; return true; }
/* a handle a game read (an outpost made from kept pieces has none: all five words 0) */
inline bool HandKnown(const int* a) { for (int i = 0; i < 5; ++i) if (a[i] != 0) return true; return false; }
/* a town name as the wire carries it: each control byte becomes '?', cut to 128 bytes */
inline std::string NameFit(const std::string& s)
{
    std::string o(s.size() > keptwire::kStrMax ? s.substr(0, keptwire::kStrMax) : s);
    for (size_t i = 0; i < o.size(); ++i) { const unsigned char c = (unsigned char)o[i]; if (c < 0x20 || c == 0x7F) o[i] = '?'; }
    return o;
}
/* a row the op may carry: SAY - owner -1; LIST, SHOW KEYS (and both files) - a real owner slot; at most kMaxTownKeys keys, cut 0 or 1, every float
   in range, the name and each key valid, no key twice */
inline bool RowOk(const TownRow& t, unsigned op)
{
    if (op == kOpSay) { if (t.owner != -1) return false; }
    else if (op == kOpList || op == kOpShowKeys) { if (t.owner < 0 || t.owner > kOwnerMax) return false; }
    else return false;
    if (t.keys.size() > kMaxTownKeys || (t.cut != 0 && t.cut != 1) || t.level < 0 || t.level > kLevelMax) return false;
    for (int j = 0; j < 3; ++j) if (!keptwire::FloatOk(t.pos[j])) return false;
    if (!keptwire::TextOk(t.name, keptwire::kStrMax, true)) return false;
    std::set<std::string> seen;
    for (size_t k = 0; k < t.keys.size(); ++k) if (!keptwire::TextOk(t.keys[k], keptwire::kKeyMax, false) || !seen.insert(t.keys[k]).second) return false;
    return true;
}

/* ---- the wire ---- */
/* the flags word: LIST 0 or 1; SAY any of kSayFirst, kSayMore, kSayWhole; SHOW KEYS any of kSayFirst, kSayMore */
inline bool FlagsOk(unsigned op, unsigned f)
{
    if (op == kOpSay) return (f & ~(kSayFirst | kSayMore | kSayWhole)) == 0;
    if (op == kOpShowKeys) return (f & ~(kSayFirst | kSayMore)) == 0;
    return f <= 1;
}
inline bool OpKnown(unsigned op) { return op == kOpSay || op == kOpList || op == kOpShowKeys; }
struct TkReader
{
    const unsigned char* p; size_t n, at; bool bad;
    TkReader(const char* q, size_t len) : p((const unsigned char*)q), n(len), at(0), bad(q == 0 && len != 0) {}
    void Take(void* out, size_t k) { if (bad || n - at < k) { bad = true; return; } std::memcpy(out, p + at, k); at += k; }
    unsigned U32() { unsigned v = 0; Take(&v, 4); return v; }
    int I32() { int v = 0; Take(&v, 4); return v; }
    float F32() { float v = 0.0f; Take(&v, 4); return v; }
    std::string Str(size_t cap, bool mayBeEmpty)
    {
        const unsigned len = U32();
        if (bad || len > cap || (size_t)len > n - at) { bad = true; return std::string(); }
        std::string s((const char*)p + at, (size_t)len);
        at += len;
        if (!keptwire::TextOk(s, cap, mayBeEmpty)) bad = true;
        return s;
    }
    bool Done() const { return !bad && at == n; }
};
/* SAY, LIST or SHOW KEYS: 0..kMaxTownsMsg rows, every one valid for the op (nothing written for a refused one) */
inline bool EncodeTowns(std::vector<char>* out, unsigned op, int first, const std::vector<TownRow>& rows)
{
    out->clear();
    if (!OpKnown(op) || first < 0 || !FlagsOk(op, (unsigned)first) || rows.size() > kMaxTownsMsg) return false;
    keptwire::PutU32(out, op); keptwire::PutU32(out, (unsigned)first); keptwire::PutU32(out, (unsigned)rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const TownRow& t = rows[i];
        if (!RowOk(t, op)) { out->clear(); return false; }
        keptwire::PutI32(out, t.owner);
        for (int j = 0; j < 5; ++j) keptwire::PutI32(out, t.hand[j]);
        for (int j = 0; j < 3; ++j) keptwire::PutF32(out, t.pos[j]);
        keptwire::PutStr(out, t.name);
        keptwire::PutU32(out, (unsigned)t.keys.size() | (t.cut != 0 ? kKeysCut : 0u));
        for (size_t k = 0; k < t.keys.size(); ++k) keptwire::PutStr(out, t.keys[k]);
        keptwire::PutU32(out, (unsigned)t.level);
    }
    return true;
}
/* 1 = decoded (every row valid for its op, nothing trailing), 0 = malformed (nothing is taken from it) */
inline int DecodeTowns(const char* p, size_t n, unsigned* op, int* first, std::vector<TownRow>* rows)
{
    rows->clear(); *op = 0; *first = 0;
    TkReader r(p, n);
    const unsigned o = r.U32(), f = r.U32(), c = r.U32();
    if (r.bad || !OpKnown(o) || !FlagsOk(o, f) || c > kMaxTownsMsg) return 0;
    std::vector<TownRow> v;
    for (unsigned i = 0; i < c; ++i)
    {
        TownRow t;
        t.owner = r.I32();
        for (int j = 0; j < 5; ++j) t.hand[j] = r.I32();
        for (int j = 0; j < 3; ++j) t.pos[j] = r.F32();
        t.name = r.Str(keptwire::kStrMax, true);
        const unsigned nkw = r.U32(), nk = nkw & ~kKeysCut;
        t.cut = (nkw & kKeysCut) != 0 ? 1 : 0;
        if (r.bad || nk > kMaxTownKeys || (size_t)nk > (r.n - r.at) / 5) return 0;   /* a key takes 5 bytes at least */
        for (unsigned k = 0; k < nk && !r.bad; ++k) t.keys.push_back(r.Str(keptwire::kKeyMax, false));
        const unsigned lv = r.U32();
        t.level = lv > (unsigned)kLevelMax ? -1 : (int)lv;   /* -1: refused by RowOk */
        if (r.bad || !RowOk(t, o)) return 0;
        v.push_back(t);
    }
    if (!r.Done()) return 0;
    *op = o; *first = (int)f; rows->swap(v);
    return 1;
}
/* one row's bytes on the wire */
inline size_t TownBytes(const TownRow& t)
{
    size_t b = 4 + 20 + 12 + 4 + t.name.size() + 4 + 4;   /* ... the key count and the size level */
    for (size_t k = 0; k < t.keys.size(); ++k) b += 4 + t.keys[k].size();
    return b;
}
/* A SAY AS MESSAGES: the rows in order, a message closed before a row that would take it past kTownsMsgBytes or kMaxTownsMsg rows;
   the first carries kSayFirst, every one but the last kSayMore, every one kSayWhole when whole != 0; no rows: one message. False = a
   row not valid for a SAY or too big for a message alone (msgs then holds nothing) */
inline bool EncodeTownsSay(std::vector<std::vector<char> >* msgs, const std::vector<TownRow>& rows, int whole)
{
    msgs->clear();
    std::vector<std::vector<TownRow> > parts(1);
    size_t bytes = 12;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const size_t b = TownBytes(rows[i]);
        if (!RowOk(rows[i], kOpSay) || 12 + b > kTownsMsgBytes) return false;
        if (!parts.back().empty() && (parts.back().size() >= kMaxTownsMsg || bytes + b > kTownsMsgBytes)) { parts.push_back(std::vector<TownRow>()); bytes = 12; }
        parts.back().push_back(rows[i]);
        bytes += b;
    }
    for (size_t p = 0; p < parts.size(); ++p)
    {
        const unsigned f = (p == 0 ? kSayFirst : 0u) | (p + 1 < parts.size() ? kSayMore : 0u) | (whole != 0 ? kSayWhole : 0u);
        std::vector<char> m;
        if (!EncodeTowns(&m, kOpSay, (int)f, parts[p])) { msgs->clear(); return false; }
        msgs->push_back(m);
    }
    return true;
}
/* SHOW KEYS AS MESSAGES (the world server, after each SHOW): split as a SAY is - the first carries kSayFirst, every one but the last
   kSayMore; no rows: one message (the game then holds no keys). At most kMaxShowKeyRows rows. False = too many rows, a row not valid
   for SHOW KEYS or too big for a message alone (msgs then holds nothing) */
const size_t kMaxShowKeyRows = 256;   /* = baseshow::kMaxShowRows: one key row per shown outpost */
inline bool EncodeShowKeys(std::vector<std::vector<char> >* msgs, const std::vector<TownRow>& rows)
{
    msgs->clear();
    if (rows.size() > kMaxShowKeyRows) return false;
    std::vector<std::vector<TownRow> > parts(1);
    size_t bytes = 12;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const size_t b = TownBytes(rows[i]);
        if (!RowOk(rows[i], kOpShowKeys) || 12 + b > kTownsMsgBytes) return false;
        if (!parts.back().empty() && (parts.back().size() >= kMaxTownsMsg || bytes + b > kTownsMsgBytes)) { parts.push_back(std::vector<TownRow>()); bytes = 12; }
        parts.back().push_back(rows[i]);
        bytes += b;
    }
    for (size_t p = 0; p < parts.size(); ++p)
    {
        const unsigned f = (p == 0 ? kSayFirst : 0u) | (p + 1 < parts.size() ? kSayMore : 0u);
        std::vector<char> m;
        if (!EncodeTowns(&m, kOpShowKeys, (int)f, parts[p])) { msgs->clear(); return false; }
        msgs->push_back(m);
    }
    return true;
}
/* THE WORLD SERVER'S SIDE OF A SPLIT SAY, one per link: kSayFirst starts it again, each message's rows are added (at most
   kMaxTownsPerSlot; past it the SAY is taken as not read whole), and the message without kSayMore ends it */
struct SayAssembly
{
    std::vector<TownRow> rows;
    int open, over;
    SayAssembly() : open(0), over(0) {}
};
const int kSayPart = 0, kSayDone = 1, kSayOrphan = 2;
/* kSayDone: *done holds the whole SAY's rows and *whole whether its town list was read whole; kSayPart: more messages follow;
   kSayOrphan: a message that does not start a SAY while none is open (nothing taken) */
inline int SayTake(SayAssembly* a, unsigned flags, const std::vector<TownRow>& rows, std::vector<TownRow>* done, int* whole)
{
    if ((flags & kSayFirst) != 0) { a->rows.clear(); a->open = 1; a->over = 0; }
    else if (a->open == 0) return kSayOrphan;
    for (size_t i = 0; i < rows.size(); ++i) { if (a->rows.size() < kMaxTownsPerSlot) a->rows.push_back(rows[i]); else a->over = 1; }
    if ((flags & kSayMore) != 0) return kSayPart;
    done->swap(a->rows);
    a->rows.clear();
    *whole = ((flags & kSayWhole) != 0 && a->over == 0) ? 1 : 0;
    a->open = 0; a->over = 0;
    return kSayDone;
}
/* THE GAME'S SIDE OF SPLIT SHOW KEYS: kSayFirst starts the list again, each message's rows are added, the message without kSayMore
   ends it. kSayDone: *done holds the whole list; kSayPart: more follow; kSayOrphan: a message that does not start a list while none is
   open, or a list past kMaxShowKeyRows rows (nothing taken: the keys held stay) */
struct ShowKeysAssembly
{
    std::vector<TownRow> rows;
    int open, over;
    ShowKeysAssembly() : open(0), over(0) {}
};
inline int ShowKeysTake(ShowKeysAssembly* a, unsigned flags, const std::vector<TownRow>& rows, std::vector<TownRow>* done)
{
    if ((flags & kSayFirst) != 0) { a->rows.clear(); a->open = 1; a->over = 0; }
    else if (a->open == 0) return kSayOrphan;
    for (size_t i = 0; i < rows.size(); ++i) { if (a->rows.size() < kMaxShowKeyRows) a->rows.push_back(rows[i]); else a->over = 1; }
    if ((flags & kSayMore) != 0) return kSayPart;
    const int over = a->over;
    if (over == 0) done->swap(a->rows);
    a->rows.clear();
    a->open = 0; a->over = 0;
    return over == 0 ? kSayDone : kSayOrphan;
}

/* ---- the files ---- */
inline std::string EncodeTownsFile(const char* header, const std::vector<TownRow>& rows, bool withKeys)
{
    std::string o = std::string(header) + "\t" + zonekeep::ZkNum(kTownsFormat) + "\n";
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const TownRow& t = rows[i];
        o += "T\t" + zonekeep::ZkNum(t.owner);
        for (int j = 0; j < 5; ++j) o += "\t" + zonekeep::ZkNum(t.hand[j]);
        for (int j = 0; j < 3; ++j) o += "\t" + zonekeep::FloatHex(t.pos[j]);
        o += "\t" + zonekeep::Esc(t.name) + "\t" + zonekeep::ZkNum(t.cut) + "\t" + zonekeep::ZkNum(t.level) + "\n";
        if (withKeys) for (size_t k = 0; k < t.keys.size(); ++k) o += "K\t" + zonekeep::Esc(t.keys[k]) + "\n";
    }
    return o;
}
/* The rows back, of format kTownsFormat or kTownsFormatKeyless. False = not a list this build reads (*why names the line); *out then
   holds nothing. withKeys: K lines allowed; every row must be valid as a LIST row. */
inline bool DecodeTownsFile(const std::string& text, const char* header, bool withKeys, std::vector<TownRow>* out, std::string* why)
{
    out->clear();
    int fmt = kTownsFormat;
    std::vector<TownRow> rows;
    std::vector<std::string> f;
    size_t at = 0;
    int lineNo = 0;
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
            if (f.size() != 2 || f[0] != header || !zonekeep::DecInt(f[1], &v) || (v != kTownsFormat && v != kTownsFormatCut && v != kTownsFormatKeyless))
            { *why = where + "not a " + header + " list of format " + zonekeep::ZkNum(kTownsFormatKeyless) + ", " + zonekeep::ZkNum(kTownsFormatCut) + " or " + zonekeep::ZkNum(kTownsFormat); return false; }
            fmt = v;
            continue;
        }
        if (line.empty()) continue;
        if (f[0] == "T")
        {
            TownRow t;
            bool ok = f.size() == (fmt == kTownsFormatKeyless ? 11u : fmt == kTownsFormatCut ? 12u : 13u) && zonekeep::DecInt(f[1], &t.owner);
            for (int j = 0; ok && j < 5; ++j) ok = zonekeep::DecInt(f[(size_t)(2 + j)], &t.hand[j]);
            for (int j = 0; ok && j < 3; ++j) ok = zonekeep::HexFloat(f[(size_t)(7 + j)], &t.pos[j]);
            if (ok && fmt != kTownsFormatKeyless) ok = zonekeep::DecInt(f[11], &t.cut);
            if (ok && fmt == kTownsFormat) ok = zonekeep::DecInt(f[12], &t.level);
            ok = ok && zonekeep::Unesc(f[10], &t.name) && RowOk(t, kOpList);
            if (!ok) { *why = where + "an outpost line this build cannot read"; return false; }
            rows.push_back(t);
        }
        else if (f[0] == "K")
        {
            std::string k;
            bool ok = withKeys && !rows.empty() && f.size() == 2 && zonekeep::Unesc(f[1], &k);
            if (ok) { rows.back().keys.push_back(k); ok = RowOk(rows.back(), kOpList); }
            if (!ok) { *why = where + "a kept piece line this build cannot read (or one with no outpost line before it, or twice)"; return false; }
        }
        else { *why = where + "a line of a kind this build does not know"; return false; }
    }
    if (lineNo == 0) { *why = "the list is empty (no header line)"; return false; }
    out->swap(rows);
    return true;
}

/* ---- the decisions ---- */
/* A KEY'S SPOT, read once (poskey.h ParseKeySpot); ok 0 = not a position key (it then matches by its text only) */
struct TkSpot
{
    int ok, sx, sy;
    long x10, z10;
    char sid[poskey::kSidRoom];
    TkSpot() : ok(0), sx(0), sy(0), x10(0), z10(0) { sid[0] = 0; }
};
inline TkSpot SpotOfKey(const std::string& k)
{
    TkSpot s;
    s.ok = poskey::ParseKeySpot(k.c_str(), s.sid, (int)sizeof(s.sid), &s.sx, &s.sy, &s.x10, &s.z10);
    return s;
}
/* ONE PIECE, the project's same-piece rule: the same text, the two texts of one key (poskey.h KeysSamePiece), or the same record in
   the same sector within kTwinDrift tenths of a unit on x and z (SameSpotSameRecord) */
inline int KeySpotsSame(const std::string& a, const TkSpot& sa, const std::string& b, const TkSpot& sb)
{
    if (poskey::KeysSamePiece(a.c_str(), b.c_str()) != 0) return 1;
    if (sa.ok == 0 || sb.ok == 0) return 0;
    return poskey::SameSpotSameRecord(sa.sid, sa.sx, sa.sy, sa.x10, sa.z10, sb.sid, sb.sx, sb.sy, sb.x10, sb.z10, poskey::kTwinDrift);
}
inline int TownKeysSame(const std::string& a, const std::string& b) { return KeySpotsSame(a, SpotOfKey(a), b, SpotOfKey(b)); }
/* KEYS OF THEIR OWNERS, looked up by the same-piece rule: Add(owner, key, tag); Find gives the lowest tag of a key of that owner that
   is the same piece (1 = found; tag may be 0) */
class TownKeyFinder
{
public:
    void Add(int owner, const std::string& key, size_t tag)
    {
        E e;
        e.owner = owner; e.key = key; e.spot = SpotOfKey(key); e.tag = tag;
        const size_t at = all_.size();
        all_.push_back(e);
        byText_[key].push_back(at);
        if (e.spot.ok != 0) bySector_[Sector(owner, e.spot)].push_back(at);
    }
    int Find(int owner, const std::string& key, size_t* tag) const
    {
        int found = 0;
        size_t best = 0;
        std::map<std::string, std::vector<size_t> >::const_iterator t = byText_.find(key);
        if (t != byText_.end())
            for (size_t i = 0; i < t->second.size(); ++i)
            {
                const E& e = all_[t->second[i]];
                if (e.owner == owner && (found == 0 || e.tag < best)) { best = e.tag; found = 1; }
            }
        const TkSpot s = SpotOfKey(key);
        if (s.ok != 0)
        {
            std::map<SectorKey, std::vector<size_t> >::const_iterator b = bySector_.find(Sector(owner, s));
            if (b != bySector_.end())
                for (size_t i = 0; i < b->second.size(); ++i)
                {
                    const E& e = all_[b->second[i]];
                    if ((found == 0 || e.tag < best) && KeySpotsSame(key, s, e.key, e.spot) != 0) { best = e.tag; found = 1; }
                }
        }
        if (found != 0 && tag != 0) *tag = best;
        return found;
    }
private:
    struct E { int owner; std::string key; TkSpot spot; size_t tag; };
    typedef std::pair<int, std::pair<int, int> > SectorKey;
    static SectorKey Sector(int owner, const TkSpot& s) { return std::make_pair(owner, std::make_pair(s.sx, s.sy)); }
    std::vector<E> all_;
    std::map<std::string, std::vector<size_t> > byText_;
    std::map<SectorKey, std::vector<size_t> > bySector_;
};
/* a town standing at (livePos) is the kept outpost t (its faction is checked by the caller) */
inline int TownStands(const float* livePos, const TownRow& t) { return keptwire::NearXZ(livePos[0], livePos[2], t.pos[0], t.pos[2], kTownSameRadius); }
/* A SAY from `slot` (whole: its game read its whole town list): that slot's rows become the said ones (owner set to the slot, at most
   kMaxTownsPerSlot). A said row is the slot's row on file whose centre it stands at (TownStands) or that names one of its pieces
   (TownKeysSame); a said row whose list is cut takes that row's keys it does not name (up to kMaxTownKeys), so keys the game could
   not say are not dropped. A SAY not read whole keeps the slot's rows it does not name. Every other slot's rows stay as they are, in
   their order. */
inline void ReplaceSlotTowns(std::vector<TownRow>* all, int slot, const std::vector<TownRow>& said, int whole = 1)
{
    std::vector<TownRow> o, old;
    for (size_t i = 0; i < all->size(); ++i) { if ((*all)[i].owner != slot) o.push_back((*all)[i]); else old.push_back((*all)[i]); }
    TownKeyFinder oldKeys;
    for (size_t j = 0; j < old.size(); ++j) for (size_t k = 0; k < old[j].keys.size(); ++k) oldKeys.Add(slot, old[j].keys[k], j);
    std::vector<int> used(old.size(), 0);
    size_t mine = 0;
    for (size_t i = 0; i < said.size() && mine < kMaxTownsPerSlot; ++i)
    {
        TownRow t = said[i];
        t.owner = slot;
        int at = -1;
        for (size_t j = 0; j < old.size() && at < 0; ++j) if (used[j] == 0 && TownStands(t.pos, old[j]) != 0) at = (int)j;
        for (size_t k = 0; k < t.keys.size() && at < 0; ++k) { size_t j = 0; if (oldKeys.Find(slot, t.keys[k], &j) != 0 && used[j] == 0) at = (int)j; }
        if (at >= 0)
        {
            used[(size_t)at] = 1;
            if (t.cut != 0)
            {
                TownKeyFinder now;
                for (size_t k = 0; k < t.keys.size(); ++k) now.Add(slot, t.keys[k], k);
                const std::vector<std::string>& ok = old[(size_t)at].keys;
                for (size_t k = 0; k < ok.size() && t.keys.size() < kMaxTownKeys; ++k)
                    if (now.Find(slot, ok[k], 0) == 0) { now.Add(slot, ok[k], t.keys.size()); t.keys.push_back(ok[k]); }
            }
        }
        o.push_back(t);
        ++mine;
    }
    if (whole == 0)
        for (size_t j = 0; j < old.size() && mine < kMaxTownsPerSlot; ++j) if (used[j] == 0) { o.push_back(old[j]); ++mine; }
    all->swap(o);
}
struct KeptTownStats
{
    /* byKey: kept pieces a said key named; piecesLeft: kept pieces of a player no said key named (piecesLeftTown of them name a town in
       their area record); saidCut: said outposts whose key list was cut */
    int towns, keys, byKey, piecesLeft, piecesLeftTown, saidUnused, keysOver, saidCut;
    KeptTownStats() : towns(0), keys(0), byKey(0), piecesLeft(0), piecesLeftTown(0), saidUnused(0), keysOver(0), saidCut(0) {}
};
/* THE RESET'S MATCH: each kept piece of a player goes to the first said outpost of that player one of whose keys is the same piece
   (TownKeysSame); the said outposts come out each with the keys of its kept pieces (sorted, each once, at most kMaxTownKeys), and said
   outposts no kept piece went to are left out. A town handle never matches (a player town's handle as its game reads it is not the
   handle its pieces' area records hold), nor does distance: a piece no said key names (a bought house, a piece its game never said)
   gets no outpost here (NearOutpostsTake and OutpostsFromPieces come after it). */
inline void KeptTownsOf(const std::vector<TownRow>& said, const std::vector<zonekeep::KeptPiece>& pieces, std::vector<TownRow>* out, KeptTownStats* st)
{
    *st = KeptTownStats();
    std::vector<TownRow> rows(said);
    TownKeyFinder f;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        for (size_t k = 0; k < rows[i].keys.size(); ++k) f.Add(rows[i].owner, rows[i].keys[k], i);
        if (rows[i].cut != 0) ++st->saidCut;
        rows[i].keys.clear();
        rows[i].cut = 0;
    }
    for (size_t i = 0; i < pieces.size(); ++i)
    {
        const zonekeep::KeptPiece& p = pieces[i];
        if (p.ownerSlot < 0) continue;
        size_t at = 0;
        if (f.Find(p.ownerSlot, p.key, &at) == 0) { ++st->piecesLeft; if (p.hasTown != 0) ++st->piecesLeftTown; continue; }
        ++st->byKey;
        rows[at].keys.push_back(p.key);
    }
    out->clear();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        TownRow t = rows[i];
        std::sort(t.keys.begin(), t.keys.end());
        t.keys.erase(std::unique(t.keys.begin(), t.keys.end()), t.keys.end());
        if (t.keys.empty()) { ++st->saidUnused; continue; }
        if (t.keys.size() > kMaxTownKeys) { st->keysOver += (int)(t.keys.size() - kMaxTownKeys); t.keys.resize(kMaxTownKeys); }
        if (!RowOk(t, kOpList)) { ++st->saidUnused; continue; }
        st->keys += (int)t.keys.size();
        ++st->towns;
        out->push_back(t);
    }
}
/* each kept piece's key -> its outpost's place in rows (the first outpost naming it) */
inline std::map<std::string, size_t> KeyIndex(const std::vector<TownRow>& rows)
{
    std::map<std::string, size_t> m;
    for (size_t i = 0; i < rows.size(); ++i) for (size_t k = 0; k < rows[i].keys.size(); ++k) m.insert(std::make_pair(rows[i].keys[k], i));
    return m;
}
/* THE LAY STEP for a kept piece of an outpost, before the piece is laid: the town this world already holds for it (still in the
   player-town list with the owner's faction), else one of the owner's standing at its centre, else a new one when founding is possible */
const int kStepNone = 0, kStepCached = 1, kStepStanding = 2, kStepFound = 3;
inline int TownLayStep(int cachedOk, int standing, int mayFound)
{
    if (cachedOk != 0) return kStepCached;
    if (standing != 0) return kStepStanding;
    return mayFound != 0 ? kStepFound : kStepNone;
}
/* owner 938: THE TEAM GATE of a kept outpost after a RESET WORLD - a game takes, founds or links a town for it only when its owner is
   this game's own player or a teammate on this game's copy of the team table (as a new outpost is shown only to teammates). Without a
   table yet the outpost WAITS (not settled: looked at again once the table comes); a non-teammate's outpost gets no town (its pieces
   are laid as they are) and is not settled either, so a later join gives it its town. ownFaction = the piece's faction is this game's
   own player's (an own outpost whatever the slots say); else mySlot < 0 or owner < 0: wait. */
const int kTeamGateTown = 0, kTeamGateWait = 1, kTeamGateNotMate = 2;
inline int KeptTownTeamGate(int owner, int mySlot, int ownFaction, int tableKnown, int sameTeam)
{
    if (ownFaction != 0) return kTeamGateTown;
    if (owner < 0 || mySlot < 0) return kTeamGateWait;
    if (owner == mySlot) return kTeamGateTown;
    if (tableKnown == 0) return kTeamGateWait;
    return sameTeam != 0 ? kTeamGateTown : kTeamGateNotMate;
}

const int kEmptyWholeToClear = 3;   /* SAYs in a row on one link, each with no outposts and read whole, before a player's outposts on file are cleared */
/* the run of SAYs in a row with no outposts and the town list read whole, after one more SAY: any other SAY ends it (0); the world
   server keeps it per link and starts a new link at 0 */
inline int EmptyWholeRun(int run, size_t saidCount, int whole)
{
    if (saidCount != 0 || whole == 0) return 0;
    return run < kEmptyWholeToClear ? run + 1 : kEmptyWholeToClear;
}
/* A SAY WITH NO OUTPOSTS while `slot` has rows on file is not taken (1 = ignore it: a game whose town list is not read yet or is still
   loading says none) until it is the kEmptyWholeToClear-th of a run (emptyWholeRun, EmptyWholeRun); then, or with outposts, it is
   taken (ReplaceSlotTowns), so a player who gave up every outpost clears them */
inline int EmptySayIgnored(const std::vector<TownRow>& all, int slot, size_t saidCount, int emptyWholeRun = 0)
{
    if (saidCount != 0 || emptyWholeRun >= kEmptyWholeToClear) return 0;
    for (size_t i = 0; i < all.size(); ++i) if (all[i].owner == slot) return 1;
    return 0;
}
/* THE SAID OUTPOSTS A RESET MATCHES AGAINST: player-towns.txt's rows, then for each owner with no row there, that owner's outposts the
   world's own earlier reset carried (oldKept, its kept-towns.txt), with their keys (at most kMaxTownsPerSlot an owner).
   *added = the rows taken from oldKept. */
inline std::vector<TownRow> SaidWithCarried(const std::vector<TownRow>& said, const std::vector<TownRow>& oldKept, int* added)
{
    std::vector<TownRow> o(said);
    std::set<int> saidOwners;
    for (size_t i = 0; i < said.size(); ++i) saidOwners.insert(said[i].owner);
    std::map<int, size_t> per;
    int n = 0;
    for (size_t i = 0; i < oldKept.size(); ++i)
    {
        const TownRow& t = oldKept[i];
        if (t.owner < 0 || saidOwners.count(t.owner) != 0 || per[t.owner] >= kMaxTownsPerSlot) continue;
        TownRow c(t);
        if (!RowOk(c, kOpList)) continue;
        o.push_back(c);
        ++per[t.owner];
        ++n;
    }
    if (added != 0) *added = n;
    return o;
}
struct CarryStats
{
    int keys, joined, rows, over;   /* keys carried on; old outposts joined to an outpost matched now; carried as they were; keys past kMaxTownKeys */
    CarryStats() : keys(0), joined(0), rows(0), over(0) {}
};
/* AFTER THE MATCH (KeptTownsOf wrote out): each outpost the world's earlier reset carried (oldKept) carries on the kept pieces of its
   own owner its keys name (TownKeysSame; the piece's own key is written) that no outpost in out names - an absent player's outpost
   and its unfinished carried pieces survive a second reset. They join the outpost of out of the same owner with the same handle (an
   old outpost with no handle - one made from kept pieces - joins by its centre alone) or standing at its centre (TownStands), else the old outpost is added to out as it was, with those keys. A key is never named twice. */
inline void CarryKeptKeys(const std::vector<TownRow>& oldKept, const std::vector<zonekeep::KeptPiece>& pieces, std::vector<TownRow>* out, CarryStats* st)
{
    *st = CarryStats();
    TownKeyFinder piecesOf;
    for (size_t i = 0; i < pieces.size(); ++i) if (pieces[i].ownerSlot >= 0) piecesOf.Add(pieces[i].ownerSlot, pieces[i].key, i);
    std::set<std::string> taken;
    for (size_t i = 0; i < out->size(); ++i) for (size_t k = 0; k < (*out)[i].keys.size(); ++k) taken.insert((*out)[i].keys[k]);
    for (size_t i = 0; i < oldKept.size(); ++i)
    {
        const TownRow& o = oldKept[i];
        std::vector<std::string> carry;
        for (size_t k = 0; k < o.keys.size(); ++k)
        {
            size_t pi = 0;
            if (piecesOf.Find(o.owner, o.keys[k], &pi) == 0) continue;
            const std::string& pk = pieces[pi].key;
            if (taken.count(pk) != 0) continue;
            carry.push_back(pk);
            taken.insert(pk);
        }
        if (carry.empty()) continue;
        int at = -1;
        for (size_t j = 0; j < out->size() && at < 0; ++j)
            if ((*out)[j].owner == o.owner && ((HandKnown(o.hand) && HandSame((*out)[j].hand, o.hand)) || TownStands(o.pos, (*out)[j]) != 0)) at = (int)j;
        if (at >= 0) ++st->joined;
        else
        {
            TownRow t(o);
            t.keys.clear();
            t.cut = 0;
            out->push_back(t);
            at = (int)out->size() - 1;
            ++st->rows;
        }
        TownRow& t = (*out)[(size_t)at];
        for (size_t k = 0; k < carry.size(); ++k)
        {
            if (t.keys.size() >= kMaxTownKeys) { ++st->over; continue; }
            t.keys.push_back(carry[k]);
            ++st->keys;
        }
        std::sort(t.keys.begin(), t.keys.end());
    }
}
/* the keys the outposts in out name */
inline std::set<std::string> OutpostKeys(const std::vector<TownRow>& out)
{
    std::set<std::string> s;
    for (size_t i = 0; i < out.size(); ++i) for (size_t k = 0; k < out[i].keys.size(); ++k) s.insert(out[i].keys[k]);
    return s;
}
inline float DistSqXZ(const float* a, const float* b) { const float dx = a[0] - b[0], dz = a[2] - b[2]; return dx * dx + dz * dz; }
/* THE WORLD TOWNS A RESET SPARES: the towns buildings no player owns name (named: zonekeep::KeepStats::worldTowns over every area
   record read), less every town a player's game said it holds (said, by its handle) - a player's own outpost is never one */
inline std::set<std::string> WorldTownsOf(const std::set<std::string>& named, const std::vector<TownRow>& said)
{
    std::set<std::string> s(named);
    for (size_t i = 0; i < said.size(); ++i) if (HandKnown(said[i].hand)) s.erase(zonekeep::TownHandText(said[i].hand));
    return s;
}
/* each kept piece's key -> its place in pieces (the first piece with that key) */
inline std::map<std::string, size_t> PieceIndex(const std::vector<zonekeep::KeptPiece>& pieces)
{
    std::map<std::string, size_t> m;
    for (size_t i = 0; i < pieces.size(); ++i) m.insert(std::make_pair(pieces[i].key, i));
    return m;
}
/* 1 = a kept piece stands in a world town (worldTowns, WorldTownsOf): its area record names that town, or it is furniture whose
   building (a kept piece, found through byKey) names it - a bought house and the furniture in it. Furniture takes its host's town
   only: the engine copies the host's handle words into furniture as it is made, so after the host is linked to an outpost the
   furniture's own words can still name the old town. */
inline int InWorldTown(const zonekeep::KeptPiece& k, const std::vector<zonekeep::KeptPiece>& pieces, const std::map<std::string, size_t>& byKey,
                       const std::set<std::string>& worldTowns)
{
    if (worldTowns.empty()) return 0;
    if (k.hostKey.empty()) return (k.hasTown != 0 && worldTowns.count(zonekeep::TownHandText(k.townHand)) != 0) ? 1 : 0;
    const std::map<std::string, size_t>::const_iterator h = byKey.find(k.hostKey);
    if (h == byKey.end()) return 0;
    const zonekeep::KeptPiece& b = pieces[h->second];
    return (b.hasTown != 0 && worldTowns.count(zonekeep::TownHandText(b.townHand)) != 0) ? 1 : 0;
}
/* kept pieces' places in pieces, ordered by their keys */
struct PieceKeyLess
{
    const std::vector<zonekeep::KeptPiece>* p;
    explicit PieceKeyLess(const std::vector<zonekeep::KeptPiece>* v) : p(v) {}
    bool operator()(size_t a, size_t b) const { return (*p)[a].key < (*p)[b].key; }
};
struct NearStats
{
    int took;   /* kept pieces no key named taken by their owner's nearest said outpost within kOutpostTakeRadius of its centre */
    int rows;   /* said outposts no kept piece went to by key, carried for the pieces they took here */
    int over;   /* pieces past kMaxTownKeys of the outpost that reached them: left to OutpostsFromPieces (overKeys) */
    std::set<std::string> overKeys;   /* those pieces' keys */
    NearStats() : took(0), rows(0), over(0) {}
};
/* AFTER THE MATCH AND THE CARRY (KeptTownsOf, CarryKeptKeys wrote out): each kept piece of a player that no outpost in out names and
   that stands within kOutpostTakeRadius (x, z) of the centre of a said outpost of its own owner goes to the nearest such outpost -
   whether its key list is empty (its game said it while none of its buildings were loaded there), cut, or whole (a piece built after
   the last say, or one an older reset gave a world town's outpost). A said outpost not in out (no kept piece went to it by key) is
   added to out, with no keys of its own, when it takes a piece here. Another owner's piece and a piece in a world town (InWorldTown)
   are never taken; a piece past its outpost's kMaxTownKeys is left to OutpostsFromPieces (st->overKeys). */
inline void NearOutpostsTake(const std::vector<TownRow>& said, const std::vector<zonekeep::KeptPiece>& pieces, const std::set<std::string>& worldTowns,
                             std::vector<TownRow>* out, NearStats* st)
{
    *st = NearStats();
    std::set<std::string> taken = OutpostKeys(*out);
    const std::map<std::string, size_t> byKey = PieceIndex(pieces);
    std::vector<int> at(said.size(), -2);   /* -2: takes nothing here; -1: takes, not in out yet; else its place in out */
    for (size_t i = 0; i < said.size(); ++i)
    {
        const TownRow& s = said[i];
        if (s.owner < 0 || s.owner > kOwnerMax) continue;
        at[i] = -1;
        for (size_t j = 0; j < out->size() && at[i] < 0; ++j)
        {
            const TownRow& o = (*out)[j];
            if (o.owner == s.owner && o.name == s.name && HandSame(o.hand, s.hand) && o.pos[0] == s.pos[0] && o.pos[1] == s.pos[1] && o.pos[2] == s.pos[2]) at[i] = (int)j;
        }
    }
    std::set<size_t> grown;
    for (size_t p = 0; p < pieces.size(); ++p)
    {
        const zonekeep::KeptPiece& k = pieces[p];
        if (k.ownerSlot < 0 || taken.count(k.key) != 0 || !keptwire::TextOk(k.key, keptwire::kKeyMax, false)) continue;
        if (InWorldTown(k, pieces, byKey, worldTowns) != 0) continue;
        int best = -1;
        float bestD = 0.0f;
        for (size_t i = 0; i < said.size(); ++i)
        {
            if (at[i] == -2 || said[i].owner != k.ownerSlot || keptwire::NearXZ(k.pos[0], k.pos[2], said[i].pos[0], said[i].pos[2], kOutpostTakeRadius) == 0) continue;
            const float d = DistSqXZ(k.pos, said[i].pos);
            if (best < 0 || d < bestD) { best = (int)i; bestD = d; }
        }
        if (best < 0) continue;
        const size_t b = (size_t)best;
        if (at[b] == -1)
        {
            TownRow t(said[b]);
            t.keys.clear();
            t.cut = 0;
            if (!RowOk(t, kOpList)) { at[b] = -2; continue; }
            out->push_back(t);
            at[b] = (int)out->size() - 1;
            ++st->rows;
        }
        TownRow& t = (*out)[(size_t)at[b]];
        if (t.keys.size() >= kMaxTownKeys) { ++st->over; st->overKeys.insert(k.key); continue; }
        t.keys.push_back(k.key);
        taken.insert(k.key);
        grown.insert((size_t)at[b]);
        ++st->took;
    }
    for (std::set<size_t>::const_iterator g = grown.begin(); g != grown.end(); ++g) std::sort((*out)[*g].keys.begin(), (*out)[*g].keys.end());
}
struct MadeStats
{
    int pieces;      /* kept pieces in the outposts made */
    int nearOther;   /* kept pieces no outpost names within kOutpostTakeRadius of an outpost of their owner that did not take them: no outpost */
    int split;       /* outposts made past the first of one group: a group of more than kMaxTownKeys pieces goes on in a further outpost */
    int refused;     /* pieces whose owner slot or key a LIST row cannot carry: no outpost */
    int worldTown;   /* kept pieces no outpost names in a world town (InWorldTown): no outpost, as the game left them */
    std::vector<size_t> made;   /* the made outposts' places in out */
    MadeStats() : pieces(0), nearOther(0), split(0), refused(0), worldTown(0) {}
};
/* one group of kept pieces OutpostsFromPieces makes outposts from: its owner, the town its pieces name (hasTown, hand), its pieces */
struct MadeGroup
{
    int owner, hasTown;
    int hand[5];
    std::vector<size_t> m;
    MadeGroup() : owner(-1), hasTown(0) { for (int i = 0; i < 5; ++i) hand[i] = 0; }
};
/* LAST: the kept pieces of a player that no outpost in out names, that stand in no world town (InWorldTown) and that stand within
   kOutpostTakeRadius of no outpost of their owner (said, or in out; a piece NearOutpostsTake left past an outpost's kMaxTownKeys,
   nearLeft, is not held back by that) get outposts made from them - for an owner whose game never said the outpost, or said it while
   its buildings were far away. The pieces go in key order, those whose area record names a town first, then the rest. A piece joins
   every group of its owner with a member within kOutpostTakeRadius of it - naming a town, only groups of that town; naming none, the
   group of its nearest such member and each other one whose town agrees with it - and the groups it joins become one, so a base strung
   out in a line is one group whatever order its pieces come in. Each group gives one outpost added to out per kMaxTownKeys of its
   pieces (in key order): its owner, no name, no handle, the mean of those pieces' positions as its centre, their keys sorted. */
inline void OutpostsFromPieces(const std::vector<TownRow>& said, const std::vector<zonekeep::KeptPiece>& pieces, const std::set<std::string>& worldTowns,
                               const std::set<std::string>& nearLeft, std::vector<TownRow>* out, MadeStats* st)
{
    *st = MadeStats();
    const std::set<std::string> taken = OutpostKeys(*out);
    const std::map<std::string, size_t> byKey = PieceIndex(pieces);
    std::vector<size_t> withTown, noTown;
    std::set<std::string> seen;
    for (size_t p = 0; p < pieces.size(); ++p)
    {
        const zonekeep::KeptPiece& k = pieces[p];
        if (k.ownerSlot < 0 || taken.count(k.key) != 0) continue;
        if (k.ownerSlot > kOwnerMax || !keptwire::TextOk(k.key, keptwire::kKeyMax, false)) { ++st->refused; continue; }
        if (!seen.insert(k.key).second) continue;
        if (InWorldTown(k, pieces, byKey, worldTowns) != 0) { ++st->worldTown; continue; }
        if (nearLeft.count(k.key) == 0)
        {
            int inReach = 0;
            for (size_t i = 0; i < said.size() && inReach == 0; ++i)
                if (said[i].owner == k.ownerSlot && keptwire::NearXZ(k.pos[0], k.pos[2], said[i].pos[0], said[i].pos[2], kOutpostTakeRadius) != 0) inReach = 1;
            for (size_t i = 0; i < out->size() && inReach == 0; ++i)
                if ((*out)[i].owner == k.ownerSlot && keptwire::NearXZ(k.pos[0], k.pos[2], (*out)[i].pos[0], (*out)[i].pos[2], kOutpostTakeRadius) != 0) inReach = 1;
            if (inReach != 0) { ++st->nearOther; continue; }
        }
        if (k.hasTown != 0) withTown.push_back(p); else noTown.push_back(p);
    }
    const PieceKeyLess byKeyText(&pieces);
    std::sort(withTown.begin(), withTown.end(), byKeyText);
    std::sort(noTown.begin(), noTown.end(), byKeyText);
    std::vector<MadeGroup> gs;   /* a group joined into another is left with no pieces */
    for (int pass = 0; pass < 2; ++pass)
    {
        const std::vector<size_t>& list = (pass == 0) ? withTown : noTown;
        for (size_t n = 0; n < list.size(); ++n)
        {
            const size_t pi = list[n];
            const zonekeep::KeptPiece& k = pieces[pi];
            std::vector<size_t> reach;   /* the groups with a member within kOutpostTakeRadius */
            int anchor = -1;
            float anchorD = 0.0f;
            for (size_t j = 0; j < gs.size(); ++j)
            {
                const MadeGroup& g = gs[j];
                if (g.m.empty() || g.owner != k.ownerSlot) continue;
                if (k.hasTown != 0 && (g.hasTown == 0 || !HandSame(g.hand, k.townHand))) continue;
                float d = -1.0f;
                for (size_t q = 0; q < g.m.size(); ++q)
                {
                    const zonekeep::KeptPiece& a = pieces[g.m[q]];
                    if (keptwire::NearXZ(k.pos[0], k.pos[2], a.pos[0], a.pos[2], kOutpostTakeRadius) == 0) continue;
                    const float e = DistSqXZ(k.pos, a.pos);
                    if (d < 0.0f || e < d) d = e;
                }
                if (d < 0.0f) continue;
                reach.push_back(j);
                if (anchor < 0 || d < anchorD) { anchor = (int)j; anchorD = d; }
            }
            if (anchor < 0)
            {
                MadeGroup g;
                g.owner = k.ownerSlot; g.hasTown = k.hasTown;
                for (int i = 0; i < 5; ++i) g.hand[i] = k.townHand[i];
                g.m.push_back(pi);
                gs.push_back(g);
                continue;
            }
            int hasTown = gs[(size_t)anchor].hasTown;
            int hand[5];
            for (int i = 0; i < 5; ++i) hand[i] = gs[(size_t)anchor].hand[i];
            std::vector<size_t> join(1, (size_t)anchor);
            for (size_t q = 0; q < reach.size(); ++q)
            {
                const MadeGroup& g = gs[reach[q]];
                if ((int)reach[q] == anchor) continue;
                if (g.hasTown != 0 && hasTown != 0 && !HandSame(g.hand, hand)) continue;
                if (g.hasTown != 0 && hasTown == 0) { hasTown = 1; for (int i = 0; i < 5; ++i) hand[i] = g.hand[i]; }
                join.push_back(reach[q]);
            }
            std::sort(join.begin(), join.end());
            MadeGroup& into = gs[join[0]];
            into.hasTown = hasTown;
            for (int i = 0; i < 5; ++i) into.hand[i] = hand[i];
            for (size_t q = 1; q < join.size(); ++q)
            {
                MadeGroup& from = gs[join[q]];
                into.m.insert(into.m.end(), from.m.begin(), from.m.end());
                from.m.clear();
            }
            into.m.push_back(pi);
        }
    }
    for (size_t j = 0; j < gs.size(); ++j)
    {
        std::vector<size_t> m(gs[j].m);
        if (m.empty()) continue;
        std::sort(m.begin(), m.end(), byKeyText);
        for (size_t from = 0; from < m.size(); from += kMaxTownKeys)
        {
            const size_t to = (m.size() - from > kMaxTownKeys) ? from + kMaxTownKeys : m.size();
            TownRow t;
            t.owner = gs[j].owner;
            double sx = 0.0, sy = 0.0, sz = 0.0;
            for (size_t n = from; n < to; ++n)
            {
                const zonekeep::KeptPiece& k = pieces[m[n]];
                sx += k.pos[0]; sy += k.pos[1]; sz += k.pos[2];
                t.keys.push_back(k.key);
            }
            const double c = (double)(to - from);
            t.pos[0] = (float)(sx / c); t.pos[1] = (float)(sy / c); t.pos[2] = (float)(sz / c);
            std::sort(t.keys.begin(), t.keys.end());
            if (!RowOk(t, kOpList)) { st->refused += (int)(to - from); continue; }
            out->push_back(t);
            st->made.push_back(out->size() - 1);
            st->pieces += (int)(to - from);
            if (from > 0) ++st->split;
        }
    }
}

}   /* namespace townkeep */
