#pragma once
/* ADMIN'S TOYCHEST v2 (--dedicated only, 2026-10-25). Pure: no disk, no clock, no engine; the
   game plugin (admin.cpp, chat.cpp), the world server (store_main.cpp) and the offline suite compile it. C++03 (VS2010 v100).

   WHO: only the server's admins - dedicated.txt's admin= and admin-loud= Steam ids, matched against the connection's id as AdminIdOf
   picks it (JoinConn.steamSelf): the world server's Steam-checked id for the connection when it has one (AuthConn.steamId, vouched
   for by the host's Steam bridge), else the id the game's HELLO tail reported itself. A SELF-REPORTED ID PROVES NOTHING (Steam ids
   are public): a dedicated server has no Steam bridge, so there it is always self-reported, and the server says so once in its log.
   Such a server is for a LAN or another trusted network, ideally with a world password (auth.txt). The WORLD SERVER decides (Gate)
   and logs every request and its result - by the player's name and IdTag, never the raw id; a game never judges its own admin
   rights. A refusal is a polite chat line. ONE EXCEPTION: /here (any player) is answered by the player's own
   game from its own lead character and never reaches the server; /help and /jail list are answered by the game too (words only).

   THE ROAD:
     1. The admin types a line starting with '/' in the chat window (chat.cpp SendFromBox -> admin.cpp AdminTyped). Parse turns it
        into a Cmd; the admin's game adds its own lead character's position (for /bring and /sethome) and sends ADMIN_REQ.
     2. The server checks the sender (Gate) and resolves the target (Choose, below):
          plain name  - BOTH the in-world players called that (a player = that player's lead character) AND every game's FIND answer
                        (how many of its own characters carry the name; for /revive, its fallen rows). Each player counts as one
                        target, each owner game with such a character one more - a player's own lead carrying the player's name is
                        the SAME target. Exactly one: the job goes on; several: refused, listed, with the @ / # forms to pick.
          @name       - players only (that player's lead), no character search.
          #name       - characters only; several owners are refused and listed. #name@player = that character on that player's game.
     3. The owner's game does the work on the main thread at a safe point (admin.cpp AdminTick) and answers ADMIN_DONE {code, text}.
     4. The server relays the words to the admin: ADMIN_REPLY {req, ok, text}, shown as grey chat lines (ChatNoticeLine). An
        ADMIN_REPLY with req 0 is a NOTICE to every player (a /jail).
   /goto is two steps: WHERE on the target's game, then MOVE on the admin's own game to the position it answered. /where with no
   name asks WHERE of every game in the world. Every MOVE carries an ABSOLUTE position (never "the nearest copy of someone"):
   the target need not be loaded on the mover's game, and the ground's height is the mover's own terrain read, else the y carried.

   THE HEALING TIERS: /medic = the v1 /heal (every part's flesh to its ceiling, stun 0, bleeding 0, the knockout latches cleared);
   /heal = /medic + blood to the engine's maximum, limb wear 0 (a crippled limb whole again), hunger up to full; /restore = /heal +
   every lost (stump) or crushed limb grown back (a fitted robotic limb is left alone).
   GOD MODE (/god [name], a toggle): the owner's game decides on / off and tells the server (ADMIN_GOD 101, accepted only inside the
   /god job); the server keeps the table (GodTable) of every god character in the world - in memory only, a player's rows dropped
   when that player leaves - and sends it to every game in the world (GOD_TABLE 102). Every game makes its own AI ignore those
   characters and refuses them damage and knockouts; the owner's game also keeps them at /heal every tick.
   /save: the world server's own save (the quit save, without quitting) and then every game's own-slot save (ADMIN_DO SAVE).
   /sethome, /home: one home per player id, kept by the world server in <world>\homes.txt (HomesText / HomesParse).
   /jail <name> [spot]: that player's squad to one of kJailSpots (random without a spot), announced to everyone.

   MESSAGE NUMBERS (world-server protocol 112): 97 ADMIN_REQ (game -> server), 98 ADMIN_DO (server -> game), 99 ADMIN_DONE
   (game -> server), 100 ADMIN_REPLY (server -> game), 101 ADMIN_GOD (game -> server), 102 GOD_TABLE (server -> game). 80-95 are
   left for upstream; 96 is DEDINFO. */
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <set>
#include "keptwire.h"   /* keptwire::FloatOk - a relayed position is checked before anything uses it (PosOk) */

namespace admincmd {

const unsigned kProtocol = 125;         /* the world-server protocol that carries the six (store_main.cpp / store.cpp check it) */
const unsigned kMsgAdminReq = 97;       /* game -> server: {u32 req, u32 verb, u32 flags, f32 x, f32 y, f32 z, str8 target, str8 extra} */
const unsigned kMsgAdminDo = 98;        /* server -> game: {u32 job, u32 op, u32 flags, f32 x, f32 y, f32 z, str8 target} */
const unsigned kMsgAdminDone = 99;      /* game -> server: {u32 job, u32 code, u32 count, f32 x, f32 y, f32 z, str16 text} */
const unsigned kMsgAdminReply = 100;    /* server -> game: {u32 req, u32 ok, str16 text} - '\n' separates lines; req 0 = a notice */
const unsigned kMsgAdminGod = 101;      /* game -> server: {u32 job, u32 uid, u32 on, str8 name} - the owner's /god decision */
const unsigned kMsgGodTable = 102;      /* server -> game: {u32 n, n x {u32 uid, u32 slot, str8 name}} - every god character */

const size_t kNameMax = 64;             /* a player or character name, as namewire's */
const size_t kTextMax = 2000;           /* a reply (a /where of every player) */
const size_t kGodMax = 64;              /* god characters in one world (the AI-thread table's size, admin.cpp) */

enum Verb { kVerbNone = 0, kVerbHeal = 1, kVerbRevive = 2, kVerbBring = 3, kVerbGoto = 4, kVerbTp = 5, kVerbWhere = 6, kVerbHelp = 7,
            kVerbMedic = 8, kVerbRestore = 9, kVerbGod = 10, kVerbSave = 11, kVerbHere = 12, kVerbSetHome = 13, kVerbHome = 14,
            kVerbJail = 15, kVerbAddItem = 16, kVerbKick = 17, kVerbBan = 18, kVerbUnban = 19, kVerbCount = 20 };
enum { kFlagAll = 1,                    /* REQ / DO heal: every living character of that player */
       kFlagHaveY = 2,                  /* DO move: y is a usable height when the mover's game reads no terrain there */
       kFlagFallen = 4,                 /* DO find: count fallen rows (the FALLEN tab's), not living characters */
       kFlagSquad = 8,                  /* DO move: the named character's whole squad (the living ones standing), not one character */
       kFlagPickPlayer = 16,            /* REQ: @name - players only */
       kFlagPickChar = 32,              /* REQ: #name - characters only (REQ extra = the player whose game, for #name@player) */
       kFlagJailList = 64 };            /* parse only: /jail list (answered by the game) */
enum Op { kOpNone = 0, kOpFind = 1, kOpWhere = 2, kOpHeal = 3, kOpRevive = 4, kOpMove = 5, kOpHealFull = 6, kOpRestore = 7,
          kOpGod = 8, kOpSave = 9, kOpAddItem = 10, kOpCount = 11 };   /* kOpHeal is the /medic tier (v1's heal) */
enum Code { kOk = 0, kNoSuch = 1, kDead = 2, kStillDown = 3, kFailed = 4, kNotReady = 5, kRefused = 6, kCodeCount = 7 };
enum Pick { kPickAny = 0, kPickPlayer = 1, kPickChar = 2 };
enum Tier { kTierMedic = 1, kTierHeal = 2, kTierRestore = 3 };

const float kBringOffsetX = 40.0f, kBringOffsetZ = 30.0f;   /* 50 world units, about 5 m (handoff.cpp PlayerTeleportPeer's scale) */
const float kCoordMax = 1.0e7f;                            /* a typed coordinate beyond this is a typo */
const double kFindWaitSec = 5.0;                           /* the server waits this long for every game's FIND / WHERE answer */
const double kDoWaitSec = 25.0;                            /* ... and this long for a job (a KO'd /bring heals, then waits to stand) */
const double kSaveWaitSec = 130.0;                         /* ... and this long for every game's own-slot save (its own give-ups: 125 s) */
const unsigned kStandWaitMs = 10000;                       /* the owner's game waits this long for a healed character to stand */
const unsigned kRegrowWaitMs = 5000;                       /* ... and this long for a /restore's limbs at the engine's safe point */
const float kHungerFull = 300.0f;                          /* /heal raises hunger to at least this (Kenshi's full stomach; see admin.cpp) */
const float kSquadSpacing = 30.0f;                         /* a squad move lays its members 30 units (3 m) apart in rows of four */
const float kHereTownNear = 3000.0f;                       /* /here names the nearest town within this distance */

inline const char* VerbName(int v)
{
    static const char* const n[kVerbCount] = { "?", "heal", "revive", "bring", "goto", "tp", "where", "help", "medic", "restore", "god",
                                               "save", "here", "sethome", "home", "jail", "additem", "kick", "ban", "unban" };
    return (v >= 0 && v < kVerbCount) ? n[v] : "?";
}
inline const char* OpName(int o)
{
    static const char* const n[kOpCount] = { "?", "find", "where", "medic", "revive", "move", "heal", "restore", "god", "save", "additem" };
    return (o >= 0 && o < kOpCount) ? n[o] : "?";
}
inline const char* CodeName(unsigned c)
{
    static const char* const n[kCodeCount] = { "ok", "noSuch", "dead", "stillDown", "failed", "notReady", "refused" };
    return c < (unsigned)kCodeCount ? n[c] : "?";
}
/* the verbs a non-admin may use: /here only (answered by the game; the server owner, 2026-10-25) */
inline bool AnyPlayerVerb(int v) { return v == kVerbHere; }
/* the verbs that name a target: required (1), optional (2: none = every player for /where, the admin's own lead for /god), none (0) */
inline int VerbTakesName(int v)
{
    switch (v)
    {
    case kVerbHeal: case kVerbMedic: case kVerbRestore: case kVerbRevive: case kVerbBring: case kVerbGoto: case kVerbTp: case kVerbJail:
    case kVerbKick: case kVerbBan: case kVerbUnban: return 1;
    case kVerbWhere: case kVerbGod: return 2;
    default: return 0;
    }
}
/* the healing op of a verb */
inline unsigned HealOp(int verb) { return verb == kVerbMedic ? (unsigned)kOpHeal : verb == kVerbRestore ? (unsigned)kOpRestore : (unsigned)kOpHealFull; }
inline int TierOfOp(unsigned op) { return op == (unsigned)kOpHeal ? kTierMedic : op == (unsigned)kOpRestore ? kTierRestore : op == (unsigned)kOpHealFull ? kTierHeal : 0; }

/* ---- text ---- */
inline bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
inline std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && IsSpace(s[a])) ++a;
    while (b > a && IsSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}
