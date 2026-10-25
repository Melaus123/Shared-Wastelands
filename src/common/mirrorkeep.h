#pragma once
/* T-985 C1: A GAME'S RECORD TABLE, KEPT BETWEEN LAUNCHES - the pure half (store.cpp does the files).
   A game keeps its own copy (the mirror) of the world server's records inside the world's folder, and a table in memory of
   each record's stamps. The mirror folder was named after the process (mirror-<pid>, since P1): two games on one computer can
   share one world folder (the test benches put both games' records under one storedir=), and the process id kept their files
   apart. A new process meant an empty table, so every launch downloaded every record again.
   THE NAME is now mirror-k<16 hex>: a hash of the world's id and the id the world knows this player by. It is the same for the
   same player in the same world after a restart, and different for another player (the protection the process id gave) and for
   another world or a reset one (a reset gives the world a new id). Two games of ONE player on one computer would share it, so
   store.cpp holds a lock file in it for the life of the process and the second game falls back to the old per-process name.
   THE TABLE (records-kept.txt in that folder) is written whole, by rename, at natural points (the records before the load are
   in; a save finished). It names its world and its identity hash in its first line and ends in a line with its row count and a
   checksum of everything above it, so a table of another world, another player, or a torn or damaged table is refused whole.
   EACH ROW names the size and last-write time of the record's mirror file as it was when the row was written (fold 1, review MED
   5: no CRC - see RowCheck). A row is taken back only when the file is there with that size and time - so the table never claims
   a record this game does not hold (the HELD list the game sends is read from this table, and a false claim would hide that
   record for good).
   FOLD 1 (review 2026-10-24): a row taken back is only this game's word. Until the world server names the key (its preload index,
   a fetch, or a record of the first whole feed snapshot) nothing made from it is woken or placed (KeptHeldBack); a key the first
   whole snapshot did not name is dropped (UnnamedDecide). Two keys that share one mirror file name are not listed (NameClashMarks).
   Pure: no files, no Windows calls. C++03. */
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <map>

