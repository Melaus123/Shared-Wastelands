/* src/common/profiles.h - prof1 (docs/design-profiles1.md sections 1, 2 and 4 step 4): SEVERAL PROFILES PER PERSON IN ONE
 * WORLD, AS PURE DECISIONS AND BYTES.
 *
 * A profile is one crew of one person in one world. To the mod each profile is a separate player: it has its own notebook
 * slot, its own save folder on the player's PC and its own faction name. The notebook keeps one line per profile in
 * profiles.txt beside slots.txt:
 *
 *   prof1 <TAB> person id <TAB> number <TAB> slot (-1 = none bound yet) <TAB> created <TAB> last played <TAB> active|deleted
 *         <TAB> profile name <TAB> faction name
 *
 * THE SLOT KEY. slots.txt and areas.txt key a player by a name (B13). Profile 1 of a person keeps the BARE person id as its
 * key - so a world that was played before profiles existed hands each person's old slot, areas and operator flag to their
 * profile 1 unchanged - and profile n >= 2 is keyed "<person>.<n>". A profile number is never given out twice to one person
 * (a deleted profile's number and its slot are RETIRED: reuse would hand the new profile the old one's records and base).
 *
 * THE LOBBY (store protocol 48). The game's HELLO ends with a u32 profile number after its mod list: 0 = "no profile yet".
 * The notebook answers a 0 with PROFILES (45) - this person's active profiles and the host's cap - instead of a WELCOME, and
 * the connection waits in the lobby (no slot, no areas). From there the game sends PROFILES up (NEW with a name, DELETE with
 * a number) and gets the list back with a verdict, or PICKS a profile by sending its HELLO again with that number, which the
 * notebook admits exactly as it admitted every HELLO before (slot bound to the profile's key) or answers with a refusal.
 *
 *   up   (game -> notebook): u8 kind (1 NEW, 2 DELETE) | u32 number (NEW: 0 = the next one) | u32 len + name (NEW)
 *        names2a (store protocol 51): kind 3 FACTION | u32 the sender's own profile number | u32 len + its player faction's
 *        in-game name. No answer: the notebook writes it into that profile's row (FactionForRow) or refuses it.
 *        T-201 PP6' (owner 175, store protocol 56): kind 4 SAVED | u32 the sender's own profile number | u32 0 (no name) - sent by a
 *        game after its own save of that profile FINISHED. No answer: the notebook marks the row played (SavedDecide).
 *        T-368 (store protocol 70): kind 3 FACTION is the name the game's world LOADED with; kind 5 RENAME | u32 the sender's own profile
 *        number | u32 len + the name its player just gave its faction. The notebook judges both against every other profile's faction
 *        name (FactionDecide) and answers only when it changed the name (FACTION answer below).
 *        T-368: kind 6 FACTION SEEN | u32 the sender's own profile number | u32 len + the name a FACTION answer gave - the game has that
 *        answer; the world sends an answer not yet acknowledged again at that profile's next admission.
 *        A faction name is any text the player could type (up to kRowFactionMax bytes): the wire carries its bytes as they are, and
 *        profiles.txt keeps it in its field form (FactionFieldFormat - plain, or '#' + hex where plain text would not survive the file).
 *   down (notebook -> game): u8 answers (0 LIST, 1 NEW, 2 DELETE, 3 PICK) | u8 verdict | u32 cap | u32 subject number
 *                            | u32 count (<= kWireRowsMax) | count x {u32 number, i32 slot, u32 created, u32 last played,
 *                              u32 played (56: 1 = a finished save reached the world), u32 len + name, u32 len + faction}
 *        T-368 (store protocol 70), two more answers, each with its own body (AnswerKindOf reads the first byte):
 *          4 FACTION: u8 4 | u8 verdict (kFacBack / kFacMoved / kFacDefault) | u32 subject number | u32 len + the name judged
 *                     | u32 len + the name the profile's faction has now - to the sender of a kind 3 / 5 whose name the world changed
 *          5 TAKEN:   u8 5 | u8 0 | u32 count (<= kTakenWireMax) | count x {u32 len + faction name} - every OTHER profile's faction
 *                     name in the world, connected or not: to a game at its admission and to every admitted game at each change
 *
 * Pure: no engine memory, no Windows, no ENet; the offline suite runs the same code. C++03 (VS2010 v100).
 */
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "areaclaim.h"   /* coopstore::PlayerIdOk - the person id is B13's player id */
#include "worlddir.h"    /* coopworld::DisplayNameOk - a profile name follows the player display-name rule */
#include "diskformat.h"  /* owner 429: LinkFolderFormatDecide */

