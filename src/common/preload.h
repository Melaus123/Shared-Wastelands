/* src/common/preload.h - T-346 slice 1 (owner decisions 255-256, 2026-09-30): THE RECORDS BEFORE THE LOAD (world-server
 * protocol 67). Pure: the world server (src/coop-store/store_main.cpp), the plugin (src/coop-plugin/store.cpp) and the offline
 * suite (src/coop-test) compile this same header.
 *
 * WHY IT EXISTS. Since T-313 a game is sent records only once its world is running (recordfeed.h), so a returning game's load
 * swaps (squad files in detour_loadFromDisk, area files in detour_sfsReadFile) used this game's record table as it last heard
 * it - anything another player changed while this one was away was not in the world as first built. Owner decision 255: this
 * game's copy is only a cache, brought up to date BEFORE the world loads; decision 256 (a): the loading screen may wait.
 *
 * THE EXCHANGE. Four more kinds inside RECORD_FEED (58), after recordfeed.h's 1-4, on reliable channel 0:
 *   up   5 INDEX_ASK  {u32 token, str afterKey}       the next page of the world server's record INDEX after that key ("" first)
 *   down 6 INDEX_PAGE {u32 token, u8 last, u32 n, n * (str key, u64 fileStamp, u64 posAt)}   keys ascending; fileStamp = the
 *                                                     record's file time (0 = a position-only record), posAt = its position's
 *   up   7 FETCH      {u32 token, u8 resume, u32 n, n * str key}   send these records now (n <= kPreFetchBatch); resume = 1:
 *                                                     the rest of a batch the last FETCH_END cut short (the world server writes
 *                                                     its queue out only on a batch's first FETCH, as the feed on its first page)
 *   down 8 FETCH_END  {u32 token, u32 answered, u32 sent} after the RECORDs (28) that answer that FETCH - the same RECORDs the
 *                                                     feed sends (SendRecordTo), so ordered before this on the one channel.
 *                                                     answered = how many of the FETCH's keys, from the first, this answer
 *                                                     covers: the world server stops once about coopfeed::kFeedPageBytes went out
 *                                                     (always at least one key); the game settles only those and asks again for
 *                                                     the rest (T-346 fold 1, review 2026-09-30 MED: no byte limit before)
 * The game asks at the title, with its load posted and held in the save-request pump (store.cpp SaveRequestPumpBody, under the
 * loading screen SaveManager::load turned on), takes every record the index names NEWER than its own table (PreWant), writes the
 * fetched records into its mirror only (no engine memory - there is no world), and lets the load go when every batch is
 * answered, or the link fails, or kPreBoundMs passes (a counted STALE LOAD). A world server one protocol behind would count the
 * new kinds as malformed and never answer, so the pair SHIPS TOGETHER (protocol 66 -> 67).
 *
 * WIRE: u8 kind, three zero bytes, then the kind's fields, little-endian (recordfeed.h's helpers); a string is u32 length + bytes
 * (<= coopfeed::kFeedKeyMax). Nothing may follow the last field. */
#ifndef COOP_COMMON_PRELOAD_H
#define COOP_COMMON_PRELOAD_H

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "recordfeed.h"

