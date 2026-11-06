/* src/common/p213trace.h - PROBE P213 (T-980, dev build only): the pure parts of the line-test trace speech.cpp writes
 * for a player's talk order - one tested condition, the conditions list as one log field, and the print cap.
 * Header-only and pure (no game memory, no Windows calls); the plugin and the offline suite compile the same text.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
// PROBE-START: P213
#ifndef SW_P213TRACE_H
#define SW_P213TRACE_H

#include <cstdio>
#include <cstring>

namespace coopprobe {

/* one condition the line test asked: the engine's DialogConditionEnum, ComparisonEnum and value, who it asked about
   (1 the speaker, 0 the listener), the answer the line test got (1 true, 0 false) and rep 1 when that answer is the mod's
   own, not the engine's (the engine had said the other) */
struct P213Cond { int type; int cmp; int val; int who; int ans; int rep; };

/* the conditions as "<type>:<cmp>:<val>:<speaker|listener>=<0|1>[*],..." (type in hex; '*' the mod's answer, rep), "none" for
   no condition asked,
   ",+<n>more" when more were asked than kept; a list that does not fit in cap ends in "..." at a whole entry.
   out always ends in a NUL (cap >= 1). Returns the length written. */
inline int P213FormatConds(const P213Cond* c, int n, int more, char* out, int cap)
{
    if (out == 0 || cap <= 0) return 0;
    out[0] = 0;
    if ((n <= 0 || c == 0) && more <= 0)
    {
        if (cap > 4) { std::strcpy(out, "none"); return 4; }
        return 0;
    }
    int len = 0;
    for (int k = 0; k < n && c != 0; ++k)
    {
        char one[96];
        std::sprintf(one, "%s0x%X:%d:%d:%s=%d%s", k ? "," : "", (unsigned int)c[k].type, c[k].cmp, c[k].val,
                     c[k].who ? "speaker" : "listener", c[k].ans, c[k].rep ? "*" : "");
        const int w = (int)std::strlen(one);
        if (len + w + 4 > cap)
        {
            if (len + 4 <= cap) { std::strcpy(out + len, "..."); len += 3; }
            return len;
        }
        std::strcpy(out + len, one);
        len += w;
    }
    if (more > 0)
    {
        char tail[32];
        std::sprintf(tail, "%s+%dmore", len ? "," : "", more);
        const int w = (int)std::strlen(tail);
        if (len + w + 1 <= cap) { std::strcpy(out + len, tail); len += w; }
        else if (len + 4 <= cap) { std::strcpy(out + len, "..."); len += 3; }
    }
    return len;
}

/* the print cap: true while fewer than cap lines were written this launch */
inline bool P213PrintDue(long printed, long cap) { return printed >= 0 && printed < cap; }

}   /* namespace coopprobe */

#endif
// PROBE-END: P213