namespace coopprof {

const unsigned kCapDefault = 3, kCapMin = 1, kCapMax = 16;   /* the host option `profilecap` (D5) */
const unsigned kNumMax = 9999;                                /* profile numbers per person, never reused */
const unsigned kWireRowsMax = 64;                             /* active rows in one PROFILES answer (cap <= 16) */
const size_t   kWireStrMax = 64;

enum Kind { kReqNew = 1, kReqDelete = 2, kReqFaction = 3, kReqSaved = 4, kReqRename = 5, kReqFactionSeen = 6 };   /* T-368: 5 RENAME - the player renamed its faction; 6 FACTION SEEN - the game has a FACTION answer */   /* T-201 PP6' (owner 175): 4 SAVED - a game's own save of its profile finished */   /* names2a: 3 FACTION - a playing game's faction name for its own row */
enum Answers { kAnsList = 0, kAnsNew = 1, kAnsDelete = 2, kAnsPick = 3, kAnsFaction = 4, kAnsTaken = 5 };   /* T-368: 4 and 5 have their own bodies */
enum Verdict
{
    kOk = 0,
    kRefusedCap = 1,        /* NEW at the host's cap */
    kRefusedName = 2,       /* NEW with a name the display-name rule refuses */
    kRefusedNameTaken = 3,  /* NEW with the name of an active profile of anyone in this world (NameSame) */
    kRefusedUnknown = 4,    /* PICK / DELETE of a number this person has no active profile under */
    kRefusedInUse = 5,      /* DELETE of the profile being played right now */
    kRefusedNumUsed = 6,    /* NEW asking for a number this person has held before (TEST profile=<n> only) */
    kRefusedNotReady = 7    /* a PROFILES request from a connection the notebook has not heard a HELLO from */
};

inline std::string PNum(long long v) { char b[32]; std::sprintf(b, "%lld", v); return b; }

/* ---- identity ---- */
inline std::string ProfileId(const std::string& person, unsigned num) { return person + "." + PNum((long long)num); }
inline std::string SlotKey(const std::string& person, unsigned num) { return num == 1 ? person : ProfileId(person, num); }
inline std::string PersonOfKey(const std::string& key)
{
    const size_t d = key.find('.');
    return d == std::string::npos ? key : key.substr(0, d);
}
inline unsigned NumOfKey(const std::string& key)
{
    const size_t d = key.find('.');
    return d == std::string::npos ? 1u : (unsigned)std::atoi(key.substr(d + 1).c_str());
}
/* The profile id a slot key stands for - "<person>.1" for a bare key, so a log line always names the profile. */
inline std::string ProfileIdOfKey(const std::string& key) { return ProfileId(PersonOfKey(key), NumOfKey(key)); }

/* ---- the host option `profilecap`: "1".."16", written without a leading zero ---- */
inline int CapValueOk(const std::string& v)
{
    if (v.empty() || v.size() > 2 || v[0] < '1' || v[0] > '9') return 0;
    for (size_t i = 0; i < v.size(); ++i) if (v[i] < '0' || v[i] > '9') return 0;
    const int n = std::atoi(v.c_str());
    return n >= (int)kCapMin && n <= (int)kCapMax;
}
inline unsigned CapFromOption(const std::string& v) { return CapValueOk(v) ? (unsigned)std::atoi(v.c_str()) : kCapDefault; }

/* ---- one row of profiles.txt ---- */
struct Row
{
    std::string person;
    unsigned    num;
    int         slot;         /* -1 = bound to no slot yet (made, never admitted) */
    long long   created, lastPlayed;
    int         active;       /* 0 = deleted: kept for good, so its number and slot stay retired */
    int         played;       /* T-201 PP6' (owner 175): 1 = a FINISHED save of this profile reached the world (PROFILES kind 4 SAVED or
                                 the operator's WORLD_SAVED). The never-played verdict; being admitted (slot, lastPlayed) is not playing. */
    std::string name, faction;
    Row() : num(0), slot(-1), created(0), lastPlayed(0), active(1), played(0) {}
};

/* A ROW'S FACTION FIELD. A faction name is whatever the player typed on Kenshi's FACTION tab (any bytes, 1..24 of them, no space at
   either end once trimmed - FactionForRow). The field keeps a name that passes the display-name rule as it is (every row written
   before this form reads unchanged: that rule refuses '#'); any other name - '#', ';', a tab, bytes beyond ASCII - is written as '#'
   followed by two upper-case hex digits per byte, so profiles.txt holds every name and gives back exactly the name typed. A world folder
   whose rows may carry the '#' form is at format 2 (swformat::kWorldFolderFormat): a build that knows only format 1 refuses to open or
   write that folder at all, so it never drops such a row as unreadable and never writes the file without it. */
inline bool FactionNameFits(const std::string& f)
{
    return !f.empty() && f.size() <= coopcfg::kCfgPlayerNameFieldMax && f[0] != ' ' && f[f.size() - 1] != ' ';
}
inline std::string FactionFieldFormat(const std::string& f)
{
    if (coopworld::DisplayNameOk(f, 0)) return f;
    static const char hex[] = "0123456789ABCDEF";
    std::string o("#");
    for (size_t i = 0; i < f.size(); ++i) { const unsigned char c = (unsigned char)f[i]; o += hex[c >> 4]; o += hex[c & 15]; }
    return o;
}
inline int HexDigitValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
/* False = the field is neither a plain name nor a '#' hex form of a name that fits (the row is unreadable). */
inline bool FactionFieldParse(const std::string& field, std::string* out)
{
    if (field.empty() || field[0] != '#')
    {
        if (!coopworld::DisplayNameOk(field, 0)) return false;
        *out = field;
        return true;
    }
    if (field.size() < 3 || (field.size() - 1) % 2 != 0) return false;
    std::string f;
    for (size_t i = 1; i + 1 < field.size(); i += 2)
    {
        const int hi = HexDigitValue(field[i]), lo = HexDigitValue(field[i + 1]);
        if (hi < 0 || lo < 0) return false;
        f += (char)(unsigned char)(hi * 16 + lo);
    }
    if (!FactionNameFits(f)) return false;
    *out = f;
    return true;
}

inline std::string RowFormat(const Row& r)
{
    return "prof1\t" + r.person + "\t" + PNum((long long)r.num) + "\t" + PNum((long long)r.slot) + "\t" + PNum(r.created)
         + "\t" + PNum(r.lastPlayed) + "\t" + (r.active ? "active" : "deleted") + "\t" + r.name + "\t" + FactionFieldFormat(r.faction)
         + (r.played ? "\tplayed" : "\tunplayed");   /* T-201 PP6' (owner 175): field 10 */
}
inline int RowParse(const std::string& line, Row* out)
{
    std::vector<std::string> f;
    size_t at = 0;
    for (;;)
    {
        const size_t t = line.find('\t', at);
        f.push_back(line.substr(at, t == std::string::npos ? std::string::npos : t - at));
        if (t == std::string::npos) break;
        at = t + 1;
    }
    if ((f.size() != 9 && f.size() != 10) || f[0] != "prof1") return 0;
    std::string& tail = f[f.size() - 1];
    if (!tail.empty() && tail[tail.size() - 1] == '\r') tail.erase(tail.size() - 1);
    std::string& fac = f[8];
    /* T-201 PP6' (owner 175): field 10 is played|unplayed. A 9-field line (written before it) is played once it was admitted (its
       slot bound) - the rule the build that wrote it used. */
    int played = -1;
    if (f.size() == 10) { if (f[9] == "played") played = 1; else if (f[9] == "unplayed") played = 0; else return 0; }
    if (!coopstore::PlayerIdOk(f[1])) return 0;
    const int n = std::atoi(f[2].c_str());
    if (n < 1 || n > (int)kNumMax) return 0;
    const int s = std::atoi(f[3].c_str());
    if (s < -1 || s >= coopstore::kSlotLifetimeMax) return 0;
    if (f[6] != "active" && f[6] != "deleted") return 0;
    std::string facName;
    if (!coopworld::DisplayNameOk(f[7], 0) || !FactionFieldParse(fac, &facName)) return 0;
    Row r;
    r.person = f[1]; r.num = (unsigned)n; r.slot = s;
    r.created = (long long)std::atof(f[4].c_str()); r.lastPlayed = (long long)std::atof(f[5].c_str());
    r.active = f[6] == "active" ? 1 : 0; r.name = f[7]; r.faction = facName;
    r.played = played >= 0 ? played : (s >= 0 ? 1 : 0);
    *out = r;
    return 1;
}

/* ---- T-368 (owner 265-270, 472): ONE NAME RULE FOR PLAYER NAMES AND FACTION NAMES IN A WORLD ----
   Two names are the same name when they match with spaces trimmed from both ends, every run of spaces inside counted as one, and
   ASCII letters compared without case (every other byte compared as it is): "Anna", "anna" and " Anna " are one name. Player names (a profile's name) and faction names
   are two separate lists; each is unique among the world's ACTIVE profiles, whoever they belong to and whether or not they are
   connected. A deleted profile's two names are free again. */
inline std::string NameKey(const std::string& s)
{
    std::string o;
    bool space = false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        if (c == ' ') { space = !o.empty(); continue; }
        if (space) { o += ' '; space = false; }
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        o += c;
    }
    return o;
}
inline bool NameSame(const std::string& a, const std::string& b) { return NameKey(a) == NameKey(b); }
/* Is this player name an active profile's name, anyone's? */
inline bool PlayerNameTaken(const std::vector<Row>& rows, const std::string& name)
{
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].active && NameSame(rows[i].name, name)) return true;
    return false;
}
/* The first active row other than `except` (-1 = none excepted) whose faction name is this one; -1 = nobody's. */
inline int FactionHolder(const std::vector<Row>& rows, const std::string& faction, int except)
{
    for (size_t i = 0; i < rows.size(); ++i) if ((int)i != except && rows[i].active && NameSame(rows[i].faction, faction)) return (int)i;
    return -1;
}
/* A new player's faction (owner 472): "Nameless <n>", n the lowest number no other active profile's faction name uses. */
const char* const kEngineFactionDefault = "Nameless";   /* the name Kenshi gives every new game's player faction */
inline std::string NamelessName(unsigned n) { return std::string(kEngineFactionDefault) + " " + PNum((long long)n); }
inline std::string NamelessFree(const std::vector<Row>& rows, int except)
{
    for (unsigned n = 1; n <= (unsigned)rows.size() + 1; ++n)
        if (FactionHolder(rows, NamelessName(n), except) < 0) return NamelessName(n);
    return NamelessName((unsigned)rows.size() + 1);   /* not reached: rows.size() + 1 numbers cannot all be held by fewer rows */
}
/* "Nameless <n>" exactly as NamelessName writes it (n from 1, no leading zero). */
inline bool IsNamelessNumbered(const std::string& f)
{
    const std::string base = std::string(kEngineFactionDefault) + " ";
    if (f.size() <= base.size() || f.compare(0, base.size(), base) != 0 || f[base.size()] == '0') return false;
    for (size_t i = base.size(); i < f.size(); ++i) if (f[i] < '0' || f[i] > '9') return false;
    return true;
}
/* In a world whose names clashed before this rule, the profile made first keeps the name: is another active row holding this faction
   name older than row i (made earlier, or made in the same second and listed first)? */
inline bool FactionHeldEarlier(const std::vector<Row>& rows, const std::string& faction, int i)
{
    for (size_t j = 0; j < rows.size(); ++j)
    {
        if ((int)j == i || !rows[j].active || !NameSame(rows[j].faction, faction)) continue;
        if (rows[j].created < rows[(size_t)i].created || (rows[j].created == rows[(size_t)i].created && (int)j < i)) return true;
    }
    return false;
}

/* ---- decisions ---- */
inline int FindRow(const std::vector<Row>& rows, const std::string& person, unsigned num)
{
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].person == person && rows[i].num == num) return (int)i;
    return -1;
}
inline unsigned CountActive(const std::vector<Row>& rows, const std::string& person)
{
    unsigned n = 0;
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].person == person && rows[i].active) ++n;
    return n;
}
/* One past the highest number this person has EVER held - a deleted profile's number is never given out again. */
inline unsigned NextNum(const std::vector<Row>& rows, const std::string& person)
{
    unsigned hi = 0;
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].person == person && rows[i].num > hi) hi = rows[i].num;
    return hi + 1;
}
inline int NewDecide(const std::vector<Row>& rows, const std::string& person, const std::string& name, unsigned cap,
                     unsigned wantNum, unsigned* numOut)
{
    if (!coopworld::DisplayNameOk(name, 0)) return kRefusedName;
    if (CountActive(rows, person) >= cap) return kRefusedCap;
    if (PlayerNameTaken(rows, name)) return kRefusedNameTaken;   /* T-368: anyone's active profile, by the world's name rule */
    unsigned n = wantNum;
    if (n == 0) n = NextNum(rows, person);
    else if (FindRow(rows, person, n) >= 0) return kRefusedNumUsed;
    if (n > kNumMax) return kRefusedNumUsed;
    *numOut = n;
    return kOk;
}
inline int PickDecide(const std::vector<Row>& rows, const std::string& person, unsigned num)
{
    const int i = FindRow(rows, person, num);
    return (i >= 0 && rows[(size_t)i].active) ? kOk : kRefusedUnknown;
}
inline int DeleteDecide(const std::vector<Row>& rows, const std::string& person, unsigned num, int playingNow)
{
    const int i = FindRow(rows, person, num);
    if (i < 0 || !rows[(size_t)i].active) return kRefusedUnknown;
    return playingNow ? kRefusedInUse : kOk;
}
/* T-201 PP6' (owner 178a): THE ONE DELETE - the notebook's PROFILES DELETE and the host's DELETE on HOST GAME -> CHANGE (the world not
   running: the host's game edits that world's profiles.txt) both apply it. kOk = the row is marked deleted, kept for good (its number
   and slot stay retired; its records and buildings stay). */
inline int DeleteApply(std::vector<Row>* rows, const std::string& person, unsigned num, int playingNow)
{
    const int v = DeleteDecide(*rows, person, num, playingNow);
    if (v == kOk) (*rows)[(size_t)FindRow(*rows, person, num)].active = 0;
    return v;
}
/* profiles.txt, line by line, as the notebook reads it (LoadProfiles) and writes it (WriteProfiles): an unreadable line, or a second
   line for the same person + number, is dropped (false). */
