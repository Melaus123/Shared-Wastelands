/* linekeep.h - THE FIRST AND THE NEWEST LINES OF ONE KIND, KEPT IN MEMORY (T-910).
 *
 * The bug report carries every [KEEPER] line of this launch in a file of its own (keeper-lines.txt): the log the report also
 * packs can be cut (the log's 10 MB ceiling, the report's size limit), and a cut can take the very lines that say why a shop
 * came up empty. A LineKeep holds the first `firstCap` lines in order and, after them, the newest `lastCap` in a ring; every
 * line is counted (`seen`). Text joins them in time order, each line ending "\r\n", with one gap line where lines between the
 * two were not kept. A line longer than kKeepLineCap is cut there.
 *
 * PURE: shared with the offline suite. Not thread-safe (the log writer calls it under its own lock). C++03 (VS2010 v100).
 */
#ifndef SW_LINEKEEP_H
#define SW_LINEKEEP_H

#include <string>
#include <vector>

namespace swlinekeep {

enum { kKeepFirst = 50, kKeepLast = 200, kKeepLineCap = 1024 };

struct LineKeep
{
    std::vector<std::string> first;   /* the first lines, in order */
    std::vector<std::string> last;    /* the newest lines: a ring once full, its oldest at `next` */
    size_t firstCap, lastCap, next;
    unsigned long long seen;          /* every line handed to Keep */
    LineKeep(size_t f, size_t l) : firstCap(f), lastCap(l), next(0), seen(0) {}
};

inline void Keep(LineKeep* k, const char* text, size_t len)
{
    if (len > (size_t)kKeepLineCap) len = (size_t)kKeepLineCap;
    ++k->seen;
    if (k->first.size() < k->firstCap) { k->first.push_back(std::string(text, len)); return; }
    if (k->lastCap == 0) return;
    if (k->last.size() < k->lastCap) { k->last.push_back(std::string(text, len)); return; }
    k->last[k->next].assign(text, len);
    k->next = (k->next + 1) % k->lastCap;
}

inline std::string NumText(unsigned long long v)
{
    char b[24];
    int i = 23;
    b[i] = 0;
    do { b[--i] = (char)('0' + (int)(v % 10u)); v /= 10u; } while (v != 0 && i > 0);
    return std::string(b + i);
}

/* The kept lines in time order; empty when none were kept. */
inline std::string Text(const LineKeep& k)
{
    std::string out;
    for (size_t i = 0; i < k.first.size(); ++i) { out += k.first[i]; out += "\r\n"; }
    const unsigned long long kept = (unsigned long long)k.first.size() + (unsigned long long)k.last.size();
    if (k.seen > kept)
    {
        const unsigned long long gap = k.seen - kept;
        out += "... not kept: " + NumText(gap) + (gap == 1 ? " line" : " lines") + " between these two (this file keeps the first "
               + NumText(k.firstCap) + " and the newest " + NumText(k.lastCap) + ") ...\r\n";
    }
    for (size_t i = 0; i < k.last.size(); ++i) { out += k.last[(k.next + i) % k.last.size()]; out += "\r\n"; }
    return out;
}

}   /* namespace swlinekeep */
#endif
