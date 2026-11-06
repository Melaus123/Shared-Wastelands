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

/* T-1020 (ported from pull request 4, task 4): THE FOLDER A FILE IS WRITTEN INTO, EVERY MISSING LEVEL MADE FIRST.
   A joining game writes per-world files (the owed hand-offs, the owed build help, the research take journal) into the
   world folder under the notebook's top folder, which only the hosting path makes, so on a joining PC the first write
   found no folder and failed every time. The writer now makes each level of the file's folder, shortest first, through
   the caller's own two calls - fs.Exists(level) and fs.Make(level) (true when made, or there already) - so the offline
   suite drives it with a pretend disk. Returns kDirReady when the folder is there (made now or before). Otherwise the
   save has failed: *failed is counted ONCE for it, *bad names the first level that could not be made, and the answer is
   kDirFailSay the first time for this *said (the caller logs it), kDirFailQuiet after (the counter keeps counting).
   T-1020 fold (review LOW 1): the DEEPEST level is asked first and a folder already there answers kDirReady at once - one
   disk check per save in the usual case, and a middle level that cannot be queried never refuses a folder that is there.
   Only a missing folder walks the levels, shortest first. fs.Exists answers false for a level that is a FILE, and fs.Make
   false for it too (it is no folder), so that level fails like any other. */
enum { kDirReady = 0, kDirFailSay = 1, kDirFailQuiet = 2 };

/* T-1020 re-check (review LOW 4): the plugin's real-disk Exists / Make (store.cpp StoreDiskDirs) decide from the disk's own answers
   here, so the offline suite checks the same rule against a real folder and a real file. attr = GetFileAttributes' answer
   (0xFFFFFFFF = INVALID_FILE_ATTRIBUTES; 0x10 = FILE_ATTRIBUTE_DIRECTORY). A FILE is no folder. */
inline bool AttrIsDir(unsigned long attr) { return attr != 0xFFFFFFFFUL && (attr & 0x10UL) != 0; }
/* Make: made now, or the call said "already exists" (ERROR_ALREADY_EXISTS) and what is there is a folder */
inline bool MadeIsDir(bool created, bool alreadyExists, unsigned long attrAfter) { return created || (alreadyExists && AttrIsDir(attrAfter)); }

inline std::string FolderOf(const std::string& file)
{
    const std::string::size_type cut = file.find_last_of("\\/");
    return cut == std::string::npos ? std::string() : file.substr(0, cut);
}

template <class Fs> int EnsureFileDir(const std::string& file, Fs& fs, long long* failed, int* said, std::string* bad)
{
    std::vector<std::string> lv;
    DirLevels(FolderOf(file), &lv);
    if (lv.empty() || fs.Exists(lv[lv.size() - 1])) return kDirReady;   /* T-1020 fold (LOW 1): the folder itself first */
    for (std::vector<std::string>::size_type i = 0; i < lv.size(); ++i)
    {
        if (fs.Exists(lv[i]) || fs.Make(lv[i])) continue;
        ++*failed;
        if (bad != 0) *bad = lv[i];
        if (*said != 0) return kDirFailQuiet;
        *said = 1;
        return kDirFailSay;
    }
    return kDirReady;
}

}   /* namespace coopdir */

#endif
