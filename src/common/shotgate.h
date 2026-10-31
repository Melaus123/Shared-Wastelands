/* src/common/shotgate.h - who fires a copy's bolts and which game decides a bolt's hit (game-to-game protocol 186).
 *
 * WHO FIRES. A copy (a character another game runs) fires when, and only when, the game that runs it says its engine fired:
 * each MSG_SHOT_FIRED (one per real fire there) makes this game fire the copy's real bolt through the engine's own fire
 * routine (GunClass::shoot 0x43A390), on the main thread at the K2 safe point, aimed where the owner aimed (AimHere). The
 * copy's own engine fires (the per-frame ranged update 0x43AB40) are always held (Shoot): the fire routine does not run - no
 * bolt, no sound, the gun keeps its bolt - and the update goes on as after a fire. This game's own characters and characters
 * with no replicated uid are never held. An arrived fire waits up to kFireWaitMs for its copy to be ready (the copy exists, its
 * ranged mode is on and it holds a gun - FireWaitOver); past that it is dropped, counted.
 *
 * AIMED BOLTS. Each copy bolt fired here at a named target (not 0) is recorded (FiredHere) until it strikes that target
 * (HitAimed) or kLifeMs passes. A copy's bolt that strikes our own player character runs the engine's hit here only against
 * such a record for that pair (HitDecideAimed). A bolt its owner fired at no replicated character (target 0), or at another
 * character, is for the look only here: the owner's game never aimed it at our character (its range and aim tests never
 * approved a shot at it) and shows no hit on it either, so a hit here would be one nobody fired. kLifeMs bounds a bolt's
 * flight (a bolt that has not struck by then never will); kCap bolts of one copy in flight are recorded, the oldest dropped.
 *
 * WHO DECIDES A HIT (HitDecide, asked inside the engine's bolt hit Character::iShotYou 0x4398F0 on the game where it runs):
 *   - a bolt on a PLAYER character (or player-faction person) is decided by that character's owner's game, with the copy's
 *     bolt there: the copy's aimed bolt on our own player character runs the engine's hit here (kHitHere; not aimed at it:
 *     kHitBlockNotAimed); our bolt on the copy of another game's player character runs the engine's hit on the copy for the
 *     look only (the copy's damage is skipped there) and nothing is sent (kHitLeave);
 *   - a bolt on an NPC is decided by the shooter's game: our bolt on the copy of another game's NPC runs here and MSG_SHOT
 *     carries the hit to the NPC's game (kHitSend); a copy's bolt on our NPC makes no hit here (kHitBlock);
 *   - a victim not classified yet keeps the shooter's game deciding (kHitSend on the shooter's game; on the victim's game the
 *     copy's bolt is blocked, kHitBlockUnclassified). Each game classifies on its own sweep, so for up to one sweep the two
 *     games can answer differently for one bolt and the hit can land on neither; both sides count it (SentUnclassified on the
 *     shooter's game, kHitBlockUnclassified on the victim's), as melee counts meleeKeptUnclassified;
 *   - our bolt on our own character or on a character with no uid, and a bolt of a character with no uid: the engine's hit
 *     runs as it is (kHitRun).
 * ShotReceiverApplies: the victim's game takes an arrived MSG_SHOT only for a victim it does not classify as a player
 * character - for a player character the copy's bolt here decides, and the message would count the hit twice.
 *
 * MSG_SHOT_FIRED (82, RELIABLE, the shooter's game -> every game in the shooter's area, on the character stream's road):
 *   shooterUid u32 | targetUid u32 (0 = no replicated character) | aim x,y,z f32 (the owner's engine's aim point for that
 *   fire) | targetAt x,y,z f32 (the target's position on the owner's game; 0,0,0 for target 0), little-endian, 32 bytes;
 *   bytes after them are ignored.
 *
 * Pure: no engine memory, no Windows, no allocation; the plugin holds one Store under a lock and the offline suite runs the
 * same functions. Times are millisecond ticks (GetTickCount in the plugin); differences are unsigned, so a wrap is harmless.
 * C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <vector>

namespace shotgate {

const unsigned int kLifeMs     = 5000;   /* an aimed bolt's record, from its fire here */
const unsigned int kFireWaitMs = 1000;   /* how long an arrived fire waits for its copy to be ready */
const int          kCap        = 4;      /* aimed bolts of one copy recorded at once */
const int          kSlots      = 64;     /* copies tracked at once */
const int          kLogPerCopy = 3;      /* hold lines per copy (the plugin's [SHOTGATE] line) */
const size_t       kWireSize   = 32;     /* MSG_SHOT_FIRED */
const float        kAimMax     = 1.0e7f; /* a coordinate's bound on the wire */

