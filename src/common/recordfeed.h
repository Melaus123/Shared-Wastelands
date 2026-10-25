/* src/common/recordfeed.h - T-313: THE RECORD FEED (world-server protocol 63, message 58 RECORD_FEED). Pure: the world server
 * (src/coop-store/store_main.cpp), the plugin (src/coop-plugin/store.cpp) and the offline suite (src/coop-test) compile this same
 * header.
 *
 * WHY IT EXISTS. Until protocol 63 the world server pushed EVERY saved record and every unique state to a game at its WELCOME and
 * forwarded every live RECORD / RECORD_GONE / UNIQUE_STATE to every connected game - including a game waiting at the title
 * screen, which has no world to apply them to. That game's arrival queue for the world-server link filled to 3/4 of its 8 MB
 * bound and the link was refused every 60 s (store.cpp InQueuePressureTick, inQueueRefusedLink).
 *
 * THE RULE. A game is sent records only while it has a world and has ASKED for them:
 *   up   1 ASK      {str afterKey}   turn the feed on and send the next page of records after that key ("" = the first page:
 *                                    BEGIN, every unique state, then records);
 *   up   2 OFF      {}               this game's world is going away - stop the live RECORD / UNIQUE_STATE;
 *   down 3 BEGIN    {u32 records, u32 uniques, u64 seqHigh}              the first page only, before its unique states;
 *   down 4 PAGE_END {str lastKey, u32 sent, u8 last, u64 seqHigh}        after each page's records.
 * The world server subscribes a connection at its ASK (before the page is sent), unsubscribes it at OFF or disconnect, and sends
 * the page and the PAGE_END on reliable channel 0 in one pass - so every live forward queued after the ASK arrives after the
 * page. RECORD_GONE still goes to every game (T-313 review H1: a game not subscribed - the title, a load - is told of a deletion
 * without a group number in no other way, the snapshot lists survivors only, and its push UP would bring the record back; it is
 * one small arrival-queue entry per deletion, applied after the load). DELETED_BITS still go to every game: a deletion made while a game waits at the title is applied there at once, and
 * every RECORD_GONE a game sends deletes a squad group whose bit it also sets and sends.
 * A page is about kFeedPageBytes of record payload (at least one record); the game asks for the next page once its arrival
 * queue holds less than kFeedLowWaterBytes from the world server. The END of the snapshot is the LAST page's PAGE_END, which
 * the game queues behind the records as a MARKER - never a count (the old push ended on a count, and a record with a newer
 * position went out as two messages, which the count did not know).
 *
 * WIRE: u8 kind, three zero bytes, then the kind's fields, little-endian; a string is u32 length + bytes (<= kFeedKeyMax).
 * Nothing may follow the last field. */
#ifndef COOP_COMMON_RECORDFEED_H
#define COOP_COMMON_RECORDFEED_H

#include <cstddef>
#include <map>
#include <string>
#include <vector>
#include "groundrec.h"   /* T-985 fold 1: IsGroundRecId - a ground record never goes as its header only */
#include "boxrec.h"      /* T-985 fold 1: IsBoxRecId - nor a box record */
#include "piecerec.h"    /* IsPieceRecId - nor a piece record */

