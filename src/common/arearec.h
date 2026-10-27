#pragma once
/* T-1084 step 3 (owner 1127 a, 1128 a, 1129 a): THE AREA-RECORDED MARK (R5), DECIDING A SAVE'S
   DOUBLES AT THE WAKE, AND THE OFF-SCREEN AUTHORITY'S BUILD ARM. Pure rules only; the world server (store_main.cpp) and the game
   (store.cpp) do the reading and the acting.

   An area is "recorded" once the game that held it has written every world group living in it to the world server and the world server
   has confirmed each write. A group the save made that has no full record is decided where it first appears - at its wake, or at the
   first sight of a group the save loaded awake - by ONE rule in every area, marked or not (AtWakeDecide; fold 1 of build 2: the old
   R6, which refused every such group in a marked area, is gone - it refused genuine residents): a double of a record standing here is
   refused; a homeless group matching no record, in an area another player held before (the world server's holder history), is refused
   (owner 1129 a); every other group wakes and is recorded. Until the area's records are all here the answer is not known: the group
   wakes but is held PENDING - its people are not adopted, it is not published and not written at its sleep - and the pending group is
   settled when they are. "Marked" only tells R5 that the area needs no new mark.

   "A full record" (RecFull): the record is here, has a file, was written (writtenAt > 0) and is not gone. A position-only note (the
   off-screen authority's sleeping-position row) is not one.

   THE WIRE - message 83 AREA_RECORDED, on the world-server link (store_main.cpp's enum; store.cpp kStoreMsgAreaRecorded), protocol 121:
     up   (a game -> the world server): {u8 x, u8 y} - "every group in area (x, y) is recorded; mark it". Taken only from the area's
          current holder, by player id (MarkTake).
     down (the world server -> a game): {u8 kind, ...}
          kind 1 LIST {u16 n, n * (u8 x, u8 y), u16 m, m * HOLDERS-ROW} - every marked area and every area's holder history; sent with
                 the first page of each record feed (the game clears its list at its WELCOME and at every world load, and asks a first
                 page after both), so its arrival says "this link's list is here";
          kind 2 MARK {u8 x, u8 y} - one newly marked area, to every connected game;
          kind 3 HOLDERS HOLDERS-ROW - one area's holder history grew, to every admitted game.
          HOLDERS-ROW = {u8 x, u8 y, u8 c, c * u16 slot}: the player slots that have ever held the area (slots.txt's numbers, which
                 belong to the player for the world's life).
   AREA KEYS: the area map's key is y * 64 + x (recordfeed.h FeedAreaKey, zones.cpp Key()). The kept-records sweep in store.cpp keys
   an area x * 64 + y; AreaKeyOfSweep / SweepKeyOfArea convert, and nothing here takes the sweep's key. */
#include <string>
#include <vector>
#include <cstddef>