namespace mirrorkeep {

const char* const kTableFile = "records-kept.txt";
const char* const kLockFile = "in-use.lock";
const char* const kTableMagic = "swkept";
const unsigned int kTableVersion = 1u;

/* FNV-1a, 64 bits (offset basis 14695981039346656037, prime 1099511628211 - written in decimal on purpose) */
inline unsigned long long Fnv64(const std::string& s)
{
    unsigned long long h = 14695981039346656037ULL;
    for (size_t i = 0; i < s.size(); ++i) { h ^= (unsigned long long)(unsigned char)s[i]; h *= 1099511628211ULL; }
    return h;
}
inline std::string Hex16(unsigned long long v) { char b[24]; std::sprintf(b, "%016llx", v); return std::string(b); }
inline std::string Hex8(unsigned int v) { char b[16]; std::sprintf(b, "%08x", v); return std::string(b); }
inline std::string U64Text(unsigned long long v) { char b[32]; std::sprintf(b, "%llu", v); return std::string(b); }
inline std::string I64Text(long long v) { char b[32]; std::sprintf(b, "%lld", v); return std::string(b); }

/* THE FOLDER NAME. The identity hash covers the world id and the player id; the player id itself never appears in a name or a log. */
inline std::string IdentityHash(const std::string& worldId, const std::string& personId) { return Hex16(Fnv64(worldId + "\n" + personId)); }
inline std::string StableDirName(const std::string& idHash) { return "mirror-k" + idHash; }
inline std::string ProcessDirName(unsigned long pid) { char b[32]; std::sprintf(b, "mirror-%lu", pid); return std::string(b); }
enum { kNameStable = 0, kNameNoWorldId = 1, kNameNoPlayer = 2 };
/* a stable name needs both ids: with no world id a remade world of the same name could not be told apart */
inline int NameDecide(int worldIdOk, int playerIdOk)
{
    if (!worldIdOk) return kNameNoWorldId;
    if (!playerIdOk) return kNameNoPlayer;
    return kNameStable;
}

/* WHY the table was or was not used this launch (the report's keptTable why field) */
enum { kWhyKept = 0, kWhyNotArmed = 1, kWhyNoWorldId = 2, kWhyNoPlayer = 3, kWhyInUse = 4, kWhyNoTable = 5, kWhyBadHeader = 6,
       kWhyOtherWorld = 7, kWhyOtherPlayer = 8, kWhyTorn = 9,
       kWhyServerBehind = 10, kWhyUnjudged = 11 };
inline const char* WhyName(int w)
{
    switch (w)
    {
    case kWhyKept: return "kept";
    case kWhyNotArmed: return "not armed";
    case kWhyNoWorldId: return "the world server named no world id";
    case kWhyNoPlayer: return "no valid player id";
    case kWhyInUse: return "the kept folder is in use by another game";
    case kWhyNoTable: return "no kept table yet";
    case kWhyBadHeader: return "the table's first line is not a kept-table header";
    case kWhyOtherWorld: return "the table belongs to another world";
    case kWhyOtherPlayer: return "the table belongs to another player";
    case kWhyTorn: return "the table is torn or damaged";
    case kWhyServerBehind: return "serverBehind: the world server's seqHigh is below the one the table was written at (restored from a backup)";
    case kWhyUnjudged: return "unjudged: no WELCOME carried the world server's seqHigh before the table's records would be used";
    }
    return "?";
}

/* ONE ROW: a record's table entry, and its mirror file as it was when the row was written */
struct KeptRow
{
    std::string key, squadSid, factionName, town, home;
    float x, y, z;
    long long writtenAt, posAt;
    unsigned int owner;
    unsigned long long seq;
    int birth;
    unsigned long long size, mtime;
    unsigned int crc;
    KeptRow() : x(0), y(0), z(0), writtenAt(0), posAt(0), owner(0), seq(0), birth(-1), size(0), mtime(0), crc(0) {}
};

inline bool FieldOk(const std::string& s) { return s.find_first_of("\t\r\n") == std::string::npos; }
inline unsigned int FloatBits(float f) { unsigned int u = 0; std::memcpy(&u, &f, 4); return u; }
inline float BitsFloat(unsigned int u) { float f = 0; std::memcpy(&f, &u, 4); return f; }
inline bool ParseU64(const std::string& s, unsigned long long* out)
{
    if (s.empty() || s.size() > 20) return false;
    unsigned long long v = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c < '0' || c > '9') return false;
        const unsigned long long d = (unsigned long long)(c - '0');
        if (v > (18446744073709551615ULL - d) / 10ULL) return false;
        v = v * 10ULL + d;
    }
    *out = v;
    return true;
}
inline bool ParseI64(const std::string& s, long long* out)
{
    const bool neg = !s.empty() && s[0] == '-';
    unsigned long long u = 0;
    if (!ParseU64(neg ? s.substr(1) : s, &u)) return false;
    if (!neg && u > 9223372036854775807ULL) return false;
    if (neg && u > 9223372036854775808ULL) return false;
    *out = neg ? (u == 9223372036854775808ULL ? (-9223372036854775807LL - 1LL) : -(long long)u) : (long long)u;
    return true;
}
inline bool ParseHex8(const std::string& s, unsigned int* out)
{
    if (s.size() != 8) return false;
    unsigned int v = 0;
    for (size_t i = 0; i < 8; ++i)
    {
        const char c = s[i]; unsigned int d = 0;
        if (c >= '0' && c <= '9') d = (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f') d = 10u + (unsigned int)(c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10u + (unsigned int)(c - 'A');
        else return false;
        v = (v << 4) | d;
    }
    *out = v;
    return true;
}
inline void SplitTabs(const std::string& line, std::vector<std::string>* f)
{
    f->clear();
    size_t at = 0;
    for (;;)
    {
        const size_t t = line.find('\t', at);
        if (t == std::string::npos) { f->push_back(line.substr(at)); return; }
        f->push_back(line.substr(at, t - at));
        at = t + 1;
    }
}

/* "r <key> <writtenAt> <posAt> <owner> <x> <y> <z> <seq> <birth> <size> <mtime> <crc> <squadSid> <faction> <town> <home>", tabs between.
   x, y, z are the float's bits in hex (exact). false = not written: an empty key, a tab or line break in a text field, or no file stamp. */
const size_t kRowFields = 17;
inline bool RowFormat(const KeptRow& r, std::string* line)
{
    if (r.key.empty() || r.writtenAt <= 0) return false;
    if (!FieldOk(r.key) || !FieldOk(r.squadSid) || !FieldOk(r.factionName) || !FieldOk(r.town) || !FieldOk(r.home)) return false;
    *line = "r\t" + r.key + "\t" + I64Text(r.writtenAt) + "\t" + I64Text(r.posAt) + "\t" + U64Text(r.owner)
          + "\t" + Hex8(FloatBits(r.x)) + "\t" + Hex8(FloatBits(r.y)) + "\t" + Hex8(FloatBits(r.z))
          + "\t" + U64Text(r.seq) + "\t" + I64Text(r.birth) + "\t" + U64Text(r.size) + "\t" + U64Text(r.mtime) + "\t" + Hex8(r.crc)
          + "\t" + r.squadSid + "\t" + r.factionName + "\t" + r.town + "\t" + r.home + "\n";
    return true;
}
inline bool RowParse(const std::string& line, KeptRow* out)
{
    std::vector<std::string> f;
    SplitTabs(line, &f);
    if (f.size() != kRowFields || f[0] != "r" || f[1].empty()) return false;
    KeptRow r;
    unsigned long long owner = 0; unsigned int xb = 0, yb = 0, zb = 0; long long birth = 0;
    r.key = f[1];
    if (!ParseI64(f[2], &r.writtenAt) || r.writtenAt <= 0 || !ParseI64(f[3], &r.posAt)) return false;
    if (!ParseU64(f[4], &owner) || owner > 4294967295ULL) return false;
    if (!ParseHex8(f[5], &xb) || !ParseHex8(f[6], &yb) || !ParseHex8(f[7], &zb)) return false;
    if (!ParseU64(f[8], &r.seq) || !ParseI64(f[9], &birth) || birth < -2147483647LL || birth > 2147483647LL) return false;
    if (!ParseU64(f[10], &r.size) || !ParseU64(f[11], &r.mtime) || !ParseHex8(f[12], &r.crc)) return false;
    r.owner = (unsigned int)owner; r.birth = (int)birth;
    r.x = BitsFloat(xb); r.y = BitsFloat(yb); r.z = BitsFloat(zb);
    r.squadSid = f[13]; r.factionName = f[14]; r.town = f[15]; r.home = f[16];
    *out = r;
    return true;
}

/* THE TABLE: "swkept 1 <worldId> <idHash>", the rows, then "end <rows> <FNV-64 hex of every byte above this line>". false = no table
   (no world id or identity, or one with a tab or line break). *refused counts rows RowFormat would not write. */
/* T-985 C1 fold 1 (finding 3): seenSeq > 0 adds a fifth header field - the highest world sequence number this game had seen when it
   wrote the table (store.cpp g_wgSeqSeen, what restoreguard::NotebookBehindLive compares). 0 writes the C1 header. */
inline bool TableFormatSeq(const std::string& worldId, const std::string& idHash, unsigned long long seenSeq, const std::vector<KeptRow>& rows, std::string* out, long long* refused)
{
    if (worldId.empty() || idHash.empty() || !FieldOk(worldId) || !FieldOk(idHash)) return false;
    std::string t = std::string(kTableMagic) + "\t" + U64Text(kTableVersion) + "\t" + worldId + "\t" + idHash + (seenSeq > 0ULL ? "\t" + U64Text(seenSeq) : std::string()) + "\n";
    unsigned long long n = 0;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        std::string line;
        if (!RowFormat(rows[i], &line)) { if (refused != 0) ++*refused; continue; }
        t += line; ++n;
    }
    t += "end\t" + U64Text(n) + "\t" + Hex16(Fnv64(t)) + "\n";
    *out = t;
    return true;
}
inline bool TableFormat(const std::string& worldId, const std::string& idHash, const std::vector<KeptRow>& rows, std::string* out, long long* refused)
{
    return TableFormatSeq(worldId, idHash, 0ULL, rows, out, refused);
}
enum { kTableOk = 0, kTableBadHeader = 1, kTableOtherWorld = 2, kTableOtherPlayer = 3, kTableTorn = 4 };
inline int WhyOfTable(int v)
{
    switch (v)
    {
    case kTableOk: return kWhyKept;
    case kTableBadHeader: return kWhyBadHeader;
    case kTableOtherWorld: return kWhyOtherWorld;
    case kTableOtherPlayer: return kWhyOtherPlayer;
    }
    return kWhyTorn;
}
/* Read a table for (worldId, idHash). Anything but kTableOk leaves *rows empty: the whole table is refused. A row line that does not
   parse inside an intact table (a later build's row) is skipped and counted in *badLines. */