inline std::string Lower(const std::string& s0) { std::string s = s0; for (size_t i = 0; i < s.size(); ++i) if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] - 'A' + 'a'); return s; }
/* ASCII case folded, spaces at the ends dropped, quotes round the whole name dropped: names compare by this */
inline std::string NameKey(const std::string& raw)
{
    std::string s = Trim(raw);
    if (s.size() >= 2 && (s[0] == '"' || s[0] == '\'') && s[s.size() - 1] == s[0]) s = Trim(s.substr(1, s.size() - 2));
    return Lower(s);
}
inline bool NamesMatch(const std::string& a, const std::string& b) { const std::string k = NameKey(a); return !k.empty() && k == NameKey(b); }
inline std::string CleanName(const std::string& raw)   /* the name as it travels: trimmed, unquoted, control bytes dropped, capped */
{
    std::string s = Trim(raw);
    if (s.size() >= 2 && (s[0] == '"' || s[0] == '\'') && s[s.size() - 1] == s[0]) s = Trim(s.substr(1, s.size() - 2));
    std::string o;
    for (size_t i = 0; i < s.size() && o.size() < kNameMax; ++i) if ((unsigned char)s[i] >= 0x20 && s[i] != 0x7F) o += s[i];
    return o;
}
inline std::string Num(float v) { char b[32]; _snprintf(b, 31, "%.0f", v); b[31] = 0; return b; }
inline bool ParseCoord(const std::string& t, float* out)
{
    if (t.empty()) return false;
    char* e = 0;
    const double d = std::strtod(t.c_str(), &e);
    if (e == t.c_str() || *e != 0 || !(d == d) || d > (double)kCoordMax || d < -(double)kCoordMax) return false;
    *out = (float)d;
    return true;
}

/* ---- the jail spots: hellish places, from the game's own placements (Kenshi 1.0.68 data/newland/leveldata/rebirth/leveldata.level,
   its 0-townlist record: each spot is one town / marker instance's x, y, z, read with kmod-work's record reader) ---- */
struct JailSpot { const char* key; const char* name; float x, y, z; const char* source; };
const JailSpot kJailSpots[] = {
    { "fog",       "the Fog Islands",      -79155.0f,  671.0f,  -38919.0f, "MARKER-Nest-Cower 40594-Newwworld.mod (a fogmen nest marker)" },
    { "ash",       "the Ashlands",          91792.0f,  506.0f,  107090.0f, "Ashland Dome II 1533506-rebirth.mod" },
    { "acid",      "the Black Desert",       8698.0f,  645.0f,  -13295.0f, "Black Desert City 52210-Newwworld.mod" },
    { "cannibal",  "the Cannibal Plains",  -65388.0f, 1291.0f,  -99378.0f, "Cannibal Village 1224-gamedata.base" },
    { "leviathan", "the Leviathan Coast",   27281.0f,  234.0f,  123868.0f, "Dead Fishing Village 64915-rebirth.mod" },
};
const int kJailSpotCount = (int)(sizeof(kJailSpots) / sizeof(kJailSpots[0]));
/* the spot a word names (its key, or its name with or without "the"); -1 none */
inline int JailSpotOf(const std::string& word)
{
    const std::string k = NameKey(word);
    if (k.empty()) return -1;
    for (int i = 0; i < kJailSpotCount; ++i)
    {
        const std::string name = Lower(kJailSpots[i].name);
        if (k == kJailSpots[i].key || k == name || (name.size() > 4 && k == name.substr(4))) return i;
    }
    return -1;
}
/* a spot rides ADMIN_REQ's flags, bits 16-23, as spot + 1 (0 = none: the server picks) */
inline unsigned JailSpotFlags(int spot) { return (spot >= 0 && spot < kJailSpotCount) ? (unsigned)(spot + 1) << 16 : 0u; }
inline int JailSpotOfFlags(unsigned flags) { const int s = (int)((flags >> 16) & 0xFFu) - 1; return (s >= 0 && s < kJailSpotCount) ? s : -1; }
/* a spot from any number (the server's own rand()) */
inline int JailSpotPick(unsigned r) { return (int)(r % (unsigned)kJailSpotCount); }
inline std::string JailListWords()
{
    std::string s = "Jail spots:";
    for (int i = 0; i < kJailSpotCount; ++i) s += std::string(i ? "," : "") + " " + kJailSpots[i].key + " (" + kJailSpots[i].name + ")";
    return s + ". /jail <name> picks one at random; /jail <name> <spot> picks that one.";
}
inline std::string JailWords(const std::string& who, int spot)
{
    return who + " has been jailed in " + (spot >= 0 && spot < kJailSpotCount ? kJailSpots[spot].name : "a hellish place") + ".";
}

/* ---- /additem: the three research books only (the server owner, 2026-10-25: the books only, for an admin, no player targeting).
   The string ids are the vanilla records' (gamedata.base / rebirth.mod, read with kmod-work): the cost item of 77 research rows
   ("Book"), of 35 ("Ancient Science Book") and of 11 ("Engineering Research"). ---- */
