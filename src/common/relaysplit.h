/* src/common/relaysplit.h - THE STEAM LINK'S SPLIT AND REJOIN. PURE: no Windows.h, no Steam, no globals, no I/O, so the offline suite
   sweeps exactly what the plugin compiles.

   WHY. Steam Networking Sockets refuses one message over 512 KB (k_cbMaxSteamNetworkingSocketsMessageSizeSend = 512 * 1024,
   steamnetworkingtypes.h), and the world link carries records up to 6,013,373 bytes (fallenwire.h kTableWorstBytes). ENet splits such
   a frame itself; on a Steam link this file does it.

   THE SHAPE. One frame (the transport's 5-byte header and its payload - the same frame ENet carries) becomes one or more Steam
   messages, each starting with one KIND byte:
     kWhole (0) - the whole frame follows;
     kMore  (1) - a piece of a frame, more pieces follow;
     kLast  (2) - the last piece of a frame.
   A frame of up to kPieceBytes travels whole; a longer one in pieces of kPieceBytes (the last one may be shorter), on the RELIABLE
   lane only: Steam delivers reliable messages in the order they were sent, so the pieces of one frame arrive one after another with
   nothing reliable between them. Unreliable messages may arrive between them; they are always whole. An unreliable frame longer than
   one piece is refused at the sender (unreliable traffic is small: MOVE, STATE).
   The receiver rejoins up to kRejoinMax bytes - ENet's own packet cap (enet.h ENET_HOST_DEFAULT_MAXIMUM_PACKET_SIZE, 32 MiB), so a
   Steam link refuses exactly what an ENet link refuses; the sender refuses a longer frame before anything is sent.
   Anything else on the wire (an empty message, an unknown kind, a piece on the unreliable lane, a last piece with no first, a whole
   reliable frame inside an open one, an empty piece, a whole message longer than one piece) is refused: a reliable stream with a
   hole in it cannot be trusted past the hole, so the caller closes the link.
   Below them, the link's small pure helpers for its log lines and its graceful close (LinkRelayed, PopText, LinkStateWord,
   LingerBufferBytes, HideIdNumbers) and its once-a-second send-rate step (NextSendRate, SendRateState). */
