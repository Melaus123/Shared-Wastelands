/* src/common/worlddir.h - WORLD NAMES, WORLD FOLDERS, PLAYER DISPLAY NAMES AND THE ONE-TIME MIGRATION PLAN,
 * AS PURE DECISIONS (W1w; decisions 58 and 59; build/design-worlds.md sections 1-2).
 *
 * Same charter as areaclaim.h: nothing in here reads a global, opens a file, or includes a Windows, ENet or Ogre
 * header. The CALLER lists folders, reads files and moves them; this header only decides. It is compiled
 * into the offline suite (and, through cfgtext.cpp, into every program that reads shared_wastelands.cfg), so the rule that
 * turns a world's name into a folder, and the list of files that belong to a world, exist once.
 *
 * WHY. Decision 58: several worlds per computer, each with its own notebook folder under <save root>\coop-store\.
 * Decision 59(d): the world list shows the names of the players who joined each world, from a 'Your name' box saved
 * once in shared_wastelands.cfg (key `playername`, see cfgtext.h).
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no brace-init.
 */
#ifndef COOP_COMMON_WORLDDIR_H
#define COOP_COMMON_WORLDDIR_H
#include "names.h"   /* the on-disk names (mod folder, files, window texts) */

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "cfgtext.h"     /* CfgNameFieldOk, CfgLower, the field caps - the world name is the cfg `world` field */
#include "areaclaim.h"   /* coopstore::PlayerIdOk - a player-name line is keyed on the same 32-hex id */