struct BookAlias { const char* key; const char* name; const char* sid; };
const BookAlias kBookAliases[] = {
    { "book",     "Book",                 "16855-nodes_otto1.mod" },
    { "rarebook", "Ancient Science Book", "43953-rebirth.mod" },
    { "engibook", "Engineering Research", "43951-rebirth.mod" },
};
const int kBookAliasCount = (int)(sizeof(kBookAliases) / sizeof(kBookAliases[0]));
const int kAddItemMax = 99;
inline int BookAliasOf(const std::string& word)
{
    const std::string k = NameKey(word);
    for (int i = 0; i < kBookAliasCount; ++i) if (k == kBookAliases[i].key) return i;
    return -1;
}
/* the alias and the count ride ADMIN_REQ's / ADMIN_DO's flags: bits 16-23 alias + 1, bits 24-31 the count */
inline unsigned AddItemFlags(int alias, int count)
{
    if (alias < 0 || alias >= kBookAliasCount || count < 1) return 0u;
    if (count > kAddItemMax) count = kAddItemMax;
    return ((unsigned)(alias + 1) << 16) | ((unsigned)count << 24);
}
inline int AddItemAliasOf(unsigned flags) { const int a = (int)((flags >> 16) & 0xFFu) - 1; return (a >= 0 && a < kBookAliasCount) ? a : -1; }
inline int AddItemCountOf(unsigned flags) { const int n = (int)((flags >> 24) & 0xFFu); return (n >= 1 && n <= kAddItemMax) ? n : 0; }
inline std::string AddItemWords(int added, int asked, int alias, const std::string& who)
{
    const std::string name = (alias >= 0 && alias < kBookAliasCount) ? kBookAliases[alias].name : "?";
    std::string s = "Added " + Num((float)added) + " x " + name + " to " + who;
    if (added < asked) s += " (" + Num((float)(asked - added)) + " more did not fit: the inventory is full)";
    return s + ".";
}

/* ---- the typed line ---- */
struct Cmd
{
    int verb; unsigned flags; std::string target, extra; float x, z; int pick; int spot;
    Cmd() : verb(kVerbNone), flags(0), x(0.0f), z(0.0f), pick(kPickAny), spot(-1) {}
};

inline bool IsCommand(const std::string& typed) { const std::string t = Trim(typed); return !t.empty() && t[0] == '/'; }
/* the verb a command word names ("heal", "/tp" without its slash, any case); kVerbNone = not one of the toychest's */
inline int VerbOfWord(const std::string& word);
/* the verb a typed line starts with ('/' and a known word); kVerbNone = not a toychest command (a plain line, or '/' and another word) */
inline int CommandVerb(const std::string& typed);
inline bool Parse(const std::string& typed, Cmd* out, std::string* err);
/* THE COMMAND'S SHAPE: a line that starts with a toychest verb is a command - a mistyped one shows its usage to the admin alone (AdminTyped),
   never to everyone as chat. ONE EXCEPTION, for the two verbs whose name is optional (/where, /god): a plain sentence after them
   ("/where is everyone", "/god mode on") goes out as chat. Such a sentence is two or more words, not quoted, with no @ or # in it; a
   name with spaces after /where or /god is written quoted ("Old Tam") or as @player / #character. */
inline bool CommandShapeOk(const std::string& typed)
{
    const int v = CommandVerb(typed);
    if (v == kVerbNone) return false;
    if (VerbTakesName(v) != 2) return true;
    const std::string t = Trim(typed);
    size_t sp = 1;
    while (sp < t.size() && !IsSpace(t[sp])) ++sp;
    const std::string rest = Trim(t.substr(sp));
    if (rest.empty()) return true;
    if (rest.size() >= 2 && (rest[0] == '"' || rest[0] == '\'') && rest[rest.size() - 1] == rest[0]) return true;   /* a quoted name */
    if (rest.find_first_of("@#") != std::string::npos) return true;                                             /* @player, #character */
    return rest.find_first_of(" \t") == std::string::npos;                                                       /* one word */
}
/* THE CHAT LINE GATE (chat.cpp SendFromBox): a typed line is taken as a command - never sent as chat - only when it starts with a toychest
   verb in a command's shape (CommandShapeOk) AND this game is linked to a dedicated world (its DEDINFO came on this link): the only world
   server that judges them. Everywhere else (single player, a hosted world, no link, '/' and any other word, a plain sentence after /where
   or /god) the line goes out as ordinary chat, exactly as without this build. */
