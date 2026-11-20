#pragma once
/* src/common/areareport.h - T-1402 (owner 1740 a, 2026-11-17): A BUG REPORT CARRIES THE STORED COPY OF THE REPORTER'S MAP SQUARE.
 *
 * Every game: its mirror copy of the area file, <world>\<mirror>\zone.X.Y.zone. The game that runs the world server also: that
 * square's world-server files (<id>.platoon) for the record kinds below, each checked against its <id>.meta line (who wrote it and
 * when, its length and checksum). The ring squares around it are only listed (names and record stamps), no files. Squad files are
 * never attached (character names and stats). Reading only: nothing is saved, nothing is written.
 *
 * Pure: names, the file list of one square, the .meta check's words and the budget part. No disk, no clock, no engine.
 */
#include <cstdio>
#include <string>
#include <vector>
#include "bugreport.h"   /* coopbug::BudgetPart, kOrderArea, kFinalBlock */
#include "storemeta.h"   /* coopstore::MetaParse, PayloadCheck, FileState */
#include "dupsquad.h"    /* dupsquad::Rec - an area file's records as the area records' own reader reads them (RecordsHolding) */

namespace areareport {

/* The record kinds of one square the report carries from the world server's folder, by their id's first word:
   zone.X.Y, ground.X.Y, then box. / door. / shop. / bldg. / cage. / towntime. X.Y.<more>. */
const int kKinds = 8;
inline const char* KindWord(int k)
{
    static const char* const w[kKinds] = { "zone", "ground", "box", "door", "shop", "bldg", "cage", "towntime" };
    return (k >= 0 && k < kKinds) ? w[k] : "";
}
/* the two kinds whose id ends at the square (zone.X.Y, ground.X.Y); the others go on after a '.' */
inline bool KindEndsAtSquare(int k) { return k == 0 || k == 1; }

/* A whole number at s[*at] ('-' allowed), at most 7 digits; *at moves past it. false = none there. */
inline bool ReadInt(const std::string& s, size_t* at, int* out)
{
    size_t i = *at;
    bool neg = false;
    if (i < s.size() && s[i] == '-') { neg = true; ++i; }
    const size_t d0 = i;
    long v = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9' && i - d0 < 7) { v = v * 10 + (s[i] - '0'); ++i; }
    if (i == d0 || (i < s.size() && s[i] >= '0' && s[i] <= '9')) return false;
    *out = (int)(neg ? -v : v);
    *at = i;
    return true;
}
/* The square and kind of a record id (or a world-server file's stem - the file name keeps letters, digits, '-' and '.'). 1 = one of
   the kinds above, with *kind, *sx, *sy; 0 = any other record (a squad, a town, a profile ...). */
inline int IdSquare(const std::string& id, int* kind, int* sx, int* sy)
{
    for (int k = 0; k < kKinds; ++k)
    {
        const std::string w = std::string(KindWord(k)) + ".";
        if (id.compare(0, w.size(), w) != 0) continue;
        size_t at = w.size();
        int x = 0, y = 0;
        if (!ReadInt(id, &at, &x) || at >= id.size() || id[at] != '.') return 0;
        ++at;
        if (!ReadInt(id, &at, &y)) return 0;
        if (KindEndsAtSquare(k) ? at != id.size() : (at >= id.size() || id[at] != '.')) return 0;
        *kind = k; *sx = x; *sy = y;
        return 1;
    }
    return 0;
}
/* A file name in the world server's folder: 1 = "<stem>.platoon" (the record's data), 2 = "<stem>.meta" (its index line), 0 = any
   other file (.prev, .tmp, the log, rules.txt ...). *stem without the suffix. */
inline int ServerFileKind(const std::string& name, std::string* stem)
{
    static const char kData[] = ".platoon", kMeta[] = ".meta";
    const size_t nd = sizeof(kData) - 1, nm = sizeof(kMeta) - 1;
    if (name.size() > nd && name.compare(name.size() - nd, nd, kData) == 0) { *stem = name.substr(0, name.size() - nd); return 1; }
    if (name.size() > nm && name.compare(name.size() - nm, nm, kMeta) == 0) { *stem = name.substr(0, name.size() - nm); return 2; }
    return 0;
}
/* THE FILE LIST FOR ONE SQUARE: of the world server's folder's file names, the stems of the records of square x,y (a .platoon or a
   .meta of one of the kinds), each once, zone first, then ground, then the rest in kind order and then name order. */
inline std::vector<std::string> SquareStems(const std::vector<std::string>& names, int x, int y)
{
    std::vector<std::vector<std::string> > byKind(kKinds);
    for (size_t i = 0; i < names.size(); ++i)
    {
        std::string stem;
        if (ServerFileKind(names[i], &stem) == 0) continue;
        int k = 0, sx = 0, sy = 0;
        if (!IdSquare(stem, &k, &sx, &sy) || sx != x || sy != y) continue;
        std::vector<std::string>& v = byKind[(size_t)k];
        bool seen = false;
        for (size_t j = 0; j < v.size() && !seen; ++j) seen = v[j] == stem;
        if (!seen) v.push_back(stem);
    }
    std::vector<std::string> out;
    for (int k = 0; k < kKinds; ++k)
    {
        std::vector<std::string>& v = byKind[(size_t)k];
        for (size_t a = 1; a < v.size(); ++a)   /* name order: a few files, an insertion sort */
            for (size_t b = a; b > 0 && v[b] < v[b - 1]; --b) v[b].swap(v[b - 1]);
        out.insert(out.end(), v.begin(), v.end());
    }
    return out;
}
/* The ring: the eight squares around x,y (the world server's delivery ring is one square), row by row. */
inline void RingSquares(int x, int y, std::vector<int>* xs, std::vector<int>* ys)
{
    xs->clear(); ys->clear();
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            if (dx == 0 && dy == 0) continue;
            xs->push_back(x + dx); ys->push_back(y + dy);
        }
}

