/* rulesfile.h - THE WORLD'S RULES FILE, rules.txt in the world folder. Pure: no I/O, no globals.

   rules.txt is the world's rule record: the world server reads it at start, writes a rule into it when an admin changes one in the
   game, and reads it again when someone edits it by hand (the folder's change notice, store_main.cpp). The file stays simple: a short
   header, then for each rule a one-line note and a `name = value` line, the name as the GAME OPTIONS row shows it (any case), the
   value as GAME OPTIONS shows it (the world server's own words are taken too).

   Reading: a line starting with # is a note; an empty line is nothing; a line naming no rule this world knows is kept in the file and
   not used; a value the world server refuses is kept in the file, not used, and said in the server window; the last good line of a
   rule wins. A rule with no line is unchanged - never reset to a default - so a half-saved file changes nothing it does not name.

   Writing back keeps every byte it does not have to change: only the value of a changed rule's line is replaced (its name as
   written, the spacing, the line ending all kept), a rule the file lacks is added at the end with its note, a removed race row's line
   goes. The world server writes the whole file only once, when the world has none (from options.txt when the world has one: the
   migration - options.txt itself is left as it was, a backup).

   RULES_CHANGED (92, down only): the rules the world server just stored - the real fact, sent to every game when a change is
   stored, from the game or from the file. Each game turns it into grey chat lines (ChangeLines): "The world's rules changed: ..."
   with no changer's name.

   THE WORLD'S PASSWORDS (T-1158 step 7, owner 1368 a / 1369 b / 1371, build/mockups-t1158-step7.md): three more lines,
   `world password =`, `admin password =` and `owner password =`, each under its notes, at the top of the file under its opening notes. They are read APART from the rules:
   never a rule, never in the rules map, never sent to a game, and their values are never said in the window or the log (a line that
   names no rule and holds the word "password" is said with its value hidden). A password line's value is everything after the `=`
   with one space after it dropped (the space the file shows); the text its key is made from has the spaces at both ends dropped
   (worldpw.h - the same as a typed box or /login); an empty one, or no line, is no password. No minimum length (owner 1368). The world server makes its keys from them in memory at each
   read (store_main.cpp); the host's game writes the world password line and, when the owner password line is empty, a long random
   owner password when HOST is pressed with the same writer (WritePasswords),
   so the file is the same as one written by hand with the same values (owner rule 2026-11-02, "one way a world works"). */
#ifndef RULESFILE_H
#define RULESFILE_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <utility>
#include <cstring>
#include <cstdio>