inline bool ChatTakesCommand(const std::string& typed, bool linkUp, bool dedicatedWorld)
{
    return linkUp && dedicatedWorld && CommandShapeOk(typed);
}
inline std::string HelpText()
{
    return "Admin commands: /medic <name> | /heal <name> | /heal all <player> | /restore <name> | /revive <name> | /god [name] | /bring <name>"
           " | /goto <name> | /tp <name> <x> <z> | /where [player] | /jail <name> [spot] | /jail list | /sethome | /home | /save"
           " | /additem book|rarebook|engibook [count] | /kick <player> [reason] | /ban <player or Steam id> [reason] | /unban <player or Steam id>."
           " Admins /login <the server's admin password> first (it never leaves your game) and /logout when done."
           " For everyone: /here."
           " A name is a character's or a player's (a player's means their lead character); @name = the player only, #name = a character"
           " only, #name@player = that player's character.";
}
inline std::string Usage(int verb)
{
    switch (verb)
    {
    case kVerbHeal:    return "Usage: /heal <character or player>, or /heal all <player>.";
    case kVerbMedic:   return "Usage: /medic <character or player>, or /medic all <player>.";
    case kVerbRestore: return "Usage: /restore <character or player>, or /restore all <player>.";
    case kVerbRevive:  return "Usage: /revive <fallen character or player>.";
    case kVerbBring:   return "Usage: /bring <character or player>.";
    case kVerbGoto:    return "Usage: /goto <character or player>.";
    case kVerbTp:      return "Usage: /tp <character or player> <x> <z>.";
    case kVerbJail:    return "Usage: /jail <character or player> [spot], or /jail list.";
    case kVerbAddItem: return "Usage: /additem book|rarebook|engibook [count] (1 to 99, to your own lead character).";
    case kVerbKick:    return "Usage: /kick <player> [reason] (a name with spaces in quotes).";
    case kVerbBan:     return "Usage: /ban <player or Steam id> [reason] (a name with spaces in quotes).";
    case kVerbUnban:   return "Usage: /unban <player name or Steam id> (as bans.txt has it).";
    default:           return HelpText();
    }
}
/* a target as typed: plain, @player, #character or #character@player. false = *err (an empty part). */
inline bool ParseTarget(const std::string& raw, int* pick, std::string* name, std::string* player, std::string* err)
{
    *pick = kPickAny; name->clear(); player->clear(); err->clear();
    const std::string t = Trim(raw);
    if (t.empty()) return true;
    if (t[0] == '@') { *pick = kPickPlayer; *name = CleanName(t.substr(1)); if (name->empty()) { *err = "@ needs a player's name."; return false; } return true; }
    if (t[0] == '#')
    {
        *pick = kPickChar;
        std::string rest = t.substr(1);
        const size_t at = rest.rfind('@');
        if (at != std::string::npos)
        {
            *player = CleanName(rest.substr(at + 1));
            rest = rest.substr(0, at);
            if (player->empty()) { *err = "#name@ needs a player's name after the @."; return false; }
        }
        *name = CleanName(rest);
        if (name->empty()) { *err = "# needs a character's name."; return false; }
        return true;
    }
    *name = CleanName(t);
    return true;
}
/* true = *out is a command to send (or one the game answers itself: help, here, jail list); false = *err is the line to show */
inline int VerbOfWord(const std::string& raw)
{
    const std::string word = NameKey(raw);
    if (word == "heal") return kVerbHeal;
    if (word == "medic") return kVerbMedic;
    if (word == "restore") return kVerbRestore;
    if (word == "revive") return kVerbRevive;
    if (word == "bring") return kVerbBring;
    if (word == "goto") return kVerbGoto;
    if (word == "tp") return kVerbTp;
    if (word == "where") return kVerbWhere;
    if (word == "god") return kVerbGod;
    if (word == "save") return kVerbSave;
    if (word == "here") return kVerbHere;
    if (word == "sethome") return kVerbSetHome;
    if (word == "home") return kVerbHome;
    if (word == "jail") return kVerbJail;
    if (word == "additem") return kVerbAddItem;
    if (word == "kick") return kVerbKick;
    if (word == "ban") return kVerbBan;
    if (word == "unban") return kVerbUnban;
    if (word == "help" || word == "admin" || word == "?") return kVerbHelp;
    return kVerbNone;
}
inline int CommandVerb(const std::string& typed)
{
    const std::string t = Trim(typed);
    if (t.empty() || t[0] != '/') return kVerbNone;
    size_t sp = 1;
    while (sp < t.size() && !IsSpace(t[sp])) ++sp;
    return VerbOfWord(t.substr(1, sp - 1));
}
inline bool Parse(const std::string& typed, Cmd* out, std::string* err)
{
    *out = Cmd(); err->clear();
    const std::string t = Trim(typed);
    if (t.empty() || t[0] != '/') { *err = HelpText(); return false; }
    size_t sp = 1;
    while (sp < t.size() && !IsSpace(t[sp])) ++sp;
    const std::string word = NameKey(t.substr(1, sp - 1));
    std::string rest = Trim(t.substr(sp));
    const int v = VerbOfWord(word);
    if (v == kVerbNone) { *err = "Unknown command /" + t.substr(1, sp - 1) + ". " + HelpText(); return false; }
    out->verb = v;
    if (v == kVerbHelp) return true;
    if (v == kVerbAddItem)
    {
        /* /additem <alias> [count]: one of the three research books, to the admin's own lead; nothing else, no target */
        std::vector<std::string> w; size_t a = 0;
        while (a < rest.size()) { while (a < rest.size() && IsSpace(rest[a])) ++a; size_t b = a; while (b < rest.size() && !IsSpace(rest[b])) ++b; if (b > a) w.push_back(rest.substr(a, b - a)); a = b; }
        if (w.empty() || w.size() > 2) { *err = Usage(v); return false; }
        const int alias = BookAliasOf(w[0]);
        if (alias < 0) { *err = "/additem knows only book, rarebook and engibook. " + Usage(v); return false; }
        int count = 1;
        if (w.size() == 2)
        {
            char* e = 0; const long n = std::strtol(w[1].c_str(), &e, 10);
            if (e == w[1].c_str() || *e != 0 || n < 1) { *err = "The count is a number from 1 to " + Num((float)kAddItemMax) + ". " + Usage(v); return false; }
            count = n > kAddItemMax ? kAddItemMax : (int)n;
        }
        out->flags |= AddItemFlags(alias, count);
        return true;
    }
    if (v == kVerbKick || v == kVerbBan || v == kVerbUnban)
    {
        /* /kick <player> [reason], /ban <player or Steam id> [reason], /unban <player or Steam id>: the first word (or a quoted name, or
           @name) is who; the rest is the reason, kept as text */
        if (rest.empty()) { *err = Usage(v); return false; }
        std::string who, why;
        if (rest[0] == '"' || rest[0] == '\'')
        {
            const size_t e = rest.find(rest[0], 1);
            if (e == std::string::npos) { *err = "The name's closing quote is missing. " + Usage(v); return false; }
            who = rest.substr(1, e - 1); why = Trim(rest.substr(e + 1));
        }
        else
        {
            size_t e = 0; while (e < rest.size() && !IsSpace(rest[e])) ++e;
            who = rest.substr(0, e); why = Trim(rest.substr(e));
        }
        if (!who.empty() && who[0] == '@') who = who.substr(1);
        who = Trim(who);
        if (who.empty()) { *err = Usage(v); return false; }
        if (v == kVerbUnban && !why.empty()) { *err = Usage(v); return false; }
        if (who.size() > kNameMax || why.size() > kNameMax) { *err = "That name or reason is too long (" + Num((float)kNameMax) + " characters at most)."; return false; }
        out->target = who; out->extra = why; out->pick = kPickPlayer; out->flags |= kFlagPickPlayer;
        return true;
    }
    if (VerbTakesName(v) == 0)
    {
        if (!rest.empty()) { *err = std::string("/") + VerbName(v) + " takes no name."; return false; }
        return true;
    }
    if (v == kVerbHeal || v == kVerbMedic || v == kVerbRestore)
    {
        size_t s2 = 0;
        while (s2 < rest.size() && !IsSpace(rest[s2])) ++s2;
        if (NameKey(rest.substr(0, s2)) == "all" && !Trim(rest.substr(s2)).empty()) { out->flags |= kFlagAll; rest = Trim(rest.substr(s2)); }
    }
    if (v == kVerbJail)
    {
        if (NameKey(rest) == "list") { out->flags |= kFlagJailList; return true; }
        /* the last word is a spot when it names one and a name stands before it (a name may hold spaces) */
        const size_t b = rest.find_last_of(" \t");
        if (b != std::string::npos)
        {
            const int s = JailSpotOf(rest.substr(b + 1));
            if (s >= 0) { out->spot = s; rest = Trim(rest.substr(0, b)); }
        }
    }
    if (v == kVerbTp)
    {
        /* the last two words are the coordinates; the name is everything before them (a name may hold spaces) */
        const size_t b = rest.find_last_of(" \t");
        if (b == std::string::npos) { *err = Usage(v); return false; }
        const std::string zs = rest.substr(b + 1); std::string head = Trim(rest.substr(0, b));
        const size_t a = head.find_last_of(" \t");
        if (a == std::string::npos) { *err = Usage(v); return false; }
        const std::string xs = head.substr(a + 1); head = Trim(head.substr(0, a));
        if (!ParseCoord(xs, &out->x) || !ParseCoord(zs, &out->z)) { *err = "/tp needs two numbers for x and z. " + Usage(v); return false; }
        rest = head;
    }
    if (Trim(rest).size() > kNameMax + 2 + kNameMax) { *err = "That name is too long (" + Num((float)kNameMax) + " characters at most)."; return false; }
    std::string perr;
    if (!ParseTarget(rest, &out->pick, &out->target, &out->extra, &perr)) { *err = perr + " " + Usage(v); return false; }
    if (out->pick == kPickAny && Trim(rest).size() > kNameMax) { *err = "That name is too long (" + Num((float)kNameMax) + " characters at most)."; return false; }
    if (out->target.empty() && VerbTakesName(v) == 1) { *err = Usage(v); return false; }
    if ((out->flags & kFlagAll) && out->pick == kPickChar) { *err = "/" + std::string(VerbName(v)) + " all takes a player's name, not #name."; return false; }
    if (out->pick == kPickPlayer) out->flags |= kFlagPickPlayer;
    if (out->pick == kPickChar) out->flags |= kFlagPickChar;
    return true;
}
/* the target as it reads in words: name, @name, #name or #name@player */
inline std::string TargetText(int pick, const std::string& name, const std::string& player)
{
    if (pick == kPickPlayer) return "@" + name;
    if (pick == kPickChar) return "#" + name + (player.empty() ? std::string() : "@" + player);
    return name;
}
inline int PickOfFlags(unsigned flags) { return (flags & kFlagPickPlayer) ? kPickPlayer : (flags & kFlagPickChar) ? kPickChar : kPickAny; }

/* ---- the admin gate (the server's) ---- */
/* WHICH ID THE ADMIN LIST IS CHECKED AGAINST. verified = the world server's Steam-checked id for the
   connection (0 = none: no Steam bridge vouched for it - always so on a dedicated server); claimed = the id its HELLO tail reported.
   A verified id always wins and a different claim is ignored; without one the claim is used and *unverified says so (1). */
inline unsigned long long AdminIdOf(unsigned long long verified, unsigned long long claimed, int* unverified)
{
    if (verified != 0) { *unverified = 0; return verified; }
    *unverified = claimed != 0 ? 1 : 0;
    return claimed;
}
/* A short tag for the log in place of a Steam id: 16 bits of an FNV-1a hash, "id#3fa2". Tens of thousands of accounts share each tag,
   so it cannot be turned back into an id, but one admin's lines are easy to follow. 0 = "id#none". */
inline std::string IdTag(unsigned long long id)
{
    if (id == 0) return "id#none";
    unsigned long long h = 1469598103934665603ULL;
    for (int i = 0; i < 8; ++i) { h ^= (id >> (8 * i)) & 0xFFULL; h *= 1099511628211ULL; }
    const unsigned t = (unsigned)((h ^ (h >> 16) ^ (h >> 32) ^ (h >> 48)) & 0xFFFFu);
    char b[16]; _snprintf(b, 15, "id#%04x", t); b[15] = 0;
    return b;
}
/* a relayed position (a /bring or /sethome spot, a /goto target's answer, a /tp's typed x and z): every coordinate a finite number
   inside the world's range (keptwire::FloatOk: NaN and infinity fail) */