namespace coopworld {

/* The default world (owner decision 249 a: "New World", was "coop"): the name used when none is given or the one given is
   invalid, and the world an old single-layout folder migrates into when the settings name no world. */
const char* const kDefaultWorld = "New World";

/* The migration's own journal, directly inside coop-store\ (see THE ONE-TIME MIGRATION PLAN below). Declared here
   because a world may not be named after it. */
const char* const kMigrationJournal = "worlds.migrated.txt";
/* W2-f (review-w2 F, H): --unmigrate's own notes (never a loose coop-store.log, which is a world file), and the marker a
   migration that made every move leaves in the world folder, so loose files that appear after it are not its own. */
const char* const kUnmigrateLog = "worlds.unmigrate.log";
const char* const kMigratedMarker = "migrated.done";
/* W2-f (review-w2 G): THE LOOSE WORLD FILES BY EXACT NAME - ONE list, read by WorldFileKnown (what PlanMigration moves)
   and by WorldNameIsStoreReserved (a world folder of one of these names would sit where the migration expects that file). */
const char* const kWorldFileExact[] = { "slots.txt", "areas.txt", "owner.txt", "pendingpos.txt", "options.txt",
                                        "clock.txt", "uniques.txt", "coop-store.log" };
const size_t kWorldFileExactCount = sizeof(kWorldFileExact) / sizeof(kWorldFileExact[0]);

/* ================= THE WORLD NAME =================
   The cfg world field's rule (CfgNameFieldOk: not empty, at most kCfgWorldFieldMax characters, none of
   \ / : * ? " < > | = # ; or a control character, and no two spaces in a row - owner decision 249 a) PLUS what Windows refuses or mangles in a folder name. */

inline int WorldIsDeviceName(const std::string& name)
{
    /* Windows keeps these names for devices WITH OR WITHOUT an extension: "con" and "con.txt" both open the
       console, never a folder. The test is on the part before the first dot, any case. */
    const std::string base = coopcfg::CfgLower(name.substr(0, name.find('.')));
    if (base == "con" || base == "prn" || base == "aux" || base == "nul") return 1;
    if (base == "conin$" || base == "conout$") return 1;   /* W1w-b: the console's input and output buffers */
    if (base.size() == 4 && (base.compare(0, 3, "com") == 0 || base.compare(0, 3, "lpt") == 0)
        && base[3] >= '1' && base[3] <= '9') return 1;
    return 0;
}

/* W1w-b: a name that is also something coop-store\ keeps at its top level, beside the world folders: a dead
   process's mirror-<pid>\ folder, the migration journal, or another world's outage queue queue-<world>.txt. A world
   folder with one of these names would be taken for that thing (or collide with it). Any case. */
inline bool WorldNameIsStoreReserved(const std::string& name)
{
    const std::string n = coopcfg::CfgLower(name);
    if (n.compare(0, 7, "mirror-") == 0) return true;
    if (n == kMigrationJournal) return true;
    if (n == kUnmigrateLog) return true;   /* W2-f (review-w2 G) */
    for (size_t i = 0; i < kWorldFileExactCount; ++i) if (n == kWorldFileExact[i]) return true;   /* W2-f: the loose world files */
    if (n.size() >= 10 && n.compare(0, 6, "queue-") == 0 && n.compare(n.size() - 4, 4, ".txt") == 0) return true;
    return false;
}

inline bool WorldNameOk(const std::string& name, std::string* why)
{
    /* W1w-b: EVERY byte of the name as given, before anything trims it. CfgNameFieldOk trims tabs, CR and LF off
       the ends and then tests only what is left, so "coop\t" would pass it and then be "coop" on the next read -
       and "con\t" would pass the device test below the same way. '~' is refused because Windows answers a
       folder's short 8.3 name too: "northl~1" can open "northlands". */
    for (std::string::size_type i = 0; i < name.size(); ++i)
    {
        const unsigned char ch = (unsigned char)name[i];
        if (ch < 0x20)
        {
            if (why != 0) *why = "Use letters, numbers, spaces, - and _ only.";
            return false;
        }
        if (ch == '~')
        {
            if (why != 0) *why = "The world name can't contain '~'.";   /* Windows reads a name with '~' as another folder's short name */
            return false;
        }
    }
    /* CfgNameFieldOk trims before it looks, so a leading or trailing space would pass it and then be a different
       name on the next read of the settings file. Refuse it here, before the trim. */
    if (!name.empty() && (name[0] == ' ' || name[name.size() - 1] == ' '))
    {
        if (why != 0) *why = "The world name cannot start or end with a space.";
        return false;
    }
    if (!coopcfg::CfgNameFieldOk(name, "world name", coopcfg::kCfgWorldFieldMax, why)) return false;
    for (std::string::size_type i = 0; i < name.size(); ++i)
    {
        const unsigned char ch = (unsigned char)name[i];
        /* 0x7F is a control character CfgNameFieldOk does not test. Bytes above it are refused because the
           folder-name rule below only knows how to ignore the case of A-Z: two accented names differing only in
           case would be one folder on disk and two worlds in the list. */
        if (ch >= 0x7F)
        {
            if (why != 0) *why = "Use letters, numbers, spaces, - and _ only.";
            return false;
        }
    }
    if (name == "." || name == "..")
    {
        if (why != 0) *why = "\"" + name + "\" can't be used as a world name.";   /* Windows reads it as a folder that is already there */
        return false;
    }
    if (name[0] == '.' || name[name.size() - 1] == '.')
    {
        if (why != 0) *why = "The world name can't start or end with a dot.";   /* Windows drops a trailing dot: the folder would differ */
        return false;
    }
    if (WorldIsDeviceName(name))
    {
        if (why != 0) *why = "\"" + name + "\" can't be used as a world name.";   /* Windows keeps it for a device, even with a dot after */
        return false;
    }
    if (WorldNameIsStoreReserved(name))
    {
        if (why != 0) *why = "\"" + name + "\" can't be used as a world name. Choose another name.";   /* the helper's own files use it (and mirror-*, queue-*) */
        return false;
    }
    return true;
}

/* W1w-b: THE WORLD ACTUALLY USED. The cfg `world` value is not checked when the file is read, so a folder is never
   built from it directly: a name WorldNameOk refuses becomes kDefaultWorld, and *fellBack says so for the caller to
   count and log. */
inline std::string WorldOrDefault(const std::string& name, bool* fellBack)
{
    const bool ok = WorldNameOk(name, 0);
    if (fellBack != 0) *fellBack = !ok;
    return ok ? name : std::string(kDefaultWorld);
}

/* THE FOLDER NAME. Windows folders ignore case, so the folder is the lower-cased name and two names that differ only
   in case are ONE world. The name as typed is kept in the folder's world.txt for display. */
inline std::string WorldFolderName(const std::string& name) { return coopcfg::CfgLower(name); }
inline bool WorldNamesCollide(const std::string& a, const std::string& b) { return WorldFolderName(a) == WorldFolderName(b); }

/* ================= W3 (decisions 58, 59; design-worlds sections 3-4): A GAME FOLLOWS THE NOTEBOOK'S WORLD =================
   The store WELCOME (protocol 45) names the notebook's world. What a game does with that name is this one pure function,
   swept by the offline suite, so the plugin's two callers (the notebook's WELCOME in store.cpp, and the host's session
   WELCOME as an early hint in config.cpp) cannot hold two ideas of it.
     ourWorld    - the world this game follows now (the cfg `world`, or one adopted earlier); empty = kDefaultWorld.
     welcome     - the name the WELCOME carried; empty = none (an older notebook, or no name at all).
     indexFolder - the FOLDER name of the world whose record index this game has already read (StoreIndexWorld), or
                   empty when none has been read yet.
   None    = the WELCOME names no usable world: nothing changes.
   Confirm = the same folder name: nothing changes, counted.
   Adopt   = another world and no other world's index read yet: follow it, in memory only (M5).
   Refuse  = this game has already read ANOTHER world's index: its records, mirror and queue belong to that folder, so the
             link is refused in words and a restart is the only way to change world (W2b). */
enum WelcomeWorldOutcome { kWelcomeWorldNone = 0, kWelcomeWorldConfirm = 1, kWelcomeWorldAdopt = 2, kWelcomeWorldRefuse = 3, kWelcomeWorldRefuseRemade = 4 };
inline int WelcomeWorldDecide(const std::string& ourWorld, const std::string& welcome, const std::string& indexFolder)
{
    if (welcome.empty() || !WorldNameOk(welcome, 0)) return kWelcomeWorldNone;
    const std::string theirs = WorldFolderName(welcome);
    if (!indexFolder.empty() && indexFolder != theirs) return kWelcomeWorldRefuse;
    if (WorldFolderName(WorldOrDefault(ourWorld, 0)) == theirs) return kWelcomeWorldConfirm;
    return kWelcomeWorldAdopt;
}
/* W3: THE WORLD A RE-READ OF shared_wastelands.cfg LEAVES THIS GAME FOLLOWING. W3-f (review-w3 item 6, M4 - the host follows
   too): for EVERY co-op role an adopted world is kept over the file's - the notebook's name wins until Kenshi is closed;
   a game that adopted nothing takes the file's, exactly as before W3. The name is W3's, when only a client followed; the
   role no longer enters. */
/* THE SAME RULE WITH THE WORLD ID. indexId = the id of the world whose index this game read ("" = none); welcomeId = the id the
   WELCOME carried. RefuseRemade = the same folder name, but another world: the world was deleted and made again under its name
   while this game kept the earlier one's records, mirror and queue open, so only a restart changes world. */
inline int WelcomeWorldDecideId(const std::string& ourWorld, const std::string& welcome, const std::string& indexFolder,
                                const std::string& indexId, const std::string& welcomeId)
{
    const int d = WelcomeWorldDecide(ourWorld, welcome, indexFolder);
    if ((d == kWelcomeWorldConfirm || d == kWelcomeWorldAdopt) && !indexFolder.empty() && indexFolder == WorldFolderName(welcome)
        && !indexId.empty() && !welcomeId.empty() && indexId != welcomeId)
        return kWelcomeWorldRefuseRemade;
    return d;
}
inline std::string WorldForRole(const std::string& fileWorld, const std::string& adopted)
{
    return !adopted.empty() ? adopted : fileWorld;
}

/* THE ONE CONSTRUCTION OF A WORLD'S FOLDER: <save root>\Shared Wastelands\worlds\<folder name> (owner 244). One trailing slash on the root is
   tolerated so the result never carries a doubled separator. W1w-b: an invalid name is kDefaultWorld's folder, and
   *fellBack (may be null) says it was. */
/* P8J-B2 (bench 2, F738): the same world folder built from the notebook's TOP folder (<data folder>\worlds, or
   <save root>\Shared Wastelands\worlds) instead of the save root, so shared_wastelands.cfg's storedir= can move the top
   folder. WorldDirIn(<root>\\Shared Wastelands\\worlds, n) is WorldDir(<root>, n) character for character (the offline
   suite checks it). owner 244: an EMPTY top folder is no world folder ("") - never one at the drive's root. */
inline std::string WorldDirIn(const std::string& storeTop, const std::string& name, bool* fellBack = 0)
{
    if (storeTop.empty()) return std::string();
    std::string top(storeTop);
    if (!top.empty() && (top[top.size() - 1] == '\\' || top[top.size() - 1] == '/')) top.erase(top.size() - 1);
    return top + "\\" + WorldFolderName(WorldOrDefault(name, fellBack));
}
inline std::string WorldDir(const std::string& saveRoot, const std::string& name, bool* fellBack = 0)
{
    std::string root(saveRoot);
    if (!root.empty() && (root[root.size() - 1] == '\\' || root[root.size() - 1] == '/')) root.erase(root.size() - 1);
    return root + "\\" + swnames::kSaveRootSub + "\\worlds\\" + WorldFolderName(WorldOrDefault(name, fellBack));
}

/* ================= THE PLAYER DISPLAY NAME (decision 59(d)) =================
   1..kCfgPlayerNameFieldMax printable characters (0x20..0x7E), no leading or trailing space. '#' and ';' are refused
   as well: shared_wastelands.cfg's reader treats either as the start of a comment and would cut the name there
   (CfgParseText, cfgtext.cpp). A tab or a newline is a control character and is refused with them. */
inline bool DisplayNameOk(const std::string& name, std::string* why)
{
    if (name.empty())
    {
        if (why != 0) *why = "Enter a player name.";
        return false;
    }
    if (name.size() > coopcfg::kCfgPlayerNameFieldMax)
    {
        if (why != 0) *why = "Name too long (24 characters max).";
        return false;
    }
    if (name[0] == ' ' || name[name.size() - 1] == ' ')
    {
        if (why != 0) *why = "Your name cannot start or end with a space.";
        return false;
    }
    for (std::string::size_type i = 0; i < name.size(); ++i)
    {
        const unsigned char ch = (unsigned char)name[i];
        if (ch < 0x20 || ch > 0x7E)
        {
            if (why != 0) *why = "Use letters, numbers, spaces and simple punctuation only.";
            return false;
        }
        if (ch == '#' || ch == ';')
        {
            if (why != 0) *why = "Your name can't contain '" + std::string(1, (char)ch) + "'.";   /* the settings reader would cut the name there */
            return false;
        }
    }
    return true;
}

/* ================= THE WORLD TABLE (the Host screen's list) =================
   world.txt line:   v1<TAB><name as typed><TAB><created, unix seconds>
   player-name line: <32-hex player id><TAB><display name>        (a later line for the same id wins)
   last played:      unix seconds as decimal text (the caller picks the source: newest save, else clock.txt time) */

/* Unix seconds as decimal text: 1..18 digits, else 0 (unknown). */
inline long long WorldDecimal(const std::string& t)
{
    if (t.empty() || t.size() > 18 || t.find_first_not_of("0123456789") != std::string::npos) return 0;
    long long v = 0;
    for (size_t i = 0; i < t.size(); ++i) v = v * 10 + (t[i] - '0');
    return v;
}

inline std::string WorldTxtFormat(const std::string& name, long long createdUnix)
{
    char b[32]; std::sprintf(b, "%lld", createdUnix);
    return "v1\t" + name + "\t" + b + "\n";
}

inline bool WorldTxtParse(const std::string& text, std::string* name, long long* createdUnix)
{
    std::string line = text.substr(0, text.find_first_of("\r\n"));
    if (line.compare(0, 3, "v1\t") != 0) return false;
    line.erase(0, 3);
    const std::string::size_type tab = line.find('\t');
    if (tab == std::string::npos) return false;
    const std::string n = line.substr(0, tab);
    const std::string c = line.substr(tab + 1);
    if (c.empty() || c.size() > 18 || c.find_first_not_of("0123456789") != std::string::npos) return false;
    if (!WorldNameOk(n, 0)) return false;
    if (name != 0) *name = n;
    if (createdUnix != 0) *createdUnix = WorldDecimal(c);
    return true;
}

inline std::string PlayerNameLineFormat(const std::string& playerId, const std::string& shownName)
{
    return playerId + "\t" + shownName + "\n";
}

/* ================= THE WORLD ID (T-490) =================
   The name says which world a folder is for; the id says WHICH world of that name, so a world deleted and made again under the
   same name, or a friend's world with the name of one this computer runs, is never taken for the other.
   Text: <created, unix seconds>-<8 lowercase hex>, at most kWorldIdMax characters. Made once: when the world is made (born), or
   when a world.txt from before ids is first read by the world server (upgraded: <its line-1 created time>-<new hex>).
   world.txt line 2: id<TAB><id><TAB><born|upgraded>. Line 1 never changes, so a reader of line 1 alone (WorldTxtParse) reads a
   two-line file exactly as a one-line one. */
const unsigned int kWorldIdMax = 27;
const long long kWorldIdAdoptSlackSec = 600;   /* an id-less save of an upgraded world is that world's when saved no earlier than this before its creation */
inline bool WorldIdOk(const std::string& id)
{
    const std::string::size_type dash = id.find('-');
    if (dash == std::string::npos || dash == 0 || dash > 18 || id.size() != dash + 9) return false;
    for (std::string::size_type i = 0; i < dash; ++i) if (id[i] < '0' || id[i] > '9') return false;
    for (std::string::size_type i = dash + 1; i < id.size(); ++i)
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return false;
    return true;
}
/* The id's creation time (unix seconds), 0 for no valid id. */
inline long long WorldIdCreated(const std::string& id) { return WorldIdOk(id) ? WorldDecimal(id.substr(0, id.find('-'))) : 0; }
/* The id's 8 hex characters, "" for no valid id. */
inline std::string WorldIdHex(const std::string& id) { return WorldIdOk(id) ? id.substr(id.find('-') + 1) : std::string(); }
/* The random part: FNV-1a over the caller's three values (its performance counter, process id and tick count), then mixed so
   every input bit reaches every output bit. */
inline unsigned int WorldIdMix(unsigned long long a, unsigned long long b, unsigned long long c)
{
    const unsigned long long v[3] = { a, b, c };
    unsigned int h = 2166136261u;
    for (int k = 0; k < 3; ++k)
        for (int byte = 0; byte < 8; ++byte) { h ^= (unsigned int)((v[k] >> (8 * byte)) & 0xFFu); h *= 16777619u; }
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
    return h;
}
inline std::string WorldIdMake(long long createdUnix, unsigned int mix)
{
    if (createdUnix < 0 || createdUnix > 999999999999999999LL) createdUnix = 0;
    char b[48]; std::sprintf(b, "%lld-%08x", createdUnix, mix);
    return std::string(b);
}
inline std::string WorldTxtIdLine(const std::string& id, bool upgraded)
{
    return "id\t" + id + "\t" + (upgraded ? "upgraded" : "born") + "\n";
}
/* Both lines, for a world made now (born) or a world.txt given its id (upgraded). */
inline std::string WorldTxtFormatWithId(const std::string& name, long long createdUnix, const std::string& id, bool upgraded)
{
    return WorldTxtFormat(name, createdUnix) + WorldTxtIdLine(id, upgraded);
}
/* Line 2. False = no line 2, or not a valid id line (a world.txt from before ids). */
inline bool WorldTxtParseId(const std::string& text, std::string* id, bool* upgraded)
{
    const std::string::size_type nl = text.find('\n');
    if (nl == std::string::npos) return false;
    std::string line = text.substr(nl + 1);
    line = line.substr(0, line.find_first_of("\r\n"));
    if (line.compare(0, 3, "id\t") != 0) return false;
    line.erase(0, 3);
    const std::string::size_type tab = line.find('\t');
    if (tab == std::string::npos) return false;
    const std::string i = line.substr(0, tab), how = line.substr(tab + 1);
    if (!WorldIdOk(i) || (how != "born" && how != "upgraded")) return false;
    if (id != 0) *id = i;
    if (upgraded != 0) *upgraded = (how == "upgraded");
    return true;
}
/* THE CREATION TIME A NEW WORLD ID CARRIES, and whether the id is upgraded. No world.txt: the oldest record file's time when the folder
   already holds records (a world older than this start - upgraded), else now (born). A world.txt from before ids: its line-1 created
   time, or the oldest record's when that is earlier (a migrated folder's line 1 can postdate its records) - upgraded. 0 = unknown. */
inline long long WorldIdCreatedChoose(bool hasWorldTxt, long long line1Created, long long oldestRecord, long long now, bool* upgraded)
{
    if (!hasWorldTxt)
    {
        if (upgraded != 0) *upgraded = oldestRecord > 0;
        return oldestRecord > 0 ? oldestRecord : now;
    }
    if (upgraded != 0) *upgraded = true;
    if (oldestRecord > 0 && (line1Created <= 0 || oldestRecord < line1Created)) return oldestRecord;
    return line1Created > 0 ? line1Created : 0;
}
/* WHICH WORLD ID A KEY FILE WRITE CARRIES (field 8). Once this game has read a world's record index: that world's id (none: the file's
   own field 8). Before it: the id of the WELCOME this game accepted. With no accepted identity: the file's own field 8, kept. A refused
   WELCOME's id is never accepted, so it never reaches a key file. */
inline std::string KeyWriteWorldId(bool indexLoaded, const std::string& indexId, const std::string& acceptedLinkId, const std::string& fileId)
{
    if (indexLoaded) return indexId.empty() ? fileId : indexId;
    return acceptedLinkId.empty() ? fileId : acceptedLinkId;
}
/* A FILETIME (100 ns since 1601) as unix seconds; 0 for a time before 1970 or none. */
inline long long FileTimeToUnix(unsigned long long ft)
{
    const unsigned long long epoch = 116444736000000000ULL;
    return ft <= epoch ? 0 : (long long)((ft - epoch) / 10000000ULL);
}

/* IS THIS SAVE THE WORLD'S? The one identity rule of the key scan, the folder-name rule and the load gate.
     saveId        - the save's key file field 8 ("" = none: a save made before ids, or by a game linked without one)
     saveTimeUnix  - its quick.save's last write, unix seconds (0 = unknown)
     welcomeId     - the id the world server's WELCOME carried ("" = none)
     welcomeCreated, upgraded - that world's creation time and whether its id was given to an existing world
   Unknown     = the WELCOME carried no id: the name alone decides, as before ids.
   Same        = the save carries this world's id.
   Other       = the save carries another id; or it carries none and the world was born with its id (no save of it lacks one),
                 or it was saved more than slackSec before the upgraded world's creation (an earlier world of that name).
   AdoptLegacy = no id, an upgraded world, saved since its creation: this world's, stamped with the id at its next key write. A world
                 whose creation time is unknown (0) adopts nothing. */
enum SaveIdentity { kIdUnknown = 0, kIdSame = 1, kIdOther = 2, kIdAdoptLegacy = 3 };
inline int SaveIdentityDecide(const std::string& saveId, long long saveTimeUnix, const std::string& welcomeId, long long welcomeCreated,
                              bool upgraded, long long slackSec)
{
    if (welcomeId.empty()) return kIdUnknown;
    if (!saveId.empty()) return saveId == welcomeId ? kIdSame : kIdOther;
    if (upgraded && welcomeCreated > 0 && saveTimeUnix > 0 && saveTimeUnix >= welcomeCreated - slackSec) return kIdAdoptLegacy;
    return kIdOther;
}
inline bool SaveIdentityMayUse(int v) { return v != kIdOther; }
inline const char* SaveIdentityName(int v)
{
    return v == kIdSame ? "same" : v == kIdOther ? "other" : v == kIdAdoptLegacy ? "adoptLegacy" : "unknown";
}

/* WHERE THIS GAME KEEPS ITS DATA FOR THE WORLD ITS WELCOME NAMED. ownWorldTxt = the text of <worlds top>\<folder>\world.txt
   ("" = none). Own = <worlds top>\<folder>: no id to tell worlds apart, or this computer runs that very world (the host, or two
   games on one computer). Joined = <joined top>\<folder>~<8 hex of the id>: a world this computer joins, kept apart from any
   world of the same name it runs. */
enum WorldDataDir { kDataDirOwn = 0, kDataDirJoined = 1 };
inline int WorldDataDirDecide(const std::string& ownWorldTxt, const std::string& welcomeId)
{
    if (!WorldIdOk(welcomeId)) return kDataDirOwn;
    std::string id; bool up = false;
    if (WorldTxtParseId(ownWorldTxt, &id, &up) && id == welcomeId) return kDataDirOwn;
    return kDataDirJoined;
}
inline std::string JoinedFolderName(const std::string& worldName, const std::string& worldId)
{
    return WorldFolderName(WorldOrDefault(worldName, 0)) + "~" + WorldIdHex(worldId);
}
inline std::string JoinedDirIn(const std::string& joinedTop, const std::string& worldName, const std::string& worldId)
{
    if (joinedTop.empty() || !WorldIdOk(worldId)) return std::string();
    std::string top(joinedTop);
    if (top[top.size() - 1] == '\\' || top[top.size() - 1] == '/') top.erase(top.size() - 1);
    return top + "\\" + JoinedFolderName(worldName, worldId);
}
/* A joined world's first folder takes the outage journal its world's same-name folder holds from before joined folders only
   when that folder is no world this computer runs (no world.txt) and the world existed before ids (upgraded). */
inline bool JoinedQueueTakeDecide(bool ownHasWorldTxt, bool ownHasQueue, bool upgraded)
{
    return !ownHasWorldTxt && ownHasQueue && upgraded;
}

inline bool PlayerNameLineParse(const std::string& raw, std::string* playerId, std::string* shownName)
{
    const std::string line = raw.substr(0, raw.find_first_of("\r\n"));
    const std::string::size_type tab = line.find('\t');
    if (tab == std::string::npos) return false;
    const std::string id = line.substr(0, tab);
    const std::string nm = line.substr(tab + 1);
    if (!coopstore::PlayerIdOk(id) || !DisplayNameOk(nm, 0)) return false;
    if (playerId != 0) *playerId = id;
    if (shownName != 0) *shownName = nm;
    return true;
}

struct WorldFolderInput
{
    std::string              folder;       /* the folder's name as listed on disk */
    std::string              worldTxt;     /* the text of its world.txt ("" = missing) */
    std::string              lastPlayed;   /* unix seconds as text ("" = unknown) */
    std::vector<std::string> playerLines;  /* its player-name lines */
};

struct WorldRow
{
    std::string              name;            /* as typed, from world.txt */
    std::string              folder;          /* WorldFolderName(name) */
    long long                lastPlayedUnix;  /* 0 = unknown */
    std::vector<std::string> players;         /* display names, sorted ignoring case */
};

inline bool WorldRowLess(const WorldRow& a, const WorldRow& b)
{
    if (a.lastPlayedUnix != b.lastPlayedUnix) return a.lastPlayedUnix > b.lastPlayedUnix;   /* newest first */
    return a.folder < b.folder;
}
inline bool NameLessNoCase(const std::string& a, const std::string& b)
{
    const std::string la = coopcfg::CfgLower(a), lb = coopcfg::CfgLower(b);
    return la != lb ? la < lb : a < b;
}

/* Pure: fed by the caller's listing. A folder without a readable world.txt, or whose world.txt names a world that is
   not this folder, is left out and named in *skipped with the reason, so the caller can log it. A second folder that
   is the same world ignoring case is left out the same way. */
inline std::vector<WorldRow> BuildWorldTable(const std::vector<WorldFolderInput>& in, std::vector<std::string>* skipped)
{
    std::vector<WorldRow> rows;
    std::set<std::string> seen;
    for (size_t i = 0; i < in.size(); ++i)
    {
        const WorldFolderInput& f = in[i];
        std::string name; long long created = 0;
        if (!WorldTxtParse(f.worldTxt, &name, &created))
        { if (skipped != 0) skipped->push_back(f.folder + ": no readable world.txt"); continue; }
        if (!WorldNamesCollide(name, f.folder))
        { if (skipped != 0) skipped->push_back(f.folder + ": world.txt names '" + name + "', a different world"); continue; }
        const std::string key = WorldFolderName(name);
        if (!seen.insert(key).second)
        { if (skipped != 0) skipped->push_back(f.folder + ": the same world as a folder already listed"); continue; }

        WorldRow r;
        r.name = name;
        r.folder = key;
        r.lastPlayedUnix = WorldDecimal(f.lastPlayed);
        std::map<std::string, std::string> byId;
        for (size_t j = 0; j < f.playerLines.size(); ++j)
        {
            std::string id, nm;
            if (PlayerNameLineParse(f.playerLines[j], &id, &nm)) byId[id] = nm;
        }
        for (std::map<std::string, std::string>::const_iterator it = byId.begin(); it != byId.end(); ++it)
            r.players.push_back(it->second);
        std::sort(r.players.begin(), r.players.end(), NameLessNoCase);
        rows.push_back(r);
    }
    std::sort(rows.begin(), rows.end(), WorldRowLess);
    return rows;
}

/* ================= THE ONE-TIME MIGRATION PLAN (design-worlds section 1) =================
   The OLD layout keeps one world's files loose in <save root>\coop-store\. The NEW layout keeps them in
   coop-store\<world>\. What lives loose in the old folder, from the code that writes it:

     SharedWastelandsServer.exe (src/coop-store/store_main.cpp)            plugin (src/coop-plugin/store.cpp)
       slots.txt areas.txt owner.txt pendingpos.txt  :299-302    <id>.platoon / <id>.zone / <id>.meta  :571-572
       <id>.platoon  <id>.meta                        :817-818    deleted.<faction>.bits               :1182
       <id>.platoon.prev  <id>.meta.prev              :819        queue-<Sanitize(world)>.txt          :716 (the
       deleted.<faction>.bits                         :822          outage queue's journal, becomes <world>\queue.txt)
       uniques.txt                                    :896        mirror-<pid>\                        :6056
       options.txt                                    :1045
       clock.txt                                      :1379
       <id>.platoon.refused                           :2505
       coop-store.log                                 :3309

   MOVED into <world>\: every file above except the queue journals of OTHER worlds and the mirror-<pid> folders.
   LEFT IN PLACE AND LISTED: mirror-<pid>\ (dead processes' copies - design-worlds section 1), queue-*.txt of other
   worlds, *.tmp leftovers of an interrupted write, and every name this list does not know.
   NOT LISTED: worlds.migrated.txt (the migration's own journal) and any other folder (a world folder). */

struct DirEntry
{
    std::string name;
    bool        isDir;
    DirEntry() : isDir(false) {}
    DirEntry(const std::string& n, bool d) : name(n), isDir(d) {}
};

struct Move
{
    std::string from;   /* relative to coop-store\ */
    std::string to;
};

struct MigrationPlan
{
    std::vector<Move>        moves;
    std::vector<std::string> leftInPlace;
    bool                     worldFellBack;   /* W1w-b: the world given failed WorldNameOk; the plan is kDefaultWorld's */
    MigrationPlan() : worldFellBack(false) {}
};

/* The plugin's own Sanitize (store.cpp:545), which names the queue file: letters, digits, '-', '_', '.' kept, the
   rest become '_'. Repeated here because this header may not include the plugin. */
inline std::string WorldQueueSanitize(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        o += keep ? c : '_';
    }
    return o;
}