inline int TableParseSeq(const std::string& text, const std::string& worldId, const std::string& idHash, std::vector<KeptRow>* rows, long long* badLines,
                         unsigned long long* seenSeq)
{
    rows->clear();
    if (seenSeq != 0) *seenSeq = 0ULL;
    const size_t nl = text.find('\n');
    if (nl == std::string::npos) return kTableBadHeader;
    std::vector<std::string> h;
    SplitTabs(text.substr(0, nl), &h);
    if ((h.size() != 4 && h.size() != 5) || h[0] != kTableMagic || h[1] != U64Text(kTableVersion)) return kTableBadHeader;
    unsigned long long seq = 0ULL;   /* fold 1 (finding 3): a C1 table has no fifth field - its seq is 0 and it is never judged behind */
    if (h.size() == 5 && (!ParseU64(h[4], &seq) || seq == 0ULL)) return kTableBadHeader;
    if (worldId.empty() || h[2] != worldId) return kTableOtherWorld;
    if (idHash.empty() || h[3] != idHash) return kTableOtherPlayer;
    const std::string::size_type endAt = text.rfind("\nend\t");
    if (endAt == std::string::npos || endAt + 1 <= nl) return kTableTorn;
    const std::string endLine = text.substr(endAt + 1);
    if (endLine.empty() || endLine[endLine.size() - 1] != '\n') return kTableTorn;
    std::vector<std::string> e;
    SplitTabs(endLine.substr(0, endLine.size() - 1), &e);
    unsigned long long count = 0;
    if (e.size() != 3 || e[0] != "end" || !ParseU64(e[1], &count)) return kTableTorn;
    if (e[2] != Hex16(Fnv64(text.substr(0, endAt + 1)))) return kTableTorn;
    std::vector<KeptRow> got;
    unsigned long long lines = 0; long long bad = 0;
    size_t at = nl + 1;
    while (at < endAt + 1)
    {
        const size_t lf = text.find('\n', at);
        const std::string line = text.substr(at, lf - at);
        at = lf + 1;
        ++lines;
        KeptRow r;
        if (RowParse(line, &r)) got.push_back(r); else ++bad;
    }
    if (lines != count) return kTableTorn;
    if (badLines != 0) *badLines += bad;
    if (seenSeq != 0) *seenSeq = seq;
    rows->swap(got);
    return kTableOk;
}
inline int TableParse(const std::string& text, const std::string& worldId, const std::string& idHash, std::vector<KeptRow>* rows, long long* badLines)
{
    return TableParseSeq(text, worldId, idHash, rows, badLines, 0);
}

