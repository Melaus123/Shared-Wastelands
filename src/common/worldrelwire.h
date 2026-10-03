#pragma once
// par24 (parity backlog P24; parity-audit-factions-economy.md row F5): WORLD-VS-WORLD FACTION STANDINGS ARE THE WORLD'S.
// The pure half: the notebook's table of NPC-faction-vs-NPC-faction standings (world_relations.txt, store protocol 55,
// WORLD_REL 51), its wire form, its merge rule and its file line. ONE header compiled into the plugin (relations.cpp,
// store.cpp), the notebook (store_main.cpp) and the offline suite, so the three cannot hold different ideas of one row.
//
// AUTHORITY (decision 48 - no host-centred rule): the NOTEBOOK's table is the record. Every game applies the whole table
// when its world runs (the WELCOME push); a game offers the pairs its own save has and the table lacks (SEED - the first
// offer of a pair wins; S2-62: while the OPERATOR's game is linked and has not ended its walk with kind 5 SEED_END, another
// game's seed is HELD and merged first-writer AFTER the operator's walk ends - the operator's values win, the pairs its save
// lacks are filled from the held rows in arrival order; SeedDecision, MergeHeld - re-check B);
// a change its OWN engine makes to a world pair goes up as a CHANGE (the latest to reach the notebook wins) and the notebook
// sends every row it took to every OTHER linked game in the order it took them, and answers the sender with kind 4 ECHO
// (one row per CHANGE row, the table's row after the merge) - so every game ends on the notebook's last value even when two
// games changed one pair at once, and a game never writes an older row over a newer change of its own that the notebook
// has not answered yet (SendBook, review-par24 #2).
// A pair with a player faction or a player's stand-in on either side is NEVER a row here (those travel by slot on the
// session link, relations.cpp): a sid that starts with '@' (the slot wire names) is refused, so decision 20 (players start
// NEUTRAL to each other) cannot be touched by this table. A stand-in's RECORD id (coop-p<n>, and protocol 67's coop-peer) is
// refused the same way (review-par24 #1): a stand-in whose player is offline is an ordinary faction to the engine and its
// record id is the only name it has here, so without this refusal another player's standing would become a world row.
//
// A ROW, keyed "<owner sid>|<other sid>" (FCS stringIDs): the OWNER faction's standing towards the OTHER (one direction).
//   relation, trust, trustNeg  RelationData +4 / +8 / +0xC;  flags  1 ally flag (+0), 2 at-war flag (+2)
// WIRE (WORLD_REL, both ways): {u8 kind, u32 count 1..256 (0..256 for SEED_END), count x [u32 aLen 1..128, a bytes, u32 bLen 1..128, b bytes,
//   f32 relation, f32 trust, f32 trustNeg, u8 flags 0..3]}, little-endian as every store message.
//   kind 1 SEED (up: pairs this game's save has that the table lacks), 2 CHANGE (up: this game's engine moved the pair),
//   3 ROWS (down: the WELCOME push), 4 ECHO (down, the sender only: its own CHANGE rows answered, taken or already held),
//   5 SEED_END (up: the last seed chunk of a walk, possibly empty - S2-62). A row the notebook took is broadcast with the kind
//   it arrived as (1 or 2; SEED_END rows as 1), so a game can tell another game's engine change from a seed.
// SIZE (measured, run logs: "[REL] snapshot: ... (103|104 factions)"): at most ~102 x 101 = ~10.3k directed world pairs;
//   a row is ~57 bytes with the usual ~18-character stringIDs, so one 256-row message is ~15 KB (70.9 KB worst case) and
//   a fully populated table ~590 KB over ~41 messages - only the pairs a save actually holds are rows.
// FILE LINE: v1<TAB>a<TAB>b<TAB>relation<TAB>trust<TAB>trustNeg<TAB>flags
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "slotwire.h"   /* review-par24 #1: coopslot::kStandInPrefix */