inline bool EndsWithNoCase(const std::string& lowerName, const char* suffix)
{
    const std::string s(suffix);
    return lowerName.size() > s.size() && lowerName.compare(lowerName.size() - s.size(), s.size(), s) == 0;
}

/* Does this loose file belong to a world's folder? */
inline bool WorldFileKnown(const std::string& lowerName)
{
    for (size_t i = 0; i < kWorldFileExactCount; ++i) if (lowerName == kWorldFileExact[i]) return true;   /* W2-f: the shared list */
    static const char* const kSuffix[] = { ".platoon", ".zone", ".meta", ".platoon.prev", ".zone.prev", ".meta.prev",
                                           ".platoon.refused" };
    for (size_t i = 0; i < sizeof(kSuffix) / sizeof(kSuffix[0]); ++i) if (EndsWithNoCase(lowerName, kSuffix[i])) return true;
    if (lowerName.compare(0, 8, "deleted.") == 0 && EndsWithNoCase(lowerName, ".bits")) return true;
    return false;
}

/* THE PLAN, from the entries directly inside the old coop-store\ and the world they belong to (the cfg world;
   kDefaultWorld when none). Each name is considered once, ignoring case, so nothing is moved twice; a listing taken
   after the plan was applied - all of it or any part of it - yields exactly the moves not yet made. W1w-b: a world
   name WorldNameOk refuses plans into kDefaultWorld and sets worldFellBack. */