inline bool PosOk(float x, float y, float z) { return keptwire::FloatOk(x) && keptwire::FloatOk(y) && keptwire::FloatOk(z); }
enum Gate { kGateOk = 0, kGateNotDedicated = 1, kGateNoSteamId = 2, kGateNotAdmin = 3 };
inline int GateCheck(int dedicated, unsigned long long steamSelf, const std::set<unsigned long long>& admins, const std::set<unsigned long long>& loud)
{
    if (!dedicated) return kGateNotDedicated;
    if (steamSelf == 0) return kGateNoSteamId;
    if (admins.count(steamSelf) == 0 && loud.count(steamSelf) == 0) return kGateNotAdmin;
    return kGateOk;
}
inline std::string GateWords(int g, int verb)
{
    const std::string cmd = std::string("/") + VerbName(verb);
    switch (g)
    {
    case kGateNotDedicated: return "Sorry, " + cmd + " works only on a dedicated server.";
    case kGateNoSteamId:    return "Sorry, " + cmd + " is for server admins, and this server could not tell who you are on Steam.";
    case kGateNotAdmin:     return "Sorry, " + cmd + " is for server admins only.";
    default:                return std::string();
    }
}

/* ---- names -> the owner's slot (the server's) ---- */
struct Player { int slot; std::string name; int inWorld; Player() : slot(-1), inWorld(0) {} Player(int s, const std::string& n, int w) : slot(s), name(n), inWorld(w) {} };
/* ---- bans.txt (the dedicated world folder): one ban per line, "<SteamID64> <username> <reason...>"; # starts a comment. The
   username is one word (its spaces written as _); the reason is the rest of the line, may be empty. ---- */
struct Ban { unsigned long long steam; std::string name, reason; Ban() : steam(0ULL) {} };
inline bool SteamIdText(const std::string& t, unsigned long long* out)
{
    if (t.size() != 17) return false;
    unsigned long long v = 0ULL;
    for (size_t i = 0; i < t.size(); ++i) { if (t[i] < '0' || t[i] > '9') return false; v = v * 10ULL + (unsigned long long)(t[i] - '0'); }
    if (v == 0ULL) return false;
    *out = v; return true;
}
inline std::string BanNameField(const std::string& name)
{
    std::string s = Trim(name);
    for (size_t i = 0; i < s.size(); ++i) if (IsSpace(s[i]) || (unsigned char)s[i] < 32) s[i] = '_';
    return s.empty() ? std::string("-") : s;
}
inline std::vector<Ban> ParseBans(const std::string& text)
{
    std::vector<Ban> out;
    size_t a = 0;
    while (a <= text.size())
    {
        size_t e = text.find('\n', a); if (e == std::string::npos) e = text.size();
        const std::string line = Trim(text.substr(a, e - a));
        a = e + 1;
        if (line.empty() || line[0] == '#') continue;
        size_t p = 0; while (p < line.size() && !IsSpace(line[p])) ++p;
        Ban bn;
        if (!SteamIdText(line.substr(0, p), &bn.steam)) continue;
        const std::string rest = Trim(line.substr(p));
        size_t q = 0; while (q < rest.size() && !IsSpace(rest[q])) ++q;
        bn.name = rest.substr(0, q); bn.reason = Trim(rest.substr(q));
        out.push_back(bn);
    }
    return out;
}
inline std::string BansText(const std::vector<Ban>& bans)
{
    std::string s = "# Banned players: <SteamID64> <username> <reason>. One per line; delete a line (or /unban) to let them back in.\r\n";
    for (size_t i = 0; i < bans.size(); ++i)
    {
        char id[24]; std::sprintf(id, "%llu", bans[i].steam);
        s += std::string(id) + " " + BanNameField(bans[i].name) + (bans[i].reason.empty() ? std::string() : " " + bans[i].reason) + "\r\n";
    }
    return s;
}
/* the ban for this Steam id: its index, -1 none */
inline int BanIndexOfSteam(const std::vector<Ban>& bans, unsigned long long steam)
{
    for (size_t i = 0; i < bans.size(); ++i) if (steam != 0ULL && bans[i].steam == steam) return (int)i;
    return -1;
}
/* THE BAN GATE (the world server's AuthGate, at every HELLO, before the same-person check, the slot, the WELCOME or any record): the
   id a ban is matched against is the one AdminIdOf picks - the Steam-checked id when the server has one for the connection, else the
   HELLO tail's own claim. Its ban's index, -1 = let it go on (not a dedicated server, no id, or not banned). A SELF-REPORTED id makes
   this a fence for honest games only: a banned player can send another id (or none), so bans.txt is no defence on an open network. */
inline int BanGateIndex(int dedicated, const std::vector<Ban>& bans, unsigned long long verified, unsigned long long claimed)
{
    if (!dedicated) return -1;
    int unverified = 0;
    return BanIndexOfSteam(bans, AdminIdOf(verified, claimed, &unverified));
}
/* the marker a /ban by player name puts in front of the reason when the id it stores was self-reported (no Steam check): it reads as
   part of the reason, so bans.txt's format is unchanged and older servers read the line as before */
const char* const kBanUnverifiedMark = "[unverified-id]";
inline std::string BanReasonMark(const std::string& reason, int unverified)
{
    if (!unverified) return reason;
    const std::string r = Trim(reason);
    return r.empty() ? std::string(kBanUnverifiedMark) : std::string(kBanUnverifiedMark) + " " + r;
}
inline bool BanIsUnverified(const Ban& b) { return b.reason.compare(0, std::strlen(kBanUnverifiedMark), kBanUnverifiedMark) == 0; }
/* /unban's match: a Steam id, else the username (any case, spaces as _); the indexes that match */
inline std::vector<int> BanMatches(const std::vector<Ban>& bans, const std::string& who)
{
    std::vector<int> out;
    unsigned long long id = 0ULL;
    if (SteamIdText(Trim(who), &id)) { const int i = BanIndexOfSteam(bans, id); if (i >= 0) out.push_back(i); return out; }
    const std::string key = NameKey(BanNameField(who));
    for (size_t i = 0; i < bans.size(); ++i) if (NameKey(bans[i].name) == key) out.push_back((int)i);
    return out;
}
/* the in-world players called `name`: their count; *slot = the first (only meaningful when the count is 1) */
inline int ResolvePlayer(const std::vector<Player>& rows, const std::string& name, int* slot)
{
    int n = 0; *slot = -1;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (!rows[i].inWorld || !NamesMatch(rows[i].name, name)) continue;
        if (n == 0) *slot = rows[i].slot;
        ++n;
    }
    return n;
}
/* the FIND answers: each game's count of own characters with the name; lead = that game's lead character is one of them */
struct Hit
{
    int slot; unsigned count; std::string player; int lead;
    Hit() : slot(-1), count(0), lead(0) {}
    Hit(int s, unsigned c, const std::string& p) : slot(s), count(c), player(p), lead(0) {}
    Hit(int s, unsigned c, const std::string& p, int l) : slot(s), count(c), player(p), lead(l) {}
};
enum { kFindNone = 0, kFindOne = 1, kFindSeveral = 2 };
inline int FindVerdict(const std::vector<Hit>& hits, int* slot, std::string* owners)
{
    int n = 0; *slot = -1; owners->clear();
    for (size_t i = 0; i < hits.size(); ++i)
    {
        if (hits[i].count == 0) continue;
        if (n == 0) *slot = hits[i].slot;
        if (!owners->empty()) *owners += ", ";
        *owners += hits[i].player.empty() ? "slot " + Num((float)hits[i].slot) : hits[i].player;
        ++n;
    }
    return n == 0 ? kFindNone : (n == 1 ? kFindOne : kFindSeveral);
}
inline std::string FindNoneWords(const std::string& name, int verb, int unanswered)
{
    std::string s = verb == kVerbRevive ? "No fallen character or player called " + name + " was found in the world."
                                        : "No character or player called " + name + " is in the world.";
    if (unanswered > 0) s += " (" + Num((float)unanswered) + " game(s) did not answer.)";
    return s;
}
inline std::string FindSeveralWords(const std::string& name, const std::string& owners)
{
    return name + " is a character of more than one player (" + owners + "). Use the player's name instead.";
}
/* THE CHOICE (plain / @ / #): the in-world players and the FIND answers -> one target, or none, or several (refused with words).
   A player target is that player's lead character (*isPlayer 1, the owner game finds its lead); a character target is a name on one
   owner's game. A plain name counts each matching player once and each owner game holding such a character once more - except
   that a player's own game answering only with its lead character (lead=1, count 1) is that same player target, not a second.
   `narrow` (#name@player): the owner's slot that player's name resolved to (-1 = no narrowing). Returns kFindNone / One / Several. */
