/* M16 (T-197, more than two players) - A BOUND ON WHAT WAITS TO BE SENT, AS PURE BOOKKEEPING.
 *
 * Before M16 every message a game or the world server sent went straight into the network layer's (ENet's) queue for
 * that link, and that queue has no limit: a link that drains slower than it is fed (a slow or stalled player, a world
 * load that stops a game servicing its socket, 100 players' movement relayed N x N) grew memory without end, silently.
 *
 * THE RULE (project rule: no fixed number may break the game - grow or fail VISIBLY):
 *   - The network queue of one link is FULL at kNetCountMax packets or kNetBytesMax bytes. Below that, and while nothing
 *     waits here, a message goes straight to the network exactly as before (Direct).
 *   - An UNRELIABLE message (channel other than kChannelReliable: a MOVE or STATE sent without the guarantee) promises no
 *     order, and the network layer itself sends it past reliable packets that are held up. So it goes straight to the
 *     network PAST the waiting line whenever the network queue has room - a large reliable message (a 2.6 MB zone record)
 *     waiting here no longer freezes other players' characters. It waits only while the network queue itself is full, or
 *     when a reliable value for the same subject waits (it then merges into that one, below). A newer unreliable value
 *     that goes straight out removes an older one of the same subject still waiting (it would arrive after it).
 *   - Otherwise it WAITS here (Put) and goes out as the network queue drains (Take): waiting unreliable messages first
 *     (they promise no order), then the reliable line, oldest first.
 *   - LATEST-VALUE classes - MOVE (12) and STATE (14), each a whole snapshot of ONE character (uid = first u32) - keep
 *     only the NEWEST waiting value per subject: a newer one removes the older from the queue and goes to the back
 *     (Put answers true; counted "coalesced"). On the world server's road the subject is {inner type, route, target,
 *     uid} going up and {inner type, origin slot, uid} coming down, so two targets or two senders are never merged.
 *     A newer value that replaces a RELIABLE one goes reliable itself (M16 fold 3: a sector crossing is never lost).
 *   - Every other message (items, money, records, requests, answers...) is MUST-DELIVER: never dropped, never merged,
 *     never reordered. The waiting list grows instead - that is the back-pressure - and the caller logs the link as
 *     CONGESTED on the edge (Edge) and again at every doubling from kWaitLogFirst waiting messages (GrowthMark).
 *   - A link that goes away takes its waiting messages with it (Clear, counted) - exactly what the network layer does
 *     with its own queue on a reset.
 * The offline suite drives all of it (coop-test, m16_*). No Windows, no ENet, no threads; the caller owns the lock.
 */
#ifndef COOP_SENDBOUND_H
#define COOP_SENDBOUND_H

#include <cstring>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "livefirst.h"   /* T-1118 (b): the live-first mode - CHUNK, the lines, the barriers, the budget */
#include "areafresh.h"   /* T-1279 (T2290 review MEDIUM): AnswerGate - an area's key */
#include "zonerec.h"     /* T-1279 (T2290 review MEDIUM): AnswerGate - a zone record's id */
#include "areadrop.h"    /* T-1320 (b): an AREAMAP's layout - the areas whose holder a changed map changes (MapHolderChange) */
#include <algorithm>     /* T-1320 (b): std::binary_search over a barrier's area list */

