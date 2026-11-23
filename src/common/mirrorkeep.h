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
    unsigned long long mirrorSeq;   /* T-1429 s2 fold 5 (review F1378): the world server's version (seq) of the copy the mirror file holds -
                                       0 = not known (this game's own copy before its echo, or a row from a table written before fold 5) */
    int ownBaseKnown;               /* T-1391 F1631 item 1: the row is this game's own save the world server has not confirmed (mirrorSeq 0), and */
    unsigned long long ownBase;     /* the world version of the copy that save was made from (0 = made from no world copy) - so it survives a quit */
    std::vector<long long> unechoed;   /* T-1391 F1633 item 1: this game's uploads of the area whose echo had not come (oldest first) - so a refusal after a
                                          reopen that names one as the stored copy is still known as this game's own (store.cpp g_zoneUnechoed) */
    KeptRow() : x(0), y(0), z(0), writtenAt(0), posAt(0), owner(0), seq(0), birth(-1), size(0), mtime(0), crc(0), mirrorSeq(0), ownBaseKnown(0), ownBase(0) {}
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
   x, y, z are the float's bits in hex (exact). false = not written: an empty key, a tab or line break in a text field, or no file stamp.
   T-1429 s2 fold 5: then "<mirrorSeq>" (kRowFieldsBase), written in the same row as the file's stamp; a row of 17 fields (a table
   written before fold 5) reads with mirrorSeq 0 - its copy's version is not known, never guessed from a time. */
const size_t kRowFields = 17;
const size_t kRowFieldsBase = 18;
const size_t kRowFieldsOwn = 19;   /* T-1391 F1631 item 1: then "<ownBase>" - written only for this game's own unconfirmed area save whose base is known */
const size_t kRowFieldsUnechoed = 20;   /* T-1391 F1633 item 1: then "<stamp>,<stamp>.." - the unechoed uploads; field 19 is then "-" when no own base is kept */
const size_t kUnechoedKeptMax = 64;
inline std::string UnechoedText(const std::vector<long long>& v)
{
    std::string t;
    for (size_t i = 0; i < v.size(); ++i) t += (i != 0 ? "," : "") + I64Text(v[i]);
    return t;
}
/* false = not a list of positive stamps (an empty field is not written, so it is not one either) */
inline bool UnechoedParse(const std::string& f, std::vector<long long>* out)
{
    std::vector<long long> v; size_t at = 0;
    for (;;)
    {
        const size_t c = f.find(',', at);
        const std::string one = f.substr(at, c == std::string::npos ? std::string::npos : c - at);
        long long x = 0;
        if (!ParseI64(one, &x) || x <= 0 || v.size() >= kUnechoedKeptMax) return false;
        v.push_back(x);
        if (c == std::string::npos) break;
        at = c + 1;
    }
    out->swap(v);
    return true;
}
inline bool RowFormat(const KeptRow& r, std::string* line)
{
    if (r.key.empty() || r.writtenAt <= 0) return false;
    if (!FieldOk(r.key) || !FieldOk(r.squadSid) || !FieldOk(r.factionName) || !FieldOk(r.town) || !FieldOk(r.home)) return false;
    *line = "r\t" + r.key + "\t" + I64Text(r.writtenAt) + "\t" + I64Text(r.posAt) + "\t" + U64Text(r.owner)
          + "\t" + Hex8(FloatBits(r.x)) + "\t" + Hex8(FloatBits(r.y)) + "\t" + Hex8(FloatBits(r.z))
          + "\t" + U64Text(r.seq) + "\t" + I64Text(r.birth) + "\t" + U64Text(r.size) + "\t" + U64Text(r.mtime) + "\t" + Hex8(r.crc)
          + "\t" + r.squadSid + "\t" + r.factionName + "\t" + r.town + "\t" + r.home + "\t" + U64Text(r.mirrorSeq)
          + ((r.ownBaseKnown != 0 || !r.unechoed.empty()) ? "\t" + (r.ownBaseKnown != 0 ? U64Text(r.ownBase) : std::string("-")) : std::string())
          + (!r.unechoed.empty() ? "\t" + UnechoedText(r.unechoed) : std::string()) + "\n";
    return true;
}
inline bool RowParse(const std::string& line, KeptRow* out)
{
    std::vector<std::string> f;
    SplitTabs(line, &f);
    if ((f.size() != kRowFields && f.size() != kRowFieldsBase && f.size() != kRowFieldsOwn && f.size() != kRowFieldsUnechoed) || f[0] != "r" || f[1].empty()) return false;
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
    if (f.size() >= kRowFieldsBase && !ParseU64(f[17], &r.mirrorSeq)) return false;   /* fold 5: absent = 0 (KeptRow()) */
    if (f.size() >= kRowFieldsOwn && f[18] != "-") { if (!ParseU64(f[18], &r.ownBase)) return false; r.ownBaseKnown = 1; }   /* T-1391 F1631 item 1 */
    if (f.size() == kRowFieldsUnechoed && !UnechoedParse(f[19], &r.unechoed)) return false;   /* T-1391 F1633 item 1 */
    *out = r;
    return true;
}

