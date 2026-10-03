// settings1 S1 (docs/design-settings1.md sections 1, 4 and 5 "S1") - LOOK ONLY.
// Logs the world-settings blocks and the mod fingerprint, with no behaviour change:
//   [SETTINGS] advanced(save) <the 11 GameplayOptions fields, design 1a / keys of section 2>
//   [SETTINGS] gameplay <the 7 world-changing GameOptions fields, design 1b / keys of section 2>
//   [MODS] fingerprint=<64-bit hash> n=<N> plugins=<P> list=<first 20 active mods, load order>
// at the session link-up edge, at each world-live edge, and on the `settingsdump` verb.
// MAIN THREAD only (SettingsTick from the command-channel tick; the verb from the same dispatch).
// Every engine read is a fault-guarded plain copy; nothing is written to the engine and nothing is sent.
// Anything that cannot be read prints "unreadable" (a whole block) or -1 (one value) rather than a guess.
//
// settings2 S2 (docs/design-settings1.md sections 2, 3.1, 3.2; user decisions 2026-09-26) - THE WORLD'S ADVANCED OPTIONS.
//   The record: notebook OPTIONS keys gp.cod .. gp.dh (src/common/gpoptions.h). The notebook accepts them from the
//   authority only (its OnOptions refuses every other game and logs it) and REPLACES them on every accepted change.
//   Applied on EVERY game (the authority included): after GameplayOptions::load 0x3EEDE0 on the live global (the
//   save-list copy is skipped by the this-check), on every OPTIONS map that arrives, and on a non-authority at its
//   world-live edge. The authority RECORDS: keys the notebook does not hold, at its world-live edge and on an OPTIONS
//   arrival while its world is live (first record, recurrence-covered); and its OWN CHANGES - a live value that
//   differs from the record at its world-live edge (New Game commits the Advanced window into the global; a loaded
//   save has just had the record applied, so it cannot differ), and the `advset` lever. No per-frame poll: every
//   check runs on one of those events. gt.* (the gameplay tab) is S4 and is not touched here.
// settings4 S4 (design 1b, 3.4, 3.5; user decision: the gameplay-tab world settings follow the world while connected, the
//   host may change them later, each player's own come back on leaving): see the S4 block below - it supersedes the
//   last sentence above.
#include "settings.h"
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <utility>
#include "coop_log.h"
#include "hooks.h"     /* coop::AddHook (own MinHook) (settings2 S2) */
#include <mygui/MyGUI_Widget.h>    /* settings3 S3: the "Set by the host." note */
#include <mygui/MyGUI_TextBox.h>
#include "../common/gpoptions.h"   /* settings2 S2: the gp.* keys, offsets and ranges - the SAME table the notebook checks */
#include "../common/names.h"   /* the on-disk names (mod folder, files, window texts) */
#include "addresses.h"
#include "net/session.h"
#include "soak.h"    /* GameplayRunning */
#include "store.h"   /* EngineWritesBlocked */
#include "config.h"  /* settings5 S5: ConfigModFingerprintSalt */

