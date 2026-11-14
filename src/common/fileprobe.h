/* src/common/fileprobe.h - P8d / T-1280: THE ONE PLACE A PROGRAM ASKS WHAT A FILE IS.
 *
 * THREE ANSWERS AND NO OTHERS (storemeta.h names them, because it is the DECISION that has to be able to refuse). P8b asked
 * "did the read succeed?", which answers the same false for "there is no such file" and for "the file is there and I could
 * not read it", and then wrote that answer back as an index line meaning "this record has no squad" - permanently, over an
 * intact one (review-p8b C-1). P8c split the two for ONE file on ONE path; the v5 validation path, three of the upgrade inputs,
 * options.txt and the deleted-number bitmaps all kept the old test (review-p8c C-2, H-1, H-3). P8d made every reader of the
 * world server's notebook come through here. T-1280 moved the check out of store_main.cpp so the game compiles the SAME one
 * (the sibling search of 2026-11-07 found twelve more reads that fed a write, in both programs), and the rule this makes
 * structural is:
 *   A kFileUnreadable ANSWER NEVER REACHES A WRITE - the caller skips the write, keeps what is in memory, counts it, names the
 *   file and the Win32 error once, and tries again at a point it already has.
 * COST, stated rather than left to be discovered: a probe READS the file. That is deliberate - a test that only asks the
 * folder is precisely the test that cannot tell "I know what this holds" from "the name is listed", and the notebook has paid
 * for that twice.
 *
 * C++03. The decision half (FileLockedStep) is pure; ProbeFile and the counter use Windows (fileprobe.cpp), and the file
 * calls go through ../coop-plugin/u8file.cpp, which every program that compiles fileprobe.cpp compiles too.
 */
#ifndef SW_FILEPROBE_H
#define SW_FILEPROBE_H

#include <set>
#include <string>
#include <vector>
#include "storemeta.h"   /* FileState: kFileAbsent / kFileUnreadable / kFileReadable */

