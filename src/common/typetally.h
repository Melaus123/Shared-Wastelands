/* src/common/typetally.h - WHAT EACH LINK CARRIED, BY MESSAGE TYPE: the count and bytes of every message handed to a link (out) and
 * taken from it (in), by its type byte (0-255), and the inner type of every LIVE envelope (53) that came in. One table per KIND of
 * link, because a type number means different things on different links (59 is TALK between two games, BUNDLE on the world-server
 * link; 53 is PARITY_REQ between two games, LIVE on the world-server link):
 *   kLinkSession - the link between two games;
 *   kLinkWorld   - a game's own link to the world server (on the world server: every game's link, summed);
 *   kLinkBridge  - a hosting game's bridge, both sides: the Steam links of the players it bridges and its own links to the world
 *                  server made for them.
 * Bytes are the message's whole frame (5-byte header + payload); a message that travelled inside a BUNDLE is counted as if it had
 * come alone, so a bundle's own header is not in these numbers (the link's ByteCounts are the wire's).
 *
 * THREAD: NOT locked. Every counting site runs on the thread that already owns its link and counts its own sent/recv beside it - the
 * game's main thread (ENet's and Steam's Poll and Send) and the world server's one loop thread - and the report is taken on that same
 * thread. Pure and header-only: the plugin, the world server and the offline suite compile this one file.
 */
#ifndef NET_TYPETALLY_H
#define NET_TYPETALLY_H

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include "liveenvelope.h"   /* kLiveUpHeader / kLiveDownHeader: where a LIVE envelope's inner type sits */

namespace nettally {

enum { kLinkSession = 0, kLinkWorld = 1, kLinkBridge = 2, kLinks = 3 };
enum { kTypes = 256, kTop = 10 };
const unsigned int kTypeLive = 53;   /* the LIVE envelope on a link to the world server */

struct Cell { long long count; long long bytes; };
struct Dir { Cell c[kTypes]; };
struct Table { Dir out; Dir in; Dir liveIn; };

inline const char* LinkName(int link)
{
    return link == kLinkWorld ? "world" : link == kLinkBridge ? "bridge" : "session";
}

/* Each kind of link's table for the window since the last Take; zero at start (static storage). */
inline Table* Of(int link)
{
    static Table t[kLinks];
    return &t[(link >= 0 && link < kLinks) ? link : kLinkSession];
}

inline void Add(Dir* d, unsigned int type, size_t bytes)
{
    Cell& c = d->c[type & 0xFFu];
    ++c.count;
    c.bytes += (long long)bytes;
}

inline void CountOut(int link, unsigned int type, size_t bytes) { Add(&Of(link)->out, type, bytes); }
inline void CountIn(int link, unsigned int type, size_t bytes)  { Add(&Of(link)->in, type, bytes); }

/* A LIVE envelope's payload (`p`, `n` bytes, after the 5-byte frame header) that came in, by its inner type. `down` = the world
   server's envelope to a game (inner type at byte 2), otherwise a game's envelope toward the world server (byte 1). An envelope too
   short to hold its header is not counted here (it is in `in` as type 53). Bytes = the whole frame, 5 + n. */
inline void CountLiveIn(int link, bool down, const char* p, size_t n)
{
    if (p == 0) return;
    if (down) { if (n >= cooplive::kLiveDownHeader) Add(&Of(link)->liveIn, (unsigned char)p[2], 5 + n); }
    else      { if (n >= cooplive::kLiveUpHeader) Add(&Of(link)->liveIn, (unsigned char)p[1], 5 + n); }
}

/* Type a is listed before type b: more bytes first; on equal bytes more messages; then the lower type number. */
inline bool Before(const Dir& d, int a, int b)
{
    if (d.c[a].bytes != d.c[b].bytes) return d.c[a].bytes > d.c[b].bytes;
    if (d.c[a].count != d.c[b].count) return d.c[a].count > d.c[b].count;
    return a < b;
}

/* The types with the most bytes, at most `max`, in Before order, into `types`; a type with no message is never listed. Returns how many. */
inline int Top(const Dir& d, int* types, int max)
{
    int n = 0;
    for (int t = 0; t < kTypes; ++t)
    {
        if (d.c[t].count == 0) continue;
        int at;
        if (n < max) at = n++;
        else if (max > 0 && Before(d, t, types[max - 1])) at = max - 1;
        else continue;
        types[at] = t;
        while (at > 0 && Before(d, types[at], types[at - 1]))
        {
            const int s = types[at]; types[at] = types[at - 1]; types[at - 1] = s;
            --at;
        }
    }
    return n;
}

inline long long Bytes(const Dir& d)
{
    long long b = 0;
    for (int t = 0; t < kTypes; ++t) b += d.c[t].bytes;
    return b;
}

inline bool Empty(const Table& t)
{
    for (int i = 0; i < kTypes; ++i)
        if (t.out.c[i].count != 0 || t.in.c[i].count != 0 || t.liveIn.c[i].count != 0) return false;
    return true;
}

/* "type:count/bytes,..." for the top ten, most bytes first; "-" when nothing moved. Integers only (no locale reaches them). */
inline std::string DirText(const Dir& d)
{
    int ty[kTop];
    const int n = Top(d, ty, kTop);
    if (n == 0) return "-";
    std::string s;
    char b[80];
    for (int i = 0; i < n; ++i)
    {
        std::sprintf(b, "%s%d:%lld/%lld", i ? "," : "", ty[i], d.c[ty[i]].count, d.c[ty[i]].bytes);
        s += b;
    }
    return s;
}

/* The whole report line for one kind of link over a window of `secs` seconds. totals are every type's bytes, not only the top ten. */
inline std::string ReportLine(int link, long long secs, const Table& t)
{
    char b[96];
    std::string s = "[NET] TRAFFIC REPORT link=";
    s += LinkName(link);
    std::sprintf(b, " secs=%lld", secs);
    s += b;
    s += " out[type:count/bytes]=" + DirText(t.out);
    s += " in[type:count/bytes]=" + DirText(t.in);
    s += " liveIn[inner:count/bytes]=" + DirText(t.liveIn);
    std::sprintf(b, " totals[outB,inB]=%lld,%lld", Bytes(t.out), Bytes(t.in));
    s += b;
    return s;
}

/* Ends the window: copies one kind of link's table into `out` and starts that table again from zero. */
inline void Take(int link, Table* out)
{
    Table* t = Of(link);
    std::memcpy(out, t, sizeof(Table));
    std::memset(t, 0, sizeof(Table));
}

}   /* namespace nettally */

#endif