namespace rulesfile {

const unsigned int kMsgRulesChanged = 92u;   /* down only: {u32 count, [str key, str value]...}, an empty value = the row was removed */
const unsigned int kProtocol = 145u;          /* the world-server protocol that carries RULES_CHANGED */

const char* const kFile = "rules.txt";
const char* const kOldFile = "options.txt";
const unsigned int kMaxPairs = 64u;           /* as OPTIONS: the most rows one message carries */
/* An OPTIONS message holding this key (any value; never a rule, never stored) carries a world's first hosting's settings filling its
   rules - the world server stores its rows like any others and tells no game about them (Announced). Counted in the 64 pairs. */
const char* const kFillKey = "@fill";
/* Left in the world folder once the world server has made or read rules.txt: from then on a missing rules.txt is never made again from
   options.txt (the old backup), so deleting rules.txt gives the world the default rules rather than the old ones (MigrateFromOld). */
const char* const kMadeFile = "rules.made";
const unsigned int kMaxStr = 4096u;
const int kShownLines = 3;                    /* the chat lines one change shows before "N more rules" */

enum Kind { kNumber = 0, kTick = 1, kPick = 2, kAmount = 3, kWhole = 4, kOnOff = 5, kRace = 6, kHidden = 7 };
struct Rule { const char* key; const char* label; int kind; const char* note; };
const int kRuleCount = 25;
/* The GAME OPTIONS rows, in the screen's order (panelstatus.h OptDefs - the offline suite checks the two agree), each with its note.
   Decision 1360 (2026-11-02): EXTRA RECRUITS IN BARS, BASE ACCESS, RESURRECTION FEE and FEE GROWTH say what their GAME OPTIONS hover
   notes say (panelstatus.h OptHoverText). A note is written only into a new file or with a rule the file lacks (WriteBack): an
   existing file's older note lines are kept as they are. */
inline const Rule* Rules()
{
    static const Rule r[kRuleCount] = {
        { "gp.gdm", "DAMAGE", kNumber, "how hard every hit lands. 0.5 to 4." },
        { "gp.cod", "CHANCE OF DEATH", kNumber, "how likely a downed character is to die. 0.5 to 4." },
        { "gp.ht", "HUNGER TIME", kNumber, "how long characters take to get hungry - higher is slower. 0.25 to 8." },
        { "gp.bs", "BUILDING SPEED", kNumber, "how fast buildings go up. 0.5 to 2." },
        { "gp.rs", "RESEARCH SPEED", kNumber, "how fast research goes. 0.5 to 2." },
        { "gp.ps", "CRAFTING SPEED", kNumber, "how fast crafting goes. 0.5 to 2." },
        { "gp.nnm", "NESTS AND CAMPS", kNumber, "how many animal nests and bandit camps the world has. 0.5 to 4." },
        { "gp.bl", "BANDITS LOOT DOWNED CHARACTERS", kTick, "bandits take items from characters they knock down. on or off." },
        { "gp.ep", "EASY PROSPECTING", kTick, "prospecting finds resources more easily. on or off." },
        { "gt.pop", "POPULATION", kNumber, "how many people live in the world. 0.5 to 4." },
        { "gt.squad", "SQUAD SIZE", kNumber, "how big the world's squads are. 0.5 to 3." },
        { "gt.raidsize", "RAID SIZE", kNumber, "how big raids are. 0.25 to 4." },
        { "gt.raidfreq", "RAID FREQUENCY", kNumber, "how often raids come. 0.25 to 3." },
        { "gt.attacks", "BASE ATTACKS", kWhole, "how often bases are attacked. 0 Bombardment, 1 High, 2 Normal, 3 Fewer, 4 Rare, 5 Never." },
        { "gt.limbloss", "LIMB LOSS", kWhole, "how easily limbs are lost. 0 Never, 1 Rare, 2 Frequent." },
        { "recruitmult", "EXTRA RECRUITS IN BARS", kPick, "how many times the usual recruits bars offer - Auto is one set per player, up to 4. Auto, 1, 2, 3 or 4." },
        { "basepolicy", "BASE ACCESS", kPick, "who may open a player's chests - teammates always share the whole base. Everyone, Owner only or Locked." },
        { "timemode", "GAME SPEED CONTROL", kPick, "who sets the game speed. Admin only, Slowest wins, Everyone agrees or Normal speed only." },
        { "profilecap", "PROFILES PER PLAYER", kWhole, "how many profiles one player may have. 1 to 16." },
        { "resurrect", "RESURRECTION", kOnOff, "fallen characters can be brought back. on or off." },
        { "resurrectfee", "RESURRECTION FEE", kAmount, "the price of bringing one back, in cats - the first is free, then it rises with each one currently reanimated. 0 or more." },
        { "resurrectgrowth", "FEE GROWTH", kPick, "how the price rises with each one currently reanimated. Steady or Steep." },
        { "spimport", "IMPORT FROM SINGLE PLAYER", kPick, "who may start a profile from a single-player save. Allowed for everyone, Owner and admins only or Not allowed." },
        { "spimportmoney", "IMPORT INCLUDES MONEY", kPick, "an imported profile keeps its money. Yes or No." },
        { "spimportresearch", "IMPORT INCLUDES RESEARCH", kPick, "an imported profile keeps its research. Yes or No." } };
    return r;
}
/* The words a pick row shows for each value the world server stores (panelstatus.h OptChoices). */
struct Word { const char* key; const char* wire; const char* word; };
const int kWordCount = 28;
inline const Word* Words()
{
    static const Word w[kWordCount] = {
        { "gt.attacks", "0", "Bombardment" }, { "gt.attacks", "1", "High" }, { "gt.attacks", "2", "Normal" }, { "gt.attacks", "3", "Fewer" },
        { "gt.attacks", "4", "Rare" }, { "gt.attacks", "5", "Never" },
        { "gt.limbloss", "0", "Never" }, { "gt.limbloss", "1", "Rare" }, { "gt.limbloss", "2", "Frequent" },
        { "recruitmult", "auto", "Auto" },
        { "basepolicy", "shared", "Everyone" }, { "basepolicy", "owner", "Owner only" }, { "basepolicy", "locked", "Locked" },
        { "timemode", "fixed", "Admin only" }, { "timemode", "consensus", "Slowest wins" },
        { "timemode", "vote", "Everyone agrees" }, { "timemode", "locked", "Normal speed only" },
        { "resurrectgrowth", "steady", "Steady" }, { "resurrectgrowth", "steep", "Steep" },
        { "spimport", "all", "Allowed for everyone" }, { "spimport", "owner", "Owner and admins only" }, { "spimport", "off", "Not allowed" },
        { "spimportmoney", "yes", "Yes" }, { "spimportmoney", "no", "No" },
        { "spimportresearch", "yes", "Yes" }, { "spimportresearch", "no", "No" },
        { "researchmode", "on", "on" }, { "researchmode", "off", "off" } };
    return w;
}
/* The rules the world server keeps that GAME OPTIONS does not show (their file name is their key). */
inline bool IsHiddenKey(const std::string& k) { return k == "researchmode" || k == "gp.ae" || k == "gp.dh" || k == "gt.civ"; }
inline bool IsRaceKey(const std::string& k) { return k.size() > 5 && k.compare(0, 5, "race.") == 0; }

inline char Lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
inline char Upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }
inline std::string LowerOf(const std::string& s) { std::string o(s); for (size_t i = 0; i < o.size(); ++i) o[i] = Lower(o[i]); return o; }
inline bool IsSpace(char c) { return c == ' ' || c == '\t'; }
inline std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && IsSpace(s[a])) ++a;
    while (b > a && IsSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}
/* trimmed, lower case, every run of spaces one space - how a name or a word is compared */
inline std::string Fold(const std::string& s)
{
    const std::string t = Trim(s);
    std::string o;
    for (size_t i = 0; i < t.size(); ++i)
    {
        if (IsSpace(t[i])) { if (!o.empty() && o[o.size() - 1] != ' ') o += ' '; continue; }
        o += Lower(t[i]);
    }
    return o;
}