namespace coopwrel {

const unsigned kWrMaxRows = 256;          // per message
const unsigned kWrMaxSid = 128;
const unsigned kWrMaxTable = 65536;       // the whole table (104 factions measured: ~10.3k directed pairs at most)
const float kWrMaxAbs = 1.0e6f;
enum { kWrSeed = 1, kWrChange = 2, kWrRows = 3, kWrEcho = 4, kWrSeedEnd = 5 };
inline bool KindOk(int k) { return k >= kWrSeed && k <= kWrSeedEnd; }

struct Row
{
    float relation, trust, trustNeg;
    unsigned flags;
    Row() : relation(0.0f), trust(0.0f), trustNeg(0.0f), flags(0) {}
};
inline bool SameRow(const Row& x, const Row& y)
{
    return x.relation == y.relation && x.trust == y.trust && x.trustNeg == y.trustNeg && x.flags == y.flags;
}
typedef std::map<std::string, Row> Table;   // key: a + "|" + b
struct WireRow { std::string a, b; Row row; };

/* review-par24 #1: a stand-in's record id - "coop-p<n>", and protocol 67's "coop-peer" (the same prefix) */
inline bool IsStandInSid(const std::string& s) { const std::string p(coopslot::kStandInPrefix); return s.compare(0, p.size(), p) == 0; }
inline bool SidOk(const std::string& s)
{
    if (s.empty() || s.size() > kWrMaxSid || s[0] == '@') return false;   /* '@' = a player / stand-in wire name: never a world row */
    if (IsStandInSid(s)) return false;   /* review-par24 #1: nor a stand-in's record id (an offline player's stand-in is an ordinary faction to the engine) */
    for (size_t i = 0; i < s.size(); ++i) { const unsigned char c = (unsigned char)s[i]; if (c < 0x20 || c == '|') return false; }
    return true;
}
inline bool ValOk(float v) { return v == v && v <= kWrMaxAbs && v >= -kWrMaxAbs; }   /* NaN and absurd values refused */
inline bool RowOk(const std::string& a, const std::string& b, const Row& r)
{
    return SidOk(a) && SidOk(b) && a != b && ValOk(r.relation) && ValOk(r.trust) && ValOk(r.trustNeg) && r.flags <= 3u;
}
inline std::string Key(const std::string& a, const std::string& b) { return a + "|" + b; }
inline bool SplitKey(const std::string& k, std::string* a, std::string* b)
{
    const size_t p = k.find('|');
    if (p == std::string::npos) return false;
    *a = k.substr(0, p); *b = k.substr(p + 1);
    return SidOk(*a) && SidOk(*b);
}

// The merge. 1 = the table changed, 0 = nothing to do (a SEED for a pair already held, an identical row), -1 = refused (a bad
// row, or a NEW pair while the table is full). SEED: first writer wins. CHANGE and ROWS: the row replaces what is held.
inline int Merge(Table* t, const std::string& a, const std::string& b, const Row& r, int kind)
{
    if (t == 0 || !RowOk(a, b, r) || (kind != kWrSeed && kind != kWrChange && kind != kWrRows)) return -1;
    const std::string k = Key(a, b);
    Table::iterator it = t->find(k);
    if (it == t->end())
    {
        if (t->size() >= kWrMaxTable) return -1;
        (*t)[k] = r;
        return 1;
    }
    if (kind == kWrSeed) return 0;
    if (SameRow(it->second, r)) return 0;
    it->second = r;
    return 1;
}

inline void PutU32(std::vector<char>* b, unsigned v) { char c[4]; std::memcpy(c, &v, 4); b->insert(b->end(), c, c + 4); }
inline void PutF32(std::vector<char>* b, float v) { char c[4]; std::memcpy(c, &v, 4); b->insert(b->end(), c, c + 4); }
inline bool Encode(std::vector<char>* out, int kind, const std::vector<WireRow>& rows)
{
    if (out == 0 || (rows.empty() && kind != kWrSeedEnd) || rows.size() > kWrMaxRows || !KindOk(kind)) return false;   /* only SEED_END may be empty: a walk that found nothing still ends */
    std::vector<char> b;
    b.push_back((char)(unsigned char)kind);
    PutU32(&b, (unsigned)rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const WireRow& w = rows[i];
        if (!RowOk(w.a, w.b, w.row)) return false;
        PutU32(&b, (unsigned)w.a.size()); b.insert(b.end(), w.a.begin(), w.a.end());
        PutU32(&b, (unsigned)w.b.size()); b.insert(b.end(), w.b.begin(), w.b.end());
        PutF32(&b, w.row.relation); PutF32(&b, w.row.trust); PutF32(&b, w.row.trustNeg);
        b.push_back((char)(unsigned char)w.row.flags);
    }
    out->swap(b);
    return true;
}
// 1 = decoded (every row valid, nothing trailing), 0 = malformed (nothing is taken from it)
inline int Decode(const char* p, size_t n, int* kind, std::vector<WireRow>* out)
{
    if (out == 0 || kind == 0) return 0;
    out->clear();
    if (p == 0 || n < 5) return 0;
    const int k = (int)(unsigned char)p[0];
    if (!KindOk(k)) return 0;
    unsigned count = 0; std::memcpy(&count, p + 1, 4);
    size_t at = 5;
    if ((count == 0 && k != kWrSeedEnd) || count > kWrMaxRows) return 0;
    std::vector<WireRow> v;
    for (unsigned i = 0; i < count; ++i)
    {
        WireRow w;
        for (int s = 0; s < 2; ++s)
        {
            unsigned len = 0;
            if (n - at < 4) return 0;
            std::memcpy(&len, p + at, 4); at += 4;
            if (len == 0 || len > kWrMaxSid || n - at < (size_t)len) return 0;
            (s == 0 ? w.a : w.b).assign(p + at, len); at += len;
        }
        if (n - at < 13) return 0;
        std::memcpy(&w.row.relation, p + at, 4); std::memcpy(&w.row.trust, p + at + 4, 4); std::memcpy(&w.row.trustNeg, p + at + 8, 4);
        w.row.flags = (unsigned)(unsigned char)p[at + 12]; at += 13;
        if (!RowOk(w.a, w.b, w.row)) return 0;
        v.push_back(w);
    }
    if (at != n) return 0;
    *kind = k;
    out->swap(v);
    return 1;
}
inline std::string Line(const std::string& a, const std::string& b, const Row& r)
{
    char t[128];
    _snprintf(t, 127, "\t%.9g\t%.9g\t%.9g\t%u\n", (double)r.relation, (double)r.trust, (double)r.trustNeg, r.flags); t[127] = 0;
    return "v1\t" + a + "\t" + b + t;
}
// 1 = a row was read into the table, 0 = the line is not a usable v1 row (*standIn = 1 when it was refused only because a
// side is a stand-in's record id - review-par24 #1: such a row is dropped from the file, counted)
inline int ParseLine(const std::string& line0, Table* t, int* standIn = 0)
{
    std::string line(line0);
    while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == '\n')) line.erase(line.size() - 1);
    if (line.compare(0, 3, "v1\t") != 0) return 0;
    std::vector<std::string> f;
    size_t s = 3;
    for (;;) { const size_t e = line.find('\t', s); if (e == std::string::npos) { f.push_back(line.substr(s)); break; } f.push_back(line.substr(s, e - s)); s = e + 1; }
    if (f.size() != 6) return 0;
    Row r; char* end = 0;
    for (int i = 0; i < 3; ++i)
    {
        if (f[2 + i].empty()) return 0;
        end = 0; const double d = std::strtod(f[2 + i].c_str(), &end);
        if (end == 0 || *end != 0) return 0;
        (i == 0 ? r.relation : i == 1 ? r.trust : r.trustNeg) = (float)d;
    }
    if (f[5].size() != 1 || f[5][0] < '0' || f[5][0] > '3') return 0;
    r.flags = (unsigned)(f[5][0] - '0');
    if (IsStandInSid(f[0]) || IsStandInSid(f[1])) { if (standIn) *standIn = 1; return 0; }
    return Merge(t, f[0], f[1], r, kWrChange) >= 0 ? 1 : 0;
}

