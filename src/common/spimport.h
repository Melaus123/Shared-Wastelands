#pragma once
/* T-1096 (owner decisions 1224, 1225 a, 1226 a) - STARTING A NEW PROFILE FROM A SINGLE-PLAYER SAVE: the pure rules.
   A player making a new profile in a multiplayer world may pick START WITH "Import from single player"; the game then runs
   Kenshi's own IMPORT GAME from a single-player save (the world reset's road: store.cpp StoreResetCarryPost) in place of the
   NEW GAME window. The host decides who may (three options on GAME OPTIONS > MULTIPLAYER); the world server checks it where
   the profile is made (store_main.cpp OnProfiles, kind kReqNewImport), not only on the asking game.
   Everything here is pure - the offline suite walks it (test_main.cpp t_t1096_*). */
#include <string>
#include <vector>
#include <utility>
#include <cstdio>

namespace spimport {

/* ---- the host's options (the world server's options.txt; OptionValueOk) ---- */
const char* const kKeyMode     = "spimport";           /* IMPORT FROM SINGLE PLAYER: "all" / "owner" / "off" (absent = "all"; "host", the old word, is "owner") */
const char* const kKeyMoney    = "spimportmoney";      /* IMPORT INCLUDES MONEY: "yes" / "no" (absent = "yes") */
const char* const kKeyResearch = "spimportresearch";   /* IMPORT INCLUDES RESEARCH: "yes" / "no" (absent = "yes") */
enum Mode { kModeAll = 0, kModeOwner = 1, kModeOff = 2 };

inline bool IsOptionKey(const std::string& k) { return k == kKeyMode || k == kKeyMoney || k == kKeyResearch; }
inline bool OptionValueOk(const std::string& k, const std::string& v)
{
    if (k == kKeyMode) return v == "all" || v == "owner" || v == "host" || v == "off";
    if (k == kKeyMoney || k == kKeyResearch) return v == "yes" || v == "no";
    return false;
}
/* The mode an option value stands for: absent (or a value no build stores - OptionValueOk refuses it) is the default, everyone. */
inline int ModeOf(const std::string& v) { return (v == "owner" || v == "host") ? kModeOwner : v == "off" ? kModeOff : kModeAll; }
inline const char* ModeName(int m) { return m == kModeOwner ? "owner" : m == kModeOff ? "off" : "all"; }
/* the value stored for a mode value given: the old "host" is stored as "owner" (owner 1383) */
inline std::string ModeValue(const std::string& v) { return ModeName(ModeOf(v)); }
/* An "includes" option: only "no" turns it off (absent = yes). */
inline bool IncludesOf(const std::string& v) { return v != "no"; }
/* GAME OPTIONS: the two "includes" rows are greyed and cannot be changed while import is Not allowed. Their values are kept. */
inline bool IncludesRowLit(const std::string& modeValue) { return ModeOf(modeValue) != kModeOff; }

/* ---- who may import (the world server's check at the profile request; the screens show the same answer) ----
   The owner and the admins (logged in, or proven at their door - owner 1379) always may, unless import is Not allowed; anyone else
   only while it is Allowed for everyone. A player id never decides it. */
inline bool ImportAllowed(int mode, bool isStaff)
{
    if (mode == kModeOff) return false;
    return isStaff || mode == kModeAll;
}

/* ---- the switch word Kenshi's import is posted with (worldreset.h): always the crew (the screen's own 0x02) and the fixed import
   spot (0x01, decision 1226 a); research (0x08) only while the host includes it; never buildings (0x04), dead / named characters'
   save state (0x10 - the shared world's own unique-character table decides) or faction relations (0x20). ---- */
/* worldreset.h's kImportScreenBit / kImportResetPositions / kImportResearch, repeated so this header needs no other (the suite checks they agree) */
const unsigned int kSwitchFixedSpot = 0x01u, kSwitchCrew = 0x02u, kSwitchResearch = 0x08u;
inline unsigned int ImportFlags(bool research)
{
    return kSwitchCrew | kSwitchFixedSpot | (research ? kSwitchResearch : 0u);
}

/* ---- the save picker's list: SINGLE-PLAYER saves only (design read: a folder with quick.save and no multiplayer-world.key). A
   folder that carries the mod's multiplayer.mark is a multiplayer save too and is left out as well. ---- */
inline bool IsSinglePlayerSave(bool isFolder, bool hasQuickSave, bool hasWorldKey, bool hasMpMark)
{
    return isFolder && hasQuickSave && !hasWorldKey && !hasMpMark;
}
struct SaveSeen
{
    std::string name; bool isFolder, quickSave, worldKey, mpMark; unsigned long long saveTime;   /* saveTime: quick.save's last write (FILETIME), 0 = none */
    SaveSeen() : isFolder(false), quickSave(false), worldKey(false), mpMark(false), saveTime(0ULL) {}
};
/* The picker's rows: the single-player saves, newest LAST PLAYED first, then by name. */
inline std::vector<SaveSeen> SinglePlayerSaves(const std::vector<SaveSeen>& all)
{
    std::vector<SaveSeen> out;
    for (size_t i = 0; i < all.size(); ++i)
        if (IsSinglePlayerSave(all[i].isFolder, all[i].quickSave, all[i].worldKey, all[i].mpMark)) out.push_back(all[i]);
    for (size_t i = 1; i < out.size(); ++i)   /* insertion sort: a short list */
        for (size_t j = i; j > 0; --j)
        {
            const SaveSeen& a = out[j - 1]; const SaveSeen& b = out[j];
            const bool swap = a.saveTime < b.saveTime || (a.saveTime == b.saveTime && a.name > b.name);
            if (!swap) break;
            const SaveSeen t = out[j - 1]; out[j - 1] = out[j]; out[j] = t;
        }
    return out;
}
/* A folder name the import may read: one plain folder name under the save root (no path, no dot names). */
inline bool FolderNameOk(const std::string& f)
{
    if (f.empty() || f == "." || f == ".." || f.size() > 200) return false;
    for (size_t i = 0; i < f.size(); ++i) { const char c = f[i]; if (c == '\\' || c == '/' || c == ':' || (unsigned char)c < 32) return false; }
    return true;
}

/* ---- money (decision 1225 a): Kenshi's import always carries the save's money; with money not included the purse is set, after
   the import's first frame, to Kenshi's own new-game amount. Keep = leave the imported purse; Set = write *setTo; Unknown = money
   is off but the new-game amount could not be read - the purse is left as imported and the game says so in its log. ---- */
enum MoneyStep { kMoneyKeep = 0, kMoneySet = 1, kMoneyUnknown = 2 };
inline int MoneyOffDecide(bool includeMoney, bool startAmountKnown, int startAmount, int* setTo)
{
    if (includeMoney) return kMoneyKeep;
    if (!startAmountKnown || startAmount < 0) return kMoneyUnknown;
    if (setTo != 0) *setTo = startAmount;
    return kMoneySet;
}
inline const char* MoneyStepName(int s) { return s == kMoneySet ? "set" : s == kMoneyUnknown ? "unknown" : "keep"; }
/* T-1096 part 2 (owner 1232 a): the new-game amount is THE WANDERER start's own "money" field, read from the game's data at run time
   (Kenshi's NEW GAME takes the purse from the chosen start's "money", decomp_871f30.txt:821-823) - never a typed-in number. The game's
   records whose STRING ID is kWandererStartSid are read (store.cpp SpStartCandsPod): one carrying an int field "money" (or several that
   agree) names the amount; none -> not found; two that disagree -> not decided (both: the purse stays as imported and the log says why).
   T-1096 fold (review 2026-10-31): by the record's string id, never by its shown name (a name changes with the game's language). Read
   from data/gamedata.base: the game-start record (type 64) named "Wanderer", string id "1980-gamedata.base", int field money = 1000. */
const char* const kWandererStartSid = "1980-gamedata.base";
struct StartCand
{
    int hasMoney, money;   /* the record carries an int field "money", and its value */
    StartCand() : hasMoney(0), money(0) {}
};
enum WandererMoney { kWandererNone = 0, kWandererFound = 1, kWandererMixed = 2 };
inline int StartMoneyFrom(const std::vector<StartCand>& c, int* amount)
{
    int found = 0, v = 0;
    for (size_t i = 0; i < c.size(); ++i)
    {
        if (c[i].hasMoney == 0 || c[i].money < 0) continue;
        if (found != 0 && c[i].money != v) return kWandererMixed;
        found = 1; v = c[i].money;
    }
    if (found == 0) return kWandererNone;
    if (amount != 0) *amount = v;
    return kWandererFound;
}

/* ---- the imported squads' record ids (design read risk): Kenshi names a player's squads after the save's faction, so a save whose
   faction has the name another player's faction has (or had) would name squads that player's records already hold (SquadIdPlan
   below gives such a squad a free id before the first save). ---- */
/* A record id no other record holds: `base` itself when free, else base + "_i" + n for the lowest n >= 1 that is free. */
inline std::string UniqueSquadId(const std::string& base, const std::vector<std::string>& taken)
{
    struct L { static bool Has(const std::vector<std::string>& t, const std::string& s) { for (size_t i = 0; i < t.size(); ++i) if (t[i] == s) return true; return false; } };
    if (!L::Has(taken, base)) return base;
    for (unsigned n = 1; n < 100000u; ++n)
    {
        char b[16]; std::sprintf(b, "_i%u", n);
        const std::string c = base + b;
        if (!L::Has(taken, c)) return c;
    }
    return std::string();
}

/* T-1096 part 2: THE IMPORTED SQUADS' IDS MADE UNIQUE AT THE IMPORT (the profile is new: nothing refers to them yet). taken: every id a
   world record holds (any owner, gone ones too) and every other squad's id in this game. For each imported id: "" = keep it (nothing
   taken holds it); else the lowest free '<id>_i<n>' (UniqueSquadId) that no taken id, no imported id and no id given earlier in this plan
   holds - an id the engine's own naming never makes (the text after its last '_' is not all digits, copysquadid.h EngineCanMint), so no
   later squad of any faction can be minted onto it; the imported id itself = no free id was found (the caller holds the save). */
inline std::vector<std::string> SquadIdPlan(const std::vector<std::string>& imported, const std::vector<std::string>& taken)
{
    std::vector<std::string> out(imported.size());
    std::vector<std::string> t(taken);
    t.insert(t.end(), imported.begin(), imported.end());
    for (size_t i = 0; i < imported.size(); ++i)
    {
        bool held = false;
        for (size_t k = 0; k < taken.size(); ++k) if (taken[k] == imported[i]) { held = true; break; }
        if (!held) continue;
        const std::string f = UniqueSquadId(imported[i], t);
        out[i] = f.empty() ? imported[i] : f;
        if (!f.empty()) t.push_back(f);
    }
    return out;
}

/* ---- T-1096 (owner 1234 e, 2026-10-31): LOOK-ALIKE COPIES OF IMPORTED NAMED CHARACTERS. Every named (unique) character in an
   imported crew comes along as his OWN look-alike: a copy of his template record with 'unique' off and its own string id (same name,
   look, skills, gear), beside the world's own named character and other importers' copies. Nobody is left out; the copy is not a
   named character anywhere (quest / named-capture rules do not apply to it). The copy's id is '<original id>~i<importer's slot>' - one
   per importer, the same player re-importing gets the same id (identical content); the original's id is inside it, so any game that
   meets the copy id (a SPAWN, a later load) makes the copy from the original it names - never by the shown name. ---- */
const char* const kCopyMark = "~i";
const unsigned int kCopySidMax = 95u;   /* templatekey::kMaxSid (factionkey.h) - repeated so this header needs no other (the suite checks they agree) */
/* a copy id: '~i' then one or more digits at the very end, with a non-empty original id before it */
inline bool IsCopySid(const std::string& s)
{
    const size_t at = s.rfind(kCopyMark);
    if (at == std::string::npos || at == 0 || at + 2 >= s.size()) return false;
    for (size_t i = at + 2; i < s.size(); ++i) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}
/* the original's id inside a copy id ("" = not a copy id) */
inline std::string OrigOfCopySid(const std::string& s) { return IsCopySid(s) ? s.substr(0, s.rfind(kCopyMark)) : std::string(); }
/* the copy id for an importer's slot: "" = none can be made (no original, no slot, the original is itself a copy, or the id would be
   longer than kCopySidMax bytes - the import is then held, as the squad-id rename's no-free-id case is) */
inline std::string CopySid(const std::string& origSid, int slot)
{
    if (origSid.empty() || slot < 0 || IsCopySid(origSid)) return std::string();
    char b[24]; std::sprintf(b, "~i%d", slot);
    const std::string c = origSid + b;
    return c.size() > kCopySidMax ? std::string() : c;
}
/* what one member's template reference asks for: while the import reads its own squad files, a template flagged unique (1; 0 = not,
   -1 = unreadable - left as it is) -> made into a copy and the reference pointed at it (kLaSwap, the import's squad-file pass); a copy
   id -> its copy must exist before the member is built (kLaEnsure - made at the top of the world load from the world's list,
   lookalikelist.h PreMakeIds, or at the member's build, BuildStepFor); anything else -> left alone. */
enum LookalikeStep { kLaNone = 0, kLaSwap = 1, kLaEnsure = 2 };
inline int LookalikeStepFor(bool importFile, int unique, bool refIsCopy)
{
    if (refIsCopy) return kLaEnsure;
    if (importFile && unique == 1) return kLaSwap;
    return kLaNone;
}
/* a squad file: '<save folder>/platoon/<name>.platoon' ('/' or '\', any case). *folderDir = the path of the save folder, *folderName =
   its last part. The import's swap gate (ImportSquadFileCheck) reads it. */
inline bool PlatoonFileOf(const std::string& path, std::string* folderDir, std::string* folderName)
{
    struct L
    {
        static char Lo(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
        static bool EqNoCase(const std::string& a, const char* b) { size_t i = 0; for (; i < a.size() && b[i] != 0; ++i) if (Lo(a[i]) != Lo(b[i])) return false; return i == a.size() && b[i] == 0; }
        static size_t LastSep(const std::string& s, size_t before) { for (size_t i = before; i > 0; --i) if (s[i - 1] == '/' || s[i - 1] == '\\') return i - 1; return std::string::npos; }
    };
    const char* const ext = ".platoon";
    if (path.size() <= 8 || !L::EqNoCase(path.substr(path.size() - 8), ext)) return false;
    const size_t s1 = L::LastSep(path, path.size());
    if (s1 == std::string::npos || s1 + 1 >= path.size() - 8) return false;
    const size_t s2 = L::LastSep(path, s1);
    if (s2 == std::string::npos || !L::EqNoCase(path.substr(s2 + 1, s1 - s2 - 1), "platoon")) return false;
    const size_t s3 = L::LastSep(path, s2);
    const std::string name = path.substr(s3 == std::string::npos ? 0 : s3 + 1, s2 - (s3 == std::string::npos ? 0 : s3 + 1));
    if (name.empty()) return false;
    if (folderDir != 0) *folderDir = path.substr(0, s2);
    if (folderName != 0) *folderName = name;
    return true;
}
/* a path made comparable: '\' read as '/', a run of separators as one, no separator at the end, any case */
inline std::string PathKey(const std::string& p)
{
    std::string o; o.reserve(p.size());
    for (size_t i = 0; i < p.size(); ++i)
    {
        char c = p[i];
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c == '/' && !o.empty() && o[o.size() - 1] == '/') continue;
        o.push_back(c);
    }
    while (o.size() > 1 && o[o.size() - 1] == '/') o.erase(o.size() - 1);
    return o;
}
/* THE IMPORT'S SWAP GATE: a squad file read while the import window is open is the imported crew's own copy only when BOTH hold -
   it lies in the platoon folder of the engine's working folder for the game in progress (workFolder: SaveFileSystem +0x148,
   '<save root>/_currentN/', decomp_470e40.txt; IMPORT GAME copies each '<source save>/platoon/<file>' to '<working folder>/platoon/<file>'
   and reads it there, decomp_3788f0.txt:428-461 via decomp_471970.txt) AND its file name is one of the source save's squad files
   (sourceFiles, read from the source save's platoon folder at the import's start). A squad saved during play also lands in the working
   folder, so the folder alone is not enough. Pure: the same file read again gets the same answer. */
enum ImportSquadFile { kIsfOurs = 0, kIsfNotSquadFile = 1, kIsfNoWorkFolder = 2, kIsfOtherFolder = 3, kIsfNoSourceList = 4, kIsfNotInSource = 5 };
inline int ImportSquadFileCheck(const std::string& path, const std::string& workFolder, const std::vector<std::string>& sourceFiles)
{
    std::string dir;
    if (!PlatoonFileOf(path, &dir, 0)) return kIsfNotSquadFile;
    const std::string wk = PathKey(workFolder);
    if (wk.empty()) return kIsfNoWorkFolder;
    if (PathKey(dir) != wk) return kIsfOtherFolder;
    if (sourceFiles.empty()) return kIsfNoSourceList;
    const size_t s = path.find_last_of("/\\");
    const std::string fk = PathKey(path.substr(s + 1));
    for (size_t i = 0; i < sourceFiles.size(); ++i) if (PathKey(sourceFiles[i]) == fk) return kIsfOurs;
    return kIsfNotInSource;
}
inline const char* ImportSquadFileWhy(int v)
{
    switch (v)
    {
    case kIsfOurs:         return "one of the source save's squad files in the engine's working folder";
    case kIsfNotSquadFile: return "not a squad file";
    case kIsfNoWorkFolder: return "the engine's working folder could not be read (or is empty)";
    case kIsfOtherFolder:  return "not in the engine's working folder";
    case kIsfNoSourceList: return "the source save's squad file list is empty (its platoon folder was not read or holds none)";
    case kIsfNotInSource:  return "in the engine's working folder but not one of the source save's squad files (a squad saved during play)";
    }
    return "?";
}
/* ---- the screens' words (decision 1224 - the approved page, word for word) ---- */
const char* const kStartWithLabel   = "START WITH";
const char* const kStartNew         = "A new character";
const char* const kStartImport      = "Import from single player";
const char* const kHintNext         = "Next, you'll choose the single-player save to import from.";
const char* const kHintNotAllowed   = "The host hasn't allowed importing from single player in this world.";
const char* const kBtnNext          = "NEXT";
const char* const kBtnCreate        = "CREATE";
const char* const kPickTitle        = "IMPORT FROM SINGLE PLAYER";
const char* const kPickText         = "Choose the single-player save to import from.";
const char* const kPickColSave      = "SAVE";
const char* const kPickColPlayed    = "LAST PLAYED";
const char* const kPickNone         = "No single-player saves found.";
const char* const kComesAlong       = "COMES ALONG";
const char* const kStaysBehind      = "STAYS BEHIND";
const char* const kAlongCrew        = "Your characters and their gear";
const char* const kAlongAnimals     = "Your animals";
const char* const kAlongMoney       = "Your money";
const char* const kAlongResearch    = "Your research";
const char* const kBehindBuildings  = "Buildings and outposts";
const char* const kBehindRelations  = "Faction relations";
const char* const kBehindDead       = "Characters who died";
const char* const kBehindMoney      = "Money (you start with the usual new-game amount)";
const char* const kBehindResearch   = "Research (you start fresh)";
const char* const kArrival          = "Your imported characters arrive together at the starting spot for imports.";
const char* const kBtnCancel        = "CANCEL";
const char* const kBtnImport        = "IMPORT";
/* The two boxes' lines with the host's money / research options: an option turned off moves its line to STAYS BEHIND. */
inline void BoxLines(bool money, bool research, std::vector<std::string>* along, std::vector<std::string>* behind)
{
    along->clear(); behind->clear();
    along->push_back(kAlongCrew); along->push_back(kAlongAnimals);
    if (money) along->push_back(kAlongMoney);
    if (research) along->push_back(kAlongResearch);
    behind->push_back(kBehindBuildings); behind->push_back(kBehindRelations); behind->push_back(kBehindDead);
    if (!money) behind->push_back(kBehindMoney);
    if (!research) behind->push_back(kBehindResearch);
}
/* NEW PROFILE's START WITH row: whether import may be picked (else the row is fixed on "A new character", arrows greyed), the
   hint under it ("" = none) and the right button's word. */
inline const char* StartHint(bool importPicked, bool allowed)
{
    if (!allowed) return kHintNotAllowed;
    return importPicked ? kHintNext : "";
}
inline const char* StartButton(bool importPicked, bool allowed) { return (importPicked && allowed) ? kBtnNext : kBtnCreate; }
/* owner 1233 (2026-10-31): the HOST's NEW PROFILE (HOST GAME's CHANGE, or the host's own game while its world runs) with import Not
   allowed shows NO hint line - the greyed arrows say it (the row height as with no hint); a joining player keeps kHintNotAllowed. */
inline const char* StartHintAs(bool importPicked, bool allowed, bool isHost)
{
    if (!allowed && isHost) return "";
    return StartHint(importPicked, allowed);
}

/* ---- the PROFILES answer's import word (store protocol 129): bit 0 = this asker may import, bit 1 = money included, bit 2 =
   research included, bit 3 = the word is present (an older world sends none: import is then not offered). ---- */
const unsigned int kWordAllowed = 1u, kWordMoney = 2u, kWordResearch = 4u, kWordPresent = 8u;
inline unsigned int AnswerWord(int mode, bool isHost, bool money, bool research)
{
    return kWordPresent | (ImportAllowed(mode, isHost) ? kWordAllowed : 0u) | (money ? kWordMoney : 0u) | (research ? kWordResearch : 0u);
}

/* ---- T-1096 part 3: NEW PROFILE's START WITH row - whether import may be picked here and the host's money / research options, from
   what this game knows: a running world's PROFILES answer word (a joiner, or the host while its world runs), or - on HOST GAME's
   CHANGE, the world not running - the world's own options.txt values (the host always asks as the host). ---- */
struct StartRowState
{
    bool allowed, money, research;
    StartRowState() : allowed(false), money(true), research(true) {}
};
inline StartRowState StartRowFromWord(unsigned word)
{
    StartRowState s;
    if ((word & kWordPresent) == 0) return s;   /* an older world sends no word: import is not offered */
    s.allowed = (word & kWordAllowed) != 0; s.money = (word & kWordMoney) != 0; s.research = (word & kWordResearch) != 0;
    return s;
}
inline StartRowState StartRowFromOptions(const std::string& mode, const std::string& money, const std::string& research)
{
    StartRowState s;
    s.allowed = ImportAllowed(ModeOf(mode), true); s.money = IncludesOf(money); s.research = IncludesOf(research);
    return s;
}
/* The row's two values: either arrow switches between them while import may be picked; otherwise the row stays on "A new character"
   with both arrows greyed (the approved page). */
inline bool StartImportPicked(bool picked, bool allowed) { return picked && allowed; }
inline const char* StartValue(bool picked, bool allowed) { return StartImportPicked(picked, allowed) ? kStartImport : kStartNew; }
inline bool StartArrowsLit(bool allowed) { return allowed; }
inline bool StartStep(bool picked, bool allowed) { return allowed ? !picked : false; }

/* ---- T-1096 fold (review 2026-10-31): THE ANSWER TO THIS GAME'S IMPORT REQUEST. The world server answers every NEW (kind 1) and
   NEW with import (kind 7) with one answer of kind NEW, in order, on the connection it came on (store_main.cpp OnProfiles), and that
   answer does not say which kind it answers. So the n-th NEW answer on a connection answers its n-th NEW request: the import's answer
   is the one whose count is the import request's own - never one matched by the profile's name. Reset with each connection. ---- */
struct AnswerTie
{
    unsigned newSent, newAnswered, importAt;   /* NEW requests sent / NEW answers read on this connection; the import's count (0 = none owed) */
    AnswerTie() : newSent(0), newAnswered(0), importAt(0) {}
};
inline void TieReset(AnswerTie* t) { t->newSent = 0; t->newAnswered = 0; t->importAt = 0; }
/* a request went out: isNew = kind 1, isImport = kind 7 (any other kind is answered otherwise, or not at all) */
inline void TieSent(AnswerTie* t, bool isNew, bool isImport)
{
    if (!isNew && !isImport) return;
    ++t->newSent;
    if (isImport) t->importAt = t->newSent;
}
/* an answer was read: 1 = it is the answer to the import request (the tie is then spent), 0 = not */
inline int TieAnswer(AnswerTie* t, bool isNewAnswer)
{
    if (!isNewAnswer || t->newAnswered >= t->newSent) return 0;   /* an answer with no request outstanding on this connection counts for nothing */
    ++t->newAnswered;
    if (t->importAt == 0 || t->newAnswered != t->importAt) return 0;
    t->importAt = 0;
    return 1;
}
/* money / research for THIS import: the answer word of the world that made its profile (or, until that answer, the word known when it
   was asked); with none, the world's OPTIONS map (optSeen) on this connection; else included. */
inline bool ImportIncludes(unsigned importWord, unsigned bit, bool optSeen, bool optValue)
{
    if ((importWord & kWordPresent) != 0) return (importWord & bit) != 0;
    if (optSeen) return optValue;
    return true;
}

}   /* namespace spimport */
