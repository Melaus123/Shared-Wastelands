/* src/common/blockpick.h - MSG_BLOCK_PICK (80, RELIABLE, game-to-game protocol 184): the block pose the defender's owner's engine
 * picked for a blow, sent to the game that runs the attacker (T-1101 step 5).
 *
 * A defender's block pose is picked during the attacker's swing (CombatTechnique_pickBlock, 05-findings 'T-1101 BLOCK POSE
 * DESIGN READ'), separately on every game. The victim's owner decides every blow, so the defender's owner's pick is the real one:
 * it plays its engine's answer untouched and sends it here - the name, or an empty name when its engine answered no block. The
 * attacker's game shows a copy of the defender: its own picker is asked again (at most kTries) until it answers the carried name,
 * or answers no block for an empty one; otherwise its own pick stands. Its block start is held (Hold, below) until the pick
 * arrives, at most kHoldMaxMs, so the pick is there when its own picker runs.
 *
 * Wire (on the character stream's road): defenderUid u32 | attackerUid u32 | nameLen u8 | name
 *   uids non-zero and different, little-endian; the name 0..hitwire::kNameMax printable ASCII bytes, no NUL (hitwire.h name
 *   rules; length 0 = the defender's owner picked no block). Bytes after the name are ignored.
 *
 * The table (the attacker's game): one entry per (defender, attacker) pair, the newest pick kept. Put on the main thread when a
 * BLOCK_PICK arrives; Hold in the block window check and Take in the pick, both on the engine's AI worker; the caller holds one
 * short lock around each and makes no engine call under it. An entry is taken once; older than kFreshMs it is stale. A Take that
 * finds nothing usable stamps the pair; a pick that arrives within kLateMs of that stamp was too late for the pick it was meant
 * for (counted, and stored already used: no later pick takes it).
 *
 * Pure: no engine memory, no Windows; the offline suite runs the same functions. C++03 (VS2010 v100).
 */
#pragma once

#include <cstddef>
#include <cstring>
#include <vector>

#include "hitwire.h"