inline int RuleIndex(const std::string& key)
{
    for (int i = 0; i < kRuleCount; ++i) if (key == Rules()[i].key) return i;
    return -1;
}
inline int KindOf(const std::string& key)
{
    const int i = RuleIndex(key);
    if (i >= 0) return Rules()[i].kind;
    if (IsRaceKey(key)) return kRace;
    if (key == "gp.ae" || key == "gp.dh" || key == "gt.civ") return kTick;
    return kHidden;
}
/* a key this world keeps: a GAME OPTIONS row, a hidden rule, or a race row */
inline bool KnownKey(const std::string& key) { return RuleIndex(key) >= 0 || IsHiddenKey(key) || IsRaceKey(key); }
/* T-1209 (owner 1414 d, mock-up 1418): race rows are not kept in rules.txt - the world reads its kenshi_api.cfg. A race row's line
   the world server removes is replaced by this one line (once in the file). */
const char* const kRaceComment = "# Race rules are not set here. Put a race mod's kenshi_api.cfg in this folder; the world reads it when it starts and when the file is saved.";
/* the race's name as players read it: the race row's id with each word's first letter in capitals ("shek" -> "Shek") */
inline std::string RaceName(const std::string& key)
{
    std::string s = IsRaceKey(key) ? key.substr(5) : key;
    for (size_t i = 0; i < s.size(); ++i) if (i == 0 || s[i - 1] == ' ') s[i] = Upper(s[i]);
    return s;
}
/* the rule's name in capitals, as its GAME OPTIONS row shows it (a rule with no row: its key) */
inline std::string Label(const std::string& key)
{
    const int i = RuleIndex(key);
    return i >= 0 ? std::string(Rules()[i].label) : key;
}
/* the rule's name in the file: the row's name in small letters (a rule with no row: its key) */
inline std::string FileName(const std::string& key)
{
    const int i = RuleIndex(key);
    return i >= 0 ? LowerOf(Rules()[i].label) : key;
}
/* the note above the rule's line, without its "# " */
inline std::string Note(const std::string& key)
{
    const int i = RuleIndex(key);
    if (i >= 0) return Label(key) + ": " + Rules()[i].note;
    if (IsRaceKey(key)) return key + ": the race rules for " + RaceName(key) + ", set in the game.";   /* the note a 0.4.0 world wrote above a race row: matched only so the row's removal takes it too (T-1209) */
    return key + ": a rule GAME OPTIONS does not show.";
}
/* the key a name in the file stands for ("" = none): a row's name in any case and spacing, or a key as the world server writes it */
inline std::string KeyOfName(const std::string& name)
{
    const std::string f = Fold(name);
    if (f.empty()) return std::string();
    for (int i = 0; i < kRuleCount; ++i) if (f == Fold(Rules()[i].label) || f == Rules()[i].key) return Rules()[i].key;
    if (IsHiddenKey(f) || IsRaceKey(f)) return f;
    return std::string();
}
/* the value as the file and the chat show it */
inline std::string Shown(const std::string& key, const std::string& wire)
{
    const int k = KindOf(key);
    if (k == kTick) return wire == "1" ? std::string("on") : wire == "0" ? std::string("off") : wire;
    for (int i = 0; i < kWordCount; ++i) if (key == Words()[i].key && wire == Words()[i].wire) return Words()[i].word;
    return wire;
}
/* the value the world server stores for a value written in the file (no range check - the world server's own check decides);
   false for an empty value */
inline bool WireOf(const std::string& key, const std::string& text, std::string* wire)
{
    const std::string t = Trim(text), f = Fold(text);
    if (t.empty()) return false;
    const int k = KindOf(key);
    if (k == kTick)
    {
        if (f == "on" || f == "yes" || f == "true" || f == "1") { *wire = "1"; return true; }
        if (f == "off" || f == "no" || f == "false" || f == "0") { *wire = "0"; return true; }
        *wire = t; return true;
    }
    if (k == kOnOff || key == "researchmode")
    {
        if (f == "on" || f == "yes" || f == "true" || f == "1") { *wire = "on"; return true; }
        if (f == "off" || f == "no" || f == "false" || f == "0") { *wire = "off"; return true; }
        *wire = t; return true;
    }
    if (key == "timemode")   /* `fixed` (the word options.txt and earlier builds wrote) and `admin` are Admin only; `vote` is Everyone agrees */
    {
        if (f == "admin" || f == "fixed" || f == "host") { *wire = "fixed"; return true; }
        if (f == "consensus" || f == "slowest") { *wire = "consensus"; return true; }
        if (f == "vote" || f == "agree") { *wire = "vote"; return true; }
        if (f == "locked" || f == "normal") { *wire = "locked"; return true; }
    }
    if (key == "spimport" && (f == "host" || f == "host only")) { *wire = "owner"; return true; }   /* the middle choice's old value and label, as v0.4.0 wrote them (owner 1383) */
    for (int i = 0; i < kWordCount; ++i)
        if (key == Words()[i].key && (f == Fold(Words()[i].word) || f == Words()[i].wire)) { *wire = Words()[i].wire; return true; }
    if (k == kPick || k == kWhole || k == kAmount || k == kNumber) { *wire = f; return true; }
    *wire = t;   /* a race row's value is its own canonical text, case and all */
    return true;
}
/* what the world server takes for the rule, for the server window's "not allowed" line ("" = no short answer) */
inline std::string Allowed(const std::string& key)
{
    const int k = KindOf(key);
    if (k == kNumber) return "a number from 0.01 to 100";
    if (k == kTick || k == kOnOff || key == "researchmode") return "on or off";
    if (k == kAmount) return "a whole number, 0 or more";
    if (key == "gt.attacks") return "0 to 5";
    if (key == "gt.limbloss") return "0 to 3";
    if (key == "profilecap") return "1 to 16";
    if (key == "recruitmult") return "Auto, 1, 2, 3 or 4";
    std::vector<std::string> w;
    for (int i = 0; i < kWordCount; ++i) if (key == Words()[i].key) w.push_back(Words()[i].word);
    std::string s;
    for (size_t i = 0; i < w.size(); ++i) s += (i == 0 ? std::string() : (i + 1 == w.size() ? std::string(" or ") : std::string(", "))) + w[i];
    return s;
}

