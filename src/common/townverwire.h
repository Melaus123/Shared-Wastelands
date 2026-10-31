/* src/common/townverwire.h - ONE TOWN VERSION PER PLACED TOWN, KEPT BY THE WORLD SERVER (message 87 TOWN_VER). Pure: the world
 * server (src/coop-store/store_main.cpp), the game (src/coop-plugin/townver.cpp, store.cpp, zones.cpp) and the offline suite
 * (src/coop-test) compile this same header.
 *
 * WHAT IS STORED. A town can carry other versions (destroyed, abandoned, run by another faction). The engine keeps the applied one
 * as one pointer on the town (Town+0x338, 0 = the town's own record). One record per PLACED town: key = "<the town's own record
 * sid>@<x>,<z>" (its position rounded to whole metres - one town record is placed several times, e.g. Hive Village x8), holding
 * the applied version's sid ("" = the town's own record), the town's owner faction sid after the switch ("" = none read), a
 * revision and the slot of the game that wrote it.
 *
 * WHO WRITES. Only the game that RUNS the town's area (its holder) writes, after the engine's own chooser
 * (ZoneManager::checkForRepopulateTown 0x9FE6D0) ran there, or - for a world saved before this record existed - its save's
 * version once the world server's whole list has arrived and holds nothing for the town. THE REVISION RULE: the world server
 * accepts a write only when its revision is the stored one + 1 (1 when nothing is stored); otherwise it answers REFUSED carrying
 * the stored record (revision 0 = none), and the writer takes that record as the truth and applies it at its next chance. So two
 * games can never flip-flop a town.
 *
 * WIRE: u8 kind, three zero bytes, str key, str ver, str fac, u32 rev, i32 slot; a string is u32 length + bytes (<= kTvStrMax), no
 * tab / CR / LF / NUL inside (the file is tab-separated lines). Nothing may follow the last field.
 *   up   1 WRITE    the holder's decision (slot is ignored - the world server writes the sender's own slot)
 *   down 2 STORED   a record the world server holds: every one before the first index page a game asks for ahead of its load
 *                   (preload.h), every one in the record feed's first page, and each accepted write to every OTHER game whose
 *                   feed is on (TvForwardTo)
 *   down 3 REFUSED  to the writer only: its revision was not stored + 1; carries the stored record (rev 0 = none stored)
 * FILE (world folder town_versions.txt): "v1\t<key>\t<ver>\t<fac>\t<rev>\t<slot>" one line per placed town. */