namespace arearec {

const unsigned kMsgAreaRecorded = 83u;   /* 28-82 are taken on the world-server link (store_main.cpp's enum; 70 TOWNS, 78 SHOW) */
const unsigned kProtocol = 121u;         /* the world-server protocol that carries AREA_RECORDED */
const int kGrid = 64;
const int kKeys = kGrid * kGrid;         /* 4096 */
const int kNoKey = -1;
const unsigned char kDownList = 1, kDownMark = 2, kDownHolders = 3;
const size_t kListMax = 4096;            /* every area at most once */
const size_t kHolderSlotsMax = 255;      /* a row's slot count is one byte */

/* ---- area keys ---- */
inline int AreaKeyXY(int x, int y) { return (x >= 0 && x < kGrid && y >= 0 && y < kGrid) ? y * kGrid + x : kNoKey; }
inline int AreaKeyX(int key) { return key % kGrid; }
inline int AreaKeyY(int key) { return key / kGrid; }
/* the sweep's x * 64 + y -> the area map's y * 64 + x */
inline int AreaKeyOfSweep(int sweepKey) { return (sweepKey >= 0 && sweepKey < kKeys) ? AreaKeyXY(sweepKey / kGrid, sweepKey % kGrid) : kNoKey; }
/* the area map's y * 64 + x -> the sweep's x * 64 + y */
inline int SweepKeyOfArea(int areaKey) { return (areaKey >= 0 && areaKey < kKeys) ? AreaKeyX(areaKey) * kGrid + AreaKeyY(areaKey) : kNoKey; }

/* one area's holder history: the player slots that have ever held it (each once, 0..65535) */
struct HolderRow { int key; std::vector<int> slots; HolderRow() : key(kNoKey) {} };

/* ---- the wire ---- */
inline void EncodeMarkUp(std::vector<char>* b, int x, int y) { b->clear(); b->push_back((char)(unsigned char)x); b->push_back((char)(unsigned char)y); }
/* 1 = exactly two bytes naming an area on the grid; *key its area key */
inline int DecodeMarkUp(const std::vector<char>& b, int* key)
{
    *key = kNoKey;
    if (b.size() != 2) return 0;
    const int k = AreaKeyXY((unsigned char)b[0], (unsigned char)b[1]);
    if (k == kNoKey) return 0;
    *key = k;
    return 1;
}
inline void EncodeDownMark(std::vector<char>* b, int key)
{
    b->clear(); b->push_back((char)kDownMark); b->push_back((char)(unsigned char)AreaKeyX(key)); b->push_back((char)(unsigned char)AreaKeyY(key));
}
/* 1 = the row can go on the wire (an area on the grid, at most 255 slots, each 0..65535) */
inline int HolderRowOk(const HolderRow& r)
{
    if (r.key < 0 || r.key >= kKeys || r.slots.size() > kHolderSlotsMax) return 0;
    for (size_t i = 0; i < r.slots.size(); ++i) if (r.slots[i] < 0 || r.slots[i] > 65535) return 0;
    return 1;
}
inline void PutHolderRow(std::vector<char>* b, const HolderRow& r)
{
    b->push_back((char)(unsigned char)AreaKeyX(r.key)); b->push_back((char)(unsigned char)AreaKeyY(r.key));
    b->push_back((char)(unsigned char)r.slots.size());
    for (size_t i = 0; i < r.slots.size(); ++i) { b->push_back((char)(unsigned char)(r.slots[i] & 0xFF)); b->push_back((char)(unsigned char)((r.slots[i] >> 8) & 0xFF)); }
}
/* 1 = a row read at *at (moved past it) */
inline int GetHolderRow(const std::vector<char>& b, size_t* at, HolderRow* r)
{
    if (*at + 3 > b.size()) return 0;
    const int key = AreaKeyXY((unsigned char)b[*at], (unsigned char)b[*at + 1]);
    const size_t c = (size_t)(unsigned char)b[*at + 2];
    if (key == kNoKey || *at + 3 + 2 * c > b.size()) return 0;
    r->key = key; r->slots.clear();
    for (size_t i = 0; i < c; ++i) r->slots.push_back((int)((unsigned)(unsigned char)b[*at + 3 + 2 * i] | ((unsigned)(unsigned char)b[*at + 4 + 2 * i] << 8)));
    *at += 3 + 2 * c;
    return 1;
}
/* keys off the grid and rows that cannot go on the wire are left out; at most kListMax of each */
inline void EncodeDownList(std::vector<char>* b, const std::vector<int>& keys, const std::vector<HolderRow>& holders)
{
    std::vector<int> ok;
    for (size_t i = 0; i < keys.size() && ok.size() < kListMax; ++i) if (keys[i] >= 0 && keys[i] < kKeys) ok.push_back(keys[i]);
    std::vector<const HolderRow*> rows;
    for (size_t i = 0; i < holders.size() && rows.size() < kListMax; ++i) if (HolderRowOk(holders[i]) != 0) rows.push_back(&holders[i]);
    b->clear(); b->push_back((char)kDownList);
    b->push_back((char)(unsigned char)(ok.size() & 0xFF)); b->push_back((char)(unsigned char)((ok.size() >> 8) & 0xFF));
    for (size_t i = 0; i < ok.size(); ++i) { b->push_back((char)(unsigned char)AreaKeyX(ok[i])); b->push_back((char)(unsigned char)AreaKeyY(ok[i])); }
    b->push_back((char)(unsigned char)(rows.size() & 0xFF)); b->push_back((char)(unsigned char)((rows.size() >> 8) & 0xFF));
    for (size_t i = 0; i < rows.size(); ++i) PutHolderRow(b, *rows[i]);
}
/* 1 = encoded (the row can go on the wire) */
inline int EncodeDownHolders(std::vector<char>* b, const HolderRow& r)
{
    b->clear();
    if (HolderRowOk(r) == 0) return 0;
    b->push_back((char)kDownHolders); PutHolderRow(b, r);
    return 1;
}
/* 1 = a well-formed LIST, MARK or HOLDERS: *kind kDownList / kDownMark / kDownHolders, *keys the marked areas it names (one for a MARK,
   none for a HOLDERS), *holders the holder rows it carries (every area's for a LIST, one for a HOLDERS). A message of any other size or
   kind, or naming an area off the grid, is malformed (0) and nothing of it is taken. */
inline int DecodeDown(const std::vector<char>& b, int* kind, std::vector<int>* keys, std::vector<HolderRow>* holders)
{
    *kind = 0; keys->clear(); holders->clear();
    if (b.empty()) return 0;
    const unsigned char k = (unsigned char)b[0];
    if (k == kDownMark)
    {
        if (b.size() != 3) return 0;
        const int key = AreaKeyXY((unsigned char)b[1], (unsigned char)b[2]);
        if (key == kNoKey) return 0;
        keys->push_back(key); *kind = kDownMark;
        return 1;
    }
    if (k == kDownHolders)
    {
        size_t at = 1; HolderRow r;
        if (GetHolderRow(b, &at, &r) == 0 || at != b.size()) return 0;
        holders->push_back(r); *kind = kDownHolders;
        return 1;
    }
    if (k != kDownList || b.size() < 5) return 0;
    const size_t n = (size_t)(unsigned char)b[1] | ((size_t)(unsigned char)b[2] << 8);
    if (n > kListMax || b.size() < 3 + 2 * n + 2) return 0;
    std::vector<int> out;
    for (size_t i = 0; i < n; ++i)
    {
        const int key = AreaKeyXY((unsigned char)b[3 + 2 * i], (unsigned char)b[4 + 2 * i]);
        if (key == kNoKey) return 0;
        out.push_back(key);
    }
    size_t at = 3 + 2 * n;
    const size_t m = (size_t)(unsigned char)b[at] | ((size_t)(unsigned char)b[at + 1] << 8);
    at += 2;
    if (m > kListMax) return 0;
    std::vector<HolderRow> rows;
    for (size_t i = 0; i < m; ++i) { HolderRow r; if (GetHolderRow(b, &at, &r) == 0) return 0; rows.push_back(r); }
    if (at != b.size()) return 0;
    keys->swap(out); holders->swap(rows); *kind = kDownList;
    return 1;
}

/* ---- the holder history's file line (the world server's area_holders.txt): "x y s1 s2 ..." ---- */
inline std::string HolderNum(int v)   /* 0..65535 as decimal digits */
{
    if (v <= 0) return std::string("0");
    std::string s;
    while (v > 0) { s.insert(s.begin(), (char)('0' + v % 10)); v /= 10; }
    return s;
}
inline std::string HolderLineFormat(const HolderRow& r)
{
    std::string s = HolderNum(AreaKeyX(r.key)) + " " + HolderNum(AreaKeyY(r.key));
    for (size_t i = 0; i < r.slots.size(); ++i) s += " " + HolderNum(r.slots[i]);
    return s;
}
/* 1 = a line naming an area on the grid and 0..255 slots, each 0..65535, each once (a repeated slot is malformed) */
inline int HolderLineParse(const std::string& line, HolderRow* r)
{
    r->key = kNoKey; r->slots.clear();
    std::vector<long> nums; size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size() && line[i] == ' ') ++i;
        if (i >= line.size()) break;
        if (line[i] < '0' || line[i] > '9') return 0;
        long v = 0; size_t d = 0;
        while (i < line.size() && line[i] >= '0' && line[i] <= '9') { v = v * 10 + (line[i] - '0'); ++i; if (++d > 6) return 0; }
        if (i < line.size() && line[i] != ' ') return 0;
        nums.push_back(v);
    }
    if (nums.size() < 2 || nums.size() - 2 > kHolderSlotsMax) return 0;
    const int key = AreaKeyXY((int)nums[0], (int)nums[1]);
    if (key == kNoKey) return 0;
    std::vector<int> slots;
    for (size_t j = 2; j < nums.size(); ++j)
    {
        if (nums[j] > 65535) return 0;
        for (size_t s = 0; s < slots.size(); ++s) if (slots[s] == (int)nums[j]) return 0;
        slots.push_back((int)nums[j]);
    }
    r->key = key; r->slots.swap(slots);
    return 1;
}
/* fold 1: a holding counts only once it is CONFIRMED - the area map's row names a slot (owner >= 0) that has reported the area loaded:
   not a ring-1 lease given ahead of the report (unconfirmed) and not a row restored from areas.txt whose holder has not reported since
   the world server started (restored) */
