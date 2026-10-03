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
#include <list>
#include <map>
#include <vector>

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
    Item() : cls(kClassMust), type(0), channel(0) {}
};

class Outbox
{
public:
    Outbox() : m_count(0), m_bytes(0), m_nextMark(kWaitLogFirst), m_was(false) {}
    static bool NetFull(long long netCount, long long netBytes) { return netCount >= kNetCountMax || netBytes >= kNetBytesMax; }
    static bool Reliable(int channel) { return channel == kChannelReliable; }
    /* Straight to the network now? Never while the network queue is full. A reliable message only while nothing waits here
       (it may never overtake one that waits). An unreliable one (cls, key and channel of `it` are read; its bytes are not)
       passes the waiting line, unless a reliable value of its subject waits - then it must merge into that one (Put). An
       older unreliable value of its subject that still waits is removed here (*replaced = true): it is out of date and
       would arrive after this one. */
    bool Direct(long long netCount, long long netBytes, const Item& it, bool* replaced = 0)
    {
        if (replaced) *replaced = false;
        if (NetFull(netCount, netBytes)) return false;
        if (Reliable(it.channel)) return m_count == 0;
        if (it.cls == kClassLatest)
        {
            Index::iterator f = m_latest.find(it.key);
            if (f != m_latest.end())
            {
                if (!f->second.loose) return false;
                Unlink(f);
                if (replaced) *replaced = true;
            }
        }
        return true;
    }
    /* Appends: a reliable message to the back of the ordered line, an unreliable one to the unordered list. True when it
       REPLACED an older waiting value for the same latest-value subject (that one is gone). */
    bool Put(const Item& it)
    {
        bool replaced = false, keepReliable = false;
        if (it.cls == kClassLatest)
        {
            Index::iterator f = m_latest.find(it.key);
            if (f != m_latest.end())
            {
                keepReliable = !f->second.loose;
                Unlink(f);
                replaced = true;
            }
        }
        const bool loose = !keepReliable && !Reliable(it.channel);   /* a newer unreliable value never makes a waiting reliable one (a sector crossing) unreliable */
        std::list<Item>& q = loose ? m_loose : m_q;
        q.push_back(it); ++m_count; m_bytes += (long long)it.bytes.size();
        if (keepReliable) q.back().channel = kChannelReliable;
        if (it.cls == kClassLatest) { Slot s; s.loose = loose; s.at = q.end(); --s.at; m_latest[it.key] = s; }
        return replaced;
    }
    /* The next waiting message (as TakeAny), only while the network queue has room. */
    bool Take(long long netCount, long long netBytes, Item* out)
    {
        if (m_count == 0 || NetFull(netCount, netBytes)) return false;
        return TakeAny(out);
    }
    /* The next waiting message regardless of room (a graceful close hands everything to the network first): the oldest
       unreliable one while any waits (they promise no order), else the oldest of the reliable line. */
    bool TakeAny(Item* out)
    {
        if (m_count == 0) return false;
        std::list<Item>& q = m_loose.empty() ? m_q : m_loose;
        std::list<Item>::iterator f = q.begin();
        if (f->cls == kClassLatest)
        {
            Index::iterator m = m_latest.find(f->key);
            if (m != m_latest.end() && m->second.at == f) m_latest.erase(m);
        }
        m_bytes -= (long long)f->bytes.size(); --m_count;
        out->cls = f->cls; out->key = f->key; out->type = f->type; out->channel = f->channel; out->bytes.swap(f->bytes);
        q.erase(f);
        if (m_count == 0) m_nextMark = kWaitLogFirst;
        return true;
    }
    /* The link is gone: every waiting message is thrown away with it. Returns how many. */
    long long Clear()
    {
        const long long n = m_count;
        m_q.clear(); m_loose.clear(); m_latest.clear(); m_count = 0; m_bytes = 0; m_nextMark = kWaitLogFirst;
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
private:
    struct Slot { bool loose; std::list<Item>::iterator at; };   /* which list (m_loose or m_q) and where */
    typedef std::map<Key, Slot> Index;
    /* Removes one waiting latest-value entry and its index entry. */
    void Unlink(Index::iterator f)
    {
        std::list<Item>& q = f->second.loose ? m_loose : m_q;
        m_bytes -= (long long)f->second.at->bytes.size(); --m_count;
        q.erase(f->second.at); m_latest.erase(f);
        if (m_count == 0) m_nextMark = kWaitLogFirst;
    }
    std::list<Item> m_q;       /* reliable messages, in the order they were sent */
    std::list<Item> m_loose;   /* unreliable messages that wait only because the network queue was full */
    Index m_latest;            /* the one waiting entry of each latest-value subject */
    long long m_count, m_bytes, m_nextMark;
    bool m_was;
};

}   /* namespace coopsb */

#endif
