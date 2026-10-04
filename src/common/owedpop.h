#pragma once
// T-581: A TOWN'S FIRST POPULATION THAT WAS SET ASIDE IS KEPT ON THE WORLD SERVER UNTIL IT IS MADE.
// Kenshi makes a building's residents and a town's bar crowd once, at the first load of a never-saved zone. When a game's town
// gate cannot answer yet it SETS THAT WORK ASIDE (townreoffer.h); a zone saved before the work is offered again would otherwise
// stay empty for good. So every piece of set-aside work is an OWED ROW on the world server, per world, until it is made.
// The pure half: the row, its wire form (OWED, world-server kind 63, protocol 87), its file line (owed_people.txt) and the
// decisions both sides take. ONE header compiled into the plugin (towngen.cpp, store.cpp), the world server (store_main.cpp)
// and the offline suite, so the three cannot hold different ideas of one row.
//
// - A ROW: kind (residents of one building / the first bar roll of one town), key (the building's position key - items.cpp
//   ObjectPositionKey - or, for a bar, the town's stringID), the town's stringID, and x / z (where the work stands: the
//   building's position, or the town's).
// - ADD: a game that sets work aside sends the row. The world server keeps it (a second ADD of the same row changes nothing).
// - EXACTLY ONCE: a row is made only by the game the world server has GIVEN it to. A game whose town check-up finds it holds the
//   area with the zone live asks for the row (CLAIM); the world server gives it to the first asker and to nobody else while that
//   game keeps it; the game hands it back (RELEASE) the moment it no longer holds the area with the zone live, and a game that
//   disconnects loses every row it held. Every game hears every row with "yours" / "another game's" / "nobody's".
// - DONE: the row leaves the table. From the game holding it: made, gone (the building no longer exists) or fault (the engine
//   call faulted). Gone, fault and empty hand the row back and are tried again, 5 minutes apart; the third of a kind removes it. From ANY game: has (that game's engine shows the residents / the bar crowd already there).
//   Every game hears GONE and forgets its own copy of the work.
// - DOWN: ROWS (the whole table at WELCOME, in chunks of 256, then every changed row) and GONE.
// WIRE (little-endian, as every world-server message): u32 op, then
//   ADD     u32 kind, str key, str sid, f32 x, f32 z
//   CLAIM   u32 kind, str key            RELEASE  u32 kind, str key            DONE  u32 kind, str key, u32 why
//   ROWS    u32 count 1..256, count x [u32 kind, str key, str sid, f32 x, f32 z, u32 claim]
//   GONE    u32 kind, str key, u32 why
//   str = u32 length 1..128, the bytes (no control characters).
// FILE LINE: v1<TAB>kind<TAB>key<TAB>sid<TAB>x<TAB>z  (who holds a row is never written: every link ends with the process)
// A row the world server lost (its file unreadable at start, a write that failed before a restart) is settled by every game that had seen it - an accepted trade-off.
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace owedpop {

const unsigned kMsgOwed = 63;
const unsigned kProtocol = 87;   /* the world-server protocol that carries OWED */

const unsigned kKindResidents = 1, kKindBar = 2;
const unsigned kOpAdd = 1, kOpClaim = 2, kOpRelease = 3, kOpDone = 4, kOpRows = 5, kOpGone = 6;
const unsigned kWhyMade = 1, kWhyHas = 2, kWhyGone = 3, kWhyFault = 4, kWhyAway = 5, kWhyEmpty = 6, kWhyFull = 7;   /* kWhyAway / kWhyFull: GONE only - never sent up (kWhyFull: the world server refused to store the work - the game makes it as before). kWhyEmpty: the engine call ran, chose no residents and nothing refused one */
const unsigned kClaimNone = 0, kClaimYou = 1, kClaimOther = 2;
const size_t kMaxRows = 256;
const size_t kStrMax = 128;
const int kTriesMax = 3;             /* a row leaves after its third "gone" or its third "fault" report; each report hands it back */
const double kRetrySpacingSec = 300.0;   /* a row whose gone / fault / empty count rose is not given again for 5 minutes */
const unsigned kReaskMs = 30000;          /* a game asks again for a row it got no answer about after 30 s */
const size_t kRowsPerGame = 2048;    /* rows one game may have stored (counted per connection's slot, rows read from the file excluded) */

// claimant: on the world server, the slot + 1 of the game holding the row (0 = nobody); on a game, kClaim* as that game sees it.
// adder / gones / faults: world server only - the slot + 1 of the game that stored the row (0 = read from the file), and how many
// times the game holding the row reported its building gone / its engine call faulted (in memory: a restart starts them again).
// empties / retryAt: world server only - empty reports, and the time (world server seconds) before which the row is not given again.
struct Row { unsigned kind; std::string key, sid; float x, z; int claimant, adder, gones, faults, empties; double retryAt; Row() : kind(0), x(0.0f), z(0.0f), claimant(0), adder(0), gones(0), faults(0), empties(0), retryAt(0.0) {} };
typedef std::map<std::string, Row> Table;   /* keyed by TableKey(kind, key) */

inline std::string TableKey(unsigned kind, const std::string& key) { return std::string(kind == kKindBar ? "b|" : "r|") + key; }
inline const char* KindName(unsigned k) { return k == kKindBar ? "bar" : (k == kKindResidents ? "residents" : "?"); }
inline const char* WhyName(unsigned w)
{
    if (w == kWhyMade) return "made";
    if (w == kWhyHas) return "has";
    if (w == kWhyGone) return "gone";
    if (w == kWhyFault) return "fault";
    if (w == kWhyAway) return "away";
    if (w == kWhyEmpty) return "empty";
    if (w == kWhyFull) return "full";
    return "?";
}
inline const char* ClaimName(int c) { return c == (int)kClaimYou ? "you" : (c == (int)kClaimOther ? "other" : "none"); }
inline bool StrOk(const std::string& s)
{
    if (s.empty() || s.size() > kStrMax) return false;
    for (size_t i = 0; i < s.size(); ++i) { const unsigned char c = (unsigned char)s[i]; if (c < 0x20 || c == 0x7F) return false; }
    return true;
}
inline bool KindOk(unsigned k) { return k == kKindResidents || k == kKindBar; }
inline bool CoordOk(float v) { return v > -1.0e7f && v < 1.0e7f; }   /* NaN fails too */
inline bool RowOk(const Row& r) { return KindOk(r.kind) && StrOk(r.key) && StrOk(r.sid) && CoordOk(r.x) && CoordOk(r.z); }
inline bool UpWhyOk(unsigned w) { return (w >= kWhyMade && w <= kWhyFault) || w == kWhyEmpty; }

// ---- wire ----
inline void PutU32(std::vector<char>* b, unsigned v) { char c[4]; std::memcpy(c, &v, 4); b->insert(b->end(), c, c + 4); }
inline void PutF32(std::vector<char>* b, float v) { char c[4]; std::memcpy(c, &v, 4); b->insert(b->end(), c, c + 4); }
inline void PutStr(std::vector<char>* b, const std::string& s) { PutU32(b, (unsigned)s.size()); b->insert(b->end(), s.begin(), s.end()); }
struct Reader
{
    const char* p; size_t n, at; bool bad;
    Reader(const char* p0, size_t n0) : p(p0), n(n0), at(0), bad(p0 == 0 && n0 != 0) {}
    unsigned U32() { if (bad || n - at < 4) { bad = true; return 0; } unsigned v; std::memcpy(&v, p + at, 4); at += 4; return v; }
    float F32() { if (bad || n - at < 4) { bad = true; return 0.0f; } float v; std::memcpy(&v, p + at, 4); at += 4; return v; }
    std::string Str() { const unsigned len = U32(); if (bad || len == 0 || len > kStrMax || n - at < len) { bad = true; return std::string(); } std::string s(p + at, p + at + len); at += len; if (!StrOk(s)) bad = true; return s; }
    bool Done() const { return !bad && at == n; }
};
inline bool EncodeAdd(std::vector<char>* out, const Row& r)
{
    out->clear();
    if (!RowOk(r)) return false;
    PutU32(out, kOpAdd); PutU32(out, r.kind); PutStr(out, r.key); PutStr(out, r.sid); PutF32(out, r.x); PutF32(out, r.z);
    return true;
}
// CLAIM / RELEASE
inline bool EncodeKey(std::vector<char>* out, unsigned op, unsigned kind, const std::string& key)
{
    out->clear();
    if ((op != kOpClaim && op != kOpRelease) || !KindOk(kind) || !StrOk(key)) return false;
    PutU32(out, op); PutU32(out, kind); PutStr(out, key);
    return true;
}
// DONE (up, why kWhyMade..kWhyFault) / GONE (down, any why)
inline bool EncodeDone(std::vector<char>* out, unsigned op, unsigned kind, const std::string& key, unsigned why)
{
    out->clear();
    if ((op != kOpDone && op != kOpGone) || !KindOk(kind) || !StrOk(key)) return false;
    if (op == kOpDone && !UpWhyOk(why)) return false;
    PutU32(out, op); PutU32(out, kind); PutStr(out, key); PutU32(out, why);
    return true;
}
// ROWS: each row's claimant field carries kClaim* as the RECEIVING game sees it
inline bool EncodeRows(std::vector<char>* out, const std::vector<Row>& rows)
{
    out->clear();
    if (rows.empty() || rows.size() > kMaxRows) return false;
    PutU32(out, kOpRows); PutU32(out, (unsigned)rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const Row& r = rows[i];
        if (!RowOk(r) || r.claimant < 0 || r.claimant > (int)kClaimOther) { out->clear(); return false; }
        PutU32(out, r.kind); PutStr(out, r.key); PutStr(out, r.sid); PutF32(out, r.x); PutF32(out, r.z); PutU32(out, (unsigned)r.claimant);
    }
    return true;
}
struct Msg { unsigned op, kind, why; std::string key; Row row; std::vector<Row> rows; Msg() : op(0), kind(0), why(0) {} };
// 1 = decoded (every field valid, nothing trailing), 0 = malformed (nothing is taken from it)
inline int Decode(const char* p, size_t n, Msg* m)
{
    *m = Msg();
    Reader r(p, n);
    m->op = r.U32();
    if (r.bad) return 0;
    if (m->op == kOpAdd)
    {
        m->row.kind = r.U32(); m->row.key = r.Str(); m->row.sid = r.Str(); m->row.x = r.F32(); m->row.z = r.F32();
        if (!r.Done() || !RowOk(m->row)) return 0;
        m->kind = m->row.kind; m->key = m->row.key;
        return 1;
    }
    if (m->op == kOpClaim || m->op == kOpRelease)
    {
        m->kind = r.U32(); m->key = r.Str();
        return (r.Done() && KindOk(m->kind)) ? 1 : 0;
    }
    if (m->op == kOpDone || m->op == kOpGone)
    {
        m->kind = r.U32(); m->key = r.Str(); m->why = r.U32();
        if (!r.Done() || !KindOk(m->kind)) return 0;
        return (m->op == kOpGone || UpWhyOk(m->why)) ? 1 : 0;
    }
    if (m->op == kOpRows)
    {
        const unsigned c = r.U32();
        if (r.bad || c == 0 || c > kMaxRows) return 0;
        for (unsigned i = 0; i < c; ++i)
        {
            Row w; w.kind = r.U32(); w.key = r.Str(); w.sid = r.Str(); w.x = r.F32(); w.z = r.F32(); const unsigned cl = r.U32();
            if (r.bad || !RowOk(w) || cl > kClaimOther) return 0;
            w.claimant = (int)cl;
            m->rows.push_back(w);
        }
        return r.Done() ? 1 : 0;
    }
    return 0;
}

// ---- the world server's table (claimant = slot + 1, 0 = nobody) ----
// 1 = a new row (nobody holds it), 0 = the row was already there (unchanged), -1 = not a valid row, kAddFull = `who` already
// stored `cap` rows (refused). who 0 = no owner (a row read from the file); cap 0 = no limit.
const int kAddFull = -2;
inline int TableAddFrom(Table* t, const Row& in, int who, size_t cap)
{
    if (!RowOk(in)) return -1;
    const std::string k = TableKey(in.kind, in.key);
    if (t->find(k) != t->end()) return 0;
    if (who > 0 && cap > 0)
    {
        size_t mine = 0;
        for (Table::const_iterator it = t->begin(); it != t->end(); ++it) if (it->second.adder == who) ++mine;
        if (mine >= cap) return kAddFull;
    }
    Row r = in; r.claimant = 0; r.adder = who; r.gones = 0; r.faults = 0; r.empties = 0; r.retryAt = 0.0;
    (*t)[k] = r;
    return 1;
}
inline int TableAdd(Table* t, const Row& in) { return TableAddFrom(t, in, 0, 0); }
const int kClaimAbsent = 0, kClaimGranted = 1, kClaimHeld = 2, kClaimLater = 3;
// CLAIM from `who` (slot + 1, >= 1) at `now` (world server seconds): granted when nobody holds the row or `who` already does; held
// when another game does; later while a counted report's spacing runs (no answer - the game asks again, AskAgain)
inline int TableClaim(Table* t, unsigned kind, const std::string& key, int who, double now = 0.0)
{
    if (who <= 0) return kClaimHeld;
    const Table::iterator it = t->find(TableKey(kind, key));
    if (it == t->end()) return kClaimAbsent;
    if (it->second.claimant == who) return kClaimGranted;
    if (it->second.claimant != 0) return kClaimHeld;
    if (now < it->second.retryAt) return kClaimLater;
    it->second.claimant = who;
    return kClaimGranted;
}
// AskAgain: a game that sent CLAIM and heard nothing (the row unchanged) asks again after kReaskMs; RELEASE is never repeated
inline int AskAgain(int isClaim, unsigned elapsedMs) { return (isClaim != 0 && elapsedMs >= kReaskMs) ? 1 : 0; }
// the reports that are counted on a row and hand it back (gone, fault, empty)
inline int CountedWhy(unsigned w) { return (w == kWhyGone || w == kWhyFault || w == kWhyEmpty) ? 1 : 0; }
// GoneKeepsLocal: a GONE that leaves this game's own work in place, made here as before (the world server refused to store it)
inline int GoneKeepsLocal(unsigned w) { return w == kWhyFull ? 1 : 0; }
// RELEASE: 1 = `who` held it and nobody does now
inline int TableRelease(Table* t, unsigned kind, const std::string& key, int who)
{
    const Table::iterator it = t->find(TableKey(kind, key));
    if (it == t->end() || who <= 0 || it->second.claimant != who) return 0;
    it->second.claimant = 0;
    return 1;
}
// DONE from `who`:
//   has                     - any game: the work is already there. The row leaves (kDoneRemoved).
//   made                    - the game holding the row: it leaves (kDoneRemoved). Any other game: the work exists all the same
//                             (that game made it while it believed it held the row), so it leaves as "has" (kDoneMadeAsHas).
//   gone / fault / empty    - only the game holding the row (else kDoneRefused): counted on the row and handed back (nobody holds
//                             it; kDoneCounted), not given again for kRetrySpacingSec, then tried again by whichever game holds the
//                             area; the third report of the same kind removes the row (kDoneRemoved). A transient fault, a building
//                             not found while a zone settles, or a re-run that made nobody does not empty the town for good.
const int kDoneAbsent = 0, kDoneRemoved = 1, kDoneRefused = -1, kDoneCounted = 2, kDoneMadeAsHas = 3;
inline int TableDone(Table* t, unsigned kind, const std::string& key, int who, unsigned why, double now = 0.0)
{
    const Table::iterator it = t->find(TableKey(kind, key));
    if (it == t->end()) return kDoneAbsent;
    const bool holder = who > 0 && it->second.claimant == who;
    if (why == kWhyHas) { t->erase(it); return kDoneRemoved; }
    if (why == kWhyMade) { if (who <= 0) return kDoneRefused; t->erase(it); return holder ? kDoneRemoved : kDoneMadeAsHas; }
    if (!holder || CountedWhy(why) == 0) return kDoneRefused;
    int& n = (why == kWhyGone) ? it->second.gones : (why == kWhyFault ? it->second.faults : it->second.empties);
    ++n;
    it->second.claimant = 0;
    it->second.retryAt = now + kRetrySpacingSec;
    if (n >= kTriesMax) { t->erase(it); return kDoneRemoved; }
    return kDoneCounted;
}
// a game left: every row it held is nobody's again; the table keys released are returned
inline std::vector<std::string> TablePeerGone(Table* t, int who)
{
    std::vector<std::string> out;
    if (who <= 0) return out;
    for (Table::iterator it = t->begin(); it != t->end(); ++it) if (it->second.claimant == who) { it->second.claimant = 0; out.push_back(it->first); }
    return out;
}
// the claim field one receiving game is told for a row
inline int ClaimFor(int claimant, int recipient)
{
    if (claimant == 0) return (int)kClaimNone;
    return (recipient > 0 && claimant == recipient) ? (int)kClaimYou : (int)kClaimOther;
}

// ---- the file ----
inline std::string Line(const Row& r)
{
    char b[64]; std::string s = "v1\t";
    _snprintf(b, 63, "%u", r.kind); b[63] = 0; s += b; s += "\t" + r.key + "\t" + r.sid + "\t";
    _snprintf(b, 63, "%.2f\t%.2f", r.x, r.z); b[63] = 0; s += b;
    return s + "\n";
}
// 1 = a row was read into the table, 0 = not a usable v1 line
inline int ParseLine(const std::string& line0, Table* t)
{
    std::string line = line0;
    while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == '\n')) line.erase(line.size() - 1);
    std::vector<std::string> f;
    size_t at = 0;
    for (;;) { const size_t tab = line.find('\t', at); if (tab == std::string::npos) { f.push_back(line.substr(at)); break; } f.push_back(line.substr(at, tab - at)); at = tab + 1; }
    if (f.size() != 6 || f[0] != "v1") return 0;
    char* e = 0;
    const unsigned long kind = std::strtoul(f[1].c_str(), &e, 10); if (e == f[1].c_str() || *e != 0) return 0;
    const double x = std::strtod(f[4].c_str(), &e); if (e == f[4].c_str() || *e != 0) return 0;
    const double z = std::strtod(f[5].c_str(), &e); if (e == f[5].c_str() || *e != 0) return 0;
    Row r; r.kind = (unsigned)kind; r.key = f[2]; r.sid = f[3]; r.x = (float)x; r.z = (float)z;
    return TableAdd(t, r) >= 0 && RowOk(r) ? 1 : 0;
}