#ifndef COOP_COMMON_TOWNVERWIRE_H
#define COOP_COMMON_TOWNVERWIRE_H

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cooptv {

const unsigned int kMsgTownVer = 87u;   /* 28-86 are taken on the world-server link (store_main.cpp's enum) */
const unsigned int kTvProtocol = 125u;   /* the world-server protocol that carries TOWN_VER */
enum { kTvWrite = 1, kTvStored = 2, kTvRefused = 3 };
const size_t kTvStrMax = 512;

struct TownVer
{
    std::string key, ver, fac;
    unsigned int rev;
    int slot;
    TownVer() : rev(0), slot(-1) {}
};
struct TvMsg { unsigned int kind; TownVer rec; TvMsg() : kind(0) {} };

inline bool TvCleanStr(const std::string& s)
{
    if (s.size() > kTvStrMax) return false;
    for (size_t i = 0; i < s.size(); ++i) if (s[i] == '\t' || s[i] == '\r' || s[i] == '\n' || s[i] == 0) return false;
    return true;
}
inline void TvPut32(std::vector<char>* b, unsigned int v) { for (int i = 0; i < 4; ++i) b->push_back((char)((v >> (8 * i)) & 0xFFu)); }
inline unsigned int TvGet32(const char* p)
{
    return (unsigned int)(unsigned char)p[0] | ((unsigned int)(unsigned char)p[1] << 8) | ((unsigned int)(unsigned char)p[2] << 16) | ((unsigned int)(unsigned char)p[3] << 24);
}
inline void TvPutStr(std::vector<char>* b, const std::string& s) { TvPut32(b, (unsigned int)s.size()); b->insert(b->end(), s.begin(), s.end()); }
inline bool TvGetStr(const char* p, size_t n, size_t* at, std::string* out)
{
    if (n < *at + 4) return false;
    const unsigned int len = TvGet32(p + *at);
    if (len > kTvStrMax || n < *at + 4 + len) return false;
    out->assign(p + *at + 4, len);
    *at += 4 + len;
    return TvCleanStr(*out);
}

/* An encode that refuses appends nothing. */
inline bool TvEncode(std::vector<char>* b, unsigned int kind, const TownVer& r)
{
    if (kind < (unsigned int)kTvWrite || kind > (unsigned int)kTvRefused || r.key.empty() || !TvCleanStr(r.key) || !TvCleanStr(r.ver) || !TvCleanStr(r.fac)) return false;
    b->push_back((char)kind); b->push_back(0); b->push_back(0); b->push_back(0);
    TvPutStr(b, r.key); TvPutStr(b, r.ver); TvPutStr(b, r.fac); TvPut32(b, r.rev); TvPut32(b, (unsigned int)r.slot);
    return true;
}
inline bool TvDecode(const char* p, size_t n, TvMsg* m)
{
    if (p == 0 || n < 4) return false;
    const unsigned int kind = (unsigned int)(unsigned char)p[0];
    if (kind < (unsigned int)kTvWrite || kind > (unsigned int)kTvRefused || p[1] != 0 || p[2] != 0 || p[3] != 0) return false;
    size_t at = 4; TvMsg t; t.kind = kind;
    if (!TvGetStr(p, n, &at, &t.rec.key) || !TvGetStr(p, n, &at, &t.rec.ver) || !TvGetStr(p, n, &at, &t.rec.fac)) return false;
    if (n != at + 8) return false;
    t.rec.rev = TvGet32(p + at); t.rec.slot = (int)TvGet32(p + at + 4);
    if (t.rec.key.empty()) return false;
    *m = t;
    return true;
}

/* THE REVISION RULE (world server): accept a write only when its revision is the stored one + 1 (1 when nothing is stored). */
inline int TvAccept(int haveStored, unsigned int storedRev, unsigned int writeRev)
{
    return writeRev == (haveStored ? storedRev : 0u) + 1u ? 1 : 0;
}
/* Who gets an accepted write (world server): every connected game but its sender whose record feed is on - a game whose feed is
   off gets the whole list before its first index page or with its feed's first page. */
inline int TvForwardTo(int connected, int isSender, int feedOn) { return (connected && !isSender && feedOn) ? 1 : 0; }
/* THE GAME'S CACHE RULE: what the world server says is the truth. REFUSED always replaces (it is the stored record the writer lost
   to; rev 0 = nothing stored -> the row is erased by the caller). STORED replaces a row of the same or a lower revision; a lower
   incoming revision is a late duplicate and is dropped. */
inline int TvCacheTake(int haveCached, unsigned int cachedRev, unsigned int kind, unsigned int inRev)
{
    if (!haveCached || kind == (unsigned int)kTvRefused) return 1;
    return inRev >= cachedRev ? 1 : 0;
}
/* The holder's publish decision after the engine's chooser ran: 0 nothing to say, else the revision to write.
   No record + the town's own record = nothing (no record already means the normal town). */
inline unsigned int TvPublishRev(int haveRec, const TownVer& rec, const std::string& ver, const std::string& fac)
{
    if (!haveRec) return ver.empty() ? 0u : 1u;
    if (rec.ver == ver && rec.fac == fac) return 0u;
    return rec.rev + 1u;
}
/* A WORLD SAVED BEFORE THIS RECORD EXISTED: the town's version from this game's save is published (revision 1) only once the world
   server's whole list is here and holds nothing for the town, the world server has not refused this game for it, the save gave the
   town another version, and this game holds the town's area now. */
inline int TvSeedDue(int listComplete, int haveRec, int stopped, int hasVersion, int holder)
{
    return (listComplete && !haveRec && !stopped && hasVersion && holder == 1) ? 1 : 0;
}
/* WHO QUEUES A SAVE'S OWN VERSION FOR THE SEED: every game whose chooser takes the version path for the town at an activation (the
   caller passes 1), for a town with no record whose save gave it another version. Only the game running the area when the seed is
   due publishes it (TvSeedDue): the area's runner can change after the activation, so the queue is not gated on it. A game that
   never runs the area holds the town as its save has it until the runner publishes, and then puts that on. */
inline int TvSeedQueue(int onVersionPath, int haveRec, int hasVersion)
{
    return (onVersionPath == 1 && !haveRec && hasVersion) ? 1 : 0;
}
/* THE "BUILT WITH" NOTE INSIDE AN AREA FILE (store.cpp TownNote*, townver.cpp): one record of id kTvNoteId and type 0x1E (the type
   of the engine's own area-file lists), added to the area's container just before the engine writes the file on the main thread, so
   it travels with the file (this game's own save, the world server's copy). String fields: "file" = "1" (set to "0" in memory after
   the write, so a record still in memory is never read as one from a file), "zone" = the area id, "n" = the town count (at most
   kTvNoteCap), and per town i "k<i>" = the placed-town key and "v<i>" = "=" + the version id the area's buildings were built with
   ("=" alone = the town's own record). */
const char* const kTvNoteId = "sw-town-builtwith";
const int kTvNoteCap = 64;
inline std::string TvNoteVerField(const std::string& ver) { return "=" + ver; }
/* 1 = a version field; *ver = the version ("" = the town's own record) */
inline int TvNoteVerParse(const std::string& field, std::string* ver)
{
    if (field.empty() || field[0] != '=') return 0;
    *ver = field.substr(1);
    return 1;
}
/* the town count field: digits only, at most kTvNoteCap towns read; -1 = not a count */
inline int TvNoteCountParse(const std::string& text)
{
    if (text.empty() || text.size() > 6) return -1;
    int n = 0;
    for (size_t i = 0; i < text.size(); ++i) { if (text[i] < '0' || text[i] > '9') return -1; n = n * 10 + (text[i] - '0'); }
    return n > kTvNoteCap ? kTvNoteCap : n;
}
/* A REBUILD AT AN ACTIVATION (the engine's rebuild drops the area file's saved buildings and loose items, decomp_9fec00): only when
   the area file this load used carries a note for the town naming another version than the town has now (after the stored record
   was put on). The engine's own base files (TvIsBaseAreaPath) count as a note of "" (the town's base version) for every town of the
   area: a never-visited area is rebuilt once from the town's version, as the game builds one itself. No note for the town (a save or
   world-server copy written before notes existed, or off the main thread) keeps the file's buildings - never a rebuild. Only the
   version counts: an owner-only difference never rebuilds. */
/* 1 = an engine base area file (data/newland/leveldata/..., any case, either slash) - not this game's save nor a world-server copy */
inline int TvIsBaseAreaPath(const std::string& path)
{
    std::string p(path);
    for (size_t i = 0; i < p.size(); ++i) { char c = p[i]; if (c == (char)92) c = '/'; if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); p[i] = c; }
    return p.find("data/newland/leveldata/") != std::string::npos ? 1 : 0;
}
/* WHERE A READ'S NOTES COME FROM: an engine base file, a save / world-server copy carrying a note, or one carrying none */
enum { kTvNoteNone = 0, kTvNoteFile = 1, kTvNoteBase = 2 };
inline int TvNoteSource(int isBaseFile, int found)
{
    if (isBaseFile) return kTvNoteBase;
    return found ? kTvNoteFile : kTvNoteNone;
}
/* The note one area-file read gives a town: 1 and *ver, or 0 = no note. A base file gives every town of the area "" (its base
   version); a copy with a note gives the town's entry when it has one; a copy with no note gives nothing. */
