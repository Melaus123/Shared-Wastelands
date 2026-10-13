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
   LingerBufferBytes, HideIdNumbers) and its once-a-second send-rate step (NextSendRate). */
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

/* ---- the Steam link's send rate (owner decision 594, 2026-10-09) ----
   Valve (steamnetworkingtypes.h, SendRateMin/SendRateMax): set both to the same value for a fixed manual rate, default 256K; the
   library does not estimate the channel's bandwidth yet. So the link steps its own rate once a second from Steam's live numbers:
   halve on loss, raise by a quarter when delivery is clean and data is waiting, otherwise keep. */
const int kSendRateStartBytes   = 524288;    /* 512 KB/s (4 Mbit/s): the rate each connection starts at */
const int kSendRateFloorBytes   = 131072;    /* 128 KB/s: never lower */
const int kSendRateCeilingBytes = 4194304;   /* 4 MB/s: never higher */

/* Steam's delivery quality (fraction of packets delivered end to end, 0..1) is known; anything else (-1 before Steam has a
   measurement, or a value out of range) is not. */
inline bool SendQualityKnown(float q)
{
    return q >= 0.0f && q <= 1.0f;
}

/* The next send rate from the current one, Steam's delivery quality here and as the other end sees it, and the bytes waiting to go
   (Steam's pending reliable + unreliable plus the link's own queue): either quality known and below 0.97 -> half (not below the
   floor); both known and at least 0.99 with more than a quarter of the rate waiting -> a quarter more (not above the ceiling);
   otherwise the same rate. */
inline int NextSendRate(int rate, float qualityLocal, float qualityRemote, unsigned int waitingBytes)
{
    const bool knownLocal = SendQualityKnown(qualityLocal);
    const bool knownRemote = SendQualityKnown(qualityRemote);
    if ((knownLocal && qualityLocal < 0.97f) || (knownRemote && qualityRemote < 0.97f))
    {
        const int half = rate / 2;
        return half < kSendRateFloorBytes ? kSendRateFloorBytes : half;
    }
    if (knownLocal && knownRemote && qualityLocal >= 0.99f && qualityRemote >= 0.99f && rate > 0
        && (long long)waitingBytes > (long long)(rate / 4))
    {
        const long long up = (long long)rate + (long long)(rate / 4);
        return up > (long long)kSendRateCeilingBytes ? kSendRateCeilingBytes : (int)up;
    }
    return rate;
}

}   /* namespace relaysplit */

#endif   /* SW_RELAYSPLIT_H */
