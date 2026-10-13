/* permdelete.h - DELETE WORLD and DELETE PROFILE delete for good (owner ruling 682: no Recycle Bin). The pure parts, the
 * SAME header the plugin (store.cpp) and the offline suite (t714_permanent_delete) compile:
 *   PathInside         - a folder is deleted only when its full path lies strictly inside the root it belongs to (the world store,
 *                        Kenshi's save root or the records stores' folder): no "." or ".." part, no empty part, never the root itself.
 *   EntryKindOf        - what the walk does with one listed entry: a link or junction is removed itself and never entered, so the
 *                        delete never reaches outside the folder; a folder is walked; anything else is a file.
 *   SaveDirGoesWithWorld / SaveDirGoesWithProfile / WorldDeleteFind - which save folders go with a deleted world or profile: the
 *                        key file names the world and carries the world's id, or no id at all (a save made before world ids,
 *                        owner 683). A folder carrying another id is another world's of that name and stays.
 *   StoreNamesToDelete - the records store names for those profiles in that world: named with the world's id, and as named
 *                        before ids.
 *   TopStateOf / ClearReadOnlyFirst / LastRank - the walk's steps: only "not found" reads as already gone; read-only is cleared
 *                        on files and folders, never through a link; world.txt and then the being-deleted marker go last.
 *   UnfinishedDeleteFolder / PathDirectChild / Pending* - a delete that stopped is finished later: a world folder carrying the
 *                        being-deleted marker, and a profile delete's line in the unfinished-deletes list.
 */
#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include "profiles.h"   /* coopprof::SaveDirSeen, SaveDirNamesWorld, WorldLeftovers, LeftoverAddOnce, ProfileId */
#include "ownrec.h"     /* coopown::PlayerStoreNamesFor - a records store's name */

namespace swdel {

const unsigned long kAttrDirectory = 0x10;    /* FILE_ATTRIBUTE_DIRECTORY */
const unsigned long kAttrReparse   = 0x400;   /* FILE_ATTRIBUTE_REPARSE_POINT: a junction, a symbolic link or another reparse point */
const unsigned long kAttrReadOnly  = 0x1;     /* FILE_ATTRIBUTE_READONLY */

/* The comparison form of a path: '/' read as '\\', ASCII letters lower case (Windows names ignore case), trailing separators dropped. */
inline std::string PathNorm(const std::string& p)
{
    std::string s(p);
    for (std::string::size_type i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c == '/') s[i] = '\\';
        else if (c >= 'A' && c <= 'Z') s[i] = (char)(c - 'A' + 'a');
    }
    while (!s.empty() && s[s.size() - 1] == '\\') s.erase(s.size() - 1);
    return s;
}
/* A full path (in PathNorm's form): a drive ("c:" or "c:\\...") or a network share ("\\\\server\\..."). */
inline bool PathFull(const std::string& n)
{
    if (n.size() >= 2 && n[1] == ':' && n[0] >= 'a' && n[0] <= 'z') return n.size() == 2 || n[2] == '\\';
    return n.size() > 2 && n[0] == '\\' && n[1] == '\\' && n[2] != '\\';
}
/* A "." or ".." part, or an empty part (past a share's leading "\\\\"). */
inline bool PathOddPart(const std::string& n)
{
    std::string::size_type from = (n.size() >= 2 && n[0] == '\\' && n[1] == '\\') ? 2 : 0;
    while (from <= n.size())
    {
        std::string::size_type e = n.find('\\', from);
        if (e == std::string::npos) e = n.size();
        const std::string part = n.substr(from, e - from);
        if (part.empty() || part == "." || part == "..") return true;
        from = e + 1;
    }
    return false;
}
/* How many parts a full path (PathNorm's form) has below its drive or its server: "c:\\data\\worlds" and "\\\\nas\\share\\saves"
   = 2, "c:\\data" and "\\\\nas\\share" = 1, "c:" and "\\\\nas" = 0. */
inline int PathPartsBelowTop(const std::string& n)
{
    std::string::size_type from = std::string::npos;
    if (n.size() >= 2 && n[1] == ':') from = 2;
    else if (n.size() > 2 && n[0] == '\\' && n[1] == '\\') from = n.find('\\', 2);
    if (from == std::string::npos) return 0;
    int parts = 0;
    for (std::string::size_type i = from; i < n.size(); ++i) if (n[i] == '\\') ++parts;
    return parts;
}
const int kMinRootParts = 2;   /* a root needs at least this many parts below its drive or server */
/* IS path STRICTLY INSIDE root? Both full paths with no odd part; case, '/' and trailing separators ignored. The root itself, a
   sibling whose name only starts with the root's, a relative path and another drive are all outside. A root with fewer than
   two parts below its drive or server (C:\\, C:\\Data, \\\\server, \\\\server\\share) has nothing inside it. */