inline bool RowsAddLine(std::vector<Row>* rows, const std::string& line)
{
    Row r;
    if (line.empty() || !RowParse(line, &r) || FindRow(*rows, r.person, r.num) >= 0) return false;
    rows->push_back(r);
    return true;
}
inline std::vector<Row> RowsParseFile(const std::string& text, int* dropped)
{
    std::vector<Row> rows;
    size_t a = 0;
    while (a < text.size())
    {
        size_t e = text.find('\n', a);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(a, e - a);
        if (!line.empty() && line != "\r" && !RowsAddLine(&rows, line) && dropped) ++*dropped;
        a = e + 1;
    }
    return rows;
}
inline std::string RowsFormatFile(const std::vector<Row>& rows)
{
    std::string body;
    for (size_t i = 0; i < rows.size(); ++i) body += RowFormat(rows[i]) + "\n";
    return body;
}
/* T-222 (downgrade hazard): a prof1 line with MORE fields than this build knows (10) was written by a newer build. RowParse drops it,
   so writing the list back would erase that profile: a file holding one is READ-ONLY to this build (the notebook's WriteProfiles and the
   host's CHANGE DELETE both refuse to rewrite it). */
inline bool RowLineNewer(const std::string& line)
{
    if (line.compare(0, 6, "prof1\t") != 0) return false;
    size_t tabs = 0;
    for (size_t i = 0; i < line.size(); ++i) if (line[i] == '\t') ++tabs;
    return tabs + 1 > 10;
}
inline int RowsNewerLines(const std::string& text)
{
    int n = 0;
    size_t a = 0;
    while (a < text.size())
    {
        size_t e = text.find('\n', a);
        if (e == std::string::npos) e = text.size();
        if (RowLineNewer(text.substr(a, e - a))) ++n;
        a = e + 1;
    }
    return n;
}

/* T-220 PP6 fold 2 - THE WORLD LOCK. The world helper (coop-store) holds <world folder>\server.lock open with no sharing for its
   whole life (Windows closes it when the process ends, however it ends); the host's CHANGE DELETE edits that world's profiles.txt only
   while it can take the same lock itself (tried once, never waited for). */
const char* const kWorldLockFile = "server.lock";
enum WorldLockVerdict { kLockTaken = 0, kLockHeld = 1, kLockFailed = 2 };
/* opened = CreateFile gave a handle; err = GetLastError() when it did not. 32 ERROR_SHARING_VIOLATION / 33 ERROR_LOCK_VIOLATION = a
   running helper (or another editor) holds it. Anything else = the lock could not be taken for another reason - also no edit. */
inline int WorldLockDecide(bool opened, unsigned long err)
{
    if (opened) return kLockTaken;
    return (err == 32ul || err == 33ul) ? kLockHeld : kLockFailed;
}
inline const char* WorldLockVerdictName(int v) { return v == kLockTaken ? "taken" : v == kLockHeld ? "held" : "failed"; }
/* Owner decision 183 (2026-09-29, option a) - THE HELPER CLOSES WHEN THE KENSHI THAT STARTED IT IS GONE. The plugin passes its own
   process id as --parent <pid>; a helper started without it (a standalone world server, the harness's runs) runs as before.
   ParentPidParse: 1 = s is a whole decimal number 1..4294967295 (written into *pid); 0 = anything else (the option is ignored). */
inline int ParentPidParse(const char* s, unsigned long* pid)
{
    if (s == 0 || *s == 0) return 0;
    unsigned long long v = 0;
    for (const char* c = s; *c != 0; ++c)
    {
        if (*c < '0' || *c > '9') return 0;
        v = v * 10ull + (unsigned long long)(*c - '0');
        if (v > 0xFFFFFFFFull) return 0;
    }
    if (v == 0) return 0;
    *pid = (unsigned long)v;
    return 1;
}
/* opened = OpenProcess(SYNCHRONIZE) gave a handle; err = GetLastError() when it did not. Watch = wait on it; Gone = 87
   ERROR_INVALID_PARAMETER, no such process: the Kenshi that started this helper already ended - close now; NotWatched = any other
   error (5 access denied, ...): the helper cannot tell, so it runs as before and says so. */
enum ParentWatchVerdict { kParentWatch = 0, kParentGone = 1, kParentNotWatched = 2 };
inline int ParentWatchDecide(bool opened, unsigned long err)
{
    if (opened) return kParentWatch;
    return err == 87ul ? kParentGone : kParentNotWatched;
}

/* T-220 PP6 fold 2 - SAVED (kind 4) IS TOLD FOR THE PROFILE'S OWN FINISHED SAVE ONLY: the save's folder is the pick's decided folder
   (a save the redirect skipped went under its own name) and that folder's quick.save is on disk. Folder names compare as Windows does
   (ASCII case ignored). */
inline std::string FolderBaseName(const std::string& dir)
{
    size_t e = dir.size();
    while (e > 0 && (dir[e - 1] == '\\' || dir[e - 1] == '/')) --e;
    size_t b = e;
    while (b > 0 && dir[b - 1] != '\\' && dir[b - 1] != '/') --b;
    return dir.substr(b, e - b);
}
inline bool FolderNameSame(const std::string& a, const std::string& b)
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
inline int SavedTellDecide(unsigned pickedNum, const std::string& profFolder, const std::string& savedDir, bool quickSave)
{
    return (pickedNum != 0 && !profFolder.empty() && FolderNameSame(profFolder, FolderBaseName(savedDir)) && quickSave) ? 1 : 0;
}
/* SAVED is OWED from the profile's own finished save until the world's list shows the row played. Send() only queues, so it is told
   again on every link (once per link generation) while owed; the world ignores repeats (kSavedSame). A save that was never told
   before a quit is still on disk: at the next WELCOME of that pick, a never-played row whose folder was FOUND by its key file and holds
   quick.save is owed again - the save folder is the record that persists, and nothing else is written for it. */
inline int SavedOwedAtWelcome(bool neverPlayed, bool folderFound, bool quickSave) { return (neverPlayed && folderFound && quickSave) ? 1 : 0; }
inline int SavedSendDecide(int owed, long sentGen, long linkGen) { return (owed != 0 && sentGen != linkGen) ? 1 : 0; }
inline int SavedOwedAfterList(int owed, const std::vector<Row>& rows, unsigned num)
{
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].num == num && rows[i].played) return 0;
    return owed;
}

/* What a game with no screen yet does with the list it was given. TEST profile=<n>: pick n, or make it. Otherwise: pick the
   profile played most recently (ties: the lowest number), or make the first one - which is today's one-profile behaviour. */
enum AutoAction { kAutoNone = 0, kAutoPick = 1, kAutoNew = 2 };
inline int AutoPickDecide(const std::vector<Row>& active, unsigned testNum, unsigned* num)
{
    if (testNum != 0)
    {
        *num = testNum;
        for (size_t i = 0; i < active.size(); ++i) if (active[i].num == testNum) return kAutoPick;
        return kAutoNew;
    }
    int best = -1;
    for (size_t i = 0; i < active.size(); ++i)
    {
        if (best < 0) { best = (int)i; continue; }
        const Row& b = active[(size_t)best];
        if (active[i].lastPlayed > b.lastPlayed || (active[i].lastPlayed == b.lastPlayed && active[i].num < b.num)) best = (int)i;
    }
    if (best >= 0) { *num = active[(size_t)best].num; return kAutoPick; }
    *num = 0;
    return kAutoNew;
}
/* The name a profile made without a screen gets: "Profile <n>" for a TEST number, else the player's name, else "Profile". */
inline std::string AutoNewName(const std::string& playerName, unsigned testNum)
{
    if (testNum != 0) return "Profile " + PNum((long long)testNum);
    if (coopworld::DisplayNameOk(playerName, 0)) return playerName;
    return "Profile";
}

/* T-201 PP5 (owner 167, 168, 17; player-path-plan.md PP5): EVERY MULTIPLAYER SAVE GOES TO ITS PROFILE'S ONE FOLDER.
   The folder is FOUND by its multiplayer-world.key (world key + profile id), never by its name; the name rule below is used only to
   create one: "<World display name> - Multiplayer - <Profile name>" for every profile (owner 167), both parts made folder-safe the
   same way, " 2", " 3" ... only while that name is taken by something that is not this (world, profile)'s folder. A folder found
   under another name is renamed ONCE to the rule's name at the title (owner 168) unless that name is already another folder. */
const char* const kSaveFolderMiddle = " - Multiplayer - ";
/* One part of the name made folder-safe: a character Windows refuses in a name (< > : " / \ | ? * and control characters), '~'
   (a short 8.3 name could answer for another folder) and anything outside printable ASCII become '_'; spaces and dots at either end
   go (Windows drops a trailing dot or space itself); nothing left is "_". */