/* T-1429 s2 fold 6 (review F1380 BLOCKER a): AN AREA ROW WITH NO VERSION IS REJECTED AT LOAD, as a changed row is - its copy's version is not
   known (a table from before fold 5, or this game's own save written before its echo), so the launch fetches the world's copy instead of
   loading one whose first save would be refused. A deleted row and an area row while area records are off are left to RowLoad. 1 = reject.
   T-1391 F1631 item 1: NOT a row that is this game's own save the world server has not confirmed with the base it was made from kept
   (ownBaseKnown) - a save made with the link down and then quit: it is taken, and the link-up push sends it judged by that base. A row
   with no version and no own base (a table from before fold 5 or before F1631) is still rejected: nothing says what it was made from. */
inline int RowNoBaseRejects(int isZone, unsigned long long mirrorSeq, int ownBaseKnown, int deleted, int zoneOff)
{
    return (isZone != 0 && deleted == 0 && zoneOff == 0 && mirrorSeq == 0ULL && ownBaseKnown == 0) ? 1 : 0;
}
/* T-1391 F1633 item 2: AN OWN AREA SAVE WRITTEN TO ITS MIRROR FILE marks the kept table - else a save after the last full save (an area
   unload) leaves its row naming the file as it was, and the reopen rejects the row as changed (RowCheck). Sent: the save's last echo
   writes the table (store.cpp g_zoneEchoWait); not sent (no link): no echo will come, so the table is written now. */
enum { kOwnSaveTableNone = 0, kOwnSaveTableMark = 1, kOwnSaveTableWriteNow = 2 };
inline int OwnSaveTableStep(int ownSaveWritten, int sent) { return ownSaveWritten == 0 ? (int)kOwnSaveTableNone : sent != 0 ? (int)kOwnSaveTableMark : (int)kOwnSaveTableWriteNow; }
/* T-1391 F1635 item 1: kOwnSaveTableWriteNow makes the write DUE, not a write per save (a full save or the quit drain of N areas was N whole
   rewrites). The table is written ONCE when the batch ends: at the tick, right after the outbox drain (store.cpp StoreTick - every save that
   drain applied and every main-thread save since the last tick, one write; an area unload's save is on disk in the table by the next frame),
   unless a full game save is still being written (savePending: a world-key row waits for it) - then that save's end writes it ('a save
   finished'; a save that never finishes clears its row within 60 s and the next tick writes it). The quit drain and world teardown write it
   at once (their own MirrorKeepFlush). A write that does not land keeps the table's mark for the next of those events, never a write per frame. */
