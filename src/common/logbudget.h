/* logbudget.h - QUIET LOGGING FOR THE RELEASE BUILD, and the drift digest's disagreement test.
 *
 * A line that repeats (one per sword-state change, one per copied character every 15 s, ...) costs a player's log most of its
 * size and says little after its first few. In the release build such a line goes through a Gate: the first kFullLines lines of
 * a key are written in full; after that the lines are counted and, at most once per kCountGapMs, one count line says how many
 * were not written. A key is one family of lines (key 0) or one thing inside it (a group name, a world id: KeyOf). The DEV build
 * writes every line as before (coop_log.h, SW_QUIETLOG).
 *
 * Pass(gate, key, nowMs, &held, &spanMs): true = write this line in full. When *held is not 0, the caller writes ONE count line
 * now: *held lines of this key were not written in the last *spanMs milliseconds (the line being judged is one of them). A
 * Gate is plain zeroed data (a static one needs no constructor); it holds kMaxKeys keys, and every key after the table is full
 * shares its last slot. Not thread-safe: two threads on one Gate can only miscount.
 *
 * TakeHeld: a flush. Held counts are otherwise written only when a later line of the same key arrives a minute on, so a key that
 * goes quiet would never say how many of its lines were held; a periodic flush and one before a bug report is packed or the game
 * exits write every key's count since its last line (coop_log.h QuietLogFlush).
 *
 * DriftDisagrees: the release build's [M5] drift line is written only for a copied character whose copy disagrees with what its
 * owner last sent - its live distance from the owner's last streamed position is past the distance at which the drive starts
 * correcting a copy, on kDriftHoldChecks checks in a row (one check taken mid-stride does not count), or dead here and alive by
 * the owner's word (or the reverse). A drift that cannot be read (-1) is not a disagreement and ends a run of far checks.
 *
 * PURE: shared with the offline suite. C++03 (VS2010 v100).
 */
#ifndef SW_LOGBUDGET_H
#define SW_LOGBUDGET_H

namespace swlogbudget {

enum { kFullLines = 5, kMaxKeys = 32 };
const unsigned int kCountGapMs = 60000u;   /* one count line per minute per key, at most */

struct Slot
{
    unsigned long long key;
    unsigned int shown;   /* lines written in full */
    unsigned int held;    /* lines not written since the last full or count line */
    unsigned int since;   /* when the last full or count line was written */
    int used;
};
struct Gate { Slot slots[kMaxKeys]; };

inline Slot* SlotFor(Gate* g, unsigned long long key)
{
    for (int i = 0; i < (int)kMaxKeys; ++i) if (g->slots[i].used && g->slots[i].key == key) return &g->slots[i];
    for (int i = 0; i < (int)kMaxKeys; ++i)
        if (!g->slots[i].used) { g->slots[i].used = 1; g->slots[i].key = key; return &g->slots[i]; }
    return &g->slots[kMaxKeys - 1];
}

inline bool Pass(Gate* g, unsigned long long key, unsigned int nowMs, unsigned int* held, unsigned int* spanMs)
{
    *held = 0; *spanMs = 0;
    Slot* s = SlotFor(g, key);
    if (s->shown < (unsigned int)kFullLines) { ++s->shown; s->since = nowMs; return true; }
    ++s->held;
    const unsigned int span = nowMs - s->since;
    if (span >= kCountGapMs) { *held = s->held; *spanMs = span; s->held = 0; s->since = nowMs; }
    return false;
}

/* A text key (a group name): FNV-1a, 64 bits; a null or empty text is key 0. */
inline unsigned long long KeyOf(const char* text)
{
    if (text == 0 || text[0] == 0) return 0;
    unsigned long long h = 1469598103934665603ull;
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p) { h ^= *p; h *= 1099511628211ull; }
    return h == 0 ? 1 : h;
}

/* From *cursor (start at 0): the next key with lines held back - its key, how many and the span since its last written line - and
   its count started again; false when no key is left. */
inline bool TakeHeld(Gate* g, int* cursor, unsigned int nowMs, unsigned long long* key, unsigned int* held, unsigned int* spanMs)
{
    while (*cursor < (int)kMaxKeys)
    {
        Slot* s = &g->slots[(*cursor)++];
        if (!s->used || s->held == 0) continue;
        *key = s->key; *held = s->held; *spanMs = nowMs - s->since;
        s->held = 0; s->since = nowMs;
        return true;
    }
    return false;
}

enum { kDriftHoldChecks = 2 };   /* far checks in a row before a copy's distance counts as a disagreement */

/* *farChecks: this copy's far checks in a row so far (0 for a new copy); updated here. */
inline bool DriftDisagrees(float driftXZ, float correctDist, int deadHere, int ownerSaysDead, int* farChecks)
{
    if (driftXZ > correctDist) { if (*farChecks < (int)kDriftHoldChecks) ++*farChecks; }
    else *farChecks = 0;
    if (*farChecks >= (int)kDriftHoldChecks) return true;
    return (deadHere != 0) != (ownerSaysDead != 0);
}

/* The drift summary's period in the release build: one line per this long with the characters checked, how many disagreed
   and the worst distance. */
const unsigned int kDriftSummaryMs = 300000u;

}   /* namespace swlogbudget */
#endif