inline bool PathInside(const std::string& path, const std::string& root)
{
    const std::string p = PathNorm(path), r = PathNorm(root);
    if (!PathFull(r) || !PathFull(p) || PathOddPart(r) || PathOddPart(p) || PathPartsBelowTop(r) < kMinRootParts) return false;
    return p.size() > r.size() + 1 && p.compare(0, r.size(), r) == 0 && p[r.size()] == '\\';
}

/* ONE LISTED ENTRY: skipped ("." / ".."), removed as a link (a reparse point - a junction or a folder link is removed with the
   folder call, a file link with the file call; its target is never touched), walked (a folder) or deleted (a file). */
enum EntryKind { kEntrySkip = 0, kEntryFile, kEntryDir, kEntryLink };
inline int EntryKindOf(const std::string& name, unsigned long attrs)
{
    if (name.empty() || name == "." || name == "..") return kEntrySkip;
    if ((attrs & kAttrReparse) != 0) return kEntryLink;
    if ((attrs & kAttrDirectory) != 0) return kEntryDir;
    return kEntryFile;
}
/* A whole file name, case ignored (the file a folder's delete leaves to last). */
inline bool NameIs(const std::string& name, const char* want)
{
    return want != 0 && PathNorm(name) == PathNorm(want);
}
/* path inside root with no further part: a folder directly in root. */
inline bool PathDirectChild(const std::string& path, const std::string& root)
{
    if (!PathInside(path, root)) return false;
    return PathNorm(path).find('\\', PathNorm(root).size() + 1) == std::string::npos;
}

/* THE TOP FOLDER'S ATTRIBUTES before a delete: read = there; not read with "file not found" (2) or "path not found" (3) =
   already gone; not read for any other reason (access denied, a sharing or network error) = unreadable, a stop. */
enum TopState { kTopPresent = 0, kTopGone, kTopUnreadable };
inline int TopStateOf(bool attrsRead, unsigned long err)
{
    if (attrsRead) return kTopPresent;
    return (err == 2 || err == 3) ? kTopGone : kTopUnreadable;
}
/* READ-ONLY IS TAKEN OFF before a delete only on a plain file or folder: on a link or junction the attribute call would reach
   its target, so a link keeps its flags. */
inline bool ClearReadOnlyFirst(unsigned long attrs)
{
    return (attrs & kAttrReadOnly) != 0 && (attrs & kAttrReparse) == 0;
}

/* THE BEING-DELETED MARKER: written into a world folder before its walk and deleted last, after world.txt. A folder carrying it is
   a delete that stopped: NEW WORLD on that name and the next title-screen start finish it. */
const char* const kDeleteMarkFile = "being-deleted.txt";
/* WHEN A FILE OF THE TOP FOLDER GOES: 0 = in the walk's order, 1 = after everything else (lastName), 2 = the very last (the
   being-deleted marker). Files below the top folder: 0. */
inline int LastRank(const std::string& name, const char* lastName, int depth)
{
    if (depth != 0) return 0;
    if (NameIs(name, kDeleteMarkFile)) return 2;
    if (NameIs(name, lastName)) return 1;
    return 0;
}
/* AN UNFINISHED DELETE: a real folder (not a link) holding the being-deleted marker as a plain file. */
inline bool UnfinishedDeleteFolder(bool dirRead, unsigned long dirAttrs, bool markRead, unsigned long markAttrs)
{
    if (!dirRead || (dirAttrs & kAttrDirectory) == 0 || (dirAttrs & kAttrReparse) != 0) return false;
    return markRead && (markAttrs & (kAttrDirectory | kAttrReparse)) == 0;
}

/* THE UNFINISHED-DELETES LIST - <data folder>\\unfinished-deletes.txt, one line per profile delete under way:
   "profdel1<TAB>world<TAB>world id<TAB>person id<TAB>profile number<TAB>stopped at". The line is written before the delete
   starts and dropped once it is done; the next title-screen start runs each line's delete again. stoppedAt = a save folder the
   delete emptied but could not remove ("" = none). */
