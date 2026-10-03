/* src/common/bugreport.h - REPORT A BUG: every pure decision behind the bug report (T-461; owner decisions 364-378).
 *
 * The player writes what happened; the game packs that text, its own current and previous launch logs, Kenshi's crash file
 * when the previous launch crashed, and each nearby player's current log into ONE zip file under kPackLimit bytes, with every
 * IP address removed from every log, and posts it to the report relay (a Cloudflare Worker that forwards it to the
 * developers' Discord - tools/relay/bug-report-worker.js). This header holds what can be decided without a game, a window or
 * a network, so the offline suite (src/coop-test/test_main.cpp) can pin it:
 *   - the approved words of every screen (build/pages/bug-report-mockup.html) and the description rule (370, 371);
 *   - the IP scrub (365): IPv4 and IPv6 addresses become a tag; loopback / unspecified addresses, ports and names stay; in the
 *     same pass a player's 32-character identity code is cut to its first 8 characters;
 *   - the compressor: raw DEFLATE (RFC 1951) in independent segments of about kSegRaw raw bytes, each ending byte-aligned,
 *     so the oldest segments of a log can be dropped after compression without compressing again;
 *   - CRC-32 combination, so a log cut at a segment boundary still gets its exact zip CRC;
 *   - the size budget and its cutting order (372): the previous launch's log first, then nearby players' logs, then this
 *     launch's log, then the crash file, oldest part first within each log;
 *   - the zip and multipart/form-data layouts;
 *   - the nearby-log message pair LOG_ASK / LOG_PART (game-to-game, through the world server) and the bundle it carries;
 *   - which crash file counts as "the last session crashed" (367 a), and when the wait for nearby players ends.
 *
 * Pure: no global state, no OS call, no MyGUI. C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_BUGREPORT_H
#define COOP_COMMON_BUGREPORT_H

#include <cstddef>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include "storemeta.h"   /* coopstore::Crc32 - CRC-32 IEEE, the zip definition */

namespace coopbug {

/* ---------------------------------------------------------------------------------------------------------------------
   THE RELAY. The one address the report is posted to: the Cloudflare Worker that forwards it to the developers' Discord
   (tools/relay/). HTTPS only - the upload refuses any other kind of address (kErrBadRelay); an empty one fails with
   kErrNoRelay. Either way the CAN'T SEND REPORT box says so, as for any other failed send.
   ------------------------------------------------------------------------------------------------------------------ */
const char* const kRelayUrl = "https://kenshi-bug-relay.allscott16.workers.dev/";

/* ---------------------------------------------------------------------------------------------------------------------
   LIMITS
   ------------------------------------------------------------------------------------------------------------------ */
const unsigned int kDescMaxChars   = 1000;              /* 371: the description's length, in characters */
const unsigned int kPackLimit      = 8000000u;          /* 372: the zip is kept under this many bytes (under 8 MB either way it is counted) */
const unsigned int kSegRaw         = 1024u * 1024u;     /* a log is compressed in segments of about this many raw bytes, cut at line ends */
const unsigned int kRawLogWindow   = 64u * 1024u * 1024u;   /* only the newest this-many bytes of a log are read: older ones could never fit */
const unsigned int kNearbyLogMax   = 1500000u;          /* a nearby game's answer (its compressed current log, the newest part) is at most this long */
const unsigned int kLogPartMax     = 48u * 1024u;       /* one LOG_PART's share of the answer - well under the LIVE envelope's 64 KB */
const unsigned int kPartGapMs      = 200;               /* an answering game sends at most one LOG_PART this often (at most about
                                                           240 KB/s) ... */
const long long    kAnswerQueueRoomBytes = 16 * 1024;   /* ... and only while its world-server link holds at most this many bytes not
                                                           yet delivered, so the answer never crowds the play that uses that link */
const unsigned int kAnswerSlotGapMs = 60000;            /* a game answers one asking slot at most once in this long */
const unsigned int kRefusalLogGapMs = 10000;            /* a refused ask is logged at most once in this long (the rest are counted) */
const unsigned int kNearbyFirstMs  = 5000;              /* a nearby game that has not started answering by then is left out */
const unsigned int kNearbyTotalMs  = 20000;             /* and none is waited for longer than this */
const unsigned int kCrashNearSec   = 600;               /* a crash file this close to the previous log's last line belongs to that launch */
const unsigned int kNameMaxBytes   = 64;                /* a nearby player's name in a bundle */

/* ---------------------------------------------------------------------------------------------------------------------
   THE APPROVED WORDS (build/pages/bug-report-mockup.html, owner 368; nothing about what is attached, owner 2026-10-02)
   ------------------------------------------------------------------------------------------------------------------ */
const char* const kButtonCaption   = "REPORT A BUG";    /* the pause-menu button, the title-screen button and the window's title */
const char* const kLabelText       = "WHAT HAPPENED?";
const char* const kAboutLine1      = "This report goes to the Shared Wastelands modder.";   /* two lines under the title (owner 425) */
const char* const kAboutLine2      = "It's for problems with the multiplayer mod.";
/* the title screen's note, right side (owner 425) */
const char* const kTitleNoteHead   = "SHARED WASTELANDS - EXPERIMENTAL";
const char* const kTitleNoteBody   = "This multiplayer mod is still in testing, so expect bugs.\n"
                                     "If something goes wrong, please report it:\n"
                                     "press Esc in game and choose REPORT A BUG,\n"
                                     "or use the REPORT A BUG button on this screen.";
const char* const kBoxHintText     = "Describe what you were doing and what went wrong.";
const char* const kNeedTextLine    = "Describe what happened to send a report.";
const char* const kCancelCaption   = "CANCEL";
const char* const kSendCaption     = "SEND";
const char* const kPreparingLine   = "Preparing your report...";
const char* const kSentTitle       = "REPORT SENT";
const char* const kSentText        = "Thank you. Your report has been sent to the Shared Wastelands modder.";
const char* const kOkCaption       = "OK";
const char* const kFailTitle       = "CAN'T SEND REPORT";
const char* const kBackCaption     = "BACK";
const char* const kTryAgainCaption = "TRY AGAIN";

/* "Sending your report... {n}%" */
inline std::string SendingLine(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    char b[64];
    std::sprintf(b, "Sending your report... %d%%", pct);
    return b;
}
/* "{n} / 1000" */
inline std::string CounterText(unsigned int used)
{
    char b[48];
    std::sprintf(b, "%u / %u", used, kDescMaxChars);
    return b;
}

/* ---------------------------------------------------------------------------------------------------------------------
   THE DESCRIPTION (370, 371)
   ------------------------------------------------------------------------------------------------------------------ */
/* Characters (UTF-8 code points) in `s` - every byte that is not a continuation byte starts one. */
inline unsigned int Utf8Chars(const std::string& s)
{
    unsigned int n = 0;
    for (size_t i = 0; i < s.size(); ++i)
        if (((unsigned char)s[i] & 0xC0u) != 0x80u) ++n;
    return n;
}
/* SEND may be pressed: the description holds something other than spaces and line breaks, and is at most kDescMaxChars long. */
inline bool DescriptionSendable(const std::string& s)
{
    if (Utf8Chars(s) > kDescMaxChars) return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return true;
    }
    return false;
}
/* The text a MyGUI edit box holds, as the player typed it: the box keeps a typed '#' as "##" and a colour as "#RRGGBB"
   (MyGUI's text tags); the first becomes '#', the second is dropped. Anything else is copied. */
