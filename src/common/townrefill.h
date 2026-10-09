#pragma once
// AN EMPTY TOWN THAT NOTHING ACCOUNTS FOR IS REFILLED ONCE; A TOWN WITH A RECORDED LOSS STAYS WIPED.
// The pure half, ONE header compiled into the plugin (store.cpp, towngen.cpp), the world server (store_main.cpp) and the
// offline suite, so the three cannot hold different ideas of one row:
//  - the per-town row the world server keeps in town_losses.txt, its merge, its wire form and its file line;
//  - the optional town field a RECORD_GONE carries after its stamp (the deleting game names the group's town);
//  - the world stamp a REFILLED or a RECORD_GONE this game has not sent yet waits with (WorldStamp, SameWorld);
//  - the tally of a town's world records, the refill decision, and the building-by-building refill's rules (FillStart,
//    FillBuilding, FillAfterRun, FillWaitStep, TownFillDone, FillStopDone, FillSkipFilled).
//
// A ROW, keyed by the town's FCS stringID:
//   losses    groups of the town the world server deleted - it only grows
//   refilled  1 once the game holding the town refilled its buildings (each building's residents made as Kenshi first made
//             them) - it is never cleared
// WIRE (TOWN_LOSS), little-endian as every world-server message:
//   down ROWS     {u32 op 1, u32 count 1..256, count x [u32 sidLen 1..128, sid bytes, u32 losses, u32 refilled]}
//                 every row at WELCOME (inside the opening push), each changed row to every connected game after
//   up   REFILLED {u32 op 2, u32 sidLen 1..128, sid bytes} - this game refilled the town's buildings
// RECORD_GONE: {str worldId, u32 stamp lo, u32 stamp hi} then, optionally, {u32 townLen 1..128, town bytes}. A reader that
//   stops after the stamp reads exactly what it read before.
// FILE LINE: v1<TAB>sid<TAB>losses<TAB>refilled
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace townrefill {

const unsigned kMsgTownLoss = 64;
const unsigned kProtocol = 90;   /* the world-server protocol that carries TOWN_LOSS and RECORD_GONE's town */
const unsigned kOpRows = 1, kOpRefilled = 2;
const unsigned kMaxRows = 256, kMaxSid = 128, kMaxLosses = 1000000;

struct Row
{
    unsigned losses, refilled;
    Row() : losses(0), refilled(0) {}
};
typedef std::map<std::string, Row> Table;
struct WireRow { std::string sid; Row row; };

inline bool SidOk(const std::string& s)
{
    if (s.empty() || s.size() > kMaxSid) return false;
    for (size_t i = 0; i < s.size(); ++i) if ((unsigned char)s[i] < 0x20) return false;   /* no tab, no newline: the file line stays one line */
    return true;
}
inline bool RowOk(const Row& r) { return r.refilled <= 1 && r.losses <= kMaxLosses; }

// Every side's merge: 1 = the stored row changed (or was new). The loss count only rises; the refilled flag is only set.
inline int Merge(Table* t, const std::string& sid, const Row& in)
{
    if (t == 0 || !SidOk(sid) || !RowOk(in)) return 0;
    Table::iterator it = t->find(sid);
    if (it == t->end()) { (*t)[sid] = in; return 1; }
    int changed = 0;
    if (in.losses > it->second.losses) { it->second.losses = in.losses; changed = 1; }
    if (in.refilled != 0 && it->second.refilled == 0) { it->second.refilled = 1; changed = 1; }
    return changed;
}
// The world server: one more lost group for the town. Returns the town's count after it, 0 = refused (no usable town id).
inline unsigned AddLoss(Table* t, const std::string& sid)
{
    if (t == 0 || !SidOk(sid)) return 0;
    Row& r = (*t)[sid];
    if (r.losses < kMaxLosses) ++r.losses;
    return r.losses;
}
// 1 = the flag was set now, 0 = it was set already (or no usable town id)
inline int MarkRefilled(Table* t, const std::string& sid)
{
    if (t == 0 || !SidOk(sid)) return 0;
    Row& r = (*t)[sid];
    if (r.refilled != 0) return 0;
    r.refilled = 1;
    return 1;
}

inline void PutU32(std::vector<char>* b, unsigned v) { char c[4]; std::memcpy(c, &v, 4); b->insert(b->end(), c, c + 4); }
inline unsigned OpOf(const char* p, size_t n) { unsigned op = 0; if (p == 0 || n < 4) return 0; std::memcpy(&op, p, 4); return op; }

inline bool EncodeRows(std::vector<char>* out, const std::vector<WireRow>& rows)
{
    if (out == 0 || rows.empty() || rows.size() > kMaxRows) return false;
    std::vector<char> b;
    PutU32(&b, kOpRows); PutU32(&b, (unsigned)rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (!SidOk(rows[i].sid) || !RowOk(rows[i].row)) return false;
        PutU32(&b, (unsigned)rows[i].sid.size()); b.insert(b.end(), rows[i].sid.begin(), rows[i].sid.end());
        PutU32(&b, rows[i].row.losses); PutU32(&b, rows[i].row.refilled);
    }
    out->swap(b);
    return true;
}
// 1 = decoded (every row valid, nothing trailing), 0 = malformed (nothing is taken from it)
inline int DecodeRows(const char* p, size_t n, std::vector<WireRow>* out)
{
    if (out == 0) return 0;
    out->clear();
    if (p == 0 || n < 8 || OpOf(p, n) != kOpRows) return 0;
    unsigned count = 0; std::memcpy(&count, p + 4, 4);
    if (count == 0 || count > kMaxRows) return 0;
    size_t at = 8;
    std::vector<WireRow> v;
    for (unsigned i = 0; i < count; ++i)
    {
        unsigned len = 0;
        if (n - at < 4) return 0;
        std::memcpy(&len, p + at, 4); at += 4;
        if (len == 0 || len > kMaxSid || n - at < (size_t)len + 8) return 0;
        WireRow w; w.sid.assign(p + at, len); at += len;
        std::memcpy(&w.row.losses, p + at, 4); std::memcpy(&w.row.refilled, p + at + 4, 4); at += 8;
        if (!SidOk(w.sid) || !RowOk(w.row)) return 0;
        v.push_back(w);
    }
    if (at != n) return 0;
    out->swap(v);
    return 1;
}
inline bool EncodeRefilled(std::vector<char>* out, const std::string& sid)
{
    if (out == 0 || !SidOk(sid)) return false;
    std::vector<char> b;
    PutU32(&b, kOpRefilled); PutU32(&b, (unsigned)sid.size()); b.insert(b.end(), sid.begin(), sid.end());
    out->swap(b);
    return true;
}
// 1 = decoded, 0 = malformed
inline int DecodeRefilled(const char* p, size_t n, std::string* sid)
{
    if (sid == 0) return 0;
    sid->clear();
    if (p == 0 || n < 8 || OpOf(p, n) != kOpRefilled) return 0;
    unsigned len = 0; std::memcpy(&len, p + 4, 4);
    if (len == 0 || len > kMaxSid || n - 8 != (size_t)len) return 0;
    std::string s(p + 8, len);
    if (!SidOk(s)) return 0;
    sid->swap(s);
    return 1;
}

// RECORD_GONE's town, written after the stamp. false = not a usable town id (nothing is written).
inline bool AppendGoneTown(std::vector<char>* b, const std::string& town)
{
    if (b == 0 || !SidOk(town)) return false;
    PutU32(b, (unsigned)town.size()); b->insert(b->end(), town.begin(), town.end());
    return true;
}
// `at` = the byte after the stamp. 1 = a town was read, 0 = none was sent, -1 = the field is malformed.
inline int ReadGoneTown(const char* p, size_t n, size_t at, std::string* out)
{
    if (out == 0) return -1;
    out->clear();
    if (at >= n) return 0;
    if (p == 0 || n - at < 4) return -1;
    unsigned len = 0; std::memcpy(&len, p + at, 4);
    if (len == 0 || len > kMaxSid || n - at - 4 < (size_t)len) return -1;
    std::string s(p + at + 4, len);
    if (!SidOk(s)) return -1;
    out->swap(s);
    return 1;
}

inline std::string Line(const std::string& sid, const Row& r)
{
    char b[48];
    _snprintf(b, 47, "\t%u\t%u\n", r.losses, r.refilled); b[47] = 0;
    return "v1\t" + sid + b;
}
// 1 = a row was read into the table (merged), 0 = the line is not a v1 row
inline int ParseLine(const std::string& line0, Table* t)
{
    std::string line(line0);
    while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == '\n')) line.erase(line.size() - 1);
    if (t == 0 || line.compare(0, 3, "v1\t") != 0) return 0;
    std::vector<std::string> f;
    size_t s = 3;
    for (;;) { const size_t e = line.find('\t', s); if (e == std::string::npos) { f.push_back(line.substr(s)); break; } f.push_back(line.substr(s, e - s)); s = e + 1; }
    if (f.size() != 3) return 0;
    for (size_t i = 1; i < 3; ++i) { if (f[i].empty() || f[i].size() > 9) return 0; for (size_t k = 0; k < f[i].size(); ++k) if (f[i][k] < '0' || f[i][k] > '9') return 0; }
    Row r;
    r.losses = (unsigned)std::strtoul(f[1].c_str(), 0, 10); r.refilled = (unsigned)std::strtoul(f[2].c_str(), 0, 10);
    if (!SidOk(f[0]) || !RowOk(r)) return 0;
    Merge(t, f[0], r);
    return 1;
}

