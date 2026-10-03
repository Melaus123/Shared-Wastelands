/* src/common/researchwire.h - loot2b (docs/design-loot2.md section 2A item 1, 4A row "L2b' lift"; T361). RESEARCH ROWS.
 * The research items (item bool "artifact" +0x180: AI Core, Ancient Science Book, Engineering Research, Book) that the
 * HOLDER lifts out of a box when that box is opened (ask on open: the holder answers the ask, or its own player opens it)
 * become rows in the notebook's research_boxes.txt, keyed by box key + item index. FIRST WRITER WINS PER BOX: a box already
 * in the table is never lifted again, so a research item a player later puts into it stays a world item. A box that was
 * lifted and held no research item gets ONE marker line (index -1), so it is "lifted" too.
 *
 *   MSG_RESEARCH_BOX (store link 44)  u32 boxes (<= kResearchMaxBoxes) | boxes x (u8 len + key, u16 n (<= kResearchMaxRows)
 *                                     | n x (i32 index, u8 len + sid, u8 len + material sid, i32 level, f32 quality, i32 qty))
 *   Up (game -> notebook): one box, the lift just made. Down: every stored box at WELCOME (in chunks), and each NEW box on
 *   change, to every game.
 *   research_boxes.txt line: v1<TAB>key<TAB>index<TAB>sid<TAB>material<TAB>level<TAB>quality (%.9g)<TAB>qty
 *                            (a lifted box with no research item: index -1, sid and material empty, numbers 0)
 *
 * GENERATION (fold 2, review-loot2b 5b - owner rule "one copy per player of restocked books"): a shop counter is lifted
 * again after each restock under the key "<box key>#g<N>" (N = 1 + the highest generation the table holds for that box);
 * the generation rides INSIDE the key string, so neither the message nor the line changes shape (store protocol stays 47).
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes. C++03 (VS2010 v100).
 */
#pragma once

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "paritywire.h"