inline int TvNoteFor(int source, int inFile, const std::string& fileVer, std::string* ver)
{
    if (source == kTvNoteBase) { *ver = std::string(); return 1; }
    if (source == kTvNoteFile && inFile) { *ver = fileVer; return 1; }
    return 0;
}
inline int TvNoteRebuild(int hasNote, const std::string& noteVer, const std::string& verNow)
{
    return (hasNote && noteVer != verNow) ? 1 : 0;
}
/* WHAT A TOWN'S BUILDINGS IN THE LOADED AREA ARE BUILT WITH after the chooser - the version that area's next write notes for it:
   rebuilt -> the version now; kept with a note -> the note's version (the kept file still shows it: at a save's load pass nothing is
   rebuilt, and the next visit rebuilds); kept with no note -> the version now (no note means keep: the file is taken as matching). */
inline std::string TvAsBuilt(int rebuilt, int hasNote, const std::string& noteVer, const std::string& verNow)
{
    return (!rebuilt && hasNote) ? noteVer : verNow;
}
/* AN AREA (sector sx, sy of the 64 x 64 map) as one number; -1 = outside the map */
inline int TvAreaIndex(int sx, int sy) { return (sx >= 0 && sx < 64 && sy >= 0 && sy < 64) ? sx * 64 + sy : -1; }
/* THE NOTE BOOK (townver.cpp keeps one, MAIN THREAD). After reading an area's file (decomp_9fec00.txt:147) the engine asks the town
   chooser for the towns placed in that area AND for towns found from it in neighbouring areas (:200, the town nearest a position),
   and a rebuild drops that town's buildings from THIS area's file (:186-191) - so every note is kept per (area loaded, town).
   Read: a file of the area was read (the engine reads its own area's file just before asking the chooser, so the area read last is
   the one being switched on; loaded 0 = no such file, the base files read just before stay its notes). Take: the chooser's note for
   a town from the area read last, once per read. Built: what the town's buildings in that area are built with after the chooser.
   For: the note the area's next write carries - every town the chooser ran for from it and every town its file named. Forget: an
   area whose read cannot be followed (read off the main thread) - nothing taken or written for it until its next read.
   WorldEnded: a world load / teardown began. It runs BEFORE the engine's teardown, which still writes area files, so the book is
   not emptied there: those writes still get their notes (For) and nothing is taken. TeardownDone: the engine's teardown returned -
   a book still ended is emptied, so no write of the next world carries the last world's notes (the next world's areas load after
   that call; were one read before it returned, that read has already emptied the book and its notes stay). */