inline std::string CaptionToPlain(const std::string& caption)
{
    std::string out;
    out.reserve(caption.size());
    for (size_t i = 0; i < caption.size(); ++i)
    {
        const char c = caption[i];
        if (c != '#') { out += c; continue; }
        if (i + 1 < caption.size() && caption[i + 1] == '#') { out += '#'; ++i; continue; }
        size_t k = 0;
        while (k < 6 && i + 1 + k < caption.size())
        {
            const char h = caption[i + 1 + k];
            if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'))) break;
            ++k;
        }
        if (k == 6) { i += 6; continue; }
        out += c;
    }
    return out;
}

/* ---------------------------------------------------------------------------------------------------------------------
   WHY A SEND FAILED, AND WHAT THE BOX SAYS (mock-up section 6)
   ------------------------------------------------------------------------------------------------------------------ */
enum { kFailNoInternet = 1, kFailServer = 2, kFailOther = 3 };
const int kErrNoRelay  = 1;   /* the "error {n}" of a game whose relay address is empty */
const int kErrBadRelay = 2;   /* ... or not an https address WinHTTP can read */
/* `winErr` = the WinHTTP / Windows error of a request that got no answer (0 = it got one); `httpStatus` = the answer's status
   code. A name that does not resolve or a server that cannot be reached is the player's connection; a timeout, a broken answer
   or a 5xx is the server; everything else names its number. */
inline int FailKindOf(int winErr, int httpStatus)
{
    if (winErr == 12007 || winErr == 12029) return kFailNoInternet;   /* ERROR_WINHTTP_NAME_NOT_RESOLVED, ERROR_WINHTTP_CANNOT_CONNECT */
    if (winErr == 12002 || winErr == 12030 || winErr == 12152) return kFailServer;   /* TIMEOUT, CONNECTION_ERROR, INVALID_SERVER_RESPONSE */
    if (winErr == 0 && httpStatus >= 500 && httpStatus <= 599) return kFailServer;
    return kFailOther;
}
/* The number "error {n}" shows: the Windows error when there was no answer, else the answer's status. */
inline int FailCodeOf(int winErr, int httpStatus) { return winErr != 0 ? winErr : httpStatus; }
inline std::string FailText(int kind, int code)
{
    if (kind == kFailNoInternet) return "Couldn't send your report. Check your internet connection and try again.";
    if (kind == kFailServer) return "The report server isn't responding. Try again later.";
    char b[96];
    std::sprintf(b, "Couldn't send your report (error %d). Try again later.", code);
    return b;
}
inline bool SendSucceeded(int winErr, int httpStatus) { return winErr == 0 && httpStatus >= 200 && httpStatus <= 299; }

/* ---------------------------------------------------------------------------------------------------------------------
   THE LOG SCRUB (365). Every IPv4 and IPv6 address in a log becomes a tag that says only what kind it was:
     [lan-ip]  10/8, 172.16/12, 192.168/16, 169.254/16, 100.64/10      [ip]  any other IPv4      [ip6]  any IPv6
   Loopback (127/8, ::1), unspecified (0.0.0.0, ::) and broadcast (255.255.255.255) identify nobody and stay as written. A port
   after an address stays; player names stay (365). An IPv4 is four dot-separated numbers 0-255 of 1-3 digits, with no digit or
   digit-dot before it and no digit or dot-digit after it (a version "1.0.68" has three parts and stays). An IPv6 is a run of hex digits and colons with nothing
   alphanumeric around it, of up to 8 groups of 1-4 hex digits, with "::" or exactly 8 groups (a time "12:34:56" and a C++
   name "std::string" stay), holding at least one decimal digit ("Bed::Add" stays), optionally ending in an IPv4.
   In the same pass every PLAYER IDENTITY CODE is cut short. The code is 32 hex characters (areaclaim.h PlayerIdOk) and is the
   only thing the world server knows a player by, so a report that carried it whole would let whoever reads the report join
   as that player. A run of exactly kIdHexLen characters 0-9 a-f A-F with no such character on either side keeps its first
   kIdKeepChars and gains "..." - the form the game's own log lines already use (playerid 0123abcd...), so lines about one
   player can still be matched. A longer or shorter hex run (a hash, a dump, an address) is something else and stays whole.
   ------------------------------------------------------------------------------------------------------------------ */