namespace blockpick {

const unsigned int kWireMin   = 9;      /* 4 + 4 + 1 + an empty name */
const unsigned int kFreshMs   = 3000;   /* an arrived pick older than this is not used */
const unsigned int kLateMs    = 1000;   /* a pick arriving this soon after an unanswered pick of its pair came too late */
const unsigned int kHoldMaxMs = 300;    /* the longest a block start waits for its pick; picks were seen up to 0.63 s after a swing
                                           began, so the block window is taken to outlast it (not measured) */
const int          kSlots     = 16;     /* pairs remembered at once */
const int          kTries     = 32;     /* asks of the engine's picker for the carried name */

const int kDecodeOk       = 0;
const int kDecodeTooShort = 1;
const int kDecodeBadUid   = 2;
const int kDecodeBadName  = 3;

struct Pick
{
    unsigned int defenderUid;
    unsigned int attackerUid;
    char         name[hitwire::kNameBuf];   /* "" = no block */
};

inline bool PickOk(const Pick& p)
{
    return p.defenderUid != 0 && p.attackerUid != 0 && p.defenderUid != p.attackerUid && hitwire::NameOk(p.name);
}

inline void PutU32(std::vector<char>* b, unsigned int v)
{
    for (int i = 0; i < 4; ++i) b->push_back((char)(unsigned char)((v >> (8 * i)) & 0xFF));
}
inline unsigned int GetU32(const char* p)
{
    return (unsigned int)(unsigned char)p[0] | ((unsigned int)(unsigned char)p[1] << 8) | ((unsigned int)(unsigned char)p[2] << 16)
         | ((unsigned int)(unsigned char)p[3] << 24);
}

/* false (nothing appended) for a pick that would not decode. */
inline bool Encode(std::vector<char>* b, const Pick& p)
{
    if (b == 0 || !PickOk(p)) return false;
    PutU32(b, p.defenderUid);
    PutU32(b, p.attackerUid);
    hitwire::PutName(b, p.name);
    return true;
}

/* kDecodeOk (out filled) or a refusal (out untouched). */
inline int Decode(const char* p, size_t size, Pick* out)
{
    if (p == 0 || size < kWireMin) return kDecodeTooShort;
    Pick w;
    std::memset(&w, 0, sizeof(w));
    w.defenderUid = GetU32(p);
    w.attackerUid = GetU32(p + 4);
    if (w.defenderUid == 0 || w.attackerUid == 0 || w.defenderUid == w.attackerUid) return kDecodeBadUid;
    size_t at = 8;
    const int rc = hitwire::TakeName(p, size, &at, w.name);
    if (rc == hitwire::kDecodeTruncated) return kDecodeTooShort;
    if (rc != hitwire::kDecodeOk) return kDecodeBadName;
    if (out != 0) *out = w;
    return kDecodeOk;
}

/* ---- the attacker's game: the arrived picks ---- */
const int kStateEmpty = 0;   /* no pick for the pair (a miss stamp may still be kept) */
const int kStateFresh = 1;   /* arrived, not yet used */
const int kStateUsed  = 2;   /* used by a pick, found stale, or arrived late */

const int kHoldStateNone = 0;   /* the pair's block start is not held */
const int kHoldStateOn   = 1;   /* held since holdAt, waiting for the pick */
const int kHoldStateDone = 2;   /* the hold that began at holdAt has ended (arrived or capped): not held again until the pair's pick
                                   is made (Take) or the attacker's next swing begins (NewSwing) */

struct Slot
{
    unsigned int def, att;     /* 0 / 0 = a free slot */
    int          state;
    unsigned int at;           /* ::GetTickCount() when the pick arrived */
    unsigned int touched;      /* the last Put, Hold or Take of the pair (the slot given up first is the one touched longest ago) */
    int          missKept;     /* a Take found nothing usable for the pair ... */
    unsigned int missAt;       /* ... at this time */
    int          hold;         /* kHoldState* */
    unsigned int holdAt;       /* the hold's first held call */
    char         name[hitwire::kNameBuf];   /* "" = no block */
};
struct Table
{
    Slot s[kSlots];
};

inline void Reset(Table* t) { std::memset(t, 0, sizeof(*t)); }

/* now - then for two GetTickCount() values; a 'then' after 'now' (read in the other order on another thread) is 0 */
inline unsigned int AgeMs(unsigned int now, unsigned int then)
{
    const unsigned int d = now - then;
    return d > 0x80000000u ? 0u : d;
}

/* The pair's slot; else a free one; else the one touched longest ago (re-cleared). Never 0. */
inline Slot* SlotFor(Table* t, unsigned int def, unsigned int att, unsigned int now)
{
    Slot* freeSlot = 0;
    Slot* oldest = 0;
    unsigned int oldestAge = 0;
    for (int i = 0; i < kSlots; ++i)
    {
        Slot* x = &t->s[i];
        if (x->def == def && x->att == att) return x;
        if (x->def == 0 && x->att == 0) { if (freeSlot == 0) freeSlot = x; continue; }
        const unsigned int age = AgeMs(now, x->touched);
        if (oldest == 0 || age > oldestAge) { oldest = x; oldestAge = age; }
    }
    Slot* r = (freeSlot != 0) ? freeSlot : oldest;
    std::memset(r, 0, sizeof(*r));
    r->def = def;
    r->att = att;
    return r;
}

/* The pair's slot, or 0 (nothing made). */
inline Slot* Find(Table* t, unsigned int def, unsigned int att)
{
    for (int i = 0; i < kSlots; ++i)
        if (t->s[i].def == def && t->s[i].att == att) return &t->s[i];
    return 0;
}

/* A pick arrived (main thread); name "" = no block. 1 = it came within kLateMs after a pick of its pair that found nothing
   (late: stored already used), 0 = in time, -1 = refused (a bad pick, nothing stored). */
inline int Put(Table* t, unsigned int def, unsigned int att, const char* name, unsigned int now)
{
    if (t == 0 || def == 0 || att == 0 || def == att || name == 0 || !hitwire::NameOk(name)) return -1;
    Slot* x = SlotFor(t, def, att, now);
    const int late = (x->missKept && AgeMs(now, x->missAt) <= kLateMs) ? 1 : 0;
    x->missKept = 0;
    x->missAt = 0;
    x->state = late ? kStateUsed : kStateFresh;   /* a late pick was meant for a pick already made: never given to the next one */
    x->at = now;
    x->touched = now;
    hitwire::SetName(x->name, name);
    return late;
}

const int kHoldGo      = 0;   /* go on (answer the engine's own 'in window'): not held - a pick is here already, or this swing's hold ended */
const int kHoldStart   = 1;   /* hold (answer 'not in window'): the first held call of a swing */
const int kHoldWait    = 2;   /* hold: a later call of the same hold, within kHoldMaxMs of its first */
const int kHoldArrived = 3;   /* go: the pick arrived during the hold (once per hold) */
const int kHoldCapped  = 4;   /* go: kHoldMaxMs passed without it (once per hold) */

/* The attacker's game's block start for (def, att) found the swing in its block window (the AI worker): hold it for the defender's
   owner's pick, or go on. Capped by TIME since the hold's first call, never by the number of calls: the block start can be re-run
   faster than its own 0.1 s retry. Peeks at the pick, never takes it (the pick itself does, afterwards). */
inline int Hold(Table* t, unsigned int def, unsigned int att, unsigned int now)
{
    if (t == 0 || def == 0 || att == 0 || def == att) return kHoldGo;
    Slot* x = Find(t, def, att);
    const bool fresh = x != 0 && x->state == kStateFresh && AgeMs(now, x->at) <= kFreshMs;
    if (fresh)
    {
        if (x->hold != kHoldStateOn) return kHoldGo;
        x->hold = kHoldStateDone;
        x->touched = now;
        return kHoldArrived;
    }
    if (x == 0) x = SlotFor(t, def, att, now);
    x->touched = now;
    if (x->hold == kHoldStateNone)
    {
        x->hold = kHoldStateOn;
        x->holdAt = now;
        return kHoldStart;
    }
    if (x->hold == kHoldStateDone) return kHoldGo;
    if (AgeMs(now, x->holdAt) < kHoldMaxMs) return kHoldWait;
    x->hold = kHoldStateDone;
    return kHoldCapped;
}

const int kTakeFresh = 0;   /* out = the carried name ("" = no block); the entry is now used */
const int kTakeNone  = 1;   /* no unused entry for the pair (a miss stamp is kept) */
const int kTakeStale = 2;   /* the entry is older than kFreshMs (now used; a miss stamp is kept) */

/* A pick for (def, att) is being made (the AI worker). out: hitwire::kNameBuf bytes, written only on kTakeFresh. */
inline int Take(Table* t, unsigned int def, unsigned int att, unsigned int now, char* out)
{
    if (t == 0 || def == 0 || att == 0) return kTakeNone;
    Slot* x = Find(t, def, att);
    if (x != 0) { x->touched = now; x->hold = kHoldStateNone; }   /* this swing's block is being picked: the next block start is a new swing's */
    if (x != 0 && x->state == kStateFresh)
    {
        x->state = kStateUsed;
        if (AgeMs(now, x->at) <= kFreshMs)
        {
            if (out != 0) std::memcpy(out, x->name, hitwire::kNameBuf);
            return kTakeFresh;
        }
        x->missKept = 1;
        x->missAt = now;
        return kTakeStale;
    }
    if (x == 0) { x = SlotFor(t, def, att, now); x->touched = now; }
    x->missKept = 1;
    x->missAt = now;
    return kTakeNone;
}

const int kNewSwingOldPick  = 1;   /* a fresh pick was waiting from an earlier swing: spent, never used */
const int kNewSwingHoldLeft = 2;   /* the last hold ended with no pick made (its block start never reached the picker) */

/* The attacker's own swing began (main thread). The defender's game picks only after this swing reaches it, so a pick still
   waiting belongs to an earlier swing: it is spent; the pair's hold and its miss stamp start afresh. Returns kNewSwing* bits. */
inline int NewSwing(Table* t, unsigned int def, unsigned int att)
{
    if (t == 0 || def == 0 || att == 0 || def == att) return 0;
    Slot* x = Find(t, def, att);
    if (x == 0) return 0;
    int r = 0;
    if (x->state == kStateFresh) { x->state = kStateUsed; r |= kNewSwingOldPick; }
    if (x->hold != kHoldStateNone) r |= kNewSwingHoldLeft;
    x->hold = kHoldStateNone;
    x->missKept = 0;
    x->missAt = 0;
    return r;
}

}   /* namespace blockpick */