#ifndef SW_RELAYSPLIT_H
#define SW_RELAYSPLIT_H

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace relaysplit {

const size_t kPieceBytes = 262144;     /* 256 KB of frame per Steam message: half Steam's 512 KB message cap */
const size_t kRejoinMax  = 33554432;   /* 32 MiB: ENet's packet cap */

enum { kWhole = 0, kMore = 1, kLast = 2 };

/* How many Steam messages a frame of `frameBytes` travels in (a frame of 0 bytes is one message). */
inline size_t PieceCount(size_t frameBytes)
{
    return frameBytes <= kPieceBytes ? 1 : (frameBytes + kPieceBytes - 1) / kPieceBytes;
}

/* Whether a frame can be sent: kSendOk; kSendTooBig = longer than kRejoinMax; kSendUnreliableTooBig = unreliable and over one piece. */
enum { kSendOk = 0, kSendTooBig = 1, kSendUnreliableTooBig = 2 };
inline int SendVerdict(size_t frameBytes, bool reliable)
{
    if (frameBytes > kRejoinMax) return kSendTooBig;
    if (!reliable && frameBytes > kPieceBytes) return kSendUnreliableTooBig;
    return kSendOk;
}

/* The Steam messages for one frame, appended to *out in sending order. false (and *out untouched) when SendVerdict refuses it. */
inline bool Split(const char* frame, size_t len, bool reliable, std::vector<std::vector<char> >* out)
{
    if (out == 0 || (len != 0 && frame == 0) || SendVerdict(len, reliable) != kSendOk) return false;
    out->reserve(out->size() + PieceCount(len));
    if (len <= kPieceBytes)
    {
        out->push_back(std::vector<char>(len + 1));
        std::vector<char>& m = out->back();
        m[0] = (char)kWhole;
        if (len != 0) std::memcpy(&m[1], frame, len);
        return true;
    }
    size_t at = 0;
    while (at < len)
    {
        const size_t n = (len - at) > kPieceBytes ? kPieceBytes : (len - at);
        out->push_back(std::vector<char>(n + 1));
        std::vector<char>& m = out->back();
        m[0] = (char)(at + n < len ? kMore : kLast);
        std::memcpy(&m[1], frame + at, n);
        at += n;
    }
    return true;
}

/* What Take did with one received message. */
enum { kTakeFrame = 1, kTakePiece = 0, kTakeBad = -1, kTakeOver = -2 };

/* One link's open split frame. */
struct Rejoin
{
    std::vector<char> buf;   /* the pieces of the open frame so far */
    bool              open;  /* a kMore arrived and its kLast has not */
    size_t            pieces;
    Rejoin() : open(false), pieces(0) {}
    void Reset() { std::vector<char>().swap(buf); open = false; pieces = 0; }   /* frees the buffer: a 32 MiB frame is not kept */
};

/* One received Steam message (`reliable` = it came on the reliable lane).
   kTakeFrame - *frame holds a whole frame and *pieces how many messages it came in;
   kTakePiece - a piece was kept, nothing to hand on yet;
   kTakeBad / kTakeOver - *why says what was wrong (kTakeOver: the frame grew past kRejoinMax); the open frame is dropped and the
   caller closes the link. */
inline int Take(Rejoin* r, const char* msg, size_t len, bool reliable, std::vector<char>* frame, size_t* pieces, const char** why)
{
    const char* unused = 0;
    if (why == 0) why = &unused;
    *why = "";
    if (r == 0 || frame == 0) { *why = "no rejoin buffer"; return kTakeBad; }
    if (len == 0 || msg == 0) { *why = "an empty message (no kind byte)"; r->Reset(); return kTakeBad; }
    const int kind = (unsigned char)msg[0];
    const char* body = msg + 1;
    const size_t n = len - 1;
    if (kind == kWhole)
    {
        if (reliable && r->open) { *why = "a whole reliable frame inside an unfinished split frame"; r->Reset(); return kTakeBad; }
        if (n > kPieceBytes) { *why = "a whole message longer than one piece"; r->Reset(); return kTakeBad; }
        frame->assign(body, body + n);
        if (pieces != 0) *pieces = 1;
        return kTakeFrame;
    }
    if (kind != kMore && kind != kLast) { *why = "an unknown kind byte"; r->Reset(); return kTakeBad; }
    if (!reliable) { *why = "a piece of a split frame on the unreliable lane"; r->Reset(); return kTakeBad; }
    if (n == 0) { *why = "an empty piece"; r->Reset(); return kTakeBad; }
    if (kind == kLast && !r->open) { *why = "a last piece with no first piece"; r->Reset(); return kTakeBad; }
    if (r->buf.size() + n > kRejoinMax) { *why = "a split frame longer than 32 MiB"; r->Reset(); return kTakeOver; }
    r->buf.insert(r->buf.end(), body, body + n);
    ++r->pieces;
    r->open = true;
    if (kind == kMore) return kTakePiece;
    frame->swap(r->buf);
    if (pieces != 0) *pieces = r->pieces;
    r->Reset();
    return kTakeFrame;
}

/* ---- the Steam link's log facts and graceful-close buffer (T-290 S1 review fold) ---- */

/* Steam routed the link through a relay: its Relayed flag (k_nSteamNetworkConnectionInfoFlags_Relayed, 16) or a relay data centre named. */
inline bool LinkRelayed(int flags, unsigned int popRelay)
{
    return (flags & 16) != 0 || popRelay != 0;
}

/* A Steam data-centre id (SteamNetworkingPOPID) as its 3- or 4-letter code. Valve packs the first three letters in bits 16-23, 8-15
   and 0-7, and a fourth in bits 24-31. "none" for 0; "0x" and 8 hex digits when the letters are not printable. out holds 11 chars. */
inline void PopText(unsigned int pop, char* out)
{
    if (pop == 0)
    {
        const char* w = "none";
        int i = 0;
        for (; w[i] != 0; ++i) out[i] = w[i];
        out[i] = 0;
        return;
    }
    const unsigned int c[4] = { (pop >> 16) & 255u, (pop >> 8) & 255u, pop & 255u, (pop >> 24) & 255u };
    bool printable = true;
    for (int i = 0; i < 3; ++i)
        if (c[i] <= 32u || c[i] >= 127u) printable = false;
    if (c[3] != 0u && (c[3] <= 32u || c[3] >= 127u)) printable = false;
    if (printable)
    {
        int n = 0;
        for (int i = 0; i < 4; ++i)
            if (i < 3 || c[3] != 0u) out[n++] = (char)c[i];
        out[n] = 0;
        return;
    }
    const char* hex = "0123456789abcdef";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 8; ++i) out[2 + i] = hex[(pop >> (28 - 4 * i)) & 15u];
    out[10] = 0;
}