inline bool IsDigitC(char c) { return c >= '0' && c <= '9'; }
inline bool IsHexC(char c) { return IsDigitC(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
inline bool IsWordC(char c) { return IsHexC(c) || (c >= 'g' && c <= 'z') || (c >= 'G' && c <= 'Z') || c == '_'; }

/* An IPv4 starting exactly at s[i]: its length and its four numbers, or 0. */
inline size_t Ipv4At(const std::string& s, size_t i, unsigned int oct[4])
{
    size_t j = i;
    for (int k = 0; k < 4; ++k)
    {
        if (k > 0) { if (j >= s.size() || s[j] != '.') return 0; ++j; }
        size_t d = 0; unsigned int v = 0;
        while (j < s.size() && IsDigitC(s[j]) && d < 4) { v = v * 10u + (unsigned int)(s[j] - '0'); ++j; ++d; }
        if (d == 0 || d > 3 || v > 255u) return 0;
        oct[k] = v;
    }
    if (j < s.size() && IsDigitC(s[j])) return 0;
    if (j + 1 < s.size() && s[j] == '.' && IsDigitC(s[j + 1])) return 0;   /* a fifth part: a version or a number list */
    return j - i;
}
inline const char* Ipv4Tag(const unsigned int o[4])
{
    if (o[0] == 127u) return 0;
    if (o[0] == 0u && o[1] == 0u && o[2] == 0u && o[3] == 0u) return 0;
    if (o[0] == 255u && o[1] == 255u && o[2] == 255u && o[3] == 255u) return 0;
    if (o[0] == 10u || (o[0] == 172u && o[1] >= 16u && o[1] <= 31u) || (o[0] == 192u && o[1] == 168u)
        || (o[0] == 169u && o[1] == 254u) || (o[0] == 100u && o[1] >= 64u && o[1] <= 127u)) return "[lan-ip]";
    return "[ip]";
}
/* An IPv6 starting exactly at s[i]: its length, or 0. *keep = loopback or unspecified (written as it is). */
inline size_t Ipv6At(const std::string& s, size_t i, bool* keep)
{
    *keep = false;
    size_t j = i;
    while (j < s.size() && j - i < 46 && (IsHexC(s[j]) || s[j] == ':')) ++j;
    size_t end = j;
    /* an IPv4 tail ("::ffff:1.2.3.4"): the last group is read as an IPv4 instead */
    bool v4tail = false;
    if (j + 1 < s.size() && s[j] == '.' && IsDigitC(s[j + 1]))
    {
        size_t g = j;
        while (g > i && s[g - 1] != ':') --g;
        unsigned int o[4];
        const size_t n4 = (g > i) ? Ipv4At(s, g, o) : 0;
        if (n4 == 0) return 0;
        end = g + n4;
        j = g;   /* the hex part ends before the IPv4 */
        v4tail = true;
    }
    else if (end < s.size() && IsWordC(s[end])) return 0;
    const std::string h = s.substr(i, j - i);
    int colons = 0, groups = 0, doubles = 0, digits = 0, decimals = 0, run = 0;
    for (size_t k = 0; k < h.size(); ++k)
    {
        if (h[k] == ':')
        {
            ++colons;
            if (k + 1 < h.size() && h[k + 1] == ':') ++doubles;
            if (run > 0) ++groups;
            run = 0;
            continue;
        }
        ++digits; ++run;
        if (IsDigitC(h[k])) ++decimals;
        if (run > 4) return 0;
    }
    if (run > 0) ++groups;
    if (v4tail) groups += 2;   /* an IPv4 tail stands for two groups */
    if (doubles > 1) return 0;
    if (colons > 7 + (doubles ? 1 : 0)) return 0;
    if (doubles == 0 && (groups != 8 || colons != (v4tail ? 6 : 7))) return 0;
    if (doubles == 1 && groups > 7) return 0;
    if (colons < 2) return 0;
    if (decimals == 0 && !v4tail && digits != 0) return 0;   /* hex letters only ("Bed::Add") is a name, not an address */
    /* no stray single colon at either end ("::" may start or end it) */
    if (!h.empty() && h[0] == ':' && !(h.size() > 1 && h[1] == ':')) return 0;
    if (!v4tail && !h.empty() && h[h.size() - 1] == ':' && !(h.size() > 1 && h[h.size() - 2] == ':')) return 0;
    if (!v4tail && (h == "::" || h == "::1")) *keep = true;
    if (digits == 0 && !v4tail) *keep = true;
    return end - i;
}
const size_t kIdHexLen = 32, kIdKeepChars = 8;
/* An identity code starting exactly at s[i] (no hex character before it, exactly kIdHexLen of them, none after): its length,
   or 0. */
inline size_t IdCodeAt(const std::string& s, size_t i)
{
    if (i > 0 && IsHexC(s[i - 1])) return 0;
    size_t j = i;
    while (j < s.size() && IsHexC(s[j]) && j - i <= kIdHexLen) ++j;
    return (j - i == kIdHexLen) ? kIdHexLen : 0;
}
/* `in` with every address replaced and every identity code cut; returns how many addresses were replaced, and adds the codes
   cut to *idsCut (may be 0). */
inline size_t ScrubIps(const std::string& in, std::string* out, size_t* idsCut = 0)
{
    out->clear();
    out->reserve(in.size());
    size_t count = 0, i = 0;
    while (i < in.size())
    {
        const char c = in[i];
        const char prev = i > 0 ? in[i - 1] : ' ';
        if (IsHexC(c) && !IsHexC(prev))
        {
            const size_t n = IdCodeAt(in, i);
            if (n != 0)
            {
                out->append(in, i, kIdKeepChars);
                *out += "...";
                if (idsCut != 0) ++*idsCut;
                i += n;
                continue;
            }
        }
        if (IsDigitC(c) && !IsDigitC(prev) && !(prev == '.' && i >= 2 && IsDigitC(in[i - 2])))
        {
            unsigned int o[4];
            const size_t n = Ipv4At(in, i, o);
            if (n != 0)
            {
                const char* tag = Ipv4Tag(o);
                if (tag != 0) { *out += tag; ++count; } else out->append(in, i, n);
                i += n;
                continue;
            }
        }
        if ((IsHexC(c) || c == ':') && !IsWordC(prev) && prev != ':' && prev != '.')
        {
            bool keep = false;
            const size_t n = Ipv6At(in, i, &keep);
            if (n != 0)
            {
                if (keep) out->append(in, i, n); else { *out += "[ip6]"; ++count; }
                i += n;
                continue;
            }
        }
        *out += c;
        ++i;
    }
    return count;
}

/* ---------------------------------------------------------------------------------------------------------------------
   CRC-32 OF A JOINED BUFFER (zlib's crc32_combine): the CRC of A followed by B from crc(A), crc(B) and B's length, so a log
   cut at a segment boundary gets its zip CRC from the kept segments' own CRCs.
   ------------------------------------------------------------------------------------------------------------------ */
inline unsigned int Gf2Times(const unsigned int* mat, unsigned int vec)
{
    unsigned int sum = 0;
    for (int i = 0; vec != 0; ++i, vec >>= 1) if (vec & 1u) sum ^= mat[i];
    return sum;
}
inline void Gf2Square(unsigned int* square, const unsigned int* mat)
{
    for (int n = 0; n < 32; ++n) square[n] = Gf2Times(mat, mat[n]);
}
inline unsigned int Crc32Combine(unsigned int crc1, unsigned int crc2, unsigned long long len2)
{
    if (len2 == 0) return crc1;
    unsigned int even[32], odd[32];
    odd[0] = 0xEDB88320u;
    unsigned int row = 1;
    for (int n = 1; n < 32; ++n) { odd[n] = row; row <<= 1; }
    Gf2Square(even, odd);
    Gf2Square(odd, even);
    do
    {
        Gf2Square(even, odd);
        if (len2 & 1u) crc1 = Gf2Times(even, crc1);
        len2 >>= 1;
        if (len2 == 0) break;
        Gf2Square(odd, even);
        if (len2 & 1u) crc1 = Gf2Times(odd, crc1);
        len2 >>= 1;
    } while (len2 != 0);
    return crc1 ^ crc2;
}

/* ---------------------------------------------------------------------------------------------------------------------
   THE COMPRESSOR: raw DEFLATE with the fixed Huffman codes (RFC 1951 3.2.6) and greedy LZ77 over a 32 KB window. One
   segment = one block of codes, then an empty stored block (the "sync flush" 00 00 FF FF), so it ends byte-aligned and
   refers to nothing before it: segments can be dropped from the front and the rest joined. A stream is any run of
   segments followed by kFinalBlock.
   ------------------------------------------------------------------------------------------------------------------ */
const unsigned char kFinalBlock[2] = { 0x03, 0x00 };   /* a last fixed-code block holding only its end code */

struct BitOut
{
    std::vector<unsigned char>* out;
    unsigned int acc;
    int n;
    explicit BitOut(std::vector<unsigned char>* o) : out(o), acc(0), n(0) {}
    void Put(unsigned int v, int bits)   /* bits <= 16, least significant first */
    {
        acc |= v << n;
        n += bits;
        while (n >= 8) { out->push_back((unsigned char)(acc & 0xFFu)); acc >>= 8; n -= 8; }
    }
    void Align() { if (n > 0) { out->push_back((unsigned char)(acc & 0xFFu)); acc = 0; n = 0; } }
};
inline unsigned int Reverse(unsigned int code, int bits)
{
    unsigned int r = 0;
    for (int k = 0; k < bits; ++k) { r = (r << 1) | (code & 1u); code >>= 1; }
    return r;
}
/* one literal / length symbol (0..285) in its fixed code */
inline void PutLitLen(BitOut* b, unsigned int sym)
{
    if (sym <= 143u)      b->Put(Reverse(0x30u + sym, 8), 8);
    else if (sym <= 255u) b->Put(Reverse(0x190u + (sym - 144u), 9), 9);
    else if (sym <= 279u) b->Put(Reverse(sym - 256u, 7), 7);
    else                  b->Put(Reverse(0xC0u + (sym - 280u), 8), 8);
}
inline void PutMatch(BitOut* b, unsigned int len, unsigned int dist)
{
    static const unsigned short kLenBase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
    static const unsigned char  kLenExtra[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    static const unsigned short kDistBase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,
                                                  4097,6145,8193,12289,16385,24577 };
    static const unsigned char  kDistExtra[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
    int li = 28;
    while (li > 0 && kLenBase[li] > len) --li;
    PutLitLen(b, 257u + (unsigned int)li);
    if (kLenExtra[li] != 0) b->Put(len - kLenBase[li], kLenExtra[li]);
    int di = 29;
    while (di > 0 && kDistBase[di] > dist) --di;
    b->Put(Reverse((unsigned int)di, 5), 5);
    if (kDistExtra[di] != 0) b->Put(dist - kDistBase[di], kDistExtra[di]);
}
/* `n` raw bytes as one independent segment, appended to *out. */
inline void CompressSegment(const unsigned char* p, size_t n, std::vector<unsigned char>* out)
{
    const int kWin = 32768, kHashBits = 15, kChainMax = 48;
    const unsigned int kHashMask = (1u << kHashBits) - 1u;
    std::vector<int> head(1u << kHashBits, -1);
    std::vector<int> prev((size_t)kWin, -1);
    BitOut b(out);
    b.Put(0, 1);   /* not the last block */
    b.Put(1, 2);   /* fixed codes */
    size_t i = 0;
    while (i < n)
    {
        unsigned int bestLen = 0, bestDist = 0;
        if (i + 2 < n)
        {
            const unsigned int h = (((unsigned int)p[i] << 10) ^ ((unsigned int)p[i + 1] << 5) ^ (unsigned int)p[i + 2]) & kHashMask;
            int j = head[h];
            int chain = 0;
            const size_t maxLen = (n - i) < 258u ? (n - i) : 258u;
            while (j >= 0 && (int)(i - (size_t)j) <= kWin && chain < kChainMax)
            {
                if (p[(size_t)j + bestLen] == p[i + bestLen])
                {
                    size_t l = 0;
                    while (l < maxLen && p[(size_t)j + l] == p[i + l]) ++l;
                    if (l > bestLen) { bestLen = (unsigned int)l; bestDist = (unsigned int)(i - (size_t)j); if (l == maxLen) break; }
                }
                const int nx = prev[(size_t)j & (size_t)(kWin - 1)];
                if (nx >= j) break;
                j = nx;
                ++chain;
            }
            prev[i & (size_t)(kWin - 1)] = head[h];
            head[h] = (int)i;
        }
        if (bestLen >= 3)
        {
            PutMatch(&b, bestLen, bestDist);
            for (size_t k = 1; k < bestLen; ++k)
            {
                const size_t q = i + k;
                if (q + 2 >= n) break;
                const unsigned int h2 = (((unsigned int)p[q] << 10) ^ ((unsigned int)p[q + 1] << 5) ^ (unsigned int)p[q + 2]) & kHashMask;
                prev[q & (size_t)(kWin - 1)] = head[h2];
                head[h2] = (int)q;
            }
            i += bestLen;
        }
        else
        {
            PutLitLen(&b, p[i]);
            ++i;
        }
    }
    PutLitLen(&b, 256u);   /* end of block */
    b.Put(0, 1);           /* an empty stored block, not the last: the segment ends byte-aligned */
    b.Put(0, 2);
    b.Align();
    out->push_back(0x00); out->push_back(0x00); out->push_back(0xFF); out->push_back(0xFF);
}

/* Where each segment of `n` raw bytes ends: at the first line end at or after `target` bytes into it (the '\n' kept in it),
   or at `n`; a line longer than 2 x target is cut at 2 x target. */
inline std::vector<size_t> SegmentEnds(const char* p, size_t n, size_t target)
{
    std::vector<size_t> ends;
    if (target == 0) target = 1;
    size_t start = 0;
    while (start < n)
    {
        size_t e = start + target;
        if (e >= n) { ends.push_back(n); break; }
        const size_t hard = (start + 2 * target < n) ? start + 2 * target : n;
        while (e < hard && p[e - 1] != '\n') ++e;
        ends.push_back(e);
        start = e;
    }
    return ends;
}
/* Where the read of a log of `size` bytes starts: 0, or - when it is longer than `window` - the byte after the first line end
   past size - window (`p` = the bytes from size - window on, `pn` of them). Returns the offset INTO p. */
inline size_t WindowStart(const char* p, size_t pn)
{
    for (size_t i = 0; i < pn; ++i) if (p[i] == '\n') return i + 1;
    return pn;
}

/* ---------------------------------------------------------------------------------------------------------------------
   A LOG, COMPRESSED: its segments (oldest first) and what was left out before the first one.
   ------------------------------------------------------------------------------------------------------------------ */
struct Seg
{
    unsigned int rawLen, crc;
    std::vector<unsigned char> bytes;
    Seg() : rawLen(0), crc(0) {}
};
struct LogPack
{
    unsigned long long rawTotal;      /* the file's size when it was read */
    unsigned long long rawDropped;    /* bytes at its start that are in no segment (the read window, a cap, the budget) */
    unsigned int ipsScrubbed;
    std::vector<Seg> segs;
    LogPack() : rawTotal(0), rawDropped(0), ipsScrubbed(0) {}
};
/* A log's text as it was read, appended to pack->segs: cut into segments at line ends (SegmentEnds, kSegRaw), each one
   scrubbed (ScrubIps - a segment ends at a line end, so no address is split) and compressed on its own. `stop` (may be 0) is
   read before each segment: non-zero stops the work and returns false. */
inline bool PackLog(const std::string& raw, LogPack* pack, const volatile long* stop)
{
    if (raw.empty()) return true;
    const std::vector<size_t> ends = SegmentEnds(raw.data(), raw.size(), kSegRaw);
    size_t start = 0;
    std::string clean;
    for (size_t k = 0; k < ends.size(); ++k)
    {
        if (stop != 0 && *stop != 0) return false;
        pack->ipsScrubbed += (unsigned int)ScrubIps(raw.substr(start, ends[k] - start), &clean);
        Seg s;
        s.rawLen = (unsigned int)clean.size();
        s.crc = coopstore::Crc32(clean.data(), clean.size());
        CompressSegment((const unsigned char*)clean.data(), clean.size(), &s.bytes);
        pack->segs.push_back(s);
        start = ends[k];
    }
    return true;
}
/* As PackLog, newest segment first and only as far back as fits in `cap` compressed bytes (with kFinalBlock); the older
   segments are never compressed, and their raw bytes count in rawDropped. A nearby game's answer uses it, so a long log costs no
   more work than the part that is sent. */
inline void PackLogNewest(const std::string& raw, LogPack* pack, unsigned long long cap)
{
    if (raw.empty()) return;
    const std::vector<size_t> ends = SegmentEnds(raw.data(), raw.size(), kSegRaw);
    std::vector<Seg> newest;
    unsigned long long used = sizeof(kFinalBlock);
    std::string clean;
    size_t k = ends.size();
    while (k > 0)
    {
        const size_t start = k >= 2 ? ends[k - 2] : 0;
        const size_t ips = ScrubIps(raw.substr(start, ends[k - 1] - start), &clean);
        Seg s;
        s.rawLen = (unsigned int)clean.size();
        s.crc = coopstore::Crc32(clean.data(), clean.size());
        CompressSegment((const unsigned char*)clean.data(), clean.size(), &s.bytes);
        if (used + s.bytes.size() > cap) break;
        used += s.bytes.size();
        pack->ipsScrubbed += (unsigned int)ips;
        newest.push_back(s);
        --k;
    }
    if (k > 0) pack->rawDropped += ends[k - 1];
    pack->segs.insert(pack->segs.end(), newest.rbegin(), newest.rend());
}
inline unsigned long long PackBytes(const LogPack& p, size_t firstKept)
{
    unsigned long long n = 0;
    for (size_t k = firstKept; k < p.segs.size(); ++k) n += p.segs[k].bytes.size();
    return n;
}
/* Drops the oldest segments until the kept ones (plus kFinalBlock) are at most `cap` bytes. */
inline void CapPack(LogPack* p, unsigned long long cap)
{
    size_t first = 0;
    while (first < p->segs.size() && PackBytes(*p, first) + sizeof(kFinalBlock) > cap)
    {
        p->rawDropped += p->segs[first].rawLen;
        ++first;
    }
    if (first > 0) p->segs.erase(p->segs.begin(), p->segs.begin() + (long)first);
}
/* The kept segments from `first` on as one DEFLATE stream, with its raw length and CRC. */
inline void JoinPack(const LogPack& p, size_t first, std::vector<unsigned char>* stream, unsigned long long* rawLen, unsigned int* crc)
{
    stream->clear();
    *rawLen = 0;
    *crc = 0;
    for (size_t k = first; k < p.segs.size(); ++k)
    {
        stream->insert(stream->end(), p.segs[k].bytes.begin(), p.segs[k].bytes.end());
        *crc = Crc32Combine(*crc, p.segs[k].crc, p.segs[k].rawLen);
        *rawLen += p.segs[k].rawLen;
    }
    stream->push_back(kFinalBlock[0]);
    stream->push_back(kFinalBlock[1]);
}

/* ---------------------------------------------------------------------------------------------------------------------
   THE SIZE BUDGET (372). Each part of the zip is `fixed` bytes that are never cut (its headers, its final block, a whole
   crash file) plus its segments' bytes. While the total is over the limit: the oldest kept segment of the lowest-`order` part
   that still has one is dropped (among parts of equal order, the one with the most kept bytes); when no part has a segment
   left to drop, a `droppable` part goes whole, lowest order first. Orders: kOrderPrevLog, kOrderNearby, kOrderThisLog,
   kOrderCrash. The result says, per part, how many leading segments go and whether the part goes whole.
   ------------------------------------------------------------------------------------------------------------------ */
enum { kOrderPrevLog = 0, kOrderNearby = 1, kOrderThisLog = 2, kOrderCrash = 3, kOrderKeep = 9 };
struct BudgetPart
{
    unsigned long long fixed;
    std::vector<unsigned long long> segBytes;
    int order;
    bool droppable;
    BudgetPart() : fixed(0), order(kOrderKeep), droppable(false) {}
};
struct BudgetCut
{
    std::vector<size_t> dropSegs;   /* per part: leading segments dropped */
    std::vector<int> dropped;       /* per part: 1 = left out whole */
    unsigned long long total;       /* the total after the cuts */
    bool fits;
};
inline BudgetCut PlanBudget(const std::vector<BudgetPart>& parts, unsigned long long limit)
{
    BudgetCut c;
    c.dropSegs.assign(parts.size(), 0);
    c.dropped.assign(parts.size(), 0);
    std::vector<unsigned long long> kept(parts.size(), 0);
    c.total = 0;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        for (size_t k = 0; k < parts[i].segBytes.size(); ++k) kept[i] += parts[i].segBytes[k];
        c.total += parts[i].fixed + kept[i];
    }
    while (c.total > limit)
    {
        int best = -1;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (c.dropped[i] != 0 || parts[i].order == kOrderKeep || c.dropSegs[i] >= parts[i].segBytes.size()) continue;
            if (best < 0 || parts[i].order < parts[(size_t)best].order
                || (parts[i].order == parts[(size_t)best].order && kept[i] > kept[(size_t)best])) best = (int)i;
        }
        if (best >= 0)
        {
            const size_t b = (size_t)best;
            const unsigned long long s = parts[b].segBytes[c.dropSegs[b]];
            ++c.dropSegs[b];
            kept[b] -= s;
            c.total -= s;
            continue;
        }
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (c.dropped[i] != 0 || !parts[i].droppable) continue;
            if (best < 0 || parts[i].order < parts[(size_t)best].order) best = (int)i;
        }
        if (best < 0) break;
        c.dropped[(size_t)best] = 1;
        c.total -= parts[(size_t)best].fixed + kept[(size_t)best];
        kept[(size_t)best] = 0;
    }
    c.fits = c.total <= limit;
    return c;
}

