/* src/common/uidblock.h - THE NOTEBOOK'S SEATS AND UID BLOCKS (M4 fold, review 2026-09-29 H1; owner decision 205 A; store
 * protocol 58).
 *
 * A uid's 10-bit part is the SEAT the making game holds among the games connected to the notebook NOW (0-1023): the lowest
 * free seat whose counters are not spent, taken when the game is WELCOMEd and freed when its connection ends (unlink, ENet
 * timeout, eviction, HELLO deadline - store_main.cpp PeerGone). The notebook (SharedWastelandsServer.exe) keeps, per SEAT, the HIGHEST
 * counter it has ever handed out, for the world's life (uid_seats.txt, written BEFORE a grant is sent, never stepped back, not a
 * repair file), and grants a game consecutive blocks of kUidBlockSize counters of its seat; the game mints only inside granted
 * blocks (uidlayout.h UidMinter). A game that takes a freed seat continues from that seat's high-water, so no uid ever repeats -
 * not after a restart, not after another player sat there. At most 1,024 games are connected at once with a seat; the 1,025th
 * gets none (counted, one log line). A seat whose counters are spent is never picked; its holder is moved to a free seat with
 * space, else it is told EXHAUSTED.
 *
 * UID_BLOCK (52) - 16 bytes, {u32 kind, u32 seat, u32 lo, u32 hi}:
 *   kind 1 ASK       game -> notebook: give me a block (seat = the seat this game mints under, 0xFFFFFFFF none - information only)
 *   kind 2 GRANT     notebook -> game: counters lo..hi of `seat` are this game's to mint, under that seat
 *   kind 3 EXHAUSTED notebook -> game: this game's seat is spent and no free seat has counters left
 *   kind 4 FAILED    notebook -> game: nothing granted (uid_seats.txt could not be written or read); the game asks again later
 *   kind 5 NO_SEAT   notebook -> game: every seat is held by a connected game; the game asks again later
 *
 * Pure: no global, no OS call. C++03 (VS2010 v100).
 */
#ifndef COOP_COMMON_UIDBLOCK_H
#define COOP_COMMON_UIDBLOCK_H

#include "uidlayout.h"
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstring>