/* ESteamNetworkingConnectionState as a word, for the log. */
inline const char* LinkStateWord(int st)
{
    switch (st)
    {
    case 0: return "none";
    case 1: return "connecting";
    case 2: return "finding-route";
    case 3: return "connected";
    case 4: return "closed-by-peer";
    case 5: return "problem-detected-locally";
    default: return "other";
    }
}

/* A graceful close's send buffer: the link's own queue on top of the normal buffer (what Steam already holds is at most that),
   capped at `cap`. */
inline long long LingerBufferBytes(long long queued, long long normal, long long cap)
{
    if (queued < 0) queued = 0;
    const long long want = queued + normal;
    return want > cap ? cap : want;
}

/* A log fragment with the other player's identity taken out: every run of 15 or more decimal digits becomes "<id>" (a 64-bit Steam
   id is 17 digits, and Steam's own texts - the connection description, the detailed status - name the other player by it), and
   every internet address becomes "<ip>": an IPv4 address (four dotted numbers) other than this PC's own 127.x.x.x, and an IPv6
   address (a run of hex digits and colons holding at least two colons). Logs travel in bug reports and never carry an address.
   Shorter numbers (connection numbers, ports) stay; a port after an address stays (<ip>:27015). */
inline bool HideIsHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
inline std::string HideIdNumbers(const std::string& s)
{
    std::string out;
    size_t i = 0;
    while (i < s.size())
    {
        /* an IPv6 address: hex digits and colons, at least two colons, not part of a longer word */
        if ((HideIsHex(s[i]) || s[i] == ':') && (i == 0 || !(HideIsHex(s[i - 1]) || s[i - 1] == '_')))
        {
            size_t j = i; int colons = 0;
            while (j < s.size() && (HideIsHex(s[j]) || s[j] == ':' || s[j] == '.')) { if (s[j] == ':') ++colons; ++j; }
            if (colons >= 2) { out += "<ip>"; i = j; continue; }
        }
        if (s[i] < '0' || s[i] > '9') { out += s[i]; ++i; continue; }
        /* a run of digits: a long id, the start of a dotted IPv4 address, or a plain number */
        size_t j = i;
        while (j < s.size() && s[j] >= '0' && s[j] <= '9') ++j;
        if (j - i >= 15) { out += "<id>"; i = j; continue; }
        size_t k = j; int dots = 0;
        while (dots < 3 && k + 1 < s.size() && s[k] == '.' && s[k + 1] >= '0' && s[k + 1] <= '9')
        {
            ++k; while (k < s.size() && s[k] >= '0' && s[k] <= '9') ++k;
            ++dots;
        }
        if (dots == 3)
        {
            if (s.compare(i, 4, "127.") == 0) out.append(s, i, k - i);   /* this PC's own loopback address */
            else out += "<ip>";
            i = k; continue;
        }
        out.append(s, i, j - i);
        i = j;
    }
    return out;
}

