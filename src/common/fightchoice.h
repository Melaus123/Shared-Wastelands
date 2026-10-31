/* src/common/fightchoice.h - T-1101 design (c), build A: THE OWNER'S FIGHT CHOICES DRIVE ITS COPIES' CHOICES (message 83
 * FIGHT_CHOICE, game-to-game protocol 187).
 *
 * The engine's fight routine (CombatClassAI 1.0.65 0x60CC10) asks its decide step (1.0.65 0x60BE20 / 1.0.68 0x60C8B0) for the next
 * move each frame a fighter is in state 4 (decomp_60cc10:168-169). The decide step either writes nothing (its 'choose nothing'
 * exit while an action animation still plays, decomp_60be20:9-10) or writes the state +0x1F0 (3 getting ready, 9 no target,
 * 10 approach because the path is blocked) and, with state 3, the next move +0x1F4 (0 attack - the target's threat routine's
 * answer stored at decomp_60bbe0:52 -, 1 block, 5 circle, 6 wait) and the getting-ready timer +0x148 = 0.5 s. The decide step
 * picks no attack MOVE: the move is picked when the swing starts (AttackState::initialiseAttack -> CombatTechnique_pickAttack),
 * and SWING already carries it (hitwire.h).
 *
 * OWNER SIDE (the game that runs a player character): each decide call of its own player character in combat mode that changes
 * the state, the next move or the target is one Choice, numbered from one per-game counter (so each uid's numbers rise) and sent
 * as FIGHT_CHOICE on SWING's road and lane. The decide step runs on the AI worker thread, so the Choice waits in a fixed queue
 * (OwnerPush) that the main thread empties (OwnerTake).
 *
 * COPY SIDE (every other game): the main thread keeps each copy's newest arrived Choice in its row (Put; an older or equal number
 * is stale, a newer one replaces an unused one - superseded). On the AI worker, the decide-step detour asks Gate for a copy of
 * another player's character in combat mode, in state 4:
 *   kGateApply  an unused Choice is waiting (at most kStaleMs old): the engine's decide step runs, then PlanFor says what to write
 *               (Settle counts it);
 *   kGateHold   none is waiting and the copy's owner is sending (a choice for it arrived within kTrafficMs): 'choose nothing'
 *               this frame - the decide step is not run and nothing is written, as its own 'choose nothing' exit; at most
 *               kHoldMs from the first held call of this wait (heldNothing counts the waits);
 *   kGateRun    the hold ran out (holdExpired, once per wait); no choice arrived within kTrafficMs when the wait began
 *               (runNoTraffic, once per wait: the owner's hook is off, the two games disagree whether it is a player character,
 *               or the link is down - the copy is not left standing unguarded); or the copy has no row (noRow, once per wait):
 *               the engine's own decide step, as before.
 * A wait ends when the copy's engine leaves state 4 (AfterRun / Settle); its next state 4 starts a new wait.
 * kHoldMs 600: the owner's choice reaches the copy in tens of ms at the median and 90% within ~0.5 s (T1662: max ~0.97 s), so a
 * wait that starts when the copy reaches state 4 together with its owner covers ~90% of the choices; a longer hold leaves the
 * copy standing in its fight stance (no guard: state 4 runs neither swing nor block, decomp_60c3a0) for longer when the owner's
 * choice is late or lost.
 * kStaleMs 1000: an owner choice starts a 0.5 s getting-ready and then its move; one that waited here longer than 1 s describes a
 * move its owner is already making or has made (staleAge; a repeated or older number is staleSeq).
 * kTrafficMs 5000: a fighting owner's choices are ~1-2.5 s apart and 90% arrive within ~0.5 s (T1662: max ~0.97 s), so a sending
 * owner's next choice is due within ~3.5 s of the last; 5 s without one means it is not sending to this copy.
 *
 * PlanFor - what the copy writes after the engine's decide step ran, in BUILD A: only the next move +0x1F4, and only when both the
 * owner and the copy's engine chose state 3 (getting ready): every state-3 branch of the decide step has already written the
 * state 3 and the 0.5 s timer, so the next move is the one field that differs, and the decide step writes each of 0 / 1 / 5 / 6
 * there on this thread at this point. The target +0x290 is never written: the engine changes it only through 0x664AF0, which also
 * tells the old and new targets who attacks them (0x5C8800) - an owner target that differs from the engine's is counted
 * (targetKept). The copy's engine still in state 4 after the call (its own 'choose nothing' exit: an action animation plays) -
 * nothing written, the Choice waits (busy). Owner state 9 / 10, or the copy's engine in 9 / 10 - the engine's choice is kept
 * (kept); approach and stagger are build B.
 *
 * FIGHT_CHOICE (83), kChoiceBytes = 20, little-endian; bytes after them are ignored:
 *   uid u32 | targetUid u32 (0 = none / not a known character) | seq u32 | state u8 | next u8 | 0 u8 | 0 u8 | readyTimer f32
 * Refused: uid 0 or the uid its own target (kBadUid); a state other than 3 / 9 / 10, state 3 with a next move other than
 * 0 / 1 / 5 / 6, state 10 with a next move other than 10 (kBadMove); a pad byte not 0 or a timer that is not a finite number in
 * [-1000, 1000] (kBadField). Nothing is written on a refusal.
 *
 * Pure: no engine memory, no Windows, no lock - the plugin holds its own lock around every call that takes a Store. No
 * allocation. C++03 (VS2010 v100).
 */