namespace coop {
namespace {

uintptr_t Base() { return (uintptr_t)::GetModuleHandleA(0); }

/* design 1a: the one live GameplayOptions, 36 bytes (Read, investigation A1 + .br). */
unsigned long long kSetAdvancedRva = 0; static AddrReg kSetAdvancedRva_reg("GameplayOptionsGlobal", &kSetAdvancedRva);   /* Steam_1.0.65 0x2132528 */
/* design 1b: the GameOptions object (0xE8 bytes, no vtable; it ends exactly where GameplayOptions starts). */
unsigned long long kSetOptionsRva = 0; static AddrReg kSetOptionsRva_reg("OptionsHolderGlobal", &kSetOptionsRva);   /* Steam_1.0.65 0x2132440 */

/* design 4 (GameWorld offsets; +0x528 and +0x568 are header-only, so they are guarded and reported, never trusted) */
const size_t kGwActiveMods    = 0x528;   /* lektor<ModInfo*> activeMods, load order */
const size_t kGwAvailOrdered  = 0x568;   /* lektor<ModInfo*> availabelModsOrderedList (every available mod) */
const size_t kMiName = 0x00, kMiFile = 0x28, kMiPath = 0x50, kMiIsBase = 0x79, kMiVersion = 0xA8 + 0x50;
const int    kMaxMods = 1024;
const size_t kStrCap  = 520;

int SafeCopy(void* dst, const void* src, size_t n)
{
    __try { std::memcpy(dst, src, n); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* An engine std::string (same VS2010 toolset) read through its own inline accessors; no object is made here. */
int SafeStr(const void* at, char* out, size_t cap)
{
    out[0] = 0;
    __try
    {
        const std::string* s = (const std::string*)at;
        size_t n = s->size();
        if (n > 4096) return 0;
        if (n >= cap) n = cap - 1;
        std::memcpy(out, s->c_str(), n);
        out[n] = 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; return 0; }
}

/* lektor = {allocator 8, u32 count +0x8, u32 maxSize +0xC, T* +0x10} (combat.cpp / build.cpp). */
int SafePtrLektor(const void* at, void** out, int cap, int* total)
{
    *total = -1;
    __try
    {
        const unsigned char* q = (const unsigned char*)at;
        const unsigned int count = *(const unsigned int*)(q + 0x8);
        const unsigned int maxSize = *(const unsigned int*)(q + 0xC);
        void* const* stuff = *(void* const* const*)(q + 0x10);
        if (count > 100000u || count > maxSize) return 0;
        const int n = (int)count < cap ? (int)count : cap;
        if (n > 0) std::memcpy(out, stuff, (size_t)n * sizeof(void*));
        *total = (int)count;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

struct ModPod { char name[kStrCap]; char file[kStrCap]; char path[kStrCap]; int version; int isBase; int ok; };

void ReadMod(const void* mi, ModPod* m)
{
    m->ok = 0; m->version = -1; m->isBase = -1;
    m->name[0] = m->file[0] = m->path[0] = 0;
    if (mi == 0) return;
    const unsigned char* q = (const unsigned char*)mi;
    const int okName = SafeStr(q + kMiName, m->name, kStrCap);
    SafeStr(q + kMiFile, m->file, kStrCap);
    SafeStr(q + kMiPath, m->path, kStrCap);
    int v = -1; if (SafeCopy(&v, q + kMiVersion, 4)) m->version = v;
    unsigned char b = 0; if (SafeCopy(&b, q + kMiIsBase, 1)) m->isBase = (int)b;
    m->ok = okName;
}

std::string F(float f) { char b[48]; _snprintf_s(b, sizeof b, _TRUNCATE, "%.9g", (double)f); return b; }
std::string I(long long v) { char b[32]; _snprintf_s(b, sizeof b, _TRUNCATE, "%lld", v); return b; }
std::string H32(unsigned int v) { char b[16]; _snprintf_s(b, sizeof b, _TRUNCATE, "%08x", v); return b; }
float FAt(const unsigned char* b, size_t off) { float f; std::memcpy(&f, b + off, 4); return f; }
int IAt(const unsigned char* b, size_t off) { int i; std::memcpy(&i, b + off, 4); return i; }

std::string Lower(std::string s) { for (size_t i = 0; i < s.size(); ++i) if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] - 'A' + 'a'); return s; }
std::string BaseName(const std::string& p) { const size_t k = p.find_last_of("\\/"); return k == std::string::npos ? p : p.substr(k + 1); }
std::string DirName(const std::string& p) { const size_t k = p.find_last_of("\\/"); return k == std::string::npos ? std::string() : p.substr(0, k); }
std::string Join(const std::string& a, const std::string& b) { if (a.empty()) return b; const char c = a[a.size() - 1]; return (c == '\\' || c == '/') ? a + b : a + "\\" + b; }
int IsFile(const std::string& p) { if (p.empty()) return 0; const DWORD a = ::GetFileAttributesA(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0; }
int IsDir(const std::string& p) { if (p.empty()) return 0; const DWORD a = ::GetFileAttributesA(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0; }

unsigned int g_crcTab[256];
int g_crcInit = 0;
int FileCrc32(const std::string& path, unsigned int* crc)
{
    if (!g_crcInit)
    {
        for (unsigned int i = 0; i < 256; ++i) { unsigned int c = i; for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1); g_crcTab[i] = c; }
        g_crcInit = 1;
    }
    HANDLE h = ::CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, 0);
    if (h == INVALID_HANDLE_VALUE) return 0;
    /* review-settings1 (stall): NOT the whole file - the full read froze the main thread for seconds on a large mod list,
       in the middle of the link-up. The fingerprint covers the file SIZE plus its first and last 64 KB: identical on two
       PCs for identical files (unlike a modified time), and bounded at 128 KB per file. */
    std::vector<unsigned char> buf(1 << 16);
    unsigned int c = 0xFFFFFFFFu;
    DWORD got = 0;
    int ok = 1;
    LARGE_INTEGER sz; sz.QuadPart = 0;
    if (!::GetFileSizeEx(h, &sz)) ok = 0;
    for (int k = 0; ok && k < 8; ++k) { const unsigned char b = (unsigned char)(sz.QuadPart >> (8 * k)); c = g_crcTab[(c ^ b) & 0xFF] ^ (c >> 8); }
    for (int part = 0; ok && part < 2; ++part)
    {
        if (part == 1)
        {
            if (sz.QuadPart <= (LONGLONG)buf.size()) break;   /* the head already covered the whole file */
            LARGE_INTEGER at; at.QuadPart = sz.QuadPart - (LONGLONG)buf.size();
            if (!::SetFilePointerEx(h, at, 0, FILE_BEGIN)) { ok = 0; break; }
        }
        if (!::ReadFile(h, &buf[0], (DWORD)buf.size(), &got, 0)) { ok = 0; break; }
        for (DWORD i = 0; i < got; ++i) c = g_crcTab[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    }
    ::CloseHandle(h);
    *crc = c ^ 0xFFFFFFFFu;
    return ok;
}

int ReadText(const std::string& path, std::string* out)
{
    HANDLE h = ::CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return 0;
    char b[4096]; DWORD got = 0;
    out->clear();
    while (::ReadFile(h, b, sizeof b, &got, 0) && got > 0 && out->size() < (1u << 20)) out->append(b, got);
    ::CloseHandle(h);
    return 1;
}

/* The DLL names in one RE_Kenshi.json array ("Plugins" or "PreloadPlugins"); a plain string scan of that array. */
void JsonArray(const std::string& text, const char* key, std::vector<std::string>* out)
{
    const std::string k = std::string("\"") + key + "\"";
    size_t at = text.find(k);
    if (at == std::string::npos) return;
    const size_t open = text.find('[', at + k.size());
    if (open == std::string::npos) return;
    const size_t close = text.find(']', open);
    if (close == std::string::npos) return;
    size_t i = open + 1;
    while (i < close)
    {
        const size_t q1 = text.find('"', i);
        if (q1 == std::string::npos || q1 >= close) break;
        const size_t q2 = text.find('"', q1 + 1);
        if (q2 == std::string::npos || q2 > close) break;
        out->push_back(text.substr(q1 + 1, q2 - q1 - 1));
        i = q2 + 1;
    }
}

/* The .mod file of an active mod. ModInfo's file/path meaning was not traced, so the candidates are tried in turn
   (Inferred); the one that opened is logged in the detail row. */
std::string ModFileOf(const ModPod& m)
{
    const std::string file = m.file, path = m.path, name = m.name;
    if (IsFile(file)) return file;
    if (IsFile(Join(path, file))) return Join(path, file);
    if (IsFile(Join(path, name + ".mod"))) return Join(path, name + ".mod");
    if (IsFile(path)) return path;
    return std::string();
}
std::string ModFolderOf(const ModPod& m)
{
    const std::string path = m.path, file = m.file;
    if (IsDir(path)) return path;
    if (IsFile(path)) return DirName(path);
    if (IsFile(file)) return DirName(file);
    return std::string();
}

/* Our own mod's row in Kenshi's mod list is swnames::kModNameLower (names.h): its compatibility is the protocol number (design 4); its DLL: coopmods::kOwnModDll (T-318). */

/* ---- the fingerprint: computed once (the mod list cannot change without a restart, Inferred) ---- */
struct Fp
{
    int done;
    std::string hash;      /* 16 hex, or "unreadable" */
    int n;                 /* active mods counted (base and co-op excluded); -1 unreadable */
    int plugins;           /* DLL rows from available mods' RE_Kenshi.json; -1 unreadable */
    std::string list;      /* first 20 active names, load order */
    std::vector<std::string> detail;   /* logged once, in full */
    int known;                         /* settings5 S5: 1 = the active list was read (base mods are always in it) */
    std::vector<std::string> activeRows, pluginRows;   /* settings5 S5: the rows the hash is made of - the store HELLO carries them */
};
Fp g_fp;   /* static storage: done starts 0; the rest is set by ComputeFingerprint before any use */

void ComputeFingerprint()
{
    g_fp.detail.clear(); g_fp.list.clear(); g_fp.n = -1; g_fp.plugins = -1; g_fp.hash = "unreadable";
    g_fp.activeRows.clear(); g_fp.pluginRows.clear(); g_fp.known = 0;   /* settings5 S5 */
    const unsigned long long gwRva = Rva("GameWorldGlobal");
    if (gwRva == 0) { g_fp.detail.push_back("[MODS] detail: GameWorldGlobal not in the address table"); return; }
    const unsigned char* gw = (const unsigned char*)(Base() + gwRva);

    std::string canon = "A:";
    std::vector<void*> act(kMaxMods, (void*)0);
    int total = -1;
    if (!SafePtrLektor(gw + kGwActiveMods, &act[0], kMaxMods, &total))
    {
        g_fp.detail.push_back("[MODS] detail: GameWorld+0x528 activeMods unreadable");
        return;
    }
    int n = 0, skippedBase = 0, skippedCoop = 0, bad = 0;
    const int lim = total < kMaxMods ? total : kMaxMods;
    ModPod m;
    for (int i = 0; i < lim; ++i)
    {
        ReadMod(act[i], &m);
        if (!m.ok)
        {
            ++bad; canon += "?;"; g_fp.detail.push_back("[MODS] active i=" + I(i) + " unreadable");
            /* settings5 fold (review-settings5 4): a placeholder row, never skipped - its "?" checksum matches nothing (modlist.h) */
            g_fp.activeRows.push_back("(unreadable mod " + I(i) + ")|?|?");
            continue;
        }
        if (m.isBase == 1) { ++skippedBase; continue; }
        if (Lower(m.name) == swnames::kModNameLower) { ++skippedCoop; continue; }
        const std::string mf = ModFileOf(m);
        unsigned int crc = 0;
        const int crcOk = mf.empty() ? 0 : FileCrc32(mf, &crc);
        const std::string crcS = crcOk ? H32(crc) : std::string("-1");
        canon += std::string(m.name) + "|" + I(m.version) + "|" + crcS + ";";
        g_fp.activeRows.push_back(std::string(m.name) + "|" + I(m.version) + "|" + crcS);   /* settings5 S5 */
        if (n < 20) g_fp.list += (n ? "," : "") + std::string(m.name);
        g_fp.detail.push_back("[MODS] active i=" + I(n) + " name=" + m.name + " version=" + I(m.version) + " crc=" + crcS
                              + " file=" + (mf.empty() ? std::string("-") : mf));
        ++n;
    }
    if (n > 20) g_fp.list += ",+" + I(n - 20) + " more";
    g_fp.n = n;

    /* plugins: every available mod's RE_Kenshi.json, "Plugins" and "PreloadPlugins" (a PreloadPlugins DLL runs while its
       mod is merely available - design 4), sorted by DLL name. T-318: only rows that can load in THIS game count
       (coopmods::PluginRowDecide): none while RE_Kenshi is not loading plugins in this process, and never our own mod's -
       each left-out row is logged with its reason. */
    canon += "P:";
    std::vector<void*> av(kMaxMods, (void*)0);
    int avTotal = -1;
    if (!SafePtrLektor(gw + kGwAvailOrdered, &av[0], kMaxMods, &avTotal))
    {
        canon += "unreadable";
        g_fp.detail.push_back("[MODS] detail: GameWorld+0x568 available list unreadable - plugins=-1");
    }
    else
    {
        std::vector<std::string> rows;
        const int avLim = avTotal < kMaxMods ? avTotal : kMaxMods;
        int folders = 0;
        /* T-318: RE_Kenshi's presence is read from THIS process - its module loaded, and no --norekenshi argument */
        const int rkModule = (::GetModuleHandleW(L"RE_Kenshi.dll") != 0) ? 1 : 0;
        const char* cmdLine = ::GetCommandLineA();
        const int rkFlagOff = coopmods::CmdLineHasArg(cmdLine ? std::string(cmdLine) : std::string(), "--norekenshi") ? 1 : 0;
        const int rkLoads = coopmods::ReKenshiLoadsPlugins(rkModule, rkFlagOff);
        int leftOwn = 0, leftNoRk = 0;
        for (int i = 0; i < avLim; ++i)
        {
            ReadMod(av[i], &m);
            if (!m.ok) continue;
            const std::string folder = ModFolderOf(m);
            if (folder.empty()) continue;
            std::string json;
            if (!ReadText(Join(folder, "RE_Kenshi.json"), &json)) continue;
            ++folders;
            const char* keys[2] = { "PreloadPlugins", "Plugins" };
            for (int k = 0; k < 2; ++k)
            {
                std::vector<std::string> dlls;
                JsonArray(json, keys[k], &dlls);
                for (size_t d = 0; d < dlls.size(); ++d)
                {
                    const int verdict = coopmods::PluginRowDecide(rkLoads, std::string(m.name), dlls[d]);
                    if (verdict != coopmods::kPluginRowCounted)
                    {
                        if (verdict == coopmods::kPluginRowOwnMod) ++leftOwn; else ++leftNoRk;
                        g_fp.detail.push_back("[MODS] plugin left out " + Lower(BaseName(dlls[d])) + "|" + keys[k] + "|" + m.name
                                              + " - " + coopmods::PluginRowWhy(verdict));
                        continue;
                    }
                    unsigned int crc = 0;
                    const int ok = FileCrc32(Join(folder, dlls[d]), &crc);
                    rows.push_back(Lower(BaseName(dlls[d])) + "|" + (ok ? H32(crc) : std::string("-1")) + "|" + keys[k] + "|" + m.name);
                }
            }
        }
        std::sort(rows.begin(), rows.end());
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
        for (size_t r = 0; r < rows.size(); ++r)
        {
            const size_t p2 = rows[r].find('|', rows[r].find('|') + 1);
            canon += rows[r].substr(0, p2) + ";";   /* dll|crc only: which mod carries it is detail */
            g_fp.pluginRows.push_back(rows[r].substr(0, p2));   /* settings5 S5 */
            g_fp.detail.push_back("[MODS] plugin " + rows[r]);
        }
        g_fp.plugins = (int)rows.size();
        g_fp.detail.push_back("[MODS] detail: available=" + I(avTotal) + " withReKenshiJson=" + I(folders)
                              + " reKenshiModule=" + I(rkModule) + " noReKenshiFlag=" + I(rkFlagOff)
                              + " reKenshiLoadsPlugins=" + I(rkLoads) + " leftOutNoReKenshi=" + I(leftNoRk)
                              + " leftOutOwnMod=" + I(leftOwn));
        if (!rkLoads)
            g_fp.detail.push_back(std::string("[MODS] detail: RE_Kenshi is not loading plugins in this game (")
                                  + (rkModule ? "started with --norekenshi" : "RE_Kenshi.dll is not loaded in this process")
                                  + ") - RE_Kenshi.json rows name no plugin here and are left out of the list");
    }

    /* settings5 S5 - TEST ONLY: modfingerprint_salt=1 adds a made-up mod at the end of the active list, so this game's
       fingerprint and list differ from every other game's and a run can prove the join refusal. */
    if (ConfigModFingerprintSalt() != 0)
    {
        canon += std::string(coopmods::kSaltRow) + ";";
        g_fp.activeRows.push_back(coopmods::kSaltRow);
        g_fp.detail.push_back(std::string("[MODS] TEST: modfingerprint_salt=1 - the made-up mod '") + coopmods::kSaltRow
                              + "' is added to this game's list so the notebook refuses it at join");
    }
    g_fp.known = (total > 0) ? 1 : 0;   /* settings5 S5: the active list always holds the base mods once it is filled */
    unsigned long long h = 1469598103934665603ull;
    for (size_t i = 0; i < canon.size(); ++i) { h ^= (unsigned char)canon[i]; h *= 1099511628211ull; }
    char hb[24]; _snprintf_s(hb, sizeof hb, _TRUNCATE, "%016llx", h);
    g_fp.hash = hb;
    g_fp.detail.push_back("[MODS] detail: activeTotal=" + I(total) + " counted=" + I(n) + " skippedBase=" + I(skippedBase)
                          + " skippedCoop=" + I(skippedCoop) + " unreadable=" + I(bad) + " canonLen=" + I((long long)canon.size()));
    g_fp.done = (n > 0) ? 1 : 0;   /* review-settings1: an EMPTY list is 'not filled yet', never 'no mods' - tried again next time */
}

std::string AdvancedLine()
{
    std::string s = "[SETTINGS] advanced(save)";
    unsigned char b[0x24];
    if (kSetAdvancedRva == 0 || !SafeCopy(b, (const void*)(Base() + kSetAdvancedRva), sizeof b))
        return s + " unreadable";
    s += " cod=" + F(FAt(b, 0x0));
    s += " ep=" + I(b[0x4]);
    s += " gdm=" + F(FAt(b, 0x8));
    s += " bs=" + F(FAt(b, 0xC));
    s += " nnm=" + F(FAt(b, 0x10));
    s += " rs=" + F(FAt(b, 0x14));
    s += " ps=" + F(FAt(b, 0x18));
    s += " ht=" + F(FAt(b, 0x1C));
    s += " bl=" + I(b[0x20]);
    s += " ae=" + I(b[0x21]);
    s += " dh=" + I(b[0x22]);
    return s;
}

std::string GameplayLine()
{
    std::string s = "[SETTINGS] gameplay";
    unsigned char b[0x78];
    if (kSetOptionsRva == 0 || !SafeCopy(b, (const void*)(Base() + kSetOptionsRva), sizeof b))
        return s + " unreadable";
    s += " pop=" + F(FAt(b, 0x48));
    s += " squad=" + F(FAt(b, 0x4C));
    s += " raidsize=" + F(FAt(b, 0x50));
    s += " raidfreq=" + F(FAt(b, 0x54));
    s += " attacks=" + I(IAt(b, 0x58));
    s += " limbloss=" + I(IAt(b, 0x74));
    s += " civ=" + I(b[0x44]);
    return s;
}

std::string ModsLine()
{
    int first = 0;
    if (!g_fp.done) { ComputeFingerprint(); first = 1; }
    if (first) for (size_t i = 0; i < g_fp.detail.size(); ++i) DebugLog(g_fp.detail[i]);
    return "[MODS] fingerprint=" + g_fp.hash + " n=" + I(g_fp.n) + " plugins=" + I(g_fp.plugins)
           + " list=" + (g_fp.list.empty() ? std::string("-") : g_fp.list);
}

std::string RecordLine();   /* defined below, same (anonymous) namespace */
std::string GtLine();       /* settings4 S4: defined below, same (anonymous) namespace */
int HostChoicesToLive(const char* at);   /* mp5: defined below, same (anonymous) namespace */
int HostChoicesSnapshot(std::vector<std::pair<std::string, std::string> >* out);   /* mp5b: defined below, same (anonymous) namespace */
volatile LONG g_hcNotAuth = 0;           /* mp5: MAIN THREAD writes (SettingsTick), the load hook reads - 1 = a notebook link is up and calls another game the authority */
void LogAll(const char* at)
{
    const std::string tail = std::string(" at=") + at;
    DebugLog(AdvancedLine() + tail);
    DebugLog(GameplayLine() + tail);
    DebugLog(ModsLine() + tail);
    DebugLog(RecordLine() + tail);   /* settings2 S2 */
    DebugLog(GtLine() + tail);       /* settings4 S4 */
}

/* ============ settings2 S2 - the world record of the advanced options ============ */
unsigned long long kGpLoadRva = 0; static AddrReg kGpLoadRva_reg("GameplayOptionsLoad", &kGpLoadRva);   /* Steam_1.0.65 0x3EEDE0 GameplayOptions::load(this, GameData* camera) - called by loadGame 0x373DC0 on the global, by 0x47DE74 on a save-list COPY */
typedef unsigned long long (*GpLoadFn)(void* self, void* data);
GpLoadFn orig_gpLoad = 0;
int g_gpHook = 0;                                  /* 1 installed, -1 AddHook failed, -2 no table row */
int g_ngHook = 0, g_smHook = 0;                    /* settings3 S3: toggleAdvancedOptions post-hook / SaveManager::newGame pre-hook - 1 installed, -1 AddHook failed, -2 no table row */
long long g_ngLocks = 0, g_ngCommits = 0, g_ngUnlocks = 0;   /* settings3 S3: MAIN THREAD counters (unlocks: the fold of review-settings3 1e) */
CRITICAL_SECTION g_recCs;
volatile LONG g_recCsInit = 0;
std::map<std::string, std::string> g_rec;          /* g_recCs: the gp.* rows of the latest complete OPTIONS map */
int g_recKnown = 0;                                /* g_recCs: a map arrived on the current notebook link */
std::map<std::string, std::string> g_recPending;   /* MAIN THREAD: the map being read */
volatile LONGLONG g_gpLoads = 0, g_gpApplied = 0;
long long g_gpSent = 0;

struct RecLock
{
    RecLock() { if (g_recCsInit) ::EnterCriticalSection(&g_recCs); }
    ~RecLock() { if (g_recCsInit) ::LeaveCriticalSection(&g_recCs); }
};
int RecSnapshot(std::map<std::string, std::string>* out) { RecLock l; *out = g_rec; return g_recKnown; }

unsigned char* GpGlobal() { return kSetAdvancedRva != 0 ? (unsigned char*)(Base() + (uintptr_t)kSetAdvancedRva) : 0; }
int GpReadLive(unsigned char* b) { unsigned char* g = GpGlobal(); return (g != 0 && SafeCopy(b, g, 0x24)) ? 1 : 0; }
std::string GpName(int i) { return coopgp::GpFields()[i].key; }
std::string GpLiveText(const unsigned char* b, int i)
{
    const coopgp::GpField& f = coopgp::GpFields()[i];
    return f.kind == coopgp::kGpFloat ? F(FAt(b, (size_t)f.off)) : I(b[f.off]);
}
std::string GpCanon(int i, double v) { return coopgp::GpFields()[i].kind == coopgp::kGpFloat ? F((float)v) : std::string(v != 0.0 ? "1" : "0"); }
int GpEquals(const unsigned char* b, int i, double v)
{
    const coopgp::GpField& f = coopgp::GpFields()[i];
    if (f.kind == coopgp::kGpBool) return b[f.off] == (unsigned char)(v != 0.0 ? 1 : 0);
    const float want = (float)v;
    return std::memcmp(b + f.off, &want, 4) == 0;
}
/* A plain aligned store into the engine's own global, the same kind of write the engine's load and the Advanced
   window's sliders make; every field but nnm is read at the moment of use (design 1a). */
int GpWrite(int i, double v)
{
    unsigned char* g = GpGlobal();
    if (g == 0) return 0;
    const coopgp::GpField& f = coopgp::GpFields()[i];
    if (f.kind == coopgp::kGpBool) { const unsigned char c = (unsigned char)(v != 0.0 ? 1 : 0); return SafeCopy(g + f.off, &c, 1); }
    const float fl = (float)v;
    return SafeCopy(g + f.off, &fl, 4);
}

/* Write the record over the live global. `over` names what is being replaced, for the log line. */
int ApplyRecord(const char* over, int logAlways)
{
    std::map<std::string, std::string> rec;
    const int known = RecSnapshot(&rec);
    if (!known || rec.empty())
    {
        if (logAlways) DebugLog(std::string("[SETTINGS] no world record of the advanced options yet - ") + over + " values stay ("
                                + (known ? "the notebook holds no gp.* row" : "no notebook map on this link yet") + ")");
        return 0;
    }
    unsigned char b[0x24];
    if (!GpReadLive(b)) { ErrorLog("[SETTINGS] the advanced options global is unreadable - the world record was NOT applied"); return 0; }
    std::string what;
    int changed = 0, present = 0, bad = 0, failed = 0;
    for (int i = 0; i < coopgp::kGpCount; ++i)
    {
        const std::string key = "gp." + GpName(i);
        std::map<std::string, std::string>::const_iterator it = rec.find(key);
        if (it == rec.end()) continue;
        ++present;
        double v = 0.0;
        if (!coopgp::GpValueOk(key, it->second, &v)) { ++bad; ErrorLog("[SETTINGS] world record " + key + "='" + it->second + "' is out of range - NOT written"); continue; }
        if (GpEquals(b, i, v)) continue;
        const std::string was = GpLiveText(b, i);
        if (!GpWrite(i, v)) { ++failed; continue; }
        what += " " + GpName(i) + " " + was + "->" + GpCanon(i, v);
        ++changed;
    }
    ::InterlockedExchangeAdd64(&g_gpApplied, (LONGLONG)changed);
    if (changed > 0 || logAlways || bad > 0 || failed > 0)
        DebugLog(std::string("[SETTINGS] world advanced options applied over ") + over + ":" + (what.empty() ? std::string(" none differed") : what)
                 + " (" + I(changed) + " changed, " + I(present) + " recorded" + (bad ? ", " + I(bad) + " bad" : std::string())
                 + (failed ? ", " + I(failed) + " write faults" : std::string()) + ")");
    return changed;
}

/* THE AUTHORITY'S CHANGE PATH. Keys absent from the record are always sent (first record); a live value that
   differs from a recorded one is sent only when `ownChange` (the world-live edge, the advset lever) - on an
   OPTIONS arrival the record has just been applied, so a difference there is not this game's change. */
void AuthoritySync(const char* at, int ownChange)
{
    if (!StoreIsWorldAuthority()) return;
    std::map<std::string, std::string> rec;
    if (!RecSnapshot(&rec)) return;   /* no map on this link yet: its arrival runs this again */
    unsigned char b[0x24];
    if (!GpReadLive(b)) { ErrorLog("[SETTINGS] authority: the advanced options global is unreadable - nothing recorded"); return; }
    std::vector<std::pair<std::string, std::string> > send;
    std::string firstList, changeList;
    int same = 0, differs = 0, badLive = 0;
    for (int i = 0; i < coopgp::kGpCount; ++i)
    {
        const std::string key = "gp." + GpName(i);
        const std::string live = GpLiveText(b, i);
        double lv = 0.0;
        if (!coopgp::GpKindValueOk(coopgp::GpFields()[i].kind, live, &lv)) { ++badLive; ErrorLog("[SETTINGS] authority: live " + key + "=" + live + " is outside the recordable range - not sent"); continue; }
        std::map<std::string, std::string>::const_iterator it = rec.find(key);
        if (it == rec.end()) { send.push_back(std::make_pair(key, live)); firstList += " " + GpName(i) + "=" + live; continue; }
        double rv = 0.0;
        if (coopgp::GpValueOk(key, it->second, &rv) && GpEquals(b, i, rv)) { ++same; continue; }
        if (!ownChange) { ++differs; continue; }
        send.push_back(std::make_pair(key, live));
        changeList += " " + GpName(i) + " " + it->second + "->" + live;
    }
    if (send.empty())
    {
        DebugLog(std::string("[SETTINGS] gp.* already recorded - not re-sent (") + I(same) + " match the live values"
                 + (differs ? ", " + I(differs) + " differ from the record - the live values stay (the authority's are the truth) and go up at the next world-live edge or advset" : std::string())
                 + (badLive ? ", " + I(badLive) + " unrecordable" : std::string()) + ", at=" + at + ")");
        return;
    }
    const bool ok = StoreSendOptions(send);
    if (ok) g_gpSent += (long long)send.size();
    DebugLog(std::string("[SETTINGS] authority: ") + (ok ? "sent " : "could NOT send ") + I((long long)send.size())
             + " advanced option(s) to the world record at=" + at
             + (firstList.empty() ? std::string() : " first record:" + firstList)
             + (changeList.empty() ? std::string() : " own change:" + changeList)
             + (ok ? " - the notebook stores them and broadcasts them to every game" : " - NOT sent (no notebook link); the live values stay as they are, and they go up at the next world-live edge or advset (a key the notebook lacks also on the next OPTIONS arrival while live)"));
}

std::string RecordLine()
{
    std::map<std::string, std::string> rec;
    const int known = RecSnapshot(&rec);
    std::string s = "[SETTINGS] world record known=" + I(known) + " authority=" + I(StoreIsWorldAuthority() ? 1 : 0)
                    + " hook=" + I(g_gpHook) + " loads=" + I((long long)g_gpLoads) + " applied=" + I((long long)g_gpApplied) + " sent=" + I(g_gpSent)
                    + " s3lock=" + I(g_ngHook) + "/" + I(g_smHook) + " locks=" + I(g_ngLocks) + " commits=" + I(g_ngCommits) + " unlocks=" + I(g_ngUnlocks) + " gp:";   /* settings3 S3 */
    int n = 0;
    for (int i = 0; i < coopgp::kGpCount; ++i)
    {
        std::map<std::string, std::string>::const_iterator it = rec.find("gp." + GpName(i));
        if (it == rec.end()) continue;
        s += " " + GpName(i) + "=" + it->second; ++n;
    }
    return s + " (" + I(n) + " of " + I(coopgp::kGpCount) + " keys)";
}

/* 3.1: the post-hook. Only the live global (the save-list info loads a COPY). Runs inside loadGame, on the
   engine's own thread for that call, right after the engine's own write of the same fields. */
unsigned long long detour_gpLoad(void* self, void* data)
{
    const unsigned long long r = orig_gpLoad(self, data);
    if (kSetAdvancedRva != 0 && self == (void*)(Base() + (uintptr_t)kSetAdvancedRva))
    {
        ::InterlockedIncrement64(&g_gpLoads);
        ApplyRecord("the save's", 1);
        /* mp5: the host's Game options go over the record and the save's values, so the world-live edge finds nothing of
           the save's to record over what the host just chose (design-mpmenu1 section 5 risk). */
        if (::InterlockedCompareExchange(&g_hcNotAuth, 0, 0) == 0) HostChoicesToLive("load (over the save's values)");
    }
    return r;
}

/* ============ settings4 S4 - the gameplay tab follows the world while connected (design 1b, 3.4, 3.5) ============
   USER DECISION: the world-changing gameplay-tab settings (gt.*) follow the world while this game holds the world's
   record (the notebook link); the host - the world's authority - may change them later and the change applies to
   everyone; each player's own values come back when they leave. The authority never follows: its live values ARE the
   world's, and it records them like gp.* (absent keys at its world-live edge and on an OPTIONS arrival while live; its
   OWN CHANGE at the world-live edge, at every Options-window close, and on `gtset`). Every other game writes the
   record's values over its own and holds its own IN MEMORY ONLY (g_gtOwn); they are put back at the notebook link's
   down edge, at world leave, and when this game becomes the authority (review-settings4 D1: also on the authority
   edge itself, which can come without an OPTIONS map). Process exit needs nothing (below).
   settings.cfg WRITERS (review-settings4, corrected): OptionsWindow::saveOptions 0x3EC950 (sole caller
   OptionsWindow::hide 0x3EE6E0) is the ONLY writer of the IN-MEMORY gameplay values. Six other writers exist -
   SaveManager::execute ('continue'), updateAutoSave ('autosaveindex'), LocaleManager setCurrentLocale/init
   ('language'), Renderer::setupConfig, LevelEditor::saveMod - and each re-reads the file and changes one key, so the
   gameplay values in the file come from the file. The detour below puts the player's own values into the held fields
   for the saveOptions call and the world's back after it (even if the save throws), so the file never receives the
   world's values; a crash while connected leaves the file with the player's own. A game whose wrapper is not
   installed never follows.
   Population, squad and raid values change NEW spawns and schedules only - squads and nests already made keep theirs,
   and the attacks-on-base timer applies from its next roll (design 6). No special handling.
   MAIN THREAD throughout: SettingsTick, the OPTIONS handler, the verb, and the Options window's close (a GUI event). */
unsigned long long kOptSaveRva = 0; static AddrReg kOptSaveRva_reg("OptionsWindowSaveOptions", &kOptSaveRva);   /* Steam_1.0.65 0x3EC950 OptionsWindow::saveOptions(this): r12 = rcx, nothing else read from the caller (disassembly) */
typedef void (*OptSaveFn)(void* self);
OptSaveFn orig_optSave = 0;
int g_optHook = 0;   /* 1 installed, -1 AddHook failed, -2 no table row */

enum { kGtFloat = 0, kGtInt = 1, kGtBool = 2 };
struct GtField { const char* key; size_t off; int kind; };
const int kGtCount = 7;
const GtField kGt[kGtCount] = {   /* GameOptions offsets, design 1b (the same ones GameplayLine prints) */
    { "pop", 0x48, kGtFloat }, { "squad", 0x4C, kGtFloat }, { "raidsize", 0x50, kGtFloat }, { "raidfreq", 0x54, kGtFloat },
    { "attacks", 0x58, kGtInt }, { "limbloss", 0x74, kGtInt }, { "civ", 0x44, kGtBool } };
int g_gtHeld = 0;                   /* bit i: the world's value is in force over this player's own in field i */
unsigned int g_gtOwn[kGtCount];     /* this player's own value of each held field - IN MEMORY ONLY */
unsigned int g_gtWorld[kGtCount];   /* the world's value last written into each held field */
int g_gtLeft = 0;                   /* the world was left while the notebook link stayed up: no following until the next world-live edge or link */
long long g_gtSaves = 0, g_gtWrapped = 0, g_gtSent = 0;

unsigned char* GtHolder() { return kSetOptionsRva != 0 ? (unsigned char*)(Base() + (uintptr_t)kSetOptionsRva) : 0; }
int GtRead(int i, unsigned int* v)
{
    unsigned char* g = GtHolder();
    *v = 0;
    if (g == 0) return 0;
    if (kGt[i].kind == kGtBool) { unsigned char b = 0; if (!SafeCopy(&b, g + kGt[i].off, 1)) return 0; *v = b; return 1; }
    return SafeCopy(v, g + kGt[i].off, 4);
}
/* A plain aligned store, the same kind of write the Options window's sliders make. Population is read on a spawn
   worker thread; an aligned 4-byte store cannot tear (design 6, Inferred). */
int GtWrite(int i, unsigned int v)
{
    unsigned char* g = GtHolder();
    if (g == 0) return 0;
    if (kGt[i].kind == kGtBool) { const unsigned char b = (unsigned char)(v & 0xFF); return SafeCopy(g + kGt[i].off, &b, 1); }
    return SafeCopy(g + kGt[i].off, &v, 4);
}
std::string GtText(int i, unsigned int v)
{
    if (kGt[i].kind == kGtFloat) { float f; std::memcpy(&f, &v, 4); return F(f); }
    if (kGt[i].kind == kGtInt) return I((int)v);
    return I((long long)(v & 0xFF));
}
int GtIndex(const std::string& keyIn)   /* "pop" or "gt.pop" -> 0..6, else -1 */
{
    const std::string k = (keyIn.compare(0, 3, "gt.") == 0) ? keyIn.substr(3) : keyIn;
    for (int i = 0; i < kGtCount; ++i) if (k == kGt[i].key) return i;
    return -1;
}
/* The notebook's own range check (src/common/gpoptions.h GtValueOk), then the field's bits. */
int GtParse(int i, const std::string& text, unsigned int* v)
{
    if (!coopgp::GtValueOk(std::string("gt.") + kGt[i].key, text)) return 0;
    if (kGt[i].kind == kGtFloat) { const float f = (float)std::strtod(text.c_str(), 0); std::memcpy(v, &f, 4); return 1; }
    if (kGt[i].kind == kGtInt) { *v = (unsigned int)std::atoi(text.c_str()); return 1; }
    *v = (text == "1") ? 1u : 0u;
    return 1;
}
/* The record's legal gt.* rows as field bits; returns the mask of the fields present. */
int GtFromRecord(const std::map<std::string, std::string>& rec, unsigned int* w)
{
    int mask = 0;
    for (int i = 0; i < kGtCount; ++i)
    {
        std::map<std::string, std::string>::const_iterator it = rec.find(std::string("gt.") + kGt[i].key);
        if (it == rec.end()) continue;
        if (!GtParse(i, it->second, &w[i])) { ErrorLog("[SETTINGS] world record gt." + std::string(kGt[i].key) + "='" + it->second + "' is out of range - NOT written"); continue; }
        mask |= 1 << i;
    }
    return mask;
}
std::string GtHeldOwnList()
{
    std::string s;
    for (int i = 0; i < kGtCount; ++i) if (g_gtHeld & (1 << i)) s += std::string(" ") + kGt[i].key + "=" + GtText(i, g_gtOwn[i]);
    return s.empty() ? std::string(" none") : s;
}

/* Put this player's own values back into every held field (memory only; settings.cfg already holds them). */
void GtRestore(const std::string& why)
{
    if (g_gtHeld == 0) return;
    std::string what;
    int failed = 0;
    for (int i = 0; i < kGtCount; ++i)
    {
        if (!(g_gtHeld & (1 << i))) continue;
        if (!GtWrite(i, g_gtOwn[i])) { ++failed; continue; }   /* review-settings4 D3: stays HELD, so the next settings.cfg write still gets the player's own value */
        what += std::string(" ") + kGt[i].key + " " + GtText(i, g_gtOwn[i]);
        g_gtHeld &= ~(1 << i);
    }
    DebugLog("[SETTINGS] " + why + " - own gameplay settings restored (" + (what.empty() ? std::string("none") : what.substr(1)) + ")"
             + (failed ? " - " + I(failed) + " write fault(s): those fields stay held (the next settings.cfg write still gets the player's own value) and are retried at the next restore" : std::string()));
}

/* A non-authority writes the world's gt.* over its own, capturing each field's own value the moment it first becomes
   held (so a relink captures the values the link-down restored, never the world's). */
void GtFollow(const char* at)
{
    if (StoreIsWorldAuthority()) { GtRestore(std::string("this game is the world's authority (") + at + ")"); return; }
    if (g_gtLeft || GtHolder() == 0) return;
    std::map<std::string, std::string> rec;
    if (!RecSnapshot(&rec)) return;   /* no map on this notebook link: nothing to follow */
    unsigned int w[kGtCount];
    std::memset(w, 0, sizeof w);
    const int present = GtFromRecord(rec, w);
    if (present == 0 && g_gtHeld == 0) return;
    if (g_optHook != 1)
    {
        static int s_said = 0;
        if (!s_said) ErrorLog(std::string("[SETTINGS] the world's gameplay-tab settings are NOT followed on this game - the settings.cfg write wrapper is not installed (hook=") + I(g_optHook) + "), so following could put the world's values into this player's file");
        s_said = 1;
        return;
    }
    std::string what;
    int changed = 0, newly = 0, failed = 0, released = 0;
    for (int i = 0; i < kGtCount; ++i)
    {
        const int bit = 1 << i;
        if (!(present & bit))
        {
            if ((g_gtHeld & bit) && GtWrite(i, g_gtOwn[i])) { g_gtHeld &= ~bit; ++released; what += std::string(" ") + kGt[i].key + " back to own " + GtText(i, g_gtOwn[i]); }
            continue;
        }
        unsigned int live = 0;
        if (!GtRead(i, &live)) { ++failed; continue; }
        if (!(g_gtHeld & bit)) { g_gtOwn[i] = live; ++newly; }
        if (live != w[i])
        {
            if (!GtWrite(i, w[i])) { ++failed; continue; }
            what += std::string(" ") + kGt[i].key + " " + GtText(i, live) + "->" + GtText(i, w[i]);
            ++changed;
        }
        g_gtHeld |= bit;
        g_gtWorld[i] = w[i];
    }
    if (changed || newly || released || failed)
        DebugLog("[SETTINGS] gameplay tab follows the world:" + (what.empty() ? std::string(" none differed") : what)
                 + " (" + I(changed) + " changed, at=" + at + "; originals held in memory:" + GtHeldOwnList() + ")"
                 + (failed ? " - " + I(failed) + " field(s) unreadable/unwritable, left as they are" : std::string())
                 + " - population/squad/raid values apply to new spawns and schedules only");
}

/* THE AUTHORITY'S gt.* CHANGE PATH, the same shape as AuthoritySync: absent keys always go up (first record); a live
   value that differs from the record goes up only as an own change (world-live edge, Options close, gtset). */
void GtAuthoritySync(const char* at, int ownChange)
{
    if (!StoreIsWorldAuthority() || GtHolder() == 0) return;
    std::map<std::string, std::string> rec;
    if (!RecSnapshot(&rec)) return;   /* no map on this link yet: its arrival runs this again */
    std::vector<std::pair<std::string, std::string> > send;
    std::string firstList, changeList;
    int same = 0, differs = 0, bad = 0;
    for (int i = 0; i < kGtCount; ++i)
    {
        const std::string key = std::string("gt.") + kGt[i].key;
        unsigned int live = 0;
        if (!GtRead(i, &live)) { ++bad; continue; }
        const std::string lt = GtText(i, live);
        if (!coopgp::GtValueOk(key, lt)) { ++bad; ErrorLog("[SETTINGS] authority: live " + key + "=" + lt + " is outside the recordable range - not sent"); continue; }
        std::map<std::string, std::string>::const_iterator it = rec.find(key);
        if (it == rec.end()) { send.push_back(std::make_pair(key, lt)); firstList += std::string(" ") + kGt[i].key + "=" + lt; continue; }
        unsigned int rv = 0;
        if (GtParse(i, it->second, &rv) && rv == live) { ++same; continue; }
        if (!ownChange) { ++differs; continue; }
        send.push_back(std::make_pair(key, lt));
        changeList += std::string(" ") + kGt[i].key + " " + it->second + "->" + lt;
    }
    if (send.empty())
    {
        DebugLog(std::string("[SETTINGS] gt.* already recorded - not re-sent (") + I(same) + " match the live values"
                 + (differs ? ", " + I(differs) + " differ from the record - the live values stay (the authority's are the truth) and go up at the next own change" : std::string())
                 + (bad ? ", " + I(bad) + " unrecordable" : std::string()) + ", at=" + at + ")");
        return;
    }
    const bool ok = StoreSendOptions(send);
    if (ok) g_gtSent += (long long)send.size();
    DebugLog(std::string("[SETTINGS] authority: ") + (ok ? "sent " : "could NOT send ") + I((long long)send.size())
             + " gameplay-tab setting(s) to the world record at=" + at
             + (firstList.empty() ? std::string() : " first record:" + firstList)
             + (changeList.empty() ? std::string() : " own change:" + changeList)
             + (ok ? " - the notebook stores them and broadcasts them to every game" : " - NOT sent (no notebook link); they go up at the next own change or world-live edge"));
}

/* design 3.5: THE settings.cfg WRITE WRAPPER. While following: a held field that differs from the world's value is a
   change the player just made in the window, so it becomes the player's own; then the own values go in for the engine's
   write and the world's come back after it. On the authority (never following), its own change goes up after the write. */
void detour_optSave(void* self)
{
    ++g_gtSaves;
    /* review-settings4 D1: the authority's live values are its own and the truth - never saved as a personal change and reverted */
    if (g_gtHeld != 0 && StoreIsWorldAuthority()) GtRestore("this game is the world's authority (Options window closed)");
    if (g_gtHeld == 0)
    {
        orig_optSave(self);
        if (StoreIsWorldAuthority()) GtAuthoritySync("Options window closed", 1);
        return;
    }
    std::string moved;
    for (int i = 0; i < kGtCount; ++i)
    {
        if (!(g_gtHeld & (1 << i))) continue;
        unsigned int live = 0;
        if (GtRead(i, &live) && live != g_gtWorld[i])
        {
            moved += std::string(" ") + kGt[i].key + " " + GtText(i, g_gtOwn[i]) + "->" + GtText(i, live);
            g_gtOwn[i] = live;
        }
    }
    int ownFail = 0, backFail = 0;
    for (int i = 0; i < kGtCount; ++i)
        if ((g_gtHeld & (1 << i)) && !GtWrite(i, g_gtOwn[i]))
        {
            ++ownFail;   /* review-settings4 D3 */
            ErrorLog("[SETTINGS] settings.cfg wrapper: the player's own " + std::string(kGt[i].key) + " could not be put in before the save - settings.cfg may hold the world's value for " + kGt[i].key);
        }
    /* review-settings4 D2: the engine's save (Ogre::Root::saveConfig inside it) can throw - the world's values go back
       before the exception travels on; the file already has the player's values. A C++ try, not __try (C2712). */
    try { orig_optSave(self); }
    catch (...)
    {
        for (int i = 0; i < kGtCount; ++i) if (g_gtHeld & (1 << i)) GtWrite(i, g_gtWorld[i]);
        ErrorLog("[SETTINGS] settings.cfg wrapper: the engine's save threw - the world's values were put back and the exception passed on");
        throw;
    }
    for (int i = 0; i < kGtCount; ++i) if ((g_gtHeld & (1 << i)) && !GtWrite(i, g_gtWorld[i])) ++backFail;
    ++g_gtWrapped;
    DebugLog("[SETTINGS] settings.cfg written by the Options window with this player's own gameplay values (" + GtHeldOwnList().substr(1)
             + "); the world's values re-applied after the write"
             + (moved.empty() ? std::string() : " - the player's own change(s) kept for when they leave:" + moved + " (the world's value stays in force while connected)"));
    if (ownFail || backFail) ErrorLog("[SETTINGS] settings.cfg wrapper: " + I(ownFail) + " own-value write fault(s) before the save, " + I(backFail) + " world-value write fault(s) after it");
}

std::string GtLine()
{
    std::map<std::string, std::string> rec;
    const int known = RecSnapshot(&rec);
    std::string s = "[SETTINGS] gameplay-tab world follow: authority=" + I(StoreIsWorldAuthority() ? 1 : 0) + " following=" + I(g_gtHeld != 0 ? 1 : 0)
                    + " held:";
    int n = 0;
    for (int i = 0; i < kGtCount; ++i)
        if (g_gtHeld & (1 << i)) { s += std::string(" ") + kGt[i].key + "(own=" + GtText(i, g_gtOwn[i]) + " world=" + GtText(i, g_gtWorld[i]) + ")"; ++n; }
    if (n == 0) s += " none";
    s += " record(known=" + I(known) + "):";
    int r = 0;
    for (int i = 0; i < kGtCount; ++i)
    {
        std::map<std::string, std::string>::const_iterator it = rec.find(std::string("gt.") + kGt[i].key);
        if (it == rec.end()) continue;
        s += std::string(" ") + kGt[i].key + "=" + it->second; ++r;
    }
    if (r == 0) s += " none";
    return s + " hook=" + I(g_optHook) + " cfgWrites=" + I(g_gtSaves) + " wrapped=" + I(g_gtWrapped) + " sent=" + I(g_gtSent) + " left=" + I(g_gtLeft);
}


/* ============ settings3 S3 - the character-creation lock (design 3.3; user decisions 2026-09-26) ============
   USER DECISION: a JOINER's New Game Advanced window shows the world's advanced options, LOCKED. The host may change
   them later, so the host's own window (this game is the world's authority) is never locked with the world's values.
   mp5b: a host with Game options rows waiting (not yet handed to a world) sees THOSE rows, locked - they are what its
   load writes (HostChoicesToLive); change them on the Game options screen.
   (a) Post-hook NewGameWindow::toggleAdvancedOptions 0x9126A0 (MAIN THREAD: a MyGUI button delegate). When the
       window is now visible, and this game holds a world record with gp.* rows and is NOT the authority: call the
       engine's own NewGameOptionsWindow::setOptions 0x9120B0 with the world's block (it copies nine dwords into the
       global 0x2132528 and calls each line's refresh, vtable +0x18 - Confirmed bytes), then disable every line of
       optionsPanel (window+0xA0) through getNumLines 0x6F6640 / getLineByNum 0x6FC3D0 (the pair setOptions itself
       calls, through the thunks 0x39CC5 / 0x42929 - Confirmed bytes) and the line's vtable +0x10, called ONLY when
       that slot is DataPanelLine::setEnabled 0x6F6020, directly or through one `jmp rel32` thunk. Every
       DataPanelLine vtable in the exe (12) holds the thunk 0x2A964 -> 0x6F6020 there (Confirmed, a .rdata scan).
       A line whose slot is anything else is left enabled, and (b) still wins.
       NOTE: the design/brief name 0x6F5980 for the check. That is the header's RVA (another build); in the running
       1.0.65 exe 0x6F5980 is the middle of another function (Confirmed bytes), so the check uses the table row
       DataPanelLineSetEnabled 0x6F6020 (the design's own .br resolution). The code wins over the brief.
   (b) Pre-hook SaveManager::newGame 0x47A930 (newGameStart 0x916530 calls it - decomp_916530): a joiner rewrites the
       global from the record read live, so a slider moved by any route can never win. */
unsigned long long kNgToggleRva = 0; static AddrReg kNgToggleRva_reg("NewGameToggleAdvanced", &kNgToggleRva);   /* Steam_1.0.65 0x9126A0 NewGameWindow::toggleAdvancedOptions(this, Widget*): the options window = this+0x100, its main widget = window+0x8 (Confirmed bytes) */
unsigned long long kNgSetOptRva = 0; static AddrReg kNgSetOptRva_reg("NewGameOptionsSetOptions", &kNgSetOptRva);   /* Steam_1.0.65 0x9120B0 NewGameOptionsWindow::setOptions(this, const GameplayOptions&) */
unsigned long long kDpNumLinesRva = 0; static AddrReg kDpNumLinesRva_reg("DatapanelGetNumLines", &kDpNumLinesRva);   /* Steam_1.0.65 0x6F6640 int DatapanelGUI::getNumLines(this, int cat) */
unsigned long long kDpLineByNumRva = 0; static AddrReg kDpLineByNumRva_reg("DatapanelGetLineByNum", &kDpLineByNumRva);   /* Steam_1.0.65 0x6FC3D0 DataPanelLine* DatapanelGUI::getLineByNum(this, int cat, int i) */
unsigned long long kDplSetEnabledRva = 0; static AddrReg kDplSetEnabledRva_reg("DataPanelLineSetEnabled", &kDplSetEnabledRva);   /* Steam_1.0.65 0x6F6020 DataPanelLine::setEnabled(this, bool) - compared, never hooked */
unsigned long long kSmNewGameRva = 0; static AddrReg kSmNewGameRva_reg("SaveManagerNewGame", &kSmNewGameRva);   /* Steam_1.0.65 0x47A930 SaveManager::newGame(this, ...) */
typedef void (*NgToggleFn)(void* self, void* sender, void* a3, void* a4);
typedef void (*NgSetOptFn)(void* win, const void* opts);
typedef int (*DpNumLinesFn)(void* panel, int cat);
typedef void* (*DpLineByNumFn)(void* panel, int cat, int i);
typedef void (*DplSetEnabledFn)(void* line, bool on);
typedef unsigned long long (*SmNewGameFn)(void* self, void* a2, void* a3, void* a4);
NgToggleFn orig_ngToggle = 0;
SmNewGameFn orig_smNewGame = 0;
const size_t kNgwOptions = 0x100, kNgoMainWidget = 0x08, kNgoPanel = 0xA0, kDplSetEnabledSlot = 0x10;
const int kNgMaxLines = 256;

/* Engine calls, each in a frame holding no C++ object (C2712). 0 = the call faulted. */
int SehSetOptions(NgSetOptFn f, void* win, const unsigned char* b) { __try { f(win, b); return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
int SehNumLines(DpNumLinesFn f, void* panel, int* n) { __try { *n = f(panel, 0); return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
int SehLineByNum(DpLineByNumFn f, void* panel, int i, void** line) { __try { *line = f(panel, 0, i); return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
int SehSetEnabled(DplSetEnabledFn f, void* line, bool on) { __try { f(line, on); return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }

/* 1 when `slot` is `want` itself or a single `jmp rel32` (the linker's thunk) to it. */
int SlotIs(uintptr_t slot, uintptr_t want)
{
    if (slot == 0 || want == 0) return 0;
    if (slot == want) return 1;
    unsigned char j[5];
    if (!SafeCopy(j, (const void*)slot, 5) || j[0] != 0xE9) return 0;
    int rel = 0;
    std::memcpy(&rel, j + 1, 4);
    return (uintptr_t)((long long)slot + 5 + (long long)rel) == want ? 1 : 0;
}

/* The joiner test both halves share. 1 = lock; else `why` says whose options stand. */
int NgJoiner(std::string* why)
{
    if (StoreIsWorldAuthority()) { *why = "this game is the world's authority - its own options stand"; return 0; }
    std::map<std::string, std::string> rec;
    if (!RecSnapshot(&rec)) { *why = "no world record on this link - not joined to a world"; return 0; }
    int n = 0;
    for (int i = 0; i < coopgp::kGpCount; ++i) if (rec.find("gp." + GpName(i)) != rec.end()) ++n;
    if (n == 0) { *why = "the world record holds no gp.* row yet"; return 0; }
    return 1;
}

/* mp5b: the host's own New Game. The gp.* rows the host chose on the Game options screen (SettingsHostChoices), not
   yet handed to a world; they are written over the live values at this game's load (HostChoicesToLive), so the
   Advanced window must show THEM, locked, or it would show numbers that do not apply. Returns how many gp.* rows. */
int NgHostChosen(std::map<std::string, std::string>* out)
{
    out->clear();
    std::vector<std::pair<std::string, std::string> > kv;
    if (!HostChoicesSnapshot(&kv)) return 0;
    for (size_t k = 0; k < kv.size(); ++k) if (kv[k].first.compare(0, 3, "gp.") == 0) (*out)[kv[k].first] = kv[k].second;
    return (int)out->size();
}

/* The world's block: the live 36 bytes with every valid recorded gp.* value laid over them. Returns the number of
   recorded values used (0 = nothing to show); *differ = how many differed from the live global. mp5b: `src` given =
   the host's Game options rows instead of the world record. */
int NgWorldBlock(unsigned char* b, int* differ, const std::map<std::string, std::string>* src)
{
    *differ = 0;
    std::map<std::string, std::string> rec;
    if (src != 0) rec = *src;
    else if (!RecSnapshot(&rec)) return 0;
    if (!GpReadLive(b)) return 0;
    int used = 0;
    for (int i = 0; i < coopgp::kGpCount; ++i)
    {
        const std::string key = "gp." + GpName(i);
        std::map<std::string, std::string>::const_iterator it = rec.find(key);
        if (it == rec.end()) continue;
        double v = 0.0;
        if (!coopgp::GpValueOk(key, it->second, &v)) { ErrorLog("[SETTINGS] world record " + key + "='" + it->second + "' is out of range - NOT shown"); continue; }
        ++used;
        if (GpEquals(b, i, v)) continue;
        ++*differ;
        const coopgp::GpField& f = coopgp::GpFields()[i];
        if (f.kind == coopgp::kGpBool) b[f.off] = (unsigned char)(v != 0.0 ? 1 : 0);
        else { const float fl = (float)v; std::memcpy(b + f.off, &fl, 4); }
    }
    return used;
}

/* The note, a TextBox of ours on the window's main widget (MyGUI puts it in the client area). Found by name, so a
   second toggle - or a rebuilt window - never makes a second one. Name: "Coop" prefix, no '_' (ui.cpp's rule). */
/* OUT OF SCOPE (review-settings3, no change): ImportGameMenu::toggleAdvancedOptions 0x47A300 has its own Advanced
   window, which S3 does not lock; importing into a co-op world is not an S3 path. The newGame backstop does not cover
   it either unless the import path reaches SaveManager::newGame (not checked). */
int NgNote(void* mainW, int allLocked, int host)
{
    if (mainW == 0) return 0;
    try
    {
        MyGUI::Widget* w = (MyGUI::Widget*)mainW;
        const std::string name("CoopWorldLockNote");
        MyGUI::Widget* x = w->findWidget(name);
        if (x == 0)
        {
            const MyGUI::IntCoord c = w->getClientCoord();
            x = w->createWidgetT("TextBox", "Kenshi_GenericTextBoxFlat", MyGUI::IntCoord(16, c.height - 34, c.width - 32, 24), MyGUI::Align::Default, name);
        }
        MyGUI::TextBox* tb = x != 0 ? x->castType<MyGUI::TextBox>(false) : 0;
        if (tb == 0) return 0;
        if (host) tb->setCaption(MyGUI::UString("Set in MULTIPLAYER > GAME OPTIONS."));   /* ui2: owner 2026-09-27 wording */
        else tb->setCaption(MyGUI::UString(allLocked ? "Set by the host." : "Set by the host - changes here won't be used."));
        tb->setVisible(true);
        return 1;
    }
    catch (...) { return 0; }
}

/* Every line of the window's panel through its own setEnabled, called ONLY when the slot is DataPanelLine::setEnabled
   (SlotIs). *n = the panel's line count (-1 unreadable). */
void NgSetLines(void* win, bool on, int* n, int* done, int* skipped, int* faults)
{
    *n = -1; *done = 0; *skipped = 0; *faults = 0;
    void* panel = 0;
    const uintptr_t want = Base() + (uintptr_t)kDplSetEnabledRva;
    if (!SafeCopy(&panel, (const unsigned char*)win + kNgoPanel, sizeof panel) || panel == 0
        || !SehNumLines((DpNumLinesFn)(Base() + (uintptr_t)kDpNumLinesRva), panel, n)) return;
    for (int i = 0; i < *n && i < kNgMaxLines; ++i)
    {
        void* line = 0;
        if (!SehLineByNum((DpLineByNumFn)(Base() + (uintptr_t)kDpLineByNumRva), panel, i, &line)) { ++*faults; continue; }
        uintptr_t vt = 0, slot = 0;
        if (line == 0 || !SafeCopy(&vt, line, sizeof vt) || vt == 0 || !SafeCopy(&slot, (const void*)(vt + kDplSetEnabledSlot), sizeof slot)) { ++*skipped; continue; }
        if (!SlotIs(slot, want)) { ++*skipped; continue; }   /* not DataPanelLine::setEnabled: never called */
        if (SehSetEnabled((DplSetEnabledFn)slot, line, on)) ++*done; else ++*faults;
    }
}

/* Hide our note if this window has one. Found by name every time - never a kept pointer. 1 = hidden. */
int NgNoteHide(void* mainW)
{
    if (mainW == 0) return 0;
    try
    {
        MyGUI::Widget* x = ((MyGUI::Widget*)mainW)->findWidget(std::string("CoopWorldLockNote"));
        if (x == 0) return 0;
        x->setVisible(false);
        return 1;
    }
    catch (...) { return 0; }
}

/* review-settings3 1e: the lock is UNDONE. NewGameWindow::close 0x916100 and newGameStart only HIDE the Advanced
   window, so a window this game locked earlier would stay grey with the note for the rest of the session after the
   game unlinks or becomes the world's host. Whenever the window becomes visible and the game is not a locked joiner
   (not a joiner, no record, or the lock could not be applied), every line is re-enabled through the same checked
   slot and the note is hidden. Nothing to undo before this process's first lock: only NgLock ever disables. */
void NgUnlock(void* win, void* mainW, const std::string& why)
{
    if (g_ngLocks == 0) return;
    int n = -1, done = 0, skipped = 0, faults = 0;
    NgSetLines(win, true, &n, &done, &skipped, &faults);
    const int hidden = NgNoteHide(mainW);
    ++g_ngUnlocks;
    DebugLog("[SETTINGS] New Game advanced window unlocked (" + I(done) + " lines re-enabled, note hidden=" + I(hidden) + ") lines=" + I(n)
             + " skipped=" + I(skipped) + " faults=" + I(faults) + " - " + why);
}

void NgLock(void* ngw)
{
    void* win = 0;
    if (ngw == 0 || !SafeCopy(&win, (const unsigned char*)ngw + kNgwOptions, sizeof win) || win == 0) { ErrorLog("[SETTINGS] New Game advanced window: the options window pointer is unreadable - NOT locked (the commit backstop still applies)"); return; }
    void* mainW = 0;
    if (!SafeCopy(&mainW, (const unsigned char*)win + kNgoMainWidget, sizeof mainW)) mainW = 0;
    if (mainW != 0 && !((MyGUI::Widget*)mainW)->getVisible()) return;   /* this click closed it */
    std::string why;
    std::map<std::string, std::string> chosen;
    const int host = NgJoiner(&why) ? 0 : 1;   /* a joiner's world record always wins over any Game options left on this game */
    if (host && NgHostChosen(&chosen) == 0) { DebugLog("[SETTINGS] New Game advanced window NOT locked: " + why + "; no Game options chosen on this game"); NgUnlock(win, mainW, why); return; }
    unsigned char b[0x24];
    int differ = 0;
    const int used = NgWorldBlock(b, &differ, host ? &chosen : 0);
    if (used == 0) { ErrorLog("[SETTINGS] New Game advanced window NOT locked: the " + std::string(host ? "Game options" : "world's") + " block could not be built (global unreadable or no valid gp.* value) - the commit backstop still applies"); NgUnlock(win, mainW, "the lock could not be applied"); return; }
    if (!SehSetOptions((NgSetOptFn)(Base() + (uintptr_t)kNgSetOptRva), win, b)) { ErrorLog("[SETTINGS] New Game advanced window: setOptions FAULTED - NOT locked (the commit backstop still applies)"); NgUnlock(win, mainW, "setOptions faulted"); return; }
    int n = -1, disabled = 0, skipped = 0, faults = 0;
    NgSetLines(win, false, &n, &disabled, &skipped, &faults);
    const int note = NgNote(mainW, (n > 0 && disabled == n) ? 1 : 0, host);
    ++g_ngLocks;
    DebugLog("[SETTINGS] New Game advanced window locked (" + I(disabled) + " lines disabled, " + (host ? "the host's Game options" : "world values") + " shown) lines=" + I(n)
             + " skipped=" + I(skipped) + " faults=" + I(faults) + " worldValues=" + I(used) + " replaced=" + I(differ) + " note=" + I(note));
}

void detour_ngToggle(void* self, void* sender, void* a3, void* a4)
{
    orig_ngToggle(self, sender, a3, a4);
    NgLock(self);
}

unsigned long long detour_smNewGame(void* self, void* a2, void* a3, void* a4)
{
    std::string why;
    if (NgJoiner(&why))
    {
        const int changed = ApplyRecord("the New Game window's", 0);
        ++g_ngCommits;
        DebugLog("[SETTINGS] New Game commit: global rewritten from the world (changed=" + I(changed) + ")");
    }
    else DebugLog("[SETTINGS] New Game commit: this game's own advanced options stand (" + why + ")");
    return orig_smNewGame(self, a2, a3, a4);
}

/* ============ mp5 (docs/design-mpmenu1.md section 5) - THE HOST'S GAME OPTIONS, chosen on the title screen ============
   The Game options screen hands over the rows the host CHANGED (SettingsHostChoices). They reach the world through the
   SAME authority path as advset / gtset / recruitmult: StoreSendOptions, which the notebook accepts only from the
   world's authority, stores in options.txt and broadcasts to every game. Sent the first moment this game is the
   authority with the world's record in hand (the OPTIONS arrival after WELCOME, or at once when it already is).
   Also WRITTEN over the host's live values at its load (detour_gpLoad, right after the S2 ApplyRecord: record over the
   save's, then these over both) and at its world-live edge (a New Game commits the Advanced window, and the gameplay tab
   has no load), so the world-live record finds nothing of the save's to re-record over the host's choice. Cleared once
   sent with the world live: from then on the live values ARE the world's, and later changes use the existing paths.
   g_hostChoices is under g_recCs (the load hook runs inside loadGame); g_hostChoicesSent is MAIN THREAD. */
std::vector<std::pair<std::string, std::string> > g_hostChoices;
int g_hostChoicesSent = 0;
volatile LONGLONG g_hcWritten = 0;
long long g_hcSends = 0;

int HostChoicesSnapshot(std::vector<std::pair<std::string, std::string> >* out) { RecLock l; *out = g_hostChoices; return out->empty() ? 0 : 1; }

int HostChoicesToLive(const char* at)
{
    std::vector<std::pair<std::string, std::string> > ch;
    if (!HostChoicesSnapshot(&ch)) return 0;
    unsigned char b[0x24];
    const int haveGp = GpReadLive(b);
    std::string what;
    int n = 0, failed = 0;
    for (size_t k = 0; k < ch.size(); ++k)
    {
        const std::string& key = ch[k].first;
        const std::string& val = ch[k].second;
        if (key.compare(0, 3, "gp.") == 0)
        {
            const int i = coopgp::GpIndex(key);
            double v = 0.0;
            if (i < 0 || !coopgp::GpValueOk(key, val, &v)) { ++failed; continue; }
            if (haveGp && GpEquals(b, i, v)) continue;
            if (!GpWrite(i, v)) { ++failed; continue; }
            what += " " + key + "->" + GpCanon(i, v);
            ++n;
        }
        else if (key.compare(0, 3, "gt.") == 0)
        {
            const int i = GtIndex(key);
            unsigned int v = 0, live = 0;
            if (i < 0 || !GtParse(i, val, &v)) { ++failed; continue; }
            if (g_gtHeld & (1 << i)) continue;   /* following another authority's world: never written over */
            if (GtRead(i, &live) && live == v) continue;
            if (!GtWrite(i, v)) { ++failed; continue; }
            what += " " + key + "->" + GtText(i, v);
            ++n;
        }
    }
    ::InterlockedExchangeAdd64(&g_hcWritten, (LONGLONG)n);
    if (n > 0 || failed > 0)
        DebugLog(std::string("[SETTINGS] host's Game options written over the live values at=") + at + ":" + (what.empty() ? std::string(" none") : what)
                 + " (" + I(n) + " changed" + (failed ? ", " + I(failed) + " not written" : std::string()) + ")");
    return n;
}

void HostChoicesSend(const char* at)
{
    if (g_hostChoicesSent || !StoreIsWorldAuthority()) return;
    std::vector<std::pair<std::string, std::string> > ch;
    if (!HostChoicesSnapshot(&ch)) return;
    std::map<std::string, std::string> rec;
    if (!RecSnapshot(&rec)) return;   /* no map on this link yet: its arrival runs this again */
    const bool ok = StoreSendOptions(ch);
    if (ok) { g_hostChoicesSent = 1; ++g_hcSends; }
    std::string what;
    for (size_t k = 0; k < ch.size(); ++k) what += " " + ch[k].first + "=" + ch[k].second;
    DebugLog(std::string("[SETTINGS] authority: ") + (ok ? "sent " : "could NOT send ") + I((long long)ch.size())
             + " Game options choice(s) to the world record at=" + at + ":" + what
             + (ok ? " - the notebook stores them and broadcasts them to every game" : " - kept; sent at the next OPTIONS arrival"));
}

void HostChoicesClear(const char* at)
{
    {
        RecLock l;
        if (g_hostChoices.empty()) return;
        g_hostChoices.clear();
    }
    g_hostChoicesSent = 0;
    DebugLog(std::string("[SETTINGS] host's Game options are in the world (at=") + at + ", " + I(g_hcSends) + " send(s), "
             + I((long long)g_hcWritten) + " live write(s)) - later changes go through the usual change paths");
}

} // namespace

void SettingsTick()
{
    static int s_link = 0, s_live = 0;
    const int link = net::SessionLinked() ? 1 : 0;
    const int live = (GameplayRunning() && !EngineWritesBlocked()) ? 1 : 0;
    static long s_arrCursor = 0;   /* M11 C2: a player entering the world (the old link's up edge is the session peer's) */
    const int linkEdge = StoreArrivalsSince(kArrServeSettings, &s_arrCursor, 0) > 0 ? 1 : 0;
    const int liveEdge = (live && !s_live) ? 1 : 0;
    s_link = link; s_live = live;
    if (linkEdge && liveEdge) LogAll("link+worldlive");
    else if (linkEdge) LogAll("link");
    else if (liveEdge) LogAll("worldlive");
    /* settings2 S2: the notebook link's down edge forgets the record (the next WELCOME re-sends the map), and the
       world-live edge is when the authority records (absent keys, and its own change) and any other game re-applies. */
    static int s_relay = 0;
    const int relay = StoreRelayLinked() ? 1 : 0;
    /* settings4 S4: the notebook link's down edge (the world's record goes with it) and world leave put this player's own
       gameplay-tab values back; after a world leave with the link still up, nothing is followed until the next world-live edge. */
    if (!relay && s_relay) { GtRestore("link down"); g_gtLeft = 0; }
    static int s_run = 0;
    const int run = GameplayRunning() ? 1 : 0;
    if (!run && s_run) { GtRestore("world left"); if (relay) g_gtLeft = 1; }
    if (run && !s_run) g_gtLeft = 0;
    s_run = run;
    /* review-settings4 D1: the notebook's authority message can make this game the authority with no OPTIONS map - its
       live values are its own and the truth from that moment. */
    static int s_auth = 0;
    const int auth = StoreIsWorldAuthority() ? 1 : 0;
    if (auth && !s_auth) GtRestore("this game became the world's authority");
    s_auth = auth;
    ::InterlockedExchange(&g_hcNotAuth, (relay && !auth) ? 1 : 0);   /* mp5: the load hook never writes the host's choices over another authority's world */
    if (!relay && s_relay) { { RecLock l; g_rec.clear(); g_recKnown = 0; } DebugLog("[SETTINGS] notebook link down - the world record is forgotten until the next WELCOME's OPTIONS map"); }
    if (!relay && s_relay) g_hostChoicesSent = 0;   /* mp5: a new link sends the host's Game options again (the notebook may have restarted) */
    s_relay = relay;
    if (liveEdge)
    {
        /* mp5: the host's Game options over the live values first, so the world-live record below sends them as its own change */
        if (StoreIsWorldAuthority()) { GtRestore("this game is the world's authority (world-live edge)"); HostChoicesToLive("world-live edge"); }
        if (StoreIsWorldAuthority()) AuthoritySync("worldlive", 1);
        else ApplyRecord("this game's values at the world-live edge", 0);
        if (StoreIsWorldAuthority()) { GtRestore("this game is the world's authority (world-live edge)"); GtAuthoritySync("worldlive", 1); }   /* settings4 S4 */
        else GtFollow("world-live edge");
        if (StoreIsWorldAuthority()) { HostChoicesSend("world-live edge"); if (g_hostChoicesSent) HostChoicesClear("world-live edge"); }   /* mp5 */
    }
}

void InstallSettings()
{
    static int s_done = 0;
    if (s_done) return;
    s_done = 1;
    ::InitializeCriticalSection(&g_recCs);
    ::InterlockedExchange(&g_recCsInit, 1);
    /* settings3 S3: the character-creation lock (design 3.3) - the two halves do not depend on each other's rows. */
    if (kNgToggleRva == 0 || kNgSetOptRva == 0 || kDpNumLinesRva == 0 || kDpLineByNumRva == 0 || kDplSetEnabledRva == 0 || kSetAdvancedRva == 0) { g_ngHook = -2; ErrorLog("[SETTINGS] S3: a New Game Advanced window row is not in the address table - the window is NOT locked (the commit backstop still applies)"); }
    else
    {
        g_ngHook = (coop::AddHook((void*)(Base() + (uintptr_t)kNgToggleRva), (void*)&detour_ngToggle, (void**)&orig_ngToggle) == coop::SUCCESS) ? 1 : -1;
        if (g_ngHook == 1) DebugLog("[SETTINGS] S3: NewGameWindow::toggleAdvancedOptions post-hook installed - a joiner's Advanced window shows the world's values, locked");
        else ErrorLog("[SETTINGS] S3: NewGameWindow::toggleAdvancedOptions post-hook NOT installed - the window is NOT locked (the commit backstop still applies)");
    }
    if (kSmNewGameRva == 0 || kSetAdvancedRva == 0) { g_smHook = -2; ErrorLog("[SETTINGS] S3: SaveManagerNewGame is not in the address table - a joiner's New Game commit is NOT rewritten from the world"); }
    else
    {
        g_smHook = (coop::AddHook((void*)(Base() + (uintptr_t)kSmNewGameRva), (void*)&detour_smNewGame, (void**)&orig_smNewGame) == coop::SUCCESS) ? 1 : -1;
        if (g_smHook == 1) DebugLog("[SETTINGS] S3: SaveManager::newGame pre-hook installed - a joiner's new game starts with the world's advanced options");
        else ErrorLog("[SETTINGS] S3: SaveManager::newGame pre-hook NOT installed - a joiner's New Game commit is NOT rewritten from the world");
    }
    /* settings4 S4: the settings.cfg write wrapper (design 3.5). First, because it does not depend on the S2 rows;
       without it this game never follows the world's gameplay-tab values (GtFollow). */
    if (kOptSaveRva == 0 || kSetOptionsRva == 0) { g_optHook = -2; ErrorLog("[SETTINGS] S4: OptionsWindowSaveOptions or OptionsHolderGlobal is not in the address table - the world's gameplay-tab settings are NOT followed"); }
    else
    {
        g_optHook = (coop::AddHook((void*)(Base() + (uintptr_t)kOptSaveRva), (void*)&detour_optSave, (void**)&orig_optSave) == coop::SUCCESS) ? 1 : -1;
        if (g_optHook == 1) DebugLog("[SETTINGS] S4: OptionsWindow::saveOptions wrapper installed - settings.cfg only ever receives this player's own gameplay values");
        else ErrorLog("[SETTINGS] S4: OptionsWindow::saveOptions wrapper NOT installed - the world's gameplay-tab settings are NOT followed on this game");
    }
    if (kGpLoadRva == 0 || kSetAdvancedRva == 0) { g_gpHook = -2; ErrorLog("[SETTINGS] S2: GameplayOptionsLoad or GameplayOptionsGlobal is not in the address table - the world's advanced options are NOT applied at load"); return; }
    g_gpHook = (coop::AddHook((void*)(Base() + (uintptr_t)kGpLoadRva), (void*)&detour_gpLoad, (void**)&orig_gpLoad) == coop::SUCCESS) ? 1 : -1;
    if (g_gpHook == 1) DebugLog("[SETTINGS] S2: GameplayOptions::load post-hook installed - every load applies the world's advanced options over the save's");
    else ErrorLog("[SETTINGS] S2: GameplayOptions::load post-hook NOT installed - the world's advanced options are applied only on OPTIONS arrival and the world-live edge");
}

void SettingsWorldOptionsBegin() { g_recPending.clear(); }
void SettingsWorldOption(const std::string& key, const std::string& value) { g_recPending[key] = value; }
void SettingsWorldOptionsEnd(bool complete)
{
    if (!complete) { DebugLog("[SETTINGS] an OPTIONS map was cut short - the world record is left as it was"); return; }
    /* review-settings2 D7: a reply holding `@refused` is the notebook saying options.txt could not be written - it
       carries no record, so it must never replace one (an empty record made the authority re-send all 11 keys, be
       refused again, and loop once per round trip). Logged once per episode; the old record is kept. */
    static int s_refusedEpisode = 0;
    if (g_recPending.find("@refused") != g_recPending.end())
    {
        if (!s_refusedEpisode) DebugLog("[SETTINGS] the notebook refused an options write (" + g_recPending["@refused"] + ") - the world record is kept as it was and nothing is re-sent for this reply");
        s_refusedEpisode = 1;
        return;
    }
    s_refusedEpisode = 0;
    { RecLock l; g_rec = g_recPending; g_recKnown = 1; }
    DebugLog(RecordLine() + " at=OPTIONS");
    HostChoicesSend("OPTIONS arrival");   /* mp5: the host's Game options, the first time this game is the authority with the record */
    if (g_hostChoicesSent && GameplayRunning() && !EngineWritesBlocked()) HostChoicesClear("OPTIONS arrival");
    /* review-settings2 D11: ON THE AUTHORITY an arrival never writes the live values - they are the truth, and a
       change not yet sent would be silently undone. It only records keys the notebook does not hold; a changed value
       goes up through the change path (world-live edge, advset). Every other game applies the record as before. */
    if (StoreIsWorldAuthority()) { if (GameplayRunning() && !EngineWritesBlocked()) AuthoritySync("OPTIONS arrival", 0); }
    else ApplyRecord("this game's values (OPTIONS arrival)", 0);
    /* settings4 S4: gt.* the same way - the authority records absent keys, every other game follows the record. */
    if (StoreIsWorldAuthority()) { GtRestore("this game is the world's authority (OPTIONS arrival)"); if (GameplayRunning() && !EngineWritesBlocked()) GtAuthoritySync("OPTIONS arrival", 0); }
    else GtFollow("OPTIONS arrival");
}

std::string SettingsAdvSetCommand(const std::string& keyIn, const std::string& value)
{
    const int i = coopgp::GpIndex(keyIn);
    if (i < 0) return "error advset: unknown key '" + keyIn + "' (cod ep gdm bs nnm rs ps ht bl ae dh)";
    const std::string key = "gp." + GpName(i);
    double v = 0.0;
    if (!coopgp::GpValueOk(key, value, &v)) return "error advset: '" + value + "' is not a legal value for " + key;
    const std::string canon = GpCanon(i, v);
    unsigned char b[0x24];
    if (!GpReadLive(b)) return "error advset: the advanced options global is unreadable";
    const std::string was = GpLiveText(b, i);
    if (!StoreIsWorldAuthority())
    {
        const bool sent = StoreSendOptions(std::vector<std::pair<std::string, std::string> >(1, std::make_pair(key, canon)));
        DebugLog("[SETTINGS] advset " + key + "=" + canon + " REFUSED on this game - it is not the world's authority, so the world's value stays in force (live " + was + ")"
                 + (sent ? "; the offer went up so the notebook logs its own refusal" : "; no notebook link - nothing sent"));
        return "refused advset " + GpName(i) + ": this game is not the world's authority (live stays " + was + ")";
    }
    if (!GpWrite(i, v)) return "error advset: the live value could not be written";
    DebugLog("[SETTINGS] advset " + key + ": " + was + "->" + canon + " on the world's authority (live now) - recorded as an own change");
    AuthoritySync("advset", 1);
    return "ok advset " + GpName(i) + "=" + canon + " (authority: live value set and sent to the world record)";
}

/* settings4 S4: the TEST-ONLY `gtset` lever, the gt.* twin of advset. Authority: live value set, recorded as an own
   change (settings.cfg is NOT written - only the Options window writes it). Anyone else: refused, the offer goes up. */
std::string SettingsGtSetCommand(const std::string& keyIn, const std::string& value)
{
    const int i = GtIndex(keyIn);
    if (i < 0) return "error gtset: unknown key '" + keyIn + "' (pop squad raidsize raidfreq attacks limbloss civ)";
    const std::string key = std::string("gt.") + kGt[i].key;
    unsigned int v = 0;
    if (!GtParse(i, value, &v)) return "error gtset: '" + value + "' is not a legal value for " + key;
    const std::string canon = GtText(i, v);
    unsigned int live = 0;
    if (!GtRead(i, &live)) return "error gtset: the gameplay options object is unreadable";
    const std::string was = GtText(i, live);
    if (!StoreIsWorldAuthority())
    {
        const bool sent = StoreSendOptions(std::vector<std::pair<std::string, std::string> >(1, std::make_pair(key, canon)));
        DebugLog("[SETTINGS] gtset " + key + "=" + canon + " REFUSED on this game - it is not the world's authority, so the world's value stays in force (live " + was + ")"
                 + (sent ? "; the offer went up so the notebook logs its own refusal" : "; no notebook link - nothing sent"));
        return "refused gtset " + std::string(kGt[i].key) + ": this game is not the world's authority (live stays " + was + ")";
    }
    GtRestore("this game is the world's authority (gtset)");   /* review-settings4 D1: a held field would undo the lever at the next restore */
    if (!GtWrite(i, v)) return "error gtset: the live value could not be written";
    DebugLog("[SETTINGS] gtset " + key + ": " + was + "->" + canon + " on the world's authority (live now; settings.cfg not written) - recorded as an own change");
    const long long sentBefore = g_gtSent;
    GtAuthoritySync("gtset", 1);
    if (g_gtSent != sentBefore) return "ok gtset " + std::string(kGt[i].key) + "=" + canon + " (authority: live value set and sent to the world record)";
    std::map<std::string, std::string> rec;
    const int known = RecSnapshot(&rec);
    return "ok gtset " + std::string(kGt[i].key) + "=" + canon + " (authority: live value set; not sent - "
           + (known ? std::string("the record already holds it, or the notebook link is down") : std::string("no world record yet")) + ")";
}

/* mp5: the Game options screen's hand-over (see HostChoicesToLive). Keys other than gp.* / gt.* (the co-op options)
   only travel to the notebook, which checks every value itself. */
int SettingsHostChoices(const std::vector<std::pair<std::string, std::string> >& kv)
{
    std::vector<std::pair<std::string, std::string> > keep;
    std::string what;
    for (size_t k = 0; k < kv.size(); ++k)
    {
        const std::string& key = kv[k].first;
        const bool gp = key.compare(0, 3, "gp.") == 0, gt = key.compare(0, 3, "gt.") == 0;
        if ((gp && !coopgp::GpValueOk(key, kv[k].second, 0)) || (gt && !coopgp::GtValueOk(key, kv[k].second))) { ErrorLog("[SETTINGS] Game options: " + key + "='" + kv[k].second + "' is out of range - dropped"); continue; }
        keep.push_back(kv[k]);
        what += " " + key + "=" + kv[k].second;
    }
    int had = 0;
    { RecLock l; had = g_hostChoices.empty() ? 0 : 1; g_hostChoices = keep; }
    g_hostChoicesSent = 0;
    if (keep.empty())
    {
        if (had) DebugLog("[SETTINGS] host's Game options withdrawn - nothing chosen on that screen is waiting to go into a world");
        return 0;
    }
    DebugLog("[SETTINGS] host's Game options handed over:" + what + " - sent when this game is the world's authority with its record, and written over the live values at this game's load and world-live edge");
    const int live = (GameplayRunning() && !EngineWritesBlocked()) ? 1 : 0;
    if (live && StoreIsWorldAuthority()) HostChoicesToLive("Game options screen (world live)");
    HostChoicesSend("Game options screen");
    const int sent = g_hostChoicesSent;
    if (sent && live) HostChoicesClear("Game options screen (world live)");
    return sent;
}

std::string SettingsLiveValue(const std::string& key)
{
    if (key.compare(0, 3, "gp.") == 0)
    {
        const int i = coopgp::GpIndex(key);
        unsigned char b[0x24];
        if (i < 0 || !GpReadLive(b)) return std::string();
        return GpLiveText(b, i);
    }
    if (key.compare(0, 3, "gt.") == 0)
    {
        const int i = GtIndex(key);
        unsigned int v = 0;
        if (i < 0 || !GtRead(i, &v)) return std::string();
        if (g_gtHeld & (1 << i)) return GtText(i, g_gtOwn[i]);   /* following a world: this player's own value */
        return GtText(i, v);
    }
    return std::string();
}

std::string SettingsDumpCommand()
{
    LogAll("verb");
    return "ok settingsdump fingerprint=" + g_fp.hash + " n=" + I(g_fp.n);
}

/* settings5 S5 (design section 4): the list the store HELLO carries - the fingerprint's own rows, so the notebook compares
   exactly what S1 hashed. MAIN THREAD (store.cpp PumpLink, at the link-up edge, i.e. at the title screen). */
int SettingsModList(coopmods::ModList* out)
{
    if (!g_fp.done)
    {
        ComputeFingerprint();
        for (size_t i = 0; i < g_fp.detail.size(); ++i) DebugLog(g_fp.detail[i]);
    }
    *out = coopmods::ModList();
    out->known = g_fp.known; out->hash = g_fp.hash; out->active = g_fp.activeRows; out->plugins = g_fp.pluginRows;
    DebugLog("[MODS] sent to the notebook: fingerprint=" + g_fp.hash + " mods=" + I((long long)g_fp.activeRows.size())
             + " plugins=" + I((long long)g_fp.pluginRows.size()) + " known=" + I(g_fp.known)
             + (ConfigModFingerprintSalt() != 0 ? std::string(" (TEST salt row included)") : std::string()));
    return g_fp.known;
}

} // namespace coop