inline int HolderConfirmed(int owner, int unconfirmed, int restored) { return (owner >= 0 && unconfirmed == 0 && restored == 0) ? 1 : 0; }
/* the history learns that `slot` holds the area: 1 = it is new to the row (written and sent), 0 = known already, or no slot */
inline int HolderNote(HolderRow* r, int slot)
{
    if (slot < 0 || slot > 65535) return 0;
    for (size_t i = 0; i < r->slots.size(); ++i) if (r->slots[i] == slot) return 0;
    if (r->slots.size() >= kHolderSlotsMax) return 0;
    r->slots.push_back(slot);
    return 1;
}
/* owner 1129 a's "another player visited the area": slots = the area's holder history, mySlot = this game's slot (-1 unknown).
   1 = a slot other than this game's held it; 0 = none did; -1 = cannot tell (this game's slot is not known and some slot held it) */
enum { kVisitDoubt = -1, kVisitNone = 0, kVisitOther = 1 };
inline int VisitedByOther(const std::vector<int>& slots, int mySlot)
{
    if (slots.empty()) return kVisitNone;
    if (mySlot < 0) return kVisitDoubt;
    for (size_t i = 0; i < slots.size(); ++i) if (slots[i] != mySlot) return kVisitOther;
    return kVisitNone;
}