namespace coopres {

const unsigned int kResearchMaxBoxes = 4096;   /* boxes in one message; the notebook's WELCOME push sends chunks of 256 */
const unsigned int kResearchMaxRows  = 1024;   /* rows in one box (a box answer carries at most cooppar::kParityMaxItems) */

const int kResearchDecodeOk       = 0;
const int kResearchDecodeTooShort = 1;
const int kResearchDecodeTooMany  = 2;
const int kResearchDecodeTrailing = 3;
const int kResearchDecodeBadKey   = 4;   /* an empty key, or a tab / newline inside a string */

struct ResearchRow
{
    int index; std::string sid, material; int level; float quality; int qty;
    ResearchRow() : index(0), level(0), quality(0.0f), qty(0) {}
};
struct ResearchBox { std::string key; std::vector<ResearchRow> rows; };   /* rows empty = lifted, none found */
typedef std::map<std::string, std::vector<ResearchRow> > ResearchTable;

/* a string that can sit in one tab-separated line */
inline bool ResearchTextOk(const std::string& s)
{
    if (s.size() > cooppar::kParityStrMax) return false;
    for (size_t i = 0; i < s.size(); ++i) if (s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == 0) return false;
    return true;
}

/* false (nothing appended) on an empty or unfit key or string, or a count over its cap. */
inline bool EncodeResearchBoxes(std::vector<char>* b, const std::vector<ResearchBox>& boxes)
{
    if (b == 0 || boxes.size() > kResearchMaxBoxes) return false;
    std::vector<char> o;
    cooppar::ParityPutU32(&o, (unsigned int)boxes.size());
    for (size_t i = 0; i < boxes.size(); ++i)
    {
        const ResearchBox& x = boxes[i];
        if (x.key.empty() || !ResearchTextOk(x.key) || x.rows.size() > kResearchMaxRows) return false;
        cooppar::ParityPutStr(&o, x.key);
        const unsigned short n = (unsigned short)x.rows.size();
        cooppar::ParityPut(&o, &n, 2);
        for (size_t r = 0; r < x.rows.size(); ++r)
        {
            const ResearchRow& w = x.rows[r];
            if (!ResearchTextOk(w.sid) || !ResearchTextOk(w.material)) return false;
            cooppar::ParityPutI32(&o, w.index); cooppar::ParityPutStr(&o, w.sid); cooppar::ParityPutStr(&o, w.material);
            cooppar::ParityPutI32(&o, w.level); cooppar::ParityPutF32(&o, w.quality); cooppar::ParityPutI32(&o, w.qty);
        }
    }
    b->insert(b->end(), o.begin(), o.end());
    return true;
}
inline int DecodeResearchBoxes(const char* p, size_t size, std::vector<ResearchBox>* out)
{
    out->clear();
    cooppar::ParityCur c(p, size);
    unsigned int nb = 0;
    if (!c.U32(&nb)) return kResearchDecodeTooShort;
    if (nb > kResearchMaxBoxes) return kResearchDecodeTooMany;
    std::vector<ResearchBox> v;
    for (unsigned int i = 0; i < nb; ++i)
    {
        ResearchBox x; unsigned int nr = 0;
        if (!c.Str(&x.key) || !c.U16(&nr)) return kResearchDecodeTooShort;
        if (x.key.empty() || !ResearchTextOk(x.key)) return kResearchDecodeBadKey;
        if (nr > kResearchMaxRows) return kResearchDecodeTooMany;
        for (unsigned int r = 0; r < nr; ++r)
        {
            ResearchRow w;
            if (!c.I32(&w.index) || !c.Str(&w.sid) || !c.Str(&w.material) || !c.I32(&w.level) || !c.F32(&w.quality) || !c.I32(&w.qty))
                return kResearchDecodeTooShort;
            if (!ResearchTextOk(w.sid) || !ResearchTextOk(w.material)) return kResearchDecodeBadKey;
            x.rows.push_back(w);
        }
        v.push_back(x);
    }
    if (c.at != size) return kResearchDecodeTrailing;
    out->swap(v);
    return kResearchDecodeOk;
}

/* FIRST WRITER WINS: 1 = the box was new and is stored whole; 0 = the key was already there and nothing changed. */
inline int ResearchMerge(ResearchTable* t, const ResearchBox& b)
{
    if (b.key.empty() || t->find(b.key) != t->end()) return 0;
    (*t)[b.key] = b.rows;
    return 1;
}
/* research rows in a table (the index -1 markers are not rows) */
inline long long ResearchRowCount(const ResearchTable& t)
{
    long long n = 0;
    for (ResearchTable::const_iterator it = t.begin(); it != t.end(); ++it) n += (long long)it->second.size();
    return n;
}

/* the file's lines for one box: one per row, or the single index -1 marker for a box with none */
inline std::string ResearchLines(const std::string& key, const std::vector<ResearchRow>& rows)
{
    std::string s;
    char num[96];
    if (rows.empty()) { s += "v1\t" + key + "\t-1\t\t\t0\t0\t0\n"; return s; }
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const ResearchRow& w = rows[i];
        std::sprintf(num, "%d", w.index); s += "v1\t" + key + "\t" + num + "\t" + w.sid + "\t" + w.material + "\t";
        std::sprintf(num, "%d\t%.9g\t%d\n", w.level, (double)w.quality, w.qty); s += num;
    }
    return s;
}
/* one file line into the table (a key seen before gains the row; index -1 marks the box and adds nothing). 1 = used. */
inline int ResearchParseLine(const std::string& lineIn, ResearchTable* t)
{
    std::string line = lineIn;
    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    std::vector<std::string> f; size_t at = 0;
    while (at <= line.size()) { const size_t tb = line.find('\t', at); if (tb == std::string::npos) { f.push_back(line.substr(at)); break; } f.push_back(line.substr(at, tb - at)); at = tb + 1; }
    if (f.size() != 8 || f[0] != "v1" || f[1].empty()) return 0;
    /* review-loot2b 3a: every field must fit the wire (u8 lengths, no control characters) and a box stays within
       kResearchMaxRows, so one bad line is skipped here instead of failing a whole WELCOME chunk at encode time */
    if (!ResearchTextOk(f[1]) || !ResearchTextOk(f[3]) || !ResearchTextOk(f[4])) return 0;
    const int index = std::atoi(f[2].c_str());
    if (index < -1) return 0;
    ResearchTable::iterator have = t->find(f[1]);
    if (index >= 0 && have != t->end() && have->second.size() >= (size_t)kResearchMaxRows) return 0;
    std::vector<ResearchRow>& rows = (*t)[f[1]];
    if (index < 0) return 1;
    ResearchRow w; w.index = index; w.sid = f[3]; w.material = f[4]; w.level = std::atoi(f[5].c_str());
    w.quality = (float)std::atof(f[6].c_str()); w.qty = std::atoi(f[7].c_str());
    rows.push_back(w);
    return 1;
}