/* A ROW AGAINST ITS FILE NOW: taken only when the file is there with the size and last-write time the row recorded.
   Fold 1 (review MED 5): NO CRC. Records average about 49 KB, so checking each file's CRC-32 read about 39 MB on the main thread at
   the WELCOME for 800 records, and every table write read each changed file again. A write to the file moves its last-write time
   (100 ns steps), so size + time is the check. The row's crc field stays in the format (written 00000000, never read), so a table
   written by C1 still reads. */
enum { kRowOk = 0, kRowNoFile = 1, kRowChanged = 2 };
inline int RowCheck(const KeptRow& r, int fileThere, unsigned long long size, unsigned long long mtime)
{
    if (!fileThere) return kRowNoFile;
    if (size != r.size || mtime != r.mtime) return kRowChanged;
    return kRowOk;
}
inline const char* RowCheckName(int v)
{
    return v == kRowOk ? "ok" : v == kRowNoFile ? "its mirror file is gone" : v == kRowChanged ? "its mirror file changed after the table was written" : "?";
}
/* a row read back replaces what the table already holds only when it is newer (LoadIndex's own rule) */
inline int RowTakes(int haveHeld, long long heldWrittenAt, long long rowWrittenAt) { return (!haveHeld || heldWrittenAt < rowWrittenAt) ? 1 : 0; }
/* T-985 C1 fold 1 (finding 3): THE WORLD SERVER STEPPED BACK. 1 = the WELCOME's seqHigh (welcomeKnown) is below the highest world
   sequence number stored in the table: the world server was restored from a backup, its stamps are older than the kept ones and
   the kept copy would win - the whole table is refused. A table with no stored seq (storedSeq 0, a C1 table) is never refused here. */
