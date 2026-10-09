// ownstore.cpp - mmo1: THE DISK HALF of the own-squad record writer. See ownstore.h.
//
// WHAT RUNS WHERE (design section 2 "The writer thread"): the engine work - the serialise and GameDataContainer::save's
// own temp file - stays on the main thread (store.cpp). EVERYTHING HERE is plain Win32 file I/O and runs on ONE plugin
// thread fed by a queue guarded by a CRITICAL_SECTION (two threads really do touch it). C++03, CreateThread, no
// std::thread. Nothing here dereferences engine memory.
//
// ONE RECORD, IN ORDER:
//   1. <key>.rec.<pid>.tmp is written and FlushFileBuffers'd (never the live file - nothing is truncated in place);
//   2. the live <key>.rec, if any, is renamed to <key>.rec.prev (one previous version kept);
//   3. the temp file is renamed to <key>.rec (MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
//   4. <key>.rec is READ BACK and its length + CRC-32 compared (coopown::CommitVerdict);
//   5. only then the index (own.index, one line per squad) is rewritten the same temp-and-rename way - THE COMMIT POINT.
// A failure at 1-3 is writeFail, at 4 verifyFail, at 5 indexFail; in every case the old index line still names the
// last good version (now .rec.prev after step 2), which is what mmo2's loader must read.
#include "ownstore.h"
#include "../common/ownrec.h"
#include "../common/storemeta.h"
#include "../common/diskformat.h"   /* owner 429: the records store's format number */
#include "../common/restoreguard.h"   /* restore1b1: the checkpoint tag rule and the prune selection */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>   /* restore1b1: SHFileOperationA - a pruned checkpoint goes to the Recycle Bin */
#include <objbase.h>   /* restore1b1 fold: CoInitializeEx on the records writer thread, where the recycle now runs */
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")   /* fold 2: the Recycle Bin policy (registry) */
#include "coop_log.h"

#include <map>
#include "u8file.h"   /* UTF-8 paths through the wide Windows file calls */