namespace coopfeed {

const unsigned int kFeedProtocol = 108;     /* the world-server protocol that carries RECORD_FEED (both sides check it at compile time); it moves with store_main.cpp kProtocol, the reason in the commit message (owner 356, 2026-10-02) */


const unsigned int kMsgRecordFeed = 58;    /* 54/55 are effort/m11a's, 57 effort/m8's */
const unsigned int kMsgRecord = 28, kMsgRecordGone = 32, kMsgDeletedBits = 33, kMsgUniqueState = 36;   /* the world server's numbers */
enum { kFeedAsk = 1, kFeedOff = 2, kFeedBegin = 3, kFeedPageEnd = 4 };
/* HELD (up, kind 9 - after preload.h's 5-8): {u8 first, u32 n, n * (str key, u64 fileStamp, u64 posAt, u32 owner)} - the stamps of
   every record in this game's table, in one or more messages sent just before its first-page ASK on the same channel; first = 1 on
   the first message starts the world server's list for this connection over. For that snapshot the world server sends a record the
   game already holds (FeedHeldCovers) as its header only, and every other record in full. */
const unsigned int kFeedHeld = 9;
const size_t kFeedHeldEntries = 2048;          /* at most this many rows a HELD message */
const size_t kFeedHeldBytes = 256u * 1024u;    /* and about this many bytes */
const size_t kFeedHeader = 4;
const size_t kFeedKeyMax = 4096;
const size_t kFeedPageBytes = 1024u * 1024u;          /* ~1 MB of record payload a page */
const size_t kFeedLowWaterBytes = 2u * 1024u * 1024u; /* ask for the next page below 2 MB held from the world server (the bound is 8 MB) */
const size_t kFeedPageRecords = 2048;                  /* review L3: and at most this many records a page (a record may go as two messages) */
const size_t kFeedEntryBound = 16384;                  /* the game's arrival-queue ENTRY bound per origin (store.cpp kInQueueMaxEntriesPerOrigin, checked at compile time) */
const size_t kFeedLowWaterEntries = kFeedEntryBound * 3 / 4 - 2 * kFeedPageRecords - 2;   /* review L3: ask for the next page below this many entries held - a full page and its marker then stay under the 3/4 pressure line */

struct FeedMsg
{
    unsigned int kind;
    std::string key;           /* ASK: afterKey; PAGE_END: lastKey */
    unsigned int records, uniques, sent, last;
    unsigned long long seqHigh;
    FeedMsg() : kind(0), records(0), uniques(0), sent(0), last(0), seqHigh(0ULL) {}
};

inline void FeedPut32(std::vector<char>* b, unsigned int v) { for (int i = 0; i < 4; ++i) b->push_back((char)((v >> (8 * i)) & 0xFFu)); }
inline void FeedPut64(std::vector<char>* b, unsigned long long v) { FeedPut32(b, (unsigned int)(v & 0xFFFFFFFFULL)); FeedPut32(b, (unsigned int)(v >> 32)); }
inline unsigned int FeedGet32(const char* p)
{
    return (unsigned int)(unsigned char)p[0] | ((unsigned int)(unsigned char)p[1] << 8) | ((unsigned int)(unsigned char)p[2] << 16) | ((unsigned int)(unsigned char)p[3] << 24);
}
inline unsigned long long FeedGet64(const char* p) { return (unsigned long long)FeedGet32(p) | ((unsigned long long)FeedGet32(p + 4) << 32); }
inline void FeedHead(std::vector<char>* b, unsigned int kind) { b->push_back((char)kind); b->push_back(0); b->push_back(0); b->push_back(0); }
inline bool FeedPutKey(std::vector<char>* b, const std::string& k)
{
    if (k.size() > kFeedKeyMax) return false;
    FeedPut32(b, (unsigned int)k.size());
    b->insert(b->end(), k.begin(), k.end());
    return true;
}

/* An encode that refuses appends nothing. */
inline bool FeedEncodeAsk(std::vector<char>* b, const std::string& afterKey)
{
    if (afterKey.size() > kFeedKeyMax) return false;
    FeedHead(b, kFeedAsk); FeedPutKey(b, afterKey);
    return true;
}
inline void FeedEncodeOff(std::vector<char>* b) { FeedHead(b, kFeedOff); }
inline void FeedEncodeBegin(std::vector<char>* b, unsigned int records, unsigned int uniques, unsigned long long seqHigh)
{
    FeedHead(b, kFeedBegin); FeedPut32(b, records); FeedPut32(b, uniques); FeedPut64(b, seqHigh);
}
inline bool FeedEncodePageEnd(std::vector<char>* b, const std::string& lastKey, unsigned int sent, bool last, unsigned long long seqHigh)
{
    if (lastKey.size() > kFeedKeyMax) return false;
    FeedHead(b, kFeedPageEnd); FeedPutKey(b, lastKey); FeedPut32(b, sent); b->push_back(last ? (char)1 : (char)0); FeedPut64(b, seqHigh);
    return true;
}
inline bool FeedDecode(const char* p, size_t n, FeedMsg* out)
{
    if (p == 0 || n < kFeedHeader || p[1] != 0 || p[2] != 0 || p[3] != 0) return false;
    FeedMsg m; m.kind = (unsigned int)(unsigned char)p[0];
    size_t at = kFeedHeader;
    if (m.kind == (unsigned int)kFeedAsk || m.kind == (unsigned int)kFeedPageEnd)
    {
        if (n - at < 4) return false;
        const unsigned int len = FeedGet32(p + at); at += 4;
        if (len > kFeedKeyMax || n - at < (size_t)len) return false;
        m.key.assign(p + at, (size_t)len); at += len;
    }
    if (m.kind == (unsigned int)kFeedAsk || m.kind == (unsigned int)kFeedOff) {}
    else if (m.kind == (unsigned int)kFeedBegin)
    {
        if (n - at < 16) return false;
        m.records = FeedGet32(p + at); m.uniques = FeedGet32(p + at + 4); m.seqHigh = FeedGet64(p + at + 8); at += 16;
    }
    else if (m.kind == (unsigned int)kFeedPageEnd)
    {
        if (n - at < 13) return false;
        m.sent = FeedGet32(p + at); m.last = (unsigned int)(unsigned char)p[at + 4]; m.seqHigh = FeedGet64(p + at + 5); at += 13;
        if (m.last > 1) return false;
    }
    else return false;
    if (at != n) return false;
    *out = m;
    return true;
}

/* ONE ROW OF HELD: one record of the game's table - its file time (0 = a position-only record), its position time, its writer. */
struct HeldRow
{
    std::string key;
    long long fileStamp, posAt;
    unsigned int owner;
    HeldRow() : fileStamp(0), posAt(0), owner(0) {}
    HeldRow(const std::string& k, long long f, long long p, unsigned int o) : key(k), fileStamp(f), posAt(p), owner(o) {}
};
/* the whole table as HELD messages, rows in the given order; always at least one message (an empty table is one message with no
   rows, so the world server's list is started over even then). A key over kFeedKeyMax is left out - that record comes in full. A
   time below 0 goes as 0. */
inline void FeedHeldEncodeAll(const std::vector<HeldRow>& rows, std::vector<std::vector<char> >* out)
{
    out->clear();
    size_t i = 0;
    do
    {
        std::vector<char> b; FeedHead(&b, kFeedHeld); b.push_back(out->empty() ? (char)1 : (char)0);
        const size_t countAt = b.size(); FeedPut32(&b, 0);
        unsigned int n = 0;
        for (; i < rows.size(); ++i)
        {
            if (n > 0 && ((size_t)n >= kFeedHeldEntries || b.size() >= kFeedHeldBytes)) break;
            if (rows[i].key.size() > kFeedKeyMax) continue;
            FeedPutKey(&b, rows[i].key);
            FeedPut64(&b, rows[i].fileStamp > 0 ? (unsigned long long)rows[i].fileStamp : 0ULL);
            FeedPut64(&b, rows[i].posAt > 0 ? (unsigned long long)rows[i].posAt : 0ULL);
            FeedPut32(&b, rows[i].owner);
            ++n;
        }
        for (int k = 0; k < 4; ++k) b[countAt + k] = (char)((n >> (8 * k)) & 0xFFu);
        out->push_back(b);
    } while (i < rows.size());
}
inline bool IsFeedHeldKind(const char* p, size_t n)
{
    return p != 0 && n >= kFeedHeader && p[1] == 0 && p[2] == 0 && p[3] == 0 && (unsigned int)(unsigned char)p[0] == kFeedHeld;
}
inline bool FeedHeldDecode(const char* p, size_t n, unsigned int* first, std::vector<HeldRow>* rows)
{
    if (!IsFeedHeldKind(p, n)) return false;
    size_t at = kFeedHeader;
    if (n - at < 5) return false;
    const unsigned int f = (unsigned int)(unsigned char)p[at]; at += 1;
    if (f > 1) return false;
    const unsigned int cnt = FeedGet32(p + at); at += 4;
    if (cnt > kFeedHeldEntries) return false;
    std::vector<HeldRow> rs;
    for (unsigned int i = 0; i < cnt; ++i)
    {
        if (n - at < 4) return false;
        const unsigned int len = FeedGet32(p + at); at += 4;
        if (len > kFeedKeyMax || n - at < (size_t)len) return false;
        HeldRow r; r.key.assign(p + at, (size_t)len); at += len;
        if (n - at < 20) return false;
        const unsigned long long fs = FeedGet64(p + at), ps = FeedGet64(p + at + 8);
        if (fs > 0x7FFFFFFFFFFFFFFFULL || ps > 0x7FFFFFFFFFFFFFFFULL) return false;
        r.fileStamp = (long long)fs; r.posAt = (long long)ps; r.owner = FeedGet32(p + at + 16); at += 20;
        rs.push_back(r);
    }
    if (at != n) return false;
    *first = f; rows->swap(rs);
    return true;
}
/* ALREADY HELD (world server, each record of a snapshot page): 1 = against the stamps the game said it holds, the game's apply
   (store.cpp ApplyRemoteRecord) refuses this record as not newer - its file time is at least the record's and it is the same
   writer's copy (an area holder's zone record is taken from another writer although not newer, ZoneRecvTakesNotNewer), and its
   position time is at least the record's later time. A record with neither time is never held. */
inline int FeedHeldCovers(long long heldFile, long long heldPos, unsigned int heldOwner, int srvHasFile, long long srvFile, long long srvPos, unsigned int srvOwner)
{
    const long long f = (srvHasFile && srvFile > 0) ? srvFile : 0;
    const long long p = srvPos > 0 ? srvPos : 0;
    if (f == 0 && p == 0) return 0;
    if (f > 0 && (heldFile < f || heldOwner != srvOwner)) return 0;
    if (heldPos < (f > p ? f : p)) return 0;
    return 1;
}
/* the stamp of a held record's header: its later time - the game holds a position time at least this (FeedHeldCovers), so its apply
   refuses the header as not newer */
inline long long FeedHeldHeaderStamp(int srvHasFile, long long srvFile, long long srvPos)
{
    const long long f = (srvHasFile && srvFile > 0) ? srvFile : 0;
    const long long p = srvPos > 0 ? srvPos : 0;
    return f > p ? f : p;
}
/* T-985 fold 1 (review HIGH): a ground record (ground.<sx>.<sy>) or a box record (box.<...>) is never sent as its header only. The
   game's receive hands those to the ground and box tables before any stamp check (store.cpp GroundRecordArrive / BoxRecordArrive),
   and they need the whole contents: an empty ground record is marked unreadable, an empty box record is not kept. Any spelling of
   the prefix counts, as in the id tests. The game leaves them out of its HELD list; the world server never judges them held. */
inline bool FeedHeldKeyListed(const std::string& key)
{
    return !coopgrec::IsGroundRecId(key) && !coopbrec::IsBoxRecId(key) && !piecerec::IsPieceRecId(key);   /* a piece record too */
}
/* the world server's judgement for one record of a snapshot page: 0 for a ground or box id (always in full), else FeedHeldCovers */
inline int FeedHeldCoversKey(const std::string& key, long long heldFile, long long heldPos, unsigned int heldOwner, int srvHasFile, long long srvFile, long long srvPos, unsigned int srvOwner)
{
    if (!FeedHeldKeyListed(key)) return 0;
    return FeedHeldCovers(heldFile, heldPos, heldOwner, srvHasFile, srvFile, srvPos, srvOwner);
}

/* WHO GETS A LIVE FORWARD. RECORD and UNIQUE_STATE go only to a subscribed connection; every other type (RECORD_GONE - review H1 - and the
   DELETED_BITS above all) is not the feed's business. Never to the sender, never to a connection that is not connected. */
enum { kFwdSkip = 0, kFwdSend = 1, kFwdWithheld = 2 };
inline bool FeedGatedType(unsigned int t) { return t == kMsgRecord || t == kMsgUniqueState; }   /* review H1: RECORD_GONE is NOT gated - it reaches a game at the title */
inline int FeedForwardDecide(unsigned int msgType, int connected, int isSender, int feedOn)
{
    if (!connected || isSender) return kFwdSkip;
    if (!FeedGatedType(msgType)) return kFwdSend;
    return feedOn ? kFwdSend : kFwdWithheld;
}

/* THE PAGE (world server). Records strictly after afterKey ("" = from the first), in key order, until at least `budget` bytes
   have been sent - always at least one record while any is left. `send(key, value)` sends one and returns its bytes on the wire.
   Returns whether this page reached the end; *lastKey is the last key sent (afterKey when none was). A record inserted before
   the cursor between two pages, changed or deleted, reached the subscribed game as a live forward, so the game ends equal to
   the source (the offline suite's paging test). Review L3: also at most maxRecords records a page. */
template <class V, class Sender>
inline bool FeedPageWalk(const std::map<std::string, V>& m, const std::string& afterKey, size_t budget, size_t maxRecords, Sender& send, std::string* lastKey, unsigned int* sent)
{
    typename std::map<std::string, V>::const_iterator it = afterKey.empty() ? m.begin() : m.upper_bound(afterKey);
    size_t bytes = 0; unsigned int k = 0; std::string last = afterKey;
    for (; it != m.end(); ++it)
    {
        if (k > 0 && (bytes >= budget || (size_t)k >= maxRecords)) break;
        bytes += send(it->first, it->second);
        ++k; last = it->first;
    }
    *lastKey = last; *sent = k;
    return it == m.end();
}
template <class V, class Sender>
inline bool FeedPageWalk(const std::map<std::string, V>& m, const std::string& afterKey, size_t budget, Sender& send, std::string* lastKey, unsigned int* sent)
{
    return FeedPageWalk(m, afterKey, budget, kFeedPageRecords, send, lastKey, sent);
}

/* THE ASK DECISION (game). The first page is asked on the first tick on which this game is welcomed on a link it speaks and
   has a running world (engine writes not blocked), once per (world generation, link generation) pair; the next page once the
   last PAGE_END said there is more AND this game holds under kFeedLowWaterBytes from the world server. */
struct FeedAskState
{
    int asked;                 /* an ASK("") went out for (worldGen, linkGen) and no OFF since */
    long worldGen, linkGen;
    int paging;                /* an ASK is out and its PAGE_END has not arrived */
    int wantNext;              /* the last PAGE_END said there is more */
    std::string nextKey;
    unsigned int epoch;        /* +1 at every first ASK and every OFF: a marker of another epoch is stale */
    long counterLinkGen;       /* the link the two counters below count on */
    unsigned long long asksSent, endsGot;   /* one PAGE_END answers each ASK, in order, on one link */
    unsigned int firstAsks;    /* T-985 step 2B: first-page ASKs on counterLinkGen - the world server numbers its AREA_DONE by the same count */
    FeedAskState() : asked(0), worldGen(0), linkGen(0), paging(0), wantNext(0), epoch(0), counterLinkGen(-1), asksSent(0ULL), endsGot(0ULL), firstAsks(0) {}
};
enum { kAskNone = 0, kAskFirst = 1, kAskNext = 2 };
inline int FeedAskDecide(const FeedAskState& s, int welcomed, int blocked, long worldGen, long linkGen, size_t heldBytes, size_t heldEntries)
{
    if (!welcomed || blocked) return kAskNone;
    if (!s.asked || s.worldGen != worldGen || s.linkGen != linkGen) return kAskFirst;
    if (s.wantNext && !s.paging && heldBytes < kFeedLowWaterBytes && heldEntries < kFeedLowWaterEntries) return kAskNext;   /* review L3: bytes AND entries */
    return kAskNone;
}
inline int FeedAskDecide(const FeedAskState& s, int welcomed, int blocked, long worldGen, long linkGen, size_t heldBytes)
{
    return FeedAskDecide(s, welcomed, blocked, worldGen, linkGen, heldBytes, 0);
}
/* the ASK the decision named has been SENT */
inline void FeedAskTaken(FeedAskState* s, int act, long worldGen, long linkGen)
{
    if (act != kAskFirst && act != kAskNext) return;
    if (s->counterLinkGen != linkGen) { s->counterLinkGen = linkGen; s->asksSent = 0ULL; s->endsGot = 0ULL; s->firstAsks = 0; }
    ++s->asksSent; s->paging = 1; s->wantNext = 0;
    if (act == kAskFirst) { s->asked = 1; s->worldGen = worldGen; s->linkGen = linkGen; s->nextKey.clear(); ++s->epoch; ++s->firstAsks; }
}
/* a PAGE_END arrived on link `linkGen`: 1 when it answers the LATEST ask (its marker is queued), 0 when it is stale - an
   answer to an ask made before an OFF, or on another link. */
inline int FeedPageEndArrived(FeedAskState* s, long linkGen, const std::string& lastKey, unsigned int last)
{
    if (s->counterLinkGen != linkGen) return 0;
    ++s->endsGot;
    if (s->endsGot != s->asksSent || !s->asked || !s->paging) return 0;
    s->paging = 0;
    if (last == 0) { s->wantNext = 1; s->nextKey = lastKey; } else { s->wantNext = 0; s->nextKey.clear(); }
    return 1;
}
/* the world is going away (every load, and quitting to the menu): returns whether an OFF is owed (an ASK went out) */
inline int FeedOffTaken(FeedAskState* s)
{
    const int was = s->asked;
    s->asked = 0; s->paging = 0; s->wantNext = 0; s->nextKey.clear(); ++s->epoch;
    return was;
}

/* the link to the world server dropped: the world server erased this game's subscription with the connection, so nothing is
   asked any more and no OFF is owed for it (an OFF now would reach the next connection before its WELCOME). The next ASK is a
   first page, on the next welcomed link. */
inline void FeedLinkDropped(FeedAskState* s)
{
    s->asked = 0; s->paging = 0; s->wantNext = 0; s->nextKey.clear();
}

/* Review L4: a BEGIN arrived on link `linkGen` - 1 when it answers the LATEST ask (it comes before that ask's PAGE_END), 0 when
   it is stale like a stale PAGE_END (an ask made before an OFF, or on another link). Read only - the PAGE_END counts. */
inline int FeedBeginArrived(const FeedAskState& s, long linkGen)
{
    return (s.counterLinkGen == linkGen && s.asked && s.paging && s.endsGot + 1ULL == s.asksSent) ? 1 : 0;
}
/* Review L1: an OFF that could not be sent is OWED on the link it was due on (owedLink; -1 none). Retried while that link is
   up; dropped once the link has moved (the world server erased the subscription with the old connection). */
enum { kOffNone = 0, kOffRetry = 1, kOffDrop = 2 };
inline int FeedOffRetryDecide(long owedLink, long curLink, int linkUp)
{
    if (owedLink < 0) return kOffNone;
    if (curLink != owedLink) return kOffDrop;
    return linkUp ? kOffRetry : kOffNone;
}
/* Review M1: THE CACHED NOTEBOOK NUMBER ONLY RISES WITHIN ONE LINK. A feed page (an older number) waits in the arrival queue
   while the RECORD_SEQ echo of this game's own later write (a newer number) is applied at once; the page's drain must not wind
   the number back. A lower number is legitimate only across links (a REPAIR drops the link). 1 = write it. */
inline int FeedSeqNoteDecide(unsigned long long cached, long cachedLink, unsigned long long incoming, long curLink)
{
    if (cachedLink != curLink) return 1;
    return incoming >= cached ? 1 : 0;
}

/* THE MARKER (game): {u32 epoch} + the PAGE_END as it arrived, queued behind the page's records and applied by the drain in
   arrival order. The LAST page's marker says this world's snapshot is applied; the FIRST such marker on a link also ends the
   notebook's push for StoreRepushUpTick (index done, repush armed) - once per link, as before. */
enum { kMarkStale = 0, kMarkPage = 1, kMarkEndFirst = 2, kMarkEndAgain = 3 };
inline void FeedMarkerEncode(std::vector<char>* b, unsigned int epoch, const std::vector<char>& pageEnd)
{
    b->clear(); FeedPut32(b, epoch); b->insert(b->end(), pageEnd.begin(), pageEnd.end());
}
inline bool FeedMarkerDecode(const std::vector<char>& b, unsigned int* epoch, FeedMsg* m)
{
    if (b.size() < 4 + kFeedHeader) return false;
    FeedMsg t;
    if (!FeedDecode(&b[4], b.size() - 4, &t) || t.kind != (unsigned int)kFeedPageEnd) return false;
    *epoch = FeedGet32(&b[0]); *m = t;
    return true;
}
inline int FeedMarkerApply(unsigned int markerEpoch, unsigned int curEpoch, unsigned int last, bool* indexDone, bool* snapshotApplied, int* repushPending)
{
    if (markerEpoch != curEpoch) return kMarkStale;
    if (last == 0) return kMarkPage;
    *snapshotApplied = true;
    if (*indexDone) return kMarkEndAgain;
    *indexDone = true; *repushPending = 1;
    return kMarkEndFirst;
}

/* ---- T-985 step 2B: AREA_DONE (down, kind 10) {u32 epoch, u8 all, u32 n, n * u16 area key} ----
   The world server says the records of the named areas (all = 1: of every area) have all been sent for this snapshot. It follows
   the PAGE_END on the same reliable channel; the game queues it behind the records as a marker (FeedMarkerEncode, as the PAGE_END)
   and the drain stamps each named area (or the whole table) with the link generation (store.cpp StoreAreaDoneApply). Each gate that
   read "the feed is done" asks whether ITS OWN AREA is done. Step 2B: the server sends only all = 1, right after the last PAGE_END
   (the empty malformed last page too), so every gate's answer is the old one; store.cpp counts any disagreement (gateDisagree).
   epoch = this connection's count of first-page ASKs (store_main.cpp g_feedFirstAsks; the game's FeedAskState::firstAsks): an
   AREA_DONE of an earlier snapshot on the link is stale. The area key is y * 64 + x (cooplive::AreaKey, zones.cpp Key()). */
const unsigned int kFeedAreaDone = 10;
const int kFeedAreaGrid = 64;
const int kFeedAreaKeys = kFeedAreaGrid * kFeedAreaGrid;   /* 4096 */
const int kFeedAreaNone = -1;                              /* no area: only the whole feed (all = 1) answers */
const int kFeedAreaAny = -2;                               /* any area: the stamp of the last AREA_DONE applied */
const size_t kFeedAreaDoneMax = 4096;                      /* at most this many keys a message */

inline int FeedAreaKey(int x, int y) { return (x >= 0 && x < kFeedAreaGrid && y >= 0 && y < kFeedAreaGrid) ? y * kFeedAreaGrid + x : kFeedAreaNone; }
/* items.cpp's sector index is x * 64 + y (GrcCentre, BoxRecSecOfId): the same sector's area key */
inline int FeedSecXYAreaKey(int sec)
{
    if (sec < 0 || sec >= kFeedAreaKeys) return kFeedAreaNone;
    return FeedAreaKey(sec / kFeedAreaGrid, sec % kFeedAreaGrid);
}
/* "zone.<X>.<Y>" (store.cpp ZoneIdOfPath) -> FeedAreaKey(X, Y); kFeedAreaNone for any other text */
inline int FeedZoneIdAreaKey(const std::string& id)
{
    if (id.size() < 8 || id.compare(0, 5, "zone.") != 0) return kFeedAreaNone;
    size_t at = 5;
    int v[2] = { 0, 0 };
    for (int f = 0; f < 2; ++f)
    {
        int digits = 0, x = 0;
        while (at < id.size() && id[at] >= '0' && id[at] <= '9')
        {
            if (digits >= 3) return kFeedAreaNone;
            x = x * 10 + (id[at] - '0'); ++digits; ++at;
        }
        if (digits == 0) return kFeedAreaNone;
        v[f] = x;
        if (f == 0) { if (at >= id.size() || id[at] != '.') return kFeedAreaNone; ++at; }
    }
    if (at != id.size()) return kFeedAreaNone;
    return FeedAreaKey(v[0], v[1]);
}

struct AreaDoneMsg
{
    unsigned int epoch, all;
    std::vector<unsigned short> keys;
    AreaDoneMsg() : epoch(0), all(0) {}
};
/* An encode that refuses (a key out of the grid, more than kFeedAreaDoneMax keys) appends nothing. */
inline bool FeedEncodeAreaDone(std::vector<char>* b, unsigned int epoch, bool all, const std::vector<unsigned short>& keys)
{
    if (keys.size() > kFeedAreaDoneMax) return false;
    for (size_t i = 0; i < keys.size(); ++i) if ((int)keys[i] >= kFeedAreaKeys) return false;
    FeedHead(b, kFeedAreaDone); FeedPut32(b, epoch); b->push_back(all ? (char)1 : (char)0); FeedPut32(b, (unsigned int)keys.size());
    for (size_t i = 0; i < keys.size(); ++i) { b->push_back((char)(keys[i] & 0xFFu)); b->push_back((char)((keys[i] >> 8) & 0xFFu)); }
    return true;
}
inline bool IsFeedAreaDoneKind(const char* p, size_t n)
{
    return p != 0 && n >= kFeedHeader && p[1] == 0 && p[2] == 0 && p[3] == 0 && (unsigned int)(unsigned char)p[0] == kFeedAreaDone;
}
inline bool FeedAreaDoneDecode(const char* p, size_t n, AreaDoneMsg* out)
{
    if (!IsFeedAreaDoneKind(p, n)) return false;
    size_t at = kFeedHeader;
    if (n - at < 9) return false;
    AreaDoneMsg m;
    m.epoch = FeedGet32(p + at); m.all = (unsigned int)(unsigned char)p[at + 4];
    const unsigned int cnt = FeedGet32(p + at + 5); at += 9;
    if (m.all > 1 || cnt > (unsigned int)kFeedAreaDoneMax) return false;
    if (n - at != (size_t)cnt * 2) return false;
    for (unsigned int i = 0; i < cnt; ++i)
    {
        const unsigned int k = (unsigned int)(unsigned char)p[at] | ((unsigned int)(unsigned char)p[at + 1] << 8); at += 2;
        if ((int)k >= kFeedAreaKeys) return false;
        m.keys.push_back((unsigned short)k);
    }
    *out = m;
    return true;
}
/* THE AREA_DONE MARKER (game): {u32 epoch} + the AREA_DONE as it arrived (FeedMarkerEncode), applied by the drain in arrival order */
inline bool IsFeedAreaDoneMarker(const std::vector<char>& b) { return b.size() >= 4 + kFeedHeader && IsFeedAreaDoneKind(&b[4], b.size() - 4); }
inline bool FeedAreaDoneMarkerDecode(const std::vector<char>& b, unsigned int* epoch, AreaDoneMsg* m)
{
    if (!IsFeedAreaDoneMarker(b)) return false;
    AreaDoneMsg t;
    if (!FeedAreaDoneDecode(&b[4], b.size() - 4, &t)) return false;
    *epoch = FeedGet32(&b[0]); *m = t;
    return true;
}
/* an AREA_DONE arrived on link `linkGen`: 1 when it belongs to this link's latest first-page ASK and that snapshot's last PAGE_END
   has been taken (nothing out, no next page wanted); 0 = stale */
inline int FeedAreaDoneArrived(const FeedAskState& s, long linkGen, unsigned int wireEpoch)
{
    return (s.counterLinkGen == linkGen && s.asked && !s.paging && !s.wantNext && wireEpoch == s.firstAsks) ? 1 : 0;
}
/* THE STAMP (fold, review LOW 4): (generation << 32) | link. The generation (store.cpp g_areaDoneGen) moves at every clear - each
   WELCOME and every world load - so a stamp written before a clear never reads as done after it, on the same link too. -1 = not done. */
inline long long FeedAreaStampMake(long gen, long link) { return (gen < 0 || link < 0) ? -1LL : (((long long)gen << 32) | (long long)(unsigned long)link); }
inline long FeedAreaStampGen(long long stamp) { return stamp < 0 ? -1L : (long)(stamp >> 32); }
/* THE TABLE (game): per area the stamp at which it was done, the whole-table stamp, and the stamp of the last write (any area). The
   plugin writes and reads each with interlocked calls (store.cpp); the offline suite uses these. */
struct AreaDoneTable
{
    volatile long long all, some;
    volatile long long area[kFeedAreaKeys];
    AreaDoneTable() { Clear(); }
    void Clear() { all = -1; some = -1; for (int i = 0; i < kFeedAreaKeys; ++i) area[i] = -1; }
    void Apply(const AreaDoneMsg& m, long long stamp)   /* a partial AREA_DONE (all = 0): its named areas */
    {
        for (size_t i = 0; i < m.keys.size(); ++i) if ((int)m.keys[i] < kFeedAreaKeys) area[m.keys[i]] = stamp;
        some = stamp;
    }
    long long Stamp(int k) const
    {
        if (k == kFeedAreaAny) return some;
        if (k < 0 || k >= kFeedAreaKeys) return -1;
        return area[k];
    }
};
/* THE LAST PAGE'S MARKER (fold, review MED 1 + 2): in 2B it says every area is done - written before g_feedDoneLinkGen, so a gate
   never sees the feed done and its area not. STEP D (filtering): the marker must stop doing this; only AREA_DONE all = 1 may then. */
inline void FeedAreaMarkAll(AreaDoneTable* t, long long stamp) { t->all = stamp; t->some = stamp; }
/* AREA_DONE AT THE DRAIN: all = 1 only counts (the marker set the whole table already) - kAdAllMismatch when the whole table is not at
   this feed's stamp, i.e. it names a feed the marker did not complete; all = 0 stamps its named areas and settles their reads (step C) */
enum { kAdAllCounted = 0, kAdAllMismatch = 1, kAdAreas = 2 };
inline int FeedAreaDoneDecide(long long allStampNow, unsigned int all, long long stamp)
{
    if (all != 0) return (stamp >= 0 && allStampNow == stamp) ? kAdAllCounted : kAdAllMismatch;
    return kAdAreas;
}
/* THE ZONE GATES (the area read, load-from-record): done ON this link in this generation - g_feedDoneLinkGen's meaning, area by area */
inline int FeedAreaDoneOnLink(long long allStamp, long long areaStamp, long gen, long link)
{
    const long long want = FeedAreaStampMake(gen, link);
    return (want >= 0 && (allStamp == want || areaStamp == want)) ? 1 : 0;
}
/* THE SNAPSHOT GATES (ground, boxes, the town): done in this generation, on any link - StoreSnapshotApplied's meaning, area by area */
inline int FeedAreaDoneHeld(long long allStamp, long long areaStamp, long gen)
{
    return (gen >= 0 && (FeedAreaStampGen(allStamp) == gen || FeedAreaStampGen(areaStamp) == gen)) ? 1 : 0;
}
/* the settle of named areas: is area k one of them */
inline bool FeedAreaListHas(const std::vector<unsigned short>& keys, int k)
{
    if (k < 0) return false;
    for (size_t i = 0; i < keys.size(); ++i) if ((int)keys[i] == k) return true;
    return false;
}

}   /* namespace coopfeed */

#endif