/* Who a fire belongs to: shooterMine 1 this game runs it, 0 a copy (uid != 0). Only kGated is ever held. */
enum { kNotCopy = 0, kOwn = 1, kGated = 2 };

inline int RuleFor(unsigned int uid, int mine)
{
    if (uid == 0) return kNotCopy;
    if (mine) return kOwn;
    return kGated;
}

struct Counts
{
    long long held;             /* copy fires of the copy's own engine, held */
    long long holds;            /* runs of holds (a run ends when the copy fires on its owner's word) */
    long long aimed;            /* copy bolts fired here at a named target, recorded */
    long long hitAimed;         /* a recorded bolt struck its target here: the hit runs here */
    long long notAimed;         /* a copy's bolt struck our player character it was not aimed at: the look only */
    long long lapsed;           /* records that aged past kLifeMs */
    long long capped;           /* records dropped because the copy already had kCap */
    long long noSlot;           /* a copy with no free slot: its record not kept / its hold not logged */
    long long passOwn, passNotCopy;
};

struct Slot
{
    unsigned int uid;           /* 0 = free */
    int          n;             /* live records, oldest first in at[] / tgt[] */
    unsigned int at[kCap];
    unsigned int tgt[kCap];     /* the target uid the bolt was fired at */
    unsigned int touched;       /* last record / hold, for reuse of a slot */
    int          holding;       /* 1 while its own fires are being held */
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
    }
    --s->n;
}

/* Drop the records of one slot that have lapsed (they are oldest first). */
inline void Prune(Store* st, Slot* s, unsigned int now)
{
    while (s->n > 0 && !Live(s->at[0], now)) { RemoveAt(s, 0); ++st->c.lapsed; }
}

/* The slot of uid, or 0. */
inline Slot* Find(Store* st, unsigned int uid)
{
    if (uid == 0) return 0;
    for (int i = 0; i < kSlots; ++i)
        if (st->s[i].uid == uid) return &st->s[i];
    return 0;
}

/* The slot of uid, or a free one taken for it: an empty slot first, else the least recently used slot holding no live record.
   0 when every slot holds live records. */
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

/* The copy uid fired one bolt here on its owner's word, at `target` (0 = no replicated character): its run of held own fires
   ends, and a bolt at a named target is recorded. Returns 1 recorded, 0 not (target 0, or no slot). */
inline int FiredHere(Store* st, unsigned int uid, unsigned int target, unsigned int now)
{
    if (uid == 0) return 0;
    Slot* s = (target != 0) ? FindOrMake(st, uid, now) : Find(st, uid);
    if (s == 0) { if (target != 0) ++st->c.noSlot; return 0; }
    s->holding = 0;
    if (target == 0) return 0;
    Prune(st, s, now);
    if (s->n >= kCap) { RemoveAt(s, 0); ++st->c.capped; }
    s->at[s->n] = now;
    s->tgt[s->n] = target;
    ++s->n;
    s->touched = now;
    ++st->c.aimed;
    return 1;
}

/* A bolt of the copy uid struck victim here: 1 (the oldest live record for that pair spent) when one of its bolts was fired
   at victim, 0 when none was. */
