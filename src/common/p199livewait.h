// PROBE-START: P199 - how long a live message really waits from being handed to the transport until it leaves the process, per link (log only)
// Row T-1118, branch effort/t1118g (never merged). LOG ONLY: nothing sent over the network changes, no message format changes.
// Revision P199b (after T1701): per-window SLOW lines, every stamp accounted for, p95, finer buckets above 100 ms.
// One Link per transport link holds a fixed ring of kRing stamps: a live message (one the live-first code puts on its LIVE line,
// either lane) is stamped when it is handed to the transport (Send) - or, when it waited in the link's line first, with the time
// it was put there (sendbound.h Item.p199At) - and the stamp is closed when the message has left the process:
//   - ENet (the game's own world-server link, the bridge's ENet links, the world server's link to each game): the stamp follows
//     the message into the packet that carries it (its own, or the bundle it joined) and closes when a walk of the peer's two
//     outgoing lists, made right after each ENet call that transmits, no longer finds that packet (how=walk). A packet ENet frees
//     first closes there when it carries flag SENT (on the wire or acknowledged). A packet freed WITHOUT flag SENT never left:
//     while the peer is still up that is ENet's throttle dropping an unreliable packet (protocol.c, packetThrottleCounter) -
//     counted dropped; once ENet has reset the peer (state DISCONNECTED before its queues are emptied, peer.c enet_peer_reset)
//     it went with the link - counted linkDropped. The world server has no free callback: a packet its ENet throttle dropped
//     leaves the lists and is counted as timed there (approximate).
//   - Steam: a byte watermark (how=bytes) - each message's mark is the running total of bytes Steam took on that link once its
//     last piece was taken; at each status read, bytes sent = that total minus Steam's pending reliable + unreliable bytes, and
//     a message whose mark is at or under it has left. Approximate: Steam counts its own bytes, may drop an unreliable message
//     it could not send (counted as sent), and sends its two lanes in its own order; the read is once per Poll, so every stamp
//     handed over between two reads is open together (why the ring is 1024 - T1701's 64 filled).
// EVERY STAMP ENDS AS EXACTLY ONE OF: timed (n), still open at the REPORT (open; the oldest one's age openMaxMs is a real lower
// bound of a wait still in progress, openMaxAt says where it waits: new / bundle / queue / out), lost with the link
// (linkDropped), dropped by the transport while the link was up (dropped: ENet's throttle, a refused packet / bundle / Steam
// send), or open over kStaleSec and given up (stale). A live message the ring had no room for is never stamped (noSlot).
// unresolved = open + linkDropped + dropped + stale + noSlot (a window's losses plus what is still open at its end - an open
// stamp is carried into the next window, so unresolved must not be summed across windows). The line checks
// carried + stamped = n + dropped + linkDropped + stale + open (carried = open at the window's start): leak = the difference, 0.
// Each window (one REPORT): count, the exact maximum, kBuckets time buckets for p50 / p90 / p95 (a percentile answers the top
// of the bucket holding its rank, never above the maximum; buckets are 25-50 ms wide from 100 to 500 ms, 100-200 ms from 500
// to 2000 ms), waits over 200 ms and of 500 ms or more. SLOW lines, per window per link: the kTopSlow slowest waits (rank=1..)
// and every wait of kBigMs or more up to kKeepBig (rank=0, those not already ranked), with the window's length (winSecs) and
// when the wait began, in seconds after the window's start (atSec; negative = handed over in an earlier window). A process
// prints at most kSlowLinesMax SLOW lines (a safety cap); lines it stops are counted (slowCapped, the process total).
// Threading: a Link has no lock of its own - each caller already serialises it (the ENet transport's link lock, the Steam
// transport's main thread, the world server's loop thread). Only the process-wide SLOW counts are shared, and they are atomic.
#ifndef P199LIVEWAIT_H
#define P199LIVEWAIT_H

#include <windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "livefirst.h"