inline std::string SaveNamePart(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const unsigned char c = (unsigned char)s[i];
        const bool bad = c < 0x20 || c > 0x7E || std::strchr("<>:\"/\\|?*~", (int)c) != 0;
        o += bad ? '_' : (char)c;
    }
    size_t a = 0, b = o.size();
    while (a < b && (o[a] == ' ' || o[a] == '.')) ++a;
    while (b > a && (o[b - 1] == ' ' || o[b - 1] == '.')) --b;
    o = o.substr(a, b - a);
    return o.empty() ? std::string("_") : o;
}
inline std::string SaveFolderBaseName(const std::string& worldDisplay, const std::string& profileName)
{
    return SaveNamePart(worldDisplay) + kSaveFolderMiddle + SaveNamePart(profileName);
}
inline bool SameFolderName(const std::string& a, const std::string& b) { return coopcfg::CfgLower(a) == coopcfg::CfgLower(b); }   /* Windows names ignore case */
/* One entry of Kenshi's save root and what its key file says (keyed false = no readable multiplayer-world.key, or a plain file). */
struct SaveDirSeen
{
    std::string name;
    bool keyed;
    std::string world;            /* the key file's world key */
    std::string profile;          /* its profile id; "" = a kcworld1 file, profile 1's (as RecycleDecide / LoadProfileMismatch read it) */
    std::string worldId;          /* T-490: field 8, the world id ("" = none) */
    bool hasGen; unsigned gen;    /* restore1a field 5, the save number */
    unsigned long long saveTime;  /* T-201 PP5 fold (M1): the folder's quick.save last write (FILETIME), 0 = none - the twin ranking */
    SaveDirSeen() : keyed(false), hasGen(false), gen(0), saveTime(0ULL) {}
};
/* THIS WORLD'S FOLDER? Its key file names the world, compared as folder names (case ignored). The one world test of the key scan
   below and of a world's leftovers (WorldLeftoversFind), so a folder the scan would load for a world is a folder its delete takes. */
/* T-490: WHICH WORLD OF THIS NAME the link is to - the id its WELCOME carried ("" = none: the name alone decides), whether that
   world was upgraded, and the slack for adopting saves made before ids (coopworld::SaveIdentityDecide). */
struct WorldIdent
{
    std::string id; bool upgraded; long long slackSec;
    WorldIdent() : upgraded(false), slackSec(coopworld::kWorldIdAdoptSlackSec) {}
};
inline int SaveDirIdentity(const SaveDirSeen& d, const WorldIdent* ident)
{
    if (ident == 0) return coopworld::kIdUnknown;
    return coopworld::SaveIdentityDecide(d.worldId, coopworld::FileTimeToUnix(d.saveTime), ident->id, coopworld::WorldIdCreated(ident->id),
                                         ident->upgraded, ident->slackSec);
}
/* The name test alone: this world's name, or a same-name world's. */
inline bool SaveDirNamesWorld(const SaveDirSeen& d, const std::string& worldKey)
{
    return d.keyed && coopworld::WorldFolderName(d.world) == coopworld::WorldFolderName(worldKey);
}
/* ident = the link's world identity (0 = the name alone). A same-name folder of another world is not this world's. */
inline bool SaveDirIsWorlds(const SaveDirSeen& d, const std::string& worldKey, const WorldIdent* ident = 0)
{
    return SaveDirNamesWorld(d, worldKey) && coopworld::SaveIdentityMayUse(SaveDirIdentity(d, ident));
}
inline bool SaveDirIsProfiles(const SaveDirSeen& d, const std::string& worldKey, const std::string& profileId, unsigned num, const WorldIdent* ident = 0)
{
    if (!SaveDirIsWorlds(d, worldKey, ident)) return false;
    return d.profile.empty() ? num == 1 : d.profile == profileId;
}
/* THE KEY SCAN: the index of this (world, profile)'s folder, or -1 (none: the name rule makes one). *owned = how many folders carry
   this world and profile. Two or more (T-201 PP5 fold M1): the newest quick.save wins - a key file's time is rewritten when a WELCOME
   marks the folder, not by a save, and a joiner's folders carry no save number - then the higher save number, then the lower name. Another world's
   folder and another profile's are never chosen. T-490: with the link's identity (ident), a same-name folder of another world is never
   chosen either (*otherWorld counts them, for this profile), and a folder carrying this world's id wins over one adopted or unknown. */
inline int SaveFolderScanChoose(const std::vector<SaveDirSeen>& dirs, const std::string& worldKey, const std::string& profileId, unsigned num, int* owned,
                                const WorldIdent* ident = 0, int* otherWorld = 0)
{
    int best = -1, n = 0, other = 0;
    for (size_t i = 0; i < dirs.size(); ++i)
    {
        if (!SaveDirIsProfiles(dirs[i], worldKey, profileId, num, ident))
        {
            if (SaveDirIsProfiles(dirs[i], worldKey, profileId, num)) ++other;   /* this name and profile, another world */
            continue;
        }
        ++n;
        if (best < 0) { best = (int)i; continue; }
        const SaveDirSeen& b = dirs[(size_t)best];
        const int si = SaveDirIdentity(dirs[i], ident) == coopworld::kIdSame ? 1 : 0, sb = SaveDirIdentity(b, ident) == coopworld::kIdSame ? 1 : 0;
        if (si != sb) { if (si > sb) best = (int)i; continue; }
        const unsigned gi = dirs[i].hasGen ? dirs[i].gen : 0u, gb = b.hasGen ? b.gen : 0u;
        if (dirs[i].saveTime > b.saveTime || (dirs[i].saveTime == b.saveTime && (gi > gb || (gi == gb && coopcfg::CfgLower(dirs[i].name) < coopcfg::CfgLower(b.name)))))
            best = (int)i;
    }
    if (owned != 0) *owned = n;
    if (otherWorld != 0) *otherWorld = other;
    return best;
}
/* A WORLD'S LEFTOVERS ON THIS COMPUTER - what DELETE WORLD sends to the Recycle Bin with the world. A world's saves are found by its
   NAME alone (the key scan above), so a later world given the same name would load them and be refused as older than the world.
   folders = every save folder whose key file names this world, any profile and any folder name; playing = such a folder that is the
   one this game plays from (keep: the settings' slot, the picked profile's folder), never moved; playingIds = those folders' profiles,
   whose records stores are in use with them and are never moved either (same id rule as below).
   worldKeys = the name asked for, then each other spelling a key file carries; profileIds = each folder's profile (a key
   file naming none = this person's profile 1, as the key scan reads it), then this person's numbers 1..sweep, so a records store whose
   save folder is already gone is still found (coopown::PlayerStoreNamesFor makes the store names). No world name = nothing. */
const unsigned kWorldLeftoverSweep = 64;   /* this person's profile numbers tried for records stores - numbers are never reused, so it reaches past the cap */
struct WorldLeftovers
{
    std::vector<size_t> folders;          /* indexes into the scan: to the Recycle Bin */
    std::vector<size_t> playing;          /* indexes into the scan: this world's, but the folder this game plays from */
    std::vector<std::string> playingIds;  /* each playing folder's profile once, never "": its records stores stay with it */
    std::vector<std::string> worldKeys;   /* each spelling once, never "" */
    std::vector<std::string> profileIds;  /* each id once, never "" */
    std::vector<size_t> keptNoId;         /* T-490, by id: folders of this name whose key file carries no world id - kept, the caller logs them */
};
inline void LeftoverAddOnce(std::vector<std::string>* v, const std::string& s)
{
    if (s.empty()) return;
    for (size_t i = 0; i < v->size(); ++i) if ((*v)[i] == s) return;
    v->push_back(s);
}
/* T-490: worldId given (non-null) = BY ID - only folders whose key file carries that id are the world's; a folder of this name with no
   id is kept (keptNoId) and one with another id is another world's, untouched. An empty id then takes no folder. The records stores
   are then named from the id (coopown::PlayerStoreNamesFor with it); stores named before ids are kept. */
inline WorldLeftovers WorldLeftoversFind(const std::vector<SaveDirSeen>& dirs, const std::string& worldName, const std::vector<std::string>& keep,
                                         const std::string& person, unsigned sweep, const std::string* worldId = 0)
{
    WorldLeftovers o;
    if (worldName.empty()) return o;
    LeftoverAddOnce(&o.worldKeys, worldName);
    for (size_t i = 0; i < dirs.size(); ++i)
    {
        if (!SaveDirNamesWorld(dirs[i], worldName)) continue;
        if (worldId != 0 && dirs[i].worldId.empty()) { o.keptNoId.push_back(i); continue; }
        if (worldId != 0 && (worldId->empty() || dirs[i].worldId != *worldId)) continue;
        bool played = false;
        for (size_t k = 0; k < keep.size() && !played; ++k) if (!keep[k].empty() && SameFolderName(dirs[i].name, keep[k])) played = true;
        const std::string id = !dirs[i].profile.empty() ? dirs[i].profile : (person.empty() ? std::string() : ProfileId(person, 1));
        if (played) { o.playing.push_back(i); LeftoverAddOnce(&o.playingIds, id); } else o.folders.push_back(i);
        LeftoverAddOnce(&o.worldKeys, dirs[i].world);
        LeftoverAddOnce(&o.profileIds, id);
    }
    if (!person.empty()) for (unsigned n = 1; n <= sweep; ++n) LeftoverAddOnce(&o.profileIds, ProfileId(person, n));
    return o;
}
/* T-490: A PROFILE DELETE'S VIEW OF THE SAVE ROOT - only folders carrying this world's id stay this world's. A same-name folder with no
   id (listed in *keptNoId, kept - the DELETE WORLD rule) or with another id reads as unkeyed here, so the key scan never picks it. An
   empty id leaves no folder this world's. */