inline int HitAimed(Store* st, unsigned int uid, unsigned int victim, unsigned int now)
{
    Slot* s = Find(st, uid);
    if (s != 0 && victim != 0)
    {
        Prune(st, s, now);
        for (int i = 0; i < s->n; ++i)
        {
            if (s->tgt[i] != victim) continue;
            RemoveAt(s, i);
            ++st->c.hitAimed;
            return 1;
        }
    }
    ++st->c.notAimed;
    return 0;
}

/* The engine's fire routine for uid's character, from that character's own engine (the per-frame ranged update). mine: this
   game runs the shooter. Returns 1 when the fire runs, 0 when it is held (every fire of a copy). */
inline int Shoot(Store* st, unsigned int uid, int mine, unsigned int now)
{
    switch (RuleFor(uid, mine))
    {
    case kOwn:    ++st->c.passOwn; return 1;
    case kGated:  break;
    default:      ++st->c.passNotCopy; return 1;
    }
    ++st->c.held;
    Slot* s = FindOrMake(st, uid, now);
    if (s == 0) { ++st->c.noSlot; return 0; }
    s->touched = now;
    if (!s->holding) { s->holding = 1; ++s->holdRuns; ++st->c.holds; }
    return 0;
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

/* ---- which game decides one bolt's hit ----
   shooterMine / victimMine: 1 this game runs it, 0 a copy, -1 no replicated uid; victimPlayer: 1 a player character or
   player-faction person, 0 an NPC, -1 not classified (or no uid). */
enum { kHitRun = 0, kHitSend = 1, kHitLeave = 2, kHitHere = 3, kHitBlock = 4, kHitBlockUnclassified = 5, kHitBlockNotAimed = 6 };

inline int HitDecide(int shooterMine, int victimMine, int victimPlayer)
{
    if (shooterMine == 0)
    {
        if (victimMine == 1 && victimPlayer == 1) return kHitHere;
        if (victimMine == 1 && victimPlayer == -1) return kHitBlockUnclassified;
        return kHitBlock;
    }
    if (shooterMine == 1 && victimMine == 0) return (victimPlayer == 1) ? kHitLeave : kHitSend;
    return kHitRun;
}

/* HitDecide, and a copy's bolt on our player character runs here only when it was fired here at that character (its record
   is spent): otherwise kHitBlockNotAimed. */
inline int HitDecideAimed(Store* st, int shooterMine, unsigned int shooterUid, int victimMine, unsigned int victimUid,
                          int victimPlayer, unsigned int now)
{
    const int how = HitDecide(shooterMine, victimMine, victimPlayer);
    if (how == kHitHere && !HitAimed(st, shooterUid, victimUid, now)) return kHitBlockNotAimed;
    return how;
}

/* Does the engine's hit run on this game for that answer (kHitBlock* = no hit here)? */
inline bool HitRuns(int decided) { return decided != kHitBlock && decided != kHitBlockUnclassified && decided != kHitBlockNotAimed; }

/* The victim's game, before it queues an arrived MSG_SHOT: victimPlayer is its own classification of its victim. */
inline bool ShotReceiverApplies(int victimPlayer) { return victimPlayer != 1; }

/* The shooter's game: our bolt on another game's copy is sent as MSG_SHOT only because the copy is not classified here yet - the
   victim's game may already classify it as a player character, refuse the MSG_SHOT and decide from the copy's bolt there,
   which may not strike it; then the hit lands nowhere. Counted, as melee's meleeKeptUnclassified. */
inline bool SentUnclassified(int shooterMine, int victimMine, int victimPlayer)
{
    return shooterMine == 1 && victimMine == 0 && victimPlayer == -1;
}

/* ---- MSG_SHOT_FIRED ---- */
const int kDecodeOk       = 0;
const int kDecodeTooShort = 1;
const int kDecodeBadUid   = 2;   /* shooter 0, or shooter == target */
const int kDecodeBadAim   = 3;   /* a coordinate not a number or past kAimMax */

struct Fired
{
    unsigned int shooterUid;
    unsigned int targetUid;      /* 0 = no replicated character */
    float        aim[3];         /* the owner's engine's aim point */
    float        targetAt[3];    /* the target's position on the owner's game (0,0,0 for target 0) */
};

inline bool CoordOk(float v) { return v == v && v < kAimMax && v > -kAimMax; }

inline bool FiredUidsOk(const Fired& f) { return f.shooterUid != 0 && f.shooterUid != f.targetUid; }

inline bool FiredAimOk(const Fired& f)
{
    for (int i = 0; i < 3; ++i) if (!CoordOk(f.aim[i]) || !CoordOk(f.targetAt[i])) return false;
    return true;
}

inline bool FiredOk(const Fired& f) { return FiredUidsOk(f) && FiredAimOk(f); }

inline void PutU32(char* p, unsigned int v) { for (int i = 0; i < 4; ++i) p[i] = (char)((v >> (8 * i)) & 0xFF); }
inline unsigned int GetU32(const char* p)
{
    unsigned int v = 0;
    for (int i = 0; i < 4; ++i) v |= (unsigned int)(unsigned char)p[i] << (8 * i);
    return v;
}
inline void PutF32(char* p, float f) { unsigned int v = 0; std::memcpy(&v, &f, 4); PutU32(p, v); }
inline float GetF32(const char* p) { const unsigned int v = GetU32(p); float f = 0.0f; std::memcpy(&f, &v, 4); return f; }

/* false (nothing appended) for a message that would not decode. */
inline bool Encode(std::vector<char>* b, const Fired& f)
{
    if (b == 0 || !FiredOk(f)) return false;
    const size_t at = b->size();
    b->resize(at + kWireSize);
    char* p = &(*b)[at];
    PutU32(p, f.shooterUid);
    PutU32(p + 4, f.targetUid);
    for (int i = 0; i < 3; ++i)
    {
        PutF32(p + 8 + 4 * i, f.aim[i]);
        PutF32(p + 20 + 4 * i, f.targetAt[i]);
    }
    return true;
}

/* *out is written only on kDecodeOk. */
inline int Decode(const char* p, size_t n, Fired* out)
{
    if (p == 0 || n < kWireSize) return kDecodeTooShort;
    Fired f;
    f.shooterUid = GetU32(p);
    f.targetUid = GetU32(p + 4);
    for (int i = 0; i < 3; ++i)
    {
        f.aim[i] = GetF32(p + 8 + 4 * i);
        f.targetAt[i] = GetF32(p + 20 + 4 * i);
    }
    if (!FiredUidsOk(f)) return kDecodeBadUid;
    if (!FiredAimOk(f)) return kDecodeBadAim;
    *out = f;
    return kDecodeOk;
}

/* The aim point here for an arrived fire: the owner's aim point, moved with its target by how far that target stands here from
   where it stood on the owner's game (hereAt, when haveHere: the target's character here and its position read). Returns 1
   moved, 0 the owner's aim point as sent (target 0, the target not here, or a moved point out of bounds). */
inline int AimHere(const Fired& f, int haveHere, const float* hereAt, float* out)
{
    for (int i = 0; i < 3; ++i) out[i] = f.aim[i];
    if (f.targetUid == 0 || !haveHere || hereAt == 0) return 0;
    float m[3];
    for (int i = 0; i < 3; ++i)
    {
        if (!CoordOk(hereAt[i])) return 0;
        m[i] = f.aim[i] + (hereAt[i] - f.targetAt[i]);
        if (!CoordOk(m[i])) return 0;
    }
    for (int i = 0; i < 3; ++i) out[i] = m[i];
    return 1;
}

/* An arrived fire whose copy is still not ready: true once it has waited kFireWaitMs (it is dropped, counted). */
inline bool FireWaitOver(unsigned int arrivedAt, unsigned int now) { return (unsigned int)(now - arrivedAt) >= kFireWaitMs; }

}   /* namespace shotgate */