/* ---- the Steam link's send rate (owner decision 991, 2026-10-21, replacing decision 594's halve-on-one-bad-second rule) ----
   Valve (steamnetworkingtypes.h, SendRateMin/SendRateMax): set both to the same value for a fixed manual rate, default 256K; the
   library does not estimate the channel's bandwidth yet. So the link steps its own rate once a second from Steam's live numbers
   (NextSendRate below), gently: cut by a fifth only after SUSTAINED poor delivery of this end's sends while the link is busy, or a
   RISING ping; climb back quickly only up to the PROVEN rate (one that had kSendRateProveReadings fresh clean readings of the far
   end's quality), never straight back to the rate the trouble happened at; above that grow by a quarter when delivery is clean and
   data is waiting - once the connection has been cut, only after the rate in use has itself been proven; otherwise keep. Loss
   measured while the link is idle never cuts. */
const int kSendRateStartBytes   = 524288;    /* 512 KB/s (4 Mbit/s): the rate each connection starts at */
const int kSendRateFloorBytes   = 262144;    /* 256 KB/s: never lower (decision 991) */
const int kSendRateCeilingBytes = 4194304;   /* 4 MB/s: never higher */
/* Decision 991 left the exact numbers to T-985's measurements; no T-985 finding reports Steam quality or ping, so these are the
   starting values chosen for T-1118 f, to be tuned from a run on a lossy, laggy Steam link. */
const int kSendRateBadSteps     = 3;         /* N: the ping window (rising delay compares this step's ping with the last N known pings) */
const int kSendRateLossReadings = 2;         /* fresh busy-bad readings of the far end's quality before a cut (sustained loss): one
                                                reading can be a passing burst, two at least kSendRateLossGapMs apart are sustained */
const unsigned int kSendRateReadingMs = 20000u;   /* Steam refreshes the far end's quality about every 20 s: an unchanged value this
                                                long after the last counted one is a new reading, so one reading repeated on many
                                                once-a-second steps counts once */
const unsigned int kSendRateLossGapMs = 15000u;   /* counted bad readings at least this far apart: shorter than Steam's ~20 s refresh,
                                                because the link steps once a second and sees the next refresh anywhere from about 19
                                                to 21 s after the last - a gap of exactly 20 s would miss about half of them */
const int kSendRateBusyPercent  = 50;        /* busy: Steam's outgoing bytes per second at least half the rate - below that the link
                                                is not pressing the rate, so its loss says nothing about the rate */
const int kSendRateBusyWaitMs   = 100;       /* ... or more than 100 ms of the rate waiting to go: data is queued behind the rate even
                                                while Steam's outgoing average (refreshed about every 5 s) still reads low */
const int kSendRateCutPercent   = 20;        /* a cut takes a fifth off the rate */
const int kSendRateClimbPercent = 25;        /* a climb toward the proven rate adds a quarter per step (rounded up), never past it */
const int kSendRateDelayPercent = 50;        /* a ping at least half again above the window's lowest ... */
const int kSendRateDelayMinMs   = 100;       /* ... and at least 100 ms above it is rising delay */
const int kSendRateProveReadings = 2;        /* fresh clean readings of the far end's quality at one rate PROVE it (and, once the
                                                connection has been cut, let it grow): the first may have been measured before the
                                                rate was set, the second is measured at it */
const int kSendRateSettleSteps  = 10;        /* during a recovery after a cut: known pings in a row each within the margin of the
                                                window's lowest before it make that lowest the base ping (a route that stays slower
                                                for good no longer blocks recovery) */

/* Why a step changed the rate (for the link's log). */
enum { kRateKeep = 0, kRateSustainedLoss = 1, kRateRisingDelay = 2, kRateClimb = 3, kRateGrowth = 4 };

inline const char* SendRateReasonText(int reason)
{
    switch (reason)
    {
    case kRateSustainedLoss: return "sustained loss";
    case kRateRisingDelay:   return "rising delay";
    case kRateClimb:         return "climb to proven";
    case kRateGrowth:        return "growth";
    default:                 return "keep";
    }
}

/* What one connection's rate rule remembers between steps. The caller keeps one per connection and calls Reset when a connection
   comes UP, so a reconnect starts with no loss run, no quality reading, no ping history, no proven rate and no cut. */
