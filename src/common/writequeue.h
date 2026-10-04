/* M14 (T-197, more than two players, piece #2) - THE WORLD SERVER'S WRITE QUEUE, AS PURE BOOKKEEPING.
 *
 * SharedWastelandsServer.exe hands every record write to ONE writer thread (store_main.cpp, "M14 (T-197): THE WRITER
 * THREAD"), so its one loop never waits on a record's disk flushes. The two pieces that decide ORDER live
 * here - no Windows, no files, no threads - so the offline suite drives them directly (coop-test, m14_*):
 *
 *   KeyGate  LOOP SIDE. At most ONE job per record key is out (queued or being written) at any moment. A
 *            message for a key whose job is out is HELD, in arrival order, and handled only after that
 *            job's completion has been applied - so it is decided against the state the disk then holds,
 *            exactly as it was when every write finished before the next message was read.
 *   Fifo     SHARED. The jobs in the order they were queued, handed to the single writer ONE AT A TIME
 *            (a second job is not handed out while one is still being written), and a failed job is
 *            counted and followed by the next exactly as a successful one is.
 *
 * Together: two writes to one record's files are never in the queue at once, and they land in the order
 * their messages arrived; a later write can never land before an earlier one. The caller holds its own
 * lock around every Fifo call; KeyGate is touched by the loop thread only.
 */
#ifndef COOP_WRITEQUEUE_H
#define COOP_WRITEQUEUE_H

#include <cctype>
#include <deque>
#include <map>
#include <set>
#include <string>

