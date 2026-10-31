/* src/common/swinggate.h - the swing credit: a copy of another player's character starts a swing only after its owner's real
 * swing; NPC copies are not gated.
 *
 * The copy's own engine AI is released while it fights (combat.cpp ApplyRemoteCombatMode), so on its own it swings whenever
 * its engine sees a chance - also while its owner's character stands on BLOCK and swings nothing. Each MSG_SWING from the
 * owner's game (one per real swing start there, naming the swing's target) gives that copy ONE credit for that target. At the
 * engine's attack start (AttackState::initialiseAttack, combat.cpp detour_sgAttackStart) the copy needs a live credit for the
 * attack's target (Before): without one it gets the engine's own 'no technique for this gap' answer (0) and no swing starts.
 * With one, the engine's attack start runs, and the credit is spent only when the engine really started the swing (After):
 * the engine also answers 0 itself when no technique fits the gap, and a credit burnt on that refusal would lose the owner's
 * real blow, since the engine asks again next frame.
 *
 * A credit matches an attack whose target is the credit's target; a credit with target 0 matches any attack; an attack whose
 * target cannot be read or is no replicated character (0) matches only target-0 credits - a blow the owner threw at one
 * character never lands on another here.
 *
 * A credit is an EVENT (the owner's swing that the copy has not yet thrown) and lives only while that swing goes on at its
 * owner: a busy copy (blocking, staggering, still in its previous swing) may throw the blow late, but only until the owner's
 * swing ends. It is spent by the copy's next STARTED swing at that target (After); dropped when the owner's swing ends
 * (OwnerEnd, on MSG_SWING_END from the owner's game: the owner throws one swing at a time, so every credit held then belongs to
 * an ended swing - a blow the copy did not throw, and so a blow that does no damage, since the victim's owner decides a blow
 * when the attacker's copy's swing hits); dropped when the owner's next swing arrives (SUPERSEDE in Add: the older swing has
 * ended even if its SWING_END was lost); dropped at the owner's combat-off (DropAll). So a copy holds at most one credit
 * (kCap 1). kLifeMs 5000 is only a safety limit, counted from the credit's ARRIVAL here, for a credit whose SWING_END and
 * next SWING both never come; nothing else happens when a credit lapses: the copy may no longer use it.
 *
 * Each credit also keeps the attack move its owner's swing started (MSG_SWING, hitwire.h). MoveFor names the move of the
 * credit an attack start would spend; the plugin plays that move on the copy when the copy's own move list for the gap holds
 * it (combat.cpp detour_fpPickAttack). The move goes with its credit: spent, lapsed, superseded or dropped alike.
 *
 * The decision reads the copy's state at the moment of the attack start (owner, player character or NPC, the target) - never
 * a value cached earlier.
 *
 * Pure: no engine memory, no Windows, no allocation; the plugin holds one Store under a lock and the offline suite runs the
 * same functions. Times are millisecond ticks (GetTickCount in the plugin); differences are unsigned, so a wrap is harmless.
 * C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>

namespace swinggate {

const unsigned int kLifeMs     = 5000;   /* a credit's safety limit, from its arrival (it is normally spent or dropped first) */
const int          kCap        = 1;      /* credits one copy may hold: a new owner swing supersedes the older credit */
const int          kSlots      = 64;     /* copies tracked at once */
const int          kLogPerCopy = 3;      /* hold lines per copy (the plugin's [SWINGGATE] line) */
const int          kMoveBuf    = 49;     /* an attack move's name and its NUL (hitwire.h kNameBuf) */

/* Who an attack start belongs to. Only kGated is ever declined. */
enum { kNotCopy = 0, kOwn = 1, kNpc = 2, kUnclassified = 3, kGated = 4 };

/* Before's answer: run the engine's attack start and stop; run it and report its answer to After; decline it. */
enum { kRun = 0, kRunThenSettle = 1, kDecline = 2 };

/* uid: the character's replicated uid (0 = not a replicated character - a world NPC); mine: this game runs it; pcClass: the
   player-character table's answer (1 a player character / player-faction person, 0 an NPC, -1 not classified yet). An
   unclassified copy keeps today's behaviour (counted) - the table classifies a new row within about half a second. */
inline int RuleFor(unsigned int uid, int mine, int pcClass)
{
    if (uid == 0) return kNotCopy;
    if (mine) return kOwn;
    if (pcClass == 1) return kGated;
    if (pcClass == 0) return kNpc;
    return kUnclassified;
}

/* A credit's target against the attack's target (0 = any / unknown). */
inline bool Matches(unsigned int creditTarget, unsigned int attackTarget)
{
    return creditTarget == 0 || (attackTarget != 0 && creditTarget == attackTarget);
}