inline std::vector<SaveDirSeen> SaveDirsOfWorldId(const std::vector<SaveDirSeen>& dirs, const std::string& worldKey, const std::string& worldId,
                                                  std::vector<size_t>* keptNoId)
{
    std::vector<SaveDirSeen> out(dirs);
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (!SaveDirNamesWorld(out[i], worldKey)) continue;
        if (!worldId.empty() && out[i].worldId == worldId) continue;
        if (out[i].worldId.empty() && keptNoId != 0) keptNoId->push_back(i);
        out[i].keyed = false;
    }
    return out;
}
/* THE NAME RULE (owner 167) - the name a NEW folder gets. */
inline std::string ProfileSaveFolderDecide(const std::string& worldDisplay, const std::string& profileName, const std::vector<SaveDirSeen>& dirs,
                                           const std::string& worldKey, const std::string& profileId, unsigned num, const WorldIdent* ident = 0)   /* T-490: another world's same-name folder is taken */
{
    const std::string base = SaveFolderBaseName(worldDisplay, profileName);
    for (unsigned k = 1; k < 10000; ++k)
    {
        const std::string cand = k == 1 ? base : base + " " + PNum((long long)k);
        bool taken = false;
        for (size_t i = 0; i < dirs.size() && !taken; ++i)
            if (SameFolderName(dirs[i].name, cand) && !SaveDirIsProfiles(dirs[i], worldKey, profileId, num, ident)) taken = true;
        if (!taken) return cand;
    }
    return base;
}
/* THE REDIRECT (decision 17: every save caller, no name exemptions): never in single player; never without a picked profile whose
   folder is decided; always otherwise. */
inline bool SaveRedirectDecide(bool roleSingle, bool profilePicked, const std::string& folder)
{
    return !roleSingle && profilePicked && !folder.empty();
}
/* T-201 PP5 fold (H1): WHAT MAY LOAD WHILE A PROFILE'S FOLDER IS DECIDED. Kenshi's saveGame deletes a save's folder before writing a
   world whose open save has another name into it, so once the folder is decided (connected, profile picked) any other world loaded
   at the title would REPLACE the profile's save at its first save. Allowed then: that folder itself, or NEW GAME for a profile whose
   folder has never been saved (no quick.save). Refused: every other load and every import. Single player / no folder: unchanged.
   The request kinds are the engine's own SaveManager+0xA0 codes. */
enum ProfileRequestKind { kProfReqLoad = 2, kProfReqImport = 3, kProfReqNewGame = 4 };
enum ProfileLoadVerdict { kProfLoadAllow = 0, kProfLoadRefuse = 1 };
/* the last part of a save name or path ("C:/x/name/" -> "name") */
inline std::string SaveNameLastPart(const std::string& s)
{
    size_t e = s.size();
    while (e > 0 && (s[e - 1] == '/' || s[e - 1] == '\\')) --e;
    const size_t a = s.find_last_of("/\\", e == 0 ? 0 : e - 1);
    return (a == std::string::npos || e == 0) ? s.substr(0, e) : s.substr(a + 1, e - a - 1);
}
inline int ProfileLoadDecide(bool roleSingle, const std::string& folder, int kind, const std::string& name, bool folderSaved)
{
    if (roleSingle || folder.empty()) return kProfLoadAllow;
    if (kind == kProfReqNewGame) return folderSaved ? kProfLoadRefuse : kProfLoadAllow;
    if (kind == kProfReqLoad && SameFolderName(SaveNameLastPart(name), folder)) return kProfLoadAllow;
    return kProfLoadRefuse;
}
/* The refusal's words (approved; the title box is CAN'T LOAD - panelstatus.h NoticeTitle). */
const char* const kLoadAutoText = "In multiplayer your game loads automatically. Use HOST GAME or JOIN GAME in the MULTIPLAYER menu.";
/* T-201 PP5 fold (H1, second guard): A SAVE IS REDIRECTED ONLY ONTO THE OPEN SAVE. openName = SaveFileSystem+0x120, the save
   the world was loaded from ("" = none open: a new game). Do = the open save is the profile folder, or none is open; OwnName =
   another save is open - the save keeps the name it asked for; Refuse = another (or an unreadable) open save asked to be written
   INTO the profile folder - not saved at all, so the profile folder is never deleted for another world. */
enum SaveRedirectOpenVerdict { kRedirectDo = 0, kRedirectOwnName = 1, kRedirectRefuse = 2 };
inline int SaveRedirectOpenDecide(bool openRead, const std::string& openName, const std::string& folder, const std::string& asked)
{
    if (openRead)
    {
        const std::string o = SaveNameLastPart(openName);
        if (o.empty() || SameFolderName(o, folder)) return kRedirectDo;
    }
    return SameFolderName(SaveNameLastPart(asked), folder) ? kRedirectRefuse : kRedirectOwnName;
}
/* PP6 fold-2 verification F2: THE ENGINE'S OWN CONTINUE / LOAD TOOK THE DECIDED SAVE. openName = SaveFileSystem+0x120 (the same
   read as the redirect above). true = it names the decided folder - the one press is done (the right save is loading); false = no
   save open, another save, or unreadable - the press failed. */
inline bool OpenSaveIsDecidedFolder(bool openRead, const std::string& openName, const std::string& folder)
{
    if (!openRead || folder.empty()) return false;
    const std::string o = SaveNameLastPart(openName);
    return !o.empty() && SameFolderName(o, folder);
}
/* T-239 (owner 191 A / 192): in multiplayer the player never saves, loads or starts a new game by hand. titleUp = TitleScreenUpPod()
   (1 up, 0 down, -1 unreadable); mpFolder = SaveRedirectWanted() (a multiplayer profile's save folder is decided - it stays decided
   while the connection is down, and is never decided in single player). Blocked only with the title DOWN in a multiplayer world: at
   the title the mod drives its own loads and new game, and an unreadable title blocks nothing. */
inline bool HandSaveBlockedDecide(int titleUp, bool mpFolder) { return titleUp == 0 && mpFolder; }
/* T-201 PP6' (player-path-plan.md PP6 + the one-press revision): WHAT THE ONE PRESS DOES ONCE THE WORLD HAS ADMITTED THE PICKED
   PROFILE. folderFound = this (world, profile)'s save folder was FOUND by its key file (SaveFolderScanChoose, not the name rule's new
   name - a folder of another world or profile under the rule's name is never found); quickSave = <save root>\<folder>\quick.save
   exists, the engine's own saveExists test (0x36C4B0) - a load of a folder without it would fail AFTER the pump deleted the title
   (T243 crash class), so a load is decided only with it; neverPlayed = the profile's row is not marked played (owner 175: no
   FINISHED save of it has reached the world). Host and joiner decide alike.
     Load    - the folder with its quick.save
     NewGame - a never-played profile with nothing to load here: Kenshi's NEW GAME window (the PP5 gate allows it: no quick.save)
     Missing - played before, no save of it on this computer: the CAN'T LOAD box (kSaveNotHereText), never a silent new game, which
               would put a second crew into the profile */
enum AutoLoadVerdict { kAutoLoadLoad = 0, kAutoLoadNewGame = 1, kAutoLoadMissing = 2 };
inline int AutoLoadDecide(bool folderFound, bool quickSave, bool neverPlayed)
{
    if (folderFound && quickSave) return kAutoLoadLoad;
    return neverPlayed ? kAutoLoadNewGame : kAutoLoadMissing;
}
inline const char* AutoLoadVerdictName(int v) { return v == kAutoLoadLoad ? "load" : v == kAutoLoadNewGame ? "newgame" : "missing"; }
/* T-201 PP6' (owner 134 + 141 + 143): THE PROFILE A HOST PRESS PLAYS. mine = this person's ACTIVE profiles in the world; chosenNum /
   chosenNew = what CHANGE -> SELECT (or its NEW PROFILE) left, 0 / "" = nothing chosen. A chosen row still there wins; a chosen new
   name that one of mine already has (NameSame - the world's NewDecide rule) plays that one; otherwise the one played last; with
   none, a new one named after the player (AutoNewName). */
struct HostProfileChoice { unsigned num; std::string name; int isNew; HostProfileChoice() : num(0), isNew(0) {} };
inline HostProfileChoice HostProfileChoose(const std::vector<Row>& mine, unsigned chosenNum, const std::string& chosenNew, const std::string& playerName)
{
    HostProfileChoice c;
    for (size_t i = 0; i < mine.size() && chosenNum != 0; ++i)
        if (mine[i].num == chosenNum) { c.num = mine[i].num; c.name = mine[i].name; return c; }
    for (size_t i = 0; i < mine.size() && !chosenNew.empty(); ++i)
        if (NameSame(mine[i].name, chosenNew)) { c.num = mine[i].num; c.name = mine[i].name; return c; }
    if (!chosenNew.empty()) { c.name = chosenNew; c.isNew = 1; return c; }
    unsigned num = 0;
    if (AutoPickDecide(mine, 0, &num) == kAutoPick)
        for (size_t i = 0; i < mine.size(); ++i) if (mine[i].num == num) { c.num = num; c.name = mine[i].name; return c; }
    c.name = AutoNewName(playerName, 0);
    c.isNew = 1;
    return c;
}
/* One person's active rows out of a world's profiles.txt text (the host reads its own world's file before the world runs). */
inline std::vector<Row> PersonActiveRows(const std::string& fileText, const std::string& person)
{
    std::vector<Row> out;
    size_t a = 0;
    while (a < fileText.size())
    {
        size_t e = fileText.find('\n', a);
        if (e == std::string::npos) e = fileText.size();
        std::string line = fileText.substr(a, e - a);
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        Row r;
        if (RowParse(line, &r) && r.active && r.person == person) out.push_back(r);
        a = e + 1;
    }
    return out;
}
/* T-368: every active row of a world's profiles.txt text, anyone's (the host's NEW PROFILE on CHANGE checks the name against them). */
inline std::vector<Row> ActiveRowsOfFile(const std::string& fileText)
{
    std::vector<Row> out;
    size_t a = 0;
    while (a < fileText.size())
    {
        size_t e = fileText.find('\n', a);
        if (e == std::string::npos) e = fileText.size();
        std::string line = fileText.substr(a, e - a);
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        Row r;
        if (RowParse(line, &r) && r.active) out.push_back(r);
        a = e + 1;
    }
    return out;
}
/* THE ONE-TIME RENAME (owner 168). None = the folder already has the rule's name (any case, or a " <n>" form the rule made);
   Do = rename it to target; KeepTaken = target is already another entry - the old name stays. */