/* ---- the file's lines ---- */
struct Line { std::string text; std::string end; };   /* the line without its ending, and its ending ("\r\n", "\n" or "" for the last) */
inline std::vector<Line> SplitLines(const std::string& s)
{
    std::vector<Line> out;
    size_t at = 0;
    while (at < s.size())
    {
        const size_t n = s.find('\n', at);
        Line l;
        if (n == std::string::npos) { l.text = s.substr(at); at = s.size(); }
        else
        {
            l.text = s.substr(at, n - at);
            l.end = "\n";
            if (!l.text.empty() && l.text[l.text.size() - 1] == '\r') { l.text.erase(l.text.size() - 1); l.end = "\r\n"; }
            at = n + 1;
        }
        out.push_back(l);
    }
    return out;
}
const char kBom[] = "\xEF\xBB\xBF";
/* the line's text with a UTF-8 byte-order mark at the very start of the file left out (line 0 only) */
inline std::string BodyOf(const std::vector<Line>& ls, size_t i)
{
    if (i == 0 && ls[0].text.compare(0, 3, kBom) == 0) return ls[0].text.substr(3);
    return ls[i].text;
}
/* A rule line's parts: the key it names ("" = a note, a blank line, or no rule this world knows - unknown says which), the value text. */
inline bool RuleLine(const std::string& body, std::string* key, std::string* value, bool* unknown)
{
    key->clear(); value->clear(); *unknown = false;
    const std::string t = Trim(body);
    if (t.empty() || t[0] == '#') return false;
    const size_t eq = t.find('=');
    if (eq == std::string::npos) { *unknown = true; return false; }
    *key = KeyOfName(t.substr(0, eq));
    if (key->empty()) { *unknown = true; return false; }
    *value = Trim(t.substr(eq + 1));
    return true;
}

/* ---- the world's passwords ---- */
enum { kPwWorld = 0, kPwAdmin = 1, kPwOwner = 2, kPwCount = 3 };
inline const char* PwName(int i) { return i == kPwWorld ? "world password" : i == kPwAdmin ? "admin password" : "owner password"; }
/* the notes above each password line, without their "# " (the mock-up's words): n from 0; 0 = past the last note */
inline const char* PwNote(int i, int n)
{
    if (i == kPwWorld) return n == 0 ? "WORLD PASSWORD: what players type to join. Empty = anyone who knows the address can join."
                            : n == 1 ? "Anyone who can open this file can read it." : 0;   /* owner 1515 (2026-11-05) */
    if (i == kPwAdmin) return n == 0 ? "ADMIN PASSWORD: what admins type after /login in the game's chat. Empty = nobody can log in"
                            : n == 1 ? "as an admin. Anyone who can open this file can read it." : 0;
    return n == 0 ? "OWNER PASSWORD: what the world's owner types after /login. The owner is always an admin and"
         : n == 1 ? "alone chooses the admins. When the world is started from the game, the game fills this in"
         : n == 2 ? "and logs its host in by itself. Empty = the world has no owner." : 0;
}
/* A password line: its name before the first "=" (any case and spacing) is a password's; value = everything after the "=" with one
   space after it dropped. false for any other line (a note, a blank line, a rule line, a line with no "="). */
inline bool PwLine(const std::string& body, int* which, std::string* value)
{
    const std::string t = Trim(body);
    if (t.empty() || t[0] == '#') return false;
    const size_t eq = body.find('=');
    if (eq == std::string::npos) return false;
    const std::string f = Fold(body.substr(0, eq));
    for (int i = 0; i < kPwCount; ++i)
        if (f == PwName(i))
        {
            *which = i;
            *value = body.substr(eq + 1);
            if (!value->empty() && (*value)[0] == ' ') value->erase(0, 1);
            return true;
        }
    return false;
}
/* the password's line as written: "world password =" when empty, "world password = <value>" otherwise */
inline std::string PwLineText(int i, const std::string& value) { return std::string(PwName(i)) + " =" + (value.empty() ? std::string() : " " + value); }
/* a password's notes and line, each ending in eol */
inline std::string PwText(int i, const std::string& value, const std::string& eol)
{
    std::string s;
    for (int n = 0; PwNote(i, n) != 0; ++n) s += std::string("# ") + PwNote(i, n) + eol;
    return s + PwLineText(i, value) + eol;
}
/* A line's text as the window and the log may show it: a line holding the word "password" (any case) shows only its name, its value
   hidden - a misspelt password line never prints the password. */
