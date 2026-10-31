#pragma once
/* startmode.h - DEDICATED: THE WORLD'S START MODE (<world>/dedicated.txt  startmode=freeplay|kenshi). Pure; swept offline by coop-test.
 *
 *   freeplay  every new character picks any start on Kenshi's own start list (as before; the default).
 *   kenshi    every NEW character in this world gets one of the six Drifter starts, picked uniformly at random, whatever start the
 *             player clicked. Race and appearance stay the player's own (the character editor follows as usual); the start's town is
 *             blank, so Kenshi places the character in a random town. A returning player loads their own save and is never touched.
 *
 * HOW A GAME LEARNS IT: the notebook adds the pair  startmode=<mode>  to every OPTIONS map it sends under --dedicated (the WELCOME's map
 * arrives before any profile pick, so before the NEW GAME window). It is never stored in options.txt and no game may set it. DEDINFO
 * carries it too (in the world only - too late for the start, shown for the record).
 *
 * WHERE IT ACTS: the pre-hook on SaveManager::newGame (src/coop-plugin/settings.cpp detour_smNewGame). Kenshi's NEW GAME window passes
 * the chosen start record's string id there (newGameStart: starts[index]->stringID); in kenshi mode the plugin passes one of the six
 * start ids below instead, after checking the record is loaded (dedicatedserver.mod switched on). Not loaded = the player's pick stands.
 *
 * THE DATA: dedicatedserver.mod (built by mod-package/dedicatedserver/build_dedicatedserver.py, which checks its start ids against this
 * table). Kenshi registers ONE .mod per mod folder, so it cannot sit beside "Shared Wastelands.mod": it ships in the subfolder
 * dedicatedserver\ of the mod folder, and the plugin copies it to <Kenshi>\mods\dedicatedserver\ and switches it on in data\mods.cfg -
 * only while the last world this game joined was a kenshi-mode world (DataModDecide, below; it takes effect from the next launch).
 *
 * C++03 (VS2010). No library calls beyond <string> and <vector>.
 */
#include <string>
#include <vector>
#include "modsenabled.h"

