#pragma once
// T-1476 (owner 1874, 2026-11-22): THE [RKCHECK] LINE - one log line at the mod's start saying whether RE_Kenshi.dll and
// KenshiLib.dll are loaded in the game at that moment, whether --norekenshi was given, and which game file is running (its
// file NAME only - never the full path, which carries the player's folder names) and in which kind of folder.
// Why: a player reports RE_Kenshi does nothing while this mod is on; their log showed RE_Kenshi.dll loaded at world load but
// not whether it was loaded when this mod started. This line answers that from the next report.
// Pure (only <string>): coop.cpp and the offline suite compile this one header. C++03. Reads nothing, writes nothing.
#include <string>

namespace rkcheck {

/* the folder words */
inline const char* kFolderUnknown()   { return "unknown"; }
inline const char* kFolderReKenshi()  { return "RE_Kenshi's own folder"; }
inline const char* kFolderKenshi()    { return "the Kenshi folder"; }

/* the part of `path` after its last '/' or '\'; the whole of it when it has neither; "" for an empty path or one ending in a slash */
inline std::string FileName(const std::string& path)
{
    const size_t s = path.find_last_of("/\\");
    return (s == std::string::npos) ? path : path.substr(s + 1);
}

/* the name of the folder that holds the file named by `path` ("C:/Games/Kenshi/kenshi_x64.exe" -> "Kenshi"); "" when the path has
   no folder part */
inline std::string ParentFolderName(const std::string& path)
{
    const size_t s = path.find_last_of("/\\");
    if (s == std::string::npos || s == 0) return std::string();
    const std::string dir = path.substr(0, s);
    const size_t p = dir.find_last_of("/\\");
    return (p == std::string::npos) ? dir : dir.substr(p + 1);
}

/* ASCII-only case-insensitive equality (a folder name is compared with the plain word "RE_Kenshi") */
inline bool SameNoCase(const std::string& a, const char* b)
{
    size_t i = 0;
    for (; i < a.size(); ++i)
    {
        if (b[i] == 0) return false;
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return b[i] == 0;
}

/* which kind of folder the running game file sits in: "unknown" when the path names no file; "RE_Kenshi's own folder" when the
   file's folder is named RE_Kenshi (any case); otherwise "the Kenshi folder" */
inline const char* FolderWord(const std::string& path)
{
    if (FileName(path).empty()) return kFolderUnknown();
    if (SameNoCase(ParentFolderName(path), "RE_Kenshi")) return kFolderReKenshi();
    return kFolderKenshi();
}

/* the line. `exePath` is the running game file's full path as UTF-8 ("" when it could not be read): only its file name and the
   folder word are printed. */
inline std::string Line(const char* road, int rkLoaded, int klLoaded, int noRk, const std::string& exePath)
{
    const std::string name = FileName(exePath);
    std::string s = std::string("[RKCHECK] at the mod's start (") + (road ? road : "?") + "): RE_Kenshi.dll loaded="
        + (rkLoaded ? "1" : "0") + " KenshiLib.dll loaded=" + (klLoaded ? "1" : "0") + " --norekenshi=" + (noRk ? "1" : "0");
    if (name.empty()) s += " game file=unknown";
    else s += " game file=" + name + " in " + FolderWord(exePath);
    s += " - RE_Kenshi's state when this mod started (T-1476)";
    return s;
}

}   /* namespace rkcheck */
