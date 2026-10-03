/* src/common/dirlevels.h - tickwait: EVERY FOLDER LEVEL OF A PATH, SHORTEST FIRST.
 *
 * WHY (bench 2). store.cpp's EnsureDir made ONE folder with one _mkdir, so a mirror folder under a store folder
 * that did not exist yet (a fresh storedir=, a new world) was never made and every heartbeat and zone write into it
 * failed. EnsureDir now makes each missing level, in this order.
 *
 * PURE: no engine memory, no global, no OS call. The offline suite drives it (t_tickwait_dir_levels). C++03.
 *
 * THE ROOT IS NEVER A LEVEL: "C:\" / "C:" (drive), "\\server\share" (UNC, and "\\?\C:" the same way), "\" (the
 * current drive's root). A relative path's first level is its first name. Trailing separators are dropped; a
 * doubled separator adds no level. Either slash counts.
 */
#ifndef COOP_COMMON_DIRLEVELS_H
#define COOP_COMMON_DIRLEVELS_H

#include <string>
#include <vector>

namespace coopdir {

inline bool IsSep(char c) { return c == '\\' || c == '/'; }

/* how many leading characters are the root (never created) */
inline std::string::size_type RootLen(const std::string& p)
{
    const std::string::size_type n = p.size();
    if (n >= 2 && IsSep(p[0]) && IsSep(p[1]))
    {
        std::string::size_type i = 2, parts = 0;
        while (i < n && parts < 2)
        {
            while (i < n && !IsSep(p[i])) ++i;   /* the server, then the share */
            ++parts;
            if (parts < 2) while (i < n && IsSep(p[i])) ++i;
        }
        return i;
    }
    if (n >= 2 && p[1] == ':') return (n >= 3 && IsSep(p[2])) ? 3 : 2;
    if (n >= 1 && IsSep(p[0])) return 1;
    return 0;
}

/* out: each folder from the first below the root to the path itself */
inline void DirLevels(const std::string& path, std::vector<std::string>* out)
{
    out->clear();
    std::string::size_type end = path.size();
    const std::string::size_type root = RootLen(path);
    while (end > root && IsSep(path[end - 1])) --end;
    std::string::size_type i;
    for (i = root; i <= end; ++i)
    {
        if (i == end || IsSep(path[i]))
        {
            if (i > root && !IsSep(path[i - 1])) out->push_back(path.substr(0, i));
        }
    }
}

}   /* namespace coopdir */

#endif