inline std::string ShownLineText(const std::string& t)
{
    if (LowerOf(t).find("password") == std::string::npos) return t;
    const size_t eq = t.find('=');
    return eq == std::string::npos ? std::string("(hidden)") : Trim(t.substr(0, eq)) + " = (hidden)";
}

typedef bool (*ValueCheck)(const std::string& key, const std::string& wire);
struct BadLine { int line; std::string text; std::string key; };   /* line numbers from 1, as Notepad counts */
struct Parsed
{
    std::map<std::string, std::string> rules;   /* key -> the stored value of its last good line */
    std::vector<BadLine> bad;                   /* a rule line whose value the world server refuses */
    std::vector<BadLine> unknown;               /* a line naming no rule this world knows (key "") */
    int duplicates;                             /* good lines of a rule an earlier good line already set */
    std::string pw[kPwCount];                   /* the password lines' values (kPwWorld, kPwAdmin, kPwOwner): the last line of each wins */
    bool pwThere[kPwCount];                     /* a line names that password */
    Parsed() : duplicates(0) { for (int i = 0; i < kPwCount; ++i) pwThere[i] = false; }
};
/* ok: the world server's check of a key and value (store_main.cpp OptionValueOk); 0 = take every mapped value */
inline Parsed Parse(const std::string& text, ValueCheck ok)
{
    Parsed p;
    const std::vector<Line> ls = SplitLines(text);
    for (size_t i = 0; i < ls.size(); ++i)
    {
        std::string key, value, wire;
        bool unknown = false;
        const std::string body = BodyOf(ls, i);
        int which = -1;
        if (PwLine(body, &which, &value)) { p.pw[which] = value; p.pwThere[which] = true; continue; }   /* a password: apart from the rules */
        if (!RuleLine(body, &key, &value, &unknown))
        {
            if (unknown) { BadLine b; b.line = (int)i + 1; b.text = ShownLineText(Trim(body)); p.unknown.push_back(b); }
            continue;
        }
        if (!WireOf(key, value, &wire) || (ok != 0 && !ok(key, wire))) { BadLine b; b.line = (int)i + 1; b.text = ShownLineText(Trim(body)); b.key = key; p.bad.push_back(b); continue; }
        if (p.rules.count(key)) ++p.duplicates;
        p.rules[key] = wire;
    }
    return p;
}

/* the line ending new lines take: the file's own (CRLF when it has any, else LF), CRLF for an empty file (Notepad's) */
inline std::string EndOf(const std::string& text)
{
    if (text.find("\r\n") != std::string::npos) return "\r\n";
    return text.find('\n') != std::string::npos ? std::string("\n") : std::string("\r\n");
}
/* the order rules are written in: the GAME OPTIONS rows, then every other key as the map holds them */
inline std::vector<std::string> KeyOrder(const std::map<std::string, std::string>& want)
{
    std::vector<std::string> o;
    for (int i = 0; i < kRuleCount; ++i) if (want.count(Rules()[i].key)) o.push_back(Rules()[i].key);
    for (std::map<std::string, std::string>::const_iterator it = want.begin(); it != want.end(); ++it)
        if (RuleIndex(it->first) < 0) o.push_back(it->first);
    return o;
}
inline std::string RuleText(const std::string& key, const std::string& wire, const std::string& eol)
{
    return "# " + Note(key) + eol + FileName(key) + " = " + Shown(key, wire) + eol;
}
inline std::string Header(const std::string& worldName, const std::string& eol)
{
    return (worldName.empty() ? std::string("# Rules for this world.") : "# Rules for the world \"" + worldName + "\".") + eol
         + "# Change a value after \"=\" and save. The world uses it within seconds" + eol
         + "# and tells every player in chat. Lines starting with # are notes." + eol
         + "# Admins can change the same rules in the game (GAME OPTIONS)." + eol;
}
/* A whole new file: the header, the three password lines (empty), then each rule with its note (the world's first rules.txt, the
   migration from options.txt, a rules.txt made again). leaveOut: bit (1 << kPw*) = that password line is not written (a 0.4.0 lock the
   world carries for it, worldpw::CarryLeaveOut - owner 1513 a). */
inline std::string Fresh(const std::map<std::string, std::string>& want, const std::string& worldName, unsigned leaveOut = 0)
{
    const std::string eol = "\r\n";
    std::string s = Header(worldName, eol);
    for (int i = 0; i < kPwCount; ++i) if ((leaveOut & (1u << i)) == 0) s += eol + PwText(i, std::string(), eol);
    const std::vector<std::string> o = KeyOrder(want);
    for (size_t i = 0; i < o.size(); ++i) s += eol + RuleText(o[i], want.find(o[i])->second, eol);
    return s;
}
/* the line index just past the file's opening notes: the run of # lines it starts with, when a blank line or the file's end follows
   it (a # line right above a rule line is that rule's note, not an opening note: 0) */
