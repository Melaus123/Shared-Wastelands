#pragma once
/* RESETTING A WORLD - the pure rules behind the world server's --reset-world (src/coop-store/store_main.cpp KeepResetWorld).
   Compiled into the world server and the offline suite (src/coop-test/test_main.cpp).

   A reset SPLITS the world in two. The world as it was is kept whole - every entry, its id, its state - in a folder of its own under the
   name "<name> - before reset <n>" (ResetOldWorldName; n counts this world's resets, ResetCountNext), so every save made in it still
   loads in it, until the host deletes it from the world list. The reset world takes the usual name and folder with a new id
   (coopworld::WorldTxtAfterReset) and only what ResetWorldGets gives it. Each entry at the top of a world folder is one of:
     KEEP    - the players and the host's choices: world.txt, format.txt, slots.txt, profiles.txt, aliases.txt, owner.txt, options.txt,
               mods.txt, uid_seats.txt (each seat's highest uid: going back could hand a kept character's uid out again), fallen.txt,
               teams.txt, own_high.txt, world.gen (a number it has seen is never issued again), auth.txt (the world's password),
               access.txt (who may enter), the world lock, the logs, server-refused.txt and a keep-buildings-<time>\ folder (the area
               records an earlier build's reset kept). A ".1" backup or a ".tmp" leftover is what its file is. The old world keeps each; the
               reset world gets a copy of each but world.txt (written new) and the world lock (made by its first start).
     MOVE    - the world's state: every squad record (<id>.platoon and <id>.meta, with their .prev and .refused; the ground, box and
               piece records - ground.<...>, box.<...>, piece.<...> - are files of this form too), the deleted-number
               bitmaps (deleted.<faction>.bits), uniques.txt, world_relations.txt, town_losses.txt, town_bars.txt, owed_people.txt, owed_done.txt, owed_meta.txt,
               research_boxes.txt, research_takes.txt, clock.txt, areas.txt, pendingpos.txt, repair.txt, queue.txt, kept-pieces.txt and
               kept-state.txt (the players' pieces an earlier reset kept and how far each was laid back, zonekeep.h), player-towns.txt and kept-towns.txt (the outposts the players' games said they hold,
               and the ones a reset kept - townkeep.h), and the folders
               repair\, recycle\ and mirror-<pid>\ (a host game's mirror copies of the world's records, src/coop-plugin/store.cpp
               g_dirMirror). The old world keeps it; the reset world starts without it.
     AREA    - an area record's file (record id "zone.<...>", recname.h): the old world keeps it; when the host keeps the players'
               buildings, the pieces players own in it (buildings, furniture, boxes and their contents) are read into the reset
               world's kept-pieces.txt (zonekeep.h).
     UNFINISHED - a before-reset-<time>\ folder an earlier build's reset left (that build deleted the world's state from it and
               stopped part-way): the old world keeps it as it is; nothing is deleted.
     UNKNOWN - anything else: the old world keeps it, named in the log; the reset world does not get it.
   Pure: names only, no files touched. C++03 (VS2010). */
#include <string>
#include <vector>
#include "worldkeep.h"   /* worldkeep::Lower, EndsWith, kRefusedFile */
#include "recname.h"     /* recname::IsAreaId */
#include "worlddir.h"    /* coopworld::WorldNameOk, WorldFolderName, WorldTxtParseEarlier; coopcfg::kCfgWorldFieldMax, CfgLower */
#include <cstdio>