namespace swstart {

enum Mode { kFreeplay = 0, kKenshi = 1 };

const char* const kOptionKey    = "startmode";            /* the OPTIONS map key (and the dedicated.txt key) */
const char* const kDataModFile  = "dedicatedserver.mod";  /* the data mod's file name: its mods.cfg line */
const char* const kDataModDir   = "dedicatedserver";      /* its folder: beside the plugin in the package, and under <Kenshi>\mods */

inline const char* ModeName(int m) { return m == kKenshi ? "kenshi" : "freeplay"; }

/* dedicated.txt and the OPTIONS value: exactly "freeplay" or "kenshi" (lower case). */
inline bool ParseMode(const std::string& v, int* out)
{
    if (v == "freeplay") { *out = kFreeplay; return true; }
    if (v == "kenshi")   { *out = kKenshi;   return true; }
    return false;
}

/* THE SIX LEANS. Every Drifter has 15 strength, toughness, dexterity and athletics and 5 in every other skill; a lean adds +5 to its
   skills. The start ids are dedicatedserver.mod's start records (type 64); build_dedicatedserver.py refuses to build if they differ. */
struct Lean { const char* name; const char* startSid; };
enum { kLeanCount = 6 };
static const Lean kLeans[kLeanCount] = {
    { "Drifter (Fighter)",    "7200041-dedicatedserver.mod" },   /* melee attack, melee defence, dodge */
    { "Drifter (Scout)",      "7200042-dedicatedserver.mod" },   /* athletics, swimming */
    { "Drifter (Assassin)",   "7200043-dedicatedserver.mod" },   /* stealth, assassination */
    { "Drifter (Labourer)",   "7200044-dedicatedserver.mod" },   /* labouring, strength */
    { "Drifter (Medic)",      "7200045-dedicatedserver.mod" },   /* field medic, cooking */
    { "Drifter (Researcher)", "7200046-dedicatedserver.mod" }    /* science, engineering */
};

/* splitmix64: spreads a weak seed (a tick count, a counter) over all 64 bits before the lean is taken from it. */
inline unsigned long long Mix64(unsigned long long x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}
/* Uniform over the six (the modulo bias of 2^64 % 6 is far below anything a player could notice). */
inline int PickLean(unsigned long long seed) { return (int)(Mix64(seed) % (unsigned long long)kLeanCount); }

/* One OPTIONS map ends: the world's mode. `complete` false (a map cut short) or a refusal reply keeps `previous`; a whole map without
   the key is a world that is not dedicated (or an older notebook) = freeplay; a value this build cannot read = freeplay (the player's
   own pick stands - never a forced start on a word we do not understand). */
inline int ModeFromMap(bool complete, bool refusal, bool sawKey, const std::string& value, int previous)
{
    if (!complete || refusal) return previous;
    int m = kFreeplay;
    if (sawKey && ParseMode(value, &m)) return m;
    return kFreeplay;
}

/* At SaveManager::newGame: what to do with the start the player picked. */
enum NewGameAction { kKeepPick = 0, kUseLean = 1, kLeanNotLoaded = 2 };
inline int NewGameDecide(int worldMode, bool linkedToThatWorld, bool leanLoaded)
{
    if (worldMode != kKenshi || !linkedToThatWorld) return kKeepPick;
    return leanLoaded ? kUseLean : kLeanNotLoaded;
}

/* Kenshi's mod list with the data mod switched on. 0 = already a line (nothing to write); 1 = *out holds the list with the line added
   at the end, in the list's own line ending (CRLF when it has none yet - the launcher writes CRLF); a last line without an ending gets
   one first. The comparison is coopmods::ModsCfgLists (ASCII case ignored, spaces around a line ignored). */
inline int ModsCfgWithLine(const std::string& text, const char* modFile, std::string* out)
{
    if (coopmods::ModsCfgLists(text.data(), (long)text.size(), modFile)) return 0;
    std::string eol = "\r\n";
    const size_t lf = text.find('\n');
    if (lf != std::string::npos && (lf == 0 || text[lf - 1] != '\r')) eol = "\n";
    else if (lf == std::string::npos && text.find('\r') != std::string::npos) eol = "\r";
    *out = text;
    if (!out->empty() && (*out)[out->size() - 1] != '\n' && (*out)[out->size() - 1] != '\r') *out += eol;
    *out += modFile;
    *out += eol;
    return 1;
}

/* Kenshi's mod list with every line that IS modFile taken out (the comparison of coopmods::ModsCfgLists), each with its own line
   ending; every other byte (a byte-order mark, the other lines, their endings) kept as it was. 0 = no such line (*out untouched);
   n > 0 = n lines removed and *out holds the new list. */
inline int ModsCfgWithoutLine(const std::string& text, const char* modFile, std::string* out)
{
    if (!coopmods::ModsCfgLists(text.data(), (long)text.size(), modFile)) return 0;
    const size_t n = text.size();
    std::string r;
    int removed = 0;
    size_t i = 0;
    if (n >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) { r.append(text, 0, 3); i = 3; }
    while (i < n)
    {
        size_t end = i;
        while (end < n && text[end] != '\n' && text[end] != '\r') ++end;
        size_t next = end;
        if (next < n && text[next] == '\r') { ++next; if (next < n && text[next] == '\n') ++next; }
        else if (next < n) ++next;
        if (end > i && coopmods::ModsCfgLists(text.data() + i, (long)(end - i), modFile)) ++removed;
        else r.append(text, i, next - i);
        i = next;
    }
    *out = r;
    return removed;
}

/* ---- WHEN THE DATA MOD IS SWITCHED ON (kmode.cpp InstallStartModeData) ----
 * Kenshi reads data\mods.cfg once, when it loads its data, which is before any world link exists. So the plugin cannot know at that
 * point whether this game is about to join a kenshi-mode world. It goes by the LAST world this game joined instead, remembered in a
 * small state file beside mods.cfg (<Kenshi>\data\kDataModStateFile, so a mod update that replaces the plugin's own folder does not
 * lose it), written when a world's OPTIONS map is complete:
 *   the last world was kenshi mode   -> dedicatedserver.mod copied to <Kenshi>\mods\dedicatedserver\ and switched on;
 *   anything else (freeplay, a hosted world, never joined one) -> left alone, EXCEPT that a mods.cfg line this plugin added itself
 *                                       is taken out again (a line someone else put there - a player, a server owner's own set-up -
 *                                       is never touched).
 * WHICH LINE IS "OURS" (DataModOwned): Kenshi's mods.cfg is one mod file name per line and nothing else, so the line itself cannot carry
 * a mark. Instead the state remembers a fingerprint of the whole mods.cfg as this plugin wrote it (ModsCfgPrint). The line stays ours
 * only while mods.cfg is byte for byte what the plugin left. Once anything else rewrites the list - the launcher after a change, a
 * player, a server owner's install script that writes its own dedicatedserver.mod line - the line is taken as that list's own and is
 * never removed; the plugin says so once in its log. A mods.cfg it cannot read changes nothing (ownership is kept for the next start).
 * THE LIMITATION, plainly: the first time a game joins a kenshi-mode world the Drifter starts are not loaded yet in that run (the
 * plugin installs the mod right then, and it takes effect from the next launch); leaving kenshi mode for another world switches the
 * mod off from the launch after that. Single player and hosted play never get it unless the last world joined was kenshi mode, and
 * while it was, the mod stays on in single player too. */
const char* const kDataModStateFile = "dedicatedserver-state.txt";   /* in <Kenshi>\data, beside mods.cfg */
struct DataModState
{
    int lastWorld;   /* -1 none known, kFreeplay or kKenshi: the start mode of the last world whose OPTIONS map arrived whole */
    int added;       /* 1 = the dedicatedserver.mod line in mods.cfg was added by this plugin (so it may take it out again) */
    unsigned long long cfgPrint;   /* ModsCfgPrint of mods.cfg exactly as this plugin left it after adding the line; 0 = none */
    DataModState() : lastWorld(-1), added(0), cfgPrint(0) {}
};
/* A fingerprint of Kenshi's whole mod list (64-bit FNV-1a over its bytes; never 0, so 0 can mean "none"). */
inline unsigned long long ModsCfgPrint(const std::string& text)
{
    unsigned long long h = 1469598103934665603ULL;
    for (size_t i = 0; i < text.size(); ++i) { h ^= (unsigned char)text[i]; h *= 1099511628211ULL; }
    return h != 0 ? h : 1;
}
/* Whether the line is still this plugin's at a start. added = the state says this plugin added it; cfgRead = mods.cfg was read;
   listed = it has the line now; sameAsLeft = it is byte for byte what the plugin left (ModsCfgPrint == cfgPrint).
   1 = ours (it may be taken out); 0 = not ours, or no longer. */
inline int DataModOwned(int added, int cfgRead, int listed, int sameAsLeft)
{
    if (added == 0) return 0;
    if (cfgRead == 0) return 1;     /* not read this time (locked, too big): nothing is known to have changed - kept for the next start */
    if (listed == 0) return 0;      /* the line is gone: nothing of ours is left on */
    return sameAsLeft != 0 ? 1 : 0;   /* rewritten by someone else since: the line is that list's own now */
}
inline std::string DataModStateText(const DataModState& s)
{
    std::string t = std::string("lastworld=") + (s.lastWorld == kKenshi ? "kenshi" : s.lastWorld == kFreeplay ? "freeplay" : "none") + "\r\n"
                    + "added=" + (s.added ? "1" : "0") + "\r\n";
    if (s.added && s.cfgPrint != 0)
    {
        static const char kHex[] = "0123456789abcdef";
        t += "cfg=";
        for (int i = 15; i >= 0; --i) t += kHex[(s.cfgPrint >> (4 * i)) & 0xF];
        t += "\r\n";
    }
    return t;
}
/* Lines "key=value"; anything else, or a value it cannot read, is ignored (the defaults stand: nothing known, nothing added). */
inline DataModState DataModStateParse(const std::string& text)
{
    DataModState s;
    size_t i = 0;
    while (i < text.size())
    {
        size_t e = text.find_first_of("\r\n", i);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(i, e - i);
        if (line == "lastworld=kenshi") s.lastWorld = kKenshi;
        else if (line == "lastworld=freeplay") s.lastWorld = kFreeplay;
        else if (line == "lastworld=none") s.lastWorld = -1;
        else if (line == "added=1") s.added = 1;
        else if (line == "added=0") s.added = 0;
        else if (line.size() == 20 && line.compare(0, 4, "cfg=") == 0)
        {
            unsigned long long v = 0; bool ok = true;
            for (size_t k = 4; k < 20 && ok; ++k)
            {
                const char c = line[k];
                const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
                if (d < 0) ok = false; else v = (v << 4) | (unsigned long long)d;
            }
            if (ok) s.cfgPrint = v;
        }
        i = e + 1;
    }
    return s;
}
enum DataModAction { kDataModLeave = 0, kDataModInstall = 1, kDataModSwitchOff = 2 };
/* At the plugin's start: owned = DataModOwned's answer; `listed` = mods.cfg was read and has the line now. */
inline int DataModDecide(int lastWorld, int owned, int listed)
{
    if (lastWorld == kKenshi) return kDataModInstall;
    if (owned != 0 && listed != 0) return kDataModSwitchOff;
    return kDataModLeave;
}

/* ---- SKIPPING THE NEW GAME WINDOW (kenshi mode; ui.cpp KmSkipTick) ----
 * In a kenshi-mode world the start chooser decides nothing (the newGame pre-hook replaces the pick), so the window the autoload
 * opens for a new character is pressed through: its own BEGIN, one title frame after it opened, and the pre-hook swaps the start.
 * RE (1.0.68 kenshi_x64.exe, read-only): NewGameWindow's ctor 0x917530 fetches "BeginButton" (0x917A68) and adds a delegate on
 * its eventMouseButtonClick (widget+0x148) to the thunk 0x481D0 -> 0x917400, newGameStart(this) - the button's only handler.
 * newGameStart hides the window (+0x38) and its Advanced window (+0x100), raises the loading screen, plays the button sound and
 * tail-calls SaveManager::newGame(starts[this+0xD0]->stringID) with no other check. The same ctor fills the start list (0x913B70)
 * and shows starts[this+0xD0] (0x916620, which reads it unchecked), and the < > arrows keep the index modulo the count, so a
 * window that was built holds a valid pick. Firing the button's own delegate is the click a mouse makes: no engine address.
 *
 * Armed only by the autoload's NEW GAME verdict in a kenshi-mode world it is linked to (the same condition as the [KMODE] line),
 * and only when every Drifter start is loaded in this run as a START record (startsLoaded; kmode.cpp DrifterStartsLoaded). On the
 * first join of such a world the data mod is installed but only takes effect from the next launch, so the starts are not loaded
 * yet: the window stays up and the player's own pick stands (NewGameDecide's kLeanNotLoaded). Never by a refused new game's reshow
 * (a refusal would loop), never in freeplay. Each title frame after the open: */
const unsigned kSkipWaitMs = 3000;   /* BEGIN not reachable within this: the window is left up (the swap on BEGIN still holds) */
inline bool SkipArm(int autoloadNewGame, int worldMode, int linkedToThatWorld, int startsLoaded)
{
    return autoloadNewGame != 0 && worldMode == kKenshi && linkedToThatWorld != 0 && startsLoaded != 0;
}
struct SkipFacts
{
    int armed;           /* the autoload opened the window for a new character in a kenshi-mode world, not yet pressed */
    int atTitle;         /* this is the title frame's tail (the in-world tail never presses) */
    int kenshiLinked;    /* still linked to that world, and it is still kenshi mode */
    int panelWanted;     /* our MULTIPLAYER panel was opened again */
    int requestCode;     /* SaveManager+0xA0: 0 none, 1 save, 2 load, 3 import, 4 new game; -1 unreadable */
    int beginReachable;  /* the BEGIN button found, shown with every parent, enabled, and not under another window */
    unsigned waitedMs;   /* since the window was opened */
    SkipFacts() : armed(0), atTitle(0), kenshiLinked(0), panelWanted(0), requestCode(0), beginReachable(0), waitedMs(0) {}
};
enum SkipStepKind { kSkipNone = 0, kSkipWait = 1, kSkipFire = 2, kSkipGiveUp = 3 };
inline int SkipStep(const SkipFacts& f)
{
    if (f.armed == 0) return kSkipNone;
    if (f.atTitle == 0 || f.kenshiLinked == 0 || f.panelWanted != 0 || f.requestCode != 0) return kSkipGiveUp;
    if (f.beginReachable != 0) return kSkipFire;
    return f.waitedMs < kSkipWaitMs ? kSkipWait : kSkipGiveUp;
}
/* why a SkipStep gave up (the log line's words) */
inline const char* SkipGiveUpWhy(const SkipFacts& f)
{
    if (f.atTitle == 0) return "the title screen is gone";
    if (f.kenshiLinked == 0) return "the world's link went down or its start mode is no longer kenshi";
    if (f.panelWanted != 0) return "the MULTIPLAYER panel was opened again";
    if (f.requestCode < 0) return "the engine's pending request cannot be read (no SaveManager, or +0xA0 unreadable)";
    if (f.requestCode != 0) return "another request is pending (CANCEL, LOAD, IMPORT or a new game already posted)";
    return "the BEGIN button was not reachable (not found, hidden, disabled or under another window - CANCEL closes it)";
}

/* ---- WHICH DRIFTER THIS CHARACTER ROLLED: a key file in the profile's save folder (kmode.cpp) ----
 * Written beside the save after the first save that follows the swap; read when that save loads again. One line:
 * "start<TAB><start id>". A file that names no lean of this build is unknown (-1). */
const char* const kLeanFileName = "kenshi-start.key";
inline std::string LeanFileText(int lean)
{
    if (lean < 0 || lean >= kLeanCount) return std::string();
    return std::string("start\t") + kLeans[lean].startSid + "\r\n";
}
inline int LeanFromFileText(const std::string& text)
{
    if (text.compare(0, 6, "start\t") != 0) return -1;
    size_t e = text.find_first_of("\r\n", 6);
    std::string sid = text.substr(6, e == std::string::npos ? std::string::npos : e - 6);
    while (!sid.empty() && (sid[sid.size() - 1] == ' ' || sid[sid.size() - 1] == '\t')) sid.erase(sid.size() - 1);
    for (int i = 0; i < kLeanCount; ++i) if (sid == kLeans[i].startSid) return i;
    return -1;
}

/* ---- THE PLAYERS TAB'S FOOTER (playerstab.cpp): a block of label / value lines under the table ----
 *   Server: Dedicated | Hosted
 *   Time:   Vote | Host | Admin only, with the world speed, or Locked x1
 *   Start:  Freeplay | Kenshi mode
 *   Class:  this character's Drifter lean (kenshi mode only, when known)
 * One parameter per line, in that order; a line with no value is left out (the lines below move up), never shown blank.
 * Nothing before this game has linked to a world server (single player). Dedicated = this link's notebook sent DEDINFO or an
 * OPTIONS map carrying startmode (only a --dedicated notebook sends either). Time on a dedicated world comes from DEDINFO's mode
 * (none = locked at x1; nothing shown before DEDINFO, which comes once in the world); on a hosted world from the options map's
 * timemode (fixed = Host, consensus = Vote; the notebook's CLOCK byte, derived from the same option, when the map had none). */
struct FooterFacts
{
    int linked;          /* a world-server link exists or existed in this process, and these facts are its */
    int dedicated;       /* DEDINFO or startmode seen on this link */
    int dedInfoHere;     /* a DEDINFO arrived on this link */
    unsigned dedMode;    /* its mode: dedinfo kModeNone 0 / Host 1 / Vote 2 / Admin 3 */
    int timeOpt;         /* hosted: -1 unknown, 0 fixed, 1 consensus */
    int haveSpeed;       /* the world's clock arrived: speed is the world's speed */
    float speed;
    int startKnown;      /* a whole OPTIONS map arrived on this link */
    int startMode;       /* kFreeplay / kKenshi */
    int lean;            /* this character's lean, -1 unknown */
    FooterFacts() : linked(0), dedicated(0), dedInfoHere(0), dedMode(0), timeOpt(-1), haveSpeed(0), speed(1.0f), startKnown(0), startMode(kFreeplay), lean(-1) {}
};
/* "paused", "x1", "x2", "x1.5" (tenths, rounded) */
inline std::string SpeedWord(float speed)
{
    if (!(speed > 0.0f)) return "paused";
    if (speed > 9999.0f) speed = 9999.0f;
    long t = (long)(speed * 10.0f + 0.5f);
    if (t < 1) t = 1;
    if (t > 99990) t = 99990;
    std::string whole;
    long w = t / 10;
    do { whole.insert(whole.begin(), (char)('0' + (int)(w % 10))); w /= 10; } while (w > 0);
    std::string s = "x" + whole;
    if (t % 10 != 0) { s += '.'; s += (char)('0' + (int)(t % 10)); }
    return s;
}
/* the values alone (no label); "" = that line is left out */
inline std::string FooterServer(const FooterFacts& f)
{
    if (f.linked == 0) return std::string();
    return f.dedicated != 0 ? "Dedicated" : "Hosted";
}
inline std::string FooterTime(const FooterFacts& f)
{
    if (f.linked == 0) return std::string();
    std::string s;
    if (f.dedicated != 0)
    {
        if (f.dedInfoHere == 0) return std::string();
        if (f.dedMode == 0) return "Locked x1";
        if (f.dedMode == 1) s = "Host";
        else if (f.dedMode == 2) s = "Vote";
        else if (f.dedMode == 3) s = "Admin only";
        else return std::string();
    }
    else if (f.timeOpt == 0) s = "Host";
    else if (f.timeOpt == 1) s = "Vote";
    else return std::string();
    if (f.haveSpeed != 0) s += " (" + SpeedWord(f.speed) + ")";
    return s;
}
inline std::string FooterStart(const FooterFacts& f)
{
    if (f.linked == 0 || f.startKnown == 0) return std::string();
    return f.startMode == kKenshi ? "Kenshi mode" : "Freeplay";
}
inline std::string FooterClass(const FooterFacts& f)
{
    if (f.linked == 0 || f.startKnown == 0 || f.startMode != kKenshi) return std::string();
    if (f.lean < 0 || f.lean >= kLeanCount) return std::string();
    return kLeans[f.lean].name;
}
/* the block: at most kFooterLines lines (playerstab.cpp sizes the block for all of them, so the table never moves as lines come) */
enum { kFooterLines = 4 };
inline const char* FooterLabel(int k)
{
    static const char* const kLabel[kFooterLines] = { "Server:", "Time:", "Start:", "Class:" };
    return k >= 0 && k < kFooterLines ? kLabel[k] : "";
}
struct FooterLine { std::string label, value; };
inline std::vector<FooterLine> FooterLines(const FooterFacts& f)
{
    const std::string v[kFooterLines] = { FooterServer(f), FooterTime(f), FooterStart(f), FooterClass(f) };
    std::vector<FooterLine> out;
    for (int k = 0; k < kFooterLines; ++k)
    {
        if (v[k].empty()) continue;
        FooterLine l; l.label = FooterLabel(k); l.value = v[k];
        out.push_back(l);
    }
    return out;
}
/* A value wider than its column (it should not be: the longest is "Drifter (Researcher)") is wrapped, never cut: greedy, broken
   at spaces; a single word wider than the column is broken between characters (never inside a UTF-8 sequence, and never between
   the two bytes of an escaped '#' "##"). width(text, ctx) measures a candidate line; maxW is the column's width. Always at least
   one line; every character of `value` except the spaces a break consumed appears exactly once, in order. */
typedef int (*FooterMeasure)(const std::string& text, void* ctx);
inline size_t FooterCharEnd(const std::string& s, size_t i)
{
    if (i >= s.size()) return s.size();
    if (s[i] == '#' && i + 1 < s.size() && s[i + 1] == '#') return i + 2;
    size_t j = i + 1;
    while (j < s.size() && ((unsigned char)s[j] & 0xC0) == 0x80) ++j;
    return j;
}
inline std::vector<std::string> FooterWrap(const std::string& value, int maxW, FooterMeasure width, void* ctx)
{
    std::vector<std::string> lines;
    std::string cur;
    size_t i = 0;
    while (i < value.size())
    {
        size_t e = value.find(' ', i);
        if (e == std::string::npos) e = value.size();
        const std::string word = value.substr(i, e - i);
        const std::string joined = cur.empty() ? word : cur + " " + word;
        if (width(joined, ctx) <= maxW) cur = joined;
        else
        {
            if (!cur.empty()) { lines.push_back(cur); cur.clear(); }
            /* the word alone, broken between characters while it is too wide (a piece holds at least one character) */
            size_t a = 0;
            while (a < word.size())
            {
                size_t b = FooterCharEnd(word, a);
                while (b < word.size())
                {
                    const size_t n = FooterCharEnd(word, b);
                    if (width(word.substr(a, n - a), ctx) > maxW) break;
                    b = n;
                }
                if (b >= word.size()) { cur = word.substr(a); break; }
                lines.push_back(word.substr(a, b - a));
                a = b;
            }
        }
        i = e < value.size() ? e + 1 : e;
    }
    if (!cur.empty() || lines.empty()) lines.push_back(cur);
    return lines;
}

}   /* namespace swstart */
