#pragma once
/* THE PLAYERS' OUTPOSTS CARRIED INTO A RESET WORLD (TOWNS, world-server kind 70). Compiled into the world server (store_main.cpp), the
   game (build.cpp, store.cpp) and the offline suite (test_main.cpp). Pure: no disk, no clock, no engine. Header-only. No C++11.

   A player's outpost is a Town the game founds when the player places a building away from any town (0x4DA6F0 -> 0x933F40); each
   placed building is linked to it (Building +0x90) and its area record names that town by five handle words (townTYPE, townC, townCS,
   townI, townS). The world server holds no save of the games, so:
   - EACH GAME SAYS ITS OWN OUTPOSTS (SAY): name, centre and handle words of every player town of its player's faction, and the
     position keys of the registered pieces linked to it (Building +0x90 is that town). A player town's handle words as the game
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
     kept-towns.txt. A kept piece no said key names is laid with no outpost; a said outpost with no kept piece is not carried.
   - THE WORLD SERVER SENDS THE KEPT OUTPOSTS (LIST) to each game at its link's first AREAS report, before its first kept rows (an
     empty list too: the game drops any list it holds). Every game takes or founds the outpost (TownLayStep) for each kept piece of it
     loaded there and links the piece to it; a piece in a town that is not a player town stays in it (TownRelinkAllowed). A SAY with
     no outposts, while that slot has rows on file, is taken only as the kEmptyWholeToClear-th SAY in a row on one link with no
     outposts and its town list read whole (EmptyWholeRun, EmptySayIgnored); the game marks no SAY whole before its player has been
     in the world it loaded for a while.
   - TEAMMATES SEE EACH OTHER'S OUTPOSTS ON THE MAP (SAID, down): the world server sends every game the outposts every player said
     (player-towns.txt, without their keys) at its link's first AREAS report and again on every change to that file. Each game keeps
     the rows of its own teammates (MateRows; the team table it already holds) and takes or founds, for each, a town of that player's
     faction here at its centre, marked discovered, so a teammate's base is on this player's map (build.cpp BdTeamTownsTick). The
     filter is the game's: a team change shows or stops showing an outpost with no word from the world server.
   WIRE (little-endian, as every world-server message): u32 op (SAY 1 up, LIST 2 down, SAID 3 down), u32 flags, u32 count 0..kMaxTownsMsg,
   count x TOWN
     flags LIST and SAID: 1 = the list starts again with this message (0 or 1). SAY: kSayFirst (this message starts a SAY), kSayMore (more
           messages of this SAY follow), kSayWhole (the game read its whole town list); a SAY that would pass kTownsMsgBytes is
           split between its rows (EncodeTownsSay)
     TOWN  i32 owner slot (SAY: -1, the sender; SAID: the outpost's player), 5 x i32 handle words, f32 x, f32 y (height), f32 z, str name (0..128 bytes, no control
           characters), u32 keys 0..kMaxTownKeys (with kKeysCut set: the outpost has pieces the list does not name), keys x str
           (1..47 bytes, each once)
     str = u32 length, the bytes.
   FILES (player-towns.txt, kept-towns.txt): TAB-separated, LF line ends (a CR before it is read as nothing); line 1 "<header><TAB>2"
   (player-towns / kept-towns), then per outpost
     T <owner slot> <type> <container> <container stamp> <index> <serial> <x> <y> <z> <name> <cut 0 or 1>
     K <key>     (after its outpost's T line: one key of that outpost - a said piece in player-towns, a kept piece in kept-towns)
   Format 1 (no cut field) is still read. A float is the 8 lower-case hex digits of its IEEE bits, an int decimal, text escaped as
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
const unsigned kOpSay = 1, kOpList = 2, kOpSaid = 3;   /* SAID: every player's said outposts, down, for teammates' map markers */
const size_t kMaxTownsMsg = 32;        /* outposts in one TOWNS */
const size_t kMaxTownsPerSlot = 32;    /* outposts one player's SAY is kept with */
const size_t kMaxTownKeys = 512;       /* kept pieces one outpost row names */
const int kOwnerMax = (int)keptwire::kClaimMax - 1;   /* an owner slot: a claim field is slot + 1 */
const char* const kPlayerTownsFile = "player-towns.txt";
const char* const kPlayerTownsHeader = "player-towns";
const char* const kKeptTownsFile = "kept-towns.txt";
const char* const kKeptTownsHeader = "kept-towns";
const int kTownsFormat = 2;   /* the outpost list files' format this build writes */
const int kTownsFormatKeyless = 1;   /* the format before it (no cut field): still read */
const unsigned kSayFirst = 1, kSayMore = 2, kSayWhole = 4;   /* a SAY's flags word */
const unsigned kKeysCut = 0x80000000u;   /* on a TOWN's key count: the outpost has pieces its list does not name */
const unsigned kStageShift = 24;         /* on a TOWN's key count, bits 24-30: the outpost's map size stage (TownRow::stage) */
const int kStageMax = 3;                 /* Town+0x388: 0 none, 1 Village, 2 Outpost, 3 Town (the engine's own player-town icon) */
const size_t kTownsMsgBytes = 60000;   /* one TOWNS message at most: under the world-server road's 64 KiB (liveenvelope.h kLiveInnerMax) with room for its headers */
const float kTownSameRadius = 50.0f;    /* Guess: a town of the owner's faction whose centre is this close (x, z) to a kept outpost's centre is that outpost */

