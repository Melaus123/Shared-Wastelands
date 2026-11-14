/* src/common/aoifilter.h - T-1339 (owner 1608 a; design B steps 1-3; world-server protocol 153): THE FORWARD FILTER. Pure: the world
 * server (src/coop-store/store_main.cpp RecordForward, AoifRecompute, AoifCatchUp) and the offline suite (src/coop-test) compile it.
 *
 * WHY. Until protocol 153 the world server sent every record any game wrote to every other game in a world, wherever it was
 * (store_main.cpp RecordForward): with many players every link carries every player's saves (T-1320 part c, run T2314: 141 of
 * 148 forwards to a slow game were for areas nowhere near it). Owner decision 1023 adopted an area of interest; 1080 kept the
 * one-time full first download at join (unchanged here); 1608 a reopened the filter for the ongoing forwards.
 *
 * THE RULE. Per game, its INTEREST = the areas of its last AREAS report (loaded / kept), the areas its player characters stand in,
 * the areas it asked AREA_FRESH for and is loading (fold 2, Game::InterestBase) and the areas it holds, widened by one ring (kRing). A record whose area is outside it is HELD for that game, not forwarded: the world server notes "stale since record
 * seq X" for (game, area) and sends ONE AREA_STALE for that area (recordfeed.h kFeedAreaStale) - once until it is caught up. When
 * the area enters the game's interest (a report, a holder change) or the game asks AREA_FRESH for it, the world server sends the
 * latest copy of every record of that area written at or after the mark (and of every squad that left it while held), then
 * AREA_DONE all=0 naming it - on the same line as the STALE, so the game reads the area as not done from the STALE until that
 * DONE (recordfeed.h FeedAreaReadyOnLink / FeedAreaReadyHeld).
 * Always forwarded (ForwardDecide): everything until the game's snapshot is out and it has sent a non-empty AREAS report after its
 * first-page ASK (filterOn = 0); a record that answers that game's own area question; a record with no area (aoi::kNoArea); a squad
 * record (placed by its position, not named by its key) to the off-screen authority (it builds sleeping copies of squads where
 * nobody is - design step 4 gives it its own interest later); a squad record whose PREVIOUS area is in interest (it left an area
 * the game sees). RECORD_GONE, UNIQUE_STATE and every other message are not records and stay world-wide (not this header's).
 * Area keys are y * 64 + x (cooplive::AreaKey, recordfeed.h FeedAreaKey). */
#ifndef COOP_COMMON_AOIFILTER_H
#define COOP_COMMON_AOIFILTER_H

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace aoif {

const int kGrid = 64;
const int kRing = 1;            /* one area-square around the reported and held areas (the LIVE delivery set's margin, liverelay.h kAreaMargin) */
const int kNoArea = -1;         /* = aoi::kNoArea (store_main.cpp checks it) */

/* INTEREST: reported + held, each widened by `ring` squares each way, clipped at the map's edge (no wrap); keys off the grid are skipped */
inline void InterestBuild(const std::set<int>& reported, const std::set<int>& held, int ring, std::set<int>* out)
{
    out->clear();
    std::set<int> base(reported);
    base.insert(held.begin(), held.end());
    for (std::set<int>::const_iterator it = base.begin(); it != base.end(); ++it)
    {
        if (*it < 0 || *it >= kGrid * kGrid) continue;
        const int x = *it % kGrid, y = *it / kGrid;
        for (int dy = -ring; dy <= ring; ++dy)
            for (int dx = -ring; dx <= ring; ++dx)
            {
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || nx >= kGrid || ny < 0 || ny >= kGrid) continue;
                out->insert(ny * kGrid + nx);
            }
    }
}
/* the areas in `after` that were not in `before`, in key order */
inline void NewlyIn(const std::set<int>& before, const std::set<int>& after, std::vector<int>* out)
{
    out->clear();
    for (std::set<int>::const_iterator it = after.begin(); it != after.end(); ++it) if (before.count(*it) == 0) out->push_back(*it);
}

/* ONE FORWARD TO ONE GAME. filterOn: that game's snapshot is out and it has reported its areas since (else everything goes, as
   before protocol 153); answer: the record answers that game's own area question; area: the record's area (RecordAreaOf), kNoArea
   none; named: its key names the area (area, ground, box, piece, door, cage, town clock) - else it is placed by its position (a
   squad); prevArea: its area before this write (any value < 0 = none); authority: that game is the off-screen authority.
   *why (may be 0) says which rule decided. */
enum { kSend = 0, kHold = 1 };
enum { kWhyFilterOff = 0, kWhyAnswer = 1, kWhyNoArea = 2, kWhyAuthority = 3, kWhyInterest = 4, kWhyPrevious = 5, kWhyHeld = 6, kWhyCount = 7 };
inline int ForwardDecide(int filterOn, int answer, int area, int named, int prevArea, int authority, const std::set<int>& interest, int* why)
{
    int w = kWhyHeld;
    if (!filterOn) w = kWhyFilterOff;
    else if (answer) w = kWhyAnswer;
    else if (area < 0) w = kWhyNoArea;
    else if (!named && authority) w = kWhyAuthority;
    else if (interest.count(area) != 0) w = kWhyInterest;
    else if (!named && prevArea >= 0 && interest.count(prevArea) != 0) w = kWhyPrevious;
    if (why != 0) *why = w;
    return w == kWhyHeld ? kHold : kSend;
}