inline MigrationPlan PlanMigration(const std::vector<DirEntry>& root, const std::string& worldGiven)
{
    MigrationPlan p;
    const std::string world = WorldOrDefault(worldGiven, &p.worldFellBack);
    const std::string folder = WorldFolderName(world);
    const std::string myQueue = coopcfg::CfgLower("queue-" + WorldQueueSanitize(world) + ".txt");
    std::set<std::string> seen;
    for (size_t i = 0; i < root.size(); ++i)
    {
        const DirEntry& e = root[i];
        const std::string n = coopcfg::CfgLower(e.name);
        if (n.empty() || !seen.insert(n).second) continue;
        if (e.isDir)
        {
            if (n.compare(0, 7, "mirror-") == 0) p.leftInPlace.push_back(e.name + "\\");
            continue;   /* any other folder is a world's */
        }
        if (n == kMigrationJournal) continue;
        if (n == myQueue)
        {
            Move m; m.from = e.name; m.to = folder + "\\queue.txt"; p.moves.push_back(m);
            continue;
        }
        if (WorldFileKnown(n))
        {
            Move m; m.from = e.name; m.to = folder + "\\" + e.name; p.moves.push_back(m);
            continue;
        }
        p.leftInPlace.push_back(e.name);
    }
    return p;
}

