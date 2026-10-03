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
 * PURE: shared with the offline suite. C++03 (VS2010 v100).
 */
#ifndef COOP_LOGROTATE_H
#define COOP_LOGROTATE_H

#include <string>

namespace cooplogrot {

/* how many launches' logs are kept, the current one included */
enum { kLogKeep = 3 };

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