const char* const kPendingDeletesFile = "unfinished-deletes.txt";
const char* const kPendingTag = "profdel1";
struct PendingProfileDelete
{
    std::string worldKey, worldId, person, stoppedAt;
    unsigned num;
    PendingProfileDelete() : num(0) {}
};
inline bool PendingFieldOk(const std::string& s) { return s.find_first_of("\t\r\n") == std::string::npos; }
/* the line, or "" when it cannot be one (no world, no person, number 0, or a tab or line break in a field) */
inline std::string PendingLine(const PendingProfileDelete& p)
{
    if (p.worldKey.empty() || p.person.empty() || p.num == 0) return std::string();
    if (!PendingFieldOk(p.worldKey) || !PendingFieldOk(p.worldId) || !PendingFieldOk(p.person) || !PendingFieldOk(p.stoppedAt)) return std::string();
    std::string n;
    unsigned v = p.num;
    do { n.insert(n.begin(), (char)('0' + v % 10)); v /= 10; } while (v != 0);
    return std::string(kPendingTag) + "\t" + p.worldKey + "\t" + p.worldId + "\t" + p.person + "\t" + n + "\t" + p.stoppedAt;
}
/* one line read back: six fields, the tag, a world, a person and a number 1..999999999; a trailing CR is ignored */
inline bool PendingParse(const std::string& lineIn, PendingProfileDelete* out)
{
    std::string line(lineIn);
    while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == '\n')) line.erase(line.size() - 1);
    std::vector<std::string> f;
    std::string::size_type from = 0;
    for (;;)
    {
        const std::string::size_type t = line.find('\t', from);
        f.push_back(line.substr(from, t == std::string::npos ? std::string::npos : t - from));
        if (t == std::string::npos) break;
        from = t + 1;
    }
    if (f.size() != 6 || f[0] != kPendingTag || f[1].empty() || f[3].empty() || f[4].empty() || f[4].size() > 9) return false;
    unsigned n = 0;
    for (size_t i = 0; i < f[4].size(); ++i)
    {
        if (f[4][i] < '0' || f[4][i] > '9') return false;
        n = n * 10 + (unsigned)(f[4][i] - '0');
    }
    if (n == 0) return false;
    out->worldKey = f[1]; out->worldId = f[2]; out->person = f[3]; out->num = n; out->stoppedAt = f[5];
    return true;
}
/* the same delete: world, world id, person and number (where it stopped aside) */
inline bool PendingSame(const PendingProfileDelete& a, const PendingProfileDelete& b)
{
    return a.worldKey == b.worldKey && a.worldId == b.worldId && a.person == b.person && a.num == b.num;
}
/* the list's lines (a file's text: CRLF or LF, empty lines left out) */
inline std::vector<std::string> PendingSplit(const std::string& text)
{
    std::vector<std::string> out;
    std::string::size_type from = 0;
    while (from < text.size())
    {
        std::string::size_type e = text.find('\n', from);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(from, e - from);
        if (!l.empty() && l[l.size() - 1] == '\r') l.erase(l.size() - 1);
        if (!l.empty()) out.push_back(l);
        from = e + 1;
    }
    return out;
}
/* THE LIST WITH p SET: every readable line of another delete kept, an unreadable one left out, the line of this delete removed -
   then, keep = true, this delete's line (its stoppedAt as given) added last. */
inline std::vector<std::string> PendingListSet(const std::vector<std::string>& lines, const PendingProfileDelete& p, bool keep)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        PendingProfileDelete q;
        if (!PendingParse(lines[i], &q) || PendingSame(q, p)) continue;
        out.push_back(PendingLine(q));
    }
    if (keep)
    {
        const std::string l = PendingLine(p);
        if (!l.empty()) out.push_back(l);
    }
    return out;
}

/* A SAVE FOLDER GOES WITH ITS WORLD: its key file carries the world's id, whatever world name it carries (T-36: the name the world had
   when the save was made - the world as it was before a reset keeps its id under a new name); or it names the world and carries no id
   at all (made before world ids). A world with no id takes only folders that name it and carry no id. */
inline bool SaveDirGoesWithWorld(const coopprof::SaveDirSeen& d, const std::string& worldKey, const std::string& worldId)
{
    if (worldKey.empty()) return false;
    if (d.keyed && coopworld::SaveIdSameWorld(d.worldId, worldId)) return true;
    return coopprof::SaveDirNamesWorld(d, worldKey) && d.worldId.empty();
}
/* ... and with its profile: the key file names this profile (a key file naming none is profile 1's, as the key scan reads it). */
inline bool SaveDirGoesWithProfile(const coopprof::SaveDirSeen& d, const std::string& worldKey, const std::string& worldId,
                                   const std::string& profileId, unsigned num)
{
    if (!SaveDirGoesWithWorld(d, worldKey, worldId)) return false;
    return d.profile.empty() ? num == 1 : d.profile == profileId;
}
/* T-36: A WORLD RESET FROM THE ONE BEING DELETED - a world folder whose world.txt names its id (line 2) and lists the deleted world's
   id among its earlier ids (line 3). unread = a world folder whose world.txt is there but could not be read (held by the world server,
   a read error): it may be such a world, so every save that could be an import source is kept (fail toward keeping). */
