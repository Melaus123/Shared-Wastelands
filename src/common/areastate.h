#pragma once
/* T-1391 DESIGN B (F1706, decided 2026-11-20): ONE STATE FILE PER AREA - <mirror>.area.
   Before: an area's own state was spread over the mirror file and six side files (.unsent, .ownn, .base, .basev, .basen, .basevn), each
   written and renamed on its own; every fold of the crash order opened new edge roads (F1680, F1683, F1696, F1700, F1704).
   Now: everything this game keeps about one area is ONE file, <mirror>.area, written whole:
     line 1  "swarea 1 <gen> <key> <pending> <sent> <rowKnown> <saveHave> <baseKept> <baseSeq> <baseAt> <baseWriter> <baseEmpty> <saveLen> <baseLen>"
             (tab-separated) - the format version, the GENERATION (raised at every commit), the pending / sent flags of this game's own save, and
             the kept base's version (seq), stamp, writer slot and EMPTY flag;
     line 2  the area's row (mirrorkeep::RowFormat: its stamp writtenAt, its world version mirrorSeq, its own base, the unechoed list) or "-";
     then    the save bytes (the copy the mirror holds), then the base bytes (only while a base is kept);
     last    "end <CRC-32 of every byte above, 8 hex>".
   THE ONE COMMIT (Commit): the whole new file is written to <mirror>.area.tmp, flushed (FlushFileBuffers), then moved over <mirror>.area with
   MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH). The plain <mirror> - the file the game loads - is rebuilt from the state
   afterwards (MirrorRebuild: <mirror>.area.rebuild, then a rename over the mirror); it is never the truth.
   THE LOAD (LoadArea): of <mirror>.area and <mirror>.area.tmp the newest WHOLE copy is taken (CRC right, the higher generation); a whole
   .area.tmp newer than .area is moved into place (the rename its commit never made); a torn or older .area.tmp is deleted.
   THE MIGRATION (MigrateRead): an area with the old side files and no .area gets one built from them at the first launch of this build.
   No game state, no game calls: files only (u8file wrappers), so the offline suite drives these exact helpers. C++03. */
#include "../coop-plugin/u8file.h"
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include "mirrorkeep.h"

namespace areastate {

const char* const kMagic = "swarea";
const unsigned int kFormat = 1u;
const char* const kMovedOldDir = "moved-old";
inline std::string AreaName(const std::string& mirrorFile) { return mirrorFile + ".area"; }
inline std::string TmpName(const std::string& mirrorFile) { return mirrorFile + ".area.tmp"; }
inline std::string RebuildName(const std::string& mirrorFile) { return mirrorFile + ".area.rebuild"; }

/* CRC-32, IEEE 802.3 reflected (0xEDB88320, init and final xor 0xFFFFFFFF) - the same value coopstore::Crc32 gives (the suite checks it);
   half-byte table, no state */
inline unsigned int Crc32(const char* p, size_t n)
{
    static const unsigned int t[16] = { 0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
                                        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu };
    unsigned int c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i)
    {
        c ^= (unsigned int)(unsigned char)p[i];
        c = (c >> 4) ^ t[c & 15u];
        c = (c >> 4) ^ t[c & 15u];
    }
    return c ^ 0xFFFFFFFFu;
}

/* THE HEADER of one area's state. pending: the save bytes are this game's own save the world server has not confirmed (was: a .unsent
   beside the mirror); sent: that save went up with the link up (its stamp waits for its echo - was the row's own stamp in its unechoed
   list, mirrorkeep::UnsentSideWasSent); rowKnown: line 2 holds the area's row (an area migrated from base files alone has none - the kept
   table's row stands for it); saveHave: save bytes are kept (an area with only a kept base and no mirror file has none). */
struct Head
{
    unsigned long long gen;
    std::string key;
    int pending, sent, rowKnown, saveHave;
    mirrorkeep::KeptRow row;
    int baseKept; unsigned long long baseSeq; long long baseAt; int baseWriter; int baseEmpty;
    Head() : gen(0), pending(0), sent(0), rowKnown(0), saveHave(0), baseKept(0), baseSeq(0), baseAt(0), baseWriter(-1), baseEmpty(0) {}
};

inline bool Flag01(const std::string& s, int* v) { if (s == "0") { *v = 0; return true; } if (s == "1") { *v = 1; return true; } return false; }

/* the whole file; false = not writable (a key with a tab or line break, a row that RowFormat refuses or that names another key, bytes that
   contradict the flags) - nothing is written then */