/* Files attached and listed, at most: a square with more is listed to this many (the rest counted). An area file bigger than
   kFileMax raw bytes is not read (it could not fit the report compressed). */
const size_t kSquareFilesMax = 200;
const unsigned long long kFileMax = 48ull * 1024ull * 1024ull;

/* THE .meta CHECK of one world-server file: the first line of its .meta (`metaText`, "" = none or unreadable - metaState says which)
   against its .platoon (dataState, its length and CRC-32 when read). The answer: kCheckOk, kCheckNoPayload (the line says no bytes -
   a position-only note), kCheckNoMeta, kCheckMetaBad (the line does not parse), or the payload's own failure (missing, torn - the
   wrong length -, CRC wrong, unreadable). *m is the parsed line (who wrote it, when, its seq) when it parsed. */
enum { kCheckOk = 0, kCheckNoPayload = 1, kCheckNoMeta = 2, kCheckMetaBad = 3, kCheckMissing = 4, kCheckTorn = 5, kCheckCrcBad = 6, kCheckUnreadable = 7,
       kCheckChanged = 8 };   /* T-1402 fold 2: the .meta line read before and after the .platoon differed, on both tries - the world server
                                 wrote the record while it was read, so no check was made (never called torn) */
/* T-1402 fold 2: the .meta line read before the .platoon and again after it are one copy when both reads agree - the same state and the
   same bytes. */
inline bool MetaSameBothReads(int stateBefore, const std::string& before, int stateAfter, const std::string& after)
{
    return stateBefore == stateAfter && before == after;
}
inline std::string FirstLine(const std::string& t)
{
    const size_t e = t.find('\n');
    return e == std::string::npos ? t : t.substr(0, e);
}
inline int MetaCheck(int metaState, const std::string& metaText, int dataState, long long dataLen, unsigned int dataCrc, coopstore::MetaLine* m)
{
    if (metaState == coopstore::kFileAbsent) return kCheckNoMeta;
    if (metaState != coopstore::kFileReadable) return kCheckUnreadable;
    if (!coopstore::MetaParse(FirstLine(metaText), m) || m->version < 5) return kCheckMetaBad;   /* below v5 a line carries no length or checksum */
    switch (coopstore::PayloadCheck(m->len, m->crc, dataState, dataLen, dataCrc))
    {
    case coopstore::kPayloadOk:           return kCheckOk;
    case coopstore::kPayloadNoneExpected: return kCheckNoPayload;
    case coopstore::kPayloadMissing:      return kCheckMissing;
    case coopstore::kPayloadTorn:         return kCheckTorn;
    case coopstore::kPayloadCrcBad:       return kCheckCrcBad;
    default:                              return kCheckUnreadable;
    }
}
inline const char* CheckWords(int c)
{
    switch (c)
    {
    case kCheckOk:         return "check passed (length and checksum match its .meta line)";
    case kCheckNoPayload:  return "its .meta line says the record has no bytes (a position-only note)";
    case kCheckNoMeta:     return "CHECK FAILED: it has no .meta line";
    case kCheckMetaBad:    return "CHECK FAILED: its .meta line could not be read as one";
    case kCheckMissing:    return "CHECK FAILED: its .meta line names bytes and there is no .platoon file";
    case kCheckTorn:       return "CHECK FAILED: its length is not the one its .meta line names (a half-written file)";
    case kCheckCrcBad:     return "CHECK FAILED: its checksum is not the one its .meta line names";
    case kCheckChanged:    return "NOT CHECKED: it changed while it was read (its .meta line was not the same before and after its bytes were read, on two tries) - the bytes may be from either copy";
    default:               return "CHECK FAILED: a file could not be read";
    }
}
/* A record stamp in words: who wrote it (the slot) and when (writtenAt, seconds), with the world server's seq when known. */
inline std::string StampWords(int owner, long long writtenAt, unsigned long long seq)
{
    char b[128];
    if (owner < 0) std::sprintf(b, "written by an unknown game at %lld", writtenAt);
    else std::sprintf(b, "written by slot %d at %lld", owner, writtenAt);
    std::string s(b);
    if (seq != 0) { std::sprintf(b, ", world-server seq %llu", seq); s += b; }
    return s;
}