inline size_t PastOpeningNotes(const std::vector<Line>& ls)
{
    size_t at = 0;
    while (at < ls.size() && !Trim(BodyOf(ls, at)).empty() && Trim(BodyOf(ls, at))[0] == '#') ++at;
    if (at < ls.size() && !Trim(BodyOf(ls, at)).empty()) return 0;
    return at;
}
/* THE PASSWORD WRITE (the world server and the host's game alike). set: the passwords this write gives a value (kPwWorld /
   kPwAdmin -> value; an empty map = none). Each line naming a set password gets "= value" (its name as written and its ending kept);
   a password no line names is added under the file's opening notes with its two notes - empty, or with its set value - the world's
   in the order world, admin, owner. Every other byte is kept. An empty old: a new file (the header and the three password lines). A value holding a
   line ending is never written (the caller refuses it first). leaveOut (owner 1513 a / 1508 b): bit (1 << kPw*) = a password no line
   names is NOT added unless this write gives it a value that is not empty (worldpw::CarryLeaveOut, HostLeaveOut). */
inline std::string WritePasswords(const std::string& old, const std::map<int, std::string>& set, const std::string& worldName, unsigned leaveOut = 0)
{
    unsigned skip = leaveOut;
    for (std::map<int, std::string>::const_iterator v = set.begin(); v != set.end(); ++v)
        if (v->first >= 0 && v->first < kPwCount && !v->second.empty()) skip &= ~(1u << v->first);
    const std::string base = (Trim(old).empty() && old.find('\n') == std::string::npos) ? Fresh(std::map<std::string, std::string>(), worldName, skip) : old;
    const std::string eol = EndOf(base);
    std::vector<Line> ls = SplitLines(base);
    bool there[kPwCount] = { false, false, false };
    for (size_t i = 0; i < ls.size(); ++i)
    {
        int which = -1; std::string value;
        if (!PwLine(BodyOf(ls, i), &which, &value)) continue;
        there[which] = true;
        std::map<int, std::string>::const_iterator w = set.find(which);
        if (w == set.end() || w->second == value) continue;
        const size_t eq = ls[i].text.find('=');
        ls[i].text = ls[i].text.substr(0, eq) + "=" + (w->second.empty() ? std::string() : " " + w->second);
    }
    const size_t at = PastOpeningNotes(ls);
    std::string add;
    for (int i = 0; i < kPwCount; ++i)
    {
        if (there[i] || (skip & (1u << i)) != 0) continue;
        std::map<int, std::string>::const_iterator w = set.find(i);
        const std::string block = PwText(i, w == set.end() ? std::string() : w->second, eol);
        add += at == 0 ? block + eol : eol + block;
    }
    std::string s;
    for (size_t i = 0; i < ls.size(); ++i)
    {
        if (i == at && !add.empty()) s += add;
        s += ls[i].text + (i + 1 == at && ls[i].end.empty() && !add.empty() ? eol : ls[i].end);
    }
    if (at >= ls.size()) s += add;
    return s;
}
/* THE EXPORT'S rules.txt (a world export, store_main.cpp KeepExport): every password line empty (`world password =`, ...), a
   password the file has no line for added empty under its opening notes, every other byte as it is - a copy of the world never
   carries its passwords. An empty file stays empty. */
inline std::string ExportText(const std::string& text)
{
    if (Trim(text).empty() && text.find('\n') == std::string::npos) return text;
    std::map<int, std::string> none;
    for (int i = 0; i < kPwCount; ++i) none[i] = std::string();
    return WritePasswords(text, none, std::string());
}
/* the world folder's own rules.txt, as an export names it ('/'-joined, below the world folder; any case) */
inline bool IsTopRulesFile(const std::string& rel) { return LowerOf(rel) == kFile; }
/* THE LINE-KEEPING WRITE. old: the file's bytes as last read or written; want: the world's rules; only: the keys this write is about
   (0 = every key). For each key in `only`: the value of each of its lines that does not already say the wanted value is replaced
   (the name as written, the spacing after "=" and the ending kept); a key `want` no longer holds (a removed race row) loses its lines
   and the note just above each - the first removed race row's line becomes kRaceComment when the file does not say it yet (T-1209);
   a key no line names is added at the end with its note. Everything else is kept byte for byte. An empty old is a whole new file
   (Fresh, with leaveOut's password lines left out). */