namespace worldreset {

enum { kResetKeep = 0, kResetMove = 1, kResetArea = 2, kResetUnknown = 3, kResetUnfinished = 4 };
const char* const kAsidePrefix = "before-reset-";            /* <world>\before-reset-<unix seconds>\: a folder an earlier build's reset left */
const char* const kKeptBuildingsPrefix = "keep-buildings-";  /* <reset world>\keep-buildings-<unix seconds>\: copies of the area records,
                                                                an earlier build's reset made when the players' buildings were kept */
const char* const kResetStagingSuffix = ".resetting";        /* <world folder>.resetting\, beside the world folder: the reset world while it
                                                                is made; renamed to the world folder's name last */
const char* const kOldWorldMid = " - before reset ";         /* "<name> - before reset <n>": the name of the world as it was */
const unsigned int kOldWorldNameTries = 1000;                /* names tried, n first and then up, when "<name> - before reset <n>" is taken */
/* The world server's exit code when its world folder is missing, <world folder>.resetting\ is beside it (a reset stopped between its two
   folder renames) and the server could not finish that reset itself at start (ResetFinishStaging refused or failed): a new, empty world
   there would hide both worlds. */
const int kServerExitResetHalfway = 35;

inline const char* ResetKindName(int k)
{
    return k == kResetKeep ? "keep" : k == kResetMove ? "state" : k == kResetArea ? "area" : k == kResetUnfinished ? "unfinished" : "unknown";
}

/* lowerName is prefix followed by one or more digits (a process id, or a reset's unix seconds). */
inline bool ResetPrefixDigits(const std::string& lowerName, const char* prefix)
{
    const std::string p(prefix);
    if (lowerName.size() <= p.size() || lowerName.compare(0, p.size(), p) != 0) return false;
    return lowerName.find_first_not_of("0123456789", p.size()) == std::string::npos;
}

/* T-985 C1 fold 1 (review LOW 7): a game's KEPT mirror folder, mirror-k<16 hex> (mirrorkeep.h StableDirName; lowerName lower-cased) */
inline bool ResetIsKeptMirror(const std::string& lowerName)
{
    const std::string p("mirror-k");
    if (lowerName.size() != p.size() + 16 || lowerName.compare(0, p.size(), p) != 0) return false;
    return lowerName.find_first_not_of("0123456789abcdef", p.size()) == std::string::npos;
}
/* A host game's mirror folder, mirror-<pid> or mirror-k<16 hex> (any case). */
inline bool ResetIsMirror(const std::string& name)
{
    const std::string l = worldkeep::Lower(name);
    return ResetPrefixDigits(l, "mirror-") || ResetIsKeptMirror(l);
}

/* The name lower-cased, with one trailing ".tmp" and then one trailing ".1" taken off: what a leftover or a backup belongs to. */
inline std::string ResetBaseName(const std::string& name)
{
    std::string l = worldkeep::Lower(name);
    if (worldkeep::EndsWith(l, ".tmp")) l.erase(l.size() - 4);
    if (worldkeep::EndsWith(l, ".1")) l.erase(l.size() - 2);
    return l;
}

/* A record's file (lower-cased name): <id>.platoon or <id>.meta, either with .prev or .refused after it. *id = the id as the
   file names it. */
inline int ResetRecordFile(const std::string& lowerName, std::string* id)
{
    std::string l(lowerName);
    if (worldkeep::EndsWith(l, ".prev")) l.erase(l.size() - 5);
    else if (worldkeep::EndsWith(l, ".refused")) l.erase(l.size() - 8);
    if (worldkeep::EndsWith(l, ".platoon")) l.erase(l.size() - 8);
    else if (worldkeep::EndsWith(l, ".meta")) l.erase(l.size() - 5);
    else return 0;
    if (l.empty()) return 0;
    if (id != 0) *id = l;
    return 1;
}

/* What a reset does with this entry at the top of the world folder. */
inline int ResetClassify(const std::string& name, int isDir)
{
    const std::string l = worldkeep::Lower(name);
    if (isDir)
    {
        if (l == "repair" || l == "recycle") return kResetMove;
        if (ResetPrefixDigits(l, "mirror-")) return kResetMove;               /* old-world copies of records a host game made */
        if (ResetIsKeptMirror(l)) return kResetMove;                         /* T-985 C1 fold 1: and a game's kept mirror-k<16 hex> folder */
        if (ResetPrefixDigits(l, kAsidePrefix)) return kResetUnfinished;      /* an earlier reset's delete that stopped part-way */
        if (ResetPrefixDigits(l, kKeptBuildingsPrefix)) return kResetKeep;    /* area records a reset set aside */
        return kResetUnknown;
    }
    const std::string b = ResetBaseName(name);
    if (worldkeep::EndsWith(l, ".log") || worldkeep::EndsWith(b, ".log")) return kResetKeep;
    static const char* const keep[] = { "world.txt", "format.txt", "slots.txt", "profiles.txt", "aliases.txt", "owner.txt", "options.txt",
                                        "mods.txt", "uid_seats.txt", "fallen.txt", "teams.txt", "own_high.txt", "world.gen", "server.lock",
                                        "auth.txt", "access.txt", worldkeep::kRefusedFile };   /* authproof::kAuthFile, accessrule::kAccessFile */
    static const char* const move[] = { "uniques.txt", "world_relations.txt", "town_losses.txt", "town_bars.txt", "owed_people.txt",
                                        "owed_done.txt", "owed_meta.txt", "research_boxes.txt", "research_takes.txt", "clock.txt", "areas.txt", "pendingpos.txt", "repair.txt",
                                        "queue.txt", "kept-pieces.txt", "kept-state.txt", "player-towns.txt", "kept-towns.txt",
                                        "recorded_areas.txt", "area_holders.txt" };   /* T-1084 step 3: the area-recorded marks (store_main.cpp RecordedAreasFile) and the holder history (AreaHoldersFile) belong to the old world's records */
    for (size_t i = 0; i < sizeof(keep) / sizeof(keep[0]); ++i) if (b == keep[i]) return kResetKeep;
    for (size_t i = 0; i < sizeof(move) / sizeof(move[0]); ++i) if (b == move[i]) return kResetMove;
    if (b.size() > 13 && b.compare(0, 8, "deleted.") == 0 && worldkeep::EndsWith(b, ".bits")) return kResetMove;
    std::string id;
    if (ResetRecordFile(b, &id)) return recname::IsAreaId(id) ? kResetArea : kResetMove;
    return kResetUnknown;
}

/* WHAT THE RESET WORLD GETS of one entry (its ResetClassify kind): Copy = a copy of it (a folder with everything below it), Buildings =
   read for the players' pieces into kept-pieces.txt (an area record, when the buildings are kept), Fresh = written new (world.txt with its .1 and .tmp:
   the reset world never holds a copy of the old world's id), None = nothing (the world lock - its first start makes its own - and the
   world's state, an unfinished folder and anything unknown, which the old world keeps). */
enum { kResetGetNone = 0, kResetGetCopy = 1, kResetGetBuildings = 2, kResetGetFresh = 3 };
inline int ResetWorldGets(const std::string& name, int kind, int keepBuildings)
{
    if (kind == kResetKeep)
    {
        const std::string b = ResetBaseName(name);
        if (b == "world.txt") return kResetGetFresh;
        if (b == "server.lock") return kResetGetNone;
        return kResetGetCopy;
    }
    return (kind == kResetArea && keepBuildings) ? kResetGetBuildings : kResetGetNone;
}

/* THE OLD WORLD'S NAME: "<name> - before reset <n>", name cut short just enough (and a space it then ends with taken off) that the whole
   is at most coopcfg::kCfgWorldFieldMax (48) characters, the world name's limit. "" = name is not a world name, n is 0, or the result
   does not pass coopworld::WorldNameOk. */
inline std::string ResetOldWorldName(const std::string& name, unsigned int n)
{
    if (n == 0 || !coopworld::WorldNameOk(name, 0)) return std::string();
    char b[16]; std::sprintf(b, "%u", n);
    const std::string suffix = std::string(kOldWorldMid) + b;
    if (suffix.size() >= coopcfg::kCfgWorldFieldMax) return std::string();
    std::string base(name);
    if (base.size() + suffix.size() > coopcfg::kCfgWorldFieldMax) base.erase(coopcfg::kCfgWorldFieldMax - suffix.size());
    while (!base.empty() && base[base.size() - 1] == ' ') base.erase(base.size() - 1);
    if (base.empty()) return std::string();
    const std::string out = base + suffix;
    return coopworld::WorldNameOk(out, 0) ? out : std::string();
}

/* The n of candidate when it is exactly the old world's name ResetOldWorldName gives for name; 0 = it is not one. */
inline unsigned int ResetOldWorldNameOf(const std::string& candidate, const std::string& name)
{
    const std::string mid(kOldWorldMid);
    const std::string::size_type at = candidate.rfind(mid);
    if (at == std::string::npos) return 0;
    const std::string d = candidate.substr(at + mid.size());
    if (d.empty() || d.size() > 9 || d[0] == '0' || d.find_first_not_of("0123456789") != std::string::npos) return 0;
    unsigned int n = 0;
    for (size_t i = 0; i < d.size(); ++i) n = n * 10u + (unsigned int)(d[i] - '0');
    return ResetOldWorldName(name, n) == candidate ? n : 0;
}
/* T-36: THE NAME BEFORE " - before reset <n>" in an old world's name exactly as ResetOldWorldName makes it - the name it was made from
   (cut short when the whole was too long: then not the whole earlier name); "" = not such a name. The records-store lookup's fallback
   for a world whose saves no longer carry its earlier name. */
inline std::string ResetOldWorldBase(const std::string& candidate)
{
    const std::string::size_type at = candidate.rfind(kOldWorldMid);
    if (at == std::string::npos || at == 0) return std::string();
    const std::string base = candidate.substr(0, at);
    return ResetOldWorldNameOf(candidate, base) != 0 ? base : std::string();
}
/* EVERY NAME BEFORE " - before reset <n>", taken off in turn (ResetOldWorldBase again on each result): a world as it was that was
   itself reset is "<name> - before reset <n> - before reset <m>", and its records stores may carry either earlier name. Outermost
   first; none for a name that is not an old world's. At most kOldWorldBasesMax (a world name is at most 48 characters, and each
   " - before reset <n>" is 17 or more). */
const unsigned int kOldWorldBasesMax = 8;
inline std::vector<std::string> ResetOldWorldBases(const std::string& candidate)
{
    std::vector<std::string> out;
    std::string c(candidate);
    for (unsigned int k = 0; k < kOldWorldBasesMax; ++k)
    {
        const std::string b = ResetOldWorldBase(c);
        if (b.empty() || b == c) break;
        out.push_back(b);
        c = b;
    }
    return out;
}

/* THE OLD WORLD'S NAME, FREE: ResetOldWorldName(name, k) for the first k from n up whose folder (coopworld::WorldFolderName - the way the
   world list names a world's folder) is none of taken (the names already in the folder the world folders sit in, any case); at most
   kOldWorldNameTries names. *used = that k. "" = none free, or no such name. */
inline std::string ResetOldWorldPick(const std::string& name, unsigned int n, const std::vector<std::string>& taken, unsigned int* used)
{
    for (unsigned int k = n; k < n + kOldWorldNameTries; ++k)
    {
        const std::string c = ResetOldWorldName(name, k);
        if (c.empty()) return std::string();
        const std::string f = coopworld::WorldFolderName(c);
        bool there = false;
        for (size_t i = 0; i < taken.size() && !there; ++i) there = coopcfg::CfgLower(taken[i]) == f;
        if (!there)
        {
            if (used != 0) *used = k;
            return c;
        }
    }
    return std::string();
}

/* THIS RESET'S n: 1 + the ids world.txt line 3 lists (coopworld::WorldTxtParseEarlier) - each reset adds there the id it replaced, so a
   world's first reset gives 1. No line 3, or one this build cannot read, gives 1. Line 3 keeps at most coopworld::kWorldEarlierIdsMax
   ids, so from that many resets on n stays at kWorldEarlierIdsMax + 1 (ResetOldWorldPick then moves it up to a free name). */
inline unsigned int ResetCountNext(const std::string& worldTxt)
{
    std::vector<std::string> ids;
    if (!coopworld::WorldTxtParseEarlier(worldTxt, &ids, 0, 0)) return 1;
    return (unsigned int)ids.size() + 1u;
}

/* A LEFTOVER <world folder>.resetting\ IS USED ONLY WHEN IT IS THIS WORLD'S RESET: the reset writes the stage's world.txt with line 3
   ending in the id the world folder's world.txt holds (coopworld::WorldTxtAfterReset puts the replaced id last). stageTxt = the stage's
   world.txt ("" = none), worldTxt = the world's. A WORLD WITH NO ID LINE: its reset gives the stage an id and the world's own earlier
   ids unchanged (none when it has no line 3 this build can read), so such a stage is this reset when it has an id line and exactly
   those earlier ids. False for a stage with no world.txt (a copy that stopped before it), another id last (another world's, or an
   earlier reset's), or a world.txt that cannot be read: such a stage is moved aside (ResetStaleName) and a fresh one made. */
inline bool ResetStageIsThisReset(const std::string& stageTxt, const std::string& worldTxt)
{
    std::string id, stageId;
    bool up = false;
    std::vector<std::string> ids;
    const bool listed = coopworld::WorldTxtParseEarlier(stageTxt, &ids, 0, 0) && !ids.empty();
    if (coopworld::WorldTxtParseId(worldTxt, &id, &up)) return listed && ids[ids.size() - 1] == id;
    if (!coopworld::WorldTxtParse(worldTxt, 0, 0) || !coopworld::WorldTxtParseId(stageTxt, &stageId, &up)) return false;
    std::vector<std::string> worldIds;
    if (!coopworld::WorldTxtParseEarlier(worldTxt, &worldIds, 0, 0)) worldIds.clear();
    if (!listed) ids.clear();
    return ids == worldIds;
}
/* FINISHING A RESET THAT STOPPED BETWEEN ITS RENAMES (the world folder missing): the stage is renamed into its place only when the
   world as it was is beside it - a folder whose world.txt (oldTxt) holds the id the stage lists last and names the world
   "<the stage's name> - before reset <n>" (ResetOldWorldNameOf). */
inline bool ResetStageFinishes(const std::string& stageTxt, const std::string& oldTxt)
{
    std::string stageName, oldName;
    if (!coopworld::WorldTxtParse(stageTxt, &stageName, 0) || !coopworld::WorldTxtParse(oldTxt, &oldName, 0)) return false;
    return ResetStageIsThisReset(stageTxt, oldTxt) && ResetOldWorldNameOf(oldName, stageName) > 0;
}
/* THE NAME A STALE STAGE IS MOVED ASIDE TO: "<stage>.stale-<unix seconds>", with "-<k>" after it for try k (from 1) when that is taken.
   Never deleted. */
inline std::string ResetStaleName(const std::string& stage, long long unixNow, unsigned int k)
{
    char b[64];
    if (k == 0) std::sprintf(b, ".stale-%lld", unixNow);
    else std::sprintf(b, ".stale-%lld-%u", unixNow, k);
    return stage + b;
}
/* A FOLDER RENAME OF THE RESET IS TRIED AGAIN when Windows refused it for a moment - right after the reset's many writes an antivirus
   or the search indexer can hold a file in the folder: access denied (5), a sharing violation (32) or a lock violation (33). At most
   kResetRenameTries tries in all; before try k+1 the wait is ResetRenameWaitMs(k) (250 ms, then 500, 750, 1000: 2.5 s at most). Any
   other refusal is final at once. */
const unsigned int kResetRenameTries = 5;
inline bool ResetRenameRetryWanted(unsigned long err, unsigned int triesDone)
{
    return (err == 5ul || err == 32ul || err == 33ul) && triesDone < kResetRenameTries;
}
inline unsigned int ResetRenameWaitMs(unsigned int triesDone) { return 250u * triesDone; }

/* ---- CARRYING A PLAYER INTO THE RESET WORLD - the game side (src/coop-plugin/store.cpp ResetCarryAtWelcome, StoreResetCarryPost) ----
   A reset world has a new id, so a profile's save of the world as it was carries an id the world now lists on world.txt line 3
   (coopworld::WorldTxtParseEarlier). At that profile's next start the save is not loaded: Kenshi's own IMPORT GAME runs from it into
   a new game of the reset world, with the tick boxes below, and the player's records store is copied to the reset world's id (the
   world as it was keeps its own). */

/* KENSHI'S IMPORT GAME REQUEST (SaveManager+0xA4, read by SaveManager::importGame 0x3788F0; 1.0.65 addresses). The import screen ORs
   the flag of every ticked box - each box sits in the screen's box list at the slot that is its flag, and 0x47A210 ORs the slot of
   each ticked box - then adds 2 (0x47B4AA). The slots come from the screen's builder (0x481000-0x4815A4: each box's label, then
   `mov edx, <slot>`). */
const unsigned int kImportResetPositions = 0x01u;   /* "Reset squad positions": every imported squad is put at the start (decomp_3788f0.txt:386, 809) */
const unsigned int kImportScreenBit      = 0x02u;   /* always added by the screen; importGame tests no 2 */
const unsigned int kImportBuildings      = 0x04u;   /* "Import Buildings": the old save's zone files, the importer's own faction only (decomp_3788f0.txt:153, 215) */
const unsigned int kImportResearch       = 0x08u;   /* "Import Research": Research::load from the old save; without it the "_Default Start" research (decomp_3788f0.txt:691-703) */
const unsigned int kImportDeadNpcs       = 0x10u;   /* "Import Dead NPCs": the towns' discovery load with its override on, and the old save's unique-character state (decomp_3788f0.txt:159, 268) */
const unsigned int kImportRelations      = 0x20u;   /* "Import Relations": every faction's relation record, all or nothing (0x9EE610, decomp_3788f0.txt:816) */
/* The reset world's import: research kept; squads stay where they stood (decision 664); dead named characters start over; buildings
   off (they come back from the world server's own records); relations off (all or nothing for every faction - the player's own
   standings belong to the player's records). Map knowledge and money come across whatever the boxes say. = 0x0A. */
inline unsigned int ResetImportFlags() { return kImportScreenBit | kImportResearch; }

/* One save folder of this profile carrying a world id, under any world name (the earlier ids decide): its world id (the key file's
   field 8) and its quick.save. */
struct ResetCarryCand
{
    std::string worldId; bool quickSave; unsigned long long saveTime;   /* saveTime: quick.save's last write (FILETIME), 0 = none */
    ResetCarryCand() : quickSave(false), saveTime(0ULL) {}
};
/* WHICH SAVE THE RESET WORLD IMPORTS for this profile: -1 = none - a folder of the world's own id was found (the import's first save
   made it: the player is in the reset world already), the world lists no earlier id, or no folder of an earlier id holds a quick.save.
   Else the folder whose id the world had last (latest on line 3), then the newest quick.save, then the lower index. */
inline int ResetCarryPick(bool ownFound, const std::vector<std::string>& earlier, const std::vector<ResetCarryCand>& c)
{
    if (ownFound || earlier.empty()) return -1;
    int best = -1;
    size_t bestRank = 0;
    for (size_t i = 0; i < c.size(); ++i)
    {
        if (!c[i].quickSave || c[i].worldId.empty()) continue;
        size_t rank = 0;
        bool listed = false;
        for (size_t k = 0; k < earlier.size(); ++k) if (earlier[k] == c[i].worldId) { rank = k; listed = true; }
        if (!listed) continue;
        if (best < 0 || rank > bestRank || (rank == bestRank && c[i].saveTime > c[(size_t)best].saveTime)) { best = (int)i; bestRank = rank; }
    }
    return best;
}
/* WHEN THE CARRY IS DECIDED: at every WELCOME that admits the pick while the reset world has no save of its own for it - no folder of
   the reset world's id was found (ownFound), or the one found holds no quick.save (ownSaved false: its save never finished). The
   import is posted again from the save of the world as it was at each start until the reset world's own folder is saved. */
inline bool ResetCarryWanted(bool ownFound, bool ownSaved) { return !ownFound || !ownSaved; }
/* THE CARRY ENDS, in the pick it was decided for, once the reset world's own folder holds a finished save (saved = its quick.save is
   there) of the reset world: its key file names the reset world's id (newId), or names none (keyId "": no key file read). A key file
   naming another id is not this world's save. After it the load gate refuses the import of the save of the world as it was. */
inline bool ResetCarryEnds(bool carryLive, bool saved, const std::string& keyId, const std::string& newId)
{
    if (!carryLive || !saved) return false;
    return keyId.empty() || coopworld::SaveIdSameWorld(keyId, newId);
}
/* THE SAVE RIGHT AFTER THE IMPORT: armed once the import the load gate let through (owed; worldAtPost = the world load count when it
   was posted) has built its world - the load count has moved and the world runs - while the carry is still decided for this pick.
   The save is the save after character creation's (OwnCreatedSaveTick): it waits until the leave save is wanted, then makes the
   reset world's folder, so a crash or a quit right after the import does not lose the imported squad. */
inline bool ResetImportSaveArm(bool owed, bool carryLive, long worldAtPost, long worldNow, bool worldRunning)
{
    return owed && carryLive && worldNow != worldAtPost && worldRunning;
}
/* ...AND ONLY ONCE KENSHI HAS READ THE PLAYER'S SQUADS IN: Kenshi reads each squad's characters into it a few seconds after the
   world's first running frame; a save before that read stores the squad with no characters, and the read does not come after it.
   listRead = the player's squad lists could be read, awake = the player's awake squads, read = how many of their character
   containers Kenshi has read, unreadable = awake squads whose container could not be read. Read only when every awake squad is read
   and there is at least one: the reset world always holds the player's squad, so none awake means it is not built yet. */
inline bool ResetImportSquadsRead(bool listRead, int awake, int read, int unreadable)
{
    return listRead && unreadable == 0 && awake > 0 && read == awake;
}

/* THE RESET BOX, shown once to each player carried into a reset world (store.cpp ResetBoxTick). Buildings are "kept" only when every
   reset from the carried save's world id on kept them: ids = the world's earlier ids oldest first (the WELCOME's history), kept[i] = 1
   when the reset that replaced ids[i] kept the players' buildings. False for an id the history does not list. */
inline bool ResetBoxKept(const std::vector<std::string>& ids, const std::vector<int>& kept, const std::string& carriedId)
{
    if (ids.size() != kept.size() || carriedId.empty()) return false;
    for (size_t i = 0; i < ids.size(); ++i)
        if (ids[i] == carriedId)
        {
            for (size_t k = i; k < ids.size(); ++k) if (kept[k] == 0) return false;
            return true;
        }
    return false;
}
/* The box's words (owner-approved), its title, and its memo in the player's records store of the reset world: one reset world id per
   line - a second start in that world does not show it again. */
inline std::string ResetBoxText(bool kept)
{
    return std::string("The host reset this world. Your characters, money, research, equipment, map and faction standings came with you. ")
         + (kept ? "Your buildings and stored items came too." : "Your buildings and stored items were not kept.")
         + " Everything else has started over.";
}
inline const char* ResetBoxTitle() { return "WORLD RESET"; }
const char* const kResetBoxMemo = "reset-box.txt";
inline bool ResetBoxShownIn(const std::string& memo, const std::string& id)
{
    if (id.empty()) return false;
    std::string::size_type from = 0;
    for (;;)
    {
        const std::string::size_type nl = memo.find('\n', from);
        std::string line = memo.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
        while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == ' ')) line.erase(line.size() - 1);
        if (line == id) return true;
        if (nl == std::string::npos) return false;
        from = nl + 1;
    }
}
inline std::string ResetBoxShownAdd(const std::string& memo, const std::string& id)
{
    if (id.empty() || ResetBoxShownIn(memo, id)) return memo;
    std::string m(memo);
    if (!m.empty() && m[m.size() - 1] != '\n') m += "\n";
    return m + id + "\n";
}

