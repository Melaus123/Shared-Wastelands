/* T-1028 (log only, 2026-11-16): WHY A STORE WRITE FAILED, in words for the log.

   Every store file this plugin writes goes through WriteFileBytes (store.cpp): a temp file is created, written, flushed when
   asked, then renamed over the old one. Until T-1028 it answered only yes or no, so a run with hundreds of "could not write"
   lines (a 0.3.3 joining game, 242 '[ZONE] RECV could not write' lines) carried no cause. WriteFileBytes now keeps, per thread,
   the step that failed and the Windows error number taken right after the failing call (before any clean-up call), and for the
   rename one note of whether the target file exists. This header turns that into the words appended to each such line, and
   decides which of a repeating line's copies are written. LOG ONLY: no answer here changes a write, a result or a decision. */
#pragma once
#include <string>
#include <cstdio>

namespace writefail {

const int kStepNone    = 0;   /* no failure recorded (the last write on this thread succeeded, or none ran) */
const int kStepRefused = 1;   /* refused before any file call: the path names a canonical record (the B10 tripwire) */
const int kStepCreate  = 2;   /* the temp file could not be created */
const int kStepWrite   = 3;   /* the bytes could not be written to the temp file (or fewer were written) */
const int kStepFlush   = 4;   /* the temp file could not be flushed to the disk */
const int kStepRename  = 5;   /* the temp file could not be renamed over the target */
const int kStepCount   = 6;

inline const char* StepName(int step)
{
    switch (step)
    {
    case kStepRefused: return "refused";
    case kStepCreate:  return "create temp";
    case kStepWrite:   return "write";
    case kStepFlush:   return "flush";
    case kStepRename:  return "rename";
    default:           return "none";
    }
}

/* the common Windows error numbers by name; 0 = none named */
inline const char* ErrName(unsigned long err)
{
    switch (err)
    {
    case 3:   return "path not found";
    case 5:   return "access denied";
    case 32:  return "sharing violation";
    case 33:  return "lock violation";
    case 112: return "disk full";
    case 206: return "name too long";
    default:  return 0;
    }
}

/* The words appended to a "could not write" line: " (step=rename err=32 sharing violation, target exists)".
   targetExists: 1 the target file exists, 0 it does not, -1 not checked (every step but rename). "" when no failure is recorded. */
inline std::string Why(int step, unsigned long err, int targetExists)
{
    if (step <= kStepNone || step >= kStepCount) return std::string();
    std::string s = std::string(" (step=") + StepName(step);
    if (step == kStepRefused) return s + " - the path names a canonical record)";
    char n[24]; std::sprintf(n, "%lu", err);
    s += std::string(" err=") + n;
    const char* name = ErrName(err);
    if (name != 0) s += std::string(" ") + name;
    if (step == kStepRename && targetExists >= 0) s += targetExists != 0 ? ", target exists" : ", no target";
    return s + ")";
}

/* A line that can repeat once per record: the first kLineCap are written whole; the next one says the rest are counted only;
   after that every kSummaryEvery-th one writes a summary. nth = this line's count since start, 1-based.
   1 = write the line, 2 = write the "further lines counted only" line, 3 = write a summary line, 0 = count only. */
const long long kLineCap      = 40;
const long long kSummaryEvery = 100;
inline int CapAction(long long nth)
{
    if (nth <= 0) return 0;
    if (nth <= kLineCap) return 1;
    if (nth == kLineCap + 1) return 2;
    return ((nth - kLineCap - 1) % kSummaryEvery) == 0 ? 3 : 0;
}

}   /* namespace writefail */