/* ---------------------------------------------------------------------------------------------------------------------
   THE ZIP (PKWARE APPNOTE 4.3: local headers, central directory, end record; no zip64 - it stays under 8 MB). Names are
   UTF-8 (flag bit 11). method 0 = stored, 8 = deflate.
   ------------------------------------------------------------------------------------------------------------------ */
struct ZipEntry
{
    std::string name;
    unsigned short method;
    unsigned int crc;
    unsigned int rawLen;
    std::vector<unsigned char> data;   /* as stored in the zip */
    ZipEntry() : method(0), crc(0), rawLen(0) {}
};
inline ZipEntry StoredEntry(const std::string& name, const std::string& text)
{
    ZipEntry e;
    e.name = name;
    e.method = 0;
    e.rawLen = (unsigned int)text.size();
    e.crc = coopstore::Crc32(text.data(), text.size());
    e.data.assign(text.begin(), text.end());
    return e;
}
/* The bytes a zip spends on one entry besides its data. */
inline unsigned long long ZipEntryOverhead(const std::string& name) { return 30u + 46u + 2u * (unsigned long long)name.size(); }
const unsigned int kZipEndBytes = 22;
inline void Put16(std::vector<unsigned char>* b, unsigned int v) { b->push_back((unsigned char)(v & 0xFFu)); b->push_back((unsigned char)((v >> 8) & 0xFFu)); }
inline void Put32(std::vector<unsigned char>* b, unsigned int v) { Put16(b, v & 0xFFFFu); Put16(b, (v >> 16) & 0xFFFFu); }
/* MS-DOS date and time words for a calendar time (years before 1980 read as 1980). */
inline unsigned int DosTime(int hour, int minute, int second) { return ((unsigned int)hour << 11) | ((unsigned int)minute << 5) | ((unsigned int)second / 2u); }
inline unsigned int DosDate(int year, int month, int day) { if (year < 1980) year = 1980; return ((unsigned int)(year - 1980) << 9) | ((unsigned int)month << 5) | (unsigned int)day; }
/* A zip record signature: the letters P K followed by the record kind (3 4 local file, 1 2 central entry, 5 6 end), stored
   little-endian as the zip format writes it. */