inline bool Format(const Head& h, const std::vector<char>& save, const std::vector<char>& base, std::vector<char>* out)
{
    if (h.key.empty() || !mirrorkeep::FieldOk(h.key) || h.key.find(' ') != std::string::npos) return false;
    if (h.saveHave == 0 && !save.empty()) return false;
    if (h.baseKept == 0 && !base.empty()) return false;
    if (h.baseAt < 0) return false;
    std::string rowLine = "-\n";
    if (h.rowKnown != 0 && (h.row.key != h.key || !mirrorkeep::RowFormat(h.row, &rowLine))) return false;
    const std::string head = std::string(kMagic) + "\t" + mirrorkeep::U64Text(kFormat) + "\t" + mirrorkeep::U64Text(h.gen) + "\t" + h.key
                           + "\t" + (h.pending != 0 ? "1" : "0") + "\t" + (h.sent != 0 ? "1" : "0") + "\t" + (h.rowKnown != 0 ? "1" : "0") + "\t" + (h.saveHave != 0 ? "1" : "0")
                           + "\t" + (h.baseKept != 0 ? "1" : "0") + "\t" + mirrorkeep::U64Text(h.baseSeq) + "\t" + mirrorkeep::I64Text(h.baseAt) + "\t" + mirrorkeep::I64Text((long long)h.baseWriter)
                           + "\t" + (h.baseEmpty != 0 ? "1" : "0") + "\t" + mirrorkeep::U64Text((unsigned long long)save.size()) + "\t" + mirrorkeep::U64Text((unsigned long long)base.size()) + "\n";
    std::vector<char> b;
    b.reserve(head.size() + rowLine.size() + save.size() + base.size() + 16);
    b.insert(b.end(), head.begin(), head.end());
    b.insert(b.end(), rowLine.begin(), rowLine.end());
    b.insert(b.end(), save.begin(), save.end());
    b.insert(b.end(), base.begin(), base.end());
    const std::string tail = "end\t" + mirrorkeep::Hex8(Crc32(b.empty() ? "" : &b[0], b.size())) + "\n";
    b.insert(b.end(), tail.begin(), tail.end());
    out->swap(b);
    return true;
}

/* what a read of one copy found */
enum { kParseOk = 0, kParseNoFile = 1, kParseBadHeader = 2, kParseTorn = 3, kParseCrc = 4, kParseBadRow = 5 };
inline const char* ParseName(int v)
{
    return v == kParseOk ? "whole" : v == kParseNoFile ? "none" : v == kParseBadHeader ? "not an area state header" : v == kParseTorn ? "TORN (shorter or longer than its header says)"
         : v == kParseCrc ? "CRC FAILED" : v == kParseBadRow ? "its row does not read" : "?";
}
const size_t kTailLen = 13;   /* "end\t" + 8 hex + "\n" */
inline int Parse(const std::vector<char>& b, Head* h, std::vector<char>* save, std::vector<char>* base)
{
    const char* p = b.empty() ? "" : &b[0];
    size_t nl1 = 0;
    while (nl1 < b.size() && nl1 < 4096 && p[nl1] != '\n') ++nl1;
    if (nl1 >= b.size() || p[nl1] != '\n') return b.size() < 4096 ? (int)kParseTorn : (int)kParseBadHeader;
    std::vector<std::string> f;
    mirrorkeep::SplitTabs(std::string(p, nl1), &f);
    if (f.size() != 15 || f[0] != kMagic || f[1] != mirrorkeep::U64Text(kFormat)) return kParseBadHeader;
    Head x;
    unsigned long long saveLen = 0, baseLen = 0; long long w = 0;
    if (!mirrorkeep::ParseU64(f[2], &x.gen) || f[3].empty()) return kParseBadHeader;
    x.key = f[3];
    if (!Flag01(f[4], &x.pending) || !Flag01(f[5], &x.sent) || !Flag01(f[6], &x.rowKnown) || !Flag01(f[7], &x.saveHave) || !Flag01(f[8], &x.baseKept)) return kParseBadHeader;
    if (!mirrorkeep::ParseU64(f[9], &x.baseSeq) || !mirrorkeep::ParseI64(f[10], &x.baseAt) || x.baseAt < 0 || !mirrorkeep::ParseI64(f[11], &w) || w < -2147483647LL || w > 2147483647LL) return kParseBadHeader;
    x.baseWriter = (int)w;
    if (!Flag01(f[12], &x.baseEmpty) || !mirrorkeep::ParseU64(f[13], &saveLen) || !mirrorkeep::ParseU64(f[14], &baseLen)) return kParseBadHeader;
    size_t nl2 = nl1 + 1;
    while (nl2 < b.size() && p[nl2] != '\n') ++nl2;
    if (nl2 >= b.size()) return kParseTorn;
    const size_t body = nl2 + 1;
    if (saveLen > (unsigned long long)b.size() || baseLen > (unsigned long long)b.size() || (unsigned long long)body + saveLen + baseLen + kTailLen != (unsigned long long)b.size()) return kParseTorn;
    const size_t tailAt = b.size() - kTailLen;
    const std::string tail(p + tailAt, kTailLen);
    unsigned int crc = 0;
    if (tail.compare(0, 4, "end\t") != 0 || tail[kTailLen - 1] != '\n' || !mirrorkeep::ParseHex8(tail.substr(4, 8), &crc)) return kParseTorn;
    if (crc != Crc32(p, tailAt)) return kParseCrc;
    const std::string rowLine(p + nl1 + 1, nl2 - nl1 - 1);
    if (x.rowKnown != 0) { if (!mirrorkeep::RowParse(rowLine, &x.row) || x.row.key != x.key) return kParseBadRow; }
    else if (rowLine != "-") return kParseBadRow;
    if ((x.saveHave == 0 && saveLen != 0) || (x.baseKept == 0 && baseLen != 0)) return kParseBadHeader;
    *h = x;
    if (save != 0) save->assign(p + body, p + body + (size_t)saveLen);
    if (base != 0) base->assign(p + body + (size_t)saveLen, p + body + (size_t)saveLen + (size_t)baseLen);
    return kParseOk;
}

