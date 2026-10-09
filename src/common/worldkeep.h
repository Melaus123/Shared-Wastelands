#pragma once
// KEEPING A WORLD'S PLAYERS - the pure rules behind the world server's backups, its start-up shrink check, moving a world
// between PCs and the alias table. Compiled into the world server (src/coop-store/store_main.cpp) and the offline suite.
//
// BACKUPS: before slots.txt, profiles.txt or aliases.txt is rewritten, the file on disk is copied to "<name>.1" (one rolling
// copy, BackupOf).
// THE SHRINK CHECK: at start-up slots.txt is compared with slots.txt.1. A player id the backup holds that slots.txt does not,
// or holds under a different number, is a player whose records would be handed to nobody or to somebody else, so the server
// refuses to start (kServerExitSlotsShrunk) unless the operator passes --accept-slots-shrink (SlotsShrinkDecide).
// MOVING A WORLD: --export copies a world folder's files into a new folder with manifest.txt - one line per file with its size
// and CRC-32 (coopstore::Crc32, the records' checksum) and an end line with the count. --import refuses unless every file the
// manifest names is there with that size and checksum and no other file is (ManifestVerify). ExportTakes says which files of
// a world folder travel: everything except the world lock, logs, temp files and the recycle folder.
// ALIASES: aliases.txt maps a player id the world has never seen (a reinstall made a new one) to the id the world knows that
// player by. The HELLO's id is resolved through it (AliasResolve) before anything else uses it, so the returning player gets
// their old slot, profiles and records. Both ids are 32-hex person ids (coopstore::PlayerIdOk); the old one must be a person
// slots.txt knows; an alias that would loop is refused (AliasAddDecide).
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "areaclaim.h"   /* coopstore::PlayerIdOk, SlotsLineParse */
#include "profiles.h"    /* coopprof::PersonOfKey - a slot key's person */
#include "diskformat.h"  /* swformat::kServerExitSlotsShrunk / kServerExitWorldAside - defined there, beside exit 5 */

namespace worldkeep {

/* ---- backups ---- */
inline std::string BackupOf(const std::string& path) { return path + ".1"; }

/* ---- the start-up shrink check ---- */
using swformat::kServerExitSlotsShrunk;   /* 33: the world server's exit code when it refuses to start over a shrunk slots.txt */
using swformat::kServerExitWorldAside;    /* 34: ... when its world folder is missing and an import's "<world>.importing" and
                                            "<world>.before-import-<time>" folders are both beside it (WorldAsideRefuses) */
const char* const kRefusedFile = "server-refused.txt";   /* <world folder>\server-refused.txt: a start refusal's sentence, appended with
                                                           its time - the panel starts the world server without a window */

/* slots.txt's lines as LoadSlots reads them: a line that does not parse is skipped; of two lines naming one id or one number
   the first stands. */
inline void SlotRowsParse(const std::vector<std::string>& lines, std::map<std::string, int>* out)
{
    std::map<int, int> used;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::string id; int slot = -1; long long seen = 0;
        if (!coopstore::SlotsLineParse(lines[i], &id, &slot, &seen)) continue;
        if (out->count(id) || used.count(slot)) continue;
        (*out)[id] = slot; used[slot] = 1;
    }
}

enum { kShrinkNone = 0, kShrinkNoBackup = 1, kShrinkRefuse = 2, kShrinkAccepted = 3 };
struct ShrinkReport
{
    int lost, moved, backupRows, currentRows;
    std::string firstLost;   /* the first id lost or moved, for the message */
    ShrinkReport() : lost(0), moved(0), backupRows(0), currentRows(0) {}
};

/* backupThere: slots.txt.1 could be read. A slots.txt that could not be read is passed as empty: every player the backup
   holds is then lost. */
inline int SlotsShrinkDecide(int backupThere, const std::map<std::string, int>& backup, const std::map<std::string, int>& current,
                             int accept, ShrinkReport* r)
{
    r->backupRows = (int)backup.size(); r->currentRows = (int)current.size();
    if (!backupThere) return kShrinkNoBackup;
    for (std::map<std::string, int>::const_iterator it = backup.begin(); it != backup.end(); ++it)
    {
        std::map<std::string, int>::const_iterator c = current.find(it->first);
        if (c == current.end()) ++r->lost;
        else if (c->second != it->second) ++r->moved;
        else continue;
        if (r->firstLost.empty()) r->firstLost = it->first;
    }
    if (r->lost == 0 && r->moved == 0) return kShrinkNone;
    return accept ? kShrinkAccepted : kShrinkRefuse;
}