/* THE BUDGET PART of one area file: its bytes are ONE compressed segment (whole or not at all - a cut area file is no use);
   fixed = its zip entry's overhead and the final block. serverCopy 1 = a world-server copy: among area files those go first
   (cutFirst 1), the reporter's own stored square - the mirror file - last. */
inline coopbug::BudgetPart AreaPart(unsigned long long overhead, unsigned long long compressed, int serverCopy)
{
    coopbug::BudgetPart p;
    p.fixed = overhead + sizeof(coopbug::kFinalBlock);
    p.segBytes.push_back(compressed);
    p.order = coopbug::kOrderArea;
    p.cutFirst = serverCopy != 0 ? 1 : 0;
    p.cutFromEnd = false;
    return p;
}
/* T-1402 fold 3: THE AREA FILES' READ LIMIT - the area share of the report (coopbug::kAreaShare percent of kPackLimit), in compressed
   bytes. Once the area files already read come to this, no further one is read: pass 1 keeps at most that share of the room for
   them, so a further file would only be cut, and reading it costs the worker time and memory. It is listed 'not attached (size)'. */
inline unsigned long long AreaReadLimit() { return (unsigned long long)coopbug::kPackLimit * coopbug::kAreaShare / 100u; }
inline bool AreaReadFull(unsigned long long compressedSoFar) { return compressedSoFar >= AreaReadLimit(); }
/* T-1402 fix (run T2493, F1163): THE RECORDS THAT HOLD `text` - a record whose own id holds it, or any of its string values, reference
   ids, instance targets or squad entries. An item in a chest is its own record (id like 111939-zone20.32-INGAME) naming its kind in the
   string value 'base data sid' (2292-gamedata.base), so counting own ids alone counted 0 records with the item in the file. */
inline bool RecHolds(const dupsquad::Rec& r, const std::string& text)
{
    if (r.sid.find(text) != std::string::npos) return true;
    for (size_t i = 0; i < r.named.size(); ++i) if (r.named[i].find(text) != std::string::npos) return true;
    for (size_t i = 0; i < r.inst.size(); ++i) if (r.inst[i].find(text) != std::string::npos) return true;
    for (size_t e = 0; e < r.entries.size(); ++e)
        for (size_t i = 0; i < r.entries[e].size(); ++i) if (r.entries[e][i].find(text) != std::string::npos) return true;
    return false;
}
inline unsigned long long RecordsHolding(const std::vector<dupsquad::Rec>& recs, const std::string& text)
{
    unsigned long long n = 0;
    if (text.empty()) return 0;
    for (size_t i = 0; i < recs.size(); ++i) if (RecHolds(recs[i], text)) ++n;
    return n;
}
/* The zip names: under area/ (tools/fetch_reports.py unpacks them to unpacked/area/), the world server's under area/world-server/. */
inline std::string MirrorZipName(int x, int y) { char b[64]; std::sprintf(b, "area/zone.%d.%d.zone", x, y); return std::string(b); }
inline std::string ServerZipName(const std::string& stem) { return "area/world-server/" + stem + ".platoon"; }

}   /* namespace areareport */