struct SendRateState
{
    int lossRun;                        /* fresh busy-bad readings counted since the last fresh clean reading or cut (held at
                                           kSendRateLossReadings); an idle bad reading or a middling one neither counts nor empties it */
    unsigned int lossMs;                /* the time (ms) of the last reading lossRun counted */
    bool readingKnown;                  /* a far-end quality reading has been counted on this connection */
    float readingQuality;               /* the last counted far-end quality reading */
    unsigned int readingMs;             /* the time (ms) it was counted */
    bool fresh;                         /* the last step saw a fresh far-end quality reading (for the link's log) */
    bool busy;                          /* the last step found the link busy (for the link's log) */
    int proven;                         /* the highest rate that had kSendRateProveReadings fresh clean readings; 0 = none. A cut of
                                           that very rate (or below) sets it to 0; a failed probe above it keeps it */
    int cleanReadings;                  /* fresh readings on clean steps at cleanRate (held at kSendRateProveReadings); a cut, an
                                           unclean step or another rate starts it again */
    int cleanRate;                      /* the rate cleanReadings counts for; 0 = none yet */
    bool hadCut;                        /* this connection has been cut: growth then waits until the rate in use is proven */
    int basePing;                       /* from a cut until the rate in use proves itself: the window's lowest ping at the first such
                                           cut (ms); after kSendRateSettleSteps steady pings the window's lowest then replaces it (the
                                           lasting-rise rule in NextSendRate); 0 = not known, or no recovery under way */
    int pingMs[kSendRateBadSteps];      /* the last known pings (ms) before this step; the oldest is overwritten first */
    int pingCount;                      /* pings held, 0..kSendRateBadSteps */
    int pingNext;                       /* the slot the next ping goes in */
    int settleSteps;                    /* while a base ping is known: known pings in a row within the margin of the window's lowest */
    SendRateState() { Reset(); }
    void Reset()
    {
        lossRun = 0; lossMs = 0; readingKnown = false; readingQuality = 0.0f; readingMs = 0; fresh = false; busy = false;
        proven = 0; cleanReadings = 0; cleanRate = 0; hadCut = false; basePing = 0; pingCount = 0; pingNext = 0;
        settleSteps = 0;
        for (int i = 0; i < kSendRateBadSteps; ++i) pingMs[i] = 0;
    }
};

/* Steam's delivery quality (fraction of packets delivered end to end, 0..1) is known; anything else (-1 before Steam has a
   measurement, or a value out of range) is not. */
inline bool SendQualityKnown(float q)
{
    return q >= 0.0f && q <= 1.0f;
}

/* A ping (ms) at least kSendRateDelayPercent and at least kSendRateDelayMinMs above a base ping; either not known (0 or less) -> no. */
inline bool SendDelayAbove(int pingMs, int baseMs)
{
    if (pingMs <= 0 || baseMs <= 0) return false;
    return (long long)pingMs * 100 >= (long long)baseMs * (100 + kSendRateDelayPercent) && pingMs >= baseMs + kSendRateDelayMinMs;
}

/* The lowest ping in the window; 0 when it holds none. */
inline int SendPingLow(const SendRateState& s)
{
    if (s.pingCount <= 0) return 0;
    int low = s.pingMs[0];
    for (int i = 1; i < s.pingCount; ++i)
        if (s.pingMs[i] < low) low = s.pingMs[i];
    return low;
}

/* Rising delay: Steam's ping this step (ms; 0 or less = Steam cannot say) is above the lowest of the last kSendRateBadSteps known
   pings by the margin (SendDelayAbove). Needs a full window: a fresh connection or one just cut cannot read as rising. */
inline bool SendDelayRising(const SendRateState& s, int pingMs)
{
    if (s.pingCount < kSendRateBadSteps) return false;
    return SendDelayAbove(pingMs, SendPingLow(s));
}