namespace coopwq {

/* M14 fold (review finding 8): THE GATE IS KEYED ON THE FILE, NOT ON THE ID. A record's files are named by
   FileName(id) (store_main.cpp Sanitize calls this very function), and Windows file names ignore case, so two
   ids that differ only in a character FileName turns into '_' or only in case name ONE pair of files. GateKey
   = FileName, lower-cased: every id that lands on one file gets one gate key, so no two jobs for one file are
   ever out together. */
inline std::string FileName(const std::string& s)
{
    std::string o; for (size_t i = 0; i < s.size(); ++i) { char c = s[i]; o += (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') ? c : '_'; }
    return o;
}
inline std::string GateKey(const std::string& id)
{
    std::string k = FileName(id);
    for (size_t i = 0; i < k.size(); ++i) if (k[i] >= 'A' && k[i] <= 'Z') k[i] = (char)(k[i] - 'A' + 'a');
    return k;
}

/* M14 fold (review finding 3): WHAT A HELD MESSAGE IS, for the one merge the gate makes. A POSITION-ONLY update
   held right behind another position-only update for the same key, from the same connection, with a strictly
   NEWER stamp, REPLACES it: handled in turn, the older would be written and then overwritten by the newer (the
   loop refuses a position whose stamp is not newer than the record's, so an equal or older stamp is never
   merged - it is held and decided as before). A full record, a delete, another game, a new connection in the
   same slot or anything held in between is never merged - those are appended, in arrival order. */
enum { kHeldOther = 0, kHeldPosition = 1 };
struct HeldTag
{
    int kind; const void* sender; unsigned connect; long long stamp; long long bytes;
    double at;   /* M16: when it was held (the loop's clock, seconds); below 0 = never expires */
    std::string id;   /* M14 fold 2 (re-check C): the RECORD id - two ids can share one gate key ('Ab' / 'ab') */
    HeldTag() : kind(kHeldOther), sender(0), connect(0), stamp(0), bytes(0), at(-1.0) {}
};
/* M14 fold 2 (re-check C): the gate key is shared by every id that lands on one file, so a merge also needs the
   SAME record id - a position for 'ab' never replaces one held for 'Ab'. */
inline bool HeldMergeable(const HeldTag& older, const HeldTag& newer)
{
    return older.kind == kHeldPosition && newer.kind == kHeldPosition && older.sender == newer.sender
        && older.connect == newer.connect && older.id == newer.id && newer.stamp > older.stamp;
}

template <class Item>
class KeyGate
{
public:
    KeyGate() : m_held(0), m_heldMax(0), m_bytes(0), m_merged(0), m_expired(0) {}
    bool Busy(const std::string& key) const { return m_busy.find(key) != m_busy.end(); }
    /* False when the key already has a job out: the caller must HOLD its message instead. A second job for
       one key is never started - that is the whole of the per-file ordering rule on the loop's side. */
    bool Start(const std::string& key) { return m_busy.insert(key).second; }
    void Finish(const std::string& key) { m_busy.erase(key); }
    bool Hold(const std::string& key, const Item& item) { return Hold(key, item, HeldTag()); }
    /* True when the item REPLACED the newest one held for this key (HeldMergeable) instead of being appended. */
    bool Hold(const std::string& key, const Item& item, const HeldTag& tag)
    {
        std::deque<Entry>& q = m_wait[key];
        if (!q.empty() && HeldMergeable(q.back().tag, tag))
        {
            m_bytes += tag.bytes - q.back().tag.bytes;
            q.back().item = item; q.back().tag = tag;
            ++m_merged;
            return true;
        }
        Entry e; e.item = item; e.tag = tag;
        q.push_back(e);
        ++m_held; m_bytes += tag.bytes;
        if (m_held > m_heldMax) m_heldMax = m_held;
        return false;
    }
    /* The OLDEST held item for a key, and only while that key has no job out. */
    bool TakeHeld(const std::string& key, Item* out)
    {
        if (Busy(key)) return false;
        typename std::map<std::string, std::deque<Entry> >::iterator it = m_wait.find(key);
        if (it == m_wait.end() || it->second.empty()) return false;
        *out = it->second.front().item;
        m_bytes -= it->second.front().tag.bytes;
        it->second.pop_front();
        --m_held;
        if (it->second.empty()) m_wait.erase(it);
        return true;
    }
    /* M16 (T-197): THE ONE DROP THE HELD LIST MAKES. A held POSITION-ONLY update (kHeldPosition - the class its owner
       sends again: its next position update or its next full record supersedes it) held since before `cutoff` is
       removed and COUNTED (Expired); the rest keep their order. Any other kind - a full record, a delete - is never
       removed here: it waits, and the loop's backpressure (kHeldMax / kHeldBytesMax) bounds how many wait. */
    long long ExpireHeld(int kind, double cutoff)
    {
        if (kind != kHeldPosition) return 0;
        long long n = 0;
        typename std::map<std::string, std::deque<Entry> >::iterator it = m_wait.begin();
        while (it != m_wait.end())
        {
            std::deque<Entry> keep;
            for (size_t i = 0; i < it->second.size(); ++i)
            {
                const Entry& e = it->second[i];
                if (e.tag.kind == kHeldPosition && e.tag.at >= 0.0 && e.tag.at < cutoff) { m_bytes -= e.tag.bytes; --m_held; ++n; }
                else keep.push_back(e);
            }
            it->second.swap(keep);
            if (it->second.empty()) m_wait.erase(it++); else ++it;
        }
        m_expired += n;
        return n;
    }
    long long Expired() const { return m_expired; }        /* M16: held position updates expired, since start */
    long long BusyCount() const { return (long long)m_busy.size(); }
    long long HeldCount() const { return m_held; }
    long long HeldBytes() const { return m_bytes; }       /* the held items' tag.bytes, summed */
    long long Merged() const { return m_merged; }          /* position updates that replaced an older held one, since start */
    /* The most held at once since the previous call (one report line), restarting from now. */
    long long TakeHeldMax() { const long long m = m_heldMax; m_heldMax = m_held; return m; }
private:
    struct Entry { Item item; HeldTag tag; };
    std::set<std::string> m_busy;
    std::map<std::string, std::deque<Entry> > m_wait;
    long long m_held, m_heldMax, m_bytes, m_merged, m_expired;
};

/* M14 fold 2 (re-check A): THE DEFERRED EVENTS. While backpressure is on, the loop keeps servicing the network so
   every game's link stays alive, and each event it takes waits here, unhandled: handed out in arrival order, all
   of them, before anything newly read. HasFor names a connection slot that still has events waiting (a new
   connection into that slot must not be answered with an older one's replies). Discard removes one slot's
   events - ONLY for a connection the loop itself has closed (reset, no event), as the network layer would have
   thrown them away; the others keep their order. */
template <class Ev>
class DeferQueue
{
public:
    DeferQueue() : m_bytes(0), m_max(0), m_total(0) {}
    void Push(const Ev& e, const void* slot, long long bytes)
    {
        Entry x; x.ev = e; x.slot = slot; x.bytes = bytes;
        m_q.push_back(x);
        ++m_slot[slot]; m_bytes += bytes; ++m_total;
        if ((long long)m_q.size() > m_max) m_max = (long long)m_q.size();
    }
    bool Pop(Ev* out)
    {
        if (m_q.empty()) return false;
        *out = m_q.front().ev;
        Forget(m_q.front());
        m_q.pop_front();
        return true;
    }
    /* Every event of one slot removed (the rest in order); the removed events are given to `out` (their
       packets are the caller's to free). Returns how many. */
    long long Discard(const void* slot, std::deque<Ev>* out)
    {
        long long n = 0;
        std::deque<Entry> keep;
        for (size_t i = 0; i < m_q.size(); ++i)
        {
            if (m_q[i].slot == slot) { if (out != 0) out->push_back(m_q[i].ev); Forget(m_q[i]); ++n; }
            else keep.push_back(m_q[i]);
        }
        m_q.swap(keep);
        return n;
    }
    bool HasFor(const void* slot) const { return m_slot.find(slot) != m_slot.end(); }
    long long Count() const { return (long long)m_q.size(); }
    long long Bytes() const { return m_bytes; }
    long long Total() const { return m_total; }           /* every event deferred, since start */
    /* The most waiting at once since the previous call (one report line), restarting from now. */
    long long TakeMax() { const long long m = m_max; m_max = (long long)m_q.size(); return m; }
private:
    struct Entry { Ev ev; const void* slot; long long bytes; };
    void Forget(const Entry& x)
    {
        m_bytes -= x.bytes;
        std::map<const void*, long long>::iterator it = m_slot.find(x.slot);
        if (it != m_slot.end() && --it->second <= 0) m_slot.erase(it);
    }
    std::deque<Entry> m_q;
    std::map<const void*, long long> m_slot;
    long long m_bytes, m_max, m_total;
};

template <class Job>
class Fifo
{
public:
    Fifo() : m_inHand(0), m_maxDepth(0), m_jobs(0), m_failed(0) {}
    void Push(const Job& j)
    {
        m_q.push_back(j);
        const long long d = Depth();
        if (d > m_maxDepth) m_maxDepth = d;
    }
    /* ONE AT A TIME: nothing is handed out while the previous job is still in hand, so the writer cannot
       start job N+1 before job N has finished - even if a second writer were ever added by mistake. */
    bool Pop(Job* out)
    {
        if (m_inHand || m_q.empty()) return false;
        *out = m_q.front();
        m_q.pop_front();
        m_inHand = 1;
        return true;
    }
    /* A failure is COUNTED and changes nothing else: the next Pop hands out the next job as after a success. */
    void Done(bool ok) { m_inHand = 0; ++m_jobs; if (!ok) ++m_failed; }
    long long Depth() const { return (long long)m_q.size() + m_inHand; }   /* queued + the one being written */
    bool Idle() const { return Depth() == 0; }
    long long Jobs() const { return m_jobs; }
    long long Failed() const { return m_failed; }
    /* The deepest the queue has been since the previous call (one report line), restarting from now. */
    long long TakeMaxDepth() { const long long m = m_maxDepth; m_maxDepth = Depth(); return m; }
private:
    std::deque<Job> m_q;
    long long m_inHand, m_maxDepth, m_jobs, m_failed;
};

/* THE FINAL SAVE AT QUIT (rule 3): the two files the main loop rewrites whole on its once-a-second tick - the teams' standing
   records (teams.txt) and the world-relations table (world_relations.txt) - are saved once more when the process is asked to
   quit, before the write queue drains. They belong to the main loop's thread, so the save runs there: at once when the quit
   came on that thread (between start-up steps, or at the top of a loop pass); asked of the loop, and waited for, when it came on
   another thread (the console's quit events, the session-end window, the parent Kenshi's watch); and not at all while the
   loop has not started (start-up is still loading on that thread and no game has sent anything). */
enum { kQuitSaveHere = 0, kQuitSaveAskLoop = 1, kQuitSaveNone = 2 };
inline int QuitSaveRoute(bool onLoopThread, bool loopRunning) { return onLoopThread ? kQuitSaveHere : loopRunning ? kQuitSaveAskLoop : kQuitSaveNone; }
/* one file is saved at quit when it holds changes not yet on disk - unless it was on disk and unreadable at start, so it is
   never rewritten this session */
inline bool QuitSaveFile(bool dirty, bool loadDeferred) { return dirty && !loadDeferred; }

}   /* namespace coopwq */

#endif