struct TownRow
{
    int owner;                       /* the player's slot; -1 in a SAY (the sender) */
    int hand[5];                     /* type, container, container stamp, index, serial */
    float pos[3];                    /* the centre: x, height, z */
    std::string name;
    std::vector<std::string> keys;   /* SAY and player-towns.txt: the pieces linked to the outpost; LIST and kept-towns.txt: its kept pieces */
    int cut;                         /* 1 = the outpost has pieces keys does not name (its game's list was cut, or not every piece could be read) */
    int stage;                       /* SAY and SAID: the outpost's map size stage as its owner's game counts it (0..kStageMax), so a teammate's map shows
                                        the icon the owner sees; 0 in LIST and in both files */
    TownRow() : owner(-1), cut(0), stage(0) { for (int i = 0; i < 5; ++i) hand[i] = 0; pos[0] = pos[1] = pos[2] = 0.0f; }
};

inline bool HandSame(const int* a, const int* b) { for (int i = 0; i < 5; ++i) if (a[i] != b[i]) return false; return true; }
/* a town name as the wire carries it: each control byte becomes '?', cut to 128 bytes */
inline std::string NameFit(const std::string& s)
{
    std::string o(s.size() > keptwire::kStrMax ? s.substr(0, keptwire::kStrMax) : s);
    for (size_t i = 0; i < o.size(); ++i) { const unsigned char c = (unsigned char)o[i]; if (c < 0x20 || c == 0x7F) o[i] = '?'; }
    return o;
}
/* a row the op may carry: SAY - owner -1; LIST, SAID (and both files) - a real owner slot; at most kMaxTownKeys keys, cut 0 or 1, every float
   in range, the name and each key valid, no key twice */
inline bool RowOk(const TownRow& t, unsigned op)
{
    if (op == kOpSay) { if (t.owner != -1) return false; }
    else if (op == kOpList || op == kOpSaid) { if (t.owner < 0 || t.owner > kOwnerMax) return false; }
    else return false;
    if (t.keys.size() > kMaxTownKeys || (t.cut != 0 && t.cut != 1) || t.stage < 0 || t.stage > kStageMax) return false;
    for (int j = 0; j < 3; ++j) if (!keptwire::FloatOk(t.pos[j])) return false;
    if (!keptwire::TextOk(t.name, keptwire::kStrMax, true)) return false;
    std::set<std::string> seen;
    for (size_t k = 0; k < t.keys.size(); ++k) if (!keptwire::TextOk(t.keys[k], keptwire::kKeyMax, false) || !seen.insert(t.keys[k]).second) return false;
    return true;
}