/* The next send rate from the current one, this connection's state, the time now (ms, any wrapping millisecond clock), Steam's
   delivery quality here (qualityLocal: how well this end receives - the caller logs it, it decides nothing) and as the far end sees
   it (qualityRemote: how well this end's sends arrive), Steam's ping (ms), Steam's outgoing bytes per second and the bytes waiting
   to go (Steam's pending reliable + unreliable plus the link's own queue). Only the far end's quality judges delivery: below 0.97
   is bad, at least 0.99 is clean, not known is neither.
   - A FRESH reading: the far end's quality known and either different from the last counted reading or at least
     kSendRateReadingMs after it (Steam refreshes it about every 20 s, so an unchanged value is still a new reading then); the
     repeats between are not readings.
   - BUSY: Steam's outgoing bytes per second at least kSendRateBusyPercent of the rate, or more than kSendRateBusyWaitMs of the
     rate waiting.
   - The LOSS RUN: a fresh bad reading while busy counts when the run is empty or it comes at least kSendRateLossGapMs after the
     last one counted; a fresh clean reading empties the run; a fresh bad reading while idle, or a middling one, neither counts nor
     empties it (no evidence about the rate either way).
   - CUT a fifth (not below the floor; at the floor the rate holds) when the run holds kSendRateLossReadings readings (sustained
     loss), or the ping is rising (SendDelayRising). A cut of the proven rate itself (or below) forgets it (trouble at a rate
     thought good: the rate cut to must prove itself); a cut of a probe above it keeps it. The first cut of a recovery remembers
     the window's lowest ping (the base ping). A cut empties the loss run, the ping window and the clean count, so the next loss
     cut needs a fresh run and the next delay cut a fresh window of kSendRateBadSteps pings.
   - A CLEAN step: the far end's quality at least 0.99 (its last value holds between Steam's refreshes) and the ping not above the
     base ping by the margin. kSendRateProveReadings fresh readings on clean steps at one rate PROVE it: it becomes the proven rate
     if higher, and the recovery ends (base ping forgotten). Any other step starts the clean count again and keeps the rate.
   - CLIMB on a clean step while below the proven rate: a quarter more per step (rounded up), never past the proven rate, so never
     to the rate the trouble happened at: after a failed probe above the proven rate the cut lands at or below it and the climb
     returns there at once.
   - GROW on a clean step at or above the proven rate: more than a quarter of the rate waiting -> a quarter more (not above the
     ceiling). Once the connection has been cut, only when the rate in use has been proven, so a rate that failed is tried again
     only after the rate below it has proven itself (two fresh readings) and then fails only after a fresh loss run.
   - The base ping follows a route that stays slower: during a recovery, once kSendRateSettleSteps known pings in a row were each
     within the margin of the window's lowest before it, that window's lowest (this ping included) becomes the base ping, so a
     ping that rose for good and then held steady makes clean steps again (kSendRateProveReadings fresh readings then prove the
     rate). A cut empties the window, so the step right after a cut never counts toward that; a ping already back near the base
     ping is a clean step at once, but the rate cut to still holds until it has proven itself, and only then grows.
   - otherwise the same rate. *reasonOut (may be 0) gets kRate* saying which. */