inline int Choose(const std::string& name, int pick, int verb, const std::vector<Player>& roster, const std::vector<Hit>& hits, int narrow,
                  int unanswered, int* slot, int* isPlayer, std::string* words)
{
    *slot = -1; *isPlayer = 0; words->clear();
    const std::string cmd = std::string("/") + VerbName(verb);
    std::vector<std::string> list; std::vector<int> slots, players;
    std::vector<int> playerSlots;
    if (pick != kPickChar)
        for (size_t i = 0; i < roster.size(); ++i)
            if (roster[i].inWorld && NamesMatch(roster[i].name, name)) { playerSlots.push_back(roster[i].slot); list.push_back("player " + roster[i].name); slots.push_back(roster[i].slot); players.push_back(1); }
    if (pick != kPickPlayer)
        for (size_t i = 0; i < hits.size(); ++i)
        {
            if (hits[i].count == 0 || (narrow >= 0 && hits[i].slot != narrow)) continue;
            bool samePlayer = false;
            for (size_t k = 0; k < playerSlots.size(); ++k) if (playerSlots[k] == hits[i].slot) samePlayer = true;
            const unsigned extra = (samePlayer && hits[i].lead) ? hits[i].count - 1 : hits[i].count;
            if (extra == 0) continue;
            const std::string owner = hits[i].player.empty() ? "slot " + Num((float)hits[i].slot) : hits[i].player;
            list.push_back(owner + "'s character " + name); slots.push_back(hits[i].slot); players.push_back(0);
        }
    if (list.empty())
    {
        if (pick == kPickPlayer) *words = "No player called " + name + " is in the world.";
        else if (pick == kPickChar) *words = verb == kVerbRevive ? "No fallen character called " + name + " was found in the world." : "No character called " + name + " is in the world.";
        else *words = FindNoneWords(name, verb, unanswered);
        if (pick != kPickPlayer && unanswered > 0 && words->find("did not answer") == std::string::npos) *words += " (" + Num((float)unanswered) + " game(s) did not answer.)";
        return kFindNone;
    }
    if (list.size() == 1) { *slot = slots[0]; *isPlayer = players[0]; return kFindOne; }
    std::string all;
    for (size_t i = 0; i < list.size(); ++i) all += (i ? ", " : "") + list[i];
    int nPlayers = 0; for (size_t i = 0; i < players.size(); ++i) nPlayers += players[i];
    if (pick == kPickPlayer) *words = "More than one player in the world is called " + name + ".";
    else if (pick == kPickChar || nPlayers == 0) *words = name + " matches more than one: " + all + ". Use " + cmd + " #" + name + "@<player> to pick one.";
    else *words = name + " matches more than one: " + all + ". Use " + cmd + " @" + name + " for the player or " + cmd + " #" + name + " for a character.";
    return kFindSeveral;
}

/* ---- the owner's side ---- */
/* /bring (and /goto) of a character: dead is refused (revive it first); knocked out or down is healed first, then moved once it
   stands (the engine's teleport leaves a downed character's ragdoll behind and it snaps back - handoff.cpp PlayerTeleport); up is
   moved at once */
enum { kMoveNow = 0, kMoveHealFirst = 1, kMoveRefuseDead = 2 };
inline int MovePlan(int dead, int down) { return dead ? kMoveRefuseDead : (down ? kMoveHealFirst : kMoveNow); }
/* the height a MOVE lands at: the mover's own terrain read when it answered, else the y the message carried, else none */
inline bool MoveHeight(int terrainRead, float terrainY, unsigned flags, float carriedY, float* y)
{
    if (terrainRead == 1) { *y = terrainY; return true; }
    if ((flags & kFlagHaveY) != 0 && carriedY == carriedY) { *y = carriedY; return true; }
    return false;
}
inline void BringSpot(float ax, float az, float* x, float* z) { *x = ax + kBringOffsetX; *z = az + kBringOffsetZ; }
/* a squad move's k-th member (0 = the leader, on the spot itself): rows of four, kSquadSpacing apart */
inline void SquadSpot(float x, float z, int k, float* ox, float* oz) { *ox = x + kSquadSpacing * (float)(k % 4); *oz = z + kSquadSpacing * (float)(k / 4); }
/* a squad move: the words. moved = names moved, down = names left (knocked out or down), dead = how many dead ones stayed */
inline std::string SquadMoveWords(const std::vector<std::string>& moved, const std::vector<std::string>& down, int dead, const std::string& where)
{
    std::string s;
    if (moved.empty()) s = "Nobody was moved";
    else
    {
        s = "Moved ";
        for (size_t i = 0; i < moved.size(); ++i) s += (i ? (i + 1 == moved.size() ? " and " : ", ") : "") + moved[i];
        s += " to " + where;
    }
    if (!down.empty())
    {
        s += "; ";
        for (size_t i = 0; i < down.size(); ++i) s += (i ? ", " : "") + down[i];
        s += down.size() == 1 ? " is knocked out and stayed" : " are knocked out and stayed";
    }
    if (dead > 0) s += "; " + Num((float)dead) + " dead one(s) stayed";
    return s + ".";
}
inline std::string WhereWords(const std::string& who, float x, float z, const std::string& sector)
{
    return who + " is at " + Num(x) + ", " + Num(z) + " (sector " + sector + ").";
}
/* /here: the /where words, the height, the nearest town (empty = none near), and the line to paste into `playerteleport abs` */
inline std::string HereWords(const std::string& who, float x, float y, float z, const std::string& sector, const std::string& town, float townDist)
{
    std::string w = WhereWords(who, x, z, sector);
    w = w.substr(0, w.size() - 1) + ", height " + Num(y);
    if (!town.empty()) w += ", near " + town + " (" + Num(townDist) + " away)";
    return w + ". Paste: playerteleport abs " + Num(x) + " " + Num(z) + " " + Num(y);
}
/* the healing words: "Alder: healed - ..." / "Alder: limbs restored, 2 regrown" */
inline std::string TierWords(int tier, const std::string& who, int regrown, int robotKept, bool standing)
{
    std::string t;
    if (tier == kTierRestore)
    {
        t = who + ": limbs restored, " + Num((float)regrown) + " regrown";
        if (robotKept > 0) t += " (" + Num((float)robotKept) + " robotic limb(s) kept)";
        t += "; blood full, every wound and debuff cleared";
    }
    else if (tier == kTierHeal) t = who + ": healed - every body part full, blood full, crippled limbs whole, bleeding, knockout and hunger cleared";
    else t = who + ": every body part full, bleeding stopped";
    t += standing ? std::string(", on their feet.") : "; still down after " + Num((float)(kStandWaitMs / 1000)) + " s - the game stands them up when its knockout ends.";
    return t;
}
/* /save: the reply. hh:mm the server's local clock; games = how many games saved; failed = one line each for those that did not */
inline std::string SaveWords(int hh, int mm, bool serverOk, int games, const std::vector<std::string>& failed)
{
    char b[16]; _snprintf(b, 15, "%02d:%02d", hh, mm); b[15] = 0;
    std::string s = std::string(serverOk ? "World saved " : "World save INCOMPLETE ") + b + " (" + (serverOk ? "server" : "server FAILED") + " + "
                    + Num((float)games) + (games == 1 ? " game)" : " games)");
    for (size_t i = 0; i < failed.size(); ++i) s += "\n" + failed[i];
    return s;
}

/* ---- the background save (town crash-safety): /save on a timer, run by the world server ----
   Kenshi's own 10-minute autosave is skipped in a co-op world (store.cpp OwnAutosaveSkip, mmo4 decision M5: the engine save is made at
   leave / Exit only, through the leave save's road - drain the records writer, the save, the mark, the checkpoint), so a game that
   crashes loses everything since its last leave. The world server now runs the /save job itself every `autosave` minutes (dedicated.txt,
   default kAutosaveDefaultMin, 0 = off): its own quit save without the exit, then ADMIN_DO SAVE to every game in the world, each of which
   saves its profile's own slot through that same road (StoreAdminSaveBegin) - never the engine's raw autosave, so the records and the
   save stay in step exactly as at a leave. No new message: the job is /save's own (protocol 111). The first save comes one interval
   after a player is in the world; an empty world waits; a save still running (an admin's /save, or the last one) moves it on by
   kAutosaveRetrySec; an admin's /save starts the interval again. */