/* W1w-b: the old layout is still (partly) there exactly when the plan over this listing still has a move - so a
   migration interrupted after any number of its moves is resumed, not mistaken for finished. */
inline bool NeedsMigration(const std::vector<DirEntry>& root, const std::string& world)
{
    return !PlanMigration(root, world).moves.empty();
}

/* THE REVERSE: the same moves backwards, last first - what --unmigrate replays from the journal. */
inline std::vector<Move> ReverseMoves(const std::vector<Move>& moves)
{
    std::vector<Move> r;
    for (size_t i = moves.size(); i > 0; --i)
    {
        Move m; m.from = moves[i - 1].to; m.to = moves[i - 1].from; r.push_back(m);
    }
    return r;
}

/* ================= THE MIGRATION JOURNAL (W2a; design-worlds section 1) =================
   <save root>\coop-store\worlds.migrated.txt, written by SharedWastelandsServer.exe. One line per move, appended and flushed
   BEFORE that move is made, so a crash at any point leaves a journal that names every move that may have happened:

       <from><TAB><to><LF>          both relative to coop-store\

   <from> is a loose name (no separator); <to> is <world folder>\<name> (exactly one separator). Nothing else is ever
   written, so a line of any other shape is not this program's and --unmigrate refuses the whole journal rather than
   move a file the journal cannot vouch for. A last line with no LF was cut off by a crash while it was being written:
   its move had not been made yet, so the reader drops it and says so (cutOffTail). A move journaled but then refused
   by Windows leaves its line behind; the reverse step for it finds nothing to move back (UnmigrateStepDecide). */