inline int OwnSaveTableFlushNow(int due, int savePending) { return (due != 0 && savePending == 0) ? 1 : 0; }
/* T-1391 (F1653 design 2) .. F1683: THE OLD SIDE FILES - <file>.unsent (an own unconfirmed save's kept row and "b <stamp>" of its base),
   <file>.ownn, <file>.base / .basev (the kept base and its line "kb1 <key> <seq> <writtenAt> <writer> <empty>"), <file>.basen / .basevn.
   T-1391 design B (F1706): REPLACED by one state file per area (areastate.h, <file>.area). These readers stay for the MIGRATION only
   (areastate::MigrateRead reads an area's old files once at the first launch of the new build; the suite writes old-shaped files with the
   two Format helpers). Nothing writes these files any more. */
inline bool UnsentSideFormat(const KeptRow& r, long long baseStamp, std::string* out)
{
    std::string line;
    if (!RowFormat(r, &line)) return false;
    *out = line + "b\t" + I64Text(baseStamp) + "\n";
    return true;
}
inline bool UnsentSideParse(const std::string& text, KeptRow* row, long long* baseStamp)
{
    const size_t nl = text.find('\n');
    if (nl == std::string::npos) return false;
    std::string first = text.substr(0, nl);
    if (!first.empty() && first[first.size() - 1] == '\r') first.erase(first.size() - 1);
    KeptRow r;
    if (!RowParse(first, &r)) return false;
    std::string rest = text.substr(nl + 1);
    while (!rest.empty() && (rest[rest.size() - 1] == '\n' || rest[rest.size() - 1] == '\r')) rest.erase(rest.size() - 1);
    long long b = 0;
    if (rest.size() < 3 || rest[0] != 'b' || rest[1] != '\t' || !ParseI64(rest.substr(2), &b) || b < 0) return false;
    *row = r; *baseStamp = b;
    return true;
}
/* T-1391 F1680 M1: the old row's 'sent' mark - its unechoed list holds the row's own stamp (the migration turns it into the state's sent flag) */
inline int UnsentSideWasSent(const KeptRow& r)
{
    for (size_t i = 0; i < r.unechoed.size(); ++i) if (r.unechoed[i] == r.writtenAt && r.writtenAt > 0) return 1;
    return 0;
}
inline bool KeptBaseFormat(const std::string& key, unsigned long long seq, long long at, int writer, int empty, std::string* out)
{
    if (key.empty() || key.find('\t') != std::string::npos || key.find('\n') != std::string::npos || at < 0) return false;
    *out = "kb1\t" + key + "\t" + U64Text(seq) + "\t" + I64Text(at) + "\t" + I64Text((long long)writer) + "\t" + (empty != 0 ? "1" : "0") + "\n";
    return true;
}
inline bool KeptBaseParse(const std::string& text, std::string* key, unsigned long long* seq, long long* at, int* writer, int* empty)
{
    std::string t = text;
    while (!t.empty() && (t[t.size() - 1] == '\n' || t[t.size() - 1] == '\r')) t.erase(t.size() - 1);
    std::vector<std::string> f;
    size_t p = 0;
    for (;;) { const size_t q = t.find('\t', p); f.push_back(t.substr(p, q == std::string::npos ? std::string::npos : q - p)); if (q == std::string::npos) break; p = q + 1; }
    unsigned long long s = 0; long long a = 0, w = 0;
    if (f.size() != 6 || f[0] != "kb1" || f[1].empty() || !ParseU64(f[2], &s) || !ParseI64(f[3], &a) || a < 0 || !ParseI64(f[4], &w) || (f[5] != "0" && f[5] != "1")) return false;
    *key = f[1]; *seq = s; *at = a; *writer = (int)w; *empty = f[5] == "1" ? 1 : 0;
    return true;
}
/* item 4, THE RELOAD: an area with an own unsent save kept (F1706: its state's pending flag) is loaded from that save - the world server's start-up copy is not
   copied over its mirror file (LoadIndex), and the first snapshot does not drop it as unnamed (the world server may never have had the
   area); the link-up push sends it and the world server judges it */