/* THE STALE BOOK of one game: per area outside its interest with a held write, the seq of its first held write and the squads that
   left it while held (their latest copy is part of its catch-up wherever they are now). */
struct StaleMark
{
    unsigned long long since;
    std::set<std::string> moved;
    int unsent;                      /* fold 1 (review MED 1): its AREA_STALE is not on the wire yet (new, or its send was refused) - sent again at the next hold or event of that game */
    int queued;                      /* fold 1 (MED 4): in the game's catch-up queue (Game::todo) */
    int running;                     /* fold 1 (MED 4): its catch-up has started - `left` is what it still has to send */
    std::deque<std::string> left;    /* the ids its running catch-up still sends; a write held meanwhile is added (Book::Hold) */
    StaleMark() : since(0ULL), unsent(1), queued(0), running(0) {}
};
struct Book
{
    std::map<int, StaleMark> stale;
    /* a write of `id` (seq) held for this game: its area is marked, and a squad's previous area too when it moved (the game's copy
       stands there). *newly gets each area marked for the first time - ONE AREA_STALE is due for each, none for an area already marked. */
    void Hold(const std::string& id, int area, int named, int prevArea, unsigned long long seq, std::vector<int>* newly)
    {
        Mark(area, seq, newly);
        Running(area, id);
        if (!named && prevArea >= 0 && prevArea != area)
        {
            Mark(prevArea, seq, newly);
            std::map<int, StaleMark>::iterator p = stale.find(prevArea);
            if (p != stale.end()) p->second.moved.insert(id);
            Running(prevArea, id);
        }
    }
    /* fold 1 (MED 4): a write held while its area's catch-up runs joins what that catch-up still sends (the catch-up's list was taken
       at its start) - so its AREA_DONE never ends a mark with a held write unsent */
    void Running(int area, const std::string& id)
    {
        std::map<int, StaleMark>::iterator it = stale.find(area);
        if (it != stale.end() && it->second.running != 0) it->second.left.push_back(id);
    }
    /* the marked areas whose AREA_STALE is not on the wire (fold 1, MED 1) */
    void Unsent(std::vector<int>* out) const
    {
        out->clear();
        for (std::map<int, StaleMark>::const_iterator it = stale.begin(); it != stale.end(); ++it) if (it->second.unsent != 0) out->push_back(it->first);
    }
    /* fold 1 (LOW 6, MED 3): the marked areas due a catch-up and not queued yet - those in `interest` (an area that entered, or one whose
       catch-up failed and that is still in it), or every one when `all` (the off-screen authority's new game) */
    void Due(const std::set<int>& interest, bool all, std::vector<int>* out) const
    {
        out->clear();
        for (std::map<int, StaleMark>::const_iterator it = stale.begin(); it != stale.end(); ++it)
            if (it->second.queued == 0 && (all || interest.count(it->first) != 0)) out->push_back(it->first);
    }
    void Mark(int area, unsigned long long seq, std::vector<int>* newly)
    {
        if (area < 0 || area >= kGrid * kGrid) return;
        std::map<int, StaleMark>::iterator it = stale.find(area);
        if (it == stale.end()) { StaleMark m; m.since = seq; stale[area] = m; if (newly != 0) newly->push_back(area); return; }
        if (seq < it->second.since) it->second.since = seq;
    }
    bool Has(int area) const { return stale.find(area) != stale.end(); }
    void Clear() { stale.clear(); }
};
/* THE CATCH-UP of one marked area: the ids now in it (inArea, may be 0) and the ids that moved out of it while held, each once, in key
   order, whose latest write is at or after the mark. seqOf(id, &seq) answers false for an id no longer held (deleted - RECORD_GONE
   went world-wide). */
template <class SeqOf>
inline void CatchUpSelect(const std::set<std::string>* inArea, const StaleMark& m, SeqOf& seqOf, std::vector<std::string>* out)
{
    out->clear();
    std::set<std::string> ids(m.moved);
    if (inArea != 0) ids.insert(inArea->begin(), inArea->end());
    for (std::set<std::string>::const_iterator it = ids.begin(); it != ids.end(); ++it)
    {
        unsigned long long s = 0ULL;
        if (!seqOf(*it, &s)) continue;
        if (s >= m.since) out->push_back(*it);
    }
}
/* the per-game state the world server keeps (store_main.cpp g_aoif): reportSeen = a non-empty AREAS report since its first-page
   ASK; snapDone = that snapshot's last page is out. The filter is on only when both hold. */