// THE TOWN A RECORD_GONE IS COUNTED AGAINST, on the world server. A game names a town after the stamp only when the group died
// (a group emptied by a hire, a merge or a stray delete names none), so no usable sent town = no loss. When a town is named, the
// town the record held there wins over the sender's (the world server's own copy); "" = nothing is counted.
inline std::string GoneLossTown(const std::string& heldTown, const std::string& sentTown)
{
    if (!SidOk(sentTown)) return std::string();
    return SidOk(heldTown) ? heldTown : sentTown;
}

// THE WORLD AN UNSENT MESSAGE BELONGS TO. A REFILLED or a RECORD_GONE this game could not send waits for a later link; town ids and
// group ids recur across worlds, so each waits with the stamp of the world it was made in - the world's name and its id ("" = not
// known) - and at a WELCOME only those of the world the game is in now are kept (SameWorld); the rest are dropped.
inline std::string WorldStamp(const std::string& name, const std::string& id) { return name + '\x1f' + id; }
// 1 = the same world: the same name, and the same id or an id not known on either side. A stamp without the separator is no world.
inline int SameWorld(const std::string& was, const std::string& now)
{
    const size_t a = was.find('\x1f'), b = now.find('\x1f');
    if (a == std::string::npos || b == std::string::npos) return 0;
    if (was.compare(0, a, now, 0, b) != 0) return 0;
    const std::string ia = was.substr(a + 1), ib = now.substr(b + 1);
    return (ia.empty() || ib.empty() || ia == ib) ? 1 : 0;
}