#ifndef COOP_COMMON_FIGHTCHOICE_H
#define COOP_COMMON_FIGHTCHOICE_H

#include <cstddef>
#include <cstring>
#include <vector>

namespace fightchoice {

const unsigned int kMsgFightChoice = 83;   /* net/transport.h MSG_FIGHT_CHOICE, liveenvelope.h kInnerFightChoice, spawnpace.h kMsgFightChoice */
const size_t       kChoiceBytes = 20;

const int kDecodeOk = 0, kDecodeTruncated = 1, kDecodeBadUid = 2, kDecodeBadMove = 3, kDecodeBadField = 4;
const int kNotOwner = 16;   /* not a decode answer: the sender does not run the character (session.cpp OnFightChoice) */

const int          kRows     = 16;      /* copies of other players' characters followed at once */
const int          kQueue    = 64;      /* owner choices waiting for the main thread */
const int          kLogFirst = 30;      /* applied choices named one line each, a launch */
const unsigned int kHoldMs   = 600;     /* see above */
const unsigned int kStaleMs  = 1000;    /* see above */
const unsigned int kTrafficMs  = 5000;  /* see above */
const unsigned int kRowIdleMs  = 30000; /* a row unused this long may be taken by another copy, even one left waiting or holding:
                                           a copy in a fight uses its row at each decision, so this one's copy died, unloaded or is lost */
const unsigned int kSeqResetMs = 10000; /* after this long without an arrival any number is taken (the owner's game restarted) */

/* ms from `then` to `now` on the tick counter, across its wrap; a stamp another thread took a moment after `now` reads 0, never as
   a huge age */
inline unsigned int AgeMs(unsigned int now, unsigned int then) { const int d = (int)(now - then); return d > 0 ? (unsigned int)d : 0u; }

struct Choice
{
    unsigned int uid, targetUid, seq;
    int          state, next;
    float        readyTimer;   /* the owner's +0x148 after its decide step (0.5 on every state-3 branch) */
};

/* the decide step's own answers this message may carry (see the header comment) */
inline bool Sendable(int state, int next)
{
    if (state == 3) return next == 0 || next == 1 || next == 5 || next == 6;
    if (state == 10) return next == 10;
    return state == 9;
}

inline const char* NextName(int next)
{
    switch (next)
    {
    case 0:  return "attack";
    case 1:  return "block";
    case 5:  return "circle";
    case 6:  return "wait";
    case 10: return "approach";
    default: return "other";
    }
}

inline void PutU32(char* p, unsigned int v) { p[0] = (char)(v & 0xFF); p[1] = (char)((v >> 8) & 0xFF); p[2] = (char)((v >> 16) & 0xFF); p[3] = (char)((v >> 24) & 0xFF); }
inline unsigned int GetU32(const char* p)
{
    return (unsigned int)(unsigned char)p[0] | ((unsigned int)(unsigned char)p[1] << 8) | ((unsigned int)(unsigned char)p[2] << 16)
         | ((unsigned int)(unsigned char)p[3] << 24);
}
inline bool TimerOk(float t) { return t == t && t >= -1000.0f && t <= 1000.0f; }

/* Appends one FIGHT_CHOICE. False (nothing appended): no buffer or a Choice Decode would refuse. */
inline bool Encode(std::vector<char>* b, const Choice& c)
{
    if (b == 0 || c.uid == 0 || c.uid == c.targetUid || !Sendable(c.state, c.next) || !TimerOk(c.readyTimer)) return false;
    char p[kChoiceBytes];
    PutU32(p, c.uid);
    PutU32(p + 4, c.targetUid);
    PutU32(p + 8, c.seq);
    p[12] = (char)(unsigned char)c.state;
    p[13] = (char)(unsigned char)c.next;
    p[14] = 0;
    p[15] = 0;
    unsigned int t = 0;
    std::memcpy(&t, &c.readyTimer, 4);
    PutU32(p + 16, t);
    b->insert(b->end(), p, p + kChoiceBytes);
    return true;
}

/* kDecodeOk and *out written, or a refusal with *out untouched (see the header comment). */
inline int Decode(const char* p, size_t n, Choice* out)
{
    if (p == 0 || out == 0 || n < kChoiceBytes) return kDecodeTruncated;
    Choice c;
    c.uid = GetU32(p);
    c.targetUid = GetU32(p + 4);
    c.seq = GetU32(p + 8);
    c.state = (unsigned char)p[12];
    c.next = (unsigned char)p[13];
    const unsigned int t = GetU32(p + 16);
    std::memcpy(&c.readyTimer, &t, 4);
    if (c.uid == 0 || c.uid == c.targetUid) return kDecodeBadUid;
    if (!Sendable(c.state, c.next)) return kDecodeBadMove;
    if (p[14] != 0 || p[15] != 0 || !TimerOk(c.readyTimer)) return kDecodeBadField;
    *out = c;
    return kDecodeOk;
}

/* What the copy writes once the engine's decide step ran (see the header comment). engineState = the copy's state after the
   call (-1 unreadable). */
enum { kPlanWrite = 0, kPlanBusy = 1, kPlanKept = 2 };
inline int PlanFor(int ownerState, int ownerNext, int engineState)
{
    if (engineState == 4) return kPlanBusy;
    if (ownerState == 3 && engineState == 3 && Sendable(ownerState, ownerNext)) return kPlanWrite;
    return kPlanKept;
}

struct LegCounts
{
    long long recv, applied, targetKept, heldNothing, holdExpired, staleSeq, staleAge, kept, busy, superseded, runNoTraffic;
};

struct Counts
{
    long long sent, recv, applied, targetKept, heldNothing, holdExpired, staleSeq, staleAge, queueFull;   /* the [M6] REPORT's first nine, in order */
    long long kept, busy, superseded, unsent, noRow, refused, notCopy, runNoTraffic, notSendable, notOwner;
};

struct Row
{
    unsigned int uid;                 /* 0 = free */
    int          waiting;             /* 1: w is an unused owner choice */
    int          wBusy;               /* 1: w met the copy's engine busy at least once (busy counts each choice once) */
    Choice       w;
    unsigned int wArrivedMs;
    int          seqKnown;
    unsigned int lastSeq, lastArriveMs;
    int          holding;             /* 1: this wait in state 4 has held; holdStartMs is its first held call */
    unsigned int holdStartMs;
    int          holdOver;            /* 1: this wait's hold ran out (counted once) */
    int          quiet;               /* 1: this wait began with no recent arrival - the engine runs for the rest of it (counted once) */
    unsigned int lastUseMs;
    LegCounts    leg;
};

struct Sent
{
    Choice c;
    double t;                         /* the owner's decide time, seconds on its own clock (for the log) */
    unsigned int decidedMs;
};

struct Applied
{
    unsigned int uid, seq, ownerTarget, engineTarget, waitedMs;
    int          next;
    double       t;
};

struct Store
{
    Row          rows[kRows];
    Counts       c;
    unsigned int lastSeq;             /* owner side: the last number handed out */
    Sent         q[kQueue];
    unsigned int qHead, qTail;        /* qTail - qHead entries wait */
    Applied      log[kLogFirst];
    int          logN;
    unsigned int noRowUid[kRows];     /* copies in a state-4 wait that found no row (noRow counted once per wait); 0 = free */
    unsigned int noRowNext;
};

inline void RowReset(Row* r, unsigned int uid, unsigned int now)
{
    Row z = Row();
    *r = z;
    r->uid = uid;
    r->lastUseMs = now;
}

/* uid's row: found, else a free one, else one unused for kRowIdleMs; 0 when none (uid 0 has none) */
inline Row* RowFor(Store* s, unsigned int uid, unsigned int now)
{
    if (uid == 0) return 0;
    Row* freeRow = 0;
    Row* idle = 0;
    for (int i = 0; i < kRows; ++i)
    {
        Row* r = &s->rows[i];
        if (r->uid == uid) return r;
        if (r->uid == 0) { if (freeRow == 0) freeRow = r; }
        else if (idle == 0 && AgeMs(now, r->lastUseMs) >= kRowIdleMs) idle = r;
    }
    Row* r = (freeRow != 0) ? freeRow : idle;
    if (r != 0) RowReset(r, uid, now);
    return r;
}

/* OWNER SIDE, any thread: one choice of our own character to send, numbered here. Returns its number, 0 = the queue is full
   (counted queueFull, not sent). */
inline unsigned int OwnerPush(Store* s, const Choice& c, double t, unsigned int now)
{
    if (s->qTail - s->qHead >= (unsigned int)kQueue) { ++s->c.queueFull; return 0; }
    if (++s->lastSeq == 0) s->lastSeq = 1;
    Sent& e = s->q[s->qTail % (unsigned int)kQueue];
    e.c = c;
    e.c.seq = s->lastSeq;
    e.t = t;
    e.decidedMs = now;
    ++s->qTail;
    return s->lastSeq;
}

/* OWNER SIDE, main thread: up to max waiting choices, oldest first. */
inline int OwnerTake(Store* s, Sent* out, int max)
{
    int n = 0;
    while (n < max && s->qHead != s->qTail)
    {
        out[n++] = s->q[s->qHead % (unsigned int)kQueue];
        ++s->qHead;
    }
    return n;
}

enum { kPutTaken = 0, kPutStale = 1, kPutNoRow = 2 };
/* COPY SIDE, main thread: an arrived choice for a copy here (the caller checked that). */
inline int Put(Store* s, const Choice& c, unsigned int now)
{
    ++s->c.recv;
    Row* r = RowFor(s, c.uid, now);
    if (r == 0) { ++s->c.noRow; return kPutNoRow; }
    ++r->leg.recv;
    r->lastUseMs = now;
    if (r->seqKnown && AgeMs(now, r->lastArriveMs) < kSeqResetMs && (int)(c.seq - r->lastSeq) <= 0)
    {
        ++s->c.staleSeq; ++r->leg.staleSeq;
        return kPutStale;
    }
    if (r->waiting) { ++s->c.superseded; ++r->leg.superseded; }
    r->waiting = 1;
    r->wBusy = 0;
    r->w = c;
    r->wArrivedMs = now;
    r->seqKnown = 1;
    r->lastSeq = c.seq;
    r->lastArriveMs = now;
    return kPutTaken;
}

enum { kGateApply = 0, kGateHold = 1, kGateRun = 2 };
/* COPY SIDE, the decide-step detour (AI worker): a copy of another player's character in combat mode, in state 4. kGateApply
   copies the waiting choice to *out and the ms it waited here to *waitedMs (it stays waiting until Settle). */
inline int Gate(Store* s, unsigned int uid, unsigned int now, Choice* out, unsigned int* waitedMs)
{
    Row* r = RowFor(s, uid, now);
    if (r == 0)
    {
        for (int i = 0; i < kRows; ++i) if (s->noRowUid[i] == uid) return kGateRun;   /* this wait is already counted */
        s->noRowUid[s->noRowNext++ % (unsigned int)kRows] = uid;
        ++s->c.noRow;
        return kGateRun;
    }
    r->lastUseMs = now;
    if (r->waiting && AgeMs(now, r->wArrivedMs) > kStaleMs)
    {
        r->waiting = 0;
        ++s->c.staleAge; ++r->leg.staleAge;
    }
    if (r->waiting)
    {
        *out = r->w;
        *waitedMs = AgeMs(now, r->wArrivedMs);
        return kGateApply;
    }
    if (r->quiet) return kGateRun;
    if (!r->holding)
    {
        if (!r->seqKnown || AgeMs(now, r->lastArriveMs) >= kTrafficMs)
        {
            r->quiet = 1;
            ++s->c.runNoTraffic; ++r->leg.runNoTraffic;
            return kGateRun;
        }
        r->holding = 1;
        r->holdStartMs = now;
        r->holdOver = 0;
        ++s->c.heldNothing; ++r->leg.heldNothing;
        return kGateHold;
    }
    if (AgeMs(now, r->holdStartMs) < kHoldMs) return kGateHold;
    if (!r->holdOver)
    {
        r->holdOver = 1;
        ++s->c.holdExpired; ++r->leg.holdExpired;
    }
    return kGateRun;
}

/* COPY SIDE, after the engine's own decide step ran without a choice (kGateRun): the copy's engine left state 4 - this wait is
   over, its next state 4 starts a new one. */
inline void AfterRun(Store* s, unsigned int uid, int engineState)
{
    if (engineState == 4 || uid == 0) return;
    for (int i = 0; i < kRows; ++i) if (s->noRowUid[i] == uid) s->noRowUid[i] = 0;
    for (int i = 0; i < kRows; ++i)
        if (s->rows[i].uid == uid) { s->rows[i].holding = 0; s->rows[i].holdOver = 0; s->rows[i].quiet = 0; return; }
}

/* COPY SIDE, after kGateApply and the engine's decide step: plan = PlanFor's answer as carried out (kPlanWrite only when the
   write took). busy leaves the choice waiting (counted once per choice); otherwise it is used (if no newer one replaced it meanwhile) and the wait ends.
   engineTarget = the uid of the copy's target after the call (0 none / unknown). Returns 1 when an applied line was kept. */
inline int Settle(Store* s, const Choice& c, int plan, unsigned int engineTarget, unsigned int waitedMs, double t)
{
    Row* r = 0;
    for (int i = 0; i < kRows && r == 0; ++i)
        if (s->rows[i].uid == c.uid && c.uid != 0) r = &s->rows[i];
    if (r == 0) return 0;
    if (plan == kPlanBusy)
    {
        if (r->waiting && r->w.seq == c.seq && !r->wBusy) { r->wBusy = 1; ++s->c.busy; ++r->leg.busy; }
        return 0;
    }
    if (r->waiting && r->w.seq == c.seq) r->waiting = 0;
    r->holding = 0;
    r->holdOver = 0;
    r->quiet = 0;
    if (plan != kPlanWrite) { ++s->c.kept; ++r->leg.kept; return 0; }
    ++s->c.applied; ++r->leg.applied;
    if (c.targetUid != engineTarget) { ++s->c.targetKept; ++r->leg.targetKept; }
    if (s->logN >= kLogFirst) return 0;
    Applied& a = s->log[s->logN++];
    a.uid = c.uid; a.seq = c.seq; a.next = c.next; a.ownerTarget = c.targetUid; a.engineTarget = engineTarget; a.waitedMs = waitedMs; a.t = t;
    return 1;
}

/* COPY SIDE, main thread: the owner's fight of uid ended - its leg's counts to *out and its row freed (a choice still waiting is
   not used in a later fight). Returns 1 when uid had a row. */
inline int LegEnd(Store* s, unsigned int uid, LegCounts* out)
{
    for (int i = 0; i < kRows; ++i)
    {
        Row* r = &s->rows[i];
        if (r->uid != uid || uid == 0) continue;
        *out = r->leg;
        Row z = Row();
        *r = z;
        return 1;
    }
    return 0;
}

/* main thread: the world is torn down or the link went down - every copy's row is freed (a choice waiting or a hold in progress
   belongs to a copy that is gone or has no feed; a copy still fighting takes a fresh row at its next arrival). The counters,
   the owner queue and the applied lines stay. Returns the rows that were in use. */
inline int ClearRows(Store* s)
{
    int n = 0;
    for (int i = 0; i < kRows; ++i)
    {
        if (s->rows[i].uid != 0) ++n;
        Row z = Row();
        s->rows[i] = z;
        s->noRowUid[i] = 0;
    }
    s->noRowNext = 0;
    return n;
}

/* main thread: the applied lines from index `from` (at most max), in order. */
inline int TakeLog(const Store* s, int from, Applied* out, int max)
{
    int n = 0;
    for (int i = from; i < s->logN && n < max; ++i) out[n++] = s->log[i];
    return n;
}

}   /* namespace fightchoice */

#endif