/* ---- moving a world: the manifest ---- */
const char* const kManifestFile = "manifest.txt";
const char* const kManifestKind = "shared-wastelands-world-export";
const size_t kExportPathMax = 240;
const size_t kManifestRowsMax = 1000000;

struct FileRow { std::string path; unsigned long long size; unsigned int crc; FileRow() : size(0), crc(0) {} };

/* A path in a manifest: relative, its parts joined by '/', no empty, "." or ".." part, no ':' or '\\', no control byte. */
inline int ExportPathOk(const std::string& p)
{
    if (p.empty() || p.size() > kExportPathMax || p[0] == '/' || p[p.size() - 1] == '/') return 0;
    size_t from = 0;
    for (;;)
    {
        const size_t s = p.find('/', from);
        const std::string part = p.substr(from, s == std::string::npos ? std::string::npos : s - from);
        if (part.empty() || part == "." || part == "..") return 0;
        if (s == std::string::npos) break;
        from = s + 1;
    }
    for (size_t i = 0; i < p.size(); ++i)
    {
        const unsigned char c = (unsigned char)p[i];
        if (c < 0x20 || c == 0x7F || c == ':' || c == '\\') return 0;
    }
    return 1;
}

inline int EndsWith(const std::string& s, const char* tail)
{
    const std::string t(tail);
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}
inline std::string Lower(const std::string& s)
{
    std::string o(s);
    for (size_t i = 0; i < o.size(); ++i) if (o[i] >= 'A' && o[i] <= 'Z') o[i] = (char)(o[i] - 'A' + 'a');
    return o;
}

/* Does this entry of a world folder travel with the world? rel is its path below the world folder ('/'-joined). Everything
   does except the world lock, the logs, temp files, a manifest.txt at the top and the recycle folder (removed records). */
inline int ExportTakes(const std::string& rel, int isDir)
{
    const std::string l = Lower(rel);
    if (isDir) return l == "recycle" ? 0 : 1;
    if (l.find('/') == std::string::npos && (l == "server.lock" || l == kManifestFile)) return 0;
    if (EndsWith(l, ".log") || EndsWith(l, ".tmp")) return 0;
    return 1;
}

inline std::string Hex8(unsigned int v) { char b[16]; std::sprintf(b, "%08x", v); return b; }
inline std::string U64(unsigned long long v) { char b[32]; std::sprintf(b, "%llu", v); return b; }

inline std::string ManifestFormat(const std::vector<FileRow>& rows)
{
    std::string o = std::string("v1\t") + kManifestKind + "\n";
    for (size_t i = 0; i < rows.size(); ++i) o += "v1\tfile\t" + rows[i].path + "\t" + U64(rows[i].size) + "\t" + Hex8(rows[i].crc) + "\n";
    o += "v1\tend\t" + U64((unsigned long long)rows.size()) + "\n";
    return o;
}

inline void SplitTabs(const std::string& line, std::vector<std::string>* f)
{
    size_t from = 0;
    for (;;)
    {
        const size_t t = line.find('\t', from);
        f->push_back(line.substr(from, t == std::string::npos ? std::string::npos : t - from));
        if (t == std::string::npos) return;
        from = t + 1;
    }
}
inline int ParseU64(const std::string& s, unsigned long long* v)
{
    if (s.empty() || s.size() > 19) return 0;
    unsigned long long x = 0;
    for (size_t i = 0; i < s.size(); ++i) { if (s[i] < '0' || s[i] > '9') return 0; x = x * 10ULL + (unsigned long long)(s[i] - '0'); }
    *v = x; return 1;
}
inline int ParseHex8(const std::string& s, unsigned int* v)
{
    if (s.size() != 8) return 0;
    unsigned int x = 0;
    for (size_t i = 0; i < 8; ++i)
    {
        const char c = s[i]; unsigned d;
        if (c >= '0' && c <= '9') d = (unsigned)(c - '0'); else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10); else return 0;
        x = (x << 4) | d;
    }
    *v = x; return 1;
}

