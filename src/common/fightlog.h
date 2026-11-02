/* src/common/fightlog.h - the [FIGHT] lines: one line per swing start and per melee hit between player characters, on both
 * games, naming the attack move, the block move, the result and the stumble (hit reaction) the engine chose.
 *
 * Each game's engine picks the attack move, the block move and the stumble of a blow by its own dice, so the two screens can
 * show different animations for the same blow. These lines put the names side by side for a readout.
 *
 * The plugin reads the names from engine memory under __try (combat.cpp) and hands the raw bytes to CleanName; the event
 * goes into one Ring under a lock (the swing and the decided hit run on the engine's AI worker, the replayed hit on the main
 * thread) and the main thread takes the events and writes the lines. At most kCap events are ever accepted, so the log holds
 * at most kCap [FIGHT] lines; the kCap-th line carries the cap note.
 *
 * Pure: no engine memory, no Windows, no allocation; the offline suite runs the same functions. C++03 (VS2010 v100).
 */
#pragma once

#include <cstddef>
#include <cstring>

namespace fightlog {

const int kNameMax = 48;                  /* the longest name printed; a longer one prints as '?' */
const int kNameBuf = kNameMax + 1;
const int kCap     = 600;                 /* [FIGHT] lines per game */
const int kRing    = 64;                  /* events waiting for the main thread */

const int kSwing = 0;
const int kHit   = 1;
const int kBlockPick = 2;                 /* T-1101 step 5: the block pose a game's engine picked, at pick time */
const int kReady     = 3;                 /* T-890 fightReady: the getting-ready timer set from a blow's real result (fightready.h) */
/* how a block pick was made (Event.how) */
const int kHowEngine     = 0;             /* the engine's own pick, nothing carried (the defender's owner: the pick it plays and sends) */
const int kHowSame       = 1;             /* the attacker's game: the engine's first pick was the carried one */
const int kHowForced     = 2;             /* the attacker's game: asked again until it answered the carried one */
const int kHowNotInList  = 3;             /* the attacker's game: the carried one never came - the engine's first pick */
const int kHowNoEntry    = 4;             /* the attacker's game: no carried pick for the pair (not arrived, or already used) */
const int kHowStale      = 5;             /* the attacker's game: the carried pick was older than blockpick.h kFreshMs */
const int kHowForcedNone = 6;             /* the attacker's game: the defender's owner picked no block - answered no block */
const int kSideDecide = 0;                /* the victim's game: the engine decides the blow here */
const int kSideReplay = 1;                /* the other game: the received HIT is played through the engine on the victim's copy */

/* out (kNameBuf bytes) = src[0..n) when it is a name worth printing: 1..kNameMax characters, every one printable ASCII and no
   quote (the line puts names in '...'); otherwise "?". Returns 1 when the name was copied. */
inline int CleanName(const char* src, size_t n, char* out)
{
    out[0] = '?'; out[1] = 0;
    if (src == 0 || n == 0 || n > (size_t)kNameMax) return 0;
    for (size_t i = 0; i < n; ++i)
    {
        const unsigned char ch = (unsigned char)src[i];
        if (ch < 0x20 || ch > 0x7E || ch == '\'') return 0;
    }
    std::memcpy(out, src, n);
    out[n] = 0;
    return 1;
}

/* out = "-": no object to name (no move, no block, no new stumble) */
inline void NoName(char* out) { out[0] = '-'; out[1] = 0; }

struct Event
{
    int          kind;                   /* kSwing / kHit / kBlockPick */
    unsigned int a;                      /* swing: the swinging character's uid; hit: the victim's; block pick: the defender's */
    unsigned int b;                      /* swing: the target's uid (0 unknown); hit / block pick: the attacker's */
    int          own;                    /* swing: 1 this game's own character, 0 the copy of another game's; block pick: the defender's */
    int          how;                    /* block pick: kHow* */
    int          result;                 /* hit: the engine's melee-hit answer, 5 blocked / 0 dodged or no hit / 2 hit */
    int          side;                   /* hit: kSideDecide / kSideReplay */
    char         move[kNameBuf];         /* swing: the move started; hit: the attacker's current attack move */
    char         block[kNameBuf];        /* hit: the victim's block move; block pick: the pick ('-' none) */
    char         stumble[kNameBuf];      /* hit: the stumble the hit played */
    int          combo;                  /* ready: the blow's comboId */
    int          held;                   /* ready: 1 the timer was still held (written), 0 it was counting (added) */
    int          why;                    /* ready: fightready.h kWhy*, kWhyNone = written or added */
    float        before, after;          /* ready: the attacker's +0x148 before and after */
};

struct Ring
{
    Event        e[kRing];
    unsigned int w, r;                   /* events written / taken; w - r is the number waiting */
    int          accepted;               /* events ever accepted; never more than kCap */
    int          written;                /* lines the main thread wrote */
    long long    lost;                   /* events refused because kRing were waiting, not yet reported */
};

inline void Reset(Ring* g) { std::memset(g, 0, sizeof(*g)); }

/* 1 = accepted. 0 = the cap is reached (nothing more is ever accepted) or the ring is full (counted in lost). */
inline int Push(Ring* g, const Event& ev)
{
    if (g->accepted >= kCap) return 0;
    if (g->w - g->r >= (unsigned int)kRing) { ++g->lost; return 0; }
    g->e[g->w % (unsigned int)kRing] = ev;
    ++g->w;
    ++g->accepted;
    return 1;
}

/* Copies up to max waiting events into out, oldest first; *lost = the refused events since the last call (then 0). */
inline int Take(Ring* g, Event* out, int max, long long* lost)
{
    int n = 0;
    while (n < max && g->r != g->w)
    {
        out[n++] = g->e[g->r % (unsigned int)kRing];
        ++g->r;
    }
    *lost = g->lost;
    g->lost = 0;
    return n;
}

/* Called once per line written; 1 when this line is the last one the cap allows (it carries the cap note). */
inline int LineIsLast(Ring* g)
{
    ++g->written;
    return g->written == kCap ? 1 : 0;
}

}   /* namespace fightlog */