namespace coopuidblk {

const unsigned int kUidBlockSize = 1024;   /* ~1.2 h of play at the measured 722-878 mints per hour; 4,096 blocks per seat */
enum { kUbAsk = 1, kUbGrant = 2, kUbExhausted = 3, kUbFailed = 4, kUbNoSeat = 5 };

struct UidBlockMsg
{
    unsigned int kind, slot, lo, hi;   /* slot = the SEAT (owner 205 A) */
    UidBlockMsg() : kind(0), slot(0), lo(0), hi(0) {}
};

inline void EncodeUidBlock(std::vector<char>* b, const UidBlockMsg& m)
{
    const unsigned int w[4] = { m.kind, m.slot, m.lo, m.hi };
    const size_t at = b->size();
    b->resize(at + 16);
    std::memcpy(&(*b)[at], w, 16);
}

/* Exactly 16 bytes and a known kind; a GRANT or EXHAUSTED names a seat a uid can carry, and a GRANT's range lies inside
   1..UidCounterMax. */
inline bool DecodeUidBlock(const char* p, size_t n, UidBlockMsg* out)
{
    if (p == 0 || n != 16) return false;
    unsigned int w[4];
    std::memcpy(w, p, 16);
    if (w[0] < (unsigned int)kUbAsk || w[0] > (unsigned int)kUbNoSeat) return false;
    if ((w[0] == (unsigned int)kUbGrant || w[0] == (unsigned int)kUbExhausted) && w[1] > coopuid::UidSlotMax) return false;
    if (w[0] == (unsigned int)kUbGrant && (w[2] == 0 || w[2] > w[3] || w[3] > coopuid::UidCounterMax)) return false;
    out->kind = w[0]; out->slot = w[1]; out->lo = w[2]; out->hi = w[3];
    return true;
}

/* seat -> the highest counter ever handed out for it (absent = 0 = none yet), for the world's life. */
struct UidBlockBook { std::map<unsigned int, unsigned int> high; };

inline unsigned int UidBlockHighOf(const UidBlockBook& b, unsigned int seat)
{
    std::map<unsigned int, unsigned int>::const_iterator it = b.high.find(seat);
    return it == b.high.end() ? 0u : it->second;
}

/* THE SEAT a linking game takes: the lowest seat no connected game holds whose counters are not spent. -1 = none (every
   seat held - 1,024 games connected - or every free seat spent). */
inline int SeatPick(const std::set<unsigned int>& held, const UidBlockBook& b)
{
    for (unsigned int s = 0; s <= coopuid::UidSlotMax; ++s)
        if (held.find(s) == held.end() && UidBlockHighOf(b, s) < coopuid::UidCounterMax) return (int)s;
    return -1;
}

/* THE GRANT: the next `size` counters of `seat` after the highest ever handed out (the last block is cut at UidCounterMax).
   1 = granted: *lo..*hi, and the book is raised - the caller writes it BEFORE it sends the grant; 0 = the seat's counters are
   spent; -1 = a seat above 1023 or a size of 0. */
inline int UidBlockGrant(UidBlockBook* b, unsigned int seat, unsigned int size, unsigned int* lo, unsigned int* hi)
{
    if (seat > coopuid::UidSlotMax || size == 0) return -1;
    const unsigned int high = UidBlockHighOf(*b, seat);
    if (high >= coopuid::UidCounterMax) return 0;
    const unsigned int room = coopuid::UidCounterMax - high;   /* >= 1 */
    const unsigned int last = high + (size < room ? size : room);
    b->high[seat] = last;
    *lo = high + 1;
    *hi = last;
    return 1;
}

inline std::string UidBlockU32Text(unsigned int v)
{
    char tmp[16]; int n = 0;
    do { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; } while (v != 0u);
    std::string s;
    while (n > 0) s += tmp[--n];
    return s;
}

inline bool UidBlockParseU32(const std::string& s, unsigned int* out)
{
    if (s.empty() || s.size() > 10) return false;
    unsigned long long v = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10ull + (unsigned long long)(s[i] - '0');
    }
    if (v > 0xFFFFFFFFull) return false;
    *out = (unsigned int)v;
    return true;
}

/* uid_seats.txt: one line per seat, "v1<TAB>seat<TAB>high". */
inline std::string UidBlockBookFormat(const UidBlockBook& b)
{
    std::string s;
    for (std::map<unsigned int, unsigned int>::const_iterator it = b.high.begin(); it != b.high.end(); ++it)
        s += "v1\t" + UidBlockU32Text(it->first) + "\t" + UidBlockU32Text(it->second) + "\n";
    return s;
}

/* Unreadable lines are counted in *bad and skipped (the notebook then grants nothing: a guessed number could repeat uids);
   a seat named twice keeps the HIGHER number - the book never steps back. */
inline void UidBlockBookParse(const std::string& text, UidBlockBook* out, int* bad)
{
    int nb = 0;
    size_t at = 0;
    while (at < text.size())
    {
        size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(at, e - at);
        at = e + 1;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty()) continue;
        const size_t t1 = line.find('\t');
        const size_t t2 = (t1 == std::string::npos) ? std::string::npos : line.find('\t', t1 + 1);
        unsigned int seat = 0, high = 0;
        if (t1 == std::string::npos || t2 == std::string::npos || line.find('\t', t2 + 1) != std::string::npos
            || line.substr(0, t1) != "v1"
            || !UidBlockParseU32(line.substr(t1 + 1, t2 - t1 - 1), &seat) || !UidBlockParseU32(line.substr(t2 + 1), &high)
            || seat > coopuid::UidSlotMax || high > coopuid::UidCounterMax)
        { ++nb; continue; }
        std::map<unsigned int, unsigned int>::iterator it = out->high.find(seat);
        if (it == out->high.end()) out->high[seat] = high;
        else if (high > it->second) it->second = high;
    }
    if (bad != 0) *bad = nb;
}

}   /* namespace coopuidblk */

#endif