// ---- a game's decisions ----
// LoadCause: the load-time gate's set-aside cause once the owed table is known. `cause` is townreoffer::DeferCause's answer,
// `may` zones' MayInventFromView answer for the work's sector (1 = this game would make it now), `owed` 1 = the work is an owed
// row this game does not hold (in the table, or sent and not yet back), `lever` the TEST-ONLY `owedtest aside on`. Work this
// game would make now is set aside when it is owed (it is made under a claim, from the check-up) or the lever is on. Every other
// answer stands. The causes are townreoffer::kDefOwed / kDefTestLever (4 / 5 there; repeated here so the header stands alone).
const int kCauseNone = 0, kCauseTestLever = 4, kCauseOwed = 5;
inline int LoadCause(int cause, int may, int owed, int lever)
{
    if (cause != kCauseNone || may != 1) return cause;
    if (lever != 0) return kCauseTestLever;
    if (owed != 0) return kCauseOwed;
    return kCauseNone;
}
// Commit: what the town check-up does with one item of set-aside work, once the facts are read.
//   decide  townreoffer::Decide (0 wait, 1 generate - this game holds the area with the zone live, 2 another game holds it)
//   found   1 = the building was found in this game's live zones (a bar: always 1); 0 = a complete search found none; -1 = unsure
//   has     1 = the engine already shows the work done here (residents homed in the building; a bar list used up, or a fill
//           recorded in the world's bar table); 0 = not done; -1 = could not be read
//   stored  0 = only this game knows it (never reached the world server); 1 = a row in the world server's table;
//           2 = sent on this link and not yet back from the world server
//   claim   kClaimNone / kClaimYou / kClaimOther (meaningful when stored == 1)
const int kActWait = 0;       // keep it (asked again at the next check-up)
const int kActMake = 1;       // make it now (the engine call)
const int kActClaim = 2;      // ask the world server for the row
const int kActForget = 3;     // settle it on this game only, as the gate settles it (nothing goes to the world server)
const int kActDoneHas = 4;    // DONE has: the work is already there
const int kActDoneGone = 5;   // DONE gone (this game holds the row): the building no longer exists
inline int Commit(int decide, int found, int has, int stored, int claim)
{
    if (has == 1) return stored == 0 ? kActForget : kActDoneHas;
    if (decide != 1 && decide != 2) return kActWait;
    if (found == 0)
    {
        if (stored == 0) return kActForget;
        if (stored == 1 && claim == (int)kClaimYou) return kActDoneGone;
        if (stored == 1 && claim == (int)kClaimNone && decide == 1) return kActClaim;
        return kActWait;
    }
    if (found < 0) return kActWait;
    if (decide == 2) return stored == 0 ? kActForget : kActWait;   /* the holder makes an owed row; this game's own copy is the gate's */
    if (has < 0) return kActWait;
    if (stored == 0) return kActMake;
    if (stored == 2) return kActWait;
    if (claim == (int)kClaimYou) return kActMake;
    if (claim == (int)kClaimNone) return kActClaim;
    return kActWait;
}
// FlushStep: what a game does with one item of its own set-aside work when it sends its work up (once the world server's opening
// push is in, so `inTable` is the whole table). sentGen = the link generation its ADD went out on (0 never, -1 not sendable),
// gen = this link's, acked = its row was seen in the world server's table on some link.
const int kFlushKeep = 0;     // nothing to do (sent on this link, or not sendable)
const int kFlushSend = 1;     // send the ADD
const int kFlushMark = 2;     // the row is in the table already: count it as sent on this link, send nothing
const int kFlushSettle = 3;   // its row was in the table and is gone from it now - made or seen there while this game was away
                              // (the removal was missed): settled, dropped here, never sent again