/* 1 when text is a whole manifest: the kind line, file lines, and an end line whose count matches, nothing after it. */
inline int ManifestParse(const std::string& text, std::vector<FileRow>* rows, std::string* why)
{
    std::vector<std::string> lines;
    size_t from = 0;
    while (from < text.size())
    {
        size_t e = text.find('\n', from); if (e == std::string::npos) e = text.size();
        std::string l = text.substr(from, e - from);
        if (!l.empty() && l[l.size() - 1] == '\r') l.erase(l.size() - 1);
        lines.push_back(l); from = e + 1;
    }
    if (lines.empty() || lines[0] != std::string("v1\t") + kManifestKind) { *why = "the first line is not a world export's"; return 0; }
    std::map<std::string, int> seen;
    for (size_t i = 1; i < lines.size(); ++i)
    {
        std::vector<std::string> f; SplitTabs(lines[i], &f);
        if (f.size() == 3 && f[0] == "v1" && f[1] == "end")
        {
            unsigned long long n = 0;
            if (!ParseU64(f[2], &n) || n != (unsigned long long)rows->size()) { *why = "the end line's count is not the number of file lines"; return 0; }
            for (size_t k = i + 1; k < lines.size(); ++k) if (!lines[k].empty()) { *why = "there are lines after the end line"; return 0; }
            return 1;
        }
        FileRow r;
        if (f.size() != 5 || f[0] != "v1" || f[1] != "file" || !ExportPathOk(f[2]) || !ParseU64(f[3], &r.size) || !ParseHex8(f[4], &r.crc))
        { *why = "line " + U64((unsigned long long)i + 1ULL) + " is not a file line"; return 0; }
        r.path = f[2];
        if (seen.count(Lower(r.path))) { *why = "'" + r.path + "' is named twice"; return 0; }
        if (rows->size() >= kManifestRowsMax) { *why = "too many file lines"; return 0; }
        seen[Lower(r.path)] = 1; rows->push_back(r);
    }
    *why = "there is no end line (the manifest is cut short)";
    return 0;
}

enum { kManOk = 0, kManEmpty = 1, kManMissing = 2, kManSize = 3, kManCrc = 4, kManExtra = 5 };
inline const char* ManifestVerdictName(int v)
{
    switch (v)
    {
    case kManOk: return "ok";
    case kManEmpty: return "the manifest names no files";
    case kManMissing: return "a file the manifest names is not there";
    case kManSize: return "a file's size is not the manifest's";
    case kManCrc: return "a file's checksum is not the manifest's";
    case kManExtra: return "a file is there that the manifest does not name";
    }
    return "?";
}
/* man: the manifest's rows; there: the files found beside it (manifest.txt itself left out), with their sizes and checksums.
   Paths compare without regard to letter case (Windows folders). */
inline int ManifestVerify(const std::vector<FileRow>& man, const std::vector<FileRow>& there, std::string* which)
{
    which->clear();
    if (man.empty()) return kManEmpty;
    std::map<std::string, size_t> at;
    for (size_t i = 0; i < there.size(); ++i) at[Lower(there[i].path)] = i;
    std::map<std::string, int> named;
    for (size_t i = 0; i < man.size(); ++i)
    {
        named[Lower(man[i].path)] = 1;
        std::map<std::string, size_t>::const_iterator t = at.find(Lower(man[i].path));
        *which = man[i].path;
        if (t == at.end()) return kManMissing;
        if (there[t->second].size != man[i].size) return kManSize;
        if (there[t->second].crc != man[i].crc) return kManCrc;
    }
    for (size_t i = 0; i < there.size(); ++i) if (!named.count(Lower(there[i].path))) { *which = there[i].path; return kManExtra; }
    which->clear();
    return kManOk;
}

/* ---- aliases ---- */
const char* const kAliasFile = "aliases.txt";
const size_t kAliasMax = 4096;

struct Alias { std::string from, to; long long setUnix; Alias() : setUnix(0) {} };   /* from: the new id; to: the id the world knows */

inline std::string AliasLineFormat(const Alias& a)
{
    char b[32]; std::sprintf(b, "%lld", a.setUnix);
    return "v1\t" + a.from + "\t" + a.to + "\t" + b;
}
inline int AliasLineParse(const std::string& line, Alias* a)
{
    std::vector<std::string> f; SplitTabs(line, &f);
    if (f.size() < 4 || f[0] != "v1" || !coopstore::PlayerIdOk(f[1]) || !coopstore::PlayerIdOk(f[2]) || f[1] == f[2]) return 0;
    a->from = f[1]; a->to = f[2]; a->setUnix = (long long)std::atof(f[3].c_str());
    return 1;
}