/* OF THE TWO SLOTS, THE NEWEST WHOLE COPY: both whole - the higher generation (a tie: .area); one whole - that one; none - nothing */
enum { kPickNone = 0, kPickArea = 1, kPickTmp = 2 };
inline int Pick(int areaOk, unsigned long long areaGen, int tmpOk, unsigned long long tmpGen)
{
    if (areaOk != 0 && tmpOk != 0) return tmpGen > areaGen ? (int)kPickTmp : (int)kPickArea;
    if (areaOk != 0) return kPickArea;
    return tmpOk != 0 ? (int)kPickTmp : (int)kPickNone;
}

/* THE PLAIN MIRROR AGAINST THE STATE at a load - decided from the bytes, never from file times alone (review of 01a710593 items 2, 3): no
   save bytes - nothing to build; no mirror file - rebuild; a mirror holding the state's save bytes - kept as it is (never rewritten, so the
   kept table's row for it still matches); other bytes - rebuilt from the state, so the mirror never stands for bytes .area does not hold.
   Other bytes in a mirror written at or after the state's last commit (mirNewer) are set aside first: a save whose commit had not landed
   before a crash or a quit (this game's own work the state does not carry), or a rebuild a power loss cut (zero-filled) - never dropped
   unseen. sameBytes: 1 same, 0 differs (or the mirror does not read), -1 not compared yet. */
enum { kMirKeep = 0, kMirRebuild = 1, kMirCompare = 2, kMirAsideRebuild = 3 };
inline int MirrorAtLoad(int saveHave, int mirThere, int mirNewer, int sameBytes)
{
    if (saveHave == 0) return kMirKeep;
    if (mirThere == 0) return kMirRebuild;
    if (sameBytes < 0) return kMirCompare;
    if (sameBytes != 0) return kMirKeep;
    return mirNewer != 0 ? (int)kMirAsideRebuild : (int)kMirRebuild;
}

/* at reopen an area state's row stands for its area when the kept table has no row of it or one not newer (the state is committed at every
   mirror write, the table only per batch); a newer table row (written after a commit that did not land) stands instead */
inline int AreaRowTakes(int haveTableRow, long long tableAt, long long areaAt) { return (areaAt > 0 && (haveTableRow == 0 || tableAt <= areaAt)) ? 1 : 0; }