enum RenameVerdict { kRenameNone = 0, kRenameDo = 1, kRenameKeepTaken = 2 };
inline int SaveFolderRenameDecide(const std::string& current, const std::string& target, const std::vector<SaveDirSeen>& dirs)
{
    if (SameFolderName(current, target)) return kRenameNone;
    if (current.size() > target.size() + 1 && SameFolderName(current.substr(0, target.size()), target) && current[target.size()] == ' ')
    {
        bool digits = true;
        for (size_t i = target.size() + 1; i < current.size(); ++i) if (current[i] < '0' || current[i] > '9') digits = false;
        if (digits && current[target.size() + 1] != '0') return kRenameNone;
    }
    for (size_t i = 0; i < dirs.size(); ++i) if (SameFolderName(dirs[i].name, target)) return kRenameKeepTaken;
    return kRenameDo;
}

/* A deleted profile's save folder goes to the Recycle Bin (owner rule D7) only when it is plainly that profile's - it exists,
   its key file names THIS world and THIS profile (a kcworld1 file, which names no profile, only for profile 1), it is not the
   folder this game plays from - and only where Windows really recycles: a local fixed drive with a Recycle Bin and a folder
   small enough for it (review-prof1 1). Windows DELETES FOR GOOD where it cannot recycle, so anything else keeps the folder. */
enum RecycleVerdict { kRecycleOk = 0, kRecycleNoFolder, kRecycleNoKey, kRecycleOtherWorld, kRecycleOtherProfile, kRecyclePlaying,
                      kRecycleNoBin, kRecycleTooBig };
inline int RecycleDecide(bool folderExists, bool keyFileRead, const std::string& keyInFile, const std::string& myWorld,
                         const std::string& profileInFile, const std::string& deletedProfileId, unsigned deletedNum,
                         const std::string& folder, const std::string& playingFolder, bool binOk, bool sizeOk)
{
    if (!folderExists) return kRecycleNoFolder;
    if (!keyFileRead) return kRecycleNoKey;
    if (myWorld.empty() || keyInFile != myWorld) return kRecycleOtherWorld;
    if (profileInFile.empty() ? deletedNum != 1 : profileInFile != deletedProfileId) return kRecycleOtherProfile;
    if (folder == playingFolder) return kRecyclePlaying;
    if (!binOk) return kRecycleNoBin;
    if (!sizeOk) return kRecycleTooBig;
    return kRecycleOk;
}

/* THE LOAD CHECK (design s5, review-prof1 3): a save whose key file names ANOTHER profile is refused - loading it would put one
   crew into two slots. A kcworld1 file names no profile and is let through (a save from before profiles is profile 1's). */
inline bool LoadProfileMismatch(const std::string& profileInFile, const std::string& pickedId)
{
    return !profileInFile.empty() && !pickedId.empty() && profileInFile != pickedId;
}

/* NUMBERS ARE NEVER REUSED, EVEN AFTER A LOST profiles.txt WRITE (review-prof1 8). Every key the notebook's slot table holds for
   this person that has no profiles.txt line - a world played before profiles (the bare person id = profile 1), or a line whose
   write was lost - is taken in as an active profile ("Profile <n>", its slot, last played = when it was last seen), so it can be
   picked and NextNum counts it. A TEST profile=2 on an old world therefore makes profile 2 BESIDE the old profile 1. */
inline int AdoptSlotKeys(std::vector<Row>* rows, const std::string& person, const std::map<std::string, int>& slotById,
                         const std::map<std::string, long long>& seenAt, long long now)
{
    int adopted = 0;
    for (std::map<std::string, int>::const_iterator it = slotById.begin(); it != slotById.end(); ++it)
    {
        if (!coopstore::SlotKeyOk(it->first) || PersonOfKey(it->first) != person) continue;
        const unsigned n = NumOfKey(it->first);
        if (n < 1 || n > kNumMax || FindRow(*rows, person, n) >= 0) continue;
        Row r; r.person = person; r.num = n; r.slot = it->second; r.created = now; r.active = 1; r.played = 1;   /* a slot the world kept: it was played */
        std::map<std::string, long long>::const_iterator sa = seenAt.find(it->first);
        r.lastPlayed = sa == seenAt.end() ? 0 : sa->second;
        r.name = "Profile " + PNum((long long)n); r.faction = r.name;
        rows->push_back(r);
        ++adopted;
    }
    return adopted;
}

/* ---- names2a (investigations/names2-design.md Q3): the faction name in a profile's row follows an in-game rename ---- */
/* A row's faction must pass the display-name rule or the notebook drops the WHOLE profile at its next start (RowParse). */
const size_t kRowFactionMax = coopcfg::kCfgPlayerNameFieldMax;   /* 24 */
/* THE CUT - the owner's choice is pending; this is the manager's recommendation, kept in this ONE helper so it can be changed:
   a name longer than the row allows keeps its first kRowFactionMax bytes (never splitting a UTF-8 character). */
inline std::string FactionCutForRow(const std::string& name)
{
    if (name.size() <= kRowFactionMax) return name;
    size_t n = kRowFactionMax;
    while (n > 0 && ((unsigned char)name[n] & 0xC0) == 0x80) --n;
    return name.substr(0, n);
}
/* The row's faction for an in-game name: spaces trimmed from both ends, cut (FactionCutForRow), trailing spaces the cut
   exposed trimmed again. Any text Kenshi's FACTION tab lets a player type stands (the row's field form holds every byte -
   FactionFieldFormat). False = nothing left (empty, or only spaces) - the caller keeps the old value. */
inline bool FactionForRow(const std::string& name, std::string* out)
{
    size_t b = 0, e = name.size();
    while (b < e && name[b] == ' ') ++b;
    while (e > b && name[e - 1] == ' ') --e;
    std::string s = FactionCutForRow(name.substr(b, e - b));
    while (!s.empty() && s[s.size() - 1] == ' ') s.erase(s.size() - 1);
    if (!FactionNameFits(s)) return false;
    *out = s;
    return true;
}
/* kFacBack / kFacMoved / kFacDefault (T-368): the world gives the profile's faction another name - the row takes *facOut and the sender
   is told (the FACTION answer). Back = a RENAME to a name another profile holds: the row keeps its name and the game goes back to it;
   Moved = the world LOADED with a name another profile holds: the row and the game take the lowest free "Nameless <n>"; Default = the
   world LOADED with Kenshi's own default "Nameless" (owner 471: every player's default faction is its own "Nameless <n>"), whoever the
   player is, operator included, played or not: the profile's own "Nameless <n>" (its row's, while no other profile holds that, else
   the lowest free one). Kenshi's plain "Nameless" is therefore never a profile's faction name after a load. */
enum FactionVerdict { kFacSet = 0, kFacSame = 1, kFacRefusedSender = 2, kFacRefusedUnknown = 3, kFacRefusedName = 4,
                      kFacBack = 5, kFacMoved = 6, kFacDefault = 7 };
/* Kind 3 FACTION (rename 0: the name the game's world loaded with) or kind 5 RENAME (rename 1: the name the player just gave) from the
   connection admitted under senderKey (its slot key; empty = not admitted). Only that profile's own row may be written: the number must
   name the sender's profile. Every verdict from kFacSet on fills *rowOut and *facOut (the row's faction name after it); kFacSame,
   kFacBack leave the row as it is. Kenshi's "Nameless" at a load is always kFacDefault (its answer is owed even where the row already
   holds the number, so the game's faction takes it). Any other name - and "Nameless" TYPED on the FACTION tab (a rename) - is judged
   by the name rule: a name no other active profile holds is the sender's, and so is its own row's name in another case or spacing; a
   name another holds is refused (Back) on a rename and replaced (Moved) at a load - except that where two rows already hold one name,
   the profile made first keeps it (and may re-case it). */
inline int FactionDecide(const std::vector<Row>& rows, const std::string& senderKey, unsigned num, const std::string& name,
                         int* rowOut, std::string* facOut, int rename = 0)
{
    if (senderKey.empty() || num < 1 || SlotKey(PersonOfKey(senderKey), num) != senderKey) return kFacRefusedSender;
    const int i = FindRow(rows, PersonOfKey(senderKey), num);
    if (i < 0 || !rows[(size_t)i].active) return kFacRefusedUnknown;
    std::string f;
    if (!FactionForRow(name, &f)) return kFacRefusedName;
    const Row& r = rows[(size_t)i];
    *rowOut = i;
    if (rename == 0 && f == kEngineFactionDefault)
    {
        *facOut = (IsNamelessNumbered(r.faction) && FactionHolder(rows, r.faction, i) < 0) ? r.faction : NamelessFree(rows, i);
        return kFacDefault;
    }
    if (FactionHolder(rows, f, i) < 0 || (NameSame(r.faction, f) && !FactionHeldEarlier(rows, f, i)))
    {
        *facOut = f;
        return r.faction == f ? kFacSame : kFacSet;
    }
    if (rename != 0) { *facOut = r.faction; return kFacBack; }
    *facOut = NamelessFree(rows, i);
    return kFacMoved;
}
/* T-368: every OTHER active profile's faction name - what the game of profile (person, num) may not take. A name this profile holds by
   right (FactionDecide: its own row's, unless a row made earlier holds it too) is left out even where another row repeats it, so the
   player can re-case or re-space their own name. */