inline int ServerBehind(unsigned long long storedSeq, int welcomeKnown, unsigned long long welcomeSeq)
{
    return (storedSeq > 0ULL && welcomeKnown && welcomeSeq < storedSeq) ? 1 : 0;
}
/* at that refusal, one key taken from the table: dropped while its record is still the table's copy; one re-fetched (or written here)
   since has another writtenAt and stays; one already gone is left */
inline int BehindDrops(int stillHere, long long writtenAtNow, long long writtenAtKept) { return (stillHere && writtenAtNow == writtenAtKept) ? 1 : 0; }
/* which table entries are written: a live record with a file stamp whose file is this game's own mirror copy, not deleted here */
inline int RowListed(int gone, long long writtenAt, int fileIsMirror, int deletedHere)
{
    return (!gone && writtenAt > 0 && fileIsMirror && !deletedHere) ? 1 : 0;
}

/* ---- T-985 C1 fold 1 (review 2026-10-24) ---- */
/* THE LOAD ORDER for one row (store.cpp MirrorKeepLoad): a key deleted here, or an area record while area records are off, is skipped
   before its file is looked at; then the file (RowCheck); then RowTakes. rowCheck is not read for a skipped row. */
enum { kLoadTaken = 0, kLoadSkipped = 1, kLoadRejected = 2, kLoadNotNewer = 3 };
inline int RowLoad(int deletedHere, int zoneOff, int rowCheck, int haveHeld, long long heldWrittenAt, long long rowWrittenAt)
{
    if (deletedHere || zoneOff) return kLoadSkipped;
    if (rowCheck != kRowOk) return kLoadRejected;
    if (RowTakes(haveHeld, heldWrittenAt, rowWrittenAt) == 0) return kLoadNotNewer;
    return kLoadTaken;
}
/* review LOW 8: only a row taken notes its town and home - a row skipped as not newer leaves the notes the table's own record made */
inline int RowLoadNotes(int load) { return load == kLoadTaken ? 1 : 0; }

/* review HIGH 1: a key brought back from the table and not yet named by the world server (its preload index, a fetch, or a record of
   a feed snapshot) is held back - nothing is woken or placed from it - until the first whole snapshot has ended. */
