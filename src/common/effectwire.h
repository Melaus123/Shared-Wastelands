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
 *   ANSWER  (any kind) : result u8 (1 APPLIED, 2 DONE, 3 REFUSED, 4 ALREADY) | reason u8 (kReason*)                 16 bytes
 *   Kinds 2..6 (HIT, KO, NOTICE, BED, ORDER) are reserved by the design and refused by this decoder until they are built.
 *
 * EAT: Character::gettingEaten (row Character_beingEaten; bool (victim, float rate, eater))
 * tail-jumps to MedicalSystem::gettingEaten(rate, eaterRaceFlag), which spends rate x the frame time of flesh. The eater's game
 * adds rate x its frame time into a BUDGET per (target, eater) pair and sends it 4 times a second; the owner spends exactly
 * that budget through its own call, one frame step at a time, so the eating pace follows the eater's game.
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes and decisions. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <vector>
#include "liveenvelope.h"   /* cooplive::IsRelayPeer / IsSessionPeerKey - the routing choice */

namespace cooffect {

const unsigned char kDirRequest = 1, kDirAnswer = 2;
const unsigned char kKindEat = 1;                    /* 2 HIT, 3 KO, 4 NOTICE, 5 BED, 6 ORDER: reserved */
const unsigned char kResultApplied = 1, kResultDone = 2, kResultRefused = 3, kResultAlready = 4;
/* Refusal reasons (the design's table). NOT_SENDERS is counted and NOT answered (a stale or forged request). */
enum { kReasonNone = 0, kReasonNotMine = 1, kReasonGone = 2, kReasonNoActor = 3, kReasonNotSenders = 4, kReasonState = 5,
       kReasonNoBed = 6, kReasonBusy = 7, kReasonFar = 8, kReasonCount = 9 };
inline const char* ReasonName(int r)
{
    static const char* const k[kReasonCount] = { "none", "notMine", "gone", "noActor", "notSenders", "state", "noBed", "busy", "far" };
    return (r >= 0 && r < kReasonCount) ? k[r] : "?";
}

const size_t kHeaderSize = 14, kEatRequestSize = 23, kAnswerSize = 16;
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
    unsigned char result, reason;     /* ANSWER */
    EffectMsg() : dir(0), kind(0), reqId(0), targetUid(0), actorUid(0), rate(0.0f), budget(0.0f), flags(0), result(0), reason(0) {}
};

inline bool FloatIn(float v, float lo, float hi) { return v == v && v >= lo && v <= hi; }   /* v == v is false for NaN */

inline bool EffectMsgOk(const EffectMsg& m)
{
    if (m.reqId == 0 || m.targetUid == 0 || m.actorUid == 0 || m.targetUid == m.actorUid || m.kind != kKindEat) return false;
    if (m.dir == kDirRequest)
        return FloatIn(m.rate, 0.0f, kEatRateMax) && m.rate > 0.0f && FloatIn(m.budget, 0.0f, kEatBudgetMax)
            && (m.flags & (unsigned char)~kEatFlagTargetDead) == 0;
    if (m.dir == kDirAnswer)
        return m.result >= kResultApplied && m.result <= kResultAlready && m.reason < kReasonCount
            && ((m.result == kResultRefused) == (m.reason != kReasonNone));
    return false;
}

/* false (nothing appended) for a message that would not decode. */
inline bool EncodeEffect(std::vector<char>* b, const EffectMsg& m)
{
    if (b == 0 || !EffectMsgOk(m)) return false;
    const size_t n = (m.dir == kDirRequest) ? kEatRequestSize : kAnswerSize;
    const size_t at = b->size();
    b->resize(at + n);
    char* p = &(*b)[at];
    p[0] = (char)m.dir; p[1] = (char)m.kind;
    std::memcpy(p + 2, &m.reqId, 4);
    std::memcpy(p + 6, &m.targetUid, 4);
    std::memcpy(p + 10, &m.actorUid, 4);
    if (m.dir == kDirRequest) { std::memcpy(p + 14, &m.rate, 4); std::memcpy(p + 18, &m.budget, 4); p[22] = (char)m.flags; }
    else { p[14] = (char)m.result; p[15] = (char)m.reason; }
    return true;
}

/* Nothing is written on a refusal. A longer payload is read up to its kind's size. */
inline int DecodeEffect(const char* p, size_t size, EffectMsg* out)
{
    if (p == 0 || out == 0 || size < kHeaderSize) return kDecodeTooShort;
    EffectMsg m;
    m.dir = (unsigned char)p[0]; m.kind = (unsigned char)p[1];
    if (m.dir != kDirRequest && m.dir != kDirAnswer) return kDecodeBadDir;
    if (m.kind != kKindEat) return kDecodeBadKind;
    std::memcpy(&m.reqId, p + 2, 4);
    std::memcpy(&m.targetUid, p + 6, 4);
    std::memcpy(&m.actorUid, p + 10, 4);
    if (size < (m.dir == kDirRequest ? kEatRequestSize : kAnswerSize)) return kDecodeTooShort;
    if (m.reqId == 0 || m.targetUid == 0 || m.actorUid == 0 || m.targetUid == m.actorUid) return kDecodeBadUid;
    if (m.dir == kDirRequest) { std::memcpy(&m.rate, p + 14, 4); std::memcpy(&m.budget, p + 18, 4); m.flags = (unsigned char)p[22]; }
    else { m.result = (unsigned char)p[14]; m.reason = (unsigned char)p[15]; }
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
    if (!budgetPending && now - lastBite > kEatIdleMs && unansweredSince == 0) return kActorClose;   /* the eating stopped by itself */
    return kActorWait;
}
/* An answer for a live pair: 0 keep eating (APPLIED / ALREADY clears the silence clock), 1 stop. */
inline int EatActorOnAnswer(unsigned char result) { return (result == kResultDone || result == kResultRefused) ? 1 : 0; }

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