inline bool JournalNamePartOk(const std::string& s)
{
    if (s.empty() || s == "." || s == "..") return false;
    for (std::string::size_type i = 0; i < s.size(); ++i)
    {
        const unsigned char ch = (unsigned char)s[i];
        if (ch < 0x20 || ch == ':' || ch == '/' || ch == '\\') return false;
    }
    return true;
}

inline std::string MigrationJournalLine(const Move& m) { return m.from + "\t" + m.to + "\n"; }

/* One line, its LF already removed (a CR before it is tolerated). */
inline bool MigrationJournalLineParse(const std::string& raw, Move* out)
{
    std::string line(raw);
    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    const std::string::size_type tab = line.find('\t');
    if (tab == std::string::npos) return false;
    const std::string from = line.substr(0, tab);
    const std::string to = line.substr(tab + 1);
    const std::string::size_type sep = to.find('\\');
    if (sep == std::string::npos) return false;
    if (!JournalNamePartOk(from) || !JournalNamePartOk(to.substr(0, sep)) || !JournalNamePartOk(to.substr(sep + 1))) return false;
    if (out != 0) { out->from = from; out->to = to; }
    return true;
}

struct JournalRead
{
    std::vector<Move> moves;      /* in the order they were written */
    int               badLines;   /* lines of any other shape (an empty line is not counted) */
    bool              cutOffTail; /* the last line had no LF - dropped */
    JournalRead() : badLines(0), cutOffTail(false) {}
};