/* ---------------- files ---------------- */
inline bool FileStat(const std::string& path, unsigned long long* size, unsigned long long* mtime)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!U8GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &fa) || (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return false;
    if (size != 0) *size = ((unsigned long long)fa.nFileSizeHigh << 32) | (unsigned long long)fa.nFileSizeLow;
    if (mtime != 0) *mtime = ((unsigned long long)fa.ftLastWriteTime.dwHighDateTime << 32) | (unsigned long long)fa.ftLastWriteTime.dwLowDateTime;
    return true;
}
inline bool There(const std::string& path) { const DWORD a = U8GetFileAttributes(path.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0; }
/* the whole file (shared read / write / delete, so a rename over it never fails for this read); false = not there or not read (*there says which) */
inline bool ReadAll(const std::string& path, std::vector<char>* out, int* there)
{
    if (there != 0) *there = 0;
    out->clear();
    HANDLE h = U8CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) { if (there != 0) *there = There(path) ? 1 : 0; return false; }
    if (there != 0) *there = 1;
    LARGE_INTEGER sz; sz.QuadPart = 0;
    if (!::GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || sz.QuadPart > (LONGLONG)(256 << 20)) { ::CloseHandle(h); return false; }
    out->resize((size_t)sz.QuadPart);
    size_t got = 0;
    while (got < out->size())
    {
        DWORD n = 0;
        const DWORD want = (DWORD)((out->size() - got) > (size_t)(1 << 30) ? (size_t)(1 << 30) : (out->size() - got));
        if (!::ReadFile(h, &(*out)[got], want, &n, 0) || n == 0) { ::CloseHandle(h); out->clear(); return false; }
        got += (size_t)n;
    }
    ::CloseHandle(h);
    return true;
}
/* the failing step of a write */
enum { kStepNone = 0, kStepFormat = 1, kStepCreate = 2, kStepWrite = 3, kStepFlush = 4, kStepRename = 5 };
inline const char* StepName(int s)
{
    return s == kStepNone ? "none" : s == kStepFormat ? "format" : s == kStepCreate ? "create" : s == kStepWrite ? "write" : s == kStepFlush ? "flush" : s == kStepRename ? "rename" : "?";
}
/* written whole (CREATE_ALWAYS) and, when asked, flushed to the disk before it is closed; a failure removes the file and answers its step */
inline int WriteWhole(const std::string& path, const std::vector<char>& b, int flush, DWORD* err)
{
    *err = 0;
    HANDLE h = U8CreateFile(path.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) { *err = ::GetLastError(); return kStepCreate; }
    int step = kStepNone;
    size_t put = 0;
    while (put < b.size() && step == kStepNone)
    {
        DWORD n = 0;
        const DWORD want = (DWORD)((b.size() - put) > (size_t)(1 << 30) ? (size_t)(1 << 30) : (b.size() - put));
        if (!::WriteFile(h, &b[put], want, &n, 0)) { *err = ::GetLastError(); step = kStepWrite; }
        else if (n == 0) step = kStepWrite;
        else put += (size_t)n;
    }
    if (step == kStepNone && flush != 0 && !::FlushFileBuffers(h)) { *err = ::GetLastError(); step = kStepFlush; }
    ::CloseHandle(h);
    if (step != kStepNone) U8DeleteFile(path.c_str());
    return step;
}
/* THE ONE COMMIT POINT of an area's state. CRASH TABLE (what the next launch's LoadArea does after a crash at each step):
     1 while .area.tmp is written      -> .area.tmp torn (CRC or length wrong): deleted; .area (the previous state) stands
     2 after the write, before flush   -> the same: an unflushed .area.tmp that did not reach the disk whole fails its CRC and is deleted;
                                          one that did is whole with the higher generation and is taken (step 4)
     3 after the flush, before rename  -> .area.tmp whole, generation higher: it is the state - moved over .area at that load
     4 the rename itself               -> MOVEFILE_WRITE_THROUGH: either the old .area with the whole .area.tmp beside it (step 3) or the new
                                          .area alone; never a half file under the .area name
     5 after the rename, before the plain mirror is rebuilt -> .area newer than the mirror: the load compares and rebuilds the mirror
                                          (MirrorAtLoad) before anything reads it
     6 while the mirror is rebuilt     -> .area.rebuild left over (deleted at the load) and the mirror older: rebuilt again (step 5)
   A failed step (no crash) answers its step; the caller keeps the state in memory and commits again later - a save is never refused.
   A failed rename leaves the whole, flushed .area.tmp in place: a crash before the next try still finds it (step 3) - and the next commit
   never writes over it or deletes it while it is newer than .area (review of 01a710593 item 1): it is moved into place first.
   THE COMMIT AFTER A FAILED RENAME (.area at N-1, the whole temp at N; this commit N or N+1), a crash at each step:
     r0 before the temp is moved        -> the temp N is taken (step 3)
     r1 the temp still cannot be moved  -> this commit answers rename and writes nothing: the temp N stays untouched, N+1 stays in the
                                           caller's memory
     r2 after the temp is moved         -> .area N alone; a commit of those same bytes is done there (the move was the commit)
     r3-r6 the new temp written, flushed, renamed -> steps 1-4 over .area N: N or N+1 at the load, never older than N */