/* THE PLAYER'S RECORDS STORE GOES WITH THE PLAYER AND STAYS WITH THE WORLD AS IT WAS: the store named for the earlier id
   (coopown::PlayerStoreName with that id) is copied, the whole folder, to the one named for the reset world's id - never moved, so
   the world as it was (its id unchanged under its new name) still has its players' records when it is played again. */
enum { kRekeyCopy = 0, kRekeyNoOld = 1, kRekeyNewThere = 2, kRekeyOldOpen = 3, kRekeyNoName = 4 };
inline int ResetRekeyDecide(const std::string& oldName, const std::string& newName, bool oldThere, bool newThere, bool oldOpen)
{
    if (oldName.empty() || newName.empty() || worldkeep::Lower(oldName) == worldkeep::Lower(newName)) return kRekeyNoName;
    if (!oldThere) return kRekeyNoOld;
    if (newThere) return kRekeyNewThere;
    if (oldOpen) return kRekeyOldOpen;
    return kRekeyCopy;
}
inline const char* ResetRekeyName(int v)
{
    return v == kRekeyCopy ? "copied" : v == kRekeyNoOld ? "this player has no records store of the world as it was"
         : v == kRekeyNewThere ? "a records store of the reset world is there already - both are left as they are"
         : v == kRekeyOldOpen ? "the old store is open - not copied" : v == kRekeyNoName ? "no store name" : "?";
}