const int kAutosaveDefaultMin = 10;
const int kAutosaveMaxMin = 240;
const double kAutosaveRetrySec = 30.0;
const char* const kAutosaveKey = "autosave";
/* dedicated.txt's value: whole minutes 0..kAutosaveMaxMin, digits only. false = unreadable (the caller keeps the default) */
inline bool AutosaveParse(const std::string& v, int* minutes)
{
    if (v.empty() || v.size() > 4) return false;
    int m = 0;
    for (size_t i = 0; i < v.size(); ++i) { if (v[i] < '0' || v[i] > '9') return false; m = m * 10 + (v[i] - '0'); }
    if (m > kAutosaveMaxMin) return false;
    *minutes = m;
    return true;
}
/* one look, once a second. nextAt < 0 = not armed (no player was in the world at the last look). */
enum { kAsOff = 0, kAsWait = 1, kAsArm = 2, kAsRun = 3, kAsDefer = 4, kAsIdle = 5 };
inline int AutosaveStep(double now, double nextAt, int minutes, int inWorld, int saveRunning)
{
    if (minutes <= 0) return kAsOff;
    if (inWorld <= 0) return kAsIdle;            /* nobody in the world: the timer is disarmed, armed again when a player is in */
    if (nextAt < 0.0) return kAsArm;             /* the first look with a player in the world: the first save one interval on */
    if (now < nextAt) return kAsWait;
    return saveRunning != 0 ? kAsDefer : kAsRun;
}
inline double AutosaveNextAt(double now, int minutes) { return now + 60.0 * (double)minutes; }

/* ---- god mode: the world server's table (in memory only) ---- */
struct GodRow { unsigned uid; int slot; std::string name; GodRow() : uid(0), slot(-1) {} GodRow(unsigned u, int s, const std::string& n) : uid(u), slot(s), name(n) {} };
struct GodTable
{
    std::vector<GodRow> rows; unsigned gen;
    GodTable() : gen(0) {}
    bool Has(unsigned uid) const { for (size_t i = 0; i < rows.size(); ++i) if (rows[i].uid == uid) return true; return false; }
    /* on: add (or rename) the row; off: drop it. true = the table changed (gen moves). A row of another slot is never taken over. */
    bool Set(int slot, unsigned uid, bool on, const std::string& name)
    {
        if (uid == 0) return false;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].uid != uid) continue;
            if (rows[i].slot != slot) return false;
            if (on) { if (rows[i].name == name) return false; rows[i].name = name; ++gen; return true; }
            rows.erase(rows.begin() + (long)i); ++gen; return true;
        }
        if (!on || rows.size() >= kGodMax) return false;
        rows.push_back(GodRow(uid, slot, name)); ++gen; return true;
    }
    /* a player gone: every row of that slot dropped; how many */
    int DropSlot(int slot)
    {
        int n = 0;
        for (size_t i = rows.size(); i-- > 0;) if (rows[i].slot == slot) { rows.erase(rows.begin() + (long)i); ++n; }
        if (n > 0) ++gen;
        return n;
    }
};
/* the owner's decision: a toggle of what the table says now */
inline bool GodToggle(const std::vector<GodRow>& table, unsigned uid) { for (size_t i = 0; i < table.size(); ++i) if (table[i].uid == uid) return false; return true; }
/* /god on a character. A dead character may not be MADE a god (revive it first), but a
   dead god row can always be switched OFF, so a row never gets stuck. */
enum GodPlan { kGodGo = 0, kGodRefuseDead = 1 };
inline int GodPlanOf(bool alive, bool inTable) { return (!alive && !inTable) ? kGodRefuseDead : kGodGo; }
/* GOD PROTECTION (the owner's top-up every 250 ms) runs only while this game is linked to a dedicated world; a god table this game
   still holds anywhere else does nothing. */
inline bool GodProtectRuns(bool linkUp, bool dedicatedWorld, size_t rows) { return linkUp && dedicatedWorld && rows != 0; }
inline std::string GodWords(const std::string& who, bool on)
{
    return on ? who + ": god mode ON - untargetable, no damage, no knockout, no hunger, no lost limbs."
              : who + ": god mode OFF - back to normal.";
}

/* ---- homes (the world server's <world>\homes.txt): one row per player id, tab-separated: id, x, y, z, area ---- */
struct Home { float x, y, z; std::string area; Home() : x(0), y(0), z(0) {} Home(float a, float b, float c, const std::string& w) : x(a), y(b), z(c), area(w) {} };
inline std::string HomeField(const std::string& s0) { std::string s; for (size_t i = 0; i < s0.size() && s.size() < kNameMax; ++i) { const unsigned char c = (unsigned char)s0[i]; if (c >= 0x20 && c != 0x7F) s += (char)c; } return s; }
inline bool HomeKeyOk(const std::string& id) { if (id.empty() || id.size() > 128) return false; for (size_t i = 0; i < id.size(); ++i) { const unsigned char c = (unsigned char)id[i]; if (c < 0x21 || c == 0x7F) return false; } return true; }
inline std::string HomesText(const std::map<std::string, Home>& homes)
{
    std::string s = "# homes: player id <tab> x <tab> y <tab> z <tab> area (written by the world server's /sethome)\r\n";
    for (std::map<std::string, Home>::const_iterator it = homes.begin(); it != homes.end(); ++it)
    {
        if (!HomeKeyOk(it->first)) continue;
        char b[96]; _snprintf(b, 95, "\t%.2f\t%.2f\t%.2f\t", it->second.x, it->second.y, it->second.z); b[95] = 0;
        s += it->first + b + HomeField(it->second.area) + "\r\n";
    }
    return s;
}
/* every readable row; a bad row is skipped and counted in *bad */
inline std::map<std::string, Home> HomesParse(const std::vector<std::string>& lines, int* bad)
{
    std::map<std::string, Home> out; *bad = 0;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::string l = lines[i];
        while (!l.empty() && (l[l.size() - 1] == '\r' || l[l.size() - 1] == '\n')) l.erase(l.size() - 1);
        if (Trim(l).empty() || Trim(l)[0] == '#') continue;
        std::vector<std::string> f; size_t a = 0;
        while (true) { const size_t b = l.find('\t', a); f.push_back(l.substr(a, b == std::string::npos ? std::string::npos : b - a)); if (b == std::string::npos) break; a = b + 1; }
        float v[3];
        if (f.size() < 4 || f.size() > 5 || !HomeKeyOk(f[0]) || !ParseCoord(f[1], &v[0]) || !ParseCoord(f[2], &v[1]) || !ParseCoord(f[3], &v[2])) { ++*bad; continue; }
        out[f[0]] = Home(v[0], v[1], v[2], f.size() == 5 ? HomeField(f[4]) : std::string());
    }
    return out;
}
inline std::string HomeSetWords(const Home& h) { return "Home set: " + Num(h.x) + " " + Num(h.y) + " " + Num(h.z) + (h.area.empty() ? std::string() : " (" + h.area + ")") + "."; }
inline std::string NoHomeWords() { return "No home set - use /sethome"; }

/* ---- the wire: little-endian, fixed sizes, a name as u8 length + bytes, a text as u16 length + bytes ---- */
inline void PutU32(std::vector<char>* b, unsigned v) { for (int i = 0; i < 4; ++i) b->push_back((char)((v >> (8 * i)) & 0xFFu)); }
inline void PutF32(std::vector<char>* b, float f) { unsigned v = 0; std::memcpy(&v, &f, 4); PutU32(b, v); }
inline bool PutStr8(std::vector<char>* b, const std::string& s) { if (s.size() > kNameMax) return false; b->push_back((char)(unsigned char)s.size()); b->insert(b->end(), s.begin(), s.end()); return true; }
inline void PutStr16(std::vector<char>* b, const std::string& s0)
{
    const std::string s = s0.size() > kTextMax ? s0.substr(0, kTextMax) : s0;
    b->push_back((char)(s.size() & 0xFF)); b->push_back((char)((s.size() >> 8) & 0xFF));
    b->insert(b->end(), s.begin(), s.end());
}
struct Cur { const unsigned char* p; size_t n, at; bool bad; };
inline Cur CurOf(const void* p, size_t n) { Cur c; c.p = (const unsigned char*)p; c.n = p ? n : 0; c.at = 0; c.bad = p == 0; return c; }
inline unsigned GetU32(Cur* c)
{
    if (c->bad || c->at > c->n || c->n - c->at < 4) { c->bad = true; return 0; }
    unsigned v = 0; for (int i = 0; i < 4; ++i) v |= (unsigned)c->p[c->at + (size_t)i] << (8 * i);
    c->at += 4; return v;
}
inline float GetF32(Cur* c) { const unsigned v = GetU32(c); float f = 0.0f; std::memcpy(&f, &v, 4); return f; }
inline std::string GetStr(Cur* c, int wide, size_t cap)
{
    if (c->bad) return std::string();
    size_t len = 0;
    if (wide) { if (c->n - c->at < 2) { c->bad = true; return std::string(); } len = (size_t)c->p[c->at] | ((size_t)c->p[c->at + 1] << 8); c->at += 2; }
    else { if (c->n - c->at < 1) { c->bad = true; return std::string(); } len = (size_t)c->p[c->at]; c->at += 1; }
    if (len > cap || c->n - c->at < len) { c->bad = true; return std::string(); }
    const std::string s((const char*)c->p + c->at, len); c->at += len; return s;
}
inline bool End(const Cur& c) { return !c.bad && c.at == c.n; }