inline std::string WriteBack(const std::string& old, const std::map<std::string, std::string>& want, const std::set<std::string>* only, const std::string& worldName,
                             unsigned leaveOut = 0)
{
    if (Trim(old).empty() && old.find('\n') == std::string::npos) return Fresh(want, worldName, leaveOut);
    const std::string eol = EndOf(old);
    std::vector<Line> ls = SplitLines(old);
    std::vector<int> drop(ls.size(), 0);
    std::set<std::string> seen;
    bool raceNote = false;   /* the file already says kRaceComment */
    for (size_t i = 0; i < ls.size() && !raceNote; ++i) raceNote = Trim(ls[i].text) == kRaceComment;
    for (size_t i = 0; i < ls.size(); ++i)
    {
        std::string key, value, wire;
        bool unknown = false;
        const std::string body = BodyOf(ls, i);
        if (!RuleLine(body, &key, &value, &unknown)) continue;
        if (only != 0 && !only->count(key)) continue;
        seen.insert(key);
        std::map<std::string, std::string>::const_iterator w = want.find(key);
        if (w == want.end())
        {
            drop[i] = 1;
            if (i > 0 && Trim(BodyOf(ls, i - 1)) == "# " + Note(key)) drop[i - 1] = 1;
            if (IsRaceKey(key) && !raceNote) { raceNote = true; drop[i] = 0; ls[i].text = kRaceComment; }   /* T-1209: one comment line takes the race rows' place */
            continue;
        }
        if (WireOf(key, value, &wire) && wire == w->second) continue;
        const size_t eq = ls[i].text.find('=');
        size_t v = eq + 1;
        while (v < ls[i].text.size() && IsSpace(ls[i].text[v])) ++v;
        const std::string gap = v > eq + 1 ? ls[i].text.substr(eq + 1, v - eq - 1) : std::string(" ");
        ls[i].text = ls[i].text.substr(0, eq + 1) + gap + Shown(key, w->second);
    }
    std::string s;
    for (size_t i = 0; i < ls.size(); ++i) if (!drop[i]) s += ls[i].text + ls[i].end;
    const std::vector<std::string> o = KeyOrder(want);
    for (size_t i = 0; i < o.size(); ++i)
    {
        if (seen.count(o[i]) || (only != 0 && !only->count(o[i]))) continue;
        if (!s.empty() && s[s.size() - 1] != '\n') s += eol;
        s += eol + RuleText(o[i], want.find(o[i])->second, eol);
    }
    return s;
}
/* T-1202 - THE HOST'S GAME OPTIONS BEFORE THE WORLD'S FIRST START. A brand-new world's server answers its first game while that
   game is still in the menu, before its Game options can reach it (they go up only once it is in the world): an import asked from
   the menu is allowed or refused, and gets money and research, and NEW PROFILE's answer carries the profile cap - all decided on the
   world server's defaults unless rules.txt already holds the host's choice. So at a world's FIRST hosting (stewardrules.h
   FirstHostingPress: no rule in its rules.txt, no options.txt) the host's game writes EVERY row the host chose for that world into
   rules.txt in the same step as the password lines, before the world server starts (ui.cpp PanelStartNotebook, store.cpp
   StoreWriteWorldPassword) - the same rows the HOST press then hands over (ui.cpp PanelOptHandOver), which the world server, already
   holding them, stores as no change. firstHostingRows: the press carries the host's chosen rows as the world's first settings
   (stewardrules.h HostPressRows gave kPressSendsHostSettings - not when GAME OPTIONS' Done was never pressed for this world, nor for a
   later hosting); chosen: those rows (key, value as GAME OPTIONS stores it); ok: the world server's own check (optionvalue.h
   worldopt::ValueOk, the body of store_main.cpp OptionValueOk; 0 = no rows). A row is written only when it is a GAME OPTIONS row
   (never a hidden rule, a race row or a message's marker), the world server takes its value, and the line written reads back as that
   same value. None otherwise: the world server's defaults, as before. */
inline std::map<std::string, std::string> FirstStartRows(bool firstHostingRows, const std::vector<std::pair<std::string, std::string> >& chosen, ValueCheck ok)
{
    std::map<std::string, std::string> out;
    if (!firstHostingRows || ok == 0) return out;
    for (size_t i = 0; i < chosen.size(); ++i)
    {
        const std::string& key = chosen[i].first;
        if (RuleIndex(key) < 0) continue;
        std::string wire, back;
        if (!WireOf(key, chosen[i].second, &wire) || !ok(key, wire)) continue;
        if (!WireOf(key, Shown(key, wire), &back) || back != wire) continue;   /* the line written ("name = <shown>") reads back as this value */
        out[key] = wire;
    }
    return out;
}
/* The rows written into the file HOST is about to hand the world server (the password write's result): the line-keeping write for
   these keys only - a line naming one gets the value, a missing one is added at the end with its note, in the file's own line
   ending; every other byte kept, no key written twice. No rows: the text as it is. options.txt is never in play here: a first
   hosting has none (MigrateFromOld is only ever false once a rule is named, and with no options.txt that read was the defaults
   anyway). */
inline std::string WriteFirstStartRows(const std::string& text, const std::map<std::string, std::string>& rows, const std::string& worldName)
{
    if (rows.empty()) return text;
    std::set<std::string> only;
    for (std::map<std::string, std::string>::const_iterator it = rows.begin(); it != rows.end(); ++it) only.insert(it->first);
    return WriteBack(text, rows, &only, worldName);
}

/* ---- what changed, said ---- */
typedef std::vector<std::pair<std::string, std::string> > Pairs;
/* the keys whose value differs between two maps (a key only in `was` comes with an empty value: removed), in the write order */
inline Pairs Diff(const std::map<std::string, std::string>& was, const std::map<std::string, std::string>& now)
{
    Pairs out;
    const std::vector<std::string> o = KeyOrder(now);
    for (size_t i = 0; i < o.size(); ++i)
    {
        std::map<std::string, std::string>::const_iterator w = was.find(o[i]);
        const std::string& v = now.find(o[i])->second;
        if (w == was.end() || w->second != v) out.push_back(std::make_pair(o[i], v));
    }
    for (std::map<std::string, std::string>::const_iterator it = was.begin(); it != was.end(); ++it)
        if (!now.count(it->first)) out.push_back(std::make_pair(it->first, std::string()));
    return out;
}
/* The rows of a stored change every game is told (RULES_CHANGED): every row it moved, a rule the world did not hold before included,
   at most kMaxPairs - none when the change is a world's first hosting filling its rules (fill: its OPTIONS message held kFillKey). */