/* THE PLAYER SQUADS KENSHI HAS READ IN SINCE THE IMPORT, AS THE LOAD HOOK SAW THEM: the platoons whose character-container load
   returned success. A fixed list (no allocation: the load hook also runs off the main thread, and the caller holds its own lock
   around every call); emptied when an import is let through and at every world load, and a platoon leaves it when the engine frees
   it (the address can be reused). full = notes refused because the list was full. */
enum { kSquadReadCap = 64 };
struct SquadReadSet { void* p[kSquadReadCap]; int n; long long full; };
inline void SquadReadClear(SquadReadSet* s) { s->n = 0; }
inline bool SquadReadHas(const SquadReadSet* s, const void* platoon)
{
    for (int i = 0; i < s->n; ++i) if (s->p[i] == platoon) return true;
    return false;
}
/* 1 = noted now, 0 = already noted (or no platoon), -1 = the list is full (counted in full, not noted) */
inline int SquadReadNote(SquadReadSet* s, void* platoon)
{
    if (platoon == 0 || SquadReadHas(s, platoon)) return 0;
    if (s->n >= kSquadReadCap) { ++s->full; return -1; }
    s->p[s->n++] = platoon;
    return 1;
}
inline void SquadReadForget(SquadReadSet* s, const void* platoon)
{
    for (int i = 0; i < s->n; ++i)
        if (s->p[i] == platoon) { s->p[i] = s->p[s->n - 1]; --s->n; return; }
}

}   /* namespace worldreset */
