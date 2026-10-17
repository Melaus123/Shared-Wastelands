/* logrotate.h - THE NAMES OF THE MOD LOG'S KEPT COPIES, one per earlier game launch.
 *
 * At start-up, before this launch's log is opened, coop_log.cpp moves every kept copy one place older: copy kLogKeep-1 is
 * deleted, each copy n becomes copy n+1, and the log itself becomes copy 1. The last kLogKeep launches are then kept, newest
 * first - the log (this launch), copy 1 (the launch before), copy 2 (the one before that) - so the log of a launch that crashed
 * is still there after the game is started again.
 *
 * RotatedName(name, n) is copy n's path. n 0 (or less) is the name itself; n >= 1 puts ".<n>" before the file's last extension
 * ("shared_wastelands_log.txt" -> "shared_wastelands_log.1.txt"), or at the end when the file part has none. A dot in a
 * folder name, or a file part's leading dot, is not an extension. Works on std::string and std::wstring.
 *
 * THE PARTS OF ONE LAUNCH'S LOG (the 10 MB ceiling, coop_log.cpp). The first kOpeningBytes of a launch stay in the opening
 * part ".start" for the whole launch; after that the log is written in parts of at most kPartBytes: when the current part is
 * full it replaces the previous part ".prev" (the older middle is deleted) and a new current part begins. On disk per launch:
 * at most 2 + 4 + 4 = 10 MB. PartName(name, n, part) is launch n's part: kPartCurrent is RotatedName(name, n); kPartStart and
 * kPartPrev put ".start" / ".prev" after the launch's ".<n>" ("shared_wastelands_log.1.start.txt"). The parts of a launch
 * rotate with it at start-up.
 *
 * CeilingStep(partBytes, lineBytes, openingCut, slack): what to do before a line of lineBytes is written into a current part
 * that holds partBytes - kCeilKeep (write it), kCeilCutOpening (the opening part is full: rename it to ".start" and begin a new
 * current part) or kCeilRollPart (the current part is full: it becomes ".prev" and a new current part begins). `slack` is extra
 * room after a rename that failed, so a failed rename is not retried on every line. An empty part is never cut (a line larger
 * than a whole part is still written).
 *
 * PURE: shared with the offline suite. C++03 (VS2010 v100).
 */
#ifndef COOP_LOGROTATE_H
#define COOP_LOGROTATE_H

#include <string>

namespace cooplogrot {

/* how many launches' logs are kept, the current one included */
enum { kLogKeep = 3 };

enum { kPartCurrent = 0, kPartStart = 1, kPartPrev = 2, kPartKinds = 3 };
const unsigned long long kOpeningBytes = 2ull * 1024ull * 1024ull;
const unsigned long long kPartBytes    = 4ull * 1024ull * 1024ull;
enum { kCeilKeep = 0, kCeilCutOpening = 1, kCeilRollPart = 2 };

inline int CeilingStep(unsigned long long partBytes, unsigned long long lineBytes, bool openingCut, unsigned long long slack)
{
    if (partBytes == 0) return kCeilKeep;
    const unsigned long long room = (openingCut ? kPartBytes : kOpeningBytes) + slack;
    if (partBytes + lineBytes <= room) return kCeilKeep;
    return openingCut ? (int)kCeilRollPart : (int)kCeilCutOpening;
}

/* `name` with `tag` (ASCII) put before the file part's last extension, or at its end when it has none. */
template <class S>
S TagBeforeExtension(const S& name, const char* tag)
{
    typedef typename S::value_type C;
    typedef typename S::size_type  Z;
    Z fileStart = 0;
    for (Z i = 0; i < name.size(); ++i)
        if (name[i] == (C)'\\' || name[i] == (C)'/' || name[i] == (C)':') fileStart = i + 1;
    Z dot = S::npos;
    for (Z i = fileStart + 1; i < name.size(); ++i)   /* + 1: a leading dot is not an extension */
        if (name[i] == (C)'.') dot = i;
    S t;
    for (const char* a = tag; *a; ++a) t += (C)*a;
    if (dot == S::npos) return name + t;
    return name.substr(0, dot) + t + name.substr(dot);
}

template <class S>
S PartName(const S& name, int n, int part)
{
    char tag[32];
    int k = 0;
    if (n > 0)
    {
        char digits[12];
        int nd = 0;
        unsigned int v = (unsigned int)n;
        while (v > 0 && nd < 12) { digits[nd++] = (char)('0' + (int)(v % 10u)); v /= 10u; }
        tag[k++] = '.';
        while (nd > 0) tag[k++] = digits[--nd];
    }
    const char* kind = part == kPartStart ? ".start" : part == kPartPrev ? ".prev" : "";
    for (const char* a = kind; *a; ++a) tag[k++] = *a;
    tag[k] = 0;
    if (k == 0) return name;
    return TagBeforeExtension(name, tag);
}

template <class S>
S RotatedName(const S& name, int n)
{
    if (n <= 0) return name;
    typedef typename S::value_type C;
    typedef typename S::size_type  Z;
    Z fileStart = 0;
    for (Z i = 0; i < name.size(); ++i)
        if (name[i] == (C)'\\' || name[i] == (C)'/' || name[i] == (C)':') fileStart = i + 1;
    Z dot = S::npos;
    for (Z i = fileStart + 1; i < name.size(); ++i)   /* + 1: a leading dot is not an extension */
        if (name[i] == (C)'.') dot = i;
    C digits[12];
    int nd = 0;
    unsigned int v = (unsigned int)n;
    while (v > 0 && nd < 12) { digits[nd++] = (C)('0' + (int)(v % 10u)); v /= 10u; }
    S tag(1, (C)'.');
    while (nd > 0) tag += digits[--nd];
    if (dot == S::npos) return name + tag;
    return name.substr(0, dot) + tag + name.substr(dot);
}

}   /* namespace cooplogrot */
#endif