struct Counts
{
    long long added;            /* credits stored (one per owner swing of a player character's copy) */
    long long allowed;          /* attack starts the engine started on a credit (the credit spent) */
    long long declined;         /* attack starts refused for want of a matching live credit (the engine retries each frame) */
    long long lapsed;           /* credits that aged past kLifeMs unused */
    long long capped;           /* credits dropped because the copy already held kCap - stays 0: a new credit supersedes (kept for the REPORT) */
    long long holds;            /* runs of declines (a run ends at the next started swing) */
    long long noSlot;           /* a copy with no free slot: its credit dropped / its start declined */
    long long passOwn, passNpc, passUnclassified, passNotCopy;
    long long engineNo;         /* a credit held, the engine's attack start ran and itself started no swing: credit kept */
    long long allowedNoCredit;  /* the engine started the swing but the credit was gone by then (lapsed in between) */
    long long dropCombatOff;    /* credits dropped unused because the owner's combat-off arrived (DropAll) */
    long long ownerEndDrop;     /* credits dropped unused because the owner's swing ended (OwnerEnd): owner blows the copy did not throw */
    long long supersededDrop;   /* credits dropped unused because the owner's next swing arrived (Add): an older swing, its end not seen */
};

struct Slot
{
    unsigned int uid;           /* 0 = free */
    int          n;             /* live credits, oldest first in at[] / tgt[] */
    unsigned int at[kCap];
    unsigned int tgt[kCap];     /* the owner's swing's target uid, 0 = any */
    char         mv[kCap][kMoveBuf];   /* the owner's swing's attack move by name, "" = not carried */
    unsigned int touched;       /* last add / attack start, for reuse of a slot */
    int          holding;       /* 1 while its starts are being declined */
    int          holdRuns;
    int          logged;        /* hold runs already given a log line */
};

struct Store
{
    Slot   s[kSlots];
    Counts c;
};

inline void Reset(Store* st) { std::memset(st, 0, sizeof(*st)); }

inline bool Live(unsigned int at, unsigned int now) { return (unsigned int)(now - at) < kLifeMs; }

inline void RemoveAt(Slot* s, int k)
{
    for (int i = k + 1; i < s->n; ++i)
    {
        s->at[i - 1] = s->at[i];
        s->tgt[i - 1] = s->tgt[i];
        std::memcpy(s->mv[i - 1], s->mv[i], kMoveBuf);
    }
    --s->n;
}

/* Drop the credits of one slot that have lapsed (they are oldest first). */
inline void Prune(Store* st, Slot* s, unsigned int now)
{
    while (s->n > 0 && !Live(s->at[0], now)) { RemoveAt(s, 0); ++st->c.lapsed; }
}

/* The oldest live credit of a slot that matches the attack's target, or -1. Call after Prune. */
inline int FindMatch(const Slot* s, unsigned int target)
{
    for (int i = 0; i < s->n; ++i)
        if (Matches(s->tgt[i], target)) return i;
    return -1;
}

/* The slot of uid, or a free one taken for it: an empty slot first, else the least recently used slot holding no live credit.
   0 when every slot holds live credits. */
inline Slot* FindOrMake(Store* st, unsigned int uid, unsigned int now)
{
    Slot* freeSlot = 0;
    Slot* reuse = 0;
    for (int i = 0; i < kSlots; ++i)
    {
        Slot* s = &st->s[i];
        if (s->uid == uid) return s;
        if (s->uid == 0) { if (freeSlot == 0) freeSlot = s; continue; }
        if (freeSlot != 0) continue;
        Prune(st, s, now);
        if (s->n == 0 && (reuse == 0 || (unsigned int)(now - s->touched) > (unsigned int)(now - reuse->touched))) reuse = s;
    }
    Slot* s = (freeSlot != 0) ? freeSlot : reuse;
    if (s == 0) return 0;
    std::memset(s, 0, sizeof(*s));
    s->uid = uid;
    s->touched = now;
    return s;
}

/* move into out (kMoveBuf bytes): "" for 0, or for a name of kMoveBuf bytes or more. */
inline void CopyMove(char* out, const char* move)
{
    out[0] = 0;
    if (move == 0) return;
    size_t n = 0;
    while (n < (size_t)kMoveBuf && move[n] != 0) ++n;
    if (n >= (size_t)kMoveBuf) return;
    std::memcpy(out, move, n);
    out[n] = 0;
}

/* One owner swing at `target` (0 = any) arrived for uid's copy, with the attack move it started (0 or "" = not carried). It
   supersedes every live credit the copy still holds (dropped, counted supersededDrop): those belong to owner swings that have
   ended. Returns 1 stored, 0 no slot. */
inline int Add(Store* st, unsigned int uid, unsigned int target, unsigned int now, const char* move = 0)
{
    if (uid == 0) return 0;
    Slot* s = FindOrMake(st, uid, now);
    if (s == 0) { ++st->c.noSlot; return 0; }
    Prune(st, s, now);
    st->c.supersededDrop += s->n;
    s->n = 0;
    s->at[s->n] = now;
    s->tgt[s->n] = target;
    CopyMove(s->mv[s->n], move);
    ++s->n;
    s->touched = now;
    ++st->c.added;
    return 1;
}

