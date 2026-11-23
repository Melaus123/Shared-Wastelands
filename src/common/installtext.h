/* src/common/installtext.h - THE SETUP PROGRAM'S TEXT DECISIONS, AND THEY ARE PURE (installer1, stage 6).
 *
 * SharedWastelandsSetup.exe (src/installer/) changes exactly one line in the game's Plugins_x64.cfg:
 * it adds `Plugin=SharedWastelandsLoader` once, right after the last existing `Plugin=` line, and on
 * uninstall it takes that line out again and nothing else (owner decisions 114/115/117).  The line
 * edit, the Steam library list parser and the Workshop-folder rule live HERE so the offline suite
 * (src/coop-test) can prove them without a Kenshi folder:
 *   * every other byte of the file is kept - line endings (CRLF, LF or a mix), comments, the
 *     `PluginFolder=.\` line, a `Plugin=RE_Kenshi` line, a missing final newline;
 *   * adding twice changes nothing the second time;
 *   * PcfgRemove(PcfgAdd(x)) == x for any x that did not already hold our line.
 *
 * PURE means: no Windows.h, no file I/O, no globals.  Text in, text out.  The CALLER reads and
 * writes the files.  C++03 (VS2010 v100): no auto, no nullptr, no range-for, no brace-init.
 */
#ifndef COOP_INSTALLTEXT_H
#define COOP_INSTALLTEXT_H
#include "names.h"   /* the on-disk names (mod folder, files, window texts) */

#include <string>
#include <vector>