inline std::vector<std::string> TakenFactionsFor(const std::vector<Row>& rows, const std::string& person, unsigned num)
{
    const int own = FindRow(rows, person, num);
    const bool ownByRight = own >= 0 && rows[(size_t)own].active && !FactionHeldEarlier(rows, rows[(size_t)own].faction, own);
    std::vector<std::string> out;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (!rows[i].active || (int)i == own) continue;
        if (ownByRight && NameSame(rows[i].faction, rows[(size_t)own].faction)) continue;
        out.push_back(rows[i].faction);
    }
    return out;
}
/* T-368, the game's side: is this name (as the world's row would keep it - FactionForRow) another profile's faction name? */
inline bool FactionTakenIn(const std::vector<std::string>& taken, const std::string& name)
{
    std::string f;
    if (!FactionForRow(name, &f)) return false;
    for (size_t i = 0; i < taken.size(); ++i) if (NameSame(taken[i], f)) return true;
    return false;
}
/* T-368, the game's side: what a FACTION answer does to this game's player faction, whose name is now `current`. Have = the faction
   carries the answer's name already. Otherwise the answer judged `asked`; when the faction no longer carries that name (the player
   renamed again, or another world loaded) the answer is old and is dropped - the newer name was sent and the world's judgement of it
   settles what the world owed (FactionOwedAfter). */
enum FactionApply { kFacApplyDrop = 0, kFacApplySilent = 1, kFacApplyBackBox = 2, kFacApplyMovedBox = 3, kFacApplyHave = 4 };
inline int FactionApplyDecide(int verdict, const std::string& current, const std::string& asked, const std::string& to)
{
    if (to.empty() || (verdict != kFacBack && verdict != kFacMoved && verdict != kFacDefault)) return kFacApplyDrop;
    if (to == current) return kFacApplyHave;
    std::string cur;
    if (!FactionForRow(current, &cur) || cur != asked) return kFacApplyDrop;
    if (verdict == kFacBack) return kFacApplyBackBox;
    if (verdict == kFacMoved) return kFacApplyMovedBox;
    return current == kEngineFactionDefault ? kFacApplySilent : kFacApplyDrop;
}
/* T-368, the game's side: what the game tells the world after a FACTION answer (`how` = FactionApplyDecide). Seen = kind 6, the faction
   carries the answer's name (applied now, or already). Report = kind 3 of the name the faction kept: the answer could not be applied
   because a faction record of this game already carries its name, so the world judges the kept name as a load and its row and the game
   agree again. None = an old answer (dropped), or that same answer was reported back once already - the world sends it again at the
   profile's next admission instead of the two going back and forth. */
enum FactionReply { kFacReplyNone = 0, kFacReplySeen = 1, kFacReplyReport = 2 };
inline int FactionReplyDecide(int how, bool recordTaken, bool reportedBefore)
{
    if (how == kFacApplyDrop) return kFacReplyNone;
    if (how == kFacApplyHave || !recordTaken) return kFacReplySeen;
    return reportedBefore ? kFacReplyNone : kFacReplyReport;
}
/* T-368, the world's side: what a judgement of a kind 3 / 5 does to the FACTION answer still owed to that profile (sent, not yet
   acknowledged). Clear = the world accepted the game's name (Set / Same), so an older answer is never sent over it; Replace = this
   judgement's own answer is owed instead; Keep = refused, nothing changed. */
enum FactionOwed { kFacOwedKeep = 0, kFacOwedClear = 1, kFacOwedReplace = 2 };
inline int FactionOwedAfter(int verdict)
{
    if (verdict == kFacSet || verdict == kFacSame) return kFacOwedClear;
    if (verdict == kFacBack || verdict == kFacMoved || verdict == kFacDefault) return kFacOwedReplace;
    return kFacOwedKeep;
}
/* T-201 PP6' (owner 175): kind 4 SAVED from the connection admitted under senderKey - only that profile's own row is marked played. */
enum SavedVerdict { kSavedSet = 0, kSavedSame = 1, kSavedRefusedSender = 2, kSavedRefusedUnknown = 3 };
inline int SavedDecide(const std::vector<Row>& rows, const std::string& senderKey, unsigned num, int* rowOut)
{
    if (senderKey.empty() || num < 1 || SlotKey(PersonOfKey(senderKey), num) != senderKey) return kSavedRefusedSender;
    const int i = FindRow(rows, PersonOfKey(senderKey), num);
    if (i < 0 || !rows[(size_t)i].active) return kSavedRefusedUnknown;
    *rowOut = i;
    return rows[(size_t)i].played ? kSavedSame : kSavedSet;
}
inline const char* SavedVerdictName(int v)
{ return v == kSavedSet ? "set" : v == kSavedSame ? "same" : v == kSavedRefusedSender ? "notYourProfile" : v == kSavedRefusedUnknown ? "unknown" : "other"; }
inline const char* FactionVerdictName(int v)
{
    switch (v)
    {
    case kFacSet: return "set"; case kFacSame: return "same"; case kFacRefusedSender: return "notYourProfile";
    case kFacRefusedUnknown: return "unknown"; case kFacRefusedName: return "name";
    case kFacBack: return "takenChangedBack"; case kFacMoved: return "takenRenamed"; case kFacDefault: return "default"; default: return "other";
    }
}

/* ---- the words a refusal is said in ---- */
inline std::string VerdictText(int v, unsigned cap)
{
    switch (v)
    {
    case kOk:               return "done";
    case kRefusedCap:       return "you already have " + PNum((long long)cap) + " profiles in this world, which is the most the host allows - delete one before making a new one";
    case kRefusedName:      return "that profile name cannot be used - use 1 to 24 plain characters";
    case kRefusedNameTaken: return "that name is already taken in this world";   /* T-368 (owner 270 a) */
    case kRefusedUnknown:   return "that profile is not in this world any more";
    case kRefusedInUse:     return "that profile is being played right now, so it cannot be deleted";
    case kRefusedNumUsed:   return "that profile can't be reused";
    case kRefusedNotReady:  return "still connecting - try again in a moment";
    default:                return "the world is running a different version of the mod";   /* words1b: host-neutral - the host sees it too */
    }
}
inline const char* VerdictName(int v)
{
    switch (v)
    {
    case kOk: return "ok"; case kRefusedCap: return "cap"; case kRefusedName: return "name"; case kRefusedNameTaken: return "nameTaken";
    case kRefusedUnknown: return "unknown"; case kRefusedInUse: return "inUse"; case kRefusedNumUsed: return "numUsed";
    case kRefusedNotReady: return "notReady"; default: return "other";
    }
}

/* ---- the wire ---- */
inline void WPutU32(std::vector<char>* b, unsigned v) { const size_t at = b->size(); b->resize(at + 4); std::memcpy(&(*b)[at], &v, 4); }
inline void WPutStr(std::vector<char>* b, const std::string& s) { WPutU32(b, (unsigned)s.size()); b->insert(b->end(), s.begin(), s.end()); }
inline bool WGetU32(const std::vector<char>& b, size_t* at, unsigned* v)
{
    if (b.size() < *at + 4) return false;
    std::memcpy(v, &b[*at], 4); *at += 4; return true;
}
inline bool WGetStr(const std::vector<char>& b, size_t* at, std::string* s)
{
    unsigned n = 0;
    if (!WGetU32(b, at, &n) || n > kWireStrMax || b.size() < *at + n) return false;
    s->assign(b.begin() + (long)*at, b.begin() + (long)(*at + n)); *at += n; return true;
}

inline void EncodeRequest(std::vector<char>* b, int kind, unsigned num, const std::string& name)
{
    b->push_back((char)(unsigned char)kind); WPutU32(b, num); WPutStr(b, name.size() > kWireStrMax ? name.substr(0, kWireStrMax) : name);
}
inline bool DecodeRequest(const std::vector<char>& b, int* kind, unsigned* num, std::string* name)
{
    if (b.empty()) return false;
    size_t at = 1;
    const int k = (unsigned char)b[0];
    if (k != kReqNew && k != kReqDelete && k != kReqFaction && k != kReqSaved && k != kReqRename && k != kReqFactionSeen) return false;   /* names2a: kind 3; T-201 PP6': kind 4; T-368: kinds 5, 6 */
    if (!WGetU32(b, &at, num) || !WGetStr(b, &at, name) || at != b.size()) return false;
    *kind = k; return true;
}