struct TvNoteBook
{
    typedef std::pair<int, std::string> Key;
    std::map<Key, std::string> note;    /* the note entries of the area's last file read (a save / world-server copy) */
    std::map<Key, std::string> built;   /* what the town's buildings in that area are built with */
    std::set<Key> taken;                /* notes the chooser took since the area's last read */
    std::map<int, int> source;          /* area -> TvNoteSource of its last file read */
    int last;                           /* the area whose file was read last (-1 none) */
    int ended;                          /* the world ended: the notes serve its teardown's writes only; the next read starts afresh */
    TvNoteBook() : last(-1), ended(0) {}
    void WorldEnded() { ended = 1; last = -1; }
    void Clear() { note.clear(); built.clear(); taken.clear(); source.clear(); last = -1; ended = 0; }
    void TeardownDone() { if (ended) Clear(); }
    void Forget(int area)
    {
        for (std::map<Key, std::string>::iterator it = note.begin(); it != note.end(); ) { if (it->first.first == area) note.erase(it++); else ++it; }
        for (std::map<Key, std::string>::iterator it = built.begin(); it != built.end(); ) { if (it->first.first == area) built.erase(it++); else ++it; }
        for (std::set<Key>::iterator it = taken.begin(); it != taken.end(); ) { if (it->first == area) taken.erase(it++); else ++it; }
        source.erase(area);
        if (last == area) last = -1;
    }
    void Read(int area, int loaded, int src, const std::vector<std::pair<std::string, std::string> >& entries)
    {
        if (ended) Clear();
        if (area < 0) { last = -1; return; }
        if (loaded)
        {
            Forget(area);
            source[area] = src;
            if (src == kTvNoteFile)
                for (size_t i = 0; i < entries.size(); ++i) { note[Key(area, entries[i].first)] = entries[i].second; built[Key(area, entries[i].first)] = entries[i].second; }
        }
        last = area;
    }
    /* 1 = the chooser's first call for the town since the read (*has, *ver = its note); 0 = already taken, or no area read */
    int Take(const std::string& town, int* has, std::string* ver)
    {
        *has = 0;
        if (last < 0 || !taken.insert(Key(last, town)).second) return 0;
        std::map<int, int>::const_iterator s = source.find(last);
        std::map<Key, std::string>::const_iterator n = note.find(Key(last, town));
        *has = TvNoteFor(s != source.end() ? s->second : (int)kTvNoteNone, n != note.end() ? 1 : 0, n != note.end() ? n->second : std::string(), ver);
        return 1;
    }
    void Built(const std::string& town, int rebuilt, int has, const std::string& noteVer, const std::string& verNow)
    {
        if (last >= 0) built[Key(last, town)] = TvAsBuilt(rebuilt, has, noteVer, verNow);
    }
    void For(int area, std::vector<std::pair<std::string, std::string> >* out) const
    {
        out->clear();
        for (std::map<Key, std::string>::const_iterator it = built.lower_bound(Key(area, std::string())); it != built.end() && it->first.first == area; ++it)
            out->push_back(std::make_pair(it->first.second, it->second));
    }
};
/* THIS GAME'S OWN WRITE, PENDING. The world server sends nothing back for an accepted write, so a write stays pending until a record
   of its revision or a later one arrives (the list on the next link, another game's forwarded write) or the world server refuses
   it. 1 = still pending. A write still pending when a link's whole list is here was lost on the way: it is sent again, and the
   world server's revision rule decides it. A REFUSAL carries the record the world server holds (rev 0 = none): one holding exactly
   the revision below the pending write cannot be a refusal of that write (it would have been accepted), so it answers an earlier
   write and the pending one stays; any other refusal ends it. */
