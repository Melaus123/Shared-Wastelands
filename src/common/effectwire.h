/* src/common/effectwire.h - T-327 first slice (session protocol 111; .modding/investigations/effect-on-copy-design-2026-09-30.md).
 *
 * ONE ROAD: "an effect on another game's character is a request to its owner". The game that drives the ACTOR (here: the eater)
 * catches the engine call that would change the TARGET's copy, does not let it land on the copy, and sends MSG_EFFECT (65,
 * RELIABLE) to the game that drives the target. The owner checks the request and runs the SAME engine function on its own
 * character with its copy of the actor as the actor argument. Its result comes back on the roads that already exist (STATE,
 * DESPAWN); only APPLIED / DONE / REFUSED come back on this message type.
 *
 *   dir u8 (1 REQUEST, 2 ANSWER) | kind u8 | reqId u32 | targetUid u32 | actorUid u32 | the fields below
 *   REQUEST kind 1 EAT : rate f32 | budget f32 | flags u8 (bit 0: the target is dead on the actor's game)          23 bytes
 *   REQUEST kind 3 KO  : source u8 (1 StealthKO) | decided u8 (0 a player character, 4 an NPC: the owner decides either way)
 *                        | skill f32 (the assassin's knockout skill on its own game, -1 unread; logged beside the copy's) 20 bytes
 *   ANSWER  kind 1 EAT : result u8 (1 APPLIED, 2 DONE, 3 REFUSED, 4 ALREADY) | reason u8 (kReason*)                 16 bytes
 *   ANSWER  kind 3 KO  : result u8 | reason u8 | outcome u8 (1 KNOCKED_OUT, 2 NOTICED; nonzero exactly with APPLIED)  17 bytes
 *                        (REFUSED STATE = the victim sees the assassin; REFUSED FAR = too far apart there)
 *   Kinds 2, 4..6 (HIT, NOTICE, BED, ORDER) are reserved by the design and refused by this decoder until they are built; a
 *   decoder that takes kind 1 only refuses kind 3 as a bad kind.
 *
 * EAT: Character::gettingEaten (row Character_beingEaten; bool (victim, float rate, eater))
 * tail-jumps to MedicalSystem::gettingEaten(rate, eaterRaceFlag), which spends rate x the frame time of flesh. The eater's game
 * adds rate x its frame time into a BUDGET per (target, eater) pair and sends it 4 times a second; the owner spends exactly
 * that budget through its own call, one frame step at a time, so the eating pace follows the eater's game.
 *
 * KO (Task_StealthKO, the sneak knockout): the assassin's game holds its task in the knockout animation while the victim is
 * another game's character and sends ONE request; the VICTIM's game decides at once on its own character: refused when its
 * victim sees its copy of the assassin (the engine ends Task_StealthKO without a roll then) or stands more than kFarUnits from
 * it, ALREADY when the victim is down, else it rolls the engine's own chance with its copy of the assassin (the copy carries the
 * owner's stats), applies the outcome through the engine's own calls, answers it and pushes STATE. The assassin's game feeds
 * the answered outcome to the engine's own roll, so its text, experience and failure reaction match; a refusal ends the task
 * through its own exit. decided 0 (another player's character) and 4 (an NPC) take the same steps; the value says which the
 * assassin's game saw.
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes and decisions. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <cfloat>    /* FLT_MAX - the KO's forced chance */
#include <vector>
#include "liveenvelope.h"   /* cooplive::IsRelayPeer / IsSessionPeerKey - the routing choice */