/* ---- the mark-set rule (the world server) ----
   wellFormed: DecodeMarkUp took it; senderId: the sending game's player id (empty: unknown); holderId: the player id the area map names
   as the area's holder now (store_main.cpp ZoneHolderIdOf; empty: nobody); already: the area is marked already. */
enum { kTakeNew = 0, kTakeAgain = 1, kRefuseMalformed = 2, kRefuseNoSender = 3, kRefuseNoHolder = 4, kRefuseNotHolder = 5, kTakeCount = 6 };
inline int MarkTake(int wellFormed, const std::string& senderId, const std::string& holderId, int already)
{
    if (wellFormed == 0) return kRefuseMalformed;
    if (senderId.empty()) return kRefuseNoSender;
    if (holderId.empty()) return kRefuseNoHolder;
    if (holderId != senderId) return kRefuseNotHolder;
    return already != 0 ? kTakeAgain : kTakeNew;
}
/* 1 = taken (stored; broadcast only when new) */
inline int MarkTakes(int take) { return (take == kTakeNew || take == kTakeAgain) ? 1 : 0; }

/* ---- a full record ----
   present: g_records holds an entry of the id; hasFile: it names a file; writtenAt: its write stamp; gone: marked gone. A position-only
   note (no file, writtenAt 0 - the off-screen authority's sleeping-position row) is not a full record. Asked everywhere "has a record"
   is asked for a save group: the decision at the wake and the matching. */
inline int RecFull(int present, int hasFile, long long writtenAt, int gone) { return (present != 0 && hasFile != 0 && writtenAt > 0 && gone == 0) ? 1 : 0; }

/* ---- the matching (one record per double) ----
   A record a save group may be matched to counts only when it is full, its number is not deleted, it is not a kept record still held
   back (MirrorKeepGated), its build has not failed in this world (g_notePlaceFailed), and its group will stand here: its copy is here
   already, or the copy gate (CopyGateNow) would build it. */
inline int RecordCounts(int full, int deleted, int keepGated, int placeFailed, int copyHere, int gateBuilds)
{
    if (full == 0 || deleted != 0 || keepGated != 0 || placeFailed != 0) return 0;
    return (copyHere != 0 || gateBuilds != 0) ? 1 : 0;
}
/* a town id as read (store.cpp PlatoonTownSid): "" = the read faulted, "-" = no town (a wilderness group), else the town's id */
inline int TownKnown(const std::string& town) { return town.empty() ? 0 : 1; }
/* does a save group match a record? sameFaction: the group's faction key is the record's; groupHome / recHome: the home building keys
   ("" none); groupTmpl / recTmpl: the squad templates ("" unread); groupTown / recTown as TownKnown; sameArea: the record stands in the
   group's area. A homed group matches only a record of the same faction, home and template (kMatchHomed); a homeless one only a homeless
   record of its faction, template and town in its area (kMatchHomeless). */