inline int TvMineKeep(unsigned int kind, unsigned int inRev, unsigned int mineRev)
{
    if (kind == (unsigned int)kTvRefused) return (inRev != 0u && inRev + 1u == mineRev) ? 1 : 0;
    return inRev >= mineRev ? 0 : 1;
}

/* WHAT THE CHOOSER DETOUR DOES for one town (townver.cpp TownVerChoose):
     kTvEngineAlone  the engine's chooser untouched: not the main thread, a hook or a read missing, a player-owned town (the engine's
                     own gate), or a game in no world-server world
     kTvRefuse       the stored record is put on first; then the engine's chooser runs but no version may qualify, so it neither
                     switches nor marks the town decided - this game does not run the town's area
     kTvStandIn      a town another player owns (a stand-in faction, which fails the engine's player-faction gate): no version may
                     qualify and nothing is put on
     kTvDecide       the stored record is put on first; then the engine decides and a change is published */
enum { kTvEngineAlone = 0, kTvRefuse = 1, kTvStandIn = 2, kTvDecide = 3 };
inline int TvChooseMode(int onMain, int hooked, int readOk, int playerOwned, int standIn, int inWorld, int holder)
{
    if (!onMain || !hooked || !readOk || playerOwned || !inWorld) return kTvEngineAlone;
    if (standIn) return kTvStandIn;
    return holder == 1 ? kTvDecide : kTvRefuse;
}

/* THE PLACED-TOWN KEY (zones.cpp TownKey prints exactly this): the town's own record sid and its position rounded to whole metres. */
inline std::string TvKey(const std::string& sid, float x, float z)
{
    char b[48];
    _snprintf(b, sizeof(b) - 1, "@%d,%d", (int)std::floor(x + 0.5f), (int)std::floor(z + 0.5f)); b[sizeof(b) - 1] = 0;
    return sid + b;
}