struct Req { unsigned req, verb, flags; float x, y, z; std::string target, extra; Req() : req(0), verb(0), flags(0), x(0), y(0), z(0) {} };
struct Do { unsigned job, op, flags; float x, y, z; std::string target; Do() : job(0), op(0), flags(0), x(0), y(0), z(0) {} };
struct Done { unsigned job, code, count; float x, y, z; std::string text; Done() : job(0), code(0), count(0), x(0), y(0), z(0) {} };
struct Reply { unsigned req, ok; std::string text; Reply() : req(0), ok(0) {} };
struct GodSet { unsigned job, uid, on; std::string name; GodSet() : job(0), uid(0), on(0) {} };

/* the verbs that travel (help, here and jail list are the game's own) */
inline bool VerbTravels(unsigned v) { return v != kVerbNone && v != kVerbHelp && v != kVerbHere && v < (unsigned)kVerbCount; }
inline bool EncodeReq(const Req& r, std::vector<char>* b) { b->clear(); PutU32(b, r.req); PutU32(b, r.verb); PutU32(b, r.flags); PutF32(b, r.x); PutF32(b, r.y); PutF32(b, r.z); return PutStr8(b, r.target) && PutStr8(b, r.extra); }
inline bool DecodeReq(const void* p, size_t n, Req* r)
{
    Cur c = CurOf(p, n); Req o;
    o.req = GetU32(&c); o.verb = GetU32(&c); o.flags = GetU32(&c); o.x = GetF32(&c); o.y = GetF32(&c); o.z = GetF32(&c); o.target = GetStr(&c, 0, kNameMax); o.extra = GetStr(&c, 0, kNameMax);
    if (!End(c) || !VerbTravels(o.verb)) return false;
    *r = o; return true;
}
inline bool EncodeDo(const Do& d, std::vector<char>* b) { b->clear(); PutU32(b, d.job); PutU32(b, d.op); PutU32(b, d.flags); PutF32(b, d.x); PutF32(b, d.y); PutF32(b, d.z); return PutStr8(b, d.target); }
inline bool DecodeDo(const void* p, size_t n, Do* d)
{
    Cur c = CurOf(p, n); Do o;
    o.job = GetU32(&c); o.op = GetU32(&c); o.flags = GetU32(&c); o.x = GetF32(&c); o.y = GetF32(&c); o.z = GetF32(&c); o.target = GetStr(&c, 0, kNameMax);
    if (!End(c) || o.op == kOpNone || o.op >= (unsigned)kOpCount) return false;
    *d = o; return true;
}
inline void EncodeDone(const Done& d, std::vector<char>* b) { b->clear(); PutU32(b, d.job); PutU32(b, d.code); PutU32(b, d.count); PutF32(b, d.x); PutF32(b, d.y); PutF32(b, d.z); PutStr16(b, d.text); }
inline bool DecodeDone(const void* p, size_t n, Done* d)
{
    Cur c = CurOf(p, n); Done o;
    o.job = GetU32(&c); o.code = GetU32(&c); o.count = GetU32(&c); o.x = GetF32(&c); o.y = GetF32(&c); o.z = GetF32(&c); o.text = GetStr(&c, 1, kTextMax);
    if (!End(c) || o.code >= (unsigned)kCodeCount) return false;
    *d = o; return true;
}
inline void EncodeReply(const Reply& r, std::vector<char>* b) { b->clear(); PutU32(b, r.req); PutU32(b, r.ok); PutStr16(b, r.text); }
inline bool DecodeReply(const void* p, size_t n, Reply* r)
{
    Cur c = CurOf(p, n); Reply o;
    o.req = GetU32(&c); o.ok = GetU32(&c); o.text = GetStr(&c, 1, kTextMax);
    if (!End(c) || o.ok > 1) return false;
    *r = o; return true;
}
inline bool EncodeGodSet(const GodSet& g, std::vector<char>* b) { b->clear(); PutU32(b, g.job); PutU32(b, g.uid); PutU32(b, g.on); return PutStr8(b, g.name); }
inline bool DecodeGodSet(const void* p, size_t n, GodSet* g)
{
    Cur c = CurOf(p, n); GodSet o;
    o.job = GetU32(&c); o.uid = GetU32(&c); o.on = GetU32(&c); o.name = GetStr(&c, 0, kNameMax);
    if (!End(c) || o.uid == 0 || o.on > 1) return false;
    *g = o; return true;
}
inline bool EncodeGodTable(const std::vector<GodRow>& rows, std::vector<char>* b)
{
    b->clear();
    if (rows.size() > kGodMax) return false;
    PutU32(b, (unsigned)rows.size());
    for (size_t i = 0; i < rows.size(); ++i) { PutU32(b, rows[i].uid); PutU32(b, (unsigned)rows[i].slot); if (!PutStr8(b, rows[i].name)) return false; }
    return true;
}
inline bool DecodeGodTable(const void* p, size_t n, std::vector<GodRow>* rows)
{
    Cur c = CurOf(p, n); std::vector<GodRow> o;
    const unsigned k = GetU32(&c);
    if (c.bad || k > kGodMax) return false;
    for (unsigned i = 0; i < k; ++i) { GodRow r; r.uid = GetU32(&c); r.slot = (int)GetU32(&c); r.name = GetStr(&c, 0, kNameMax); if (c.bad || r.uid == 0) return false; o.push_back(r); }
    if (!End(c)) return false;
    *rows = o; return true;
}
/* a reply's lines (the chat shows each as its own grey line); empty lines dropped */
inline std::vector<std::string> Lines(const std::string& text)
{
    std::vector<std::string> out; size_t a = 0;
    while (a <= text.size())
    {
        size_t b = text.find('\n', a); if (b == std::string::npos) b = text.size();
        const std::string l = Trim(text.substr(a, b - a)); if (!l.empty()) out.push_back(l);
        a = b + 1;
    }
    return out;
}

/* Text a game sent, as it may stand in a log line. A line break in it would start a line of its own, one that reads exactly like a real
   "ADMIN:" line, so a break is written as " | " (a CRLF counts once) and every other control byte as '?'. Breaks at the very end are
   dropped. Bytes from 0x80 up (a UTF-8 name) are kept. Capped at `cap` bytes, and the cap says so: "...[+N bytes]". */
const size_t kLogTextMax = 400;
inline std::string LogSafe(const std::string& s, size_t cap = kLogTextMax)
{
    size_t n = s.size();
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) --n;
    std::string o;
    size_t i = 0;
    for (; i < n && o.size() < cap; ++i)
    {
        const unsigned char c = (unsigned char)s[i];
        if (c == '\r' || c == '\n') { if (c == '\r' && i + 1 < n && s[i + 1] == '\n') ++i; o += " | "; }
        else o += (c < 0x20 || c == 0x7F) ? '?' : (char)c;
    }
    if (i < n) { char b[32]; _snprintf(b, 31, "...[+%u bytes]", (unsigned)(n - i)); b[31] = 0; o += b; }
    return o;
}

/* the server log's line for one use: who (the player's name and IdTag - never the raw Steam id), what, target, result. Every text
   field goes through LogSafe here, so no caller can put a line break from a game into the log. */
inline std::string LogLine(const std::string& admin, unsigned long long steam, int verb, unsigned flags, const std::string& target, const std::string& result)
{
    return "ADMIN: " + LogSafe(admin, 64) + " (" + IdTag(steam) + ") /" + VerbName(verb) + ((flags & kFlagAll) ? " all" : "") + (target.empty() ? std::string() : " " + LogSafe(target, 160))
           + " -> " + LogSafe(result);
}

}   /* namespace admincmd */