namespace cooffect {

const unsigned char kDirRequest = 1, kDirAnswer = 2;
const unsigned char kKindEat = 1, kKindKo = 3;       /* 2 HIT, 4 NOTICE, 5 BED, 6 ORDER: reserved */
const unsigned char kResultApplied = 1, kResultDone = 2, kResultRefused = 3, kResultAlready = 4;
/* Refusal reasons (the design's table). NOT_SENDERS is counted and NOT answered (a stale or forged request). */
enum { kReasonNone = 0, kReasonNotMine = 1, kReasonGone = 2, kReasonNoActor = 3, kReasonNotSenders = 4, kReasonState = 5,
       kReasonNoBed = 6, kReasonBusy = 7, kReasonFar = 8, kReasonCount = 9 };
inline const char* ReasonName(int r)
{
    static const char* const k[kReasonCount] = { "none", "notMine", "gone", "noActor", "notSenders", "state", "noBed", "busy", "far" };
    return (r >= 0 && r < kReasonCount) ? k[r] : "?";
}

const size_t kHeaderSize = 14, kEatRequestSize = 23, kAnswerSize = 16, kKoRequestSize = 20, kKoAnswerSize = 17;
const unsigned char kKoSourceStealth = 1;                                                  /* REQUEST KO: Task_StealthKO */
const unsigned char kKoDecidedOwner = 0;   /* REQUEST KO: the assassin's game sees another player's character */
const unsigned char kKoDecidedNpc = 4;     /* REQUEST KO: the assassin's game sees an NPC (1..3 are refused) */
const float kKoSkillUnread = -1.0f;        /* REQUEST KO: the assassin's skill could not be read on its game */
const float kKoSkillMax = 1000.0f;         /* REQUEST KO: a skill above this is garbage */
const unsigned char kKoOutcomeNone = 0, kKoOutcomeKnockedOut = 1, kKoOutcomeNoticed = 2;   /* ANSWER KO: the outcome applied */
const unsigned char kEatFlagTargetDead = 1;
const float kEatRateMax   = 1000.0f;   /* a rate above this is garbage (the lever uses 1.0; the task's own rate is Read-unknown) */
const float kEatBudgetMax = 1000.0f;   /* rate x seconds in one request: 250 ms of the largest rate */

const int kDecodeOk = 0, kDecodeTooShort = 1, kDecodeBadDir = 2, kDecodeBadKind = 3, kDecodeBadUid = 4, kDecodeBadField = 5;

struct EffectMsg
{
    unsigned char dir, kind;
    unsigned int  reqId, targetUid, actorUid;
    float         rate, budget;       /* REQUEST EAT */
    unsigned char flags;              /* REQUEST EAT */
    unsigned char source, decided;    /* REQUEST KO */
    float         skill;              /* REQUEST KO: the assassin's knockout skill on its own game (kKoSkillUnread) */
    unsigned char result, reason;     /* ANSWER */
    unsigned char outcome;            /* ANSWER KO */
    EffectMsg() : dir(0), kind(0), reqId(0), targetUid(0), actorUid(0), rate(0.0f), budget(0.0f), flags(0), source(0), decided(0), skill(0.0f),
                  result(0), reason(0), outcome(0) {}
};

inline bool FloatIn(float v, float lo, float hi) { return v == v && v >= lo && v <= hi; }   /* v == v is false for NaN */

inline bool EffectMsgOk(const EffectMsg& m)
{
    if (m.reqId == 0 || m.targetUid == 0 || m.actorUid == 0 || m.targetUid == m.actorUid || (m.kind != kKindEat && m.kind != kKindKo))
        return false;
    if (m.dir == kDirRequest && m.kind == kKindKo)
        return m.source == kKoSourceStealth && (m.decided == kKoDecidedOwner || m.decided == kKoDecidedNpc)
            && FloatIn(m.skill, kKoSkillUnread, kKoSkillMax);
    if (m.dir == kDirRequest)
        return FloatIn(m.rate, 0.0f, kEatRateMax) && m.rate > 0.0f && FloatIn(m.budget, 0.0f, kEatBudgetMax)
            && (m.flags & (unsigned char)~kEatFlagTargetDead) == 0;
    if (m.dir == kDirAnswer)
    {
        if (!(m.result >= kResultApplied && m.result <= kResultAlready && m.reason < kReasonCount
              && ((m.result == kResultRefused) == (m.reason != kReasonNone))))
            return false;
        if (m.kind == kKindKo)   /* an outcome is carried exactly when the owner APPLIED one */
            return m.outcome <= kKoOutcomeNoticed && ((m.outcome != kKoOutcomeNone) == (m.result == kResultApplied));
        return true;
    }
    return false;
}

inline size_t EffectMsgSize(unsigned char dir, unsigned char kind)
{
    if (kind == kKindKo) return (dir == kDirRequest) ? kKoRequestSize : kKoAnswerSize;
    return (dir == kDirRequest) ? kEatRequestSize : kAnswerSize;
}

/* false (nothing appended) for a message that would not decode. */
inline bool EncodeEffect(std::vector<char>* b, const EffectMsg& m)
{
    if (b == 0 || !EffectMsgOk(m)) return false;
    const size_t n = EffectMsgSize(m.dir, m.kind);
    const size_t at = b->size();
    b->resize(at + n);
    char* p = &(*b)[at];
    p[0] = (char)m.dir; p[1] = (char)m.kind;
    std::memcpy(p + 2, &m.reqId, 4);
    std::memcpy(p + 6, &m.targetUid, 4);
    std::memcpy(p + 10, &m.actorUid, 4);
    if (m.dir == kDirRequest && m.kind == kKindKo) { p[14] = (char)m.source; p[15] = (char)m.decided; std::memcpy(p + 16, &m.skill, 4); }
    else if (m.dir == kDirRequest) { std::memcpy(p + 14, &m.rate, 4); std::memcpy(p + 18, &m.budget, 4); p[22] = (char)m.flags; }
    else { p[14] = (char)m.result; p[15] = (char)m.reason; if (m.kind == kKindKo) p[16] = (char)m.outcome; }
    return true;
}

/* Nothing is written on a refusal. A longer payload is read up to its kind's size. */
inline int DecodeEffect(const char* p, size_t size, EffectMsg* out)
{
    if (p == 0 || out == 0 || size < kHeaderSize) return kDecodeTooShort;
    EffectMsg m;
    m.dir = (unsigned char)p[0]; m.kind = (unsigned char)p[1];
    if (m.dir != kDirRequest && m.dir != kDirAnswer) return kDecodeBadDir;
    if (m.kind != kKindEat && m.kind != kKindKo) return kDecodeBadKind;
    std::memcpy(&m.reqId, p + 2, 4);
    std::memcpy(&m.targetUid, p + 6, 4);
    std::memcpy(&m.actorUid, p + 10, 4);
    if (size < EffectMsgSize(m.dir, m.kind)) return kDecodeTooShort;
    if (m.reqId == 0 || m.targetUid == 0 || m.actorUid == 0 || m.targetUid == m.actorUid) return kDecodeBadUid;
    if (m.dir == kDirRequest && m.kind == kKindKo) { m.source = (unsigned char)p[14]; m.decided = (unsigned char)p[15]; std::memcpy(&m.skill, p + 16, 4); }
    else if (m.dir == kDirRequest) { std::memcpy(&m.rate, p + 14, 4); std::memcpy(&m.budget, p + 18, 4); m.flags = (unsigned char)p[22]; }
    else { m.result = (unsigned char)p[14]; m.reason = (unsigned char)p[15]; if (m.kind == kKindKo) m.outcome = (unsigned char)p[16]; }
    if (!EffectMsgOk(m)) return kDecodeBadField;
    *out = m;
    return kDecodeOk;
}

/* ---- THE DETOUR'S CASES (any thread). own: 1 driven here, 0 a copy, -1 not replicated (no uid). ---- */
enum { kCaseOriginal = 0, kCaseRequest = 1, kCaseSuppress = 2, kCaseNoActor = 3 };
inline int EatDetourCase(int targetOwn, int actorOwn)
{
    if (targetOwn != 0) return kCaseOriginal;   /* the target is driven here (or no game's character): the engine's own call */
    if (actorOwn == 1) return kCaseRequest;     /* a copy eaten by our eater: ask its owner */
    if (actorOwn == 0) return kCaseSuppress;    /* neither driven here (an onlooker): nothing, nothing sent (F349's puppet rule) */
    return kCaseNoActor;                        /* an eater no game can name: the owner could not use it - the eater stops */
}

/* ---- THE OWNER'S CHECK (main thread). distSq < 0 = a position could not be read (refused FAR: it cannot be checked). ---- */
const float kFarUnits = 150.0f;   /* 15 m at 10 world units a metre (the project's Inferred scale); a Guess to tune in the run */
/* T-327 fold 2: no standing-victim refusal - the owner applies every bite through the engine's own gettingEaten and the eater's
   task decides when eating stops (as in single player). STATE is left for the owner-side eating OFF after a caught fault. */
inline int EffectValidate(bool targetMine, bool senderDrivesActor, bool targetLoaded, bool actorLoaded, bool blocked, float distSq)
{
    if (!targetMine) return kReasonNotMine;
    if (!senderDrivesActor) return kReasonNotSenders;
    if (!targetLoaded) return kReasonGone;
    if (!actorLoaded) return kReasonNoActor;
    if (blocked) return kReasonBusy;
    if (!(distSq >= 0.0f) || distSq > kFarUnits * kFarUnits) return kReasonFar;
    return kReasonNone;
}
inline bool ReasonAnswered(int reason) { return reason != kReasonNotSenders; }
/* Review fold L1: a refusal ends the owner's existing pair for the (target, eater) - except NOT_SENDERS, which is a stale or
   forged request from a game that does not drive that eater and is not answered: the real sender's pair is left alone. */
inline bool RefusalEndsOwnerPair(int reason) { return reason != kReasonNone && reason != kReasonNotSenders; }

/* ---- ACCUMULATE / FLUSH. The detour adds rate x dt in micro-units (an interlocked add); the owner spends it frame by frame. ---- */
const long long kMicro = 1000000;
inline long long EatBiteMicro(float rate, float dt)
{
    if (!FloatIn(rate, 0.0f, kEatRateMax) || !FloatIn(dt, 0.0f, 1.0f)) return 0;   /* a frame longer than 1 s is not a bite */
    return (long long)((double)rate * (double)dt * (double)kMicro + 0.5);
}
inline float EatMicroToBudget(long long micro)
{
    if (micro <= 0) return 0.0f;
    const double b = (double)micro / (double)kMicro;
    return (float)(b > (double)kEatBudgetMax ? (double)kEatBudgetMax : b);
}
/* One owner frame: how much of `budget` to spend and the rate that spends exactly that in `dt` (the engine multiplies by dt).
   Returns the spend; 0 (and *rateOut 0) = nothing to do this frame. */
inline float EatSpendStep(float budget, float rate, float dt, float* rateOut)
{
    if (rateOut != 0) *rateOut = 0.0f;
    if (!(budget > 0.0f) || !(rate > 0.0f) || !(dt > 0.0f) || !(dt <= 1.0f)) return 0.0f;
    float spend = rate * dt;
    if (spend > budget) spend = budget;
    if (rateOut != 0) *rateOut = spend / dt;
    return spend;
}

/* Milliseconds from `then` to `now` on GetTickCount's wrapping clock; 0 when `then` is ahead of `now` (a stamp another thread wrote
   after `now` was read). */
inline unsigned int EffSinceMs(unsigned int now, unsigned int then)
{
    const int d = (int)(now - then);
    return d > 0 ? (unsigned int)d : 0u;
}

/* ---- THE EATER'S STOP RULES (main thread, every frame per pair). Manager decision 3 (2026-09-30): after a refusal or 3 s with
   no answer, the eater STOPS - the detour then answers 1 for the pair, which ends Task_EatPrisoner. The owner answers APPLIED to
   every request it accepts, so `unansweredSince` (the first send after the last answer, 0 = nothing outstanding) is the clock.
   Review fold M1: a STOPPED pair is held (the eater's call answers 1) for 10 s from the STOP (`stoppedAt`), bites or not, then
   freed - the bites a stopped pair still receives never extend the hold. */
const unsigned int kEatSendMs = 250, kEatSilenceMs = 3000, kEatIdleMs = 1000, kEatHoldMs = 10000;
enum { kActorWait = 0, kActorSend = 1, kActorStopTimeout = 2, kActorClose = 3, kActorHold = 4, kActorFree = 5 };
inline int EatActorDecide(bool stopped, unsigned int now, unsigned int lastBite, unsigned int lastSend, unsigned int unansweredSince,
                          bool budgetPending, unsigned int stoppedAt)
{
    if (stopped) return (now - stoppedAt > kEatHoldMs) ? kActorFree : kActorHold;
    if (unansweredSince != 0 && now - unansweredSince > kEatSilenceMs) return kActorStopTimeout;
    if (budgetPending && (lastSend == 0 || now - lastSend >= kEatSendMs)) return kActorSend;
    /* the eating stopped by itself; lastBite is the detour's stamp, which may be newer than `now` */
    if (!budgetPending && EffSinceMs(now, lastBite) > kEatIdleMs && unansweredSince == 0) return kActorClose;
    return kActorWait;
}
/* An answer for a live pair: 0 keep eating (APPLIED / ALREADY clears the silence clock), 1 stop. */
inline int EatActorOnAnswer(unsigned char result) { return (result == kResultDone || result == kResultRefused) ? 1 : 0; }

/* ---- THE SNEAK KNOCKOUT (KO). The engine's roll (Task_StealthKO stage 2): success when chance > roll, the roll uniform in
   0..100. ---- */
inline bool KoRollSucceeds(float chance, float roll) { return chance > roll; }
/* The chance the engine's own branch is given so that it lands on `outcome` whatever it rolls: above every roll, or below it. */
inline float KoForcedChance(unsigned char outcome) { return (outcome == kKoOutcomeKnockedOut) ? FLT_MAX : -FLT_MAX; }

/* The assassin's game's slot per (victim copy, assassin). FREE and CLAIMING are free or being taken; the hook (any thread) claims
   NEW, moves FORCED -> FORCED_DONE and END -> ENDED; the main thread does every other move. */
enum { kKoFree = 0, kKoClaiming = 1, kKoNew = 2, kKoHeld = 3, kKoForced = 4, kKoEnd = 5, kKoEnded = 6, kKoForcedDone = 7 };
enum { kKoActHold = 0, kKoActClassify = 1, kKoActSend = 2, kKoActRunForced = 3, kKoActRunEngine = 4, kKoActEndTask = 5 };
enum { kKoAnsNone = 0, kKoAnsVerdict = 1, kKoAnsRefused = 2, kKoAnsAlready = 3 };
/* The answer a slot records: APPLIED with an outcome = the verdict; ALREADY; anything else ends the task as a refusal. */
inline int KoAnswerKind(unsigned char result, unsigned char outcome)
{
    if (result == kResultApplied && outcome != kKoOutcomeNone) return kKoAnsVerdict;
    if (result == kResultAlready) return kKoAnsAlready;
    return kKoAnsRefused;
}
const int kKoPcUnknown = -3;              /* not classified yet (CopyIsPlayerCharacter: 1 player character, 0 NPC, -1 unreadable, -2 no copy) */
const unsigned int kKoAnswerMs = 3000;    /* a request's answer is due within this; the assassin's game ends its task after it */
const unsigned int kKoOwnerCutMs = 1500;  /* the owner applies a request only this soon after it ARRIVED (the owner's clock); a later
                                             one it refuses BUSY, never applied. The assassin's kKoAnswerMs counts from its SEND. */
const unsigned int kKoRoundTripMs = 1500; /* the round trip (request out, answer back) the cut leaves room for: kKoOwnerCutMs +
                                             kKoRoundTripMs <= kKoAnswerMs, so an applied verdict reaches the assassin's game before
                                             it gives up whenever the round trip is under 1.5 s */
/* MAIN THREAD (the owner): a request that arrived sinceArrivalMs ago is past the cut - refused BUSY, never applied. */
inline bool KoOwnerPastCut(unsigned int sinceArrivalMs) { return sinceArrivalMs > kKoOwnerCutMs; }
const unsigned int kKoGoneMs = 2000;      /* the task's update has not visited its slot this long: the task is no longer running */

/* MAIN THREAD, each frame per slot. victimMineNow: the victim is driven here now (a hand-over). ans: the owner's answer (kKoAns*).
   waitedMs: since the request was sent (0 before). A player character and an NPC take the same steps: one request, one answer. */
inline int KoActorDecide(int state, int pc, bool victimMineNow, bool copyHere, int ans, unsigned int waitedMs)
{
    if (state == kKoNew)
    {
        if (victimMineNow) return kKoActRunEngine;
        if (!copyHere) return kKoActEndTask;
        if (pc == kKoPcUnknown) return kKoActClassify;
        if (pc == 1 || pc == 0) return kKoActSend;   /* a player character or an NPC: the victim's own game decides */
        return kKoActEndTask;                        /* unreadable, or no copy */
    }
    if (state == kKoHeld)
    {
        if (ans == kKoAnsVerdict) return kKoActRunForced;
        /* handed to this game while the answer was awaited: the request is out (its game may have rolled already) and an answer
           from a game that no longer runs the victim is not taken (KoOnAnswer) - the task ends, never a second roll here */
        if (victimMineNow) return kKoActEndTask;
        if (ans == kKoAnsRefused || ans == kKoAnsAlready || !copyHere || waitedMs > kKoAnswerMs) return kKoActEndTask;
        return kKoActHold;
    }
    return kKoActHold;
}
/* MAIN THREAD: the `decided` a slot's request carries - how this game classified the victim. Every game on a link speaks the same
   protocol (HELLO / WELCOME refuse another number), so an owner that would not know decided 4 is never on the other end. */
inline unsigned char KoRequestDecided(int pc) { return (pc == 0) ? kKoDecidedNpc : kKoDecidedOwner; }
/* ANY THREAD (the task's update in stage 1, victim a copy, assassin driven here): what the update does for its slot's state. */
inline int KoHookAct(int state)
{
    if (state == kKoForced) return kKoActRunForced;        /* the engine's own update runs; the chance hook returns the verdict */
    if (state == kKoEnd) return kKoActEndTask;             /* the task's own exit */
    return kKoActHold;                                     /* the task waits in stage 1: its update is skipped (the animation is not re-requested) */
}
/* MAIN THREAD: a slot to free - ENDED at once; FORCED, FORCED_DONE and END once the task's update stopped visiting it. */
inline bool KoSlotDone(int state, unsigned int sinceSeenMs)
{
    if (state == kKoEnded) return true;
    if (state == kKoForced || state == kKoForcedDone || state == kKoEnd) return sinceSeenMs > kKoGoneMs;
    return false;
}

/* THE VICTIM'S GAME DECIDES (MAIN THREAD, the K2 safe point). KoOwnerCheck returns a refusal reason (1..8), kKoOwnAlready or
   kKoOwnRoll: the request's own check first (validateReason: EffectValidate, FAR beyond kFarUnits between the victim and this
   game's copy of the assassin), then a victim already down, then the victim seeing the assassin (its senses record of the
   assassin) - the engine's own quiet end of the task, no roll. KoDecide adds the engine's chance against its roll. */
enum { kKoOwnAlready = 100, kKoOwnRoll = 101, kKoOwnKnockedOut = 102, kKoOwnFailed = 103 };
inline int KoOwnerCheck(bool down, bool sees, int validateReason)
{
    if (validateReason != kReasonNone) return validateReason;
    if (down) return kKoOwnAlready;
    if (sees) return (int)kReasonState;
    return kKoOwnRoll;
}
inline int KoDecide(int check, float chance, float roll)
{
    if (check != kKoOwnRoll) return check;
    return KoRollSucceeds(chance, roll) ? (int)kKoOwnKnockedOut : (int)kKoOwnFailed;
}
/* The outcome a decision applies and answers: KNOCKED_OUT, NOTICED (the engine's failure), or none (a refusal or ALREADY). */
inline unsigned char KoDecidedOutcome(int decision)
{
    if (decision == kKoOwnKnockedOut) return kKoOutcomeKnockedOut;
    if (decision == kKoOwnFailed) return kKoOutcomeNoticed;
    return kKoOutcomeNone;
}
/* KoClassDiffer names a difference in how the two games classify the victim (ownerPc = CopyIsPlayerCharacter on the owner's own
   character: 1 a player character, 0 an NPC, -1 unreadable); the owner decides the same way either way and says it:
   kKoClassOwnerSaysPc  - the assassin's game called it an NPC; the owner's game has a player character;
   kKoClassOwnerSaysNpc - the assassin's game called it a player character; the owner's game has an NPC. */
enum { kKoClassSame = 0, kKoClassOwnerSaysPc = 1, kKoClassOwnerSaysNpc = 2 };
inline int KoClassDiffer(unsigned char decided, int ownerPc)
{
    if (decided == kKoDecidedNpc && ownerPc == 1) return kKoClassOwnerSaysPc;
    if (decided == kKoDecidedOwner && ownerPc == 0) return kKoClassOwnerSaysNpc;
    return kKoClassSame;
}

/* ---- ROUTING (T-327 manager decision 5): by the target's OWNER, through the notebook (LIVE route SLOT) when that road is up,
   else the session link; exactly one road, never both (the M7a rule). The session peer rides the notebook only once it is proven
   reachable there (CharStreamRoad's fold F1 rule). ---- */
enum { kRoadNone = 0, kRoadSession = 1, kRoadLive = 2 };
inline int EffectRequestRoad(bool liveReady, bool sessionUp, unsigned int ownerKey, int linkPeerSlot, bool sessionPeerRelayOk)
{
    const bool relay = cooplive::IsRelayPeer(ownerKey);
    const bool sess = cooplive::IsSessionPeerKey(ownerKey, linkPeerSlot);
    if (liveReady && relay && (!sess || sessionPeerRelayOk || !sessionUp)) return kRoadLive;
    if (sessionUp && sess) return kRoadSession;
    return kRoadNone;
}
/* The answer goes back on the road the request came by (RELSYNC's rule), and only there. */
inline int EffectAnswerRoad(bool viaRelay, bool liveReady, bool sessionUp)
{
    if (viaRelay) return liveReady ? kRoadLive : kRoadNone;
    return sessionUp ? kRoadSession : kRoadNone;
}

} // namespace cooffect