// WHAT A TOWN'S WORLD RECORDS SAY ABOUT ITS PEOPLE (the game's StoreTownRecordTally fills it). Every group of the town that has a
// record is counted once: deleted, or living - and a living one is placed here, waiting to be placed here, in an area another
// game runs, unplaceable (no position, or its area cannot be read), or its creation from the record failed here.
struct Tally
{
    int living, placedHere, waitingHere, elsewhere, unplaceable, createFailed, deleted;
    Tally() : living(0), placedHere(0), waitingHere(0), elsewhere(0), unplaceable(0), createFailed(0), deleted(0) {}
};

// THE REFILL DECISION, at one check-up of one town. The engine reads are the caller's.
struct RefillIn
{
    int held;         // 1 = this game holds the town's area, its zone is live here and the town is in loaded range
    int settled;      // 1 = held for the refill's settle time
    int ready;        // 1 = the world server's opening push and this world's record snapshot are in (losses and records are known)
    int playerTown;   // 1 = the town's owner is a player's faction (or could not be read)
    int residents;    // groups on the town's residents list now; -1 = not read
    Tally tally;
    unsigned losses;  // the world server's recorded losses for the town
    unsigned refilled;// the world server's refilled flag
    int goneHere;     // groups of the town this game deleted on a death road since it started, plus its gones naming the town not sent yet
    RefillIn() : held(0), settled(0), ready(0), playerTown(0), residents(-1), losses(0), refilled(0), goneHere(0) {}
};
enum { kRdWaitNotHeld = 0, kRdWaitSettle, kRdWaitReady, kRdSkipPlayerTown, kRdSkipUnread, kRdSkipNotEmpty, kRdSkipRecords, kRdRefuseLoss, kRdSkipRefilled, kRdRefill, kRdN };
inline const char* DecideName(int d)
{
    static const char* const k[kRdN] = { "notHeld", "settling", "notReady", "playerTown", "unread", "notEmpty", "records", "loss", "refilled", "refill" };
    return (d >= 0 && d < kRdN) ? k[d] : "?";
}
// A town with a recorded loss - on the world server, a deleted group of it this game still knows, or a group of it this
// game deleted on a death road since it started or whose gone naming it is not sent yet - is never refilled. The game's own delete
// leaves no trace in the records the tally reads (DeleteEntryHere erases the note and the record), so goneHere is what carries it
// until the world server's row says so.
inline int RefillDecide(const RefillIn& in)
{
    if (in.held == 0) return kRdWaitNotHeld;
    if (in.settled == 0) return kRdWaitSettle;
    if (in.ready == 0) return kRdWaitReady;
    if (in.playerTown != 0) return kRdSkipPlayerTown;
    if (in.residents < 0) return kRdSkipUnread;
    if (in.residents > 0) return kRdSkipNotEmpty;
    if (in.tally.living > 0) return kRdSkipRecords;
    if (in.losses > 0 || in.tally.deleted > 0 || in.goneHere > 0) return kRdRefuseLoss;
    if (in.refilled != 0) return kRdSkipRefilled;
    return kRdRefill;
}
// THE REFILL, BUILDING BY BUILDING. A town starts being refilled on the game that holds its area when the
// refill decision says refill and the empty-town verdict says nothing accounts for it (FillStart). The game lists the town's
// buildings and, a few per frame, decides each one at the moment of acting (FillBuilding): the town's own facts first - this game
// still holds the area, the world server's row shows no loss and no refill, this game deleted none of the town's groups - then the
// building's: a player's (this game's, another player's stand-in, or an owner that cannot be read) is left alone, as is one
// another game's purchase or build row for which still waits here; one Kenshi's town
// population does not fill (populateAllTheBuildings' test) is skipped; one a loaded group or a living world record names as its
// home is skipped; a read that fails waits. Only then are the building's residents made, as Kenshi first made them.
inline int FillStart(int decide, int unexplained, int running) { return (decide == kRdRefill && unexplained != 0 && running == 0) ? 1 : 0; }
struct FillIn
{
    int held;          // 1 = this game holds the town's area, the zone is live and the town in loaded range
    unsigned losses;   // the world server's recorded losses for the town
    unsigned refilled; // the world server's refilled flag
    int goneHere;      // groups of the town this game deleted on a death road since it started, plus its gones not sent yet
    int owner;         // 1 = the building's owner is a player's faction (this game's, another player's, a stand-in) or could not be read
    int populatable;   // 1 = a building Kenshi's town population fills, 0 = not, -1 = unreadable
    int mayHere;       // 1 = this game may make people at the building's own area (decision 34's area answer)
    int home;          // the building's residents read: 1 = a loaded group names it as home, 0 = none, -1 = no complete answer
    int recordsHome;   // living world records whose group's home is this building
    int buildPending;  // another game's purchase or build row for the building still waits here: 1 yes, 0 no, -1 unread
    FillIn() : held(0), losses(0), refilled(0), goneHere(0), owner(1), populatable(-1), mayHere(0), home(-1), recordsHome(0), buildPending(0) {}
};
enum { kFbStopNotHeld = 0, kFbStopLoss, kFbStopRefilled, kFbSkipOwned, kFbSkipNotPop, kFbSkipHome, kFbWait, kFbFill, kFbN };
inline const char* FillName(int v)
{
    static const char* const k[kFbN] = { "notHeld", "loss", "refilled", "owned", "notPopulated", "home", "wait", "fill" };
    return (v >= 0 && v < kFbN) ? k[v] : "?";
}
inline bool FillStops(int v) { return v >= kFbStopNotHeld && v <= kFbStopRefilled; }   // the town's refill ends here, nothing is sent
inline bool FillSkips(int v) { return v >= kFbSkipOwned && v <= kFbSkipHome; }         // the building is handled without being filled
inline int FillBuilding(const FillIn& in)
{
    if (in.held == 0) return kFbStopNotHeld;
    if (in.losses > 0 || in.goneHere > 0) return kFbStopLoss;
    if (in.refilled != 0) return kFbStopRefilled;
    if (in.owner != 0 || in.buildPending > 0) return kFbSkipOwned;
    if (in.populatable == 0) return kFbSkipNotPop;
    if (in.home > 0 || in.recordsHome > 0) return kFbSkipHome;
    if (in.populatable < 0 || in.home < 0 || in.buildPending < 0 || in.mayHere == 0) return kFbWait;
    return kFbFill;
}
// What one engine call for a building leaves: ran = WorldGenRerunPopulate's answer (1 ran, 0 set aside by the gate, -1 no original
// or a fault), made / refused = the groups this call made / the creations refused inside it, tries = calls for this building so far
// (this one included). A call that ran and made groups, or ran and was refused nothing, is done; a set-aside call or one whose every
// creation was refused is tried again later; a fault is tried again up to kFillFaultTries calls and then given up.
const int kFillFaultTries = 3;
enum { kFrDone = 0, kFrRetry, kFrGiveUp };
inline int FillAfterRun(int ran, long long made, long long refused, int tries)
{
    if (ran == 1) return (made == 0 && refused > 0) ? kFrRetry : kFrDone;
    if (ran == 0) return kFrRetry;
    return tries < kFillFaultTries ? kFrRetry : kFrGiveUp;
}
// A BUILDING THAT IS PUT OFF - a read that did not complete, an area this game may not make people in, a key that does not resolve
// to one building, no object factory yet - or whose call is to be repeated is looked at again after the retry gap. Every put-off
// is counted; at kFillWaitCap the building is handled without being filled (kFwGiveUp), so a town's refill always ends.
const int kFillWaitCap = 30;
enum { kFwAgain = 0, kFwGiveUp };
inline int FillWaitStep(int waits) { return waits >= kFillWaitCap ? kFwGiveUp : kFwAgain; }   // waits = the put-offs so far, this one included
// THE TOWN IS DONE when no building of it is left to handle: REFILLED goes once when at least one building was filled (kTdSend);
// a town in which nothing could be filled is dropped without it (kTdNothing), so the row never says refilled for a town still empty.
enum { kTdSend = 1, kTdNothing };
inline int TownFillDone(int filled) { return filled > 0 ? kTdSend : kTdNothing; }
// A REFILL THAT STOPS PART-WAY. When it stops because this game no longer holds the town's area (or the town left loaded range) after
// filling at least one building, those buildings' people exist: the town is marked refilled and REFILLED goes (kTdSend), so no other
// game refills it. A stop for a recorded loss or for another game's refill sends nothing.
inline int FillStopDone(int stop, int filled) { return (stop == kFbStopNotHeld && filled > 0) ? kTdSend : kTdNothing; }
// A building this refill's own call was set aside for (the residents gate set its squads aside; the re-offer makes them later) and
// that now has residents homed there was filled through that re-offer: it counts as filled.
inline int FillSkipFilled(int verdict, int setAsideBefore) { return (verdict == kFbSkipHome && setAsideBefore != 0) ? 1 : 0; }

}   // namespace townrefill