namespace coopinst {

/* The value Ogre is given; it prepends PluginFolder and appends .dll. */
inline const char* LoaderPluginName() { return swnames::kLoaderPlugin; }

inline std::string InstTrim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

inline bool InstEqualNoCase(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

/* One line's content (no '\n'; a trailing '\r' is ignored by the trim).  Is it a `Plugin=` line?
   Ogre's ConfigFile trims around the key and the value, and "PluginFolder" is a different key. */
inline bool PcfgIsPluginLine(const std::string& line, std::string* value)
{
    const size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    if (InstTrim(line.substr(0, eq)) != "Plugin") return false;
    if (value) *value = InstTrim(line.substr(eq + 1));
    return true;
}

/* Is this line a `Plugin=` line naming `name`?  The value compares without case: Windows file names do not have one. */
inline bool PcfgIsNamedLine(const std::string& line, const char* name)
{
    std::string v;
    return PcfgIsPluginLine(line, &v) && InstEqualNoCase(v, name);
}

/* Is this line OUR line? */
inline bool PcfgIsOurLine(const std::string& line) { return PcfgIsNamedLine(line, LoaderPluginName()); }

/* The file split into lines: each entry is [begin, end) of the content and the offset just past its
   '\n' (== end when the last line has no newline). */
struct PcfgLine { size_t begin, end, next; };

inline std::vector<PcfgLine> PcfgSplit(const std::string& text)
{
    std::vector<PcfgLine> out;
    size_t at = 0;
    while (at < text.size())
    {
        const size_t nl = text.find('\n', at);
        PcfgLine l;
        l.begin = at;
        if (nl == std::string::npos) { l.end = text.size(); l.next = text.size(); }
        else { l.end = nl; l.next = nl + 1; }
        out.push_back(l);
        at = l.next;
    }
    return out;
}

/* The file's own line ending: the first one it uses; CRLF for a file with none (a Windows file). */
inline std::string PcfgEol(const std::string& text)
{
    const size_t nl = text.find('\n');
    if (nl == std::string::npos) return "\r\n";
    return (nl > 0 && text[nl - 1] == '\r') ? "\r\n" : "\n";
}

inline bool PcfgHasOurLine(const std::string& text)
{
    const std::vector<PcfgLine> ls = PcfgSplit(text);
    for (size_t i = 0; i < ls.size(); ++i)
        if (PcfgIsOurLine(text.substr(ls[i].begin, ls[i].end - ls[i].begin))) return true;
    return false;
}

/* ADD: our line right after the last `Plugin=` line (at the end when there is none), once. */
inline std::string PcfgAdd(const std::string& text, bool* changed)
{
    if (changed) *changed = false;
    if (PcfgHasOurLine(text)) return text;
    const std::string eol = PcfgEol(text);
    const std::string ours = std::string("Plugin=") + LoaderPluginName();
    const std::vector<PcfgLine> ls = PcfgSplit(text);
    size_t after = text.size();          /* insert position: the end unless a Plugin= line exists */
    bool lastHasNewline = text.empty() || text[text.size() - 1] == '\n';
    for (size_t i = 0; i < ls.size(); ++i)
    {
        if (PcfgIsPluginLine(text.substr(ls[i].begin, ls[i].end - ls[i].begin), 0))
        {
            after = ls[i].next;
            lastHasNewline = ls[i].next != ls[i].end;
        }
    }
    std::string out = text.substr(0, after);
    if (lastHasNewline) out += ours + eol;      /* a whole line of our own, the file's ending */
    else out += eol + ours;                       /* the file ended with no newline: keep it that way */
    out += text.substr(after);
    if (changed) *changed = true;
    return out;
}

/* REMOVE: every `Plugin=` line naming `name`, and nothing else.  When such a line is the file's last line
   with no newline, the newline before it goes too - that is the one PcfgAdd put there. */
inline std::string PcfgRemoveNamed(const std::string& text, const char* name, bool* changed)
{
    if (changed) *changed = false;
    const std::vector<PcfgLine> ls = PcfgSplit(text);
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < ls.size(); ++i)
    {
        const PcfgLine& l = ls[i];
        if (!PcfgIsNamedLine(text.substr(l.begin, l.end - l.begin), name))
        {
            out.append(text, l.begin, l.next - l.begin);
            continue;
        }
        if (changed) *changed = true;
        if (l.next == l.end)   /* last line, no newline after it */
        {
            if (out.size() >= 2 && out[out.size() - 2] == '\r' && out[out.size() - 1] == '\n') out.erase(out.size() - 2);
            else if (!out.empty() && out[out.size() - 1] == '\n') out.erase(out.size() - 1);
        }
    }
    return out;
}

/* REMOVE our line (Setup's uninstall). */
inline std::string PcfgRemove(const std::string& text, bool* changed) { return PcfgRemoveNamed(text, LoaderPluginName(), changed); }

/* REMOVE the loader's previous line, `Plugin=` + swnames::kOldLoaderPlugin (Setup's install): its DLL is gone, and a
   Plugins_x64.cfg line naming a DLL that is not there stops Kenshi starting. */
inline std::string PcfgRemoveOld(const std::string& text, bool* changed)
{
    return PcfgRemoveNamed(text, swnames::kOldLoaderPlugin, changed);
}

/* STEAM'S LIBRARY LIST (steamapps\libraryfolders.vdf).  Both shapes Steam has written:
     new:  "0" { "path"  "D:\\SteamLibrary" ... }
     old:  "1"  "D:\\SteamLibrary"
   Returns every quoted value whose key is "path" or a number and that looks like a drive or UNC path,
   with the vdf's doubled backslashes undone.  Order kept, no duplicates. */
inline std::vector<std::string> VdfLibraryPaths(const std::string& vdf)
{
    std::vector<std::string> toks;   /* quoted strings only, in order; a '{' or '}' becomes "{" / "}" markers */
    size_t i = 0;
    while (i < vdf.size())
    {
        const char c = vdf[i];
        if (c == '"')
        {
            std::string s;
            ++i;
            while (i < vdf.size() && vdf[i] != '"')
            {
                if (vdf[i] == '\\' && i + 1 < vdf.size()) { s += vdf[i + 1]; i += 2; continue; }
                s += vdf[i++];
            }
            ++i;
            toks.push_back(std::string("\"") + s);
        }
        else if (c == '{' || c == '}') { toks.push_back(std::string(1, c)); ++i; }
        else ++i;
    }
    std::vector<std::string> out;
    for (size_t k = 0; k + 1 < toks.size(); ++k)
    {
        if (toks[k][0] != '"' || toks[k + 1][0] != '"') continue;
        const std::string key = toks[k].substr(1), val = toks[k + 1].substr(1);
        bool numeric = !key.empty();
        for (size_t j = 0; j < key.size(); ++j) if (key[j] < '0' || key[j] > '9') numeric = false;
        if (!(InstEqualNoCase(key, "path") || numeric)) continue;
        const bool looksPath = (val.size() >= 3 && val[1] == ':' && (val[2] == '\\' || val[2] == '/'))
                            || (val.size() >= 2 && val[0] == '\\' && val[1] == '\\');
        if (!looksPath) continue;
        bool dup = false;
        for (size_t j = 0; j < out.size(); ++j) if (InstEqualNoCase(out[j], val)) dup = true;
        if (!dup) out.push_back(val);
        ++k;   /* the value is not a key */
    }
    return out;
}

/* THE GAME'S FILE NAMES: a Kenshi folder holds kenshi_x64.exe (Steam) or kenshi_GOG_x64.exe (GOG); either one
   marks the folder as Kenshi's, and a running process of either name is Kenshi running. */
inline int GameExeCount() { return 2; }
inline const char* GameExeName(int i) { return i == 0 ? "kenshi_x64.exe" : (i == 1 ? "kenshi_GOG_x64.exe" : ""); }
inline bool IsGameExeName(const std::string& name)
{
    for (int i = 0; i < GameExeCount(); ++i) if (InstEqualNoCase(name, GameExeName(i))) return true;
    return false;
}

/* THE WORKSHOP LAYOUT: a folder <X>\steamapps\workshop\content\233860\<id>[\...] names the game folder
   <X>\steamapps\common\Kenshi.  Returns "" when the folder is not inside that layout.  Either slash. */
inline std::string WorkshopGameFolder(const std::string& folder)
{
    std::string low = folder;
    for (size_t i = 0; i < low.size(); ++i)
    {
        if (low[i] == '/') low[i] = '\\';
        if (low[i] >= 'A' && low[i] <= 'Z') low[i] = (char)(low[i] - 'A' + 'a');
    }
    const std::string key = "\\steamapps\\workshop\\content\\233860\\";
    const size_t at = low.rfind(key);
    if (at == std::string::npos || at == 0) return "";
    if (low.size() <= at + key.size()) return "";   /* no <id> after it */
    std::string base = folder.substr(0, at);
    for (size_t i = 0; i < base.size(); ++i) if (base[i] == '/') base[i] = '\\';
    return base + "\\steamapps\\common\\Kenshi";
}

/* T-1364 (owner 1667 / 1668): DOES SETUP HAVE TO COPY THE MOD'S FOLDER INTO <Kenshi>\mods\?
   The loader (src/kenshi-loader/loader.cpp) looks for its marker only in <Kenshi>\mods\*\ and in
   <Kenshi>\..\..\workshop\content\233860\*\ - one folder level below each.  Setup run from any other folder
   must copy its own folder to <Kenshi>\mods\<swnames::kModName>, or nothing ever loads.
   The CALLER passes full paths (GetFullPathName, then GetLongPathName so a short 8.3 name compares equal to
   its long name); here they are compared without case, either slash, repeated and trailing slashes ignored. */

/* The comparison form of a full path: '\' only, ASCII lower case, no repeated '\' (a leading "\\" of a UNC
   path is kept), no trailing '\'. */
inline std::string InstPathKey(const std::string& path)
{
    std::string out;
    out.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i)
    {
        char c = path[i];
        if (c == '/') c = '\\';
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c == '\\' && !out.empty() && out[out.size() - 1] == '\\' && out.size() > 1) continue;
        out += c;
    }
    while (out.size() > 2 && out[out.size() - 1] == '\\') out.erase(out.size() - 1);
    if (out == "\\") out.clear();
    return out;
}