namespace coopsb {

enum { kClassMust = 0, kClassLatest = 1 };
enum { kDirUp = 0, kDirDown = 1, kDirNoLive = 2 };   /* a game's LIVE to the world server / the world server's LIVE to a game / M16 fold 2: a link with no LIVE envelope (the session link, where type 53 is MSG_PARITY_REQ) */
enum { kChannelReliable = 0 };   /* M16 fold 3: Item.channel 0 = reliable on both ends (net::CH_RELIABLE; the world server's kChReliable) */

const unsigned int kTypeMove = 12, kTypeState = 14, kTypeLive = 53;   /* net::MSG_MOVE / MSG_STATE; the store link's LIVE */
const size_t kLiveUpHeader = 12, kLiveDownHeader = 8;                 /* = cooplive::kLiveUpHeader / kLiveDownHeader (m16 test) */

const long long kNetCountMax = 1024;              /* packets in one link's network queue */
const long long kNetBytesMax = 1024LL * 1024;     /* bytes in one link's network queue */
const long long kWaitLogFirst = 1024;             /* waiting messages at the first growth line; then every doubling */

struct Key
{
    unsigned long long a, b, c;   /* type (and inner type) / route+target or origin / uid */
    Key() : a(0), b(0), c(0) {}
};
inline bool operator<(const Key& x, const Key& y)
{
    if (x.a != y.a) return x.a < y.a;
    if (x.b != y.b) return x.b < y.b;
    return x.c < y.c;
}

inline unsigned int SbU32(const char* p) { unsigned int v = 0; std::memcpy(&v, p, 4); return v; }

/* The class of one message: `type` and its PAYLOAD (after the 5-byte frame header). Latest-value only for a MOVE or a
   STATE whose uid can be read; a LIVE envelope is read by its direction, and only a well-formed one carrying a MOVE or a
   STATE is latest-value. Anything unreadable is MUST-DELIVER - the safe side. */
inline int ClassOf(unsigned int type, const char* p, size_t n, int dir, Key* key)
{
    *key = Key();
    if (p == 0) return kClassMust;
    if (type == kTypeMove || type == kTypeState)
    {
        if (n < 4) return kClassMust;
        key->a = type; key->c = SbU32(p);
        return kClassLatest;
    }
    if (type != kTypeLive || (dir != kDirUp && dir != kDirDown)) return kClassMust;   /* M16 fold 2: 53 is a LIVE envelope only on the world-server link */
    unsigned int inner = 0, len = 0; size_t at = 0; unsigned long long who = 0;
    if (dir == kDirUp)
    {
        if (n < kLiveUpHeader) return kClassMust;
        inner = (unsigned char)p[1]; len = SbU32(p + 8); at = kLiveUpHeader;
        who = ((unsigned long long)(unsigned char)p[0] << 32) | SbU32(p + 4);   /* route | target */
    }
    else
    {
        if (n < kLiveDownHeader) return kClassMust;
        unsigned short slot = 0; std::memcpy(&slot, p, 2);
        inner = (unsigned char)p[2]; len = SbU32(p + 4); at = kLiveDownHeader;
        who = slot;                                                             /* origin slot */
    }
    if ((inner != kTypeMove && inner != kTypeState) || len < 4 || (size_t)len != n - at) return kClassMust;
    key->a = ((unsigned long long)kTypeLive << 32) | inner; key->b = who; key->c = SbU32(p + at);
    return kClassLatest;
}

struct Item
{
    int cls; Key key; unsigned int type; int channel; std::vector<char> bytes;   /* bytes = the whole frame as sent */
    /* T-1118 (b), live-first mode only (SetLiveFirst): the caller sets line (livefirst::kLineLive / kLineRest), barrier
       (livefirst::IsBarrier) and len (the whole frame's bytes, read by Direct before `bytes` is filled). A frame taken as a CHUNK
       comes out with type livefirst::kMsgChunk. */
    int line; bool barrier; size_t len;
    // PROBE-START: P199 - when a live frame was handed to the transport (0 = not stamped); read only by the probe's log lines
    double p199At;
    // PROBE-END: P199
    /* the per-character gate (livefirst::SubjectsOf, set by the caller with line): the characters a LIVE envelope's message names;
       bySender = it speaks for every character of its sender instead; origin = that sender (the origin slot toward a game, 0 up) */
    std::vector<unsigned int> subjects; bool bySender; unsigned int origin;
    /* T-1118 (c): the kind its bytes are measured under with its sender (livefirst::KindOf, set by the caller with line); 0 = not
       measured (it never leaves its line for the BULK line) */
    unsigned int kind;
    /* review MED: its characters are a list to compare (livefirst::ListOnly, a ROSTER) - on the BULK line it holds no live frame */
    bool listOnly;
    /* T-1320 B (the world server's links; RecordFrameHead, set by the caller): a RECORD frame's id (empty = not a record), its own
       stamp, whether it carries the record's file (full; a position-only copy carries none), and whether it is a FORWARDED copy (the
       record stream - only such a copy is ever replaced, by a newer full copy of the same id: Outbox::Put) */
    std::string recId; long long recStamp; bool recFull; bool recForward;
    /* T-1320 (b), a barrier only (set by the caller): the areas (areafresh::Key, sorted) whose holder a changed AREAMAP changes against the
       last map put on its link (MapHolderChange), or barAll = every area - the default, so a WELCOME, a PLAYER_GONE, the bridge's maps and
       an unreadable map hold every answer as before. An ANSWER-BULK RECORD of an area waits only for a barrier holding it. T-1320 fix 3: a
       changed AREAMAP on the LIVE line (livefirst.h kMapChangedLive) holds the same set, and waits itself for every ANSWER-BULK RECORD of a
       held area put before it (Outbox::MapWaitsFor). */
    bool barAll; std::vector<int> barAreas;
    /* T-1320 fix 3 fold 1 (set by the caller from livefirst.h MapRepeat::LastFirst): an AREAMAP that is its link's first since the last
       Reset / Forget - while it waits on the REST line every later map stays there too (Outbox::FirstMapWaiting), and it is never removed */
    bool mapFirst;
    Item() : cls(kClassMust), type(0), channel(0), line(livefirst::kLineRest), barrier(false), len(0), p199At(0.0), bySender(false), origin(0), kind(0), listOnly(false),
             recStamp(0), recFull(false), recForward(false), barAll(true), mapFirst(false) {}
};

/* T-1320 (b): the area key (areafresh::Key) of a RECORD frame's head (frame header included; its first field is the record's id: u32
   length, then the id, zone.<X>.<Y> for an area): -1 for another type, an id that is not an area's, or - whole - one that cannot be
   read; -2 for a first piece too short to hold the id (whole = false). AnswerGate::AreaOfRecordHead answers this. */
inline int AreaKeyOfRecordHead(const char* f, size_t n, bool whole)
{
    if (f == 0 || n < 1 || (unsigned char)f[0] != (unsigned char)28) return -1;   /* 28 = RECORD (AnswerGate::kTypeRecord) */
    if (n < 9) return whole ? -1 : -2;
    unsigned int len = 0; std::memcpy(&len, f + 5, 4);
    if (len < 5 || len > 64) return -1;
    if ((size_t)len > n - 9) return whole ? -1 : -2;
    int x = -1, y = -1;
    if (zonerec::ZoneRecIdParse(std::string(f + 9, len), &x, &y) == 0) return -1;
    const int k = areafresh::Key(x, y);
    return k >= 0 ? k : -1;
}
/* T-1320 (b): the area of a frame on the ANSWER-BULK line (whole, header included): a RECORD's (*record = true) or an AREA_FRESH's (FRESH
   or CANT; *record = false); -1 when it cannot be read (such a frame is held by every barrier put before it and passes no frame). */
inline int AnswerAreaOf(const char* f, size_t n, bool* record)
{
    *record = false;
    if (f == 0 || n < livefirst::kFrameHeader) return -1;
    if ((unsigned char)f[0] == (unsigned char)28) { *record = true; return AreaKeyOfRecordHead(f, n, true); }
    if ((unsigned char)f[0] != (unsigned char)livefirst::kTypeAreaFresh) return -1;
    areafresh::Msg m;
    if (!areafresh::Decode(f + livefirst::kFrameHeader, n - livefirst::kFrameHeader, &m)) return -1;
    return areafresh::Key(m.x, m.y);
}
/* T-1320 fix 3 (owner 1607 a) - ONE QUEUED COPY OF AN AREA ANSWER PER LINK: a full answer RECORD (not a forward) put on the ANSWER-BULK
   line while an answer copy of the same record id waits there NOT STARTED: a newer one (its stamp above the queued copy's) replaces that
   copy in place (its queue position and put order kept, as forwards replace each other); the same or an older one adds nothing (the
   game re-asks after ~10 s without an answer: T2314 queued 24.31's 108,843-byte record 12-17 times). No unsent copy waiting (none, or
   the one waiting is partly sent - it finishes): appended as before. */
enum { kAnsDupAppend = 0, kAnsDupReplace = 1, kAnsDupSkip = 2 };
inline int AnswerDupRule(bool queuedUnsent, long long queuedStamp, long long newStamp)
{
    if (!queuedUnsent) return kAnsDupAppend;
    return newStamp > queuedStamp ? kAnsDupReplace : kAnsDupSkip;
}
/* T-1320 (b): the areas whose HOLDER (a row's owner column; an area with no row is held by nobody) differs between the last AREAMAP put on
   a link (`prev`, a whole frame; 0 = none: an empty map, every area held by nobody) and this one (whole, header included), sorted into
   *areas. Answers true = EVERY area only when either map is unreadable or a row is off the 64 x 64 grid - the safe side. T-1320 fix 3 (owner
   1605 a): a changed seat table is no longer every area - the game judges a zone record against its area's holder alone (store.cpp
   AreaHolderSlotTS / ZoneRecvJudge); the seat table never enters that judgement. */
inline bool MapHolderChange(const std::vector<char>* prev, const char* frame, size_t n, std::vector<int>* areas)
{
    areas->clear();
    if ((prev != 0 && prev->size() < livefirst::kFrameHeader) || frame == 0 || n < livefirst::kFrameHeader) return true;
    const unsigned char* q[2] = { prev != 0 ? (const unsigned char*)&(*prev)[0] + livefirst::kFrameHeader : 0, (const unsigned char*)frame + livefirst::kFrameHeader };
    const size_t qn[2] = { prev != 0 ? prev->size() - livefirst::kFrameHeader : 0, n - livefirst::kFrameHeader };
    std::vector<int> owner[2];
    for (int m = 0; m < 2; ++m)
    {
        owner[m].assign(64 * 64, -1);
        if (q[m] == 0) continue;   /* no earlier map: every area held by nobody */
        int listed[coopdrop::kAreaSeats]; int seats = 0, rows = 0; size_t at = 0;
        if (!coopdrop::AreaMapLayout(q[m], qn[m], listed, &seats, &at, &rows)) return true;
        for (int i = 0; i < rows; ++i)
        {
            int x = 0, y = 0, o = -1; unsigned mask[coopdrop::kAreaMaskWords];
            if (!coopdrop::AreaMapNextRow(q[m], qn[m], &at, &x, &y, &o, mask)) return true;
            const int k = areafresh::Key(x, y);
            if (k < 0) return true;
            owner[m][k] = o;
        }
    }
    for (int k = 0; k < 64 * 64; ++k) if (owner[0][k] != owner[1][k]) areas->push_back(k);
    return false;
}

/* T-1320 B: a RECORD frame's head (the whole frame, header included: type u8 | length u32 | the record as the world server's
   EncodeRecord writes it - id, squad, faction (each u32 length + bytes), position 3 x f32, stamp u32 low + u32 high, owner u32, file
   length u32, the file ...). True with *id, *stamp and *full (the file length > 0) for a well-formed RECORD head; false for another
   type or a head that cannot be read (the frame is then never replaced and never replaces). */
const unsigned int kTypeRecordFrame = 28;   /* = store_main.cpp MSG_RECORD (store_main.cpp checks) */
inline bool RecordFrameHead(const char* f, size_t n, std::string* id, long long* stamp, bool* full)
{
    if (f == 0 || n < 5 || (unsigned char)f[0] != (unsigned char)kTypeRecordFrame) return false;
    size_t at = 5;
    std::string first;
    for (int k = 0; k < 3; ++k)
    {
        if (n - at < 4) return false;
        const unsigned int len = SbU32(f + at); at += 4;
        if ((size_t)len > n - at) return false;
        if (k == 0) first.assign(f + at, len);
        at += len;
    }
    if (first.empty() || n - at < 12 + 8 + 4 + 4) return false;
    at += 12;
    const unsigned long long lo = SbU32(f + at), hi = SbU32(f + at + 4);
    at += 8 + 4;
    const unsigned int fileLen = SbU32(f + at);
    *id = first; *stamp = (long long)((hi << 32) | lo); *full = fileLen > 0;
    return true;
}

/* T-1118 (b) - LIVE-FIRST MODE (SetLiveFirst(true); the world-server link, both ends). Off, the box is exactly M16's. On:
   - three lines: the unreliable list (as before), the LIVE line (guaranteed live frames) and the REST line (every other guaranteed
     frame, in order). Take hands over the unreliable list first, then the LIVE line while ENet's queue is not full, then the REST
     line while what ENet holds is under the BUDGET (SetBudget; livefirst::Rate). A live frame goes straight out (Direct) while no
     live frame and no barrier waits, whatever REST frames wait; a REST frame only while nothing at all waits, ENet holds less than
     the budget and it is not longer than one piece.
   - a REST frame longer than livefirst::kChunkBytes leaves as CHUNK frames of up to that many bytes, cut one at a time as Take
     allows, each carrying at most the room left under the budget when it is cut (livefirst::PieceBytes: at least
     livefirst::kChunkMinBytes; TakeAny's close cuts full pieces); a cut refused for no room waits for the next Take (Count drops
     when its last piece goes, Bytes with each piece). A REST frame never overtakes a live one (a REST frame
     goes straight out only while nothing waits) and REST frames keep their order.
   - a BARRIER (Item.barrier, REST line): no live frame put after it is handed over before it.
   - the ANNOUNCE line (livefirst::kLineAnnounce: a character's CONTEXT, SPAWN, APPEARANCE and CLOTHING): Take hands it over after the LIVE line and before
     the REST line, whole, never cut, when it fits the budget (Fits: what ENet holds plus the frame, or ENet holds nothing). A
     barrier holds it as it holds the LIVE line. An announcement goes straight out only while no live frame, no announcement and no
     barrier waits and it fits the budget.
   - THE PER-CHARACTER GATE: a guaranteed live frame that names a character (Item.subjects, every one the receiver reads) with an
     announcement waiting here, or that speaks for its sender (Item.bySender) while one of that sender's announcements waits, goes
     on the HELD list (counted gateHeld) - so does one sharing a character with a frame already held (each character's frames keep
     their order), and a sender-wide one behind an earlier held sender-wide one of its sender. A frame naming characters never
     waits for a sender-wide one (a ROSTER HASH every 5 s would hold all of its sender's live frames through a burst). As each announcement leaves, every held frame none of whose characters (or sender) has an
     announcement put BEFORE it still waiting, and that shares none with an earlier frame still held, moves to the LIVE line at
     its put-order place (the LIVE line is in put order) - right after its own announcement, not after the whole burst. Barriers
     hold it there as any live frame. Unreliable frames are not gated (they promise no order and pass every line, as before).
   - T-1118 (c) THE BULK LINE (livefirst::kLineBulk; livefirst.h kBulkFrameBytes): a guaranteed frame of the LIVE line whose kind
     (Item.kind, 0 = not measured) and sender (Item.origin) is measured goes on the BULK line instead when the frame is longer than
     kBulkFrameBytes, when that kind from that sender put more than Budget() / kBulkShareDiv bytes here within the last
     kBulkWindowSec (straight out or waiting, this frame included), or when a frame of that kind from that sender still waits on the
     BULK line (sticky: same-kind frames keep their order). Take hands it over after the ANNOUNCE line and before the REST line,
     whole, when it fits the budget (Fits); a barrier holds it as it holds the ANNOUNCE line. It goes straight out only while no
     live, announcement, BULK or held frame and no barrier waits and it fits the budget. A waiting
     BULK frame gates later live frames as an announcement does (the same books, kept apart so the holds can be counted: one naming
     any of its characters, and a sender-wide one when it names characters or speaks for its sender itself). Its line comes out as
     kLineBulk (Take), so the callers do not measure it as a live frame.
   - the stats (livefirst::Stats): pieces cut, frames cut, live frames that passed waiting REST frames, the longest wait of a live
     frame here (SetNow gives the clock), the BULK line's counts.
   - T-1279 (T2290) THE ANSWER LINE (livefirst::kLineAnswer; livefirst.h AreaFreshLine): taken before every other reliable line,
     whole, in put order, with no byte budget (an AREA_FRESH is 25 bytes) and past REST barriers - except while a WELCOME waits on
     the REST line (a game hears nothing of a connection before its WELCOME). The put order of the last REST frame (RestPutSeq) and
     RestGoneTo tell the caller whether a frame it put there has left this queue whole (its last piece handed over, so anything
     handed over after it reaches the game after it) - AnswerGate below. */
class Outbox
{
public:
    Outbox() : m_count(0), m_bytes(0), m_nextMark(kWaitLogFirst), m_was(false), m_liveFirst(false), m_budget(kNetBytesMax), m_now(0.0), m_seq(0), m_lastDirectBulk(false), m_restPutSeq(0),
               m_lastPutSeq(0), m_lastPutLine(-1), m_lastPutReplaced(0), m_lastPutReplacedBytes(0), m_lastPutAnsDup(0),
               m_firstMapSeq(0), m_sectorsSlot(0), m_nextSectorsSlot(0), m_lastMapsRemoved(0), m_lastSectorsRemoved(0), m_lastMapTook(0)
               { std::memset(m_restOf, 0, sizeof(m_restOf)); }
    static bool NetFull(long long netCount, long long netBytes) { return netCount >= kNetCountMax || netBytes >= kNetBytesMax; }
    static bool Reliable(int channel) { return channel == kChannelReliable; }
    /* T-1118 (b): the live-first mode, its budget (bytes ENet may hold before a REST piece waits) and its clock (seconds). */
    void SetLiveFirst(bool on) { m_liveFirst = on; }
    bool LiveFirst() const { return m_liveFirst; }
    void SetBudget(long long bytes) { m_budget = bytes < 1 ? 1 : bytes; }
    long long Budget() const { return m_budget; }
    void SetNow(double now) { m_now = now; }
    /* Straight to the network now? Never while the network queue is full. A reliable message only while nothing waits here
       (it may never overtake one that waits) - in live-first mode, a live one while no live one and no barrier waits, a REST one
       as above. An unreliable one (cls, key and channel of `it` are read; its bytes are not) passes the waiting line, unless a
       reliable value of its subject waits - then it must merge into that one (Put). An older unreliable value of its subject that
       still waits is removed here (*replaced = true): it is out of date and would arrive after this one. */
    bool Direct(long long netCount, long long netBytes, const Item& it, bool* replaced = 0)
    {
        if (replaced) *replaced = false;
        m_lastDirectBulk = false;
        if (NetFull(netCount, netBytes)) return false;
        if (Reliable(it.channel) && !m_liveFirst) return m_count == 0;
        if (Reliable(it.channel) && it.line == livefirst::kLineAnswer)   /* T-1279 (T2290): ahead of every line, never past a waiting WELCOME */
            return m_answer.empty() && m_restOf[livefirst::kTypeWelcome & 0xFF] == 0;
        if (Reliable(it.channel) && it.line == livefirst::kLineAnnounce)
            return m_live.empty() && m_announce.empty() && m_held.empty() && m_barriers.empty() && Fits(netBytes, it)
                   && !AnnBlocked(it, ~0ULL);   /* review HIGH 1: never ahead of a waiting BULK frame naming its character */
        if (Reliable(it.channel) && it.line != livefirst::kLineLive)
        {
            const size_t whole = it.len != 0 ? it.len : it.bytes.size();
            return m_count == 0 && netBytes < m_budget && (!livefirst::CutsInto(whole) || it.type == livefirst::kMsgChunk);   /* a CHUNK handed on (a bridge) is never cut again */
        }
        const int why = BulkWhy(it, Reliable(it.channel));   /* T-1118 (c): 0 unless a measured guaranteed LIVE frame is bulk */
        if (why != livefirst::kBulkNot)
        {
            if (!m_live.empty() || !m_announce.empty() || !m_bulk.empty() || !m_held.empty() || !m_barriers.empty() || !Fits(netBytes, it)) return false;
        }
        else
        {
            if (Reliable(it.channel) && (!m_live.empty() || !m_barriers.empty()))
            {
                if (!m_barriers.empty()) ++m_stats.barrierHeld;
                return false;
            }
            if (Reliable(it.channel) && Gated(it)) return false;   /* behind its character's waiting announcement or BULK frame (Put) */
            if (Reliable(it.channel) && it.line == livefirst::kLineLive && it.type == livefirst::kTypeAreaMap
                && (m_restOf[livefirst::kTypeAreaMap & 0xFF] > 0 || m_restOf[livefirst::kTypePlayerSectors & 0xFF] > 0))
                return false;   /* T-1320 fix 3 fold 1: Put removes the AREAMAP / PLAYERSECTORS it supersedes on the REST line (RestMapsSupersede) */
            if (Reliable(it.channel) && it.line == livefirst::kLineLive && it.barrier && it.type == livefirst::kTypeAreaMap
                && MapWaitsFor(it.barAll, it.barAreas, ~0ULL)) return false;   /* T-1320 fix 3: after the answers of its areas put before it */
        }
        if (it.cls == kClassLatest)
        {
            Index::iterator f = m_latest.find(it.key);
            if (f != m_latest.end())
            {
                if (f->second.list != kListLoose) return false;
                Unlink(f);
                if (replaced) *replaced = true;
            }
        }
        if (why != livefirst::kBulkNot) { BulkWhyCount(why); m_lastDirectBulk = true; }
        else if (m_liveFirst && Reliable(it.channel))
        {
            if (!m_rest.empty()) ++m_stats.livePassed;
            if (netBytes > m_stats.liveAheadMaxBytes) m_stats.liveAheadMaxBytes = netBytes;
        }
        if (m_liveFirst && Reliable(it.channel) && it.line == livefirst::kLineLive) WindowAdd(it);
        return true;
    }
    /* Appends: a reliable message to the back of its ordered line, an unreliable one to the unordered list. True when it
       REPLACED an older waiting value for the same latest-value subject (that one is gone). */
    bool Put(const Item& it)
    {
        bool replaced = false, keepReliable = false;
        if (it.cls == kClassLatest)
        {
            Index::iterator f = m_latest.find(it.key);
            if (f != m_latest.end())
            {
                keepReliable = f->second.list != kListLoose;
                Unlink(f);
                replaced = true;
            }
        }
        const bool loose = !keepReliable && !Reliable(it.channel);   /* a newer unreliable value never makes a waiting reliable one (a sector crossing) unreliable */
        int which = loose ? kListLoose : kListRest, why = livefirst::kBulkNot;
        if (!loose && m_liveFirst && it.line == livefirst::kLineAnnounce) which = kListAnnounce;
        else if (!loose && m_liveFirst && it.line == livefirst::kLineAnswer) which = kListAnswer;   /* T-1279 (T2290) */
        else if (!loose && m_liveFirst && it.line == livefirst::kLineAnswerBulk) which = kListAnsBulk;   /* T-1320 B */
        else if (!loose && m_liveFirst && it.line == livefirst::kLineLive)   /* after any Unlink above */
        {
            why = BulkWhy(it, true);
            which = why != livefirst::kBulkNot ? kListBulk : (Gated(it) ? kListHeld : kListLive);
            WindowAdd(it);
        }
        m_lastPutReplaced = 0; m_lastPutReplacedBytes = 0; m_lastRemoved.clear(); m_lastPutAnsDup = kAnsDupAppend;
        m_lastMapsRemoved = 0; m_lastSectorsRemoved = 0; m_lastMapTook = 0;
        const unsigned long long sectorsSlot = m_sectorsSlot;   /* T-1320 fix 3 fold 1: valid for the very next Put only */
        m_sectorsSlot = 0;
        /* T-1320 fix 3 fold 1 (owner 1607 a, review LOW b): a FORWARDED full RECORD for a game whose answer copy of the same record id waits
           on the ANSWER-BULK line not started - the same record key reached it by both roads (a from-records answer, then the holder's
           write forwarded): one queued copy (AnswerDupRule) - newer: its bytes take the answer copy's place (it stays an answer, not a
           forward; older waiting forwards of the id go as for any newer full copy); the same or older: it adds nothing. */
        if (!loose && m_liveFirst && it.recFull && !it.recId.empty() && it.cls == kClassMust && !it.barrier && it.recForward && which == kListRest)
        {
            std::map<std::string, std::list<Entry>::iterator>::iterator q = m_ansOf.find(it.recId);
            if (q != m_ansOf.end())
            {
                std::list<Entry>::iterator e = q->second;
                const int r = AnswerDupRule(true, e->it.recStamp, it.recStamp);
                m_lastPutAnsDup = r;
                if (r == kAnsDupReplace)
                {
                    std::list<Entry>::iterator unused;
                    RecReplace(it, which, &unused, false);
                    m_bytes += (long long)it.bytes.size() - (long long)e->it.bytes.size();   /* sent == 0: the whole old frame was counted */
                    e->it = it; e->it.recForward = false; e->it.line = livefirst::kLineAnswerBulk;   /* seq, at, sent (0), area and its place kept */
                }
                m_lastPutSeq = e->seq; m_lastPutLine = (int)livefirst::kLineAnswerBulk;
                return replaced;
            }
        }
        /* T-1320 fix 3 fold 1 (H1): an AREAMAP on the LIVE line removes the AREAMAPs / PLAYERSECTORS it supersedes on the REST line
           (RestMapsSupersede) and, when one of them was a barrier, takes the earliest such barrier's place in put order and its holds */
        unsigned long long mapTake = 0; bool remBar = false, remAll = false; std::vector<int> remAreas;
        if (!loose && m_liveFirst && which == kListLive && it.type == livefirst::kTypeAreaMap && Reliable(it.channel))
            RestMapsSupersede(&mapTake, &remBar, &remAll, &remAreas);
        const bool answer = !loose && m_liveFirst && it.recFull && !it.recId.empty() && it.cls == kClassMust && !it.barrier && !it.recForward && which == kListAnsBulk;
        if (answer)
        {   /* T-1320 fix 3 (owner 1607 a): one unsent copy of an area answer per link (AnswerDupRule) */
            std::map<std::string, std::list<Entry>::iterator>::iterator q = m_ansOf.find(it.recId);
            const int r = AnswerDupRule(q != m_ansOf.end(), q != m_ansOf.end() ? q->second->it.recStamp : 0, it.recStamp);
            if (r != kAnsDupAppend)
            {
                std::list<Entry>::iterator e = q->second;
                m_lastPutAnsDup = r;
                if (r == kAnsDupReplace)
                {
                    std::list<Entry>::iterator unused;
                    RecReplace(it, which, &unused, false);   /* older waiting forwards of the record go, as for any newer full copy */
                    m_bytes += (long long)it.bytes.size() - (long long)e->it.bytes.size();   /* sent == 0: the whole old frame was counted */
                    e->it = it;                                                             /* seq, at, sent (0), area and its place kept */
                }
                m_lastPutSeq = e->seq; m_lastPutLine = (int)livefirst::kLineAnswerBulk;
                return replaced;
            }
        }
        if (!loose && m_liveFirst && it.recFull && !it.recId.empty() && it.cls == kClassMust && !it.barrier && (which == kListRest || which == kListAnsBulk))
        {   /* T-1320 B: the newest full copy wins; T-1320 fold 1: on its own line it takes the older copy's place (nothing is appended) */
            std::list<Entry>::iterator in;
            if (RecReplace(it, which, &in, true))
            {
                m_lastPutSeq = in->seq; m_lastPutLine = which == kListRest ? (int)livefirst::kLineRest : (int)livefirst::kLineAnswerBulk;
                if (answer) m_ansOf[it.recId] = in;   /* fix 3: the answer copy now waiting there */
                return replaced;
            }
        }
        std::list<Entry>& q = ListOf(which);
        q.push_back(Entry());
        Entry& e = q.back();
        std::list<Entry>::iterator self = q.end(); --self;
        e.it = it; e.at = m_now; e.sent = 0;
        /* T-1320 fix 3 fold 1: the put order - a map that superseded a REST barrier map takes that map's (mapTake); the PLAYERSECTORS put
           right after such a map on the LIVE line takes the removed PLAYERSECTORS' (sectorsSlot) - each moved to its put-order place on
           the LIVE line (the line is in put order); otherwise the next number */
        const unsigned long long slot = mapTake != 0 ? mapTake
                                      : ((sectorsSlot != 0 && which == kListLive && it.type == livefirst::kTypePlayerSectors) ? sectorsSlot : 0);
        e.seq = slot != 0 ? slot : ++m_seq;
        if (slot != 0)
        {
            std::list<Entry>::iterator to = q.begin();
            while (to != self && to->seq < slot) ++to;
            if (to != self) q.splice(to, q, self);   /* self and e stay valid */
        }
        if (mapTake != 0 || remBar)
        {   /* the removed barrier maps' holds pass to this one (a repeat that superseded a changed map becomes a barrier) */
            e.it.barAll = (it.barrier && it.barAll) || remAll;
            if (!it.barrier) e.it.barAreas.clear();
            e.it.barAreas.insert(e.it.barAreas.end(), remAreas.begin(), remAreas.end());
            std::sort(e.it.barAreas.begin(), e.it.barAreas.end());
            e.it.barAreas.erase(std::unique(e.it.barAreas.begin(), e.it.barAreas.end()), e.it.barAreas.end());
            e.it.barrier = true;
        }
        m_lastMapTook = mapTake;
        m_lastPutSeq = e.seq; m_lastPutLine = which == kListRest ? (int)livefirst::kLineRest : (which == kListAnsBulk ? (int)livefirst::kLineAnswerBulk : -1);
        if ((which == kListRest || which == kListAnsBulk) && it.recForward && !it.recId.empty()) { RecSlot r; r.list = which; r.at = self; m_recOf[it.recId].push_back(r); }
        if (answer) m_ansOf[it.recId] = self;   /* T-1320 fix 3 */
        e.passed = (which == kListLive || which == kListHeld) && !m_rest.empty();
        if (which == kListAnnounce) { AnnNote(e, true); ++m_stats.announceQueued; }
        if (which == kListHeld) { if (GatedByBulk(it)) ++m_stats.bulkHeld; HeldNote(e, 1); ++m_stats.gateHeld; }
        if (which == kListBulk) { e.it.line = livefirst::kLineBulk; BulkNote(e, true); ++m_stats.bulkQueued; BulkWhyCount(why); }
        ++m_count; m_bytes += (long long)it.bytes.size();
        if (keepReliable) e.it.channel = kChannelReliable;
        if (which == kListRest && m_liveFirst && it.barrier)
        {
            m_barriers.push_back(e.seq);
            Bar& bi = m_barInfo[e.seq];   /* T-1320 (b): what it holds on the ANSWER-BULK line (AnsHeldBy) */
            bi.type = it.type; bi.at = m_now; bi.all = it.barAll; bi.areas = it.barAreas;
            std::sort(bi.areas.begin(), bi.areas.end());
        }
        if (which == kListLive && m_liveFirst && e.it.barrier && it.type == livefirst::kTypeAreaMap)
        {   /* T-1320 fix 3: a changed AREAMAP on the LIVE line - what it holds on the ANSWER-BULK line (AnsHeldBy) and waits for (MapWaitsFor);
               fold 1: kept sorted by put order (a map that took a removed map's place is inserted at that place) */
            std::deque<unsigned long long>::iterator at = m_liveMaps.end();
            while (at != m_liveMaps.begin() && *(at - 1) > e.seq) --at;
            m_liveMaps.insert(at, e.seq);
            Bar& bi = m_barInfo[e.seq];
            bi.type = it.type; bi.at = m_now; bi.all = e.it.barAll; bi.areas = e.it.barAreas;
            std::sort(bi.areas.begin(), bi.areas.end());
        }
        if (which == kListRest && it.type == livefirst::kTypeAreaMap && it.mapFirst) m_firstMapSeq = e.seq;   /* T-1320 fix 3 fold 1: FirstMapWaiting */
        if (mapTake != 0) m_sectorsSlot = m_nextSectorsSlot;   /* T-1320 fix 3 fold 1: for the PLAYERSECTORS put next */
        if (which == kListAnsBulk) e.area = AnswerAreaOf(it.bytes.empty() ? 0 : &it.bytes[0], it.bytes.size(), &e.ansRecord);   /* T-1320 (b) */
        if (which == kListRest) { ++m_restOf[it.type & 0xFF]; m_restPutSeq = e.seq; }
        if (it.cls == kClassLatest) { Slot s; s.list = which; s.at = self; m_latest[it.key] = s; }
        return replaced;
    }
    /* The next waiting message (as TakeAny), only while the network queue has room - in live-first mode the line rules above. */
    bool Take(long long netCount, long long netBytes, Item* out)
    {
        if (m_count == 0) return false;
        if (m_liveFirst) return TakeLiveFirst(netCount, netBytes, false, out);
        if (NetFull(netCount, netBytes)) return false;
        return TakeAny(out);
    }
    /* The next waiting message regardless of room (a graceful close hands everything to the network first): the oldest
       unreliable one while any waits (they promise no order), else the oldest of the reliable line (live-first mode: the LIVE
       line unless a barrier holds it, else the REST line, cut). */
    bool TakeAny(Item* out)
    {
        if (m_count == 0) return false;
        if (m_liveFirst) return TakeLiveFirst(0, 0, true, out);
        return TakeWhole(m_loose.empty() ? kListRest : kListLoose, out);
    }
    /* The link is gone: every waiting message is thrown away with it. Returns how many. */
    long long Clear()
    {
        const long long n = m_count;
        m_rest.clear(); m_loose.clear(); m_live.clear(); m_barriers.clear(); m_latest.clear(); m_count = 0; m_bytes = 0; m_nextMark = kWaitLogFirst;
        m_announce.clear(); m_held.clear(); m_annOf.clear(); m_annFrom.clear(); m_heldOf.clear(); m_heldScoped.clear();
        m_bulk.clear(); m_bulkOf.clear(); m_bulkFrom.clear(); m_bulkWait.clear(); m_window.clear(); m_bulkBare.clear(); m_heldBare.clear();
        std::memset(m_restOf, 0, sizeof(m_restOf));
        m_answer.clear();   /* T-1279 (T2290); the REST line is empty, so every RestGoneTo mark is met */
        m_ansBulk.clear(); m_recOf.clear(); m_lastRemoved.clear();   /* T-1320 B */
        m_barInfo.clear(); m_liveMaps.clear(); m_ansOf.clear();   /* T-1320 (b); fix 3 */
        m_firstMapSeq = 0; m_sectorsSlot = 0; m_nextSectorsSlot = 0;   /* T-1320 fix 3 fold 1 */
        return n;
    }
    /* +1 = messages started waiting (the link is congested), -1 = nothing waits any more, 0 = no change since the last call. */
    int Edge()
    {
        const bool w = m_count != 0;
        const int e = (w && !m_was) ? 1 : ((!w && m_was) ? -1 : 0);
        m_was = w;
        return e;
    }
    /* True once each time the waiting count reaches kWaitLogFirst, then twice that, and so on (restarts when empty). */
    bool GrowthMark()
    {
        if (m_count < m_nextMark) return false;
        while (m_nextMark <= m_count) m_nextMark *= 2;
        return true;
    }
    bool Empty() const { return m_count == 0; }
    long long Count() const { return m_count; }
    long long Bytes() const { return m_bytes; }
    /* T-1118 (b): REST frames wait (a record or its pieces) / live frames wait / the stats since the last DrainStats, added into *into. */
    bool RestWaiting() const { return !m_rest.empty(); }
    long long LiveCount() const { return (long long)m_live.size(); }
    long long AnnounceCount() const { return (long long)m_announce.size(); }   /* announcements waiting on the ANNOUNCE line */
    long long HeldCount() const { return (long long)m_held.size(); }           /* live frames held behind an announcement or a BULK frame */
    long long BulkCount() const { return (long long)m_bulk.size(); }           /* T-1118 (c): frames waiting on the BULK line */
    // PROBE-START: P223 - the bytes still to go on each list (walks every list; the caller limits how often). LOG ONLY.
    void P223Bytes(long long* rest, long long* announce, long long* held, long long* live, long long* bulk) const
    {
        const int which[5] = { kListRest, kListAnnounce, kListHeld, kListLive, kListBulk };
        long long* out[5] = { rest, announce, held, live, bulk };
        for (int k = 0; k < 5; ++k)
        {
            const std::list<Entry>& q = const_cast<Outbox*>(this)->ListOf(which[k]);
            long long b = 0;
            for (std::list<Entry>::const_iterator i = q.begin(); i != q.end(); ++i) b += (long long)(i->it.bytes.size() - i->sent);
            if (k == 0) { const std::list<Entry>& l = m_loose; for (std::list<Entry>::const_iterator i = l.begin(); i != l.end(); ++i) b += (long long)(i->it.bytes.size() - i->sent); }
            *out[k] = b;
        }
    }
    // PROBE-END: P223
    /* T-1118 (c): the last Direct that answered true sent a BULK frame (a guaranteed live frame moved off the LIVE line): the caller
       does not time it as a live frame. */
    bool LastDirectBulk() const { return m_lastDirectBulk; }
    /* The gate's own books (announcements and BULK frames per character and per sender, held frames per character, held sender-wide
       frames per sender, BULK frames per kind and sender): 0 once nothing waits on the ANNOUNCE, BULK or HELD list (the offline suite
       checks they never outlive what they count). The byte window (kBulkWindowSec) is not a gate book. */
    long long GateBookSize() const
    {
        return (long long)(m_annOf.size() + m_annFrom.size() + m_heldOf.size() + m_heldScoped.size() + m_bulkOf.size() + m_bulkFrom.size() + m_bulkWait.size()
                           + m_bulkBare.size() + m_heldBare.size());
    }
    /* Frames of one type waiting on the REST line (the reliable line when not live-first), whole or partly cut: counted when put
       there, uncounted when handed over whole, when the last piece of a cut one goes, when a newer value replaces it, and at Clear.
       A link reads it for AREAMAP and PLAYERSECTORS (livefirst.h MapRepeat). */
    long long RestWaitingOf(unsigned int type) const { return m_restOf[type & 0xFF]; }
    /* T-1279 (T2290; review LOW-1: by put order, not by bytes): the put order of the last frame put on the REST line (read right after
       a Put that went there: that frame's mark; 0 = none yet), and whether every REST frame put up to a mark has left this queue -
       handed over (whole, or its last piece), replaced, or thrown away with the link (Clear). The REST line is in put order and a
       must-deliver frame leaves it only from its head; a replaced latest-value frame leaves from the middle, and that meets no
       mark: a mark is met only once the head is past it, so a frame put after the mark that leaves early (replaced) can never make
       a frame up to the mark that still waits count as gone. */
    unsigned long long RestPutSeq() const { return m_restPutSeq; }
    bool RestGoneTo(unsigned long long mark) const { return m_rest.empty() || m_rest.front().seq > mark; }
    long long AnswerCount() const { return (long long)m_answer.size(); }   /* frames waiting on the ANSWER line */
    /* T-1320 B: frames waiting on the ANSWER-BULK line; the put order and line of the last Put (LastPutLine: livefirst::kLineRest,
       kLineAnswerBulk, or -1 for any other line); and whether every frame put on that line up to a mark has left it (LineGoneTo -
       T-1320 (b): that line is taken out of put order (AnsBulkPick), so for it a mark is ONE frame's put order and is met once that frame
       has left - frames of one area still leave in their order, so an area's oldest mark is met first). Another line: true (not followed). LastPutReplaced / LastPutReplacedBytes: the waiting forwarded
       RECORDs the last Put removed (a newer full copy of the same id - RecReplace) and their bytes. */
    long long AnswerBulkCount() const { return (long long)m_ansBulk.size(); }
    unsigned long long LastPutSeq() const { return m_lastPutSeq; }
    int LastPutLine() const { return m_lastPutLine; }
    bool LineGoneTo(int line, unsigned long long mark) const
    {
        if (line == livefirst::kLineRest) return RestGoneTo(mark);
        if (line == livefirst::kLineAnswerBulk)   /* T-1320 (b): that line is taken out of put order (AnsBulkPick) - the frame itself has left */
        {
            for (std::list<Entry>::const_iterator i = m_ansBulk.begin(); i != m_ansBulk.end(); ++i) if (i->seq == mark) return false;
            return true;
        }
        return true;
    }
    long long LastPutReplaced() const { return m_lastPutReplaced; }
    /* T-1320 fix 3: what the last Put did with a full answer RECORD (AnswerDupRule): kAnsDupAppend, kAnsDupReplace or kAnsDupSkip */
    int LastPutAnswerDup() const { return m_lastPutAnsDup; }
    /* T-1320 fix 3 fold 1 (H1): the link's first AREAMAP (Item.mapFirst) still waits on the REST line, whole or partly cut - livefirst.h
       MapLine keeps every later map on REST meanwhile. The REST line leaves only from its head and that map is never removed, so it waits
       exactly while the head's put order is not past it (RestGoneTo). */
    bool FirstMapWaiting() const { return m_firstMapSeq != 0 && !RestGoneTo(m_firstMapSeq); }
    /* T-1320 fix 3 fold 1 (H1): the REST line's head is an AREAMAP or a PLAYERSECTORS partly sent (only the head is ever in pieces) - it
       may not be removed, so the next map goes on REST behind it (MapLine). */
    bool RestMapPartly() const
    {
        return !m_rest.empty() && m_rest.front().sent > 0
               && (m_rest.front().it.type == livefirst::kTypeAreaMap || m_rest.front().it.type == livefirst::kTypePlayerSectors);
    }
    /* T-1320 fix 3 fold 1: what the last Put did as an AREAMAP on the LIVE line - REST AREAMAPs / PLAYERSECTORS it removed
       (RestMapsSupersede), and the put order it took from a removed barrier map (0 = its own) */
    long long LastPutMapsRemoved() const { return m_lastMapsRemoved; }
    long long LastPutSectorsRemoved() const { return m_lastSectorsRemoved; }
    unsigned long long LastPutMapTook() const { return m_lastMapTook; }
    long long LastPutReplacedBytes() const { return m_lastPutReplacedBytes; }
    /* T-1320 fold 1: a full RECORD that replaced an older waiting forward on its own line took that frame's place: LastPutSeq / LastPutLine
       then name that place (its put order is the older frame's), and nothing was appended. LastPutRemoved: the put order of every waiting
       forward the last Put removed outright (a later one on the same line, or one on another line) - AnswerGate::Waits forgets the marks
       that name them. */
    const std::vector<unsigned long long>& LastPutRemoved() const { return m_lastRemoved; }
    void DrainStats(livefirst::Stats* into)
    {
        if (into != 0) into->Add(m_stats);
        m_stats = livefirst::Stats();
    }
private:
    enum { kListLoose = 0, kListLive = 1, kListRest = 2, kListAnnounce = 3, kListHeld = 4, kListBulk = 5, kListAnswer = 6, kListAnsBulk = 7 };
    struct Entry
    {
        Item it; unsigned long long seq; double at; size_t sent; bool passed;   /* sent = bytes already cut off in CHUNKs; passed = REST frames waited when it was put */
        int area; bool ansRecord;   /* T-1320 (b), the ANSWER-BULK line: its area (AnswerAreaOf; -1 unread) and whether it is a RECORD (else a FRESH / CANT) */
        Entry() : seq(0), at(0.0), sent(0), passed(false), area(-1), ansRecord(false) {}
    };
    /* T-1320 (b): a barrier waiting on the REST line - its type, put time, and the areas it holds on the ANSWER-BULK line (all, or the
       sorted list) */
    struct Bar { unsigned int type; double at; bool all; std::vector<int> areas; Bar() : type(0), at(0.0), all(true) {} };
    struct Slot { int list; std::list<Entry>::iterator at; };   /* which list and where */
    typedef std::map<Key, Slot> Index;
    std::list<Entry>& ListOf(int which)
    {
        return which == kListLoose ? m_loose : (which == kListLive ? m_live : (which == kListAnnounce ? m_announce
               : (which == kListHeld ? m_held : (which == kListBulk ? m_bulk : (which == kListAnswer ? m_answer : (which == kListAnsBulk ? m_ansBulk : m_rest))))));
    }
    typedef std::map<unsigned int, std::deque<unsigned long long> > SeqBook;   /* per character / sender: the put order of each waiting announcement, oldest first */
    typedef std::map<unsigned int, long long> CountBook;                        /* per character / sender: held frames (absent = none) */
    static bool Earlier(const SeqBook& b, unsigned int k, unsigned long long seq)
    {
        SeqBook::const_iterator f = b.find(k);
        return f != b.end() && !f->second.empty() && f->second.front() < seq;
    }
    static bool Has(const CountBook& b, unsigned int k) { CountBook::const_iterator f = b.find(k); return f != b.end() && f->second > 0; }
    static void CountAdd(CountBook* b, unsigned int k, long long d)
    {
        long long& v = (*b)[k];
        v += d;
        if (v <= 0) b->erase(k);
    }
    static void SeqDrop(SeqBook* b, unsigned int k, unsigned long long seq)
    {
        SeqBook::iterator f = b->find(k);
        if (f == b->end()) return;
        for (std::deque<unsigned long long>::iterator i = f->second.begin(); i != f->second.end(); ++i)
            if (*i == seq) { f->second.erase(i); break; }
        if (f->second.empty()) b->erase(f);
    }
    /* The per-character gate at Put / Direct: a guaranteed live frame waits while an announcement of a character it names is queued
       here, or a frame already held names one of them; one speaking for its sender waits while one of its sender's announcements or
       held sender-wide frames is queued; one naming no character (Bare) while a BULK or held no-character frame of its sender and
       order family is queued (livefirst::BareFamily). An unmeasured frame naming no character never waits; one naming characters
       never waits for a sender-wide one. */
    bool Gated(const Item& it) const
    {
        if (!m_liveFirst || it.line != livefirst::kLineLive) return false;
        if (m_annOf.empty() && m_annFrom.empty() && m_held.empty() && m_bulkOf.empty() && m_bulkFrom.empty() && m_bulkBare.empty()) return false;
        if (it.bySender) return m_annFrom.find(it.origin) != m_annFrom.end() || m_bulkFrom.find(it.origin) != m_bulkFrom.end() || Has(m_heldScoped, it.origin);
        if (Bare(it)) return m_bulkBare.find(BareKey(it)) != m_bulkBare.end() || Has(m_heldBare, BareKey(it));   /* its sender's and family's */
        if (it.subjects.empty()) return false;
        for (size_t i = 0; i < it.subjects.size(); ++i)
            if (m_annOf.find(it.subjects[i]) != m_annOf.end() || m_bulkOf.find(it.subjects[i]) != m_bulkOf.end() || Has(m_heldOf, it.subjects[i])) return true;
        return false;
    }
    /* T-1118 (c): Gated because a BULK frame naming one of its characters (or of its sender, sender-wide) waits - counted bulkHeld. */
    bool GatedByBulk(const Item& it) const
    {
        if (it.bySender) return m_bulkFrom.find(it.origin) != m_bulkFrom.end();
        if (Bare(it)) return m_bulkBare.find(BareKey(it)) != m_bulkBare.end();
        for (size_t i = 0; i < it.subjects.size(); ++i)
            if (m_bulkOf.find(it.subjects[i]) != m_bulkOf.end()) return true;
        return false;
    }
    /* A measured live frame that names no character and does not speak for its sender (a box's contents, a box item move with owner
       0, an item answer naming only a request id, a price table) - what the receiver's own subject reader (spawnpace::MessageSubjects)
       finds no character in. What it is about cannot be read here, so a waiting BULK one holds its sender's later ones of the same
       ORDER FAMILY (livefirst::BareFamily: its own kind, or the item road) and no other: a burst of price tables holds no item
       answer. Unmeasured frames (an AREAMAP repeat) are never Bare. BareKey: the books' key - the sender (an origin slot, 16 bits;
       0 toward the world server) and the family. */
    static bool Bare(const Item& x) { return !x.bySender && x.subjects.empty() && x.kind != 0; }
    static unsigned int BareKey(const Item& x) { return ((x.origin & 0xFFFFu) << 16) | (livefirst::BareFamily(x.kind) & 0xFFFFu); }
    static bool Shares(const Item& a, const Item& b)
    {
        for (size_t i = 0; i < a.subjects.size(); ++i)
            for (size_t j = 0; j < b.subjects.size(); ++j)
                if (a.subjects[i] == b.subjects[j]) return true;
        return false;
    }
    /* review HIGH 1: an announcement put at `seq` (~0 = now) waits while a BULK frame put before it names one of its characters (or
       speaks for its sender; a ROSTER list included), or a frame held behind one does - so a SPAWN never overtakes an earlier BULK
       frame, or the DESPAWN held behind it, about the same character. Read only while a BULK frame waits (a frame is held behind
       a BULK frame only then; one held behind an earlier announcement is released onto the LIVE line before this one is taken). */
    bool AnnBlocked(const Item& a, unsigned long long seq) const
    {
        if (m_bulk.empty()) return false;
        for (std::list<Entry>::const_iterator b = m_bulk.begin(); b != m_bulk.end() && b->seq < seq; ++b)
            if ((b->it.bySender && b->it.origin == a.origin) || Shares(a, b->it)) return true;
        for (std::list<Entry>::const_iterator h = m_held.begin(); h != m_held.end() && h->seq < seq; ++h)
            if ((h->it.bySender && h->it.origin == a.origin) || Shares(a, h->it)) return true;
        return false;
    }
    /* T-1118 (c): a frame joins (add) or leaves the BULK line - its put order under each character it names, under its sender when it
       names characters or speaks for its sender itself (a BULK frame naming none, a town price table, holds no sender-wide frame),
       and its kind's waiting count (sticky). */
    typedef std::map<unsigned long long, long long> KindBook;   /* (sender << 32 | kind) -> BULK frames waiting */
    static unsigned long long KindKey(const Item& it) { return ((unsigned long long)it.origin << 32) | it.kind; }
    void BulkNote(const Entry& e, bool add)
    {
        if (Bare(e.it))
        {
            if (add) m_bulkBare[BareKey(e.it)].push_back(e.seq);
            else SeqDrop(&m_bulkBare, BareKey(e.it), e.seq);
        }
        for (size_t i = 0; !e.it.listOnly && i < e.it.subjects.size(); ++i)
        {
            if (add) m_bulkOf[e.it.subjects[i]].push_back(e.seq);
            else SeqDrop(&m_bulkOf, e.it.subjects[i], e.seq);
        }
        if (!e.it.listOnly && (e.it.bySender || !e.it.subjects.empty()))
        {
            if (add) m_bulkFrom[e.it.origin].push_back(e.seq);
            else SeqDrop(&m_bulkFrom, e.it.origin, e.seq);
        }
        long long& v = m_bulkWait[KindKey(e.it)];
        v += add ? 1 : -1;
        if (v <= 0) m_bulkWait.erase(KindKey(e.it));
    }
    /* An ANNOUNCE or BULK frame (whole, never cut) may start now when the network has room for it: the room asked for is the frame's
       own length, but never more than half the budget - the network's count may never fall near 0 while live traffic flows (ENet
       counts sent-but-unacknowledged bytes), so a long frame starts once half the budget is free and the network then holds at most
       half a budget plus the frame. A longer frame never asks for more room than a shorter one. The LIVE line has no byte limit;
       REST pieces are cut to the room left. */
    bool Fits(long long netBytes, const Item& it) const
    {
        const long long whole = (long long)(it.len != 0 ? it.len : it.bytes.size());
        const long long room = whole < m_budget / 2 ? whole : m_budget / 2;
        return netBytes + room <= m_budget;
    }
    /* T-1118 (c): why a frame goes on the BULK line (livefirst::kBulkSize / kBulkShare / kBulkSticky), or kBulkNot. Only a guaranteed
       frame of the LIVE line whose kind is measured; the byte window is read as of SetNow's clock, this frame's bytes included. */
    int BulkWhy(const Item& it, bool reliable)
    {
        if (!m_liveFirst || !reliable || it.line != livefirst::kLineLive || it.kind == 0) return livefirst::kBulkNot;
        const unsigned long long k = KindKey(it);
        const long long whole = (long long)(it.len != 0 ? it.len : it.bytes.size());
        if (m_bulkWait.find(k) != m_bulkWait.end()) return livefirst::kBulkSticky;
        if (whole > (long long)livefirst::kBulkFrameBytes) return livefirst::kBulkSize;
        if (WindowBytes(k) + whole > m_budget / livefirst::kBulkShareDiv) return livefirst::kBulkShare;
        return livefirst::kBulkNot;
    }
    void BulkWhyCount(int why)
    {
        if (why == livefirst::kBulkSize) ++m_stats.bulkBySize;
        else if (why == livefirst::kBulkShare) ++m_stats.bulkByShare;
        else if (why == livefirst::kBulkSticky) ++m_stats.bulkBySticky;
    }
    /* The bytes one kind of one sender put here within the last kBulkWindowSec (older entries forgotten first; an emptied kind leaves
       the window). */
    struct Window { std::deque<std::pair<double, long long> > q; long long sum; Window() : sum(0) {} };
    typedef std::map<unsigned long long, Window> WindowBook;
    long long WindowBytes(unsigned long long k)
    {
        WindowBook::iterator f = m_window.find(k);
        if (f == m_window.end()) return 0;
        WindowTrim(&f->second);
        if (f->second.q.empty()) { m_window.erase(f); return 0; }
        return f->second.sum;
    }
    void WindowTrim(Window* w) const
    {
        while (!w->q.empty() && w->q.front().first <= m_now - livefirst::kBulkWindowSec) { w->sum -= w->q.front().second; w->q.pop_front(); }
    }
    /* A measured guaranteed frame of the LIVE line put here (straight out or waiting on any line): its bytes join its kind's window. */
    void WindowAdd(const Item& it)
    {
        if (!Reliable(it.channel) || it.kind == 0) return;
        if (m_window.size() > 256)   /* kinds of senders gone quiet: forget the empty ones */
        {
            for (WindowBook::iterator i = m_window.begin(); i != m_window.end(); )
            {
                WindowTrim(&i->second);
                if (i->second.q.empty()) m_window.erase(i++); else ++i;
            }
        }
        Window& w = m_window[KindKey(it)];
        const long long whole = (long long)(it.len != 0 ? it.len : it.bytes.size());
        w.q.push_back(std::make_pair(m_now, whole)); w.sum += whole;
    }
    /* An announcement joins (add) or leaves the ANNOUNCE line: its put order under each character it names and under its sender. */
    void AnnNote(const Entry& e, bool add)
    {
        for (size_t i = 0; i < e.it.subjects.size(); ++i)
        {
            if (add) m_annOf[e.it.subjects[i]].push_back(e.seq);
            else SeqDrop(&m_annOf, e.it.subjects[i], e.seq);
        }
        if (add) m_annFrom[e.it.origin].push_back(e.seq);
        else SeqDrop(&m_annFrom, e.it.origin, e.seq);
    }
    /* A frame joins (+1) or leaves (-1) the HELD list. */
    void HeldNote(const Entry& e, long long d)
    {
        if (e.it.bySender) { CountAdd(&m_heldScoped, e.it.origin, d); return; }
        if (Bare(e.it)) { CountAdd(&m_heldBare, BareKey(e.it), d); return; }
        for (size_t i = 0; i < e.it.subjects.size(); ++i) CountAdd(&m_heldOf, e.it.subjects[i], d);
    }
    /* Moves to the LIVE line, at its put-order place, every held frame none of whose characters (or sender) has an announcement put
       before it still waiting, and that shares no character (or sender) with an earlier frame still held - oldest first. */
    void ReleaseHeld()
    {
        if (m_held.empty()) return;
        std::set<unsigned int> uids, scoped, bare;   /* the characters the frames still held so far name / the senders of the sender-wide ones / the sender-and-family keys of the no-character ones */
        std::list<Entry>::iterator h = m_held.begin();
        while (h != m_held.end())
        {
            const Item& x = h->it;
            bool wait = false;
            if (x.bySender) wait = Earlier(m_annFrom, x.origin, h->seq) || Earlier(m_bulkFrom, x.origin, h->seq) || scoped.count(x.origin) != 0;
            else if (Bare(x)) wait = Earlier(m_bulkBare, BareKey(x), h->seq) || bare.count(BareKey(x)) != 0;
            else
            {
                for (size_t i = 0; !wait && i < x.subjects.size(); ++i)
                    wait = Earlier(m_annOf, x.subjects[i], h->seq) || Earlier(m_bulkOf, x.subjects[i], h->seq) || uids.count(x.subjects[i]) != 0;
            }
            if (wait)
            {
                if (x.bySender) scoped.insert(x.origin);
                else if (Bare(x)) bare.insert(BareKey(x));
                else uids.insert(x.subjects.begin(), x.subjects.end());
                ++h;
                continue;
            }
            std::list<Entry>::iterator next = h; ++next;
            HeldNote(*h, -1);
            std::list<Entry>::iterator to = m_live.end();
            while (to != m_live.begin())
            {
                std::list<Entry>::iterator b = to; --b;
                if (b->seq < h->seq) break;
                to = b;
            }
            m_live.splice(to, m_held, h);   /* h stays valid, now on the LIVE line */
            if (h->it.cls == kClassLatest)
            {
                Index::iterator m = m_latest.find(h->it.key);
                if (m != m_latest.end() && m->second.at == h) m->second.list = kListLive;
            }
            h = next;
        }
    }
    /* Hands over the head of one list whole. */
    bool TakeWhole(int which, Item* out)
    {
        std::list<Entry>& q = ListOf(which);
        if (q.empty()) return false;
        std::list<Entry>::iterator f = q.begin();
        if (f->it.cls == kClassLatest)
        {
            Index::iterator m = m_latest.find(f->it.key);
            if (m != m_latest.end() && m->second.at == f) m_latest.erase(m);
        }
        if (which == kListRest && !m_barriers.empty() && m_barriers.front() == f->seq) { m_barInfo.erase(f->seq); m_barriers.pop_front(); }
        if (which == kListLive && !m_liveMaps.empty() && m_liveMaps.front() == f->seq) { m_barInfo.erase(f->seq); m_liveMaps.pop_front(); }   /* T-1320 fix 3 */
        if (which == kListRest) --m_restOf[f->it.type & 0xFF];
        if (f->it.recForward && !f->it.recId.empty()) RecDrop(f->it.recId, f);   /* T-1320 B */
        if (which == kListAnsBulk) AnsDrop(f);   /* T-1320 fix 3 */
        if (which == kListLive)
        {
            const double w = m_now - f->at;
            if (f->passed) ++m_stats.livePassed;
            ++m_stats.liveWaited;
            if (w > m_stats.liveWaitMaxSec) m_stats.liveWaitMaxSec = w;
        }
        if (which == kListAnnounce)
        {
            const double w = m_now - f->at;
            AnnNote(*f, false);
            ++m_stats.announceSent;
            if (w > m_stats.announceWaitMaxSec) m_stats.announceWaitMaxSec = w;
            ReleaseHeld();   /* onto the LIVE line, never this list: f stays valid */
        }
        if (which == kListBulk)   /* T-1118 (c) */
        {
            const double w = m_now - f->at;
            BulkNote(*f, false);
            ++m_stats.bulkSent;
            if (w > m_stats.bulkWaitMaxSec) m_stats.bulkWaitMaxSec = w;
            ReleaseHeld();   /* onto the LIVE line, never this list: f stays valid */
        }
        m_bytes -= (long long)(f->it.bytes.size() - f->sent); --m_count;
        out->cls = f->it.cls; out->key = f->it.key; out->type = f->it.type; out->channel = f->it.channel; out->bytes.swap(f->it.bytes);
        out->line = f->it.line; out->barrier = f->it.barrier; out->len = 0;
        // PROBE-START: P199 - the frame goes out with its first hand-over stamp, so its wait here is timed (the senders time only a LIVE-line frame)
        out->p199At = f->it.p199At;
        // PROBE-END: P199
        q.erase(f);
        if (m_count == 0) m_nextMark = kWaitLogFirst;
        return true;
    }
    /* T-1118 (b): Take / TakeAny in live-first mode (`any` = regardless of room, full pieces; otherwise a piece carries at most the
       room left under the budget, read from the caller's netBytes now). */
    bool TakeLiveFirst(long long netCount, long long netBytes, bool any, Item* out)
    {
        if (!any && NetFull(netCount, netBytes)) return false;
        if (!m_loose.empty()) return TakeWhole(kListLoose, out);
        if (!m_answer.empty() && m_restOf[livefirst::kTypeWelcome & 0xFF] == 0) return TakeWhole(kListAnswer, out);   /* T-1279 (T2290): first, past barriers - after a waiting WELCOME */
        if (!m_held.empty() && m_announce.empty()) ReleaseHeld();   /* nothing left to wait for (each announcement's leaving already released them) */
        /* T-1320 fix 3: a changed AREAMAP at the head of the LIVE line waits for the ANSWER-BULK RECORDs of its areas put before it
           (MapWaitsFor); meanwhile nothing put after it goes on ANNOUNCE, BULK or REST either (AnsHeldBy holds its areas' answers), as
           behind a barrier on REST */
        const unsigned long long liveHold = LiveMapHeld();
        const unsigned long long firstHold = liveHold != 0 && (m_barriers.empty() || liveHold < m_barriers.front()) ? liveHold : (m_barriers.empty() ? 0 : m_barriers.front());
        if (!m_live.empty() && liveHold == 0)
        {
            if (m_barriers.empty() || m_live.front().seq < m_barriers.front())
            {
                if (m_live.front().it.channel == kChannelReliable && netBytes > m_stats.liveAheadMaxBytes) m_stats.liveAheadMaxBytes = netBytes;
                return TakeWhole(kListLive, out);
            }
            ++m_stats.barrierHeld;
        }
        /* the ANNOUNCE line: before REST, a frame starting only when it fits the budget (Fits); one held by a barrier lets REST drain up to that barrier */
        if (!m_announce.empty() && (firstHold == 0 || m_announce.front().seq < firstHold)
            && !AnnBlocked(m_announce.front().it, m_announce.front().seq))   /* review HIGH 1: else the BULK frame it waits for goes first */
            return (any || Fits(netBytes, m_announce.front().it)) ? TakeWhole(kListAnnounce, out) : false;
        /* T-1320 B the ANSWER-BULK line (an area answer's RECORD and its FRESH): after ANNOUNCE, ahead of BULK and REST, cut like REST under
           the budget, held by a barrier put before it that holds it (T-1320 (b): AnsHeldBy - a WELCOME or PLAYER_GONE holds every frame,
           a changed AREAMAP only the RECORDs of the areas whose holder it changes). ONE FRAME IN PIECES AT A TIME on the link (the far end's Joiner joins one frame): a head
           that must be cut waits while a REST frame is partly cut - that frame's pieces finish first (BULK and REST go meanwhile; REST only
           finishes its open frame, below). A head that goes whole may go between pieces, as a live frame does. */
        const bool restOpen = !m_rest.empty() && m_rest.front().sent > 0;
        const bool bulkReady = !m_bulk.empty() && (firstHold == 0 || m_bulk.front().seq < firstHold);
        /* T-1320 (b): the frame taken is the first one free (AnsBulkPick) - a RECORD waits only for a barrier put before it that holds its
           area, so it may go ahead of an earlier answer of another area held by a map; it is moved to the head of the line first, so the
           frame in pieces is always the head */
        const std::list<Entry>::iterator pick = AnsBulkPick(restOpen);
        if (pick != m_ansBulk.end() && pick != m_ansBulk.begin()) m_ansBulk.splice(m_ansBulk.begin(), m_ansBulk, pick);
        if (pick != m_ansBulk.end())
        {
            /* T-1320 fold 1 (review MEDIUM): BETWEEN AN ANSWER'S PIECES (its head partly sent) a waiting BULK frame goes first, whole, when it
               fits the budget - and while it does not fit, the answer's next piece waits for it (as BULK waits ahead of REST), so a long
               answer never holds BULK, and the live frames held behind a BULK frame (ReleaseHeld runs when one is taken), for its whole
               send. Still one frame in pieces at a time: a BULK frame is never cut. An answer frame not started yet stays ahead of BULK. */
            if (bulkReady && m_ansBulk.front().sent > 0) return (any || Fits(netBytes, m_bulk.front().it)) ? TakeWhole(kListBulk, out) : false;
            if (!any && netBytes >= m_budget) return false;
            const bool took = TakeCut(kListAnsBulk, netBytes, any, out);
            return took;
        }
        /* T-1118 (c) the BULK line: after ANNOUNCE, before REST, a frame starting only when it fits the budget (Fits), held by a barrier as ANNOUNCE is */
        if (bulkReady)
            return (any || Fits(netBytes, m_bulk.front().it)) ? TakeWhole(kListBulk, out) : false;
        if (m_rest.empty() || (!any && netBytes >= m_budget)) return false;
        if (liveHold != 0 && m_rest.front().seq > liveHold) return false;   /* T-1320 fix 3: REST put after a waiting LIVE map stays behind it */
        if (!restOpen && !m_ansBulk.empty() && m_ansBulk.front().sent > 0 && WillCut(m_rest.front())) return false;   /* T-1320 B: never two frames in pieces at once */
        return TakeCut(kListRest, netBytes, any, out);
    }
    /* T-1320 B: the head of a line would be cut into pieces (not started yet), or is being cut (started). */
    static bool WillCut(const Entry& h)
    {
        const size_t whole = h.it.bytes.size();
        return h.sent > 0 || (livefirst::CutsInto(whole) && whole <= livefirst::kJoinMax && h.it.type != livefirst::kMsgChunk);
    }
    /* T-1320 (b): the put order of the first barrier waiting on the REST line, put before this ANSWER-BULK frame, that holds it (0 = none):
       a WELCOME, a PLAYER_GONE (any barrier not an AREAMAP) and every barrier for a frame whose area could not be read hold it; an AREAMAP
       holds a RECORD only when its set holds the record's area (Item.barAll / barAreas) and never holds a FRESH or CANT (the game reads no
       map for them: store.cpp AreaFreshArrive). The game judges a record of an area against that area's holder alone (store.cpp
       ZoneRecvJudge), and a map that does not change the area's holder leaves that judgement as it was. */
    unsigned long long AnsHeldBy(const Entry& e) const
    {
        for (std::deque<unsigned long long>::const_iterator b = m_barriers.begin(); b != m_barriers.end() && *b < e.seq; ++b)
        {
            const std::map<unsigned long long, Bar>::const_iterator i = m_barInfo.find(*b);
            if (i == m_barInfo.end() || i->second.type != livefirst::kTypeAreaMap || e.area < 0) return *b;
            if (!e.ansRecord) continue;
            if (i->second.all || std::binary_search(i->second.areas.begin(), i->second.areas.end(), e.area)) return *b;
        }
        /* T-1320 fix 3: a changed AREAMAP waiting on the LIVE line holds as one on REST does */
        for (std::deque<unsigned long long>::const_iterator b = m_liveMaps.begin(); b != m_liveMaps.end() && *b < e.seq; ++b)
        {
            const std::map<unsigned long long, Bar>::const_iterator i = m_barInfo.find(*b);
            if (i == m_barInfo.end() || e.area < 0) return *b;
            if (!e.ansRecord) continue;
            if (i->second.all || std::binary_search(i->second.areas.begin(), i->second.areas.end(), e.area)) return *b;
        }
        return 0;
    }
    /* T-1320 fix 3 - THE PER-AREA REVERSE WAIT: a changed AREAMAP holding `all` or the sorted `areas` (Item.barAll / barAreas) may not leave
       while an ANSWER-BULK RECORD of a held area, put before it (seq below `before`; ~0 = a map not put yet), still waits - or any
       ANSWER-BULK frame whose area could not be read: that answer was put under the earlier map and is judged against it. A FRESH or
       CANT is never waited for (the game reads no map for it: store.cpp AreaFreshArrive). A forwarded RECORD on REST is not waited for:
       the game's judgement differs only for a copy not newer than its own (zonerec.h ZoneRecvJudge), and waiting for REST is the hold
       this fix removes (T2314: 0.3 - 2.4 MB of REST ahead). */
    bool MapWaitsFor(bool all, const std::vector<int>& areas, unsigned long long before) const
    {
        for (std::list<Entry>::const_iterator i = m_ansBulk.begin(); i != m_ansBulk.end(); ++i)
        {
            if (i->seq >= before) continue;   /* the line is taken out of put order (AnsBulkPick): walk it whole */
            if (i->area < 0) return true;
            if (!i->ansRecord) continue;
            if (all || std::binary_search(areas.begin(), areas.end(), i->area)) return true;
        }
        return false;
    }
    /* T-1320 fix 3: the put order of the changed AREAMAP at the head of the LIVE line while it waits (MapWaitsFor), else 0. */
    unsigned long long LiveMapHeld() const
    {
        if (m_live.empty() || m_liveMaps.empty() || m_live.front().seq != m_liveMaps.front()) return 0;
        const std::map<unsigned long long, Bar>::const_iterator i = m_barInfo.find(m_liveMaps.front());
        if (i == m_barInfo.end()) return 0;
        return MapWaitsFor(i->second.all, i->second.areas, m_liveMaps.front()) ? m_liveMaps.front() : 0;
    }
    /* T-1320 (b): the ANSWER-BULK frame Take hands over next, or end(): the head while it is in pieces (one frame in pieces at a time;
       nothing put after it holds it); else the first frame in put order that no barrier holds (AnsHeldBy), that may start (not one to be
       cut while a REST frame is in pieces) and that has no earlier frame of its own area still waiting (frames of one area keep their
       order: a FRESH never goes ahead of its own record). A frame whose area could not be read passes no frame, and no frame passes it. */
    std::list<Entry>::iterator AnsBulkPick(bool restOpen)
    {
        if (m_ansBulk.empty()) return m_ansBulk.end();
        if (m_ansBulk.front().sent > 0) return m_ansBulk.begin();
        std::set<int> blocked; bool skipped = false;
        for (std::list<Entry>::iterator i = m_ansBulk.begin(); i != m_ansBulk.end(); ++i)
        {
            if (i->area >= 0 && blocked.count(i->area) != 0) continue;
            if (i->area < 0 && skipped) break;
            const unsigned long long by = AnsHeldBy(*i);
            if (by == 0 && !(restOpen && WillCut(*i))) return i;
            if (i->area < 0) break;
            blocked.insert(i->area); skipped = true;
        }
        return m_ansBulk.end();
    }
    /* The head of the REST line (or, T-1320 B, of the ANSWER-BULK line): whole when it is not longer than one piece, else its next piece -
       at most the room left under the budget (livefirst::PieceBytes; full pieces at a close). The last piece removes the frame. */
    bool TakeCut(int which, long long netBytes, bool any, Item* out)
    {
        std::list<Entry>& q = ListOf(which);
        Entry& h = q.front();
        const size_t whole = h.it.bytes.size();
        if (h.sent == 0 && (!livefirst::CutsInto(whole) || whole > livefirst::kJoinMax || h.it.type == livefirst::kMsgChunk)) return TakeWhole(which, out);   /* over 32 MiB: whole - ENet refuses it, counted by the caller; a CHUNK handed on is never cut again */
        if (h.sent == 0)
        {
            ++m_stats.cutFrames;
            if (h.it.cls == kClassLatest)   /* from its first piece on it can no longer be replaced */
            {
                Index::iterator m = m_latest.find(h.it.key);
                if (m != m_latest.end() && m->second.at == q.begin()) m_latest.erase(m);
            }
            if (h.it.recForward && !h.it.recId.empty()) RecDrop(h.it.recId, q.begin());   /* T-1320 B: a frame partly sent is never replaced */
            if (which == kListAnsBulk) AnsDrop(q.begin());   /* T-1320 fix 3: nor is an answer copy partly sent - a newer one goes after it */
        }
        std::vector<char> c;
        const size_t n = livefirst::ChunkEncodeUpTo(&h.it.bytes[0], whole, h.sent, livefirst::PieceBytes(m_budget, netBytes, any), &c);
        if (n == 0) return TakeWhole(which, out);   /* cannot happen (the checks above); never loops */
        h.sent += n; m_bytes -= (long long)n; ++m_stats.chunks;
        out->cls = kClassMust; out->key = Key(); out->type = livefirst::kMsgChunk; out->channel = kChannelReliable; out->bytes.swap(c);
        out->line = which == kListAnsBulk ? (int)livefirst::kLineAnswerBulk : (int)livefirst::kLineRest; out->barrier = false; out->len = 0;
        // PROBE-START: P199 - a record piece carries no live stamp
        out->p199At = 0.0;
        // PROBE-END: P199
        if (h.sent >= whole)
        {
            if (which == kListRest && !m_barriers.empty() && m_barriers.front() == h.seq) { m_barInfo.erase(h.seq); m_barriers.pop_front(); }
            if (which == kListRest) --m_restOf[h.it.type & 0xFF];
            q.pop_front(); --m_count;
            if (m_count == 0) m_nextMark = kWaitLogFirst;
        }
        return true;
    }
    /* T-1320 B - THE NEWEST FULL COPY WINS IN THE QUEUE. A full RECORD put on line `which` (its file carried) replaces every waiting
       FORWARDED copy of the same id (Item.recForward) that has not started to go (a started one left the index at its first piece) and
       whose own stamp is not newer than the new copy's (older, or the same write sent again). A position-only copy (no file) never
       replaces anything: it carries no people. Counted in LastPutReplaced / LastPutReplacedBytes.
       T-1320 fold 1 (review HIGH): the EARLIEST such copy on the new copy's own line is replaced IN PLACE - the new bytes and stamp take
       its queue position and put order (seq), so a FRESH already queued behind it stays behind the record, and the counts (m_count,
       m_restOf), the gate's mark at that seq and this index stay right (its slot stays while the new copy is a forward too). Every other
       one (a later one on that line, or one on another line - a REST forward when the new copy goes on the ANSWER-BULK line) is removed,
       and its put order is listed in LastPutRemoved so AnswerGate::Waits forgets every mark naming it in the same step. Answers true
       with *into = the replaced-in-place entry, or false when the caller must append the new copy. */
    bool RecReplace(const Item& it, int which, std::list<Entry>::iterator* into, bool inPlaceOk)
    {
        RecBook::iterator f = m_recOf.find(it.recId);
        if (f == m_recOf.end()) return false;
        std::vector<RecSlot>& v = f->second;   /* in put order */
        bool inPlace = false;
        for (size_t i = 0; i < v.size(); )
        {
            std::list<Entry>::iterator e = v[i].at;
            if (e->sent != 0 || e->it.recStamp > it.recStamp) { ++i; continue; }
            const long long b = (long long)e->it.bytes.size();
            ++m_lastPutReplaced; m_lastPutReplacedBytes += b;
            if (!inPlace && inPlaceOk && v[i].list == which && !e->it.barrier)
            {
                inPlace = true; *into = e;
                m_bytes += (long long)it.bytes.size() - b;   /* sent == 0: the whole old frame was counted */
                e->it = it;                                  /* seq, at, sent (0) and its place kept */
                if (it.recForward) ++i; else v.erase(v.begin() + i);   /* the new copy is replaceable only if it is a forward itself */
                continue;
            }
            m_bytes -= b; --m_count;
            if (v[i].list == kListRest) --m_restOf[e->it.type & 0xFF];
            m_lastRemoved.push_back(e->seq);
            ListOf(v[i].list).erase(e);
            v.erase(v.begin() + i);
        }
        if (v.empty()) m_recOf.erase(f);
        if (m_count == 0) m_nextMark = kWaitLogFirst;
        return inPlace;
    }
    /* T-1320 fix 3 fold 1 (H1) - MAPS AND SECTORS ARE LATEST-VALUE. An AREAMAP put on the LIVE line (livefirst.h MapLine: not the link's
       first, the first no longer waiting, none partly sent, not cut) removes every AREAMAP and PLAYERSECTORS still waiting on the REST
       line NOT STARTED (a partly sent one never: its pieces must finish; the link's first map never: it is gone by then) and put after
       the newest waiting WELCOME / PLAYER_GONE (a map never passes one of those: one put before such a barrier stays). Removing one cannot
       lose what the game needs: an AREAMAP is the WHOLE map (the world server builds every row each time: store_main.cpp AreaMapBuild)
       and the game applies each map whole, replacing the last (store.cpp ~22985-23000), so the newer map carries everything the removed
       one did, newer; a PLAYERSECTORS is likewise the whole list, and the one put right after the new map replaces it.
       Out: *take = the put order of the earliest removed BARRIER map (0 = none) - the new map takes that place, so every frame that map
       held (live frames put after it; ANSWER-BULK RECORDs of its areas put after it, AnsHeldBy) stays behind the new one, and the new one
       waits for its areas' answers put before that place (MapWaitsFor), as the removed map would have; *bar / *all / *areas = a barrier
       map was removed / one held every area / the union of their area sets (merged into the new map's holds: each set is a change from
       the map before it, so the union covers every change from the last map the game keeps to the new one). m_nextSectorsSlot = the put
       order of the first removed PLAYERSECTORS after *take (the new map's PLAYERSECTORS takes it, right behind the map). */
    void RestMapsSupersede(unsigned long long* take, bool* bar, bool* all, std::vector<int>* areas)
    {
        *take = 0; *bar = false; *all = false; areas->clear(); m_nextSectorsSlot = 0;
        unsigned long long fence = 0;   /* the newest waiting WELCOME / PLAYER_GONE (any barrier not an AREAMAP) */
        for (std::deque<unsigned long long>::const_iterator b = m_barriers.begin(); b != m_barriers.end(); ++b)
        {
            const std::map<unsigned long long, Bar>::const_iterator i = m_barInfo.find(*b);
            if ((i == m_barInfo.end() || i->second.type != livefirst::kTypeAreaMap) && *b > fence) fence = *b;
        }
        std::vector<unsigned long long> sectors;
        for (std::list<Entry>::iterator r = m_rest.begin(); r != m_rest.end(); )
        {
            const unsigned int t = r->it.type;
            if (r->seq <= fence || r->sent != 0 || r->it.mapFirst || (t != livefirst::kTypeAreaMap && t != livefirst::kTypePlayerSectors)) { ++r; continue; }
            if (r->it.barrier)
            {
                for (std::deque<unsigned long long>::iterator b = m_barriers.begin(); b != m_barriers.end(); ++b)
                    if (*b == r->seq) { m_barriers.erase(b); break; }
                const std::map<unsigned long long, Bar>::iterator i = m_barInfo.find(r->seq);
                if (i == m_barInfo.end() || i->second.all) *all = true;
                else areas->insert(areas->end(), i->second.areas.begin(), i->second.areas.end());
                if (i != m_barInfo.end()) m_barInfo.erase(i);
                if (*take == 0) *take = r->seq;   /* the line is in put order: the first met is the earliest */
                *bar = true;
            }
            if (t == livefirst::kTypeAreaMap) ++m_lastMapsRemoved; else { ++m_lastSectorsRemoved; sectors.push_back(r->seq); }
            if (r->it.cls == kClassLatest)
            {
                Index::iterator m = m_latest.find(r->it.key);
                if (m != m_latest.end() && m->second.at == r) m_latest.erase(m);
            }
            --m_restOf[t & 0xFF];
            m_bytes -= (long long)r->it.bytes.size(); --m_count;
            r = m_rest.erase(r);
        }
        for (size_t k = 0; *take != 0 && k < sectors.size(); ++k) if (sectors[k] > *take) { m_nextSectorsSlot = sectors[k]; break; }
        if (m_count == 0) m_nextMark = kWaitLogFirst;
    }
    /* T-1320 fix 3: an answer copy leaves the one-copy index (taken whole, or its first piece cut). */
    void AnsDrop(std::list<Entry>::iterator at)
    {
        if (at->it.recId.empty()) return;
        std::map<std::string, std::list<Entry>::iterator>::iterator f = m_ansOf.find(at->it.recId);
        if (f != m_ansOf.end() && f->second == at) m_ansOf.erase(f);
    }
    /* T-1320 B: a forwarded record leaves the replace index (taken whole, or its first piece cut). */
    void RecDrop(const std::string& id, std::list<Entry>::iterator at)
    {
        RecBook::iterator f = m_recOf.find(id);
        if (f == m_recOf.end()) return;
        for (size_t i = 0; i < f->second.size(); ++i)
            if (f->second[i].at == at) { f->second.erase(f->second.begin() + i); break; }
        if (f->second.empty()) m_recOf.erase(f);
    }
    /* Removes one waiting latest-value entry and its index entry (never one partly cut: its index entry is gone by then). */
    void Unlink(Index::iterator f)
    {
        std::list<Entry>& q = ListOf(f->second.list);
        m_bytes -= (long long)(f->second.at->it.bytes.size() - f->second.at->sent); --m_count;
        if (f->second.at->it.barrier)
            for (std::deque<unsigned long long>::iterator b = m_barriers.begin(); b != m_barriers.end(); ++b)
                if (*b == f->second.at->seq) { m_barInfo.erase(*b); m_barriers.erase(b); break; }
        if (f->second.at->it.barrier)   /* T-1320 fix 3 */
            for (std::deque<unsigned long long>::iterator b = m_liveMaps.begin(); b != m_liveMaps.end(); ++b)
                if (*b == f->second.at->seq) { m_barInfo.erase(*b); m_liveMaps.erase(b); break; }
        if (f->second.list == kListRest) --m_restOf[f->second.at->it.type & 0xFF];
        const bool wasHeld = f->second.list == kListHeld, wasBulk = f->second.list == kListBulk;
        if (wasHeld) HeldNote(*f->second.at, -1);
        if (wasBulk) BulkNote(*f->second.at, false);   /* T-1118 (c) */
        q.erase(f->second.at); m_latest.erase(f);
        if (wasHeld || wasBulk) ReleaseHeld();   /* frames held behind this one alone may go now */
        if (m_count == 0) m_nextMark = kWaitLogFirst;
    }
    std::list<Entry> m_rest;    /* reliable messages, in the order they were sent (live-first mode: the REST line) */
    std::list<Entry> m_loose;   /* unreliable messages that wait only because the network queue was full */
    std::list<Entry> m_live;    /* T-1118 (b), live-first mode: the LIVE line */
    std::list<Entry> m_announce;   /* live-first mode: the ANNOUNCE line (CONTEXT, SPAWN, APPEARANCE, CLOTHING) */
    std::list<Entry> m_held;       /* live-first mode: live frames held behind an announcement of a character they name, in put order */
    SeqBook m_annOf, m_annFrom;    /* the waiting announcements' put order per character named / per sender */
    CountBook m_heldOf, m_heldScoped;   /* held frames per character named / held sender-wide frames per sender */
    std::list<Entry> m_bulk;       /* T-1118 (c), live-first mode: the BULK line, in put order */
    std::list<Entry> m_answer;     /* T-1279 (T2290), live-first mode: the ANSWER line, in put order */
    std::list<Entry> m_ansBulk;    /* T-1320 B, live-first mode: the ANSWER-BULK line, in put order */
    struct RecSlot { int list; std::list<Entry>::iterator at; };
    typedef std::map<std::string, std::vector<RecSlot> > RecBook;
    RecBook m_recOf;               /* T-1320 B: the waiting forwarded RECORDs not started yet, per id (RecReplace) */
    unsigned long long m_lastPutSeq; int m_lastPutLine; long long m_lastPutReplaced, m_lastPutReplacedBytes;   /* T-1320 B: the last Put */
    std::vector<unsigned long long> m_lastRemoved;   /* T-1320 fold 1: the put order of each waiting forward the last Put removed (LastPutRemoved) */
    SeqBook m_bulkOf, m_bulkFrom;  /* the waiting BULK frames' put order per character named / per sender (BulkNote) */
    KindBook m_bulkWait;           /* BULK frames waiting per kind of each sender (sticky) */
    SeqBook m_bulkBare;            /* the waiting no-character BULK frames' put order per sender and order family (BareKey) */
    CountBook m_heldBare;          /* held no-character frames per sender and order family (BareKey) */
    bool m_lastDirectBulk;         /* LastDirectBulk */
    unsigned long long m_restPutSeq;   /* T-1279 (T2290): the put order of the last frame put on the REST line (RestPutSeq) */
    WindowBook m_window;           /* the bytes each kind of each sender put here within the last kBulkWindowSec */
    std::deque<unsigned long long> m_barriers;   /* T-1118 (b): the put order (seq) of each barrier waiting on the REST line, oldest first */
    std::map<unsigned long long, Bar> m_barInfo;   /* T-1320 (b): per waiting barrier (by seq) its type, put time and the areas it holds */
    std::deque<unsigned long long> m_liveMaps;   /* T-1320 fix 3: the put order of each changed AREAMAP waiting on the LIVE line, oldest first */
    std::map<std::string, std::list<Entry>::iterator> m_ansOf;   /* T-1320 fix 3 (owner 1607 a): per record id the answer copy waiting on ANSWER-BULK not started */
    int m_lastPutAnsDup;   /* T-1320 fix 3: LastPutAnswerDup */
    unsigned long long m_firstMapSeq;   /* T-1320 fix 3 fold 1: the put order of the link's first AREAMAP put on REST (FirstMapWaiting), 0 = none */
    unsigned long long m_sectorsSlot;   /* T-1320 fix 3 fold 1: the put order the next Put takes if it is a LIVE PLAYERSECTORS (0 = none) */
    unsigned long long m_nextSectorsSlot;   /* T-1320 fix 3 fold 1: RestMapsSupersede's removed PLAYERSECTORS place, handed to m_sectorsSlot */
    long long m_lastMapsRemoved, m_lastSectorsRemoved; unsigned long long m_lastMapTook;   /* T-1320 fix 3 fold 1: LastPutMapsRemoved / ... */
    Index m_latest;             /* the one waiting entry of each latest-value subject */
    long long m_count, m_bytes, m_nextMark;
    bool m_was;
    bool m_liveFirst; long long m_budget; double m_now; unsigned long long m_seq;
    livefirst::Stats m_stats;
    long long m_restOf[256];    /* frames waiting on the REST line, by type (RestWaitingOf) */
};

/* T-1279 (T2290; review MEDIUM) - THE ANSWER LINE'S GATE, per link toward a game: the world server's link to each game (store_main.cpp
   SbPeer) and the host's Steam bridge links toward joiners (net/steam_transport.cpp LfSend). An AREA_FRESH CANT goes on the ANSWER
   line (livefirst.h AreaFreshLine) only when nothing it may not pass still waits on that link's REST line:
     - a RECORD of its own area (a CANT current or fault ends the wait and the copy there loads; zonerec.h ZoneRecvJudge would take
       the holder's copy even when it is not newer, so the record must be judged first, as it was before the ANSWER line);
     - on a link that hands pieces on (the bridge): a piece of a RECORD whose area its first piece did not show (the first piece not
       seen here, or too short to hold the id) - any area, the safe side; and a piece of a WELCOME (the Outbox holds the ANSWER line
       only behind a WHOLE waiting WELCOME - RestWaitingOf counts a piece as a CHUNK).
   Each such frame put on the REST line is marked with its put order (Outbox::RestPutSeq; a later piece of the same frame moves the
   mark on to it), and the mark is met once the REST line's head is past it (Outbox::RestGoneTo) - a must-deliver frame leaves that
   line only from its head, so nothing put later can meet it early. Anything already handed to the network reaches the game first
   (one reliable channel, in order). Every frame of the link goes through HoldOf (Direct ones too): a frame cut in pieces is followed
   from its first piece to its last. Reset with the link. */
class AnswerGate
{
public:
    enum { kHoldNone = -1, kHoldAnyArea = -2, kHoldWelcome = -3 };   /* -1 / -2 = AreaKeyOfRecordHead's */
    enum { kTypeRecord = 28 };   /* RECORD on the world-server link (= store_main.cpp MSG_RECORD; store_main.cpp checks it; = AreaKeyOfRecordHead's 28) */
    AnswerGate() : m_open(kHoldNone), m_known(false) {}
    void Reset() { m_marks.clear(); m_bulkMarks.clear(); m_fresh.clear(); m_open = kHoldNone; m_known = false; }
    /* The area key a RECORD frame's head carries (frame header included; its first field is the record's id: u32 length, then the
       id, zone.<X>.<Y> for an area): kHoldNone for another type, an id that is not an area's, or - whole - one that cannot be read
       (never taken); kHoldAnyArea for a first piece too short to hold the id (whole = false). */
    static int AreaOfRecordHead(const char* f, size_t n, bool whole) { return AreaKeyOfRecordHead(f, n, whole); }   /* T-1320 (b): moved above Outbox (-1 = kHoldNone, -2 = kHoldAnyArea) */
    /* T-1339 (protocol 153): RECORD_FEED (58) and its AREA_DONE kind (10) - store_main.cpp checks both against recordfeed.h. A catch-up
       (store_main.cpp AoifCatchUp: an area's records written since its AREA_STALE, then AREA_DONE all = 0 naming it, all on REST) ends
       with that DONE; while it waits on REST an AREA_FRESH or CANT of its area goes behind it (AfSend reads HoldsRest), so an answer never
       passes the catch-up's records. The whole frame: header, kind 10 and three zero bytes, u32 epoch, u8 all, u32 n, n * u16 key. One
       key = that area; all = 1 (the first download's) = no hold, as before; several keys = any area. */
    enum { kTypeRecordFeed = 58, kFeedKindAreaDone = 10, kFeedKindAreaStale = 11 };
    static int AreaOfCatchUpDone(const char* f, size_t n)
    {
        const size_t h = livefirst::kFrameHeader;
        if (n >= h + 14 && (unsigned char)f[h] == (unsigned char)kFeedKindAreaStale && f[h + 1] == 0 && f[h + 2] == 0 && f[h + 3] == 0)
        {   /* fold 1 (MED 4): an AREA_STALE {u32 epoch, u32 n, keys} holds its area too while it waits, so a catch-up run on the ANSWER-BULK
               line (store_main.cpp AoifCatchUpNow) is chosen only once its STALE has left REST - its DONE never passes its STALE */
            unsigned int sn = 0; std::memcpy(&sn, f + h + 8, 4);
            if (sn == 0 || n != h + 12 + (size_t)sn * 2) return kHoldNone;
            if (sn > 1) return kHoldAnyArea;
            const int sk = (int)((unsigned int)(unsigned char)f[h + 12] | ((unsigned int)(unsigned char)f[h + 13] << 8));
            const int sa = areafresh::Key(sk % 64, sk / 64);
            return sa >= 0 ? sa : (int)kHoldNone;
        }
        if (n < h + 13 || (unsigned char)f[h] != (unsigned char)kFeedKindAreaDone || f[h + 1] != 0 || f[h + 2] != 0 || f[h + 3] != 0) return kHoldNone;
        if (f[h + 8] != 0) return kHoldNone;   /* all = 1 */
        unsigned int cnt = 0; std::memcpy(&cnt, f + h + 9, 4);
        if (cnt == 0 || n != h + 13 + (size_t)cnt * 2) return kHoldNone;
        if (cnt > 1) return kHoldAnyArea;
        const int k = (int)((unsigned int)(unsigned char)f[h + 13] | ((unsigned int)(unsigned char)f[h + 14] << 8));   /* the feed's key: y * 64 + x */
        const int a = areafresh::Key(k % 64, k / 64);   /* the hold's key is a zone record's: areafresh::Key (x * 64 + y) */
        return a >= 0 ? a : (int)kHoldNone;
    }
    /* One guaranteed frame toward the game, whole (header included), before it is sent or put: what a CANT may not pass while it
       waits - an area key, kHoldAnyArea, kHoldWelcome, or kHoldNone. A CHUNK: the hold of the frame its first piece began (read
       from that piece), kHoldAnyArea for a piece whose first piece was not seen here (or a malformed one); its last piece ends it. */
    int HoldOf(const char* f, size_t n)
    {
        if (f == 0 || n < livefirst::kFrameHeader) return kHoldNone;
        if ((unsigned char)f[0] == (unsigned char)kTypeRecordFeed) return AreaOfCatchUpDone(f, n);   /* T-1339: a catch-up's AREA_DONE holds its area like a RECORD of it */
        if ((unsigned char)f[0] != (unsigned char)livefirst::kMsgChunk) return AreaOfRecordHead(f, n, true);
        const size_t head = livefirst::kFrameHeader + livefirst::kChunkHeader;
        if (n <= head) { m_open = kHoldNone; m_known = false; return kHoldAnyArea; }
        unsigned int total = 0, at = 0;
        std::memcpy(&total, f + livefirst::kFrameHeader, 4);
        std::memcpy(&at, f + livefirst::kFrameHeader + 4, 4);
        const char* piece = f + head;
        const size_t pn = n - head;
        if (at == 0)
        {
            m_open = (unsigned char)piece[0] == (unsigned char)livefirst::kTypeWelcome ? (int)kHoldWelcome : AreaOfRecordHead(piece, pn, false);
            m_known = true;
        }
        const int hold = m_known ? m_open : (int)kHoldAnyArea;
        if ((size_t)at + pn >= (size_t)total) { m_open = kHoldNone; m_known = false; }   /* its last piece */
        return hold;
    }
    /* The frame HoldOf answered `hold` for was just put on the REST line of `box` - T-1320 B: or on its ANSWER-BULK line (Outbox::LastPutLine),
       marked there apart (an area's record only). T-1320 fold 1: each mark is the put order of ONE waiting frame of that hold (a hold keeps
       a list of them, oldest first): a record that replaced an older one in place is marked at that place (Outbox::LastPutSeq, already
       listed), and first, in the same step, every frame the Put removed outright (Outbox::LastPutRemoved) is struck from every list - so
       no mark reports a record waiting that is gone, and a hold's earlier frames that still wait keep it held. */
    void Waits(int hold, const Outbox& box)
    {
        Forget(box);
        if (hold == kHoldNone) return;
        if (box.LastPutLine() == livefirst::kLineAnswerBulk) { if (hold >= 0) MarkAdd(&m_bulkMarks, hold, box.LastPutSeq(), livefirst::kLineAnswerBulk, box); return; }
        MarkAdd(&m_marks, hold, box.LastPutLine() == livefirst::kLineRest ? box.LastPutSeq() : box.RestPutSeq(), livefirst::kLineRest, box);
    }
    /* T-1320 fold 1: strikes the frames the last Put of `box` removed from every REST and ANSWER-BULK mark list (put orders are unique on
       a box). Waits calls it; repeating it is harmless. */
    void Forget(const Outbox& box)
    {
        const std::vector<unsigned long long>& gone = box.LastPutRemoved();
        for (size_t i = 0; i < gone.size(); ++i) { MarkDrop(&m_marks, gone[i]); MarkDrop(&m_bulkMarks, gone[i]); }
    }
    /* T-1320 B / C: a frame just put on `box` (whole, header included): an AREA_FRESH of kind FRESH put on the REST or ANSWER-BULK line is
       marked under its area, so FreshWaiting can tell a FRESH of that area still waits on this link. */
    void FreshPut(const char* f, size_t n, const Outbox& box)
    {
        const int line = box.LastPutLine();
        if (f == 0 || n < livefirst::kFrameHeader || (unsigned char)f[0] != (unsigned char)livefirst::kTypeAreaFresh || line < 0) return;
        areafresh::Msg m;
        if (!areafresh::Decode(f + livefirst::kFrameHeader, n - livefirst::kFrameHeader, &m) || m.kind != (unsigned int)areafresh::kFresh) return;
        const int k = areafresh::Key(m.x, m.y);
        if (k >= 0) { Mark& x = m_fresh[k]; x.line = line; x.seq = box.LastPutSeq(); }
    }
    bool FreshWaiting(int key, const Outbox& box)
    {
        std::map<int, Mark>::iterator m = m_fresh.find(key);
        if (m == m_fresh.end()) return false;
        if (box.LineGoneTo(m->second.line, m->second.seq)) { m_fresh.erase(m); return false; }
        return true;
    }
    /* T-1320 B: a RECORD of area `key` waits on the REST line (HoldsRest: as Holds before T-1320 - the area, any area, a WELCOME) / on
       the ANSWER-BULK line (OnAnswerBulk). Holds = either. */
    bool HoldsRest(int key, const Outbox& box)
    {
        const bool area = key >= 0 && Waiting(key, box);
        const bool any = Waiting(kHoldAnyArea, box);
        const bool welcome = Waiting(kHoldWelcome, box);
        return area || any || welcome;
    }
    bool OnAnswerBulk(int key, const Outbox& box) { return MarkWaits(&m_bulkMarks, key, livefirst::kLineAnswerBulk, box); }
    /* A CANT of area `key` (areafresh::Key) may not take the ANSWER line: something it may not pass still waits on the REST line (or,
       T-1320 B, a RECORD of its area on the ANSWER-BULK line). */
    bool Holds(int key, const Outbox& box)
    {
        const bool rest = HoldsRest(key, box);
        const bool bulk = key >= 0 && OnAnswerBulk(key, box);
        return rest || bulk;
    }
    size_t Marks() const { return m_marks.size() + m_bulkMarks.size() + m_fresh.size(); }   /* marks not yet found met (the offline suite) */
private:
    bool Waiting(int hold, const Outbox& box) { return MarkWaits(&m_marks, hold, livefirst::kLineRest, box); }
    /* T-1320 fold 1: a hold's marks - the put order of each of its frames still listed as waiting on one line, oldest first. A line hands
       must-deliver frames over only from its head, so the frames met (LineGoneTo) are always at the front of the list; one removed from
       the middle (Outbox::RecReplace) is struck by Forget. */
    typedef std::map<int, std::deque<unsigned long long> > MarkBook;
    static void MarkAdd(MarkBook* b, int hold, unsigned long long seq, int line, const Outbox& box)
    {
        std::deque<unsigned long long>& d = (*b)[hold];
        while (!d.empty() && box.LineGoneTo(line, d.front())) d.pop_front();   /* keeps the list to what still waits */
        if (seq == 0 || box.LineGoneTo(line, seq)) { if (d.empty()) b->erase(hold); return; }
        std::deque<unsigned long long>::iterator at = d.end();
        while (at != d.begin() && *(at - 1) > seq) --at;
        if (at != d.begin() && *(at - 1) == seq) return;   /* already listed (a record replaced in place) */
        d.insert(at, seq);
    }
    static bool MarkWaits(MarkBook* b, int hold, int line, const Outbox& box)
    {
        MarkBook::iterator m = b->find(hold);
        if (m == b->end()) return false;
        while (!m->second.empty() && box.LineGoneTo(line, m->second.front())) m->second.pop_front();
        if (m->second.empty()) { b->erase(m); return false; }
        return true;
    }
    static void MarkDrop(MarkBook* b, unsigned long long seq)
    {
        for (MarkBook::iterator m = b->begin(); m != b->end(); )
        {
            for (std::deque<unsigned long long>::iterator i = m->second.begin(); i != m->second.end(); ++i)
                if (*i == seq) { m->second.erase(i); break; }
            if (m->second.empty()) b->erase(m++); else ++m;
        }
    }
    MarkBook m_marks;       /* hold -> the put order of each of its frames put on the REST line and not yet found gone */
    MarkBook m_bulkMarks;   /* T-1320 B: area -> the put order of each of its RECORDs put on the ANSWER-BULK line and not yet found gone */
    struct Mark { int line; unsigned long long seq; Mark() : line(-1), seq(0) {} };
    std::map<int, Mark> m_fresh;                 /* T-1320 B / C: area -> the line and put order of its last FRESH put on this link */
    int m_open;                                  /* the hold of the frame being handed on in pieces */
    bool m_known;                                /* its first piece was seen here */
};

}   /* namespace coopsb */

#endif