inline unsigned int ZipSig(unsigned int a, unsigned int b) { return 0x50u | (0x4Bu << 8) | (a << 16) | (b << 24); }
inline std::vector<unsigned char> BuildZip(const std::vector<ZipEntry>& es, unsigned int dosTime, unsigned int dosDate)
{
    std::vector<unsigned char> z;
    std::vector<unsigned int> offsets;
    for (size_t i = 0; i < es.size(); ++i)
    {
        const ZipEntry& e = es[i];
        offsets.push_back((unsigned int)z.size());
        Put32(&z, ZipSig(3, 4)); Put16(&z, 20); Put16(&z, 0x0800u); Put16(&z, e.method);
        Put16(&z, dosTime); Put16(&z, dosDate); Put32(&z, e.crc); Put32(&z, (unsigned int)e.data.size()); Put32(&z, e.rawLen);
        Put16(&z, (unsigned int)e.name.size()); Put16(&z, 0);
        z.insert(z.end(), e.name.begin(), e.name.end());
        z.insert(z.end(), e.data.begin(), e.data.end());
    }
    const unsigned int cdStart = (unsigned int)z.size();
    for (size_t i = 0; i < es.size(); ++i)
    {
        const ZipEntry& e = es[i];
        Put32(&z, ZipSig(1, 2)); Put16(&z, 20); Put16(&z, 20); Put16(&z, 0x0800u); Put16(&z, e.method);
        Put16(&z, dosTime); Put16(&z, dosDate); Put32(&z, e.crc); Put32(&z, (unsigned int)e.data.size()); Put32(&z, e.rawLen);
        Put16(&z, (unsigned int)e.name.size()); Put16(&z, 0); Put16(&z, 0); Put16(&z, 0); Put16(&z, 0); Put32(&z, 0);
        Put32(&z, offsets[i]);
        z.insert(z.end(), e.name.begin(), e.name.end());
    }
    const unsigned int cdSize = (unsigned int)z.size() - cdStart;
    Put32(&z, ZipSig(5, 6)); Put16(&z, 0); Put16(&z, 0); Put16(&z, (unsigned int)es.size()); Put16(&z, (unsigned int)es.size());
    Put32(&z, cdSize); Put32(&z, cdStart); Put16(&z, 0);
    return z;
}