struct Answer
{
    int answers, verdict;
    unsigned cap, subject;
    std::vector<Row> rows;   /* person is not on the wire; active rows only */
    /* T-490 (store protocol 69): the world's identity after the rows - {str world, str world id, u32 flags (bit 0 = upgraded)} - so a
       game in the lobby, which gets no WELCOME until it picks, can judge the folder it would use before the PROFILES screen opens */
    bool hasWorld, worldIdUpgraded; std::string world, worldId;
    Answer() : answers(kAnsList), verdict(kOk), cap(kCapDefault), subject(0), hasWorld(false), worldIdUpgraded(false) {}
};
inline void EncodeAnswer(std::vector<char>* b, const Answer& a)
{
    b->push_back((char)(unsigned char)a.answers); b->push_back((char)(unsigned char)a.verdict);
    WPutU32(b, a.cap); WPutU32(b, a.subject);
    const size_t n = a.rows.size() > kWireRowsMax ? kWireRowsMax : a.rows.size();
    WPutU32(b, (unsigned)n);
    for (size_t i = 0; i < n; ++i)
    {
        const Row& r = a.rows[i];
        WPutU32(b, r.num); WPutU32(b, (unsigned)r.slot); WPutU32(b, (unsigned)r.created); WPutU32(b, (unsigned)r.lastPlayed); WPutU32(b, r.played ? 1u : 0u);
        WPutStr(b, r.name); WPutStr(b, r.faction);
    }
    if (a.hasWorld) { WPutStr(b, a.world.substr(0, kWireStrMax)); WPutStr(b, a.worldId.substr(0, kWireStrMax)); WPutU32(b, a.worldIdUpgraded ? 1u : 0u); }   /* T-490 */
}
inline bool DecodeAnswer(const std::vector<char>& b, Answer* a)
{
    if (b.size() < 2) return false;
    Answer o;
    o.answers = (unsigned char)b[0]; o.verdict = (unsigned char)b[1];
    size_t at = 2; unsigned n = 0;
    if (o.answers > kAnsPick) return false;
    if (!WGetU32(b, &at, &o.cap) || !WGetU32(b, &at, &o.subject) || !WGetU32(b, &at, &n) || n > kWireRowsMax) return false;
    for (unsigned i = 0; i < n; ++i)
    {
        Row r; unsigned s = 0, c = 0, l = 0, pl = 0;
        if (!WGetU32(b, &at, &r.num) || !WGetU32(b, &at, &s) || !WGetU32(b, &at, &c) || !WGetU32(b, &at, &l) || !WGetU32(b, &at, &pl)
            || !WGetStr(b, &at, &r.name) || !WGetStr(b, &at, &r.faction)) return false;
        r.slot = (int)s; r.created = (long long)c; r.lastPlayed = (long long)l; r.active = 1; r.played = pl != 0 ? 1 : 0;
        o.rows.push_back(r);
    }
    if (at != b.size())   /* T-490: the world's identity */
    {
        unsigned fl = 0;
        if (!WGetStr(b, &at, &o.world) || !WGetStr(b, &at, &o.worldId) || !WGetU32(b, &at, &fl) || at != b.size()) return false;
        o.hasWorld = true; o.worldIdUpgraded = (fl & 1u) != 0;
    }
    *a = o; return true;
}

/* T-368: the first byte of a PROFILES answer says which body follows (-1 = empty). */
inline int AnswerKindOf(const std::vector<char>& b) { return b.empty() ? -1 : (int)(unsigned char)b[0]; }
const unsigned kTakenWireMax = 4096;   /* faction names in one TAKEN answer: the world's active profiles, far above any real world */
inline void EncodeTaken(std::vector<char>* b, const std::vector<std::string>& names)
{
    b->push_back((char)(unsigned char)kAnsTaken); b->push_back(0);
    const size_t n = names.size() > kTakenWireMax ? kTakenWireMax : names.size();
    WPutU32(b, (unsigned)n);
    for (size_t i = 0; i < n; ++i) WPutStr(b, names[i].substr(0, kWireStrMax));
}
inline bool DecodeTaken(const std::vector<char>& b, std::vector<std::string>* names)
{
    if (b.size() < 2 || (unsigned char)b[0] != kAnsTaken) return false;
    size_t at = 2; unsigned n = 0;
    if (!WGetU32(b, &at, &n) || n > kTakenWireMax) return false;
    std::vector<std::string> o;
    for (unsigned i = 0; i < n; ++i) { std::string s; if (!WGetStr(b, &at, &s)) return false; o.push_back(s); }
    if (at != b.size()) return false;
    names->swap(o);
    return true;
}
struct FactionAnswer { int verdict; unsigned num; std::string asked, name; FactionAnswer() : verdict(kFacSame), num(0) {} };
inline void EncodeFactionAnswer(std::vector<char>* b, const FactionAnswer& a)
{
    b->push_back((char)(unsigned char)kAnsFaction); b->push_back((char)(unsigned char)a.verdict);
    WPutU32(b, a.num); WPutStr(b, a.asked.substr(0, kWireStrMax)); WPutStr(b, a.name.substr(0, kWireStrMax));
}
inline bool DecodeFactionAnswer(const std::vector<char>& b, FactionAnswer* a)
{
    if (b.size() < 2 || (unsigned char)b[0] != kAnsFaction) return false;
    FactionAnswer o; o.verdict = (unsigned char)b[1];
    if (o.verdict != kFacBack && o.verdict != kFacMoved && o.verdict != kFacDefault) return false;
    size_t at = 2;
    if (!WGetU32(b, &at, &o.num) || !WGetStr(b, &at, &o.asked) || !WGetStr(b, &at, &o.name) || at != b.size()) return false;
    *a = o;
    return true;
}

/* T-490 / owner 429: DOES THE FOLDER A LINK WOULD USE REFUSE IT? The folder is coopworld::WorldDataDirDecide's - the world's own folder
   (ownWorldTxt = its world.txt, "" = none) or the joined folder - and that folder's format.txt decides for its kind
   (swformat::FormatDecide). Read only: judged at the profile lobby's first answer and at the WELCOME, before anything is written there.
   *joined = the joined folder was judged. */
inline int LinkFolderFormatDecide(const std::string& ownWorldTxt, const std::string& worldId, bool ownFmtExists, const std::string& ownFmt,
                                  bool joinedFmtExists, const std::string& joinedFmt, unsigned* found, std::string* why, bool* joined)
{
    const bool j = coopworld::WorldDataDirDecide(ownWorldTxt, worldId) == coopworld::kDataDirJoined;
    if (joined != 0) *joined = j;
    const int kind = j ? swformat::kKindJoined : swformat::kKindWorld;
    unsigned n = 0;
    const int rs = swformat::FormatParse(j ? joinedFmtExists : ownFmtExists, j ? joinedFmt : ownFmt, kind, &n, why);
    if (found != 0) *found = n;
    return swformat::FormatDecide(rs, n, swformat::FolderFormatKnown(kind));
}

/* PP7 (owner decision 162): KENSHI'S OWN LISTS SHOW SINGLE-PLAYER SAVES ONLY. A save folder carrying the world stamp
   (multiplayer-world.key - the file the save hook writes and the load gate reads; no file = a single-player save) is left out of
   Kenshi's LOAD GAME list and is never CONTINUE's choice. Every other use of the save folders is untouched: the MULTIPLAYER
   menu's own loads (HOST / PLAY autoload, the load verb), IMPORT GAME's list (owner decision 259 pending), the save dialog's
   list, anySavesExist and loadGame itself. */
enum KenshiSaveUse { kSaveUseLoadMenu = 0, kSaveUseContinue = 1, kSaveUseAutoload = 2, kSaveUseImportMenu = 3, kSaveUseOther = 4 };
inline bool KenshiListHides(int use, bool stamped)
{
    return stamped && (use == kSaveUseLoadMenu || use == kSaveUseContinue);
}
/* One folder of the save root CONTINUE loads from (SaveManager::getSavePath), as CONTINUE's choice sees it. */
struct ContinueCand
{
    std::string name;
    bool stamped;                 /* multiplayer-world.key is in the folder */
    bool hasSave;                 /* quick.save is in the folder (the engine's own saveExists test) */
    unsigned long long saveTime;  /* quick.save's last write (FILETIME) */
    ContinueCand() : stamped(false), hasSave(false), saveTime(0ULL) {}
};
/* CONTINUE's save. Kenshi's own choice is the last game saved or loaded (SaveManager+0x0, settings.cfg "continue"); a
   single-player choice (or an empty one, or one naming no folder in the list) is kept exactly. When it names a stamped folder:
   the newest single-player save in the list (newest quick.save, then the lower name, case ignored); none = "" - the engine then
   hides CONTINUE, as it does with no saves at all. */
inline std::string ContinueChoose(const std::string& current, const std::vector<ContinueCand>& dirs)
{
    if (current.empty()) return current;
    bool currentStamped = false;
    for (size_t i = 0; i < dirs.size(); ++i)
        if (SameFolderName(dirs[i].name, current) && dirs[i].stamped) currentStamped = true;
    if (!KenshiListHides(kSaveUseContinue, currentStamped)) return current;
    int best = -1;
    for (size_t i = 0; i < dirs.size(); ++i)
    {
        if (!dirs[i].hasSave || KenshiListHides(kSaveUseContinue, dirs[i].stamped)) continue;
        if (best < 0) { best = (int)i; continue; }
        const ContinueCand& b = dirs[(size_t)best];
        if (dirs[i].saveTime > b.saveTime || (dirs[i].saveTime == b.saveTime && coopcfg::CfgLower(dirs[i].name) < coopcfg::CfgLower(b.name)))
            best = (int)i;
    }
    return best < 0 ? std::string() : dirs[(size_t)best].name;
}

} // namespace coopprof