inline Pairs Announced(const Pairs& moved, bool fill)
{
    Pairs out;
    if (fill) return out;
    for (size_t i = 0; i < moved.size() && out.size() < (size_t)kMaxPairs; ++i) out.push_back(moved[i]);
    return out;
}
/* The world's rules are made from options.txt only when rules.txt names no rule (absent, or holding only the password lines the host's
   game writes ahead of the first start of a world from before rules.txt) AND the world server never made or read one here (kMadeFile
   absent): a world from before rules.txt. A deleted rules.txt otherwise gives the default rules. rulesNamed: rules.txt is there and
   names a rule (NamesARule), or is there and cannot be read. */
inline bool MigrateFromOld(bool rulesNamed, bool madeMarkerThere) { return !rulesNamed && !madeMarkerThere; }
/* the text has a line naming a rule (its value good or not) - a password line, a note or an unknown line is none */
inline bool NamesARule(const std::string& text)
{
    const std::vector<Line> ls = SplitLines(text);
    for (size_t i = 0; i < ls.size(); ++i)
    {
        std::string key, value; bool unknown = false; int which = -1;
        if (PwLine(BodyOf(ls, i), &which, &value)) continue;
        if (RuleLine(BodyOf(ls, i), &key, &value, &unknown)) return true;
    }
    return false;
}
/* one change's words: "RAID SIZE is now 1.5" / "the race rules for Shek" */
inline std::string ChangeWords(const std::string& key, const std::string& wire)
{
    if (IsRaceKey(key)) return "the race rules for " + RaceName(key);
    if (wire.empty()) return Label(key) + " is no longer set";
    return Label(key) + " is now " + Shown(key, wire);
}
/* The chat lines every game shows for one stored change: no changer's name: one line a rule for the first three,
   then one line for the rest. */
inline std::vector<std::string> ChangeLines(const Pairs& changed)
{
    std::vector<std::string> out;
    const std::string head = "The world's rules changed: ";
    for (size_t i = 0; i < changed.size() && (int)i < kShownLines; ++i) out.push_back(head + ChangeWords(changed[i].first, changed[i].second) + ".");
    if ((int)changed.size() > kShownLines)
    {
        const size_t more = changed.size() - (size_t)kShownLines;
        char n[24];
        _snprintf(n, sizeof n - 1, "%u", (unsigned)more);
        n[sizeof n - 1] = 0;
        out.push_back(head + n + (more == 1 ? " more rule" : " more rules") + " (see GAME OPTIONS).");
    }
    return out;
}
/* The world server's window lines (mock-up screen 17). */
inline std::string WindowChangeLine(const std::string& key, const std::string& wire) { return std::string(kFile) + ": " + ChangeWords(key, wire) + "."; }
inline std::string WindowBadLine(const BadLine& b, bool haveNow, const std::string& now)
{
    char n[24];
    _snprintf(n, sizeof n - 1, "%d", b.line);
    n[sizeof n - 1] = 0;
    const std::string allowed = Allowed(b.key);
    return std::string(kFile) + " line " + n + ": \"" + b.text + "\" is not allowed" + (allowed.empty() ? std::string() : " (" + allowed + ")")
         + ". The world keeps " + (haveNow ? Shown(b.key, now) : std::string("it as it was")) + ".";
}
inline std::string WindowUnknownLine(const BadLine& b)
{
    char n[24];
    _snprintf(n, sizeof n - 1, "%d", b.line);
    n[sizeof n - 1] = 0;
    return std::string(kFile) + " line " + n + ": \"" + b.text + "\" is not a rule this world knows. It stays in the file and is not used.";
}
inline std::string WindowUnreadableLine() { return std::string(kFile) + " could not be read. The world keeps its rules; save it again."; }

/* ---- RULES_CHANGED's payload ---- */
inline void PutU32(std::vector<char>* b, unsigned int v) { const size_t at = b->size(); b->resize(at + 4); std::memcpy(&(*b)[at], &v, 4); }
inline void PutStr(std::vector<char>* b, const std::string& s) { PutU32(b, (unsigned int)s.size()); b->insert(b->end(), s.begin(), s.end()); }
inline bool Encode(const Pairs& changed, std::vector<char>* out)
{
    out->clear();
    if (changed.empty() || changed.size() > (size_t)kMaxPairs) return false;
    PutU32(out, (unsigned int)changed.size());
    for (size_t i = 0; i < changed.size(); ++i)
    {
        if (changed[i].first.size() > kMaxStr || changed[i].second.size() > kMaxStr) { out->clear(); return false; }
        PutStr(out, changed[i].first); PutStr(out, changed[i].second);
    }
    return true;
}
inline bool Decode(const void* p, size_t n, Pairs* out)
{
    out->clear();
    const char* b = (const char*)p;
    unsigned int c = 0;
    if (b == 0 || n < 4) return false;
    std::memcpy(&c, b, 4);
    if (c == 0 || c > kMaxPairs) return false;
    size_t at = 4;
    for (unsigned int i = 0; i < c; ++i)
    {
        std::string s[2];
        for (int k = 0; k < 2; ++k)
        {
            unsigned int len = 0;
            if (n - at < 4) { out->clear(); return false; }
            std::memcpy(&len, b + at, 4); at += 4;
            if (len > kMaxStr || n - at < len) { out->clear(); return false; }
            s[k].assign(b + at, len); at += len;
        }
        if (s[0].empty()) { out->clear(); return false; }
        out->push_back(std::make_pair(s[0], s[1]));
    }
    if (at != n) { out->clear(); return false; }
    return true;
}

}

#endif