namespace coopstore {

struct FileProbe
{
    int               kind;    /* coopstore::kFileAbsent | kFileUnreadable | kFileReadable */
    unsigned long     err;     /* the Win32 error behind kFileUnreadable. 0 means THE REFUSAL WAS OURS (the
                                  size cap) - a different thing to know, and one that would be invisible if
                                  it shared a code with a real error. */
    long long         attrLen; /* the size the FOLDER reports, available whenever the attribute query worked,
                                  so an unreadable file can still have its size named in a log line.
                                  review-p8c M-5: `EXISTS (-1 bytes)` told a reader nothing. -1 = unknown. */
    long long         len;     /* bytes in hand; meaningful only when kFileReadable */
    unsigned int      crc;     /* CRC-32 of those bytes; meaningful only when kFileReadable */
    std::vector<char> bytes;
    int               ownOpenFailed;   /* T-1280 fold 1 (L1): 1 = the caller's own open failed and only this second look read it (ProbeAfterOwnOpen) */
    FileProbe() : kind(kFileAbsent), err(0), attrLen(-1), len(0), crc(0), ownOpenFailed(0) {}
};
const long long kProbeMaxBytes = 64LL << 20;
/* ONLY ERROR_FILE_NOT_FOUND / ERROR_PATH_NOT_FOUND from the attribute query are kFileAbsent; a directory of that name, a
   sharing violation, a permission, a short read or a file above kProbeMaxBytes is kFileUnreadable; a file of zero bytes is
   kFileReadable with len 0. */
FileProbe ProbeFile(const std::string& path);
/* the file's own name - the part after the last '\' or '/' - for a log line: a full path carries the user's folder name */
std::string ProbeLeafName(const std::string& path);
/* T-1280 fold 1 (L1): a site that opened the file ITSELF first and probes only when that open failed. A probe that then reads the file
   (it was let go in between) leaves the site with NOTHING read - its own stream failed - so the answer it acts on is UNREADABLE (the
   write waits for the next look), never "readable" with nothing in hand. Absent stays absent. Pure. */
inline void ProbeAfterOwnOpen(FileProbe* p)
{
    if (p->kind != kFileReadable) return;
    p->kind = kFileUnreadable; p->err = 0; p->ownOpenFailed = 1; p->bytes.clear(); p->len = 0; p->crc = 0;
}
/* T-1280 fold 1 (M1): the files whose LAST READ was unreadable. While a path is listed, nothing deletes it, empties it or writes over
   it - the rows it holds stay on disk until a read of it that works (readable or absent) takes it off the list. Pure. */
inline void HeldPathNote(std::set<std::string>* held, const std::string& path, int kind)
{
    if (path.empty()) return;
    if (kind == kFileUnreadable) held->insert(path); else held->erase(path);
}
inline bool HeldPathKeeps(const std::set<std::string>& held, const std::string& path) { return !path.empty() && held.find(path) != held.end(); }
/* T-1280 fold 1c (N3): the same note for a load that may not USE the file it probed. A held file is let go only when it is gone, or
   when this load read its rows (`used`); one that reads but was not used stays held - its rows were never resolved. An unreadable one
   is held. Pure. */
inline void HeldPathNoteUse(std::set<std::string>* held, const std::string& path, int kind, bool used)
{
    if (path.empty()) return;
    if (kind == kFileAbsent || (kind == kFileReadable && used)) held->erase(path);
    else if (kind == kFileUnreadable) held->insert(path);
}
/* T-1280 fold 1c (N1): where rows go whose own file (`first`) is held: kHeldWriteFirst it is not; kHeldWriteSecond the other file
   (`second`, "" = none) is not held, so it takes them; kHeldWriteMemory both are - kept in memory until one is let go. Pure. */
const int kHeldWriteFirst = 0, kHeldWriteSecond = 1, kHeldWriteMemory = 2;
inline int HeldWriteWhere(const std::set<std::string>& held, const std::string& first, const std::string& second)
{
    if (!HeldPathKeeps(held, first)) return kHeldWriteFirst;
    if (!second.empty() && !HeldPathKeeps(held, second)) return kHeldWriteSecond;
    return kHeldWriteMemory;
}

/* T-1280 - ONE SITE'S STEP ON A FILE ANOTHER PROGRAM HOLDS. `held` is the site's own flag (0 at start), so a site retrying
   every second says the file ONCE and counts the run once, and the attempt that finds the file readable again is
   counted as a retry that went through. Pure. */
const int kLockGo        = 0;   /* readable or absent, and nothing was held: the site goes on as it always did */
const int kLockSkipFirst = 1;   /* unreadable, first of a run: skip the write, keep memory, SAY it (one line) */
const int kLockSkipAgain = 2;   /* unreadable again: skip the write, keep memory, say nothing new */
const int kLockRecovered = 3;   /* readable or absent after a held run: the site goes on - its held write goes ahead now */
int FileLockedStep(int kind, int* held);
inline bool FileLockSkips(int step) { return step == kLockSkipFirst || step == kLockSkipAgain; }

/* FileLockedStep and the program's counter: seen (every held RUN, counted at its first skip - a site retrying every second is not
   counted again each second; T-1280 fold 1, L8) and retried (every held run that ended with the write going ahead). Any thread. */
int FileLockedNote(int kind, int* held);
/* " fileLocked[seen,retried]=<seen>,<retried>" - the REPORT token, the same form in both programs */
std::string FileLockedToken();
/* the one line a site says at kLockSkipFirst: "<what>: <file name> is ON DISK (<n> bytes) and could NOT BE READ (Windows
   error <e>) - <keep>." plus the token. `what` names the site, `keep` says what is kept and when it is tried again. */
std::string FileLockedLine(const char* what, const std::string& path, const FileProbe& p, const char* keep);

}   /* namespace coopstore */

#endif