/* ---- the wire ---- */
/* the flags word: LIST and SAID 0 or 1; SAY any of kSayFirst, kSayMore, kSayWhole */
inline bool FlagsOk(unsigned op, unsigned f) { return op == kOpSay ? (f & ~(kSayFirst | kSayMore | kSayWhole)) == 0 : f <= 1; }
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
/* SAY, LIST or SAID: 0..kMaxTownsMsg rows, every one valid for the op (nothing written for a refused one) */
inline bool EncodeTowns(std::vector<char>* out, unsigned op, int first, const std::vector<TownRow>& rows)
{
    out->clear();
    if ((op != kOpSay && op != kOpList && op != kOpSaid) || first < 0 || !FlagsOk(op, (unsigned)first) || rows.size() > kMaxTownsMsg) return false;
    keptwire::PutU32(out, op); keptwire::PutU32(out, (unsigned)first); keptwire::PutU32(out, (unsigned)rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const TownRow& t = rows[i];
        if (!RowOk(t, op)) { out->clear(); return false; }
        keptwire::PutI32(out, t.owner);
        for (int j = 0; j < 5; ++j) keptwire::PutI32(out, t.hand[j]);
        for (int j = 0; j < 3; ++j) keptwire::PutF32(out, t.pos[j]);
        keptwire::PutStr(out, t.name);
        keptwire::PutU32(out, (unsigned)t.keys.size() | (t.cut != 0 ? kKeysCut : 0u) | ((unsigned)t.stage << kStageShift));
        for (size_t k = 0; k < t.keys.size(); ++k) keptwire::PutStr(out, t.keys[k]);
    }
    return true;
}
/* 1 = decoded (every row valid for its op, nothing trailing), 0 = malformed (nothing is taken from it) */
inline int DecodeTowns(const char* p, size_t n, unsigned* op, int* first, std::vector<TownRow>* rows)
{
    rows->clear(); *op = 0; *first = 0;
    TkReader r(p, n);
    const unsigned o = r.U32(), f = r.U32(), c = r.U32();
    if (r.bad || (o != kOpSay && o != kOpList && o != kOpSaid) || !FlagsOk(o, f) || c > kMaxTownsMsg) return 0;
    std::vector<TownRow> v;
    for (unsigned i = 0; i < c; ++i)
    {
        TownRow t;
        t.owner = r.I32();
        for (int j = 0; j < 5; ++j) t.hand[j] = r.I32();
        for (int j = 0; j < 3; ++j) t.pos[j] = r.F32();
        t.name = r.Str(keptwire::kStrMax, true);
        const unsigned nkw = r.U32(), nk = nkw & ((1u << kStageShift) - 1u);
        t.cut = (nkw & kKeysCut) != 0 ? 1 : 0;
        t.stage = (int)((nkw & ~kKeysCut) >> kStageShift);   /* above kStageMax: RowOk refuses the row */
        if (r.bad || nk > kMaxTownKeys || (size_t)nk > (r.n - r.at) / 5) return 0;   /* a key takes 5 bytes at least */
        for (unsigned k = 0; k < nk && !r.bad; ++k) t.keys.push_back(r.Str(keptwire::kKeyMax, false));
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
    size_t b = 4 + 20 + 12 + 4 + t.name.size() + 4;
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
        o += "\t" + zonekeep::Esc(t.name) + "\t" + zonekeep::ZkNum(t.cut) + "\n";
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
            if (f.size() != 2 || f[0] != header || !zonekeep::DecInt(f[1], &v) || (v != kTownsFormat && v != kTownsFormatKeyless))
            { *why = where + "not a " + header + " list of format " + zonekeep::ZkNum(kTownsFormatKeyless) + " or " + zonekeep::ZkNum(kTownsFormat); return false; }
            fmt = v;
            continue;
        }
        if (line.empty()) continue;
        if (f[0] == "T")
        {
            TownRow t;
            bool ok = f.size() == (fmt == kTownsFormatKeyless ? 11u : 12u) && zonekeep::DecInt(f[1], &t.owner);
            for (int j = 0; ok && j < 5; ++j) ok = zonekeep::DecInt(f[(size_t)(2 + j)], &t.hand[j]);
            for (int j = 0; ok && j < 3; ++j) ok = zonekeep::HexFloat(f[(size_t)(7 + j)], &t.pos[j]);
            if (ok && fmt != kTownsFormatKeyless) ok = zonekeep::DecInt(f[11], &t.cut);
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
/* SAID: the said outposts as the map needs them - keys dropped (a marker needs a name and a centre; a key list is up to kMaxTownKeys
   strings), cut cleared, every row valid for SAID (one that is not is left out) */
inline std::vector<TownRow> SaidForMap(const std::vector<TownRow>& said)
{
    std::vector<TownRow> o;
    for (size_t i = 0; i < said.size(); ++i)
    {
        TownRow t(said[i]);
        t.keys.clear(); t.cut = 0;
        if (RowOk(t, kOpSaid)) o.push_back(t);
    }
    return o;
}
/* the SAID rows this game shows: those whose player is one of its teammates - never its own (its own outposts are its own towns),
   never a slot that is not on its team */
inline std::vector<size_t> MateRows(const std::vector<TownRow>& said, const std::vector<int>& mates, int mySlot)
{
    std::vector<size_t> o;
    if (mySlot < 0) return o;
    for (size_t i = 0; i < said.size(); ++i)
        if (said[i].owner != mySlot && std::find(mates.begin(), mates.end(), said[i].owner) != mates.end()) o.push_back(i);
    return o;
}
/* a SAID row's name in this game: the same outpost the same text on every look (its player, its centre to a unit, its name) */
inline std::string SaidKey(const TownRow& t)
{
    return zonekeep::ZkNum(t.owner) + "|" + zonekeep::ZkNum((int)t.pos[0]) + "|" + zonekeep::ZkNum((int)t.pos[2]) + "|" + t.name;
}
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
   gets no outpost. */
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
   and its unfinished carried pieces survive a second reset. They join the outpost of out of the same owner with the same handle or
   standing at its centre (TownStands), else the old outpost is added to out as it was, with those keys. A key is never named twice. */
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
            if ((*out)[j].owner == o.owner && (HandSame((*out)[j].hand, o.hand) || TownStands(o.pos, (*out)[j]) != 0)) at = (int)j;
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
/* A KEPT PIECE'S TOWN LINK: 1 = it may be linked to its outpost's town (it names no town, or a player town); 0 = it stays as it is
   (a town that is not a player town - a bought or handed-back house's - or isPlayerTown -1: the player-town list could not be read) */
inline int TownRelinkAllowed(int hasTown, int isPlayerTown)
{
    if (hasTown == 0) return 1;
    return isPlayerTown == 1 ? 1 : 0;
}

}   /* namespace townkeep */