/* loot2c (docs/design-loot2.md section 2 steps 3-5, 4A row "L2c' show/take"; T362) - TAKE ROWS.
 * A take row says ONE PLAYER took their own copy of ONE research row: (table key, row index, player id). The player id is
 * the slots.txt id (B13: the 32 hex characters of shared_wastelands.cfg), so it survives relinks. FIRST WRITER WINS per
 * (key, index, player): a row already there is never replaced. Giving a copy to a friend stays allowed (owner 2026-09-26):
 * nothing tracks an item after its take row.
 *
 *   MSG_RESEARCH_TAKE (store link 46)  u32 n (<= kResearchMaxTakes) | n x (u8 len + table key, i32 index (>= 0), u8 len + player)
 *   Up (game -> notebook): one take; the notebook refuses a row that names another player than the sender's own id.
 *   Down: every row at WELCOME (in chunks), and each NEW row on change, to every game.
 *   research_takes.txt line: v1<TAB>key<TAB>index<TAB>player
 */
const unsigned int kResearchMaxTakes = 4096;
struct ResearchTake { std::string key; int index; std::string player; ResearchTake() : index(0) {} };
typedef std::map<std::string, ResearchTake> ResearchTakeTable;   /* ResearchTakeId -> the row */

inline bool ResearchTakeOk(const ResearchTake& t)
{
    return !t.key.empty() && ResearchTextOk(t.key) && t.index >= 0 && !t.player.empty() && ResearchTextOk(t.player);
}
inline std::string ResearchTakeId(const ResearchTake& t)
{
    char num[16]; std::sprintf(num, "%d", t.index);
    return t.key + "\t" + num + "\t" + t.player;
}
/* false (nothing appended) on an unfit row or a count over its cap */
inline bool EncodeResearchTakes(std::vector<char>* b, const std::vector<ResearchTake>& v)
{
    if (b == 0 || v.size() > kResearchMaxTakes) return false;
    std::vector<char> o;
    cooppar::ParityPutU32(&o, (unsigned int)v.size());
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (!ResearchTakeOk(v[i])) return false;
        cooppar::ParityPutStr(&o, v[i].key); cooppar::ParityPutI32(&o, v[i].index); cooppar::ParityPutStr(&o, v[i].player);
    }
    b->insert(b->end(), o.begin(), o.end());
    return true;
}
inline int DecodeResearchTakes(const char* p, size_t size, std::vector<ResearchTake>* out)
{
    out->clear();
    cooppar::ParityCur c(p, size);
    unsigned int n = 0;
    if (!c.U32(&n)) return kResearchDecodeTooShort;
    if (n > kResearchMaxTakes) return kResearchDecodeTooMany;
    std::vector<ResearchTake> v;
    for (unsigned int i = 0; i < n; ++i)
    {
        ResearchTake t;
        if (!c.Str(&t.key) || !c.I32(&t.index) || !c.Str(&t.player)) return kResearchDecodeTooShort;
        if (!ResearchTakeOk(t)) return kResearchDecodeBadKey;
        v.push_back(t);
    }
    if (c.at != size) return kResearchDecodeTrailing;
    out->swap(v);
    return kResearchDecodeOk;
}
/* FIRST WRITER WINS: 1 = a new row, stored; 0 = already there (or unfit), nothing changed */
inline int ResearchTakeMerge(ResearchTakeTable* t, const ResearchTake& r)
{
    if (!ResearchTakeOk(r)) return 0;
    const std::string id = ResearchTakeId(r);
    if (t->find(id) != t->end()) return 0;
    (*t)[id] = r;
    return 1;
}
inline std::string ResearchTakeLine(const ResearchTake& r) { return "v1\t" + ResearchTakeId(r) + "\n"; }
/* one file line (without its newline; a trailing CR is dropped): 1 = a fit row into *out, 0 = unusable */
inline int ResearchTakeParseLine(const std::string& lineIn, ResearchTake* out)
{
    std::string line = lineIn;
    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    std::vector<std::string> f; size_t at = 0;
    while (at <= line.size()) { const size_t tb = line.find('\t', at); if (tb == std::string::npos) { f.push_back(line.substr(at)); break; } f.push_back(line.substr(at, tb - at)); at = tb + 1; }
    if (f.size() != 4 || f[0] != "v1" || f[2].empty() || f[2].size() > 9) return 0;
    for (size_t i = 0; i < f[2].size(); ++i) if (f[2][i] < '0' || f[2][i] > '9') return 0;
    ResearchTake r; r.key = f[1]; r.index = std::atoi(f[2].c_str()); r.player = f[3];
    if (!ResearchTakeOk(r)) return 0;
    *out = r;
    return 1;
}

} // namespace coopres
