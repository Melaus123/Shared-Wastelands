/* src/common/pumpcarry.h - THE WORLD-SERVER LINK'S CARRY.
 *
 * One PumpLink call polls everything the world-server socket holds but handles only part of it (Stop, below, says where a
 * call ends). The rest waits here, in arrival order, and the next call takes from the
 * front. New messages are appended at the back; a payload is swapped in and out, never copied.
 *
 * A relayed MOVE is a LEVEL: only the newest position of a character matters. A MOVE for character uid U from game slot S
 * that arrives while an earlier MOVE for the same (S, U) still waits here overwrites that earlier message's payload and is
 * not appended, so a crowd of moving characters cannot grow the carry without bound. The EARLIER slot is kept, so a MOVE
 * never jumps ahead of an event message that arrived before it - the same rule the arrival queue applies to a relayed MOVE
 * (store.cpp InQueueEnqueue), keyed by the same test (cooplive::LiveDownDecode + cooplive::RelayedIsLevel, the uid in the
 * inner payload's first 4 bytes).
 *
 * Each keyed message's ABSOLUTE position (a running count of everything ever appended) is remembered; positions below the
 * running base have been taken off the front, so a popped message is never mistaken for a waiting one.
 *
 * Pure: the plugin's PumpLink and the offline suite compile this same code. No C++11. */
#ifndef COOP_COMMON_PUMPCARRY_H
#define COOP_COMMON_PUMPCARRY_H

#include <cstddef>
#include <cstring>
#include <deque>
#include <map>
#include <vector>
#include "liveenvelope.h"
#include "liverelay.h"

namespace pumpcarry {

/* The messages every PumpLink call handles before the time and saturation tests below can end it. */
const size_t kBatchMin = 64;

/* Whether one PumpLink call stops before taking its next message; handled = messages taken so far in this call.
   capTest > 0 (the test-only `pumpcap <n>` verb): stop once capTest messages are handled, whatever the time.
   Otherwise every call handles kBatchMin messages; past that it stops once usedUs (microseconds since the drain began)
   reaches budgetUs, or when saturatedBlocked - the arrival queue is near full while engine writes are blocked, the
   same brake PumpLink applies before polling - so a blocked stretch such as a load does not push more than kBatchMin a
   call into a nearly full queue. The test is made before each message, so the message in hand when the budget runs
   out is still finished. */
inline bool Stop(size_t handled, int capTest, long long usedUs, long long budgetUs, bool saturatedBlocked)
{
    if (capTest > 0) return handled >= (size_t)capTest;
    return handled >= kBatchMin && (usedUs >= budgetUs || saturatedBlocked);
}

struct Key
{
    unsigned int slot, innerType, uid;
    Key() : slot(0), innerType(0), uid(0) {}
    Key(unsigned int s, unsigned int t, unsigned int u) : slot(s), innerType(t), uid(u) {}
    bool operator<(const Key& o) const
    {
        if (slot != o.slot) return slot < o.slot;
        if (innerType != o.innerType) return innerType < o.innerType;
        return uid < o.uid;
    }
};

/* true (and *out) when a LIVE envelope (the store message's payload) carries a relayed LEVEL with a non-zero uid: the
   envelope decodes, cooplive::RelayedIsLevel says its inner type is one (a MOVE of at least 4 bytes), and the first 4 inner
   bytes - the character uid - are not 0. Anything else is an ordinary message that keeps its own place. */
inline bool LiveLevelKey(const char* p, size_t n, Key* out)
{
    if (out == 0) return false;
    cooplive::LiveDown d;
    if (!cooplive::LiveDownDecode(p, n, &d)) return false;
    if (!cooplive::RelayedIsLevel(d.innerType, d.innerLen)) return false;
    unsigned int uid = 0;
    std::memcpy(&uid, p + d.innerAt, 4);
    if (uid == 0) return false;
    *out = Key(d.originSlot, d.innerType, uid);
    return true;
}

/* M is a message with a std::vector<char> `payload` member (net::Message in the plugin). */
template <class M>
class Carry
{
public:
    Carry() : m_base(0), m_collapsed(0), m_max(0) {}
    size_t Size() const { return m_q.size(); }
    bool Empty() const { return m_q.empty(); }
    const M& At(size_t i) const { return m_q[i]; }
    long long Collapsed() const { return m_collapsed; }
    size_t MaxSize() const { return m_max; }

    /* Appends *m, whose payload is swapped out (m is left with an empty payload). keyed: m is a level with key k - if a
       level with the same key still waits, m's payload replaces that one's in its own slot and nothing is appended.
       Returns true when it collapsed. */
    bool Append(M* m, bool keyed, const Key& k)
    {
        if (keyed)
        {
            typename std::map<Key, unsigned long long>::iterator it = m_at.find(k);
            if (it != m_at.end())
            {
                if (it->second >= m_base && it->second < m_base + (unsigned long long)m_q.size())
                {
                    m_q[(size_t)(it->second - m_base)].payload.swap(m->payload);
                    m->payload.clear();
                    ++m_collapsed;
                    return true;
                }
                m_at.erase(it);
            }
        }
        std::vector<char> body;
        body.swap(m->payload);
        m_q.push_back(*m);
        m_q.back().payload.swap(body);
        if (keyed) m_at[k] = m_base + (unsigned long long)(m_q.size() - 1);
        if (m_q.size() > m_max) m_max = m_q.size();
        return false;
    }

    /* Takes the front message into *out (its payload swapped, not copied). false when the carry is empty. */
    bool PopFront(M* out)
    {
        if (out == 0 || m_q.empty()) return false;
        std::vector<char> body;
        body.swap(m_q.front().payload);
        *out = m_q.front();
        out->payload.swap(body);
        m_q.pop_front();
        ++m_base;
        if (m_q.empty()) m_at.clear();   /* nothing waits, so no remembered position can point at a waiting message */
        return true;
    }

    /* Everything waiting is dropped; the base moves past it so no remembered position survives. */
    void Clear()
    {
        m_base += (unsigned long long)m_q.size();
        m_q.clear();
        m_at.clear();
    }

    /* The index (from the front) of the waiting level with key k, or -1 when none waits. */
    long long IndexOf(const Key& k) const
    {
        typename std::map<Key, unsigned long long>::const_iterator it = m_at.find(k);
        if (it == m_at.end() || it->second < m_base || it->second >= m_base + (unsigned long long)m_q.size()) return -1;
        return (long long)(it->second - m_base);
    }

private:
    std::deque<M> m_q;
    std::map<Key, unsigned long long> m_at;
    unsigned long long m_base;
    long long m_collapsed;
    size_t m_max;
};

}   /* namespace pumpcarry */

#endif
