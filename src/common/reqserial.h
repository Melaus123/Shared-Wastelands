/* T-336 - THE REQUEST COUNTER'S RANDOM BASE PER LAUNCH (the industry-standard random initial sequence number).
 *
 * This game's request counter (items.cpp g_reqNextId) names every request it sends another game - box and shop moves, ground
 * TAKEs, and the drop serial every PUT of one ground drop carries (inv5p1b 6). The holder of a ground area remembers the PUTs it
 * served by (peer, serial) for kGrServedKeepMs, and answers a second PUT with the same pair "already done" without building.
 * The counter used to start at 1, and a game that restarted keeps its peer id, so its first NEW drop after a relaunch carried
 * serial 1 again and was swallowed as a repeat: the item showed as held by the holder and the holder never built it (T711).
 *
 * SerialBase mixes (FNV-1a) four readings taken once per process launch - the tick count, the process id and both halves of the
 * performance counter - so a relaunched game starts somewhere unrelated to its previous run. It is never 0 (0 = none).
 * SerialTake hands out the counter's current value and steps it, skipping 0 across the wrap. Pure: shared with the offline suite. */
#ifndef COOP_REQSERIAL_H
#define COOP_REQSERIAL_H

namespace coopreq {

inline unsigned int SerialBase(unsigned int tick, unsigned int pid, unsigned int qpcLow, unsigned int qpcHigh)
{
    unsigned int h = 2166136261u;
    const unsigned int v[4] = { tick, pid, qpcLow, qpcHigh };
    for (int i = 0; i < 4; ++i) for (int k = 0; k < 4; ++k) { h ^= (v[i] >> (8 * k)) & 0xFFu; h *= 16777619u; }
    return (h == 0) ? 1u : h;
}

/* The id to use now; *next moves on and never rests on 0. A counter found at 0 hands out 1. */
inline unsigned int SerialTake(unsigned int* next)
{
    const unsigned int id = (*next == 0) ? 1u : *next;
    *next = id + 1u;
    if (*next == 0) *next = 1u;
    return id;
}

}   /* namespace coopreq */
#endif
