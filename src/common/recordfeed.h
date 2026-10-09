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

namespace coopfeed {

const unsigned int kFeedProtocol = 90;     /* the world-server protocol that carries RECORD_FEED (both sides check it at compile time); it moves with store_main.cpp kProtocol, the reason in the commit message (owner 356, 2026-10-02) */
const unsigned int kMsgRecordFeed = 58;    /* 54/55 are effort/m11a's, 57 effort/m8's */
const unsigned int kMsgRecord = 28, kMsgRecordGone = 32, kMsgDeletedBits = 33, kMsgUniqueState = 36;   /* the world server's numbers */
enum { kFeedAsk = 1, kFeedOff = 2, kFeedBegin = 3, kFeedPageEnd = 4 };
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
    FeedAskState() : asked(0), worldGen(0), linkGen(0), paging(0), wantNext(0), epoch(0), counterLinkGen(-1), asksSent(0ULL), endsGot(0ULL) {}
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
    if (s->counterLinkGen != linkGen) { s->counterLinkGen = linkGen; s->asksSent = 0ULL; s->endsGot = 0ULL; }
    ++s->asksSent; s->paging = 1; s->wantNext = 0;
    if (act == kAskFirst) { s->asked = 1; s->worldGen = worldGen; s->linkGen = linkGen; s->nextKey.clear(); ++s->epoch; }
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

}   /* namespace coopfeed */

#endif
