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
#include <vector>
#include "livefirst.h"   /* T-1118 (b): the live-first mode - CHUNK, the lines, the barriers, the budget */

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
    Item() : cls(kClassMust), type(0), channel(0), line(livefirst::kLineRest), barrier(false), len(0), p199At(0.0), bySender(false), origin(0), kind(0), listOnly(false) {}
};

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
     frame here (SetNow gives the clock), the BULK line's counts. */
class Outbox
{
public:
    Outbox() : m_count(0), m_bytes(0), m_nextMark(kWaitLogFirst), m_was(false), m_liveFirst(false), m_budget(kNetBytesMax), m_now(0.0), m_seq(0), m_lastDirectBulk(false) { std::memset(m_restOf, 0, sizeof(m_restOf)); }
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
        else if (!loose && m_liveFirst && it.line == livefirst::kLineLive)   /* after any Unlink above */
        {
            why = BulkWhy(it, true);
            which = why != livefirst::kBulkNot ? kListBulk : (Gated(it) ? kListHeld : kListLive);
            WindowAdd(it);
        }
        std::list<Entry>& q = ListOf(which);
        q.push_back(Entry());
        Entry& e = q.back();
        e.it = it; e.seq = ++m_seq; e.at = m_now; e.sent = 0;
        e.passed = (which == kListLive || which == kListHeld) && !m_rest.empty();
        if (which == kListAnnounce) { AnnNote(e, true); ++m_stats.announceQueued; }
        if (which == kListHeld) { if (GatedByBulk(it)) ++m_stats.bulkHeld; HeldNote(e, 1); ++m_stats.gateHeld; }
        if (which == kListBulk) { e.it.line = livefirst::kLineBulk; BulkNote(e, true); ++m_stats.bulkQueued; BulkWhyCount(why); }
        ++m_count; m_bytes += (long long)it.bytes.size();
        if (keepReliable) e.it.channel = kChannelReliable;
        if (which == kListRest && m_liveFirst && it.barrier) m_barriers.push_back(e.seq);
        if (which == kListRest) ++m_restOf[it.type & 0xFF];
        if (it.cls == kClassLatest) { Slot s; s.list = which; s.at = q.end(); --s.at; m_latest[it.key] = s; }
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
    void DrainStats(livefirst::Stats* into)
    {
        if (into != 0) into->Add(m_stats);
        m_stats = livefirst::Stats();
    }
private:
    enum { kListLoose = 0, kListLive = 1, kListRest = 2, kListAnnounce = 3, kListHeld = 4, kListBulk = 5 };
    struct Entry
    {
        Item it; unsigned long long seq; double at; size_t sent; bool passed;   /* sent = bytes already cut off in CHUNKs; passed = REST frames waited when it was put */
        Entry() : seq(0), at(0.0), sent(0), passed(false) {}
    };
    struct Slot { int list; std::list<Entry>::iterator at; };   /* which list and where */
    typedef std::map<Key, Slot> Index;
    std::list<Entry>& ListOf(int which)
    {
        return which == kListLoose ? m_loose : (which == kListLive ? m_live : (which == kListAnnounce ? m_announce
               : (which == kListHeld ? m_held : (which == kListBulk ? m_bulk : m_rest))));
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
        if (which == kListRest && !m_barriers.empty() && m_barriers.front() == f->seq) m_barriers.pop_front();
        if (which == kListRest) --m_restOf[f->it.type & 0xFF];
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
        if (!m_held.empty() && m_announce.empty()) ReleaseHeld();   /* nothing left to wait for (each announcement's leaving already released them) */
        if (!m_live.empty())
        {
            if (m_barriers.empty() || m_live.front().seq < m_barriers.front())
            {
                if (m_live.front().it.channel == kChannelReliable && netBytes > m_stats.liveAheadMaxBytes) m_stats.liveAheadMaxBytes = netBytes;
                return TakeWhole(kListLive, out);
            }
            ++m_stats.barrierHeld;
        }
        /* the ANNOUNCE line: before REST, a frame starting only when it fits the budget (Fits); one held by a barrier lets REST drain up to that barrier */
        if (!m_announce.empty() && (m_barriers.empty() || m_announce.front().seq < m_barriers.front())
            && !AnnBlocked(m_announce.front().it, m_announce.front().seq))   /* review HIGH 1: else the BULK frame it waits for goes first */
            return (any || Fits(netBytes, m_announce.front().it)) ? TakeWhole(kListAnnounce, out) : false;
        /* T-1118 (c) the BULK line: after ANNOUNCE, before REST, a frame starting only when it fits the budget (Fits), held by a barrier as ANNOUNCE is */
        if (!m_bulk.empty() && (m_barriers.empty() || m_bulk.front().seq < m_barriers.front()))
            return (any || Fits(netBytes, m_bulk.front().it)) ? TakeWhole(kListBulk, out) : false;
        if (m_rest.empty() || (!any && netBytes >= m_budget)) return false;
        Entry& h = m_rest.front();
        const size_t whole = h.it.bytes.size();
        if (h.sent == 0 && (!livefirst::CutsInto(whole) || whole > livefirst::kJoinMax || h.it.type == livefirst::kMsgChunk)) return TakeWhole(kListRest, out);   /* over 32 MiB: whole - ENet refuses it, counted by the caller; a CHUNK handed on is never cut again */
        if (h.sent == 0)
        {
            ++m_stats.cutFrames;
            if (h.it.cls == kClassLatest)   /* from its first piece on it can no longer be replaced */
            {
                Index::iterator m = m_latest.find(h.it.key);
                if (m != m_latest.end() && m->second.at == m_rest.begin()) m_latest.erase(m);
            }
        }
        std::vector<char> c;
        const size_t n = livefirst::ChunkEncodeUpTo(&h.it.bytes[0], whole, h.sent, livefirst::PieceBytes(m_budget, netBytes, any), &c);
        if (n == 0) return TakeWhole(kListRest, out);   /* cannot happen (the checks above); never loops */
        h.sent += n; m_bytes -= (long long)n; ++m_stats.chunks;
        out->cls = kClassMust; out->key = Key(); out->type = livefirst::kMsgChunk; out->channel = kChannelReliable; out->bytes.swap(c);
        out->line = livefirst::kLineRest; out->barrier = false; out->len = 0;
        // PROBE-START: P199 - a record piece carries no live stamp
        out->p199At = 0.0;
        // PROBE-END: P199
        if (h.sent >= whole)
        {
            if (!m_barriers.empty() && m_barriers.front() == h.seq) m_barriers.pop_front();
            --m_restOf[h.it.type & 0xFF];
            m_rest.pop_front(); --m_count;
            if (m_count == 0) m_nextMark = kWaitLogFirst;
        }
        return true;
    }
    /* Removes one waiting latest-value entry and its index entry (never one partly cut: its index entry is gone by then). */
    void Unlink(Index::iterator f)
    {
        std::list<Entry>& q = ListOf(f->second.list);
        m_bytes -= (long long)(f->second.at->it.bytes.size() - f->second.at->sent); --m_count;
        if (f->second.at->it.barrier)
            for (std::deque<unsigned long long>::iterator b = m_barriers.begin(); b != m_barriers.end(); ++b)
                if (*b == f->second.at->seq) { m_barriers.erase(b); break; }
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
    SeqBook m_bulkOf, m_bulkFrom;  /* the waiting BULK frames' put order per character named / per sender (BulkNote) */
    KindBook m_bulkWait;           /* BULK frames waiting per kind of each sender (sticky) */
    SeqBook m_bulkBare;            /* the waiting no-character BULK frames' put order per sender and order family (BareKey) */
    CountBook m_heldBare;          /* held no-character frames per sender and order family (BareKey) */
    bool m_lastDirectBulk;         /* LastDirectBulk */
    WindowBook m_window;           /* the bytes each kind of each sender put here within the last kBulkWindowSec */
    std::deque<unsigned long long> m_barriers;   /* T-1118 (b): the put order (seq) of each barrier waiting on the REST line, oldest first */
    Index m_latest;             /* the one waiting entry of each latest-value subject */
    long long m_count, m_bytes, m_nextMark;
    bool m_was;
    bool m_liveFirst; long long m_budget; double m_now; unsigned long long m_seq;
    livefirst::Stats m_stats;
    long long m_restOf[256];    /* frames waiting on the REST line, by type (RestWaitingOf) */
};

}   /* namespace coopsb */

#endif
