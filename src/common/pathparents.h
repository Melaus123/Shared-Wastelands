/* src/common/pathparents.h - task 4 (0.4.0 contributor brief): EVERY PARENT FOLDER OF A FILE PATH, TOP FIRST.
 *
 * A joining game writes per-world files (build_handoffs_owed_<key>.txt) into a world folder that only the host path creates, so on a
 * joining PC its first write could find no folder and fail (Inferred from the code). The writer now makes each folder above the file first, top first, the way the host path
 * makes its two levels. Pure: the game and the offline suite compile it; the folder creation itself stays in the game (U8CreateDirectory).
 *
 * "C:\a\b\f.txt" -> "C:\a", "C:\a\b". A drive root ("C:\"), a UNC share root ("\\server\share") and the file itself are never
 * listed; both separators are accepted and kept as written; an empty path or a bare name gives none. */
#pragma once
#include <string>
#include <vector>

namespace cooppath
{
inline bool PathSep(char c) { return c == '\\' || c == '/'; }
inline std::vector<std::string> ParentDirsTopFirst(const std::string& file)
{
    std::vector<std::string> out;
    size_t start = 0;   /* the first character after the root: no folder ends inside the root */
    if (file.size() >= 2 && PathSep(file[0]) && PathSep(file[1]))
    {   /* \\server\share\... : skip the server and the share */
        size_t n = 0, i = 2;
        for (; i < file.size(); ++i) if (PathSep(file[i]) && ++n == 2) break;
        if (i >= file.size()) return out;
        start = i + 1;
    }
    else if (file.size() >= 2 && file[1] == ':') start = (file.size() >= 3 && PathSep(file[2])) ? 3 : 2;
    else if (!file.empty() && PathSep(file[0])) start = 1;
    for (size_t i = start; i < file.size(); ++i)
    {
        if (!PathSep(file[i]) || i == start) continue;
        if (PathSep(file[i - 1])) continue;   /* a doubled separator names no new folder */
        out.push_back(file.substr(0, i));
    }
    return out;
}
}