inline int KeptHeldBack(int awaitingConfirm, int snapshotDone) { return (awaitingConfirm && !snapshotDone) ? 1 : 0; }

/* review HIGH 1: AT THE END OF THE FIRST WHOLE FEED SNAPSHOT, one key brought back from the table. The world server keeps only the
   HELD keys it still holds, and its snapshot lists every record it holds, so a kept key the snapshot did not name was dropped there
   while this player was away (a discard delete leaves no deleted mark): it is dropped here too (kUnnamedDrop). VALID ONLY WHILE
   THAT SNAPSHOT IS WHOLE (snapshotWhole): a filtered snapshot (step C2) names only part of the world and must not drop the rest.
   A key already gone here is left; a key whose record changed here since the load (this game wrote it) is kept and counted. */
enum { kUnnamedKeep = 0, kUnnamedDrop = 1, kUnnamedChangedHere = 2 };
inline int UnnamedDecide(int snapshotWhole, int named, int stillHere, long long writtenAtNow, long long writtenAtKept)
{
    if (!snapshotWhole || named || !stillHere) return kUnnamedKeep;
    if (writtenAtNow != writtenAtKept) return kUnnamedChangedHere;
    return kUnnamedDrop;
}

/* review MED 4: TWO KEYS, ONE FILE. A record's file name turns every unsafe character into '_' (writequeue.h FileName) and Windows
   ignores case, so two keys can name one mirror file, and its size and time would vouch for both. (*clash)[i] = 1 when names[i],
   lower-cased (ASCII), equals another entry's; neither is listed. Returns how many entries clash. */
inline std::string LowerAscii(const std::string& s)
{
    std::string l(s);
    for (size_t i = 0; i < l.size(); ++i) if (l[i] >= 'A' && l[i] <= 'Z') l[i] = (char)(l[i] - 'A' + 'a');
    return l;
}
inline long long NameClashMarks(const std::vector<std::string>& names, std::vector<int>* clash)
{
    std::vector<std::string> low(names.size());
    std::map<std::string, int> seen;
    for (size_t i = 0; i < names.size(); ++i) { low[i] = LowerAscii(names[i]); ++seen[low[i]]; }
    clash->assign(names.size(), 0);
    long long n = 0;
    for (size_t i = 0; i < names.size(); ++i) if (seen[low[i]] > 1) { (*clash)[i] = 1; ++n; }
    return n;
}

/* A ROW'S FILE STAMP (store.cpp MirrorKeepWrite): the size and last-write time of the row's own mirror file, taken just before; the
   crc field stays 0 (the file is not read). */
inline void RowStamp(KeptRow* r, unsigned long long size, unsigned long long mtime) { r->size = size; r->mtime = mtime; r->crc = 0; }
/* MirrorKeepWrite's last step. rows[i] was stamped from names[i] (its own mirror file). The rows whose file name clashes with no other
   entry's (NameClashMarks) are copied to *listed in order, each with its own stamp; *clashAt gets the indexes left out. Returns how
   many clash, or -1 with nothing listed when the two lists are not the same length (a row could then carry another file's stamp). */
inline long long ListUnclashed(const std::vector<KeptRow>& rows, const std::vector<std::string>& names, std::vector<KeptRow>* listed, std::vector<size_t>* clashAt)
{
    listed->clear(); clashAt->clear();
    if (rows.size() != names.size()) return -1;
    std::vector<int> clash;
    const long long n = NameClashMarks(names, &clash);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (clash[i] == 0) listed->push_back(rows[i]);
        else clashAt->push_back(i);
    }
    return n;
}