enum { kMatchNo = 0, kMatchHomed = 1, kMatchHomeless = 2 };
inline int MatchRecord(int sameFaction, const std::string& groupHome, const std::string& recHome, const std::string& groupTmpl,
                       const std::string& recTmpl, const std::string& groupTown, const std::string& recTown, int sameArea)
{
    if (sameFaction == 0) return kMatchNo;
    if (groupTmpl.empty() || groupTmpl != recTmpl) return kMatchNo;
    if (!groupHome.empty()) return groupHome == recHome ? kMatchHomed : kMatchNo;
    if (!recHome.empty()) return kMatchNo;
    if (TownKnown(groupTown) == 0 || groupTown != recTown) return kMatchNo;
    return sameArea != 0 ? kMatchHomeless : kMatchNo;
}
/* one candidate record: counts (RecordCounts), match (MatchRecord), claimed (another group was matched to it already in this world -
   one double per record), d2 (the squared distance from the group to the record) */
struct MatchCand { int counts; int match; int claimed; float d2; MatchCand() : counts(0), match(kMatchNo), claimed(0), d2(0.0f) {} };
/* the nearest counting, matching, unclaimed candidate; -1 none */
inline int MatchPick(const std::vector<MatchCand>& c)
{
    int best = -1;
    for (size_t i = 0; i < c.size(); ++i)
    {
        if (c[i].counts == 0 || c[i].match == kMatchNo || c[i].claimed != 0) continue;
        if (best < 0 || c[i].d2 < c[best].d2) best = (int)i;
    }
    return best;
}

/* ---- THE DECISION AT THE WAKE (store.cpp ArWakeDecide / ArFirstSight / ArSettleTick) - one rule in marked and unmarked areas ----
   A world group asked at its wake (detour_activate, after T-300), or - a group the save loaded already awake, which never passes the wake -
   at the first sight of it (DeathWatchTick's registration or the people sweep, whichever comes first). Inputs, each read now:
   haveId: it carries a numbered world id; worldFac: a world faction (store.cpp T300WorldFaction); playerSide: a player's or a player
   team's squad; context: a context platoon (built here for another game's announced squad); hasFull: a FULL record of its own id is
   here (RecFull); floorRead: this game has read its factions' load floors (LoadFloorTick, the first running tick after the world
   load) - before that no group's floor can be known, so the group is held pending (kAwFloorPending) and decided once they are read;
   floorKnown / belowFloor: the save made it (its number below its faction's load floor; a faction missing after the read is
   floorKnown 0 and wakes); areaKnown: its area was read (the sleeping position at a wake, a member's
   position when awake); linkUp: the world-server link is up and welcomed; listHere: this link's mark list (with the holder history) is
   here; marked: its area is marked - NOT an input of the decision (carried for the counters and so the tests can show the answer is
   the same either way); feedDone: every record of its area is here (AreaDoneHeldGate - false until this world's feed ends);
   unique: it may hold a unique character (never removed); homeKnown: its home was read (a home or none); tmplKnown: its squad template
   was read; matched: MatchPick found a record it doubles; homed: it has a home; townKnown: its town was read (TownKnown); visited:
   VisitedByOther on its area's holder history (owner 1129 a). The order is the order of the answers below; every answer is counted.
   kAwFloorPending is asked just before kAwFloorUnknown but numbered last, so the REPORT's existing counter positions stay where they were. */
enum { kAwNoId = 0, kAwNotWorld = 1, kAwPlayerSide = 2, kAwContext = 3, kAwRecorded = 4, kAwFloorUnknown = 5, kAwNotSave = 6, kAwNoArea = 7,
       kAwNoLink = 8, kAwPendingList = 9, kAwPendingFeed = 10, kAwUnique = 11, kAwHomeUnread = 12, kAwTmplUnread = 13,
       kAwRefuseDuplicate = 14, kAwHomedUnmatched = 15, kAwTownUnread = 16, kAwRefuseHomeless = 17, kAwVisitDoubt = 18, kAwNotVisited = 19,
       kAwFloorPending = 20, kAwCount = 21 };