/* The folder one level up (keys only); a root stays itself, as Windows resolves "C:\.." to "C:\". */
inline std::string InstParentKey(const std::string& key)
{
    const size_t at = key.rfind('\\');
    if (at == std::string::npos || at < 2) return key;
    return key.substr(0, at);
}

/* `path` is `folder` itself or anywhere below it. */
inline bool InstInside(const std::string& path, const std::string& folder)
{
    const std::string p = InstPathKey(path), f = InstPathKey(folder);
    if (p.empty() || f.empty()) return false;
    if (p == f) return true;
    return p.size() > f.size() && p.compare(0, f.size(), f) == 0 && p[f.size()] == '\\';
}

/* `dir` is exactly one level below `parent` (a folder the loader's one-level search sees). */
inline bool InstDirectChild(const std::string& dir, const std::string& parent)
{
    const std::string d = InstPathKey(dir), p = InstPathKey(parent);
    if (d.empty() || p.empty() || d.size() <= p.size() + 1) return false;
    if (d.compare(0, p.size(), p) != 0 || d[p.size()] != '\\') return false;
    return d.find('\\', p.size() + 1) == std::string::npos;
}

/* The Workshop folder the loader of <kenshi> searches: <kenshi>\..\..\workshop\content\233860 (keys). */
inline std::string LoaderWorkshopKey(const std::string& kenshi)
{
    const std::string k = InstPathKey(kenshi);
    if (k.empty()) return "";
    return InstParentKey(InstParentKey(k)) + "\\workshop\\content\\233860";
}

/* COPY NEEDED: Setup's own folder is neither a direct subfolder of <kenshi>\mods\ nor a direct subfolder of the
   Workshop folder the loader of THAT Kenshi searches (a Workshop copy in another Steam library is not seen). */
inline bool CopyNeeded(const std::string& selfDir, const std::string& kenshi)
{
    const std::string k = InstPathKey(kenshi);
    if (k.empty() || InstPathKey(selfDir).empty()) return true;
    if (InstDirectChild(selfDir, k + "\\mods")) return false;
    if (InstDirectChild(selfDir, LoaderWorkshopKey(k))) return false;
    return true;
}

/* THE NOTE (owner 1668 "Uninstall moves the copy Setup made"): a one-line file Setup writes into a mods folder copy it
   makes NEW.  An existing folder Setup only overwrote gets none, so a folder the player placed by hand never carries it. */
inline const char* SetupNoteName() { return "made-by-setup.txt"; }
inline const char* SetupNoteText() { return "made by " SW_NAME " Setup\r\n"; }

/* UNINSTALL recycles <Kenshi>\mods\<mod folder> only when it is there, holds the note, and Setup is not running from
   inside it (it cannot recycle its own folder). */
inline bool UninstallRecyclesCopy(bool folderExists, bool notePresent, bool setupInside)
{
    return folderExists && notePresent && !setupInside;
}

/* INSTALLED (decision 1668): the game, the loader DLL, our Plugins_x64.cfg line AND a usable marker where the
   loader looks.  Loader + line with no mod folder found is NOT installed: Setup shows the Install screen. */
inline bool SetupInstalled(bool hasGame, bool hasLoader, bool hasLine, bool markerFound)
{
    return hasGame && hasLoader && hasLine && markerFound;
}

}   /* namespace coopinst */

#endif