inline int CommitBytes(const std::string& mirrorFile, const std::vector<char>& bytes, DWORD* err)
{
    const std::string tmp = TmpName(mirrorFile), area = AreaName(mirrorFile);
    std::vector<char> tb; int tThere = 0;
    if (ReadAll(tmp, &tb, &tThere))
    {
        Head th, ah; std::vector<char> ab, x1, x2; int aThere = 0;
        const int tOk = Parse(tb, &th, &x1, &x2) == kParseOk ? 1 : 0;
        const bool aRead = tOk != 0 && ReadAll(area, &ab, &aThere);
        if (tOk != 0 && !aRead && aThere != 0) { *err = 0; return kStepRename; }   /* .area there and not readable (held): neither file is touched */
        const int aOk = (aRead && Parse(ab, &ah, &x1, &x2) == kParseOk) ? 1 : 0;
        if (tOk != 0 && Pick(aOk, ah.gen, 1, th.gen) == kPickTmp)
        {   /* the only whole copy of a newer generation: moved into place before anything is written under its name */
            if (U8MoveFileEx(tmp.c_str(), area.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) { *err = ::GetLastError(); return kStepRename; }
            if (tb == bytes) return kStepNone;
        }   /* a temp not whole, or not newer than .area, holds nothing the load would take: written over */
    }
    else if (tThere != 0) { *err = 0; return kStepCreate; }   /* there and not readable: never written over unread */
    const int s = WriteWhole(tmp, bytes, 1, err);
    if (s != kStepNone) return s;
    if (U8MoveFileEx(tmp.c_str(), area.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) { *err = ::GetLastError(); return kStepRename; }
    return kStepNone;
}
inline int Commit(const std::string& mirrorFile, const Head& h, const std::vector<char>& save, const std::vector<char>& base, DWORD* err)
{
    *err = 0;
    std::vector<char> b;
    if (!Format(h, save, base, &b)) return kStepFormat;
    return CommitBytes(mirrorFile, b, err);
}
/* the plain mirror the game loads, from the state's save bytes: <mirror>.area.rebuild written whole (flushed when asked - an own pending
   save), then renamed over the mirror; a failure answers its step and leaves the mirror as it was */
inline int MirrorRebuild(const std::string& mirrorFile, const std::vector<char>& save, int flush, DWORD* err)
{
    const std::string rb = RebuildName(mirrorFile);
    const int s = WriteWhole(rb, save, flush, err);
    if (s != kStepNone) return s;
    if (U8MoveFileEx(rb.c_str(), mirrorFile.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) { *err = ::GetLastError(); U8DeleteFile(rb.c_str()); return kStepRename; }
    return kStepNone;
}

/* ONE AREA AT A LOAD: the newest whole copy of .area / .area.tmp (Pick). pick kPickTmp: .area.tmp is moved over .area (finished = 1; a
   failed move leaves it - the state is still taken from it); kPickArea with a .area.tmp beside it: that one is torn or older and is deleted;
   kPickNone: a torn .area.tmp is deleted, a .area that is not whole is left for the caller (NOT KNOWN - it sets its bytes aside). */
struct LoadOut
{
    int areaThere, areaRes, tmpThere, tmpRes, pick, finished, tmpRemoved; DWORD err;
    unsigned long long areaGen, tmpGen;
    LoadOut() : areaThere(0), areaRes(kParseNoFile), tmpThere(0), tmpRes(kParseNoFile), pick(kPickNone), finished(0), tmpRemoved(0), err(0), areaGen(0), tmpGen(0) {}
};
inline int LoadArea(const std::string& mirrorFile, Head* h, std::vector<char>* save, std::vector<char>* base, LoadOut* o)
{
    const std::string area = AreaName(mirrorFile), tmp = TmpName(mirrorFile);
    std::vector<char> ab, tb, as, abase, ts, tbase;
    Head ah, th;
    if (ReadAll(area, &ab, &o->areaThere)) o->areaRes = Parse(ab, &ah, &as, &abase); else o->areaRes = o->areaThere != 0 ? (int)kParseTorn : (int)kParseNoFile;
    if (ReadAll(tmp, &tb, &o->tmpThere)) o->tmpRes = Parse(tb, &th, &ts, &tbase); else o->tmpRes = o->tmpThere != 0 ? (int)kParseTorn : (int)kParseNoFile;
    o->areaGen = o->areaRes == kParseOk ? ah.gen : 0ULL; o->tmpGen = o->tmpRes == kParseOk ? th.gen : 0ULL;
    o->pick = Pick(o->areaRes == kParseOk ? 1 : 0, ah.gen, o->tmpRes == kParseOk ? 1 : 0, th.gen);
    if (o->pick == kPickTmp)
    {
        *h = th; save->swap(ts); base->swap(tbase);
        if (U8MoveFileEx(tmp.c_str(), area.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) o->finished = 1; else o->err = ::GetLastError();
    }
    else
    {
        if (o->pick == kPickArea) { *h = ah; save->swap(as); base->swap(abase); }
        if (o->tmpThere != 0 && (o->pick == kPickArea || o->tmpRes != kParseOk)) o->tmpRemoved = U8DeleteFile(tmp.c_str()) != 0 ? 1 : 0;
    }
    return o->pick;
}

/* ---------------- THE MIGRATION from the old side files (first launch of this build) ----------------
   The old files of an area <mirror>: .unsent (its own unconfirmed save's row, mirrorkeep::UnsentSideParse), .ownn (an own save whose rename
   over the mirror may not have been made), .base / .basev (the kept base and its version line, mirrorkeep::KeptBaseParse), .basen / .basevn
   (a base swap's new pair). Today's leftover rules are applied ONCE, as decisions on what to read (nothing is renamed among the old files):
   MigSaveFrom is the .ownn rule (F1683 C1: finished only when its mark is stamped with it and the mirror does not already hold it);
   MigBaseFrom the swap rule (F1683 C2: past its commit point - .basevn whole - the new pair, else the older pair; a pair that cannot be
   trusted is NOT KNOWN). A file that is there and cannot be read makes the area NOT KNOWN: its old files stay where they are, its save
   bytes are set aside by the caller, and no state is built. */
inline const char* const* OldSuffixes(size_t* n)
{
    static const char* const s[6] = { ".unsent", ".ownn", ".basevn", ".basev", ".basen", ".base" };
    *n = 6;
    return s;
}
enum { kMigSaveMirror = 0, kMigSaveOwnn = 1 };
inline int MigSaveFrom(int markWhole, unsigned long long markSize, unsigned long long markMtime, int ownnThere, unsigned long long ownnSize, unsigned long long ownnMtime,
                       int mirThere, unsigned long long mirSize, unsigned long long mirMtime)
{
    if (markWhole == 0 || ownnThere == 0) return kMigSaveMirror;
    if (markSize != ownnSize || markMtime != ownnMtime) return kMigSaveMirror;
    if (mirThere != 0 && mirSize == markSize && mirMtime == markMtime) return kMigSaveMirror;
    return kMigSaveOwnn;
}
enum { kMbNone = 0, kMbOldPair = 1, kMbNewFromBasen = 2, kMbNewFromBase = 3, kMbNotKnown = 4 };
inline const char* MigBaseName(int v)
{
    return v == kMbNone ? "no base" : v == kMbOldPair ? ".base with its .basev line" : v == kMbNewFromBasen ? "the swap's new pair (.basen with its .basevn line - past its commit point)"
         : v == kMbNewFromBase ? "the swap's new pair (.base already holds the new bytes, its line in .basevn)" : v == kMbNotKnown ? "NOT KNOWN (a line that cannot be read, or a half-swapped pair)" : "?";
}
inline int MigBaseFrom(int vThere, int vWhole, int bThere, int vnThere, int vnWhole, int bnThere)
{
    if (vnThere != 0)
    {
        if (vnWhole != 0) return bnThere != 0 ? (int)kMbNewFromBasen : bThere != 0 ? (int)kMbNewFromBase : (int)kMbNone;   /* a line with no bytes: none */
        if (bnThere == 0) return kMbNotKnown;   /* .base may hold the new bytes beside the older line */
    }
    if (vThere == 0) return kMbNone;   /* a .base with no line is never read */
    if (vWhole == 0) return kMbNotKnown;
    return bThere != 0 ? (int)kMbOldPair : (int)kMbNone;
}
enum { kMigNothing = 0, kMigBuilt = 1, kMigNotKnown = 2, kMigDiscardOnly = 3 };
inline const char* MigName(int v)
{
    return v == kMigNothing ? "no old files" : v == kMigBuilt ? "built" : v == kMigNotKnown ? "NOT KNOWN" : v == kMigDiscardOnly ? "nothing to keep (strays only)" : "?";
}
typedef std::string (*MirrorOfFn)(const std::string& key);
struct Mig
{
    int result, saveFrom, baseFrom, markWhole;
    Head h; std::vector<char> save, base;
    std::vector<std::string> oldFiles;   /* every old file of the area that is there - moved to moved-old\ once the state's commit landed */
    std::string why;                     /* NOT KNOWN: which file */
    std::vector<char> aside;             /* NOT KNOWN: the bytes to set aside (the .ownn's, else the mirror's) */
    Mig() : result(kMigNothing), saveFrom(kMigSaveMirror), baseFrom(kMbNone), markWhole(0) {}
};
/* reads the old files of one area (mirrorFile names it); mirrorOf(key) must give mirrorFile back for a key read from a file (else the file
   is another area's and is not used). No file is changed. */
inline void MigrateRead(const std::string& mirrorFile, MirrorOfFn mirrorOf, Mig* m)
{
    size_t ns = 0; const char* const* sfx = OldSuffixes(&ns);
    for (size_t i = 0; i < ns; ++i) if (There(mirrorFile + sfx[i])) m->oldFiles.push_back(mirrorFile + sfx[i]);
    if (m->oldFiles.empty()) { m->result = kMigNothing; return; }
    const std::string fU = mirrorFile + ".unsent", fO = mirrorFile + ".ownn", fB = mirrorFile + ".base", fV = mirrorFile + ".basev", fBn = mirrorFile + ".basen", fVn = mirrorFile + ".basevn";
    std::string notKnown;
    /* the mark (.unsent) */
    mirrorkeep::KeptRow mr; long long mbase = 0; int uThere = 0;
    std::vector<char> ub;
    const bool uRead = ReadAll(fU, &ub, &uThere);
    if (uThere != 0)
    {
        m->markWhole = (uRead && mirrorkeep::UnsentSideParse(std::string(ub.begin(), ub.end()), &mr, &mbase) && !mr.key.empty() && _stricmp(mirrorOf(mr.key).c_str(), mirrorFile.c_str()) == 0) ? 1 : 0;
        if (m->markWhole == 0) notKnown = ".unsent cannot be read";
    }
    /* the base pair */
    std::string vKey, vnKey; unsigned long long vSeq = 0, vnSeq = 0; long long vAt = 0, vnAt = 0; int vWr = -1, vnWr = -1, vEm = 0, vnEm = 0;
    std::vector<char> tb; int tThere = 0;
    const int vThere = There(fV) ? 1 : 0, vnThere = There(fVn) ? 1 : 0;
    const int vWhole = (vThere != 0 && ReadAll(fV, &tb, &tThere) && mirrorkeep::KeptBaseParse(std::string(tb.begin(), tb.end()), &vKey, &vSeq, &vAt, &vWr, &vEm) && _stricmp(mirrorOf(vKey).c_str(), mirrorFile.c_str()) == 0) ? 1 : 0;
    const int vnWhole = (vnThere != 0 && ReadAll(fVn, &tb, &tThere) && mirrorkeep::KeptBaseParse(std::string(tb.begin(), tb.end()), &vnKey, &vnSeq, &vnAt, &vnWr, &vnEm) && _stricmp(mirrorOf(vnKey).c_str(), mirrorFile.c_str()) == 0) ? 1 : 0;
    m->baseFrom = MigBaseFrom(vThere, vWhole, There(fB) ? 1 : 0, vnThere, vnWhole, There(fBn) ? 1 : 0);
    if (m->baseFrom == kMbNotKnown && notKnown.empty()) notKnown = vnThere != 0 && vnWhole == 0 ? ".basevn cannot be read (and .base may already hold its new bytes)" : ".basev cannot be read";
    if (m->baseFrom == kMbOldPair || m->baseFrom == kMbNewFromBase || m->baseFrom == kMbNewFromBasen)
    {
        const std::string bytesFile = m->baseFrom == kMbNewFromBasen ? fBn : fB;
        if (!ReadAll(bytesFile, &m->base, &tThere) || m->base.empty()) { if (notKnown.empty()) notKnown = bytesFile.substr(mirrorFile.size()) + " (the kept base's bytes) cannot be read"; m->base.clear(); }
        const bool useNew = m->baseFrom != kMbOldPair;
        m->h.baseKept = 1; m->h.baseSeq = useNew ? vnSeq : vSeq; m->h.baseAt = useNew ? vnAt : vAt; m->h.baseWriter = useNew ? vnWr : vWr; m->h.baseEmpty = useNew ? vnEm : vEm;
        m->h.key = useNew ? vnKey : vKey;
    }
    /* the save: the .ownn when its mark stands for it (the rename never made), else the mirror */
    unsigned long long os = 0, ot = 0, ms = 0, mt = 0;
    const int oThere = FileStat(fO, &os, &ot) ? 1 : 0, mThere = FileStat(mirrorFile, &ms, &mt) ? 1 : 0;
    m->saveFrom = MigSaveFrom(m->markWhole, m->markWhole != 0 ? mr.size : 0ULL, m->markWhole != 0 ? mr.mtime : 0ULL, oThere, os, ot, mThere, ms, mt);
    const std::string saveFile = m->saveFrom == kMigSaveOwnn ? fO : mirrorFile;
    int sThere = 0;
    const bool sRead = ReadAll(saveFile, &m->save, &sThere);
    if (sThere != 0 && !sRead && notKnown.empty()) notKnown = (m->saveFrom == kMigSaveOwnn ? std::string(".ownn") : std::string("the mirror file")) + " cannot be read";
    if (m->markWhole != 0 && sThere == 0 && notKnown.empty()) notKnown = "the own save's mirror file is gone";
    if (m->markWhole != 0) m->h.key = mr.key;
    if (!notKnown.empty())
    {
        m->result = kMigNotKnown; m->why = notKnown;
        if (!sRead) { int t2 = 0; std::vector<char> any; if (oThere != 0 && ReadAll(fO, &any, &t2)) m->aside.swap(any); else if (ReadAll(mirrorFile, &any, &t2)) m->aside.swap(any); }
        else m->aside = m->save;
        m->h = Head(); m->save.clear(); m->base.clear();
        return;
    }
    if (m->markWhole == 0 && m->h.baseKept == 0) { m->result = kMigDiscardOnly; m->h = Head(); m->save.clear(); return; }   /* strays only: a .ownn with no mark, a .base or .basen with no line */
    m->h.saveHave = sThere != 0 ? 1 : 0;
    if (m->h.saveHave == 0) m->save.clear();
    if (m->markWhole != 0)
    {   /* the mark: this game's own save the world server has not confirmed; its 'sent' mark was the row's own stamp unechoed */
        m->h.rowKnown = 1; m->h.row = mr; m->h.pending = mr.mirrorSeq == 0ULL ? 1 : 0; m->h.sent = mirrorkeep::UnsentSideWasSent(mr);
        m->h.row.size = 0; m->h.row.mtime = 0; m->h.row.crc = 0;   /* restamped from the plain mirror at every load */
    }
    m->h.gen = 1;
    m->result = kMigBuilt;
}
/* an old file moved into <its folder>\moved-old\ (made when missing), never replacing one there: a name taken gets "-<n>" (n up to 99).
   Returns the new path, "" = not moved (it stays where it is). */
inline std::string MoveOld(const std::string& path)
{
    const size_t cut = path.find_last_of("\\/");
    if (cut == std::string::npos) return std::string();
    const std::string dir = path.substr(0, cut) + "\\" + kMovedOldDir;
    U8CreateDirectory(dir.c_str(), 0);
    const std::string name = path.substr(cut + 1);
    std::string to = dir + "\\" + name;
    for (int i = 1; i < 100 && U8GetFileAttributes(to.c_str()) != INVALID_FILE_ATTRIBUTES; ++i) { char b[16]; std::sprintf(b, "-%d", i); to = dir + "\\" + name + b; }
    if (U8GetFileAttributes(to.c_str()) != INVALID_FILE_ATTRIBUTES) return std::string();
    return U8MoveFileEx(path.c_str(), to.c_str(), 0) != 0 ? to : std::string();
}

/* THE ROAD OF ONE AREA AT THE SCAN (review of 01a710593 item 4 and its re-check). A whole state naming this area - taken. No whole state:
   NO .area at all and old side files there (a crash during the migration's first commit left only a torn temp) - migrated from them;
   NO .area and no old files - NOT KNOWN; a .area there that reads but is not whole, or names another area - NOT KNOWN, its bytes set
   aside and the file moved out of the way (old side files stay where they are, never read over it); a .area there that cannot be read
   now (held by another program) - NOT KNOWN for this launch, left where it is: nothing moved, set aside or rebuilt over it. */
enum { kScanTake = 0, kScanMigrate = 1, kScanNotKnownMove = 2, kScanNotKnownLeave = 3 };
inline int ScanRoad(int pick, int keyOk, int areaThere, int areaReads, int oldThere)
{
    if (pick != kPickNone && keyOk != 0) return kScanTake;
    if (areaThere == 0) return oldThere != 0 ? (int)kScanMigrate : (int)kScanNotKnownMove;
    return areaReads != 0 ? (int)kScanNotKnownMove : (int)kScanNotKnownLeave;
}
/* AN AREA STATE DROPPED with its record (review of 01a710593 item 5): its files are deleted, unless it held an own pending save whose
   bytes were not set aside - then they are moved to moved-old\ (never deleted; a file that cannot be moved stays where it is). */
inline int DropKeeps(int pending, int asideOk) { return (pending != 0 && asideOk == 0) ? 1 : 0; }
inline void DropFiles(const std::string& mirrorFile, int keep, std::string* areaTo, std::string* tmpTo)
{
    areaTo->clear(); tmpTo->clear();
    const std::string area = AreaName(mirrorFile), tmp = TmpName(mirrorFile);
    if (keep != 0)
    {
        if (There(area)) *areaTo = MoveOld(area);
        if (There(tmp)) *tmpTo = MoveOld(tmp);
        return;
    }
    U8DeleteFile(tmp.c_str());
    U8DeleteFile(area.c_str());
}

}   /* namespace areastate */