/* WAS THE SNAPSHOT SEEN WHOLE (the unnamed-key drop's snapshotWhole, store.cpp MirrorKeepSnapshotEnd). The wire has no truncation
   flag; the PAGE_END marker carries its page's record count, and the BEGIN the world server's record total. beginTotal = the BEGIN's
   total, sentTotal = the records every PAGE_END of this snapshot counted, lastSent = the LAST page's count. 0 (not whole) when:
   - the LAST page carried no record while earlier pages carried some: a page walk stops only where another record follows, so an
     empty last page after full ones is the world server's answer to a key too long for the wire (its records went, its PAGE_END
     did not count them, and the keys after it were never sent) - or records deleted between two pages;
   - the pages carried fewer records than the BEGIN said (a cut page, or records deleted while the snapshot was paged).
   A record added while the snapshot was paged only raises sentTotal. A snapshot not whole drops no unnamed key: those keys stay
   until named or re-fetched. */
inline int SnapshotWholeForDrop(long long beginTotal, long long sentTotal, long long lastSent)
{
    if (lastSent == 0 && sentTotal > 0) return 0;
    if (sentTotal < beginTotal) return 0;
    return 1;
}

/* THE TABLE'S STORED SEQ AGAINST THE WORLD SERVER (store.cpp MirrorKeepWelcomeSeq / MirrorKeepSnapshotEnd), judged once, while every
   kept key is still held back. storedSeq = the table header's seq (0: none, a C1 table - never refused here); welcomeKnown /
   welcomeSeq = the WELCOME's world numbers. A WELCOME with no world numbers cannot rule out a world server restored from a backup,
   and the preload that WELCOME starts names (so releases) kept keys before a later WELCOME could judge them: a table with a stored
   seq is refused then (kSeqRefuseUnjudged) rather than used unjudged. */
enum { kSeqKeep = 0, kSeqRefuseBehind = 1, kSeqRefuseUnjudged = 2 };
inline int SeqJudge(unsigned long long storedSeq, int welcomeKnown, unsigned long long welcomeSeq)
{
    if (storedSeq == 0ULL) return kSeqKeep;
    if (welcomeKnown) return ServerBehind(storedSeq, 1, welcomeSeq) != 0 ? kSeqRefuseBehind : kSeqKeep;
    return kSeqRefuseUnjudged;
}

/* A KEPT ROW BESIDE THE INDEX-LOAD RECORD (store.cpp MirrorKeepLoad). A wake cannot wait for the world server, so a kept row taken
   back does not replace the record LoadIndex gave the same key: it is held beside it until the world server names the key, and every
   road (the load and wake swaps, re-creation, placement) keeps reading the index-load record meanwhile. A key with no index-load
   record takes the row as before (held back; the save's copy meanwhile). An AREA record is never held beside: LoadIndex copies the
   world server's area file onto the one mirror file name the kept row also names, so the two cannot be held apart. */
enum { kKeptNotTaken = 0, kKeptReplace = 1, kKeptBeside = 2 };
inline int KeptPlace(int load, int haveIndexRecord, int isArea)
{
    if (load != kLoadTaken) return kKeptNotTaken;
    if (haveIndexRecord && !isArea) return kKeptBeside;
    return kKeptReplace;
}
/* when the world server names the key: the row held beside replaces the record there only when newer (RowTakes); one gone since takes nothing */
inline int BesideSwapIn(int stillHere, long long writtenAtNow, long long besideWrittenAt)
{
    return (stillHere && RowTakes(1, writtenAtNow, besideWrittenAt) != 0) ? 1 : 0;
}
/* THE TABLE REFUSED WHOLE (store.cpp MirrorKeepRefuseTable), one key taken from it: dropped while its record is still the table's copy
   (BehindDrops) - unless the current snapshot has already named it: the world server holds that key, and its own record of it came
   or comes on that snapshot. */
inline int RefuseDrops(int stillHere, long long writtenAtNow, long long writtenAtKept, int namedThisSnapshot)
{
    return (!namedThisSnapshot && BehindDrops(stillHere, writtenAtNow, writtenAtKept) != 0) ? 1 : 0;
}

}   /* namespace mirrorkeep */