struct ResetChildWorld
{
    std::string id; std::vector<std::string> earlier; bool unread;
    ResetChildWorld() : unread(false) {}
};
/* T-36: A SAVE FOLDER A RESET WORLD STILL IMPORTS FROM: a save with its quick.save whose id a world reset from it lists as earlier,
   while no save folder of the same profile carries that reset world's id with a quick.save of its own - that world's start imports
   the player's characters from it (worldreset::ResetCarryPick, ResetCarryWanted) until its own folder is saved. Any save with its
   quick.save and a world id, while a world folder's world.txt could not be read (unread). Used by DELETE WORLD and profile delete. */
inline bool SaveDirCarriedOnward(const std::vector<coopprof::SaveDirSeen>& dirs, size_t i, const std::vector<ResetChildWorld>& children)
{
    if (i >= dirs.size()) return false;
    const coopprof::SaveDirSeen& d = dirs[i];
    if (!d.keyed || d.worldId.empty() || d.saveTime == 0ULL) return false;
    for (size_t c = 0; c < children.size(); ++c)
    {
        const ResetChildWorld& w = children[c];
        if (w.unread) return true;   /* a world.txt that could not be read: this save may be its import source */
        if (w.id.empty() || std::find(w.earlier.begin(), w.earlier.end(), d.worldId) == w.earlier.end()) continue;
        bool own = false;
        for (size_t j = 0; j < dirs.size() && !own; ++j)
            if (dirs[j].keyed && dirs[j].saveTime != 0ULL && dirs[j].profile == d.profile && coopworld::SaveIdSameWorld(dirs[j].worldId, w.id)) own = true;
        if (!own) return true;
    }
    return false;
}
/* A DELETED WORLD'S SAVES ON THIS COMPUTER: folders = every save folder that goes with the world (above), any profile and any
   folder name; playing = such a folder this game plays from (keep: the settings' slot, the picked profile's folder) - in use,
   left; playingIds = those folders' profiles. worldKeys = the name asked for, then each other spelling a key file carries;
   profileIds = each folder's profile (none named = this person's profile 1), then this person's numbers 1..sweep, so a records
   store whose save folder is already gone is still found. No world name = nothing. */
inline coopprof::WorldLeftovers WorldDeleteFind(const std::vector<coopprof::SaveDirSeen>& dirs, const std::string& worldName, const std::string& worldId,
                                                const std::vector<std::string>& keep, const std::string& person, unsigned sweep,
                                                const std::vector<ResetChildWorld>* children = 0, std::vector<size_t>* carried = 0)
{
    coopprof::WorldLeftovers o;
    if (worldName.empty()) return o;
    coopprof::LeftoverAddOnce(&o.worldKeys, worldName);
    for (size_t i = 0; i < dirs.size(); ++i)
    {
        if (!SaveDirGoesWithWorld(dirs[i], worldName, worldId)) continue;
        bool played = false;
        for (size_t k = 0; k < keep.size() && !played; ++k) if (!keep[k].empty() && coopprof::SameFolderName(dirs[i].name, keep[k])) played = true;
        if (!played && children != 0 && SaveDirCarriedOnward(dirs, i, *children)) { played = true; if (carried != 0) carried->push_back(i); }   /* T-36: a reset world still imports from it */
        const std::string id = !dirs[i].profile.empty() ? dirs[i].profile : (person.empty() ? std::string() : coopprof::ProfileId(person, 1));
        if (played) { o.playing.push_back(i); coopprof::LeftoverAddOnce(&o.playingIds, id); } else o.folders.push_back(i);
        coopprof::LeftoverAddOnce(&o.worldKeys, dirs[i].world);
        coopprof::LeftoverAddOnce(&o.profileIds, id);
    }
    if (!person.empty()) for (unsigned n = 1; n <= sweep; ++n) coopprof::LeftoverAddOnce(&o.profileIds, coopprof::ProfileId(person, n));
    return o;
}
/* THE RECORDS STORES that go: every store name for these world spellings and profile ids named with the world's id, then the
   same names as made before ids (a world with no id: those alone). Each once. */
inline std::vector<std::string> StoreNamesToDelete(const std::vector<std::string>& worldKeys, const std::vector<std::string>& profileIds,
                                                   const std::string& worldId)
{
    std::vector<std::string> out = coopown::PlayerStoreNamesFor(worldKeys, profileIds, worldId);
    if (worldId.empty()) return out;
    const std::vector<std::string> before = coopown::PlayerStoreNamesFor(worldKeys, profileIds);
    for (size_t i = 0; i < before.size(); ++i)
        if (std::find(out.begin(), out.end(), before[i]) == out.end()) out.push_back(before[i]);
    return out;
}

}   /* namespace swdel */