inline std::string TvFileLine(const TownVer& r)
{
    char n[48]; _snprintf(n, sizeof(n) - 1, "\t%u\t%d", r.rev, r.slot); n[sizeof(n) - 1] = 0;
    return "v1\t" + r.key + "\t" + r.ver + "\t" + r.fac + n;
}
inline bool TvParseLine(const std::string& lineIn, TownVer* out)
{
    std::string line = lineIn;
    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    std::vector<std::string> f; size_t at = 0;
    for (;;) { const size_t t = line.find('\t', at); if (t == std::string::npos) { f.push_back(line.substr(at)); break; } f.push_back(line.substr(at, t - at)); at = t + 1; }
    if (f.size() != 6 || f[0] != "v1" || f[1].empty() || f[4].empty()) return false;
    for (size_t i = 1; i < 4; ++i) if (!TvCleanStr(f[i])) return false;
    for (size_t i = 0; i < f[4].size(); ++i) if (f[4][i] < '0' || f[4][i] > '9') return false;
    TownVer r; r.key = f[1]; r.ver = f[2]; r.fac = f[3]; r.rev = (unsigned int)strtoul(f[4].c_str(), 0, 10); r.slot = atoi(f[5].c_str());
    if (r.rev == 0) return false;
    *out = r;
    return true;
}

/* OWNER DECISION 260: "player ally / player enemy" conditions are IGNORED in multiplayer; only the named-character conditions
   (dead / imprisoned / alive, player involved) decide. The engine (WorldEventStateQuery::isTrue 0x9A6F30) answers one world state
   = ALL of its "NPC is", "NPC is NOT", "player ally", "player enemy" terms; the chooser then compares that answer with the
   reference's expected bool and weighs a version by its number of references. "Ignore" is applied per world state, inside the
   chooser only:
     kTvEngine    no standing term                       -> the engine's own answer
     kTvExpected  standing terms only (no NPC term)      -> the reference is met whatever it expects (the answer = its expected bool)
     kTvStripped  NPC terms AND standing terms (none in vanilla) -> the engine's answer with the standing terms removed
   The weight is NOT changed (an ignored reference still counts), so a version keeps the specificity its author gave it. */
enum { kTvEngine = 0, kTvExpected = 1, kTvStripped = 2 };
inline int TvIgnoreAction(unsigned long long npcTerms, unsigned long long standingTerms)
{
    if (standingTerms == 0) return kTvEngine;
    return npcTerms == 0 ? kTvExpected : kTvStripped;
}
/* The chooser, modelled for the offline suite (build/decomp_9fe6d0.txt): every reference must be met; the version equal to the
   current one is skipped; weight = number of references, 0 never applies; the best is kept only on a STRICTLY greater weight
   (ties go to the earliest). `npcAnswer` = the answer of the world state's NPC terms (1 when it has none). */
struct TvModelRef { unsigned int npcTerms, standingTerms; int npcAnswer, standingAnswer, expected; };
inline int TvModelRefMet(const TvModelRef& r, int ignoreStandings)
{
    int answer;
    const int act = ignoreStandings ? TvIgnoreAction(r.npcTerms, r.standingTerms) : kTvEngine;
    if (act == kTvExpected) return 1;
    if (act == kTvStripped) answer = r.npcAnswer;
    else answer = (r.npcAnswer && (r.standingTerms == 0 || r.standingAnswer)) ? 1 : 0;
    return answer == r.expected ? 1 : 0;
}
inline int TvModelChoose(const std::vector<std::vector<TvModelRef> >& cands, int current, int ignoreStandings)
{
    int best = -1; size_t bestW = 0;
    for (size_t c = 0; c < cands.size(); ++c)
    {
        if ((int)c == current || cands[c].empty()) continue;
        int ok = 1;
        for (size_t i = 0; i < cands[c].size() && ok; ++i) ok = TvModelRefMet(cands[c][i], ignoreStandings);
        if (ok && cands[c].size() > bestW) { best = (int)c; bestW = cands[c].size(); }
    }
    return best;
}

}   /* namespace cooptv */

#endif
