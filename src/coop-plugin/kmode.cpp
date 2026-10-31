/* kmode.cpp - DEDICATED: THE WORLD'S START MODE, plugin side. See kmode.h and src/common/startmode.h (the pure rules, tested offline).

   WHAT IS ENFORCED: in a world whose notebook says startmode=kenshi, the start record Kenshi's NEW GAME window hands to
   SaveManager::newGame (its string id; newGameStart passes starts[index]->stringID) is replaced by one of the six Drifter starts,
   picked uniformly at random, before the request is posted. The engine's pump then looks the id up as a START record (type 64) and
   begins the game from it - the character editor follows as usual, so race and appearance are the player's.
   THE START CHOOSER IS PRESSED THROUGH: when the autoload opens the NEW GAME window for a new character in such a world, ui.cpp
   (KmSkipTick) fires the window's own BEGIN one title frame later (swstart::SkipStep; the RE is in startmode.h), so the player
   sees at most a frame of it and lands in the character editor. Anything unsure leaves the window up and the swap on BEGIN
   stands; whatever the player picks there is still replaced. The window is pressed through only when every Drifter start is loaded
   in this run (DrifterStartsLoaded); on a first join the data mod only takes effect from the next launch, so the window stays up and
   the player chooses. If the Drifter record is not loaded (dedicatedserver.mod switched off) the player's pick stands and the log
   says so - never a start id the engine cannot find.

   THE DATA MOD: Kenshi registers one .mod per mod folder, so dedicatedserver.mod ships in <this mod>\dedicatedserver\ and is copied
   to <Kenshi>\mods\dedicatedserver\ (and switched on in data\mods.cfg) at a start where it is missing or different - ONLY while the
   last world this game joined was a kenshi-mode world (swstart::DataModDecide; the state file swstart::kDataModStateFile beside
   mods.cfg remembers it). Otherwise a mods.cfg line this plugin added is taken out again - only while mods.cfg is still exactly as the
   plugin left it (swstart::DataModOwned) - and a line it did not add is left alone.
   Kenshi reads mods.cfg before any link, so a change takes effect from the next launch. */
#include "kmode.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstring>
#include <string>
#include <vector>

#include "coop_log.h"
#include "addresses.h"          /* coop::GameWorldPtr */
#include "command_channel.h"    /* PathNextToDll */
#include "store.h"              /* StoreLinkIsUp, StoreLinkGen */
#include "u8file.h"
#include "game/GameWorld.h"
#include "game/GameData.h"
#include "game/GameDataManager.h"
#include "../common/startmode.h"