/* fold 2 (T2336): THE AREAS A GAME IS IN AND LOADING. A report carrying the LOADING mark names the areas the engine has loaded - after a
   teleport the OLD ones (the engine adds the new areas only when its load ends), so the area the game stands in and is laying was outside
   its interest for the whole freeze (T2336: 16,33 for ~60 s), a write there was held and its AREA_STALE landed after the game had laid
   the area from the answer to its own AREA_FRESH ask. Two facts the game already sends are added to the reported areas (each widened by
   the same ring): STANDING - the areas its player characters stand in (the AREAS report's player-sector list, replaced by each report);
   ASKED - each area it asked AREA_FRESH for, kept while the ask is open (no FRESH / CANT sent to it yet) and, once answered, until its next
   report WITHOUT the loading mark that lists areas (that report says what the engine keeps; an area laid during a walk stands next to a
   kept one, inside the ring). So an area with an open ask, or one being laid during a freeze, is in interest: its writes are forwarded and
   no AREA_STALE is ever sent for it. */
struct Game
{
    int reportSeen, snapDone;
    std::set<int> interest, lastReported;   /* lastReported: fold 1 (LOW 7) - the last NON-EMPTY report's areas */
    std::set<int> standing;                 /* fold 2: its player characters' areas (the last report's player-sector list) */
    std::map<int, int> asked;               /* fold 2: area -> 0 its AREA_FRESH ask is open, 1 answered (kept until a non-empty unmarked report) */
    Book book;
    std::deque<int> todo;                   /* fold 1 (MED 4): areas whose catch-up waits or runs, the front first */
    Game() : reportSeen(0), snapDone(0) {}
    int On() const { return (reportSeen != 0 && snapDone != 0) ? 1 : 0; }
    void SnapshotStart() { reportSeen = 0; snapDone = 0; interest.clear(); lastReported.clear(); standing.clear(); asked.clear(); book.Clear(); todo.clear(); }   /* a first-page ASK: its snapshot carries every latest copy */
    /* fold 2: an AREAS report's facts - psXY = its player-sector list (x,y pairs; off-grid skipped; empty = "not known", none kept);
       loadingMark = it carries the LOADING mark; listed = how many areas it names. A non-empty report without the mark ends every
       answered ask. */
    void ReportFacts(const std::vector<int>& psXY, int loadingMark, int listed)
    {
        standing.clear();
        for (size_t k = 0; k + 1 < psXY.size(); k += 2)
            if (psXY[k] >= 0 && psXY[k] < kGrid && psXY[k + 1] >= 0 && psXY[k + 1] < kGrid) standing.insert(psXY[k + 1] * kGrid + psXY[k]);
        if (loadingMark != 0 || listed <= 0) return;
        for (std::map<int, int>::iterator it = asked.begin(); it != asked.end(); ) { if (it->second != 0) asked.erase(it++); else ++it; }
    }
    void Asked(int area) { if (area >= 0 && area < kGrid * kGrid) asked.insert(std::make_pair(area, 0)); }   /* a re-ask of an answered one keeps it answered (its answer may be on the way and is not sent twice) */
    void Answered(int area) { std::map<int, int>::iterator it = asked.find(area); if (it != asked.end()) it->second = 1; }
    /* the base of its interest: the reported areas, plus where it stands and every area it asked for (above) */
    void InterestBase(const std::set<int>& reported, std::set<int>* out) const
    {
        *out = reported;
        out->insert(standing.begin(), standing.end());
        for (std::map<int, int>::const_iterator it = asked.begin(); it != asked.end(); ++it) out->insert(it->first);
    }
    void Queue(int area)
    {
        std::map<int, StaleMark>::iterator it = book.stale.find(area);
        if (it == book.stale.end() || it->second.queued != 0) return;
        it->second.queued = 1; todo.push_back(area);
    }
};
/* fold 1 (LOW 7): THE AREAS A REPORT COUNTS FOR. An empty report while the filter is on keeps the last non-empty one: a game that leaves
   its world sends OFF (its state goes), so an empty report from a game in its world is a moment between loads - shrinking to its held
   areas would mark (and STALE) its own areas until the next report. *last takes a non-empty report. */
inline void ReportedFor(const std::set<int>& now, int filterOn, std::set<int>* last, std::set<int>* out)
{
    if (!now.empty()) { *last = now; *out = now; return; }
    *out = filterOn != 0 ? *last : now;
}
/* fold 1 (MED 4): THE PACE. A catch-up sends at most kCatchUpPerGame records a loop pass for one game, kCatchUpPerPass for all games,
   and none while that game's send queue already holds kCatchUpQueueBytes or more (each record is a disk read on the loop thread, and a
   whole area at once would sit in front of every later forward). An area the game ASKED for (AREA_FRESH) is not paced: the game waits
   on it, and its catch-up runs whole before any answer (store_main.cpp AoifCatchUpNow). */
const int kCatchUpPerGame = 4;
const int kCatchUpPerPass = 16;
const long long kCatchUpQueueBytes = 256 * 1024;
inline int CatchUpBudget(long long queuedBytes, int passLeft)
{
    if (passLeft <= 0 || queuedBytes >= kCatchUpQueueBytes) return 0;
    return passLeft < kCatchUpPerGame ? passLeft : kCatchUpPerGame;
}

}   /* namespace aoif */

#endif