inline JournalRead MigrationJournalParse(const std::string& text)
{
    JournalRead r;
    std::string::size_type at = 0;
    while (at < text.size())
    {
        const std::string::size_type lf = text.find('\n', at);
        if (lf == std::string::npos) { r.cutOffTail = true; break; }
        const std::string line = text.substr(at, lf - at);
        at = lf + 1;
        if (line.empty() || line == "\r") continue;
        Move m;
        if (MigrationJournalLineParse(line, &m)) r.moves.push_back(m); else ++r.badLines;
    }
    return r;
}

/* Does this journal already hold a move into <folder>\ (any case)? Then a migration into that folder was started, and
   the records found there are its own, not another world's. */
inline bool JournalHasMovesInto(const JournalRead& j, const std::string& folder)
{
    const std::string want = coopcfg::CfgLower(folder) + "\\";
    for (size_t i = 0; i < j.moves.size(); ++i)
        if (coopcfg::CfgLower(j.moves[i].to).compare(0, want.size(), want) == 0) return true;
    return false;
}

/* W2-f (review-w2 H): IS THIS START RESUMING A MIGRATION INTO <folder>? Only while the journal names moves into it AND
   the folder has no kMigratedMarker. A migration that made every move leaves the marker, so loose files that appear
   after it are not the rest of it: beside a folder that already holds records they are the mixing refusal again. */