namespace {

int  g_kmWorld = swstart::kFreeplay;   /* the mode of the world on link g_kmLinkGen */
long g_kmLinkGen = -1;
bool g_kmSaw = false;                  /* this OPTIONS map carried the key */
std::string g_kmValue;
long long g_kmEnforced = 0, g_kmNotLoaded = 0, g_kmKept = 0;
unsigned long long g_kmCounter = 0;
long g_kmMapGen = -1;                  /* the link a whole OPTIONS map arrived on (the start mode is known for it) */
long g_kmKeyGen = -1;                  /* the link whose OPTIONS map carried startmode (only a --dedicated notebook sends it) */
int  g_kmTimeOpt = -1;                 /* the options map's timemode on link g_kmTimeGen: 0 fixed, 1 consensus, -1 unknown */
long g_kmTimeGen = -1;
/* THIS CHARACTER'S LEAN (the footer's Class line): set at the swap, read from the save folder's key file at a load.
   g_kmLeanOwed: 1 = write the key file beside the next save, 2 = remove it there (a new character that was not swapped). */
int  g_kmLean = -1;
int  g_kmLeanOwed = 0;

/* THE DATA MOD'S STATE (swstart::DataModState): read at the start, rewritten when a world's start mode becomes known */
swstart::DataModState g_dmState;
bool g_dmStateRead = false;
bool g_dmOnThisRun = false;            /* dedicatedserver.mod was switched on in mods.cfg when this run started (InstallStartModeData) */
bool g_dmJoinInstalled = false;        /* installed during this run on joining a kenshi-mode world (from the next launch) */

std::string NumS(long long v) { char b[32]; _snprintf(b, sizeof(b), "%lld", v); b[sizeof(b) - 1] = 0; return b; }

/* The engine's std::string (same compiler, same layout) copied out under SEH. */
int SehCopyStr(const void* s, char* buf, size_t cap)
{
    __try
    {
        const std::string* p = (const std::string*)s;
        size_t n = p->size();
        if (n >= cap) n = cap - 1;
        std::memcpy(buf, p->data(), n);
        buf[n] = 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { buf[0] = 0; return 0; }
}

/* The record of `sid` in the game's data, and its type; 0 when absent or unreadable. No C++ object in this frame (C2712). */
GameData* SehFindRecord(const std::string* sid, int* type)
{
    __try
    {
        ::GameWorld* gw = coop::GameWorldPtr();
        if (gw == 0) return 0;
        GameData* gd = gw->gamedata.getData(*sid);
        if (gd != 0) *type = (int)gd->type;
        return gd;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

unsigned long long Seed()
{
    LARGE_INTEGER q; q.QuadPart = 0; ::QueryPerformanceCounter(&q);
    ++g_kmCounter;
    return (unsigned long long)q.QuadPart ^ ((unsigned long long)::GetTickCount() << 20) ^ ((unsigned long long)::GetCurrentProcessId() << 40)
           ^ (g_kmCounter * 0x9E3779B97F4A7C15ULL);
}

/* ---- files (UTF-8 paths through u8file.h) ---- */
bool ReadAll(const std::string& path, std::string* out, size_t limit)
{
    out->clear();
    HANDLE h = U8CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; sz.QuadPart = 0;
    bool ok = ::GetFileSizeEx(h, &sz) != 0 && sz.QuadPart >= 0 && (unsigned long long)sz.QuadPart <= (unsigned long long)limit;
    if (ok && sz.QuadPart > 0)
    {
        out->resize((size_t)sz.QuadPart);
        DWORD got = 0;
        ok = ::ReadFile(h, &(*out)[0], (DWORD)out->size(), &got, 0) != 0 && got == (DWORD)out->size();
    }
    ::CloseHandle(h);
    if (!ok) out->clear();
    return ok;
}
/* Written beside the target, then moved over it in one step: a failed write never leaves half a file. */
bool WriteReplace(const std::string& path, const std::string& data, DWORD* err)
{
    const std::string tmp = path + ".new";
    HANDLE h = U8CreateFile(tmp.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) { *err = ::GetLastError(); return false; }
    DWORD put = 0;
    const bool ok = data.empty() || (::WriteFile(h, data.data(), (DWORD)data.size(), &put, 0) != 0 && put == (DWORD)data.size());
    if (!ok) *err = ::GetLastError();
    const bool flushed = ::FlushFileBuffers(h) != 0;
    ::CloseHandle(h);
    if (!ok || !flushed) { if (ok) *err = ::GetLastError(); U8DeleteFile(tmp.c_str()); return false; }
    if (!U8MoveFileEx(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { *err = ::GetLastError(); U8DeleteFile(tmp.c_str()); return false; }
    return true;
}
bool IsFile(const std::string& p) { const DWORD a = U8GetFileAttributes(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0; }
std::string FolderOf(const std::string& p) { const size_t s = p.find_last_of("\\/"); return s == std::string::npos ? std::string() : p.substr(0, s); }

/* The game folder by coop.cpp's rule: the exe's folder, or its parent (RE_Kenshi), whichever holds data\mods.cfg. "" = neither. */
std::string GameFolder()
{
    wchar_t buf[2048];
    const DWORD n = ::GetModuleFileNameW(0, buf, 2048);
    if (n == 0 || n >= 2048) return std::string();
    const std::string exeDir = FolderOf(U8FromW(std::wstring(buf, n).c_str()));
    if (!exeDir.empty() && IsFile(exeDir + "\\data\\mods.cfg")) return exeDir;
    const std::string up = FolderOf(exeDir);
    if (!up.empty() && IsFile(up + "\\data\\mods.cfg")) return up;
    return std::string();
}

std::string DataModStatePath() { const std::string game = GameFolder(); return game.empty() ? std::string() : game + "\\data\\" + swstart::kDataModStateFile; }
void DataModStateLoad()
{
    if (g_dmStateRead) return;
    g_dmStateRead = true;
    std::string text;
    const std::string path = DataModStatePath();
    if (!path.empty() && ReadAll(path, &text, 4096)) g_dmState = swstart::DataModStateParse(text);
}
void DataModStateSave(const char* why)
{
    const std::string path = DataModStatePath();
    DWORD err = 0;
    if (path.empty() || !WriteReplace(path, swstart::DataModStateText(g_dmState), &err))
        ErrorLog("[KMODE] data mod: the state file " + path + " could NOT be written (Windows error " + NumS((long long)err) + ") - " + why);
}
/* Copy the .mod into <Kenshi>\mods\dedicatedserver\ (when it differs) and switch it on in mods.cfg. *added = 1 when this call added
   the mods.cfg line, and *print is then the fingerprint of mods.cfg as written (swstart::ModsCfgPrint). true = installed and on now. */
bool DataModInstall(int* added, unsigned long long* print)
{
    *added = 0; *print = 0;
    const std::string src = coop::PathNextToDll((std::string(swstart::kDataModDir) + "\\" + swstart::kDataModFile).c_str());
    std::string bytes;
    if (src.empty() || !ReadAll(src, &bytes, 64u << 20) || bytes.empty())
    {
        DebugLog("[KMODE] data mod: no " + std::string(swstart::kDataModFile) + " in this mod's " + swstart::kDataModDir + " folder (" + src + ") - nothing installed; a kenshi-mode world's starts need it");
        return false;
    }
    const std::string game = GameFolder();
    if (game.empty()) { ErrorLog("[KMODE] data mod: the Kenshi folder (the one holding data\\mods.cfg) was not found - nothing installed"); return false; }
    const std::string dir = game + "\\mods\\" + swstart::kDataModDir, dst = dir + "\\" + swstart::kDataModFile;
    std::string have;
    const bool same = ReadAll(dst, &have, 64u << 20) && have == bytes;
    if (!same)
    {
        U8CreateDirectory((game + "\\mods").c_str(), 0);
        U8CreateDirectory(dir.c_str(), 0);
        DWORD err = 0;
        if (!WriteReplace(dst, bytes, &err)) { ErrorLog("[KMODE] data mod: " + dst + " could NOT be written (Windows error " + NumS((long long)err) + ") - a kenshi-mode world's starts are not available here"); return false; }
        DebugLog("[KMODE] data mod: " + dst + " " + (have.empty() ? "installed" : "updated") + " (" + NumS((long long)bytes.size()) + " bytes, from " + src + ")");
    }
    else DebugLog("[KMODE] data mod: " + dst + " is current");
    const std::string cfg = game + "\\data\\mods.cfg";
    std::string text;
    if (!ReadAll(cfg, &text, (size_t)coopmods::kModsCfgMaxBytes)) { ErrorLog("[KMODE] data mod: Kenshi's mod list " + cfg + " could not be read - " + swstart::kDataModFile + " NOT switched on"); return false; }
    std::string updated;
    if (swstart::ModsCfgWithLine(text, swstart::kDataModFile, &updated) == 0) { DebugLog("[KMODE] data mod: " + std::string(swstart::kDataModFile) + " is switched on in " + cfg); return true; }
    DWORD err = 0;
    if (!WriteReplace(cfg, updated, &err)) { ErrorLog("[KMODE] data mod: " + cfg + " could NOT be written (Windows error " + NumS((long long)err) + ") - " + swstart::kDataModFile + " is NOT switched on"); return false; }
    *added = 1; *print = swstart::ModsCfgPrint(updated);
    DebugLog("[KMODE] data mod: " + std::string(swstart::kDataModFile) + " switched on in " + cfg + " (added as its last line). Kenshi reads the list when it loads its data;"
             " if this run had read it already, the Drifter starts come with the next launch");
    return true;
}
/* A world's start mode is known (a whole OPTIONS map): remembered for the next launch; a kenshi-mode world whose data mod is not on
   in this run gets it installed now (it takes effect from the next launch). MAIN THREAD. */
void DataModWorldJoined(int mode)
{
    DataModStateLoad();
    const int last = mode == swstart::kKenshi ? swstart::kKenshi : swstart::kFreeplay;
    bool dirty = false;
    if (last == swstart::kKenshi && !g_dmOnThisRun && !g_dmJoinInstalled)
    {
        int added = 0; unsigned long long print = 0;
        if (DataModInstall(&added, &print))
        {
            g_dmJoinInstalled = true;
            if (added) { g_dmState.added = 1; g_dmState.cfgPrint = print; dirty = true; }
            ErrorLog(std::string("[KMODE] data mod: this world is kenshi mode and ") + swstart::kDataModFile + " was not switched on when this game started - it is"
                     " installed and switched on now, and the Drifter starts are available from the NEXT launch (a new character made in this run keeps the start it picks)");
        }
    }
    const bool changed = g_dmState.lastWorld != last;
    if (changed) { g_dmState.lastWorld = last; dirty = true; }
    if (!dirty) return;
    DataModStateSave("the next launch may not switch the data mod on or off as it should");
    if (changed)
        DebugLog(std::string("[KMODE] data mod: the last world joined is now ") + swstart::ModeName(last) + " mode - from the next launch " + swstart::kDataModFile
                 + (last == swstart::kKenshi ? " is switched on" : " is switched off again if this plugin switched it on (a line put there by hand is left alone)"));
}

}   /* anonymous namespace */

namespace coop
{

void StartModeOptionsBegin() { g_kmSaw = false; g_kmValue.clear(); }

bool StartModeOption(const std::string& key, const std::string& value)
{
    if (key != swstart::kOptionKey) return false;
    g_kmSaw = true; g_kmValue = value;
    return true;
}

void StartModeOptionsEnd(bool complete, bool refusal)
{
    const long gen = (long)StoreLinkGen();
    if (complete && !refusal) { g_kmMapGen = gen; if (g_kmSaw) g_kmKeyGen = gen; }
    const int was = (g_kmLinkGen == gen) ? g_kmWorld : swstart::kFreeplay;   /* another link's world says nothing about this one */
    const int now = swstart::ModeFromMap(complete, refusal, g_kmSaw, g_kmValue, was);
    if (g_kmSaw && complete && !refusal) { int tmp = 0; if (!swstart::ParseMode(g_kmValue, &tmp)) ErrorLog("[KMODE] the world sent startmode '" + g_kmValue + "', which this build cannot read - treated as freeplay"); }
    if (now != was || g_kmLinkGen != gen)
        DebugLog(std::string("[KMODE] this world's start mode is ") + swstart::ModeName(now) + (now == swstart::kKenshi
                 ? " - a NEW character here gets a random Drifter start (any start picked in the NEW GAME window is replaced); a saved character loads as it is"
                 : " - a new character picks any start") + " (link " + NumS(gen) + ")");
    g_kmWorld = now; g_kmLinkGen = gen;
    if (complete && !refusal) DataModWorldJoined(now);
}

void StartModeDedInfo(unsigned startMode)
{
    if (g_kmLinkGen == (long)StoreLinkGen() && (int)startMode == g_kmWorld) return;
    DebugLog(std::string("[KMODE] DEDINFO says the start mode is ") + swstart::ModeName((int)startMode) + "; the OPTIONS map said "
             + swstart::ModeName(g_kmLinkGen == (long)StoreLinkGen() ? g_kmWorld : swstart::kFreeplay) + " (the OPTIONS map decides)");
}

int StartModeWorld() { return (g_kmLinkGen == (long)StoreLinkGen() && StoreLinkIsUp()) ? g_kmWorld : swstart::kFreeplay; }

bool StartModeNewGame(const void* picked, std::string* use)
{
    char pb[256];
    if (picked == 0 || !SehCopyStr(picked, pb, sizeof(pb))) std::strcpy(pb, "?");
    const bool linked = StoreLinkIsUp() && g_kmLinkGen == (long)StoreLinkGen();
    const int mode = linked ? g_kmWorld : swstart::kFreeplay;
    if (mode != swstart::kKenshi)
    {
        ++g_kmKept;
        g_kmLean = -1; g_kmLeanOwed = 2;   /* a new character with its own pick: no lean, and no stale key file in a reused folder */
        DebugLog(std::string("[KMODE] new game: the start '") + pb + "' stands (" + (linked ? std::string("this world's start mode is ") + swstart::ModeName(mode) : std::string("not linked to a world")) + ")");
        return false;
    }
    const int lean = swstart::PickLean(Seed());
    const std::string sid = swstart::kLeans[lean].startSid;
    int type = -1;
    const GameData* gd = SehFindRecord(&sid, &type);
    const bool loaded = gd != 0 && type == 64;   /* 64 = a START record (the type the engine's pump asks getData for) */
    const int act = swstart::NewGameDecide(mode, linked, loaded);
    if (act != swstart::kUseLean)
    {
        ++g_kmNotLoaded;
        g_kmLean = -1; g_kmLeanOwed = 2;
        ErrorLog(std::string("[KMODE] new game: start not enforced - the Drifter start ") + sid + " is " + (gd == 0 ? "not loaded" : "not a start record (type " + NumS(type) + ")")
                 + " in this game (" + swstart::kDataModFile + " is not switched on in Kenshi's mod list, or not installed); the start the player picked ('" + pb
                 + "') stands. notLoaded=" + NumS(g_kmNotLoaded));
        return false;
    }
    *use = sid;
    ++g_kmEnforced;
    g_kmLean = lean; g_kmLeanOwed = 1;   /* the footer names it; its key file goes beside this character's first save */
    DebugLog(std::string("[KMODE] new game: kenshi start mode - the picked start '") + pb + "' is replaced by " + swstart::kLeans[lean].name + " (" + sid
             + "), lean " + NumS(lean + 1) + " of " + NumS(swstart::kLeanCount) + " picked at random; enforced=" + NumS(g_kmEnforced));
    return true;
}

void StartModeTimeOption(const std::string& value)
{
    g_kmTimeGen = (long)StoreLinkGen();
    g_kmTimeOpt = value == "consensus" ? 1 : value == "fixed" ? 0 : -1;
}

void StartModeSaveWritten(const std::string& dir)
{
    if (g_kmLeanOwed == 0 || dir.empty()) return;
    const std::string path = dir + "\\" + swstart::kLeanFileName;
    if (g_kmLeanOwed == 2)
    {
        if (IsFile(path) && !U8DeleteFile(path.c_str())) { ErrorLog("[KMODE] lean file: " + path + " could NOT be removed (Windows error " + NumS((long long)::GetLastError()) + ") - tried again at the next save"); return; }
        g_kmLeanOwed = 0;
        return;
    }
    const std::string text = swstart::LeanFileText(g_kmLean);
    DWORD err = 0;
    if (text.empty()) { g_kmLeanOwed = 0; return; }
    if (!WriteReplace(path, text, &err)) { ErrorLog("[KMODE] lean file: " + path + " could NOT be written (Windows error " + NumS((long long)err) + ") - tried again at the next save"); return; }
    g_kmLeanOwed = 0;
    DebugLog(std::string("[KMODE] lean file written: ") + path + " (" + swstart::kLeans[g_kmLean].name + ")");
}

void StartModeSaveLoaded(const std::string& dir)
{
    g_kmLeanOwed = 0;
    g_kmLean = -1;
    if (dir.empty()) return;
    std::string text;
    const std::string path = dir + "\\" + swstart::kLeanFileName;
    if (!ReadAll(path, &text, 4096)) return;   /* an older character, or one never swapped: the footer says only Kenshi mode */
    g_kmLean = swstart::LeanFromFileText(text);
    DebugLog(std::string("[KMODE] lean file read: ") + path + " - " + (g_kmLean >= 0 ? swstart::kLeans[g_kmLean].name : "names no Drifter start of this build (unknown)"));
}

void StartModeFooter(std::vector<std::string>* labels, std::vector<std::string>* values)
{
    swstart::FooterFacts f;
    const long gen = (long)StoreLinkGen();
    int dedHere = 0, haveClock = 0, clockMode = 0;
    unsigned dedMode = 0;
    float speed = 1.0f;
    StoreFooterTimeFacts(&dedHere, &dedMode, &haveClock, &speed, &clockMode);
    f.linked = gen > 0 ? 1 : 0;
    f.dedInfoHere = dedHere;
    f.dedMode = dedMode;
    f.dedicated = (dedHere != 0 || g_kmKeyGen == gen) ? 1 : 0;
    f.timeOpt = g_kmTimeGen == gen && g_kmTimeOpt >= 0 ? g_kmTimeOpt : (haveClock != 0 ? clockMode : -1);
    f.haveSpeed = haveClock;
    f.speed = speed;
    f.startKnown = g_kmMapGen == gen ? 1 : 0;
    f.startMode = g_kmLinkGen == gen ? g_kmWorld : swstart::kFreeplay;
    f.lean = g_kmLean;
    const std::vector<swstart::FooterLine> lines = swstart::FooterLines(f);
    labels->clear(); values->clear();
    for (size_t k = 0; k < lines.size(); ++k) { labels->push_back(lines[k].label); values->push_back(lines[k].value); }
}

bool GameRecordLoaded(const std::string& sid)
{
    int type = -1;
    return SehFindRecord(&sid, &type) != 0;
}

bool DrifterStartsLoaded(std::string* why)
{
    for (int i = 0; i < swstart::kLeanCount; ++i)
    {
        const std::string sid = swstart::kLeans[i].startSid;
        int type = -1;
        const GameData* gd = SehFindRecord(&sid, &type);
        if (gd == 0 || type != 64)
        {
            if (why != 0) *why = std::string("the Drifter start ") + sid + " is " + (gd == 0 ? "not loaded" : "not a start record (type " + NumS(type) + ")") + " in this run";
            return false;
        }
    }
    return true;
}

void InstallStartModeData()
{
    DataModStateLoad();
    const std::string game = GameFolder();
    const std::string cfg = game.empty() ? std::string() : game + "\\data\\mods.cfg";
    std::string text;
    const bool cfgRead = !cfg.empty() && ReadAll(cfg, &text, (size_t)coopmods::kModsCfgMaxBytes);
    const int listed = cfgRead && coopmods::ModsCfgLists(text.data(), (long)text.size(), swstart::kDataModFile) ? 1 : 0;
    const int same = cfgRead && swstart::ModsCfgPrint(text) == g_dmState.cfgPrint ? 1 : 0;
    const int owned = swstart::DataModOwned(g_dmState.added, cfgRead ? 1 : 0, listed, same);
    if (g_dmState.added && !owned)
    {
        g_dmState.added = 0; g_dmState.cfgPrint = 0;
        DataModStateSave("harmless: the line is only left alone");
        if (listed) DebugLog("[KMODE] data mod: " + cfg + " was changed by something else since this plugin added " + swstart::kDataModFile
                             + " - the line is that list's own now and this plugin never takes it out (untick it in the launcher if it is not wanted)");
    }
    if (!cfgRead && !cfg.empty()) ErrorLog("[KMODE] data mod: Kenshi's mod list " + cfg + " could not be read at the start - nothing in it is changed this run");
    const int act = swstart::DataModDecide(g_dmState.lastWorld, owned, listed);
    if (act == swstart::kDataModInstall)
    {
        int added = 0; unsigned long long print = 0;
        g_dmOnThisRun = DataModInstall(&added, &print);
        if (added) { g_dmState.added = 1; g_dmState.cfgPrint = print; DataModStateSave("a later switch-off cannot tell that this plugin added the line"); }
        return;
    }
    g_dmOnThisRun = listed != 0;
    if (act == swstart::kDataModSwitchOff)
    {
        std::string updated;
        DWORD err = 0;
        if (swstart::ModsCfgWithoutLine(text, swstart::kDataModFile, &updated) > 0 && WriteReplace(cfg, updated, &err))
        {
            g_dmState.added = 0; g_dmState.cfgPrint = 0;
            DataModStateSave("the next launch may try the switch-off again");
            DebugLog("[KMODE] data mod: the last world joined was not kenshi mode - " + std::string(swstart::kDataModFile) + " (switched on by this plugin) is switched off in "
                     + cfg + " again; Kenshi read the list before this point, so it is off from the next launch");
        }
        else ErrorLog("[KMODE] data mod: " + cfg + " could NOT be written (Windows error " + NumS((long long)err) + ") - " + swstart::kDataModFile + " stays switched on; tried again at the next launch");
        return;
    }
    DebugLog(std::string("[KMODE] data mod: not switched on by this plugin - the last world this game joined was ")
             + (g_dmState.lastWorld == swstart::kFreeplay ? "not kenshi mode" : "none yet") + (listed ? " (a mods.cfg line put there by someone else is left as it is)" : "")
             + "; joining a kenshi-mode world installs it for the next launch");
}

}   /* namespace coop */