namespace cooppre {

const unsigned int kPreProtocol = 69;   /* the world-server protocol that carries kinds 5-8 (checked against coopfeed::kFeedProtocol) */
enum { kPreIndexAsk = 5, kPreIndexPage = 6, kPreFetch = 7, kPreFetchEnd = 8 };
const size_t kPreIndexPageEntries = 4096;          /* at most this many index rows a page */
const size_t kPreIndexPageBytes = 512u * 1024u;    /* and about this many bytes */
const size_t kPreFetchBatch = 256;                 /* keys per FETCH */
const unsigned int kPreBoundMs = 60000;            /* the longest the load is held (decision 256 a); then a counted stale load */

struct IndexEntry
{
    std::string key;
    unsigned long long fileStamp, posAt;
    IndexEntry() : fileStamp(0ULL), posAt(0ULL) {}
    IndexEntry(const std::string& k, unsigned long long f, unsigned long long p) : key(k), fileStamp(f), posAt(p) {}
};
struct PreMsg
{
    unsigned int kind, token, last, resume, answered, sent;   /* resume: FETCH; answered, sent: FETCH_END */
    std::string key;                    /* INDEX_ASK: afterKey */
    std::vector<IndexEntry> entries;    /* INDEX_PAGE */
    std::vector<std::string> keys;      /* FETCH */
    PreMsg() : kind(0), token(0), last(0), resume(0), answered(0), sent(0) {}
};

/* one of this header's kinds (the caller then decodes it); recordfeed.h's FeedDecode refuses them */
inline bool IsPreKind(const char* p, size_t n)
{
    if (p == 0 || n < coopfeed::kFeedHeader || p[1] != 0 || p[2] != 0 || p[3] != 0) return false;
    const unsigned int k = (unsigned int)(unsigned char)p[0];
    return k >= (unsigned int)kPreIndexAsk && k <= (unsigned int)kPreFetchEnd;
}

inline bool PreEncodeIndexAsk(std::vector<char>* b, unsigned int token, const std::string& afterKey)
{
    if (afterKey.size() > coopfeed::kFeedKeyMax) return false;
    coopfeed::FeedHead(b, kPreIndexAsk); coopfeed::FeedPut32(b, token); coopfeed::FeedPutKey(b, afterKey);
    return true;
}
/* entries with a key over the bound are not encodable - the whole page is refused (appends nothing) */
inline bool PreEncodeIndexPage(std::vector<char>* b, unsigned int token, bool last, const std::vector<IndexEntry>& es)
{
    for (size_t i = 0; i < es.size(); ++i) if (es[i].key.size() > coopfeed::kFeedKeyMax) return false;
    coopfeed::FeedHead(b, kPreIndexPage); coopfeed::FeedPut32(b, token); b->push_back(last ? (char)1 : (char)0);
    coopfeed::FeedPut32(b, (unsigned int)es.size());
    for (size_t i = 0; i < es.size(); ++i) { coopfeed::FeedPutKey(b, es[i].key); coopfeed::FeedPut64(b, es[i].fileStamp); coopfeed::FeedPut64(b, es[i].posAt); }
    return true;
}
inline bool PreEncodeFetch(std::vector<char>* b, unsigned int token, bool resume, const std::vector<std::string>& keys)
{
    if (keys.size() > kPreFetchBatch) return false;
    for (size_t i = 0; i < keys.size(); ++i) if (keys[i].size() > coopfeed::kFeedKeyMax) return false;
    coopfeed::FeedHead(b, kPreFetch); coopfeed::FeedPut32(b, token); b->push_back(resume ? (char)1 : (char)0); coopfeed::FeedPut32(b, (unsigned int)keys.size());
    for (size_t i = 0; i < keys.size(); ++i) coopfeed::FeedPutKey(b, keys[i]);
    return true;
}
inline void PreEncodeFetchEnd(std::vector<char>* b, unsigned int token, unsigned int answered, unsigned int sent)
{
    coopfeed::FeedHead(b, kPreFetchEnd); coopfeed::FeedPut32(b, token); coopfeed::FeedPut32(b, answered); coopfeed::FeedPut32(b, sent);
}
inline bool PreGetKey(const char* p, size_t n, size_t* at, std::string* out)
{
    if (n - *at < 4) return false;
    const unsigned int len = coopfeed::FeedGet32(p + *at); *at += 4;
    if (len > coopfeed::kFeedKeyMax || n - *at < (size_t)len) return false;
    out->assign(p + *at, (size_t)len); *at += len;
    return true;
}
inline bool PreDecode(const char* p, size_t n, PreMsg* out)
{
    if (!IsPreKind(p, n)) return false;
    PreMsg m; m.kind = (unsigned int)(unsigned char)p[0];
    size_t at = coopfeed::kFeedHeader;
    if (n - at < 4) return false;
    m.token = coopfeed::FeedGet32(p + at); at += 4;
    if (m.kind == (unsigned int)kPreIndexAsk) { if (!PreGetKey(p, n, &at, &m.key)) return false; }
    else if (m.kind == (unsigned int)kPreIndexPage)
    {
        if (n - at < 5) return false;
        m.last = (unsigned int)(unsigned char)p[at]; at += 1;
        if (m.last > 1) return false;
        const unsigned int cnt = coopfeed::FeedGet32(p + at); at += 4;
        if (cnt > kPreIndexPageEntries) return false;
        for (unsigned int i = 0; i < cnt; ++i)
        {
            IndexEntry e;
            if (!PreGetKey(p, n, &at, &e.key) || n - at < 16) return false;
            e.fileStamp = coopfeed::FeedGet64(p + at); e.posAt = coopfeed::FeedGet64(p + at + 8); at += 16;
            m.entries.push_back(e);
        }
    }
    else if (m.kind == (unsigned int)kPreFetch)
    {
        if (n - at < 5) return false;
        m.resume = (unsigned int)(unsigned char)p[at]; at += 1;
        if (m.resume > 1) return false;
        const unsigned int cnt = coopfeed::FeedGet32(p + at); at += 4;
        if (cnt > kPreFetchBatch) return false;
        for (unsigned int i = 0; i < cnt; ++i) { std::string k; if (!PreGetKey(p, n, &at, &k)) return false; m.keys.push_back(k); }
    }
    else
    {
        if (n - at < 8) return false;
        m.answered = coopfeed::FeedGet32(p + at); m.sent = coopfeed::FeedGet32(p + at + 4); at += 8;
    }
    if (at != n) return false;
    *out = m;
    return true;
}

/* THE INDEX PAGE (world server): rows strictly after afterKey ("" = from the first), in key order, at most maxEntries and about
   maxBytes (always at least one row while any is left). stamp(record, &fileStamp, &posAt) reads one record's two times. Returns
   whether this page reached the end; *lastKey = the last key put (afterKey when none was). */
template <class V, class Stamp>
inline bool PreIndexWalk(const std::map<std::string, V>& m, const std::string& afterKey, size_t maxEntries, size_t maxBytes, Stamp& stamp,
                         std::vector<IndexEntry>* out, std::string* lastKey)
{
    typename std::map<std::string, V>::const_iterator it = afterKey.empty() ? m.begin() : m.upper_bound(afterKey);
    size_t bytes = 0; std::string last = afterKey;
    for (; it != m.end(); ++it)
    {
        if (!out->empty() && (out->size() >= maxEntries || bytes >= maxBytes)) break;
        if (it->first.size() > coopfeed::kFeedKeyMax) continue;   /* not encodable - never in the index (the feed answers it the same way) */
        IndexEntry e; e.key = it->first; stamp(it->second, &e.fileStamp, &e.posAt);
        bytes += 4 + e.key.size() + 16;
        out->push_back(e); last = it->first;
    }
    *lastKey = last;
    return it == m.end();
}

/* THE FETCH ANSWER (world server; T-346 fold 1): the asked keys in order until about maxBytes have gone out - always at least one
   key while any was asked, so every answer moves the batch on. A key not in m is answered by its absence (deleted since the index;
   its RECORD_GONE reaches every game). snd(record) puts one record on the wire and returns its bytes. Returns how many keys, from
   the first, this answer covers (FETCH_END's answered); *sent = records put, *missing = keys not held. */
template <class V, class Send>
inline unsigned int PreFetchWalk(const std::map<std::string, V>& m, const std::vector<std::string>& keys, size_t maxBytes, Send& snd,
                                 unsigned int* sent, unsigned int* missing)
{
    size_t bytes = 0; size_t i = 0; *sent = 0; *missing = 0;
    for (; i < keys.size(); ++i)
    {
        if (i > 0 && bytes >= maxBytes) break;
        typename std::map<std::string, V>::const_iterator it = m.find(keys[i]);
        if (it == m.end()) { *missing += 1; continue; }
        bytes += snd(it->second); *sent += 1;
    }
    return (unsigned int)i;
}

/* WHICH RECORDS TO FETCH (game): one index row against this game's own table entry. The same "newer" the apply uses
   (store.cpp ApplyRemoteRecord: a full record wins only with a LATER file time, a position only with a later position time), so a
   fetched record is never one the apply would refuse as not newer. Not fetched: a group this game holds deleted (the apply
   refuses it), an area record while this game's area records are off, and - for an area - a position time alone (area records
   carry no position). */
inline int PreWant(int haveLocal, long long localFile, long long localPos, unsigned long long srvFile, unsigned long long srvPos,
                   int deletedHere, int isZone, int zoneOn)
{
    if (deletedHere) return 0;
    if (isZone && !zoneOn) return 0;
    if (!haveLocal) return (srvFile > 0ULL || (!isZone && srvPos > 0ULL)) ? 1 : 0;
    if (srvFile > 0ULL && (long long)srvFile > localFile) return 1;
    if (!isZone && srvPos > 0ULL && (long long)srvPos > localPos) return 1;
    return 0;
}

/* THE STATE (game), one per held load. */
enum { kPhIdle = 0, kPhIndex = 1, kPhFetch = 2, kPhDone = 3, kPhReleased = 4 };
struct PreState
{
    int phase;
    unsigned int token;
    long linkGen;
    std::string afterKey;              /* the last index key seen */
    std::vector<std::string> want;     /* newer on the world server, in index order */
    size_t batchBegin, batchEnd;       /* the FETCH out: want[batchBegin, batchEnd) */
    std::set<std::string> failed;      /* fetched but not written (stays stale) */
    std::set<std::string> settled;     /* answered: written, not newer after all, or not on the world server any more */
    std::map<std::string, unsigned long long> wantFile;   /* a wanted key's file time in the index (only when it named one) */
    std::set<std::string> posOnly;     /* answered with its position only although the index named a newer file (unreadable there) */
    std::set<std::string> reasked;     /* asked once more for that reason - never twice */
    int resume;                        /* the FETCH out is the rest of a batch an answer cut short */
    unsigned long long pages, rows, cuts;
    PreState() : phase(kPhIdle), token(0), linkGen(0), batchBegin(0), batchEnd(0), resume(0), pages(0ULL), rows(0ULL), cuts(0ULL) {}
};
inline void PreBegin(PreState* s, unsigned int token, long linkGen)
{
    s->phase = kPhIndex; s->token = token; s->linkGen = linkGen; s->afterKey.clear(); s->want.clear();
    s->batchBegin = 0; s->batchEnd = 0; s->failed.clear(); s->settled.clear(); s->pages = 0ULL; s->rows = 0ULL;
    s->wantFile.clear(); s->posOnly.clear(); s->reasked.clear(); s->resume = 0; s->cuts = 0ULL;
}
/* the next FETCH's keys; false (and phase Done) when every wanted key has been asked and answered */
inline bool PreNextBatch(PreState* s, std::vector<std::string>* keys)
{
    keys->clear();
    s->batchBegin = s->batchEnd;
    if (s->batchBegin >= s->want.size()) { s->phase = kPhDone; return false; }
    s->batchEnd = s->batchBegin + kPreFetchBatch < s->want.size() ? s->batchBegin + kPreFetchBatch : s->want.size();
    for (size_t i = s->batchBegin; i < s->batchEnd; ++i) keys->push_back(s->want[i]);
    s->phase = kPhFetch; s->resume = 0;
    return true;
}
enum { kActNone = 0, kActAskIndex = 1, kActFetch = 2, kActDone = 3 };
/* an INDEX_PAGE arrived: kActNone when it is not this held load's (token, phase); else the rows are judged by want(entry) (1 =
   fetch) and the next step named: another INDEX_ASK after s->afterKey, a FETCH of *keys, or Done (nothing newer). */
template <class Want>
inline int PreOnIndexPage(PreState* s, const PreMsg& m, Want& want, std::vector<std::string>* keys)
{
    if (s->phase != kPhIndex || m.kind != (unsigned int)kPreIndexPage || m.token != s->token) return kActNone;
    ++s->pages;
    for (size_t i = 0; i < m.entries.size(); ++i)
    {
        ++s->rows;
        if (!s->afterKey.empty() && m.entries[i].key <= s->afterKey) continue;   /* never twice, never backwards */
        s->afterKey = m.entries[i].key;
        if (want(m.entries[i])) { s->want.push_back(m.entries[i].key); if (m.entries[i].fileStamp > 0ULL) s->wantFile[m.entries[i].key] = m.entries[i].fileStamp; }
    }
    if (m.last == 0 && !m.entries.empty()) return kActAskIndex;
    if (m.last == 0) { s->phase = kPhDone; return kActDone; }   /* "more" with no rows would ask forever - the index ends here */
    s->batchEnd = 0;
    return PreNextBatch(s, keys) ? kActFetch : kActDone;
}
/* a FETCH_END arrived (T-346 fold 1): the first m.answered keys of the FETCH out are answered (their RECORDs came first on the
   one channel) - settled unless their write failed; a key answered with its position only although the index named a newer file
   is asked ONCE more (appended to want). The rest of a batch the answer did not cover is asked again (resume); an answer that
   covers none ends the fetch (it would be asked forever - those keys stay not current). Then the next batch, or Done. */
inline int PreOnFetchEnd(PreState* s, const PreMsg& m, std::vector<std::string>* keys)
{
    if (s->phase != kPhFetch || m.kind != (unsigned int)kPreFetchEnd || m.token != s->token) return kActNone;
    const size_t out = s->batchEnd - s->batchBegin;
    const size_t upTo = s->batchBegin + ((size_t)m.answered < out ? (size_t)m.answered : out);
    for (size_t i = s->batchBegin; i < upTo && i < s->want.size(); ++i)
    {
        const std::string k = s->want[i];   /* a copy: the re-ask below appends to want */
        if (s->failed.find(k) == s->failed.end()) { s->settled.insert(k); continue; }
        if (s->posOnly.find(k) != s->posOnly.end() && s->reasked.insert(k).second) { s->failed.erase(k); s->posOnly.erase(k); s->want.push_back(k); }
    }
    if (upTo == s->batchBegin && out > 0) { keys->clear(); s->phase = kPhDone; return kActDone; }
    if (upTo < s->batchEnd)
    {
        s->batchBegin = upTo; s->resume = 1; ++s->cuts; keys->clear();
        for (size_t i = s->batchBegin; i < s->batchEnd; ++i) keys->push_back(s->want[i]);
        return kActFetch;
    }
    return PreNextBatch(s, keys) ? kActFetch : kActDone;
}
/* a RECORD with no payload (its position only) for a key whose index row named a file NEWER than this game's own: the world
   server could not read that file (locked / unreadable - SendRecordTo then sends the position alone), so the key is NOT current */
inline bool PrePositionOnlyShort(const PreState& s, const std::string& key, long long localFile)
{
    std::map<std::string, unsigned long long>::const_iterator it = s.wantFile.find(key);
    return it != s.wantFile.end() && (long long)it->second > localFile;
}
/* the keys this load would build from a copy that is NOT current: wanted and not settled */
inline void PreStaleKeys(const PreState& s, std::set<std::string>* out)
{
    out->clear();
    for (size_t i = 0; i < s.want.size(); ++i) if (s.settled.find(s.want[i]) == s.settled.end()) out->insert(s.want[i]);
}

/* THE WAIT'S EXIT RULE (game, every pump frame while the load is held) - a recurring check on the state, never a fixed delay:
   Go once every batch is answered (Done); StaleLink once the link this exchange began on is down or replaced; StaleTimeout at
   the bound; otherwise Wait. A state that is not in flight (Idle / Released) never holds the load. */
enum { kPreWait = 0, kPreGo = 1, kPreStaleLink = 2, kPreStaleTimeout = 3 };
inline int PreWaitDecide(int phase, int linkOk, unsigned int elapsedMs, unsigned int boundMs)
{
    if (phase == kPhDone) return kPreGo;
    if (phase != kPhIndex && phase != kPhFetch) return kPreGo;
    if (!linkOk) return kPreStaleLink;
    if (elapsedMs >= boundMs) return kPreStaleTimeout;
    return kPreWait;
}

}   /* namespace cooppre */

#endif