inline int IndexCopyYields(int ownUnsentKept) { return ownUnsentKept != 0 ? 1 : 0; }
/* (UnnamedDecideOwn: below UnnamedDecide) */
/* the own base goes in an area row only while its mirror copy has no version (an echoed copy's version says it all) */
inline int RowOwnBaseWritten(unsigned long long mirrorSeq, int ownBaseHere) { return (mirrorSeq == 0ULL && ownBaseHere != 0) ? 1 : 0; }
/* fold 5 / 6: the version MirrorKeepWrite puts in an area row - the mirror's version only for the stamp it was taken for, else 0 */
inline unsigned long long RowMirrorSeq(int known, long long baseStamp, unsigned long long baseSeq, long long rowStamp)
{
    return (known != 0 && baseStamp == rowStamp) ? baseSeq : 0ULL;
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
/* T-1390: THE WORLD ID A TABLE'S HEADER NAMES ("" = no table header). The header line alone, no checksum: a joining game's folder search
   (store.cpp JoinedDirChosen, coopworld::JoinedFolderPick) asks only whose table it is; the table itself is checked whole when it is read. */
inline std::string TableHeaderWorldId(const std::string& text)
{
    std::vector<std::string> f;
    SplitTabs(text.substr(0, text.find_first_of("\r\n")), &f);
    if (f.size() < 4 || f[0] != kTableMagic) return std::string();
    return f[2];
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
/* T-1391 (F1653 item 4): an own unsent area save kept (IndexCopyYields above) is never dropped as unnamed - kept as one changed here */
inline int UnnamedDecideOwn(int snapshotWhole, int named, int stillHere, long long writtenAtNow, long long writtenAtKept, int ownUnsent)
{
    const int v = UnnamedDecide(snapshotWhole, named, stillHere, writtenAtNow, writtenAtKept);
    return (v == kUnnamedDrop && ownUnsent != 0) ? (int)kUnnamedChangedHere : v;
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

/* WAS THE SNAPSHOT SEEN WHOLE (the unnamed-key drop's snapshotWhole; store.cpp StoreFeedMarkerApply judges it once and hands it to
   MirrorKeepSnapshotEnd, MirrorKeepStaleHoldEnd and the town wait's PreTownWaitEnd). T-985 C2b fold 6: the world server judges it and
   says so on the LAST PAGE_END (complete, coopfeed::FeedSnapshotComplete: every key its BEGIN counted was paged or deleted, no page
   was cut, no record send was refused). last / complete = that PAGE_END's two flags; nothing else is asked.
   History: until fold 6 the game counted (pages + deleted >= the BEGIN's total; an empty last page read as the world server's
   answer to a key too long for the wire). T1533 (2026-10-25): the raw count made a snapshot NOT whole when another game deleted a
   record between the BEGIN and its page (fold 5 added the deleted ones); fold 5's review: a record created after the BEGIN hid one
   whose send was refused (a false whole), and one created and deleted after a page's last key left an empty last page that looked
   like a cut (a false not-whole). 07-bug-classes: a completeness test by count on a set that changes while it is read.
   A snapshot not whole drops no unnamed key: those keys stay until named or re-fetched. */
inline int SnapshotWholeForDrop(unsigned int last, unsigned int complete)
{
    return (last == 1u && complete == 1u) ? 1 : 0;
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
/* T-1391 (review of e3082c6cc, B1): a row holding this game's own UNSENT area save (.unsent kept) is never dropped by the table's refusal
   (the world server behind, or not judged) - the same exemption UnnamedDecideOwn gives the snapshot's end: the save stays for the
   link-up push and the world server judges it there */
inline int RefuseDropsOwn(int stillHere, long long writtenAtNow, long long writtenAtKept, int namedThisSnapshot, int ownUnsent)
{
    return ownUnsent != 0 ? 0 : RefuseDrops(stillHere, writtenAtNow, writtenAtKept, namedThisSnapshot);
}

}   /* namespace mirrorkeep */