namespace coop {

namespace {

struct OwnJob
{
    std::vector<char>  bytes;
    unsigned int       crc;
    unsigned long long seq;
    long long          writtenAt;
    int                chars;
    unsigned int       epoch; unsigned long long nbSeq; bool stamped;   /* restore1c: o2 */
    OwnJob() : crc(0), seq(0), writtenAt(0), chars(0), epoch(0), nbSeq(0), stamped(false) {}
};

CRITICAL_SECTION g_ownCs;
volatile long    g_ownCsInit = 0;
HANDLE           g_ownEvent = 0;
HANDLE           g_ownThread = 0;
std::map<std::string, OwnJob> g_ownQ;                       /* under g_ownCs */
std::string      g_ownDir;                                  /* under g_ownCs */
std::map<std::string, coopown::IndexLine> g_ownIndex;       /* WRITER THREAD ONLY after open (open fills it with the thread idle) */
unsigned long long g_ownBusySeq = 0;   /* mmo4 fold: the seq of the job being written, under g_ownCs (valid while g_ownBusy) */
volatile long    g_ownBusy = 0;
/* restore1b1 fold (review item 4): checkpoint jobs for THIS thread, run between two records; their results for the main thread. */
struct OwnCpJob { std::string tag, world; unsigned int cap; OwnCpJob() : cap(0) {} };
std::vector<OwnCpJob>    g_ownCpJobs;    /* under g_ownCs */
std::vector<OwnCpResult> g_ownCpNotes;   /* under g_ownCs; drained by OwnStoreCheckpointResults (main thread) */
volatile LONG64 g_osCpNotesDropped = 0;   /* fold 2 */
unsigned int g_ownStampEpoch = 0; unsigned long long g_ownStampSeq = 0; bool g_ownStampKnown = false;   /* restore1c: under g_ownCs */
void CheckpointRunOnWriter(const std::string& dir, const OwnCpJob& j);   /* WRITER THREAD; defined at the end of this file, the same (anonymous) namespace */
volatile LONG64  g_osCommitted = 0, g_osWriteFail = 0, g_osVerifyFail = 0, g_osIndexFail = 0, g_osReplaced = 0,
                 g_osBytes = 0, g_osDiskUsMax = 0, g_osDiskUsSum = 0;
volatile long    g_osLoggedWriteFail = 0, g_osLoggedVerifyFail = 0, g_osLoggedIndexFail = 0;
/* mmo1b (review-mmo1 item 1): THE WRITER THREAD NEVER LOGS - ErrorLog/DebugLog are not thread-safe. The first failure of
   each kind is recorded here as plain data under g_ownCs, and OwnStoreDrainFailLog (main thread, every OwnTick) logs it. */
struct OwnFailNote { int pending; unsigned long err; char path[300]; };
OwnFailNote      g_osFail[3];                               /* 0 writeFail, 1 verifyFail, 2 indexFail; under g_ownCs */
volatile long    g_osFailPending = 0;
/* mmo1b (review-mmo1 item 4): key -> the last COMMITTED record's length and CRC; under g_ownCs. */
std::map<std::string, std::pair<long long, unsigned int> > g_ownCommitted;
std::map<std::string, unsigned long long> g_ownCommittedSeq;   /* mmo6: key -> the last COMMITTED seq; under g_ownCs */
std::string      g_ownIdWorld, g_ownIdProfile;              /* mmo2 fold: the index header's identity; under g_ownCs */
volatile LONG64  g_osRotated = 0, g_osRotateSkipped = 0, g_osTmpDeleted = 0, g_osTmpKept = 0;

void OwnCsInit()
{
    if (::InterlockedCompareExchange(&g_ownCsInit, 1, 0) == 0)
    {
        ::InitializeCriticalSection(&g_ownCs);
        g_ownEvent = ::CreateEventA(0, FALSE, FALSE, 0);
    }
}

bool WriteAllFlushed(const std::string& path, const char* p, size_t n)
{
    HANDLE h = U8CreateFile(path.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    size_t done = 0;
    while (ok && done < n)
    {
        DWORD w = 0;
        const DWORD want = (DWORD)((n - done) > 0x100000 ? 0x100000 : (n - done));
        if (!::WriteFile(h, p + done, want, &w, 0) || w == 0) ok = false;
        done += w;
    }
    if (ok && !::FlushFileBuffers(h)) ok = false;
    ::CloseHandle(h);
    if (!ok) U8DeleteFile(path.c_str());
    return ok;
}

bool ReadAll(const std::string& path, std::vector<char>* out)
{
    out->clear();
    HANDLE h = U8CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; sz.QuadPart = 0;
    bool ok = ::GetFileSizeEx(h, &sz) != 0 && sz.QuadPart >= 0 && sz.QuadPart < (64LL << 20);
    if (ok && sz.QuadPart > 0)
    {
        out->resize((size_t)sz.QuadPart);
        DWORD r = 0;
        ok = ::ReadFile(h, &(*out)[0], (DWORD)sz.QuadPart, &r, 0) != 0 && (LONG64)r == sz.QuadPart;
    }
    ::CloseHandle(h);
    return ok;
}

bool MoveReplace(const std::string& from, const std::string& to)
{
    return U8MoveFileEx(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

std::string Join(const std::string& dir, const std::string& name) { return dir + "\\" + name; }

void MaxInto(volatile LONG64* slot, LONG64 v)
{
    for (;;)
    {
        const LONG64 cur = ::InterlockedCompareExchange64(slot, 0, 0);
        if (v <= cur) return;
        if (::InterlockedCompareExchange64(slot, v, cur) == cur) return;
    }
}

/* WRITER THREAD. Records a failure for the main thread to log (mmo1b item 1). No logging, no engine call. */
void NoteFail(int kind, unsigned long err, const std::string& path)
{
    ::EnterCriticalSection(&g_ownCs);
    OwnFailNote& n = g_osFail[kind];
    n.pending = 1; n.err = err;
    size_t m = 0;
    for (; m < path.size() && m < sizeof(n.path) - 1; ++m) n.path[m] = path[m];
    n.path[m] = 0;
    ::LeaveCriticalSection(&g_ownCs);
    ::InterlockedExchange(&g_osFailPending, 1);
}

/* WRITER THREAD. The index file, rewritten whole (one line per squad; ~100 bytes each) through temp + rename. */
bool WriteIndex(const std::string& dir)
{
    std::string all;
    {   /* mmo2 fold (review-mmo2 item 5): the store's identity is the index's first line */
        ::EnterCriticalSection(&g_ownCs);
        const std::string w = g_ownIdWorld, p = g_ownIdProfile;
        ::LeaveCriticalSection(&g_ownCs);
        std::string head;
        if (coopown::EncodeIndexHeader(w, p, &head)) all = head;
    }
    for (std::map<std::string, coopown::IndexLine>::const_iterator it = g_ownIndex.begin(); it != g_ownIndex.end(); ++it)
    {
        std::string line;
        if (!coopown::EncodeIndexLine(it->second, &line)) return false;
        all += line;
    }
    char pid[24]; _snprintf(pid, 23, "%lu", (unsigned long)::GetCurrentProcessId()); pid[23] = 0;
    const std::string tmp = Join(dir, std::string(coopown::IndexFile()) + "." + pid + ".tmp");
    if (!WriteAllFlushed(tmp, all.data(), all.size())) return false;
    if (!MoveReplace(tmp, Join(dir, coopown::IndexFile()))) { U8DeleteFile(tmp.c_str()); return false; }
    return true;
}

/* WRITER THREAD. Steps 1-5 of the file comment. */
void CommitOne(const std::string& dir, const std::string& key, const OwnJob& j)
{
    LARGE_INTEGER f, t0, t1; ::QueryPerformanceFrequency(&f); ::QueryPerformanceCounter(&t0);
    const std::string live = Join(dir, coopown::RecFile(key));
    const std::string prev = Join(dir, coopown::PrevFile(key));
    const std::string tmp  = Join(dir, coopown::TmpFile(key, (unsigned long)::GetCurrentProcessId()));
    unsigned long err = 0;
    bool written = !j.bytes.empty() && WriteAllFlushed(tmp, &j.bytes[0], j.bytes.size());
    if (!written) err = ::GetLastError();
    if (written)
    {
        /* mmo1b (review-mmo1 item 3): ONLY THE INDEXED VERSION IS ROTATED ONTO .prev. A live file that is not what the
           index names (a failed verify/index step left it) is replaced, and .prev - the indexed version - is kept. */
        if (U8GetFileAttributes(live.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            std::map<std::string, coopown::IndexLine>::const_iterator ix = g_ownIndex.find(key);
            const bool haveIx = ix != g_ownIndex.end();
            std::vector<char> cur;
            const bool curRead = haveIx && ReadAll(live, &cur);
            const unsigned int curCrc = curRead && !cur.empty() ? coopstore::Crc32(&cur[0], cur.size()) : 0;
            if (coopown::RotateToPrev(haveIx, curRead, (long long)cur.size(), curCrc, haveIx ? ix->second.len : 0, haveIx ? ix->second.crc : 0))
            {
                if (!MoveReplace(live, prev)) { written = false; err = ::GetLastError(); }
                else ::InterlockedIncrement64(&g_osRotated);
            }
            else ::InterlockedIncrement64(&g_osRotateSkipped);
        }
        if (written && !MoveReplace(tmp, live)) { written = false; err = ::GetLastError(); }
        if (!written) U8DeleteFile(tmp.c_str());
    }
    std::vector<char> back;
    const bool readBack = written && ReadAll(live, &back);
    const unsigned int backCrc = readBack && !back.empty() ? coopstore::Crc32(&back[0], back.size()) : 0;
    const int v = coopown::CommitVerdict(written, readBack, (long long)back.size(), backCrc, (long long)j.bytes.size(), j.crc);
    if (v == coopown::kCommitWriteFail)
    {
        ::InterlockedIncrement64(&g_osWriteFail);
        if (::InterlockedExchange(&g_osLoggedWriteFail, 1) == 0) NoteFail(0, err, live);   /* mmo1b: logged by the main thread */
        return;
    }
    if (v == coopown::kCommitVerifyFail)
    {
        ::InterlockedIncrement64(&g_osVerifyFail);
        if (::InterlockedExchange(&g_osLoggedVerifyFail, 1) == 0) NoteFail(1, 0, live);   /* mmo1b: logged by the main thread */
        return;
    }
    coopown::IndexLine l;
    l.key = key; l.seq = j.seq; l.len = (long long)j.bytes.size(); l.crc = j.crc; l.writtenAt = j.writtenAt; l.chars = j.chars;
    l.stamped = j.stamped; l.epoch = j.epoch; l.nbSeq = j.nbSeq;   /* restore1c: o2 */
    /* mmo1b: a squad with no line before this write gets NO line back on failure (operator[] used to leave an empty one) */
    std::map<std::string, coopown::IndexLine>::iterator had = g_ownIndex.find(key);
    const bool existed = had != g_ownIndex.end();
    const coopown::IndexLine old = existed ? had->second : coopown::IndexLine();
    g_ownIndex[key] = l;
    if (!WriteIndex(dir))
    {
        const unsigned long ierr = ::GetLastError();
        if (existed) g_ownIndex[key] = old; else g_ownIndex.erase(key);
        ::InterlockedIncrement64(&g_osIndexFail);
        if (::InterlockedExchange(&g_osLoggedIndexFail, 1) == 0) NoteFail(2, ierr, dir);   /* mmo1b: logged by the main thread */
        return;
    }
    ::EnterCriticalSection(&g_ownCs);   /* mmo1b item 4: the skip-unchanged test's reference is set only here, on a commit */
    g_ownCommitted[key] = std::make_pair(l.len, l.crc);
    g_ownCommittedSeq[key] = l.seq;   /* mmo6 */
    ::LeaveCriticalSection(&g_ownCs);
    ::QueryPerformanceCounter(&t1);
    const LONG64 us = f.QuadPart ? (LONG64)((t1.QuadPart - t0.QuadPart) * 1000000LL / f.QuadPart) : 0;
    ::InterlockedIncrement64(&g_osCommitted);
    ::InterlockedExchangeAdd64(&g_osBytes, (LONG64)j.bytes.size());
    ::InterlockedExchangeAdd64(&g_osDiskUsSum, us);
    MaxInto(&g_osDiskUsMax, us);
}

DWORD WINAPI OwnWriterMain(LPVOID)
{
    const HRESULT coHr = ::CoInitializeEx(0, COINIT_APARTMENTTHREADED);   /* restore1b1 fold: SHFileOperationA (the checkpoint prune) runs on THIS thread */
    for (;;)
    {
        ::WaitForSingleObject(g_ownEvent, 500);
        for (;;)
        {
            std::string key, dir; OwnJob job; bool have = false;
            OwnCpJob cp; bool haveCp = false;   /* restore1b1 fold (review item 4): a checkpoint runs HERE, between two records */
            ::EnterCriticalSection(&g_ownCs);
            if (!g_ownCpJobs.empty())
            {
                cp = g_ownCpJobs.front(); g_ownCpJobs.erase(g_ownCpJobs.begin()); dir = g_ownDir; haveCp = true;
                g_ownBusySeq = 0;   /* no record in hand: the leave drain never waits on a checkpoint */
                ::InterlockedExchange(&g_ownBusy, 1);
            }
            else if (!g_ownQ.empty())
            {
                std::map<std::string, OwnJob>::iterator it = g_ownQ.begin();
                key = it->first; job = it->second; dir = g_ownDir; g_ownQ.erase(it); have = true;
                g_ownBusySeq = job.seq;   /* mmo4 fold */
                ::InterlockedExchange(&g_ownBusy, 1);
            }
            ::LeaveCriticalSection(&g_ownCs);
            if (haveCp) { CheckpointRunOnWriter(dir, cp); ::InterlockedExchange(&g_ownBusy, 0); continue; }   /* restore1b1 fold */
            if (!have) break;
            CommitOne(dir, key, job);
            ::InterlockedExchange(&g_ownBusy, 0);
        }
    }
    if (SUCCEEDED(coHr)) ::CoUninitialize();   /* restore1b1 fold: the matching end (the loop above never leaves; kept for the pairing) */
    return 0;
}

bool MakeDirs(const std::string& dir)
{
    for (std::string::size_type i = 3; i <= dir.size(); ++i)
    {
        if (i == dir.size() || dir[i] == '\\' || dir[i] == '/')
        {
            const std::string part = dir.substr(0, i);
            if (U8GetFileAttributes(part.c_str()) == INVALID_FILE_ATTRIBUTES && !U8CreateDirectory(part.c_str(), 0)
                && ::GetLastError() != ERROR_ALREADY_EXISTS) return false;
        }
    }
    const DWORD a = U8GetFileAttributes(dir.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif
bool PidAliveOther(unsigned long pid)
{
    if (pid == 0 || pid == ::GetCurrentProcessId()) return false;
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h == 0) return false;
    DWORD code = 0;
    const bool alive = ::GetExitCodeProcess(h, &code) != 0 && code == STILL_ACTIVE;
    ::CloseHandle(h);
    return alive;
}
/* MAIN THREAD, writer idle (mmo1b item 7): leftover *.tmp from a process that died mid-write are deleted at store
   selection. A name carrying the id of ANOTHER running process (a dot-separated all-digit part) is left alone. */
void CleanTmp(const std::string& dir)
{
    U8FindData fd;
    HANDLE h = U8FindFirstFile(Join(dir, "*.tmp").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do
    {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        const std::string name(fd.cFileName);
        bool other = false, allDig = true;
        unsigned long v = 0; size_t n = 0;
        for (std::string::size_type i = 0; i <= name.size(); ++i)
        {
            const char ch = i < name.size() ? name[i] : '.';
            if (ch == '.') { if (n > 0 && allDig && PidAliveOther(v)) other = true; v = 0; n = 0; allDig = true; }
            else { ++n; if (ch >= '0' && ch <= '9') { if (v < 100000000UL) v = v * 10 + (unsigned long)(ch - '0'); } else allDig = false; }
        }
        if (other) ::InterlockedIncrement64(&g_osTmpKept);
        else if (U8DeleteFile(Join(dir, name).c_str())) ::InterlockedIncrement64(&g_osTmpDeleted);
    } while (U8FindNextFile(h, &fd));
    ::FindClose(h);
}

}   // namespace

bool OwnStoreOpen(const std::string& dir, unsigned long long* maxSeq, int* badLines)
{
    OwnCsInit();
    *maxSeq = 0; *badLines = 0;
    ::EnterCriticalSection(&g_ownCs);
    const bool idle = g_ownQ.empty() && g_ownCpJobs.empty() && ::InterlockedCompareExchange(&g_ownBusy, 0, 0) == 0;   /* restore1b1 fold: no checkpoint waiting either */
    ::LeaveCriticalSection(&g_ownCs);
    if (!idle) return false;
    if (!MakeDirs(dir)) return false;
    CleanTmp(dir);   /* mmo1b item 7 */
    std::vector<char> b;
    std::vector<std::string> lines;
    if (ReadAll(Join(dir, coopown::IndexFile()), &b) && !b.empty())
    {
        std::string all(b.begin(), b.end()), cur;
        for (std::string::size_type i = 0; i < all.size(); ++i)
        {
            if (all[i] == '\n') { lines.push_back(cur); cur.clear(); } else cur += all[i];
        }
        if (!cur.empty()) lines.push_back(cur);
    }
    *maxSeq = coopown::MaxSeqOf(lines, badLines);
    ::EnterCriticalSection(&g_ownCs);   /* the thread is idle and the queue empty (checked above): the index is ours */
    g_ownIndex.clear();
    g_ownCommitted.clear();   /* mmo1b item 4: what the index names IS the last committed record */
    g_ownCommittedSeq.clear();   /* mmo6 */
    for (std::vector<std::string>::size_type i = 0; i < lines.size(); ++i)
    {
        coopown::IndexLine l;
        if (coopown::ParseIndexLine(lines[i], &l)) { g_ownIndex[l.key] = l; g_ownCommitted[l.key] = std::make_pair(l.len, l.crc); g_ownCommittedSeq[l.key] = l.seq; /* mmo6 */ }
    }
    g_ownDir = dir;
    ::LeaveCriticalSection(&g_ownCs);
    if (g_ownThread == 0)
    {
        DWORD tid = 0;
        g_ownThread = ::CreateThread(0, 0, OwnWriterMain, 0, 0, &tid);
        if (g_ownThread == 0) return false;
    }
    return true;
}

int OwnStoreFormatCheck(const std::string& dir, unsigned int* found, std::string* why)
{
    const std::string p = Join(dir, swnames::kFormatFile);
    const bool exists = U8GetFileAttributes(p.c_str()) != INVALID_FILE_ATTRIBUTES;
    std::vector<char> b; std::string t;
    if (exists && ReadAll(p, &b)) t.assign(b.begin(), b.end());
    const int rs = swformat::FormatParse(exists, t, swformat::kKindRecords, found, why);
    return swformat::FormatDecide(rs, *found, swformat::kRecordsFolderFormat);
}
/* Step 0 -> 1 is the number alone, so no step touches the records; format.txt is written last (temp file, then replaced). */
bool ConvertRecordsFolder(const std::string& dir, unsigned int from)
{
    if (swformat::FormatSteps(from, swformat::kRecordsFolderFormat).empty()) return true;   /* already this build's number */
    const std::string t = swformat::FormatText(swformat::kKindRecords, swformat::kRecordsFolderFormat);
    const std::string p = Join(dir, swnames::kFormatFile), tmp = p + ".tmp";
    return WriteAllFlushed(tmp, t.data(), t.size()) && MoveReplace(tmp, p);
}

void OwnStoreEnqueue(const std::string& key, const std::vector<char>& bytes, unsigned int crc, unsigned long long seq,
                     long long writtenAt, int chars)
{
    OwnCsInit();
    ::EnterCriticalSection(&g_ownCs);
    OwnJob& j = g_ownQ[key];
    if (!j.bytes.empty()) ::InterlockedIncrement64(&g_osReplaced);
    j.bytes = bytes; j.crc = crc; j.seq = seq; j.writtenAt = writtenAt; j.chars = chars;
    j.epoch = g_ownStampEpoch; j.nbSeq = g_ownStampSeq; j.stamped = g_ownStampKnown;   /* restore1c */
    ::LeaveCriticalSection(&g_ownCs);
    if (g_ownEvent) ::SetEvent(g_ownEvent);
}

void OwnStoreStats(OwnStoreStat* o)
{
    OwnCsInit();
    o->committed = ::InterlockedCompareExchange64(&g_osCommitted, 0, 0);
    o->writeFail = ::InterlockedCompareExchange64(&g_osWriteFail, 0, 0);
    o->verifyFail = ::InterlockedCompareExchange64(&g_osVerifyFail, 0, 0);
    o->indexFail = ::InterlockedCompareExchange64(&g_osIndexFail, 0, 0);
    o->replacedQueued = ::InterlockedCompareExchange64(&g_osReplaced, 0, 0);
    o->bytesCommitted = ::InterlockedCompareExchange64(&g_osBytes, 0, 0);
    o->diskUsMax = ::InterlockedCompareExchange64(&g_osDiskUsMax, 0, 0);
    o->diskUsSum = ::InterlockedCompareExchange64(&g_osDiskUsSum, 0, 0);
    o->rotated = ::InterlockedCompareExchange64(&g_osRotated, 0, 0);   /* mmo1b */
    o->rotateSkipped = ::InterlockedCompareExchange64(&g_osRotateSkipped, 0, 0);
    o->tmpDeleted = ::InterlockedCompareExchange64(&g_osTmpDeleted, 0, 0);
    o->tmpKept = ::InterlockedCompareExchange64(&g_osTmpKept, 0, 0);
    ::EnterCriticalSection(&g_ownCs);
    o->queued = (long long)g_ownQ.size();
    ::LeaveCriticalSection(&g_ownCs);
    o->busy = (int)::InterlockedCompareExchange(&g_ownBusy, 0, 0);
}

/* mmo4 fold (review-mmo4 item 7): the leave save's drain waits for the records queued before it began, not for an empty
   queue. The lowest seq still queued or being written; 0 = nothing pending. */
unsigned long long OwnStoreOldestPendingSeq()
{
    OwnCsInit();
    unsigned long long lo = 0;
    ::EnterCriticalSection(&g_ownCs);
    for (std::map<std::string, OwnJob>::const_iterator it = g_ownQ.begin(); it != g_ownQ.end(); ++it)
        if (it->second.seq != 0 && (lo == 0 || it->second.seq < lo)) lo = it->second.seq;
    if (::InterlockedCompareExchange(&g_ownBusy, 0, 0) != 0 && g_ownBusySeq != 0 && (lo == 0 || g_ownBusySeq < lo)) lo = g_ownBusySeq;
    ::LeaveCriticalSection(&g_ownCs);
    return lo;
}

bool OwnStoreLastFor(const std::string& key, long long* len, unsigned int* crc)
{
    OwnCsInit();
    bool have = false;
    ::EnterCriticalSection(&g_ownCs);
    std::map<std::string, OwnJob>::const_iterator q = g_ownQ.find(key);
    if (q != g_ownQ.end()) { *len = (long long)q->second.bytes.size(); *crc = q->second.crc; have = true; }
    else
    {
        std::map<std::string, std::pair<long long, unsigned int> >::const_iterator k = g_ownCommitted.find(key);
        if (k != g_ownCommitted.end()) { *len = k->second.first; *crc = k->second.second; have = true; }
    }
    ::LeaveCriticalSection(&g_ownCs);
    return have;
}

void OwnStoreDrainFailLog()
{
    if (::InterlockedCompareExchange(&g_ownCsInit, 0, 0) == 0 || ::InterlockedExchange(&g_osFailPending, 0) == 0) return;
    OwnFailNote n[3];
    ::EnterCriticalSection(&g_ownCs);
    for (int i = 0; i < 3; ++i) { n[i] = g_osFail[i]; g_osFail[i].pending = 0; }
    ::LeaveCriticalSection(&g_ownCs);
    if (n[0].pending)
        ErrorLog("[OWNSAVE] writeFail: could not write/rename " + std::string(n[0].path) + " (GetLastError " + coopown::Hex8(n[0].err)
                 + ") - counted; logged once (by the main thread; the writer thread never logs)");
    if (n[1].pending)
        ErrorLog("[OWNSAVE] verifyFail: " + std::string(n[1].path) + " read back with the wrong length or CRC - its index line was NOT"
                 " written; logged once");
    if (n[2].pending)
        ErrorLog("[OWNSAVE] indexFail: own.index in " + std::string(n[2].path) + " could not be rewritten (GetLastError "
                 + coopown::Hex8(n[2].err) + ") - the record file is newer than its line; logged once");
}

void OwnStoreSetIdentity(const std::string& world, const std::string& profile)
{
    OwnCsInit();
    ::EnterCriticalSection(&g_ownCs);
    g_ownIdWorld = world; g_ownIdProfile = profile;
    ::LeaveCriticalSection(&g_ownCs);
}

std::string OwnStoreDir()
{
    OwnCsInit();
    ::EnterCriticalSection(&g_ownCs);
    const std::string d = g_ownDir;
    ::LeaveCriticalSection(&g_ownCs);
    return d;
}

/* ---- mmo6 ---- */
bool OwnStoreCommittedSeq(const std::string& key, unsigned long long* seq)
{
    OwnCsInit();
    bool have = false;
    ::EnterCriticalSection(&g_ownCs);
    std::map<std::string, unsigned long long>::const_iterator k = g_ownCommittedSeq.find(key);
    if (k != g_ownCommittedSeq.end()) { *seq = k->second; have = true; }
    ::LeaveCriticalSection(&g_ownCs);
    return have;
}

bool OwnLedgerAppend(const std::string& line, unsigned int cap, int* linesNow)
{
    const std::string dir = OwnStoreDir();
    if (dir.empty()) return false;
    const std::string path = Join(dir, coopown::LedgerFile());
    std::vector<std::string> lines;
    std::vector<char> b;
    /* mmo6 fold (review-mmo6 MED 4): only "not found" is an empty ledger; an existing file that cannot be read refuses */
    const DWORD attr = U8GetFileAttributes(path.c_str());
    const DWORD aerr = (attr == INVALID_FILE_ATTRIBUTES) ? ::GetLastError() : 0;
    const bool notFound = attr == INVALID_FILE_ATTRIBUTES && (aerr == ERROR_FILE_NOT_FOUND || aerr == ERROR_PATH_NOT_FOUND);
    const bool readOk = !notFound && ReadAll(path, &b);
    const int verdict = coopown::LedgerLoadVerdict(notFound, readOk);
    if (verdict == coopown::kLedgerLoadRefuse) return false;
    int unknownKept = 0;
    if (verdict == coopown::kLedgerLoadUse && !b.empty()) coopown::LedgerSplitKeep(std::string(b.begin(), b.end()), &lines, &unknownKept);
    (void)unknownKept;
    std::string ln = line;
    while (!ln.empty() && (ln[ln.size() - 1] == '\n' || ln[ln.size() - 1] == '\r')) ln.erase(ln.size() - 1);
    lines.push_back(ln);
    coopown::LedgerKeepLast(&lines, cap);
    std::string all;
    for (size_t i = 0; i < lines.size(); ++i) { all += lines[i]; all += '\n'; }
    char pid[16];
    std::sprintf(pid, "%lu", (unsigned long)::GetCurrentProcessId());
    const std::string tmp = path + "." + pid + ".tmp";   /* CleanTmp's pid rule leaves a live process's temp alone */
    if (!WriteAllFlushed(tmp, all.data(), all.size())) { U8DeleteFile(tmp.c_str()); return false; }
    bool moved = MoveReplace(tmp, path);
    if (!moved)   /* restore1b1 fold 2 (recheck item 2): once more after a sharing/access error (a checkpoint copy was reading it) */
    {
        const DWORD e = ::GetLastError();
        if (e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED) { ::Sleep(20); moved = MoveReplace(tmp, path); }
    }
    if (!moved) { U8DeleteFile(tmp.c_str()); return false; }
    if (linesNow != 0) *linesNow = (int)lines.size();
    return true;
}

bool OwnLedgerRead(std::vector<coopown::LedgerEntry>* out, int* badLines)
{
    if (badLines != 0) *badLines = 0;
    const std::string dir = OwnStoreDir();
    if (dir.empty() || out == 0) return false;
    std::vector<char> b;
    if (!ReadAll(Join(dir, coopown::LedgerFile()), &b)) return false;
    std::vector<std::string> lines;
    const int bad = b.empty() ? 0 : coopown::LedgerSplit(std::string(b.begin(), b.end()), &lines);
    if (badLines != 0) *badLines = bad;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        coopown::LedgerEntry e;
        if (coopown::LedgerLineParse(lines[i], &e) != 0) out->push_back(e);
    }
    return true;
}

/* ---- restore1b1 ---- */
unsigned long long OwnStoreCommittedHigh()
{
    OwnCsInit();
    unsigned long long hi = 0ULL;
    ::EnterCriticalSection(&g_ownCs);
    for (std::map<std::string, unsigned long long>::const_iterator it = g_ownCommittedSeq.begin(); it != g_ownCommittedSeq.end(); ++it)
        if (it->second > hi) hi = it->second;
    ::LeaveCriticalSection(&g_ownCs);
    return hi;
}

namespace {
bool CheckpointCopyFile(const std::string& from, const std::string& to, long long* bytes)
{
    if (!U8CopyFile(from.c_str(), to.c_str(), FALSE)) return false;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (U8GetFileAttributesEx(to.c_str(), GetFileExInfoStandard, &a)) *bytes += ((long long)a.nFileSizeHigh << 32) | (long long)a.nFileSizeLow;
    return true;
}
/* THE PLUGIN'S RECYCLE CALL (store.cpp's deleted-profile folder): FO_DELETE with FOF_ALLOWUNDO - the Recycle Bin, never a hard delete. */
bool CheckpointRecycle(const std::string& dir)
{
    std::vector<char> from(dir.begin(), dir.end()); from.push_back(0); from.push_back(0);   /* SHFileOperation wants a double-NUL list */
    SHFILEOPSTRUCTA op; std::memset(&op, 0, sizeof(op));
    op.wFunc = FO_DELETE; op.pFrom = &from[0];
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_WANTNUKEWARNING | FOF_SILENT | FOF_NOERRORUI;
    return U8SHFileOperation(&op) == 0 && !op.fAnyOperationsAborted;
}
}   // namespace

namespace {
unsigned long long CheckpointFolderBytes(const std::string& dir)   /* a checkpoint folder is flat: its files' sizes */
{
    unsigned long long n = 0ULL;
    U8FindData fd;
    HANDLE h = U8FindFirstFile(Join(dir, "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0ULL;
    do { if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) n += ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow; } while (U8FindNextFile(h, &fd));
    ::FindClose(h);
    return n;
}
/* restore1b1 fold 2 (recheck item 1): THE DRIVE'S RECYCLE BIN POLICY. SHQueryRecycleBinA answers even for a bin set to "delete
   immediately", where FOF_WANTNUKEWARNING would raise a modal prompt. root = "X:\". False = the policy cannot be read. */
bool BinPolicyRead(const std::string& root, unsigned long* nuke, unsigned long* maxMb)
{
    char vol[MAX_PATH]; vol[0] = 0;
    if (!::GetVolumeNameForVolumeMountPointA(root.c_str(), vol, MAX_PATH)) return false;
    const std::string guid = restoreguard::VolumeGuidOf(vol);
    if (guid.empty()) return false;
    const std::string base = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\BitBucket";
    HKEY k = 0;
    if (::RegOpenKeyExA(HKEY_CURRENT_USER, (base + "\\Volume\\" + guid).c_str(), 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    DWORD t = 0, v = 0, m = 0, n = sizeof(v);
    bool ok = ::RegQueryValueExA(k, "NukeOnDelete", 0, &t, (LPBYTE)&v, &n) == ERROR_SUCCESS && t == REG_DWORD;
    n = sizeof(m);
    ok = ok && ::RegQueryValueExA(k, "MaxCapacity", 0, &t, (LPBYTE)&m, &n) == ERROR_SUCCESS && t == REG_DWORD;
    ::RegCloseKey(k);
    if (!ok) return false;
    *nuke = v; *maxMb = m;
    if (::RegOpenKeyExA(HKEY_CURRENT_USER, base.c_str(), 0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS)   /* the older, global setting */
    {
        DWORD g = 0; n = sizeof(g);
        if (::RegQueryValueExA(k, "NukeOnDelete", 0, &t, (LPBYTE)&g, &n) == ERROR_SUCCESS && t == REG_DWORD && g != 0) *nuke = 1;
        ::RegCloseKey(k);
    }
    return true;
}
/* fold (review item 6; fold 2 item 1): the drive's Recycle Bin can take this folder AND its policy would not delete it for good -
   asked BEFORE any recycle. */
bool CheckpointBinCanTake(const std::string& dirIn)
{
    char full[MAX_PATH * 4]; full[0] = 0;   /* UTF-8: a 260-character path takes up to 780 bytes */
    const DWORD fn = U8GetFullPathName(dirIn.c_str(), (DWORD)sizeof(full), full, 0);
    if (fn == 0 || fn >= (DWORD)sizeof(full)) return false;
    const std::string dir(full);
    if (dir.size() < 3 || dir[1] != ':' || (dir[2] != '\\' && dir[2] != '/')) return false;
    const std::string root = dir.substr(0, 2) + "\\";
    unsigned long nuke = 1, maxMb = 0;
    if (!restoreguard::BinPolicyAllows(BinPolicyRead(root, &nuke, &maxMb), nuke, maxMb, CheckpointFolderBytes(dir))) return false;
    SHQUERYRBINFO qi; std::memset(&qi, 0, sizeof(qi)); qi.cbSize = sizeof(qi);
    const bool fixed = ::GetDriveTypeA(root.c_str()) == DRIVE_FIXED;
    const bool bin = fixed && SUCCEEDED(::SHQueryRecycleBinA(root.c_str(), &qi));
    ULARGE_INTEGER fr, tot, all; fr.QuadPart = tot.QuadPart = all.QuadPart = 0;
    if (bin && !::GetDiskFreeSpaceExA(root.c_str(), &fr, &tot, &all)) tot.QuadPart = 0;
    return restoreguard::RecycleBinCanTake(fixed, bin, CheckpointFolderBytes(dir), tot.QuadPart);
}
bool PathThere(const std::string& p) { return U8GetFileAttributes(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
/* fold 2 (recheck item 2): pp.ledger is written by the MAIN thread (temp + rename) - read it sharing read, write and delete, so
   this copy never blocks that rename. */
bool CheckpointCopyShared(const std::string& from, const std::string& to, long long* bytes)
{
    HANDLE h = U8CreateFile(from.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::vector<char> b; LARGE_INTEGER sz; sz.QuadPart = 0;
    bool ok = ::GetFileSizeEx(h, &sz) != 0 && sz.QuadPart >= 0 && sz.QuadPart < (64LL << 20);
    if (ok && sz.QuadPart > 0)
    {
        b.resize((size_t)sz.QuadPart); DWORD r = 0;
        ok = ::ReadFile(h, &b[0], (DWORD)sz.QuadPart, &r, 0) != 0 && (LONG64)r == sz.QuadPart;
    }
    ::CloseHandle(h);
    if (!ok || !WriteAllFlushed(to, b.empty() ? "" : &b[0], b.size())) return false;
    *bytes += (long long)b.size();
    return true;
}
/* WRITER THREAD (fold item 4): one checkpoint and its prune, between two records. Never logs - the result is a note. */
void CheckpointRunOnWriter(const std::string& dir, const OwnCpJob& j)
{
    OwnCpResult r; r.tag = j.tag; r.world = j.world; r.rc = 1;
    long long* bytes = &r.bytes;
    const std::string root = Join(Join(dir, "checkpoint"), j.world), done = Join(root, j.tag), part = done + ".part";
    int rc = 1;
    if (dir.empty() || j.tag.empty() || j.world.empty()) rc = -1;
    else if (PathThere(done)) rc = 0;
    else if (!MakeDirs(root)) rc = -3;
    if (rc == 1 && PathThere(part) && (!CheckpointBinCanTake(part) || !CheckpointRecycle(part))) rc = -3;   /* a copy a crash cut short */
    if (rc == 1 && !MakeDirs(part)) rc = -3;
    if (rc == 1)
    {
        /* own.index FIRST: every record it names is then the live .rec or the kept .rec.prev, both copied after it */
        const std::string idx(coopown::IndexFile());
        if (U8GetFileAttributes(Join(dir, idx).c_str()) != INVALID_FILE_ATTRIBUTES && !CheckpointCopyFile(Join(dir, idx), Join(part, idx), bytes)) rc = -3;
        U8FindData fd;
        HANDLE h = rc == 1 ? U8FindFirstFile(Join(dir, "*").c_str(), &fd) : INVALID_HANDLE_VALUE;
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
                const std::string name(fd.cFileName);
                if (name == idx) continue;
                if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".tmp") == 0) continue;
                const bool copied = name == coopown::LedgerFile() ? CheckpointCopyShared(Join(dir, name), Join(part, name), bytes)   /* fold 2 (recheck item 2) */
                                                            : CheckpointCopyFile(Join(dir, name), Join(part, name), bytes);
                if (!copied) { rc = -3; break; }
            } while (U8FindNextFile(h, &fd));
            ::FindClose(h);
        }
        if (rc == 1 && !U8MoveFileEx(part.c_str(), done.c_str(), 0)) rc = -3;
        if (rc != 1 && PathThere(part) && CheckpointBinCanTake(part)) CheckpointRecycle(part);
    }
    r.rc = rc;
    if (rc == 1)
    {
        std::vector<std::string> names;
        U8FindData fd;
        HANDLE h = U8FindFirstFile(Join(root, "*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                const std::string n(fd.cFileName);
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && n != "." && n != "..") names.push_back(n);
            } while (U8FindNextFile(h, &fd));
            ::FindClose(h);
        }
        const std::vector<std::string> out = restoreguard::PruneSelect(names, j.cap);
        bool binTakes = true;
        for (size_t i = 0; i < out.size() && binTakes; ++i) if (!CheckpointBinCanTake(Join(root, out[i]))) binTakes = false;
        if (!binTakes) r.pruneNoBin = (int)out.size();   /* fold (review item 6): nothing is moved - the main thread says so */
        for (size_t i = 0; i < out.size() && binTakes; ++i) { if (CheckpointRecycle(Join(root, out[i]))) ++r.pruned; else ++r.pruneFailed; }
    }
    ::EnterCriticalSection(&g_ownCs);
    if (g_ownCpNotes.size() < 32) g_ownCpNotes.push_back(r);
    else ::InterlockedIncrement64(&g_osCpNotesDropped);   /* fold 2: counted */
    ::LeaveCriticalSection(&g_ownCs);
}
}   // namespace

bool OwnStoreCheckpointRequest(const std::string& tag, const std::string& world, unsigned int cap)
{
    OwnCsInit();
    if (tag.empty() || world.empty()) return false;
    bool queued = false;
    ::EnterCriticalSection(&g_ownCs);
    if (!g_ownDir.empty() && g_ownCpJobs.size() < 8)
    {
        OwnCpJob j; j.tag = tag; j.world = world; j.cap = cap;
        g_ownCpJobs.push_back(j); queued = true;
    }
    ::LeaveCriticalSection(&g_ownCs);
    if (queued && g_ownEvent) ::SetEvent(g_ownEvent);
    return queued;
}

bool OwnStoreCheckpointSettled()   /* fold 2 (recheck item 3) */
{
    OwnCsInit();
    ::EnterCriticalSection(&g_ownCs);
    const bool settled = g_ownCpJobs.empty() && ::InterlockedCompareExchange(&g_ownBusy, 0, 0) == 0;
    ::LeaveCriticalSection(&g_ownCs);
    return settled;
}

long long OwnStoreCheckpointNotesDropped() { return ::InterlockedCompareExchange64(&g_osCpNotesDropped, 0, 0); }

void OwnStoreCheckpointResults(std::vector<OwnCpResult>* out)
{
    OwnCsInit();
    ::EnterCriticalSection(&g_ownCs);
    out->insert(out->end(), g_ownCpNotes.begin(), g_ownCpNotes.end());
    g_ownCpNotes.clear();
    ::LeaveCriticalSection(&g_ownCs);
}

/* ======================= restore1c (design s4): THE REPAIR ON A PLAYER'S PC =======================
   MAIN THREAD with the writer idle (nothing queued, nothing in hand): own.index, the checkpoints and own.repair are read, the pure
   plan (restoreguard::OwnRepairPlan) decides each squad, every post-R file goes to the Recycle Bin in ONE call (never a hard delete,
   never a prompt: the bin policy is asked first, and nothing is touched when it cannot take them), the checkpoint copies are put
   in, the ledger is cut back to its base checkpoint, the index is rewritten (temp + rename) and the store re-opened. */
bool OwnStoreIdleNow()
{
    OwnCsInit();
    ::EnterCriticalSection(&g_ownCs);
    const bool idle = g_ownQ.empty() && g_ownCpJobs.empty() && ::InterlockedCompareExchange(&g_ownBusy, 0, 0) == 0;
    ::LeaveCriticalSection(&g_ownCs);
    return idle;
}
void OwnStoreSetStamp(unsigned int epoch, unsigned long long nbSeq, bool known)
{
    OwnCsInit();
    ::EnterCriticalSection(&g_ownCs);
    g_ownStampEpoch = epoch; g_ownStampSeq = nbSeq; g_ownStampKnown = known;
    ::LeaveCriticalSection(&g_ownCs);
}
namespace {
const char* kOwnRepairFile = "own.repair";
void RpLines(const std::vector<char>& b, std::vector<std::string>* lines)
{
    std::string cur;
    for (size_t i = 0; i < b.size(); ++i)
    {
        if (b[i] == '\n') { if (!cur.empty() && cur[cur.size() - 1] == '\r') cur.erase(cur.size() - 1); lines->push_back(cur); cur.clear(); }
        else cur += b[i];
    }
    if (!cur.empty()) lines->push_back(cur);
}
/* The index of a folder: its identity line (kept as found) and its record lines. False = no index file. */
bool RpReadIndex(const std::string& dir, std::string* header, std::map<std::string, coopown::IndexLine>* out)
{
    std::vector<char> b;
    if (!ReadAll(Join(dir, coopown::IndexFile()), &b)) return false;
    std::vector<std::string> lines; RpLines(b, &lines);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (coopown::IsIndexHeader(lines[i])) { if (header != 0) *header = lines[i] + "\n"; continue; }
        coopown::IndexLine l;
        if (coopown::ParseIndexLine(lines[i], &l)) (*out)[l.key] = l;
    }
    return true;
}
bool RpFileMatches(const std::string& path, long long len, unsigned int crc)
{
    std::vector<char> b;
    if (!ReadAll(path, &b)) return false;
    return (long long)b.size() == len && (b.empty() ? 0u : coopstore::Crc32(&b[0], b.size())) == crc;
}
/* ONE Recycle Bin call for every file (FO_DELETE + FOF_ALLOWUNDO, the plugin's recycle flags). */
bool RpRecycleFiles(const std::vector<std::string>& files)
{
    if (files.empty()) return true;
    std::vector<char> from;
    for (size_t i = 0; i < files.size(); ++i) { from.insert(from.end(), files[i].begin(), files[i].end()); from.push_back(0); }
    from.push_back(0);
    SHFILEOPSTRUCTA op; memset(&op, 0, sizeof(op));
    op.wFunc = FO_DELETE; op.pFrom = &from[0];
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_WANTNUKEWARNING | FOF_SILENT | FOF_NOERRORUI;
    return U8SHFileOperation(&op) == 0 && !op.fAnyOperationsAborted;
}
}   // namespace

void OwnStoreRepairApply(const std::vector<restoreguard::RepairRow>& rows, const std::string& world, OwnRepairResult* out)
{
    OwnRepairResult r;
    const std::string dir = OwnStoreDir();
    if (dir.empty()) { r.rc = -1; *out = r; return; }
    if (!OwnStoreIdleNow()) { r.rc = -2; *out = r; return; }
    unsigned int applied = 0;
    {
        std::vector<char> b;
        if (ReadAll(Join(dir, kOwnRepairFile), &b) && !b.empty()) restoreguard::OwnRepairSeenParse(std::string(b.begin(), b.end()), &applied);
    }
    unsigned int top = applied;
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].epoch > applied) { ++r.applied; if (rows[i].epoch > top) top = rows[i].epoch; }
    r.epoch = top;
    if (r.applied == 0) { r.rc = 0; *out = r; return; }
    std::string header; std::map<std::string, coopown::IndexLine> cur;
    RpReadIndex(dir, &header, &cur);
    /* the checkpoints of THIS world: <store>\checkpoint\<world>\<tag> */
    std::vector<restoreguard::CpView> cps; std::vector<std::string> cpDirs; std::vector<std::map<std::string, coopown::IndexLine> > cpLines;
    {
        const std::string root = Join(Join(dir, "checkpoint"), world);
        U8FindData fd; HANDLE h = U8FindFirstFile(Join(root, "*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
                restoreguard::CheckpointTag t;
                if (!restoreguard::ParseCheckpointTag(std::string(fd.cFileName), &t)) continue;
                std::map<std::string, coopown::IndexLine> ls;
                if (!RpReadIndex(Join(root, fd.cFileName), 0, &ls)) continue;
                restoreguard::CpView v; v.tagSeq = t.seq;
                for (std::map<std::string, coopown::IndexLine>::const_iterator it = ls.begin(); it != ls.end(); ++it)
                {
                    restoreguard::OwnStampView sv;
                    sv.epoch = it->second.stamped ? it->second.epoch : 0u;
                    sv.nbSeq = (it->second.stamped && it->second.nbSeq < t.seq) ? it->second.nbSeq : t.seq;
                    v.lines[it->first] = sv;
                }
                cps.push_back(v); cpDirs.push_back(Join(root, fd.cFileName)); cpLines.push_back(ls);
            } while (U8FindNextFile(h, &fd));
            ::FindClose(h);
        }
    }
    std::map<std::string, restoreguard::OwnStampView> stamps;
    unsigned long long maxSeq = 0ULL;
    for (std::map<std::string, coopown::IndexLine>::const_iterator it = cur.begin(); it != cur.end(); ++it)
    {
        restoreguard::OwnStampView sv;
        if (it->second.stamped) { sv.epoch = it->second.epoch; sv.nbSeq = it->second.nbSeq; }   /* an o1 line predates the numbers: it survives */
        stamps[it->first] = sv;
        if (it->second.seq > maxSeq) maxSeq = it->second.seq;
    }
    const std::map<std::string, restoreguard::OwnRepairStep> plan = restoreguard::OwnRepairPlan(rows, stamps, cps);
    std::vector<std::string> bin; int changed = 0;
    for (std::map<std::string, restoreguard::OwnRepairStep>::const_iterator it = plan.begin(); it != plan.end(); ++it)
    {
        if (it->second.action == restoreguard::kOrKeep) continue;
        ++changed;
        const std::string live = Join(dir, coopown::RecFile(it->first)), prev = Join(dir, coopown::PrevFile(it->first));
        if (PathThere(live)) bin.push_back(live);
        if (PathThere(prev)) bin.push_back(prev);
    }
    const std::string ledger = Join(dir, coopown::LedgerFile());
    const int ledgerCp = changed > 0 ? restoreguard::LedgerBaseCheckpoint(rows, applied, cps) : -1;
    if (changed > 0 && PathThere(ledger)) { bin.push_back(ledger); r.ledgerRecycled = 1; }
    if (!bin.empty())
    {
        if (!CheckpointBinCanTake(dir)) { r.rc = -3; *out = r; return; }   /* nothing touched: never a hard delete */
        if (!RpRecycleFiles(bin)) { r.rc = -4; *out = r; return; }
    }
    std::string all = header;
    for (std::map<std::string, restoreguard::OwnRepairStep>::const_iterator it = plan.begin(); it != plan.end(); ++it)
    {
        const std::map<std::string, coopown::IndexLine>::const_iterator c = cur.find(it->first);
        if (c == cur.end()) continue;
        coopown::IndexLine l = c->second;
        if (it->second.action == restoreguard::kOrDrop) { ++r.dropped; continue; }
        if (it->second.action == restoreguard::kOrFromCheckpoint)
        {
            const std::map<std::string, coopown::IndexLine>& cl = cpLines[(size_t)it->second.cp];
            const std::map<std::string, coopown::IndexLine>::const_iterator k = cl.find(it->first);
            const std::string src = Join(cpDirs[(size_t)it->second.cp], coopown::RecFile(it->first));
            const std::string srcPrev = Join(cpDirs[(size_t)it->second.cp], coopown::PrevFile(it->first));
            std::string from;
            if (k != cl.end() && RpFileMatches(src, k->second.len, k->second.crc)) from = src;
            else if (k != cl.end() && RpFileMatches(srcPrev, k->second.len, k->second.crc)) from = srcPrev;
            if (from.empty() || !U8CopyFile(from.c_str(), Join(dir, coopown::RecFile(it->first)).c_str(), FALSE)) { ++r.copyFailed; ++r.dropped; continue; }
            l = k->second; l.seq = ++maxSeq; l.stamped = true; l.epoch = it->second.stamp.epoch; l.nbSeq = it->second.stamp.nbSeq;
            ++r.fromCheckpoint;
        }
        std::string line;
        if (coopown::EncodeIndexLine(l, &line)) all += line;
    }
    r.recycled = changed;
    if (ledgerCp >= 0)
    {
        const std::string cl = Join(cpDirs[(size_t)ledgerCp], coopown::LedgerFile());
        if (PathThere(cl) && U8CopyFile(cl.c_str(), ledger.c_str(), FALSE)) r.ledgerFromCheckpoint = 1;
    }
    char pid[24]; _snprintf(pid, 23, "%lu", (unsigned long)::GetCurrentProcessId()); pid[23] = 0;
    if (changed > 0)
    {
        const std::string tmp = Join(dir, std::string(coopown::IndexFile()) + "." + pid + ".tmp");
        if (!WriteAllFlushed(tmp, all.data(), all.size()) || !MoveReplace(tmp, Join(dir, coopown::IndexFile()))) { U8DeleteFile(tmp.c_str()); r.rc = -4; *out = r; return; }
    }
    {
        const std::string seen = restoreguard::OwnRepairSeenFormat(top);
        const std::string tmp = Join(dir, std::string(kOwnRepairFile) + "." + pid + ".tmp");
        if (!WriteAllFlushed(tmp, seen.data(), seen.size()) || !MoveReplace(tmp, Join(dir, kOwnRepairFile))) { U8DeleteFile(tmp.c_str()); r.rc = -4; *out = r; return; }
    }
    unsigned long long ms = 0ULL; int bad = 0;
    OwnStoreOpen(dir, &ms, &bad);   /* the index, the committed maps and the counter, as the folder now says */
    r.maxSeq = ms > maxSeq ? ms : maxSeq;
    r.rc = 1; *out = r;
}

}   // namespace coop