struct AwIn
{
    int haveId, worldFac, playerSide, context, hasFull, floorRead, floorKnown, belowFloor, areaKnown, linkUp, listHere, marked, feedDone,
        unique, homeKnown, tmplKnown, matched, homed, townKnown, visited;
    AwIn() : haveId(0), worldFac(0), playerSide(0), context(0), hasFull(0), floorRead(0), floorKnown(0), belowFloor(0), areaKnown(0), linkUp(0), listHere(0),
             marked(0), feedDone(0), unique(0), homeKnown(0), tmplKnown(0), matched(0), homed(0), townKnown(0), visited(kVisitNone) {}
};
inline int AtWakeDecide(const AwIn& a)
{
    if (a.haveId == 0) return kAwNoId;
    if (a.worldFac == 0) return kAwNotWorld;
    if (a.playerSide != 0) return kAwPlayerSide;
    if (a.context != 0) return kAwContext;
    if (a.hasFull != 0) return kAwRecorded;
    if (a.floorRead == 0) return kAwFloorPending;    /* the floors are read at the first running tick: held, decided then */
    if (a.floorKnown == 0) return kAwFloorUnknown;   /* its faction has no floor after the read: woken, as any group */
    if (a.belowFloor == 0) return kAwNotSave;
    if (a.areaKnown == 0) return kAwNoArea;
    if (a.linkUp == 0) return kAwNoLink;             /* nothing will arrive: woken, and written as any group */
    if (a.listHere == 0) return kAwPendingList;      /* the list (and the holder history) comes with the feed's first page */
    if (a.feedDone == 0) return kAwPendingFeed;      /* marked or not: the records are not all here */
    if (a.unique != 0) return kAwUnique;
    if (a.homeKnown == 0) return kAwHomeUnread;
    if (a.tmplKnown == 0) return kAwTmplUnread;
    if (a.matched != 0) return kAwRefuseDuplicate;
    if (a.homed != 0) return kAwHomedUnmatched;
    if (a.townKnown == 0) return kAwTownUnread;
    if (a.visited == kVisitOther) return kAwRefuseHomeless;
    return a.visited == kVisitDoubt ? kAwVisitDoubt : kAwNotVisited;
}
/* what an answer does: refuse (the duplicate-wake road), hold pending, or wake */
enum { kActWake = 0, kActRefuse = 1, kActPending = 2 };
inline int AwAct(int answer)
{
    if (answer == kAwRefuseDuplicate || answer == kAwRefuseHomeless) return kActRefuse;
    if (answer == kAwPendingList || answer == kAwPendingFeed || answer == kAwFloorPending) return kActPending;
    return kActWake;
}
/* 1 = the answer needs the costly reads (home, template, town, the matching scan, the holder history) before it is final: every cheap
   input passed. The caller reads them, then asks again; a refusal is asked once more with the unique read (the recipe walk). */
inline int AwNeedsMatch(int answer) { return answer == kAwNotVisited ? 1 : 0; }

/* ---- settling a pending group (store.cpp ArSettleTick, each ArTick turn) ----
   present: the group is still here; awake: it is awake now; adopted: an awake group's people are registered (adopted, or announced
   copies) - kept, never removed; sameLink: the link it went pending on is the link up now (a link drop is a bound); timedOut: the bound
   (kPendingBoundMs) has passed - THE RULE (fold 1): 60 s with neither the group's own going-pending nor any progress of this world's
   record feed (an ask sent, a page or marker arrived), so a long feed that keeps moving strands no group; againAct: AwAct of AtWakeDecide
   asked again now. Released and kept groups are written as any awake group (the heartbeat); a group ASLEEP cannot be written here, so
   it is never released or kept asleep: it stays pending (kPsWaitAsleep) and its next wake decides it again (detour_activate). A
   refusal is taken asleep too (its sleeping copy is destroyed). Kept by the bound or the link: its area stays unmarked for this world
   load. */