/* The owner's combat-off for uid arrived: every credit its copy still holds is dropped, counted dropCombatOff. Returns the
   number dropped. Takes no slot for a copy that has none. */
inline int DropAll(Store* st, unsigned int uid)
{
    if (uid == 0) return 0;
    for (int i = 0; i < kSlots; ++i)
    {
        Slot* s = &st->s[i];
        if (s->uid != uid) continue;
        const int n = s->n;
        s->n = 0;
        st->c.dropCombatOff += n;
        return n;
    }
    return 0;
}

/* The owner's swing of uid ended (MSG_SWING_END): every live credit its copy still holds is dropped, counted ownerEndDrop (a
   lapsed one is counted lapsed). Returns the number dropped. Takes no slot for a copy that has none. */
inline int OwnerEnd(Store* st, unsigned int uid, unsigned int now)
{
    if (uid == 0) return 0;
    for (int i = 0; i < kSlots; ++i)
    {
        Slot* s = &st->s[i];
        if (s->uid != uid) continue;
        Prune(st, s, now);
        const int n = s->n;
        s->n = 0;
        st->c.ownerEndDrop += n;
        return n;
    }
    return 0;
}

/* The attack move of the credit an attack start of uid's copy at `target` would spend now (the oldest live matching one),
   into out (kMoveBuf bytes); "" when there is no such credit or it carries no move. Spends nothing and takes no slot.
   Returns 1 when a name was copied. */
inline int MoveFor(Store* st, unsigned int uid, unsigned int target, unsigned int now, char* out)
{
    out[0] = 0;
    if (uid == 0) return 0;
    for (int i = 0; i < kSlots; ++i)
    {
        Slot* s = &st->s[i];
        if (s->uid != uid) continue;
        Prune(st, s, now);
        const int k = FindMatch(s, target);
        if (k < 0 || s->mv[k][0] == 0) return 0;
        std::memcpy(out, s->mv[k], kMoveBuf);
        return 1;
    }
    return 0;
}

/* An attack start of uid's character at `target` (0 = unreadable / not replicated). Spends nothing. */
inline int Before(Store* st, unsigned int uid, int mine, int pcClass, unsigned int target, unsigned int now)
{
    switch (RuleFor(uid, mine, pcClass))
    {
    case kOwn:          ++st->c.passOwn; return kRun;
    case kNpc:          ++st->c.passNpc; return kRun;
    case kUnclassified: ++st->c.passUnclassified; return kRun;
    case kGated:        break;
    default:            ++st->c.passNotCopy; return kRun;
    }
    Slot* s = FindOrMake(st, uid, now);
    if (s == 0) { ++st->c.noSlot; ++st->c.declined; return kDecline; }
    Prune(st, s, now);
    s->touched = now;
    if (FindMatch(s, target) >= 0) return kRunThenSettle;
    ++st->c.declined;
    if (!s->holding) { s->holding = 1; ++s->holdRuns; ++st->c.holds; }
    return kDecline;
}

/* After kRunThenSettle: started = the engine's attack start answered non-zero (the swing began). Only then is the oldest
   matching credit spent; an engine refusal keeps it for the engine's next attempt. */
inline void After(Store* st, unsigned int uid, unsigned int target, unsigned int now, int started)
{
    if (!started) { ++st->c.engineNo; return; }
    ++st->c.allowed;
    Slot* s = FindOrMake(st, uid, now);
    if (s == 0) { ++st->c.allowedNoCredit; return; }
    s->holding = 0;
    s->touched = now;
    Prune(st, s, now);
    const int k = FindMatch(s, target);
    if (k >= 0) RemoveAt(s, k);
    else ++st->c.allowedNoCredit;
}

/* Before and After in one call, for the offline suite: engineStarts = what the engine's attack start would answer.
   Returns 1 when the swing starts. */
inline int Gate(Store* st, unsigned int uid, int mine, int pcClass, unsigned int target, unsigned int now, int engineStarts)
{
    const int v = Before(st, uid, mine, pcClass, target, now);
    if (v == kDecline) return 0;
    if (v == kRunThenSettle) After(st, uid, target, now, engineStarts);
    return engineStarts ? 1 : 0;
}

/* Hold runs not yet logged, at most kLogPerCopy per copy and `max` in all: fills uids / runs, marks them logged. */
inline int TakeLogs(Store* st, unsigned int* uids, int* runs, int max)
{
    int n = 0;
    for (int i = 0; i < kSlots && n < max; ++i)
    {
        Slot* s = &st->s[i];
        if (s->uid == 0 || s->logged >= kLogPerCopy || s->holdRuns <= s->logged) continue;
        ++s->logged;
        uids[n] = s->uid;
        runs[n] = s->holdRuns;
        ++n;
    }
    return n;
}

}   /* namespace swinggate */