/* Follows the table from id; an id with no alias is itself. A loop (only a hand-edited file can hold one) resolves to id. */
inline std::string AliasResolve(const std::map<std::string, Alias>& t, const std::string& id)
{
    std::string cur = id;
    for (size_t step = 0; step <= t.size(); ++step)
    {
        std::map<std::string, Alias>::const_iterator it = t.find(cur);
        if (it == t.end()) return cur;
        cur = it->second.to;
    }
    return id;
}

/* A slot key ("<person>" or "<person>.<n>", the id a research take names): its person follows the table, its profile number stays. */
inline std::string AliasResolveKey(const std::map<std::string, Alias>& t, const std::string& key)
{
    return coopprof::SlotKey(AliasResolve(t, coopprof::PersonOfKey(key)), coopprof::NumOfKey(key));
}

/* A missing world folder is refused only when an import's staging copy ("<world>.importing") AND an earlier world it set aside
   ("<world>.before-import-<time>") are both beside it: the import stopped between setting the earlier world aside and putting the
   new copy in place (or could not put the earlier world back), so a fresh world here would hide both. A staging copy alone is an
   unfinished import whose source still holds the data; a set-aside world alone is one an import replaced. */
inline bool WorldAsideRefuses(bool worldExists, bool stagingExists, bool asideExists) { return !worldExists && stagingExists && asideExists; }

/* aliases.txt's lines into the table: a line that does not parse, names an id a second time, or would close a loop is dropped
   and counted in *bad. */
inline void AliasTableLoad(const std::vector<std::string>& lines, std::map<std::string, Alias>* t, int* bad)
{
    for (size_t i = 0; i < lines.size(); ++i)
    {
        Alias a;
        if (!AliasLineParse(lines[i], &a)) { if (!lines[i].empty()) ++*bad; continue; }
        if (t->count(a.from) || t->size() >= kAliasMax || AliasResolve(*t, a.to) == a.from) { ++*bad; continue; }
        (*t)[a.from] = a;
    }
}
inline std::string AliasTableFormat(const std::map<std::string, Alias>& t)
{
    std::string o;
    for (std::map<std::string, Alias>::const_iterator it = t.begin(); it != t.end(); ++it) o += AliasLineFormat(it->second) + "\n";
    return o;
}

/* Is a person (a 32-hex id) one this world's slots name - on any of their profiles? */
inline int PersonKnown(const std::map<std::string, int>& slots, const std::string& person)
{
    for (std::map<std::string, int>::const_iterator it = slots.begin(); it != slots.end(); ++it)
        if (coopprof::PersonOfKey(it->first) == person) return 1;
    return 0;
}

enum { kAliasOk = 0, kAliasBadId = 1, kAliasSame = 2, kAliasLoop = 3, kAliasOldUnknown = 4, kAliasFull = 5, kAliasNotThere = 6 };
inline const char* AliasVerdictText(int v)
{
    switch (v)
    {
    case kAliasOk: return "ok";
    case kAliasBadId: return "both ids must be 32 lowercase hex characters (the playerid line of shared_wastelands.cfg)";
    case kAliasSame: return "the two ids are the same";
    case kAliasLoop: return "the old id already leads back to the new one - that would be a loop";
    case kAliasOldUnknown: return "this world's slots.txt has never named the old id - nothing would come back";
    case kAliasFull: return "the alias table is full";
    case kAliasNotThere: return "the new id has no alias";
    }
    return "?";
}
/* Adding from -> to. slots: this world's slots.txt (the old id, followed through the table, must be a person it names). */
inline int AliasAddDecide(const std::map<std::string, Alias>& t, const std::string& from, const std::string& to, const std::map<std::string, int>& slots)
{
    if (!coopstore::PlayerIdOk(from) || !coopstore::PlayerIdOk(to)) return kAliasBadId;
    if (from == to) return kAliasSame;
    const std::string end = AliasResolve(t, to);
    if (end == from) return kAliasLoop;
    if (!PersonKnown(slots, end)) return kAliasOldUnknown;
    if (!t.count(from) && t.size() >= kAliasMax) return kAliasFull;
    return kAliasOk;
}

/* "<new>=<old>" from the command line. */
inline int AliasArgParse(const std::string& arg, std::string* from, std::string* to)
{
    const size_t e = arg.find('=');
    if (e == std::string::npos) return 0;
    *from = arg.substr(0, e); *to = arg.substr(e + 1);
    return coopstore::PlayerIdOk(*from) && coopstore::PlayerIdOk(*to);
}

}   /* namespace worldkeep */