const unsigned kPendingBoundMs = 60000u;
enum { kPsWait = 0, kPsRefuse = 1, kPsRelease = 2, kPsKeepBound = 3, kPsKeepLink = 4, kPsKeepAdopted = 5, kPsGone = 6, kPsWaitAsleep = 7, kPsCount = 8 };
inline int PendingSettle(int present, int awake, int adopted, int sameLink, int timedOut, int againAct)
{
    if (present == 0) return kPsGone;
    if (adopted != 0) return kPsKeepAdopted;
    if (sameLink == 0) return awake != 0 ? kPsKeepLink : kPsWaitAsleep;
    if (againAct == kActPending)
    {
        if (timedOut == 0) return kPsWait;
        return awake != 0 ? kPsKeepBound : kPsWaitAsleep;
    }
    if (againAct == kActRefuse) return kPsRefuse;
    return awake != 0 ? kPsRelease : kPsWaitAsleep;
}
/* 1 = a settle answer that leaves the group's area unmarked for this world load (a pending group released by the bound) */
inline int SettleStucksArea(int ps) { return (ps == kPsKeepBound || ps == kPsKeepLink) ? 1 : 0; }
/* the time since a tick-count stamp; fold 2: a stamp AHEAD of now (a "never 0" stamp made odd with | 1 in this same even millisecond,
   which wraps the difference to ~4.29e9) is 0 elapsed, never a whole wrap */
inline unsigned TicksSince(unsigned now, unsigned at) { const unsigned d = now - at; return d >= 0x80000000u ? 0u : d; }
/* the bound's elapsed time: from the later of the group's going-pending and the feed's last progress (0 = none yet) */
inline unsigned PendingElapsed(unsigned now, unsigned pendingAt, unsigned feedProgressAt)
{
    const unsigned sincePending = TicksSince(now, pendingAt);
    if (feedProgressAt == 0) return sincePending;
    const unsigned sinceFeed = TicksSince(now, feedProgressAt);
    return sinceFeed < sincePending ? sinceFeed : sincePending;
}

/* ---- R5: the mark's send rule (store.cpp ArTick, each turn of an area's run) ----
   mineHeld: MineHeldTS (1 held here, 0 not, -1 no fresh map, -2 frozen); listHere / marked as above; stuck: the area's recording is stuck
   in this world load (a pending group released by the bound, or an asked write never answered); loaded: the area is loaded here;
   feedDone: every record of it is here; pendingInK: an AWAKE pending group of this game stands in it now (read now, not where it went
   pending; an asleep one is decided at its wake by the same rule, marked or not, so it does not hold the mark); sweepPending: a newly-held sweep entry for
   it still waits; sameLink: the run asked its writes on the link up now (-1 never: a run's first asking is a kMkAsk); waiting: writes it
   asked not yet confirmed. kMkAsk: ask the writes (again, on a new link); kMkSend: ask once more for groups that came meanwhile, and when
   none is asked send AREA_RECORDED. */
enum { kMkSend = 0, kMkLost = 1, kMkMarked = 2, kMkStuck = 3, kMkWaitList = 4, kMkWaitMap = 5, kMkWaitLoad = 6, kMkWaitFeed = 7,
       kMkWaitPending = 8, kMkWaitSweep = 9, kMkAsk = 10, kMkWaitWrites = 11, kMkCount = 12 };
inline int MarkSendStep(int mineHeld, int listHere, int marked, int stuck, int loaded, int feedDone, int pendingInK, int sweepPending, int sameLink, int waiting)
{
    if (mineHeld == 0 || mineHeld == -2) return kMkLost;
    if (listHere == 0) return kMkWaitList;
    if (marked != 0) return kMkMarked;
    if (stuck != 0) return kMkStuck;
    if (mineHeld != 1) return kMkWaitMap;
    if (loaded == 0) return kMkWaitLoad;
    if (feedDone == 0) return kMkWaitFeed;
    if (pendingInK != 0) return kMkWaitPending;
    if (sweepPending != 0) return kMkWaitSweep;
    if (sameLink == 0) return kMkAsk;
    if (waiting != 0) return kMkWaitWrites;
    return kMkSend;
}
/* 1 = the run ends (dropped): the area left this game, is marked, or is stuck */
inline int MarkStepEnds(int step) { return (step == kMkLost || step == kMkMarked || step == kMkStuck) ? 1 : 0; }