inline int FlushStep(long sentGen, long gen, int inTable, int acked)
{
    if (sentGen < 0 || sentGen == gen) return kFlushKeep;
    if (inTable != 0) return kFlushMark;
    if (sentGen > 0 && acked != 0) return kFlushSettle;
    return kFlushSend;   /* never sent, or sent and never seen in the table (the link dropped before it arrived) */
}
// BarHas: is a town's first bar roll shown done, for the town check-up? `filled` (this town object's list used up) counts at once,
// as it always did; a fill recorded in the world's bar table, or the owed row removed by the world server (settled), counts only
// while the zone is live and the world is not being freed (may -2), as the check-up's other answers do.
inline int BarHas(int filled, int zoneLive, int may, int filledElsewhere, int settled)
{
    if (filled != 0) return 1;
    return (zoneLive != 0 && may != -2 && (filledElsewhere != 0 || settled != 0)) ? 1 : 0;
}
// AfterRerun: what a residents re-run that ran leaves, from the re-run's own counts (the re-run slot: `made` squads made; `keep` =
// townpending::RerunKeep - none made because decision 34 refused; `givenUp` = townpending::KeptGiveUp on this building's kept re-runs
// this session; `refusedAny` = creations the re-run asked for that any creation gate refused, any cause) and where the work stands
// (`stored`, as in Commit; a re-run is reached only with 0 or 1).
const int kRunLocalDone = 0;       // only this game knew the work: its entry ends, nothing is sent
const int kRunKeep = 1;            // kept: the entry stays and an owed row stays this game's (no DONE)
const int kRunGiveUpLocal = 2;     // given up for the session: the entry ends (only this game knew it)
const int kRunGiveUpRelease = 3;   // given up for the session: the entry ends and the owed row is handed back (RELEASE), never removed
const int kRunDoneMade = 4;        // residents made: DONE made
const int kRunDoneEmpty = 5;       // the call chose no residents and nothing refused one: DONE empty (counted, spaced)
const int kRunRelease = 6;         // nothing made, refused for another cause (no notebook, not the holder, no map...): the entry stays
                                   // and the owed row is handed back (RELEASE, not counted) - like a lost hold
inline int AfterRerun(long long made, int keep, int givenUp, int stored, long long refusedAny)
{
    if (keep != 0) { if (givenUp == 0) return kRunKeep; return stored == 1 ? kRunGiveUpRelease : kRunGiveUpLocal; }
    if (stored != 1) return kRunLocalDone;
    if (made > 0) return kRunDoneMade;
    return refusedAny > 0 ? kRunRelease : kRunDoneEmpty;
}
// KeepClaim: a row this game holds is kept only while this game holds its area with the zone live and the lever is off;
// otherwise it is handed back at once so the game that does hold it can make it.
inline int KeepClaim(int zoneLive, int may, int lever)
{
    return (zoneLive != 0 && may == 1 && lever == 0) ? 1 : 0;
}

}   // namespace owedpop