/* ---------------------------------------------------------------------------------------------------------------------
   THE UPLOAD: multipart/form-data with three fields - `description` (the player's text), `info` (one line: game build and
   where the report was made) and `report` (the zip, filename bug-report.zip). The boundary is `seed` grown until it occurs in
   no field.
   ------------------------------------------------------------------------------------------------------------------ */
inline bool Contains(const std::vector<unsigned char>& hay, const std::string& needle)
{
    if (needle.empty() || hay.size() < needle.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i)
        if (hay[i] == (unsigned char)needle[0] && std::memcmp(&hay[i], needle.data(), needle.size()) == 0) return true;
    return false;
}
inline std::string PickBoundary(const std::string& seed, const std::string& desc, const std::string& info, const std::vector<unsigned char>& zip)
{
    std::string b = seed;
    for (int k = 0; k < 64; ++k)
    {
        if (desc.find(b) == std::string::npos && info.find(b) == std::string::npos && !Contains(zip, b)) return b;
        b += (char)('a' + (k % 26));
    }
    return b;
}
inline std::vector<unsigned char> BuildMultipart(const std::string& boundary, const std::string& desc, const std::string& info,
                                                 const std::vector<unsigned char>& zip)
{
    std::string head;
    head += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"description\"\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n";
    head += desc;
    head += "\r\n--" + boundary + "\r\nContent-Disposition: form-data; name=\"info\"\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n";
    head += info;
    head += "\r\n--" + boundary + "\r\nContent-Disposition: form-data; name=\"report\"; filename=\"bug-report.zip\"\r\nContent-Type: application/zip\r\n\r\n";
    const std::string tail = "\r\n--" + boundary + "--\r\n";
    std::vector<unsigned char> body(head.begin(), head.end());
    body.insert(body.end(), zip.begin(), zip.end());
    body.insert(body.end(), tail.begin(), tail.end());
    return body;
}