/* COMPLETENESS, per write the run asked. echoConfirmed: a RECORD_SEQ answered this game's write of it (R5Confirmed); fullAsNew: a full
   record of it written by another game, newer than the newest known when the write was asked, is here; deleted: its number is deleted
   (wiped out - nothing of it lives in the area); timedOut: kWriteBoundMs since the ask. An asked write never answered is stuck: the run
   ends and the area stays unmarked for this world load. */
const unsigned kWriteBoundMs = 60000u;
enum { kWdDone = 0, kWdWait = 1, kWdDeleted = 2, kWdStuck = 3, kWdCount = 4 };
inline int WriteDone(int echoConfirmed, int fullAsNew, int deleted, int timedOut)
{
    if (echoConfirmed != 0 || fullAsNew != 0) return kWdDone;
    if (deleted != 0) return kWdDeleted;
    return timedOut != 0 ? kWdStuck : kWdWait;
}
/* one group's write is confirmed: a RECORD_SEQ for its id carried a stamp newer than the newest record of it known when its write was
   asked (floor; this game's writes stamp max(now, last + 1), so the asked write is newer than floor) */
inline int R5Confirmed(int hasStamp, long long stamp, long long floor) { return (hasStamp != 0 && stamp > floor) ? 1 : 0; }

/* ---- the off-screen authority's build arm (squadwriter.h CopyBuildGate) ----
   heldByOther: HeldByOtherTS at the squad's position (1 another game holds it, 0 nobody else, -1 no fresh map, -2 frozen). The
   authority builds a sleeping copy only where no game holds the area: the holder's own group is the live one there, and a copy the
   authority builds beside it is refused as handed and built again by the runner's next write (the T1571 loop). */
enum { kAuthBuild = 0, kAuthNoMap = 1, kAuthHeldElsewhere = 2 };
inline int AuthorityArm(int heldByOther)
{
    if (heldByOther == 0) return kAuthBuild;
    return heldByOther == -1 ? kAuthNoMap : kAuthHeldElsewhere;
}
/* sweep trigger (d): before = this game's last remembered answer for the area (1 another game held it, 0 nobody else), now =
   HeldByOtherTS (1 / 0 / -1 no fresh map / -2 frozen: its holder left). 1 = an area another game held is held by nobody else now - directly
   or through frozen (1 -> -2 -> 0, the usual way a holder leaves); the authority's copies of its records may be built. *store = the answer
   to remember: only a holder (1) or nobody (0) is remembered - no fresh map and frozen keep the old one (review fold 1). */
inline int AreaFreedStep(int before, int now, int mineHeld, int* store)
{
    if (now != 0 && now != 1) { *store = before; return 0; }
    *store = now;
    return (before == 1 && now == 0 && mineHeld != 1) ? 1 : 0;
}
/* a mark arrived for an area: kMsSweep = sweep it once now (loaded here and held here; elsewhere the newly-held trigger does it when it fires) */
enum { kMsSweep = 0, kMsNotLoaded = 1, kMsNotHeld = 2, kMsCount = 3 };
inline int MarkArrivalSweep(int loadedHere, int mineHeld)
{
    if (loadedHere == 0) return kMsNotLoaded;
    return mineHeld == 1 ? kMsSweep : kMsNotHeld;
}

/* ---- a position-only note (the authority's off-screen move) that moved no copy here: why ----
   foundUnderNoteKey: a sleeping copy was found under the note's faction key (then it was applied); usable / hasCopy / failedBefore /
   mineHeld: the travel-in road's inputs (squadwriter.h TravelInBuild). */
enum { kNoteApplied = 0, kNoteNotNewer = 1, kNoteOtherKey = 2, kNoteNotHeld = 3, kNoteNoRecord = 4, kNoteFailedBefore = 5, kNoteBuilt = 6, kNoteCount = 7 };
inline int NoteSkipWhy(int notNewer, int foundUnderNoteKey, int usable, int hasCopy, int failedBefore, int mineHeld)
{
    if (notNewer != 0) return kNoteNotNewer;
    if (foundUnderNoteKey != 0) return kNoteApplied;
    if (usable == 0) return kNoteNoRecord;
    if (hasCopy != 0) return kNoteOtherKey;
    if (failedBefore != 0) return kNoteFailedBefore;
    if (mineHeld != 1) return kNoteNotHeld;
    return kNoteBuilt;
}

}   // namespace arearec