// review-par24 #2 (the game side, MAIN THREAD): how many CHANGE sends of each pair the notebook has not answered yet (kind 4
// ECHO). A row for a pair with an unanswered send is not written into the engine - it can be older than this game's own
// change (its own earlier echo, or a seed) - until the echo of the LAST send is back; the table's row is then written as
// usual (it already holds every row the notebook took after that send, in the order it took them).
struct SendBook
{
    std::map<std::string, int> n;
    void Sent(const std::string& k) { ++n[k]; }
    /* true = that was the last unanswered send of k (or k had none on the book: an echo of a send made before a reset) */
    bool Echo(const std::string& k)
    {
        std::map<std::string, int>::iterator it = n.find(k);
        if (it == n.end()) return true;
        if (--it->second > 0) return false;
        n.erase(it);
        return true;
    }
    bool Waiting(const std::string& k) const { return n.find(k) != n.end(); }
    void Clear() { n.clear(); }
    size_t Size() const { return n.size(); }
};
// review-par24 #2: may the notebook's row for a pair be written into this game's engine now? Not while this game holds a
// newer change of that pair it has not sent (dirty), nor while a send of it is unanswered.
inline bool MayApply(bool dirtyHere, bool waitingEcho) { return !dirtyHere && !waitingEcho; }

// S2-62 (owner decision 2026-09-28; the notebook side): the world's own save sets the starting table. A SEED from the
// OPERATOR's game is taken (first writer wins, as always); another game's seed is HELD while the operator's game is linked
// and has not ended its walk (SEED_END); when it does, the held rows go through the ordinary first-writer merge AFTER the
// operator's rows (MergeHeld - re-check B: the operator's values win, a pair its save lacks is filled from the held rows in
// arrival order; nothing held is dropped); with the operator not linked the first-in rule applies (FALLBACK - the held rows
// are released the same way, in arrival order, first). After the operator's walk every seed is taken first-writer.
enum { kSdTake = 0, kSdHold = 1, kSdFallback = 2 };
inline int SeedDecision(bool fromOperator, bool operatorLinked, bool operatorSeeded)
{
    if (fromOperator || operatorSeeded) return kSdTake;
    return operatorLinked ? kSdHold : kSdFallback;
}
// re-check B: the held seed rows through the first-writer merge, in arrival order. *taken gets each row the table took (a
// pair it lacked); *same counts rows for a pair already held (the operator's value, or an earlier held row, stays); *refused
// counts bad rows and new pairs a full table cannot take.
inline void MergeHeld(Table* t, const std::vector<WireRow>& held, std::vector<WireRow>* taken, long long* same, long long* refused)
{
    for (size_t i = 0; i < held.size(); ++i)
    {
        const int r = Merge(t, held[i].a, held[i].b, held[i].row, kWrSeed);
        if (r < 0) { if (refused != 0) ++*refused; }
        else if (r == 0) { if (same != 0) ++*same; }
        else if (taken != 0) taken->push_back(held[i]);
    }
}

}   // namespace coopwrel