/* ---------------------------------------------------------------------------------------------------------------------
   KENSHI'S CRASH FILE (367 a). A crashDump*_x64.zip or .dmp in the game folder belongs to the previous launch when it was
   written no later than this launch started and within kCrashNearSec of the previous launch's log's last line - the crash ended
   that launch. Of those, one is attached: a .zip before a .dmp, the newest first. Times are seconds on one clock; `prevLogEnd`
   0 = there is no previous log (nothing is attached).
   ------------------------------------------------------------------------------------------------------------------ */
inline bool CrashFromLastLaunch(long long crashAt, long long prevLogEnd, long long thisLaunchStart)
{
    if (crashAt <= 0 || prevLogEnd <= 0) return false;
    if (crashAt > thisLaunchStart) return false;
    const long long d = crashAt - prevLogEnd;
    return d <= (long long)kCrashNearSec && d >= -(long long)kCrashNearSec;
}
/* "crashDump1.0.68_x64.zip" -> 2, "...dmp" -> 1, anything else 0 (a zip is preferred over a dump of the same crash). */
inline int CrashFileKind(const std::string& name)
{
    std::string l;
    for (size_t i = 0; i < name.size(); ++i) l += (char)((name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i]);
    if (l.compare(0, 9, "crashdump") != 0) return 0;
    if (l.size() > 8 && l.compare(l.size() - 8, 8, "_x64.zip") == 0) return 2;
    if (l.size() > 8 && l.compare(l.size() - 8, 8, "_x64.dmp") == 0) return 1;
    return 0;
}

/* ---------------------------------------------------------------------------------------------------------------------
   NEARBY PLAYERS' LOGS (366, 377). The reporting game sends LOG_ASK by LIVE route AREA for its player's sector - the world
   server delivers it to every other game that has that sector in its delivery area (loaded + one ring), i.e. the nearby
   players. Each answers, without telling its player, with its CURRENT log (scrubbed, compressed, capped to kNearbyLogMax) as
   a bundle in LOG_PARTs by LIVE route SLOT back to the asker. Both are game-to-game messages the world server only relays;
   the receiving game reads them in its store layer (liverelay.h LiveInnerRoadLocal), never in the arrival queue.
     LOG_ASK  (cooplive::kInnerLogAsk)  {u32 ask id, u32 most bytes wanted}                                    8 bytes
     LOG_PART (cooplive::kInnerLogPart) {u32 ask id, u32 part, u32 parts, u32 bundle length, part bytes}       16 + up to kLogPartMax
     bundle  {u32 'KMLB', u32 1, u32 name length, name, u64 raw total, u64 raw dropped, u32 IPs scrubbed, u32 segments,
              segments x {u32 raw length, u32 CRC, u32 compressed length}, the segments' bytes in order}
   ------------------------------------------------------------------------------------------------------------------ */