namespace p199 {

const int kRing = 1024;            // pending stamps per link (P199b: 64 filled between Steam's status reads)
const int kTopSlow = 3;            // the slowest waits kept per link per window
const int kKeepBig = 10;           // waits of kBigMs or more kept per link per window
const long long kBigMs = 500;
const int kSlowLinesMax = 2000;    // SLOW lines per process, ever - a safety cap; the lines it stops are counted
const long long kSlowMs = 200;
const double kStaleSec = 60.0;     // a stamp open this long is counted stale and dropped
const int kBuckets = 32;           // 31 bounded buckets (ms) and one over 5 s

inline long long BucketTop(int i)
{
    static const long long t[kBuckets - 1] = { 1, 2, 5, 10, 20, 30, 50, 75, 100, 125, 150, 175, 200, 250, 300, 350, 400, 450, 500,
                                               600, 700, 800, 900, 1000, 1200, 1400, 1600, 1800, 2000, 3000, 5000 };
    return (i >= 0 && i < kBuckets - 1) ? t[i] : -1;
}
inline int BucketOf(long long ms)
{
    for (int i = 0; i < kBuckets - 1; ++i) if (ms <= BucketTop(i)) return i;
    return kBuckets - 1;
}
// The p-th percentile (1..100) of n waits counted in b: the top of the bucket holding that rank, never above maxMs (the last
// bucket, over 5 s, answers maxMs). 0 when n is 0.
inline long long Percentile(const long long* b, long long n, long long maxMs, int p)
{
    if (n <= 0) return 0;
    long long rank = (n * (long long)p + 99) / 100;
    if (rank < 1) rank = 1;
    long long c = 0;
    for (int i = 0; i < kBuckets; ++i)
    {
        c += b[i];
        if (c >= rank) { const long long top = BucketTop(i); return (top < 0 || top > maxMs) ? maxMs : top; }
    }
    return maxMs;
}
inline double Now()
{
    LARGE_INTEGER c, f; ::QueryPerformanceCounter(&c); ::QueryPerformanceFrequency(&f);
    return f.QuadPart > 0 ? (double)c.QuadPart / (double)f.QuadPart : 0.0;
}
// The inner kind byte of a LIVE envelope (53) as livefirst.h LineOf reads it; 0 for any other frame.
inline unsigned int InnerOf(unsigned int type, const char* payload, size_t n, int dir)
{
    if (type != livefirst::kTypeLive || payload == 0) return 0;
    if (dir == livefirst::kDirUp) return n >= livefirst::kLiveUpHeader ? (unsigned int)(unsigned char)payload[1] : 0u;
    if (dir == livefirst::kDirDown) return n >= livefirst::kLiveDownHeader ? (unsigned int)(unsigned char)payload[2] : 0u;
    return 0;
}
inline unsigned int InnerOfFrame(const std::vector<char>& f, int dir)
{
    if (f.size() <= livefirst::kFrameHeader) return 0;
    return InnerOf((unsigned int)(unsigned char)f[0], &f[livefirst::kFrameHeader], f.size() - livefirst::kFrameHeader, dir);
}
inline volatile LONG* SlowLines() { static volatile LONG n = 0; return &n; }
inline volatile LONG* SlowCapped() { static volatile LONG n = 0; return &n; }

// kNew: handed over, not yet in a packet or queue; kHeld: in a lane's open bundle; kQueued: in a Steam link's own queue (mark =
// that queue's pumped-bytes total once its last piece is pumped); kOut: in an ENet packet (pkt) or with Steam (pkt 0, mark =
// bytes Steam took through its last piece).
enum { kFree = 0, kNew = 1, kHeld = 2, kQueued = 3, kOut = 4 };
inline const char* StateName(int st)
{
    switch (st) { case kNew: return "new"; case kHeld: return "bundle"; case kQueued: return "queue"; case kOut: return "out"; }
    return "none";
}
// Why a stamp ended without a time.
enum { kLostDropped = 0, kLostLink = 1, kLostStale = 2 };
struct Stamp { double t0; const void* pkt; unsigned long long mark; long long ahead; unsigned int type, inner; int state, lane; bool seen; };
struct Slow { long long ms, ahead, seq; double t0; unsigned int type, inner; };
// One stamped packet in a walk, ordered by address so each command in ENet's lists is found by a binary search.
struct WalkRow { size_t pkt; int i; };
inline bool WalkLess(const WalkRow& a, const WalkRow& b) { return a.pkt < b.pkt; }

class Link
{
public:
    Link() : open_(0), hint_(0), queuedN_(0), walkN_(0), last_(0), seq_(0)
    {
        std::memset(s_, 0, sizeof(s_));
        for (int i = 0; i < kRing; ++i) s_[i].state = kFree;
        Restart(Now());
        // PROBE-START: P223
        p223On = false; p223N = 0; p223Lost = 0;
        // PROBE-END: P223
    }
    // PROBE-START: P223 - waits of 300 ms or more closed since the caller last emptied the list (the world server's links only). LOG ONLY.
    enum { kP223Keep = 64 };
    bool p223On; int p223N; long long p223Lost; Slow p223Slow[kP223Keep];
    // PROBE-END: P223
    // A live message handed over at t0: its stamp (-1 = the ring is full; counted noSlot). ahead = bytes it waits behind.
    int Open(double t0, unsigned int type, unsigned int inner, long long ahead)
    {
        for (int k = 0; k < kRing; ++k)
        {
            const int i = (hint_ + k) % kRing;
            if (s_[i].state != kFree) continue;
            Stamp& e = s_[i];
            e.t0 = t0; e.pkt = 0; e.mark = 0; e.ahead = ahead; e.type = type; e.inner = inner; e.state = kNew; e.lane = -1; e.seen = false;
            ++open_; ++stamped_;
            hint_ = (i + 1) % kRing;
            return i;
        }
        ++noSlot_;
        return -1;
    }
    bool Any() const { return open_ > 0; }
    void Hold(int i, int lane) { if (Is(i, kNew)) { s_[i].state = kHeld; s_[i].lane = lane; } }
    void OnPacket(int i, const void* pkt) { if (Is(i, kNew) && pkt != 0) { s_[i].state = kOut; s_[i].pkt = pkt; } }
    void LaneOnPacket(int lane, const void* pkt)
    {
        if (open_ == 0 || pkt == 0) return;
        for (int i = 0; i < kRing; ++i)
            if (s_[i].state == kHeld && s_[i].lane == lane) { s_[i].state = kOut; s_[i].pkt = pkt; }
    }
    // A bundle ENet refused: its messages are dropped.
    void LoseLane(int lane)
    {
        if (open_ == 0) return;
        for (int i = 0; i < kRing; ++i) if (s_[i].state == kHeld && s_[i].lane == lane) Lose(i, kLostDropped);
    }
    // After a hand-over: a stamp still new was refused (ENet / Steam would not take the frame) - dropped.
    void LoseIfNew(int i) { if (Is(i, kNew)) Lose(i, kLostDropped); }
    // The link is gone: what had not reached ENet goes with it (an ENet packet's stamps go when ENet frees it unsent).
    void LoseNotOut()
    {
        if (open_ == 0) return;
        for (int i = 0; i < kRing; ++i) if (s_[i].state == kNew || s_[i].state == kHeld || s_[i].state == kQueued) Lose(i, kLostLink);
    }
    void LoseAll()
    {
        if (open_ == 0) return;
        for (int i = 0; i < kRing; ++i) if (s_[i].state != kFree) Lose(i, kLostLink);
    }
    // ENet: right after a transmitting call - WalkBegin, Seen for each command in the peer's two outgoing lists, WalkEnd.
    void WalkBegin()
    {
        walkN_ = 0; last_ = 0;
        if (open_ == 0) return;
        for (int i = 0; i < kRing; ++i)
            if (s_[i].state == kOut && s_[i].pkt != 0) { s_[i].seen = false; walk_[walkN_].pkt = (size_t)s_[i].pkt; walk_[walkN_].i = i; ++walkN_; }
        if (walkN_ > 1) std::sort(walk_, walk_ + walkN_, WalkLess);
    }
    bool Walking() const { return walkN_ > 0; }
    void Seen(const void* pkt)
    {
        if (pkt == 0 || pkt == last_) return;   // a packet's fragments are consecutive
        last_ = pkt;
        const size_t key = (size_t)pkt;
        int lo = 0, hi = walkN_;
        while (lo < hi) { const int mid = lo + (hi - lo) / 2; if (walk_[mid].pkt < key) lo = mid + 1; else hi = mid; }
        for (int k = lo; k < walkN_ && walk_[k].pkt == key; ++k) s_[walk_[k].i].seen = true;
    }
    void WalkEnd(double at)
    {
        for (int k = 0; k < walkN_; ++k)
        {
            const int i = walk_[k].i;
            if (s_[i].state == kOut && s_[i].pkt != 0 && !s_[i].seen) Close(i, at);
        }
        walkN_ = 0; last_ = 0;
    }
    // ENet's free callback: sent = the packet carried flag SENT (on the wire or acknowledged); otherwise it never left -
    // dropped by ENet's throttle while the peer is up (linkUp), gone with the link once ENet has reset the peer.
    void Freed(const void* pkt, bool sent, bool linkUp)
    {
        if (open_ == 0 || pkt == 0) return;
        double now = 0.0;
        for (int i = 0; i < kRing; ++i)
        {
            if (s_[i].state != kOut || s_[i].pkt != pkt) continue;
            if (!sent) { Lose(i, linkUp ? kLostDropped : kLostLink); continue; }
            if (now == 0.0) now = Now();
            Close(i, now);
        }
    }
    // Steam: into the link's own queue (qMark = pumped-bytes total once its last piece is pumped); pumped to Steam; taken
    // by Steam at once (an unreliable message); Steam has sent `sent` bytes in all.
    void Queued(int i, unsigned long long qMark) { if (Is(i, kNew)) { s_[i].state = kQueued; s_[i].mark = qMark; ++queuedN_; } }
    void Pumped(unsigned long long pumped, unsigned long long handed)
    {
        if (queuedN_ <= 0) return;
        for (int i = 0; i < kRing; ++i)
            if (s_[i].state == kQueued && s_[i].mark <= pumped) { s_[i].state = kOut; s_[i].pkt = 0; s_[i].mark = handed; --queuedN_; }
    }
    void OutAt(int i, unsigned long long mark) { if (Is(i, kNew)) { s_[i].state = kOut; s_[i].pkt = 0; s_[i].mark = mark; } }
    void SentUpTo(unsigned long long sent)
    {
        if (open_ == 0) return;
        double now = 0.0;
        for (int i = 0; i < kRing; ++i)
        {
            if (s_[i].state != kOut || s_[i].pkt != 0 || s_[i].mark > sent) continue;
            if (now == 0.0) now = Now();
            Close(i, now);
        }
    }
    // The window's LIVEWAIT line and its SLOW lines (the window's slowest, then its other waits of kBigMs or more), then a new window.
    void Report(const char* link, const char* how, long long rttMs, double now, std::vector<std::string>* out)
    {
        for (int i = 0; i < kRing; ++i)
            if (s_[i].state != kFree && now - s_[i].t0 > kStaleSec) Lose(i, kLostStale);
        long long openMaxMs = 0; int openMaxAt = kFree;
        for (int i = 0; i < kRing; ++i)
        {
            if (s_[i].state == kFree) continue;
            long long a = (long long)((now - s_[i].t0) * 1000.0 + 0.5);
            if (a < 0) a = 0;
            if (openMaxAt == kFree || a > openMaxMs) { openMaxMs = a; openMaxAt = s_[i].state; }
        }
        long long secs = (long long)(now - since_ + 0.5);
        if (secs < 0) secs = 0;
        const long long unresolved = (long long)open_ + linkDropped_ + dropped_ + stale_ + noSlot_;
        const long long leak = carried_ + stamped_ - (cnt_ + dropped_ + linkDropped_ + stale_ + (long long)open_);
        char line[720];
        _snprintf(line, sizeof(line) - 1, "[PROBE] P199 LIVEWAIT link=%s secs=%lld n=%lld maxMs=%lld p50Ms=%lld p90Ms=%lld over200=%lld unresolved=%lld how=%s rttMs=%lld open=%d"
                  " p95Ms=%lld over500=%lld openMaxMs=%lld openMaxAt=%s linkDropped=%lld dropped=%lld stale=%lld noSlot=%lld stamped=%lld carried=%lld leak=%lld slowCapped=%ld",
                  link, secs, cnt_, max_, Percentile(b_, cnt_, max_, 50), Percentile(b_, cnt_, max_, 90), over_, unresolved, how, rttMs, open_,
                  Percentile(b_, cnt_, max_, 95), over500_, openMaxMs, StateName(openMaxAt), linkDropped_, dropped_, stale_, noSlot_, stamped_, carried_, leak,
                  (long)*SlowCapped());
        line[sizeof(line) - 1] = 0;
        out->push_back(line);
        for (int k = 0; k < topN_; ++k) SlowLine(link, top_[k], k + 1, secs, out);
        for (int k = 0; k < bigN_; ++k)
        {
            bool ranked = false;
            for (int j = 0; j < topN_; ++j) if (top_[j].seq == big_[k].seq) ranked = true;
            if (!ranked) SlowLine(link, big_[k], 0, secs, out);
        }
        Restart(now);
    }
private:
    bool Is(int i, int st) const { return i >= 0 && i < kRing && s_[i].state == st; }
    void Free(int i) { if (s_[i].state == kQueued) --queuedN_; s_[i].state = kFree; s_[i].pkt = 0; --open_; }
    void Lose(int i, int why)
    {
        if (why == kLostLink) ++linkDropped_; else if (why == kLostStale) ++stale_; else ++dropped_;
        Free(i);
    }
    void Close(int i, double now)
    {
        long long ms = (long long)((now - s_[i].t0) * 1000.0 + 0.5);
        if (ms < 0) ms = 0;
        ++cnt_; ++b_[BucketOf(ms)];
        if (ms > max_) max_ = ms;
        if (ms > kSlowMs) ++over_;
        Slow w; w.ms = ms; w.ahead = s_[i].ahead; w.seq = ++seq_; w.t0 = s_[i].t0; w.type = s_[i].type; w.inner = s_[i].inner;
        // PROBE-START: P223 - a slow wait kept for the world server's P223 line (only where the caller turned it on; it empties the list after each walk)
        if (p223On && ms >= 300) { if (p223N < kP223Keep) p223Slow[p223N++] = w; else ++p223Lost; }
        // PROBE-END: P223
        if (ms >= kBigMs) { ++over500_; if (bigN_ < kKeepBig) big_[bigN_++] = w; }
        if (topN_ < kTopSlow || ms > top_[topN_ - 1].ms)
        {
            int k = topN_ < kTopSlow ? topN_++ : kTopSlow - 1;
            while (k > 0 && top_[k - 1].ms < ms) { top_[k] = top_[k - 1]; --k; }
            top_[k] = w;
        }
        Free(i);
    }
    void SlowLine(const char* link, const Slow& w, int rank, long long secs, std::vector<std::string>* out)
    {
        if (::InterlockedIncrement(SlowLines()) > (LONG)kSlowLinesMax) { ::InterlockedIncrement(SlowCapped()); return; }
        char line[320];
        _snprintf(line, sizeof(line) - 1, "[PROBE] P199 SLOW link=%s waitMs=%lld type=%u inner=%u bytesAhead=%lld rank=%d atSec=%.1f winSecs=%lld",
                  link, w.ms, w.type, w.inner, w.ahead, rank, w.t0 - since_, secs);
        line[sizeof(line) - 1] = 0;
        out->push_back(line);
    }
    void Restart(double now)
    {
        cnt_ = 0; max_ = 0; over_ = 0; over500_ = 0; linkDropped_ = 0; dropped_ = 0; stale_ = 0; noSlot_ = 0; stamped_ = 0;
        carried_ = (long long)open_; topN_ = 0; bigN_ = 0; since_ = now;
        for (int i = 0; i < kBuckets; ++i) b_[i] = 0;
    }
    Stamp s_[kRing];
    int open_, hint_, queuedN_;
    WalkRow walk_[kRing]; int walkN_; const void* last_;
    long long cnt_, max_, over_, over500_, linkDropped_, dropped_, stale_, noSlot_, stamped_, carried_, b_[kBuckets];
    Slow top_[kTopSlow]; int topN_;
    Slow big_[kKeepBig]; int bigN_;
    long long seq_;
    double since_;
};

}   // namespace p199

#endif
// PROBE-END: P199