inline int NextSendRate(SendRateState& s, unsigned int nowMs, int rate, float qualityLocal, float qualityRemote, int pingMs,
                        float outBytesPerSec, unsigned int waitingBytes, int* reasonOut)
{
    (void)qualityLocal;   /* how well this end receives says nothing about how this end's sends arrive */
    if (reasonOut) *reasonOut = kRateKeep;
    const bool known = SendQualityKnown(qualityRemote);
    const bool bad = known && qualityRemote < 0.97f;
    const bool clean = known && qualityRemote >= 0.99f;
    bool fresh = false;
    if (known && (!s.readingKnown || qualityRemote != s.readingQuality || nowMs - s.readingMs >= kSendRateReadingMs))
    {
        fresh = true;
        s.readingKnown = true;
        s.readingQuality = qualityRemote;
        s.readingMs = nowMs;
    }
    const bool busy = rate > 0 && ((double)outBytesPerSec * 100.0 >= (double)rate * kSendRateBusyPercent
                                   || (long long)waitingBytes * 1000 > (long long)rate * kSendRateBusyWaitMs);
    s.fresh = fresh;
    s.busy = busy;
    if (fresh && clean) s.lossRun = 0;
    else if (fresh && bad && busy && (s.lossRun == 0 || nowMs - s.lossMs >= kSendRateLossGapMs))
    {
        if (s.lossRun < kSendRateLossReadings) ++s.lossRun;
        s.lossMs = nowMs;
    }
    const bool rising = SendDelayRising(s, pingMs);
    const int lowBefore = SendPingLow(s);
    if (pingMs > 0)
    {
        s.pingMs[s.pingNext] = pingMs;
        s.pingNext = (s.pingNext + 1) % kSendRateBadSteps;
        if (s.pingCount < kSendRateBadSteps) ++s.pingCount;
    }
    const bool lossCut = s.lossRun >= kSendRateLossReadings;
    if (lossCut || rising)
    {
        s.cleanReadings = 0;   /* a loss or rising step is not clean, at the floor too */
        if (rate <= kSendRateFloorBytes) return rate;   /* already at the floor: hold, the loss run stays full until a fresh clean reading */
        long long cut = (long long)rate * (100 - kSendRateCutPercent) / 100;
        if (cut < (long long)kSendRateFloorBytes) cut = kSendRateFloorBytes;
        if (rate <= s.proven) s.proven = 0;   /* the trouble came at the proven rate itself: it is not proven any more; trouble on a step above it (a failed probe) keeps it, and the climb returns there */
        if (s.basePing == 0) s.basePing = lowBefore > 0 ? lowBefore : (pingMs > 0 ? pingMs : 0);   /* the first cut of a recovery */
        s.hadCut = true;
        s.lossRun = 0;
        s.pingCount = 0;
        s.pingNext = 0;
        s.settleSteps = 0;
        s.cleanRate = (int)cut;
        if (reasonOut) *reasonOut = lossCut ? kRateSustainedLoss : kRateRisingDelay;
        return (int)cut;
    }
    if (rate != s.cleanRate)   /* another rate than the clean count was for (the rate stepped): count afresh */
    {
        s.cleanReadings = 0;
        s.cleanRate = rate;
    }
    /* The low point remembered at the first cut can be from a route that is gone; a ping that has stayed near the window's lowest
       for kSendRateSettleSteps known steps in a row is the route's ping now, so it becomes the base ping and clean steps can count
       again. No recovery (no base ping), an unknown ping, an empty window (just cut) or a ping above the margin starts the count
       again. */
    if (s.basePing <= 0 || pingMs <= 0 || lowBefore <= 0 || SendDelayAbove(pingMs, lowBefore)) s.settleSteps = 0;
    else if (++s.settleSteps >= kSendRateSettleSteps)
    {
        s.basePing = SendPingLow(s);
        s.settleSteps = 0;
    }
    if (!known && rate > 0 && !SendDelayAbove(pingMs, s.basePing)) return rate;   /* no quality reading this step: neither clean nor bad - the count keeps */
    if (!clean || rate <= 0 || SendDelayAbove(pingMs, s.basePing))   /* not clean, or the ping has not come back down: keep */
    {
        s.cleanReadings = 0;
        return rate;
    }
    if (fresh && s.cleanReadings < kSendRateProveReadings) ++s.cleanReadings;
    if (s.cleanReadings >= kSendRateProveReadings)
    {
        if (rate > s.proven) s.proven = rate;   /* clean over enough fresh readings: proven */
        s.basePing = 0;                          /* the recovery from the last cut is over */
        s.settleSteps = 0;
    }
    if (rate < s.proven)
    {
        long long up = (long long)rate + ((long long)rate * kSendRateClimbPercent + 99) / 100;
        if (up > (long long)s.proven) up = s.proven;
        if (reasonOut) *reasonOut = kRateClimb;
        return (int)up;
    }
    if ((!s.hadCut || s.cleanReadings >= kSendRateProveReadings) && (long long)waitingBytes > (long long)(rate / 4))
    {
        const long long up = (long long)rate + (long long)(rate / 4);
        const int next = up > (long long)kSendRateCeilingBytes ? kSendRateCeilingBytes : (int)up;
        if (next != rate && reasonOut) *reasonOut = kRateGrowth;
        return next;
    }
    return rate;
}

}   /* namespace relaysplit */

#endif   /* SW_RELAYSPLIT_H */