const unsigned int kBundleMagic = 0x424C4D4Bu;   /* "KMLB" */
const size_t kLogAskBytes = 8, kLogPartHeader = 16;
inline void PutLe32(std::vector<char>* b, unsigned int v) { char t[4]; std::memcpy(t, &v, 4); b->insert(b->end(), t, t + 4); }
inline unsigned int GetLe32(const char* p) { unsigned int v = 0; std::memcpy(&v, p, 4); return v; }
inline void LogAskEncode(std::vector<char>* b, unsigned int askId, unsigned int maxBytes) { b->clear(); PutLe32(b, askId); PutLe32(b, maxBytes); }
inline bool LogAskDecode(const char* p, size_t n, unsigned int* askId, unsigned int* maxBytes)
{
    if (p == 0 || n != kLogAskBytes) return false;
    *askId = GetLe32(p); *maxBytes = GetLe32(p + 4);
    return *askId != 0;
}
/* How many parts a bundle of `total` bytes travels in (at least one). */
inline unsigned int PartCountFor(unsigned int total) { return total == 0 ? 1u : (total + kLogPartMax - 1u) / kLogPartMax; }
/* Part `k` of `bundle`. */
inline void LogPartEncode(std::vector<char>* b, unsigned int askId, const std::vector<char>& bundle, unsigned int k)
{
    b->clear();
    const unsigned int total = (unsigned int)bundle.size();
    const unsigned int parts = PartCountFor(total);
    PutLe32(b, askId); PutLe32(b, k); PutLe32(b, parts); PutLe32(b, total);
    const size_t at = (size_t)k * kLogPartMax;
    if (at < bundle.size())
    {
        const size_t len = (bundle.size() - at) < kLogPartMax ? (bundle.size() - at) : kLogPartMax;
        b->insert(b->end(), bundle.begin() + (long)at, bundle.begin() + (long)(at + len));
    }
}
struct LogPart { unsigned int askId, part, parts, total; const char* data; size_t len; };
inline bool LogPartDecode(const char* p, size_t n, LogPart* out)
{
    if (p == 0 || n < kLogPartHeader) return false;
    out->askId = GetLe32(p); out->part = GetLe32(p + 4); out->parts = GetLe32(p + 8); out->total = GetLe32(p + 12);
    out->data = p + kLogPartHeader; out->len = n - kLogPartHeader;
    if (out->askId == 0 || out->total > kNearbyLogMax + 4096u) return false;
    if (out->parts != PartCountFor(out->total) || out->part >= out->parts) return false;
    const size_t at = (size_t)out->part * kLogPartMax;
    const size_t want = out->total == 0 ? 0 : (((size_t)out->total - at) < kLogPartMax ? ((size_t)out->total - at) : (size_t)kLogPartMax);
    return out->len == want;
}
/* One answering game's parts, put together. Add returns false for a part that disagrees with the first one's shape. */
struct PartsJoin
{
    unsigned int parts, total, have;
    std::vector<char> data;
    std::vector<char> got;
    PartsJoin() : parts(0), total(0), have(0) {}
    bool Add(const LogPart& p)
    {
        if (parts == 0) { parts = p.parts; total = p.total; data.assign(p.total, 0); got.assign(p.parts, 0); }
        if (p.parts != parts || p.total != total) return false;
        if (got[p.part] != 0) return true;   /* a repeat changes nothing */
        if (p.len != 0) std::memcpy(&data[(size_t)p.part * kLogPartMax], p.data, p.len);
        got[p.part] = 1;
        ++have;
        return true;
    }
    bool Complete() const { return parts != 0 && have == parts; }
};
inline std::vector<char> BundleEncode(const std::string& name, const LogPack& p)
{
    std::vector<char> b;
    const std::string nm = name.size() > kNameMaxBytes ? name.substr(0, kNameMaxBytes) : name;
    PutLe32(&b, kBundleMagic); PutLe32(&b, 1); PutLe32(&b, (unsigned int)nm.size());
    b.insert(b.end(), nm.begin(), nm.end());
    PutLe32(&b, (unsigned int)(p.rawTotal & 0xFFFFFFFFu)); PutLe32(&b, (unsigned int)(p.rawTotal >> 32));
    PutLe32(&b, (unsigned int)(p.rawDropped & 0xFFFFFFFFu)); PutLe32(&b, (unsigned int)(p.rawDropped >> 32));
    PutLe32(&b, p.ipsScrubbed); PutLe32(&b, (unsigned int)p.segs.size());
    for (size_t k = 0; k < p.segs.size(); ++k) { PutLe32(&b, p.segs[k].rawLen); PutLe32(&b, p.segs[k].crc); PutLe32(&b, (unsigned int)p.segs[k].bytes.size()); }
    for (size_t k = 0; k < p.segs.size(); ++k) b.insert(b.end(), p.segs[k].bytes.begin(), p.segs[k].bytes.end());
    return b;
}
inline bool BundleDecode(const std::vector<char>& b, std::string* name, LogPack* p)
{
    size_t at = 0;
    if (b.size() < 12 || GetLe32(&b[0]) != kBundleMagic || GetLe32(&b[4]) != 1u) return false;
    const unsigned int nl = GetLe32(&b[8]);
    at = 12;
    if (nl > kNameMaxBytes || b.size() - at < (size_t)nl + 24) return false;
    name->assign(&b[at], (size_t)nl);
    at += nl;
    p->rawTotal = (unsigned long long)GetLe32(&b[at]) | ((unsigned long long)GetLe32(&b[at + 4]) << 32);
    p->rawDropped = (unsigned long long)GetLe32(&b[at + 8]) | ((unsigned long long)GetLe32(&b[at + 12]) << 32);
    p->ipsScrubbed = GetLe32(&b[at + 16]);
    const unsigned int ns = GetLe32(&b[at + 20]);
    at += 24;
    if (ns > 100000u || (b.size() - at) / 12 < ns) return false;
    std::vector<Seg> segs(ns);
    std::vector<unsigned int> clen(ns);
    unsigned long long sum = 0;
    for (unsigned int k = 0; k < ns; ++k)
    {
        segs[k].rawLen = GetLe32(&b[at]); segs[k].crc = GetLe32(&b[at + 4]); clen[k] = GetLe32(&b[at + 8]);
        sum += clen[k];
        at += 12;
    }
    if (sum != (unsigned long long)(b.size() - at)) return false;
    for (unsigned int k = 0; k < ns; ++k)
    {
        if (clen[k] < 4) return false;
        segs[k].bytes.assign((const unsigned char*)&b[at], (const unsigned char*)&b[at] + clen[k]);
        /* every segment ends with the sync marker, so the kept ones can be joined */
        const std::vector<unsigned char>& s = segs[k].bytes;
        if (s[s.size() - 4] != 0x00 || s[s.size() - 3] != 0x00 || s[s.size() - 2] != 0xFF || s[s.size() - 1] != 0xFF) return false;
        at += clen[k];
    }
    p->segs.swap(segs);
    return true;
}
/* The wait for nearby answers is over: kNearbyTotalMs since the ask, or kNearbyFirstMs since it with every game that began
   answering finished. `started` / `finished` count games. */
inline bool NearbyWaitOver(unsigned int sinceAskMs, int started, int finished)
{
    if (sinceAskMs >= kNearbyTotalMs) return true;
    return sinceAskMs >= kNearbyFirstMs && finished >= started;
}

/* ---------------------------------------------------------------------------------------------------------------------
   AN ANSWERING GAME'S MANNERS. Its answer travels on the same guaranteed world-server link as its own play, so:
     - the next part goes only when kPartGapMs have passed since the last one AND the link holds at most kAnswerQueueRoomBytes
       not yet delivered (`queueKnown` = the link can say; one that cannot keeps the plain gap) - the answer uses a link that is
       nearly idle and waits behind play instead of piling up in front of it;
     - an answer still going kNearbyTotalMs after its ask arrived reaches nobody (the asker stopped waiting at least that long
       ago, since it counts from sending) and stops - the message pair has no cancel, so this is also where a cancelled
       report's answers end;
     - one asking slot is answered at most once in kAnswerSlotGapMs (`answeredBefore` = that slot has had an answer since this
       game started, `sinceMs` = since that answer began), so a game that asks again and again cannot keep the link busy;
     - a refused ask is logged at most once in kRefusalLogGapMs; the line counts the refusals since the previous one.
   ------------------------------------------------------------------------------------------------------------------ */
inline bool AnswerPartDue(unsigned int sinceLastMs, bool queueKnown, long long queuedBytes)
{
    if (sinceLastMs < kPartGapMs) return false;
    return !queueKnown || queuedBytes <= kAnswerQueueRoomBytes;
}
inline bool AnswerExpired(unsigned int sinceAskMs) { return sinceAskMs >= kNearbyTotalMs; }
inline bool AskerMayBeAnswered(bool answeredBefore, unsigned int sinceMs) { return !answeredBefore || sinceMs >= kAnswerSlotGapMs; }
inline bool RefusalLogDue(bool loggedBefore, unsigned int sinceMs) { return !loggedBefore || sinceMs >= kRefusalLogGapMs; }
/* A player's name as a zip folder name: path and control characters become '-', at most 24 bytes, never empty. */
inline std::string SafeName(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size() && o.size() < 24; ++i)
    {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7F || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            o += '-';
        else o += (char)c;
    }
    while (!o.empty() && (o[o.size() - 1] == ' ' || o[o.size() - 1] == '.')) o.erase(o.size() - 1);
    return o.empty() ? std::string("player") : o;
}

}   /* namespace coopbug */

#endif