inline bool MigrationResuming(const JournalRead& j, const std::string& folder, bool markerPresent)
{
    return !markerPresent && JournalHasMovesInto(j, folder);
}

/* The name the journal is given once --unmigrate has put every file back. W2-f (review-w2 I): --unmigrate is for going
   back to an OLDER build. After play the world folder holds records written since the migration, so the next start of
   THIS build finds loose world files beside a folder with records and refuses (the mixing refusal) until that folder is
   moved away - it does not simply migrate afresh. */
inline std::string MigrationJournalUndoneName(long long unixSeconds)
{
    char b[32]; std::sprintf(b, "%lld", unixSeconds);
    return std::string(kMigrationJournal) + ".undone." + b;
}

/* ONE REVERSE STEP: move <world folder>\<name> back to the loose <name>. Never overwrites. */
enum UnmigrateStep
{
    kUnmigrateMove,          /* the moved file is there and the loose name is free: move it back */
    kUnmigrateAlreadyBack,   /* the moved file is gone and the loose one is there: done before (or never moved) */
    kUnmigrateMissing,       /* neither is there: nothing to move - said, not guessed at */
    kUnmigrateConflict       /* both are there: stop - moving would overwrite one of them */
};
inline UnmigrateStep UnmigrateStepDecide(bool movedFileExists, bool looseNameExists)
{
    if (movedFileExists) return looseNameExists ? kUnmigrateConflict : kUnmigrateMove;
    return looseNameExists ? kUnmigrateAlreadyBack : kUnmigrateMissing;
}

}   /* namespace coopworld */

#endif   /* COOP_COMMON_WORLDDIR_H */
