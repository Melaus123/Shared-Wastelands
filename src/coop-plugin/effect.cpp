// effect.cpp - T-327 FIRST SLICE (2026-09-30; .modding/investigations/effect-on-copy-design-2026-09-30.md sections 2-7):
// EATING ANOTHER GAME'S CHARACTER IS A REQUEST TO ITS OWNER.
//
// The engine road (Read, 1.0.65; build/decomp_435350.txt, decomp_353bf0.txt, decomp_64f8e0.txt and an offline xref scan of
// kenshi_x64.exe on 2026-09-30):
//   Character::gettingEaten (row Character_beingEaten;
//   bool (Character* victim, float rate, Character* eater)) sets victim+0x279 |= 7, runs its first-bite block (rememberCharacter
//   and the alert) while the victim is not down, and TAIL-JUMPS to MedicalSystem::gettingEaten(victim+0x458, rate, the eater's
//   race flag +0x7d), whose bool it returns: 1 = eaten up (alive: the flesh is gone, limbs severed, "{1} has been eaten alive.",
//   GameWorld::destroy "eaten"; dead: the corpse's dead branch ends in the same destroy).
//   Its ONLY references are its jump stub and three vtable slots (+0x380 of three Character vtables): no direct call anywhere, and
//   MedicalSystem::gettingEaten's ONLY caller is this function. So EVERY eat - Task_EatPrisoner (whose +0x380 call ends the
//   task when it answers 1) and any corpse eating - passes this one entry: T-307's corpse case is covered by the same hook.
//
// The three cases of the detour (effectwire.h EatDetourCase):
//   the victim is driven here, or no game's character  -> the engine's own call, unchanged (our own animal eating our own);
//   the victim is a COPY and the eater is driven here  -> HOLD: the copy is untouched; the bite (rate x frame time) is added to
//                                                          the pair's budget; the main thread sends it to the victim's owner 4x/s;
//   the victim is a COPY and the eater a copy too      -> suppress, send nothing (an onlooker; F349's puppet rule);
//   the victim is a COPY and the eater has no uid      -> answer 1 (stop): the owner could not name the eater.
// The detour answers 0 ("still eating") while the pair is live, and 1 once the pair STOPPED (manager decision 3, 2026-09-30):
// the owner answered DONE or REFUSED, 3 s passed with no answer, there is no road, or the copy is gone.
//
// The owner (MSG_EFFECT REQUEST, main thread): target mine, the sender drives the eater, both loaded, not loading or saving,
// the eater's copy within 15 m. Accepted: the budget is added to its own pair and APPLIED is answered (every accepted request,
// so the eater's silence clock means "the owner stopped answering"). At the K2 safe point (worker paused) each frame it spends
// up to rate x frame time of the budget through the ORIGINAL gettingEaten on its own character with its copy of the eater; a 1
// is answered DONE. The engine's own end (the destroy "eaten") runs there, and its existing DESPAWN clears the eater's copy.
//
// Threads: the detour runs on whatever thread the task update runs (the AI worker is assumed - F124 rules: no allocation, lock
// or log; interlocked fields of a fixed 32-slot table). Everything else is the MAIN THREAD. The [EFFECT] REPORT prints the
// detour's thread id beside the main thread's.
//
// A SNEAK KNOCKOUT (MSG_EFFECT kind 3 KO; the block 'A SNEAK KNOCKOUT' below) takes the same road for Task_StealthKO: the assassin's
// game holds its task and sends one request; the victim's owner decides, applies the outcome on its own character and answers it.

#include "effect.h"
#include "spawn.h"
#include "store.h"         // EngineWritesBlocked, StoreIsDeadPod, StoreWorldGenNow
#include "replicate.h"     // IsDownedCharacter: a sneak knockout of a victim already down is answered ALREADY
#include "medical.h"       // StatePush, StealthKnockOutOwned: the owner's side of a sneak knockout
#include "addresses.h"
#include "hooks.h"
#include "net/session.h"
#include "coop_log.h"
#include "../common/effectwire.h"
#include <vector>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <intrin.h>        // _ReturnAddress: the sneak knockout's chance hook acts only for the task's own roll

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace coop {
namespace {

unsigned long long kEffEatRva = 0; static AddrReg kEffEatRva_reg("Character_beingEaten", &kEffEatRva);   /* Steam_1.0.65 0x435350 */
unsigned long long kEffDtRva = 0;  static AddrReg kEffDtRva_reg("GameFrameDt", &kEffDtRva);               /* Steam_1.0.65 0x2132734, a float var (farm.cpp's row) */

typedef bool (*EatFn)(::Character*, float, ::Character*);
EatFn orig_eat = 0;
EatFn g_eatEntry = 0;     /* the hooked entry itself: the eatbite lever calls THROUGH the detour */

/* ANY THREAD (interlocked) */
volatile LONG64 g_eatCalls = 0, g_eatOwnBites = 0, g_eatCopyBites = 0, g_eatSuppressed = 0, g_eatNoActorUid = 0, g_eatTableFull = 0;
volatile LONG64 g_eatStopReturns = 0;
volatile LONG   g_eatThread = 0;

/* MAIN THREAD */
DWORD g_mainThread = 0;
long long g_eqSent = 0, g_eqIn = 0, g_eqApplied = 0, g_eqRefused = 0, g_eqDone = 0, g_eqTimeout = 0, g_eqStopped = 0;
long long g_eqRefusedWhy[cooffect::kReasonCount] = { 0 };
enum { kStopDone = 0, kStopRefused = 1, kStopTimeout = 2, kStopNoRoute = 3, kStopHandedHere = 4, kStopCopyGone = 5, kStopBad = 6, kStopCount = 7 };
long long g_eqStopWhy[kStopCount] = { 0 };
long long g_eqRoadLive = 0, g_eqRoadSession = 0, g_eqAnsIn[5] = { 0 }, g_eqAnsStale = 0, g_eqAnsSent = 0, g_eqAnsFail = 0;
long long g_eqMalformed = 0, g_eqFault = 0, g_eqPairsOpened = 0, g_eqOwnPairsOpened = 0, g_eqLogged = 0;
double g_eqBudgetSent = 0.0, g_eqBudgetIn = 0.0, g_eqBudgetSpent = 0.0;
unsigned int g_eqNextReq = 0;
const long long kEqLogLimit = 200;
bool g_eqInstalled = false;
/* T-327 review fold (2026-09-30) */
bool g_ownEatOff = false;   /* M2: the first caught fault in the owner's apply turns owner-side eating OFF until the game closes */
long long g_eqOwnOffRefused = 0, g_eqBudgetDroppedPairs = 0, g_eqDeadDisagree = 0, g_eqTablesCleared = 0;
double g_eqBudgetDropped = 0.0;
long g_eqWorldGen = 0, g_eqLinkGen = 0;
bool g_eqGenSeen = false;

std::string EffN(long long v) { std::ostringstream s; s << v; return s.str(); }
std::string EffF3(double v) { std::ostringstream s; s.setf(std::ios::fixed); s.precision(3); s << v; return s.str(); }
bool EqLog() { if (g_eqLogged >= kEqLogLimit) return false; ++g_eqLogged; return true; }

// ---- THE EATER'S PAIR TABLE. A detour claims and feeds a slot; the main thread sends, stops and frees it. ----
struct EatSlot
{
    volatile LONG state;               /* 0 free, 1 being claimed by a detour, 2 live, 3 stopped (the detour answers 1) */
    volatile unsigned int target, eater;
    volatile LONG rateBits;            /* the last bite's rate, as float bits */
    volatile LONG64 micro;             /* rate x frame time since the last send, micro-units */
    volatile LONG lastBite;            /* GetTickCount of the last detour call for the pair */
    /* MAIN THREAD only */
    unsigned int reqId;
    DWORD opened, lastSend, unansweredSince;
    DWORD stoppedAt;                   /* review fold M1: the STOP's time - the pair frees 10 s after it, bites or not */
    long long sent;
    double budgetSent;
};
const int kEatSlots = 32;
EatSlot g_eat[kEatSlots];

int EffOwn(const void* obj, unsigned int* uid)   /* 1 driven here, 0 a copy, -1 no uid. Address compares only - any thread. */
{
    const unsigned int u = (obj != 0) ? FindSpawnedUid(obj) : 0;
    *uid = u;
    if (u == 0) return -1;
    return net::IsUidMineAnyThread(u) ? 1 : 0;
}

float EffDt()   /* the engine's frame time (the float MedicalSystem::gettingEaten multiplies the rate by) */
{
    const unsigned long long a = (kEffDtRva != 0) ? AddrAbs(kEffDtRva) : 0;
    if (a == 0) return 1.0f / 60.0f;
    return *(const volatile float*)(uintptr_t)a;
}

// ANY THREAD: no allocation, lock or log. true = the pair is stopped (or the table is full): the caller answers 1.
bool EatBite(unsigned int vu, unsigned int eu, float rate)
{
    const LONG now = (LONG)::GetTickCount();
    const LONG64 add = (LONG64)cooffect::EatBiteMicro(rate, EffDt());
    LONG rb = 0;
    std::memcpy(&rb, &rate, 4);
    for (int i = 0; i < kEatSlots; ++i)
    {
        EatSlot& s = g_eat[i];
        const LONG st = s.state;
        if ((st == 2 || st == 3) && s.target == vu && s.eater == eu)
        {
            ::InterlockedExchange(&s.lastBite, now);
            if (st == 3) { ::InterlockedIncrement64(&g_eatStopReturns); return true; }
            ::InterlockedExchange(&s.rateBits, rb);
            ::InterlockedExchangeAdd64(&s.micro, add);
            return false;
        }
    }
    for (int i = 0; i < kEatSlots; ++i)
    {
        EatSlot& s = g_eat[i];
        if (::InterlockedCompareExchange(&s.state, 1, 0) != 0) continue;
        s.target = vu;
        s.eater = eu;
        ::InterlockedExchange(&s.rateBits, rb);
        ::InterlockedExchange64(&s.micro, add);
        ::InterlockedExchange(&s.lastBite, now);
        ::InterlockedExchange(&s.state, 2);
        return false;
    }
    ::InterlockedIncrement64(&g_eatTableFull);
    return true;
}

// DETOUR, any thread.
bool detour_eat(::Character* victim, float rate, ::Character* eater)
{
    ::InterlockedIncrement64(&g_eatCalls);
    if (g_eatThread == 0) ::InterlockedExchange(&g_eatThread, (LONG)::GetCurrentThreadId());
    unsigned int vu = 0, eu = 0;
    const int vm = EffOwn(victim, &vu);
    const int em = EffOwn(eater, &eu);
    switch (cooffect::EatDetourCase(vm, em))
    {
    case cooffect::kCaseOriginal: ::InterlockedIncrement64(&g_eatOwnBites); return orig_eat(victim, rate, eater);
    case cooffect::kCaseSuppress: ::InterlockedIncrement64(&g_eatSuppressed); return false;
    case cooffect::kCaseNoActor:  ::InterlockedIncrement64(&g_eatNoActorUid); return true;
    default: break;
    }
    ::InterlockedIncrement64(&g_eatCopyBites);
    return EatBite(vu, eu, rate);
}

// MAIN THREAD. POD, __try (C2712): one gettingEaten call; 1 / 0 its answer, -1 a fault.
int EffCallEat(EatFn fn, ::Character* v, float rate, ::Character* a)
{
    __try { return fn(v, rate, a) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

const char* StopName(int w)
{
    static const char* const k[kStopCount] = { "done", "refused", "timeout", "noRoute", "handedHere", "copyGone", "notSendable" };
    return (w >= 0 && w < kStopCount) ? k[w] : "?";
}

void EatStop(EatSlot& s, int why, const std::string& detail)
{
    s.stoppedAt = ::GetTickCount();
    ::InterlockedExchange(&s.state, 3);
    ++g_eqStopped;
    ++g_eqStopWhy[why];
    if (why == kStopTimeout) ++g_eqTimeout;
    if (EqLog())
        DebugLog("[EFFECT] eat END (eater's game) req=" + EffN(s.reqId) + " victim uid=" + EffN(s.target) + " (copy) eater uid=" + EffN(s.eater)
                 + " (mine) why=" + StopName(why) + (detail.empty() ? std::string() : " (" + detail + ")") + " sent=" + EffN(s.sent)
                 + " budgetSent=" + EffF3(s.budgetSent) + " after " + EffN((long long)(::GetTickCount() - s.opened))
                 + " ms - the eater's call now answers 1 for this pair (its eating task ends)");
}

void EatFree(EatSlot& s)
{
    s.reqId = 0; s.opened = 0; s.lastSend = 0; s.unansweredSince = 0; s.stoppedAt = 0; s.sent = 0; s.budgetSent = 0.0;
    s.target = 0; s.eater = 0;
    ::InterlockedExchange64(&s.micro, 0);
    ::InterlockedExchange(&s.state, 0);
}

// ---- THE OWNER'S PAIRS (MAIN THREAD) ----
struct OwnPair
{
    unsigned int target, actor, key, reqId;
    bool viaRelay;
    bool deadMismatch;                 /* review fold L4: the last request's 'target dead' flag disagreed with this game */
    float rate;
    double budget, spent;
    DWORD opened, lastReq;
    long long reqs, calls;
};
std::vector<OwnPair> g_own;
struct PendingAnswer { unsigned int target, actor, key, reqId; bool viaRelay; unsigned char result, reason; };
std::vector<PendingAnswer> g_ansQ;   /* the K2 safe point's answers, sent by EffectTick */

void EffAnswer(unsigned int target, unsigned int actor, unsigned int reqId, unsigned int key, bool viaRelay,
               unsigned char result, unsigned char reason)
{
    cooffect::EffectMsg a;
    a.dir = cooffect::kDirAnswer; a.kind = cooffect::kKindEat; a.reqId = reqId; a.targetUid = target; a.actorUid = actor;
    a.result = result; a.reason = reason;
    std::vector<char> b;
    if (!cooffect::EncodeEffect(&b, a)) { ++g_eqAnsFail; return; }
    if (net::SendEffectAnswer(key, viaRelay, b) != 0) ++g_eqAnsSent; else ++g_eqAnsFail;
}

void OwnEnd(size_t i, const std::string& why)
{
    const OwnPair& o = g_own[i];
    if (EqLog())
        DebugLog("[EFFECT] eat END (owner's game) req=" + EffN(o.reqId) + " victim uid=" + EffN(o.target) + " (mine) eater uid=" + EffN(o.actor)
                 + " (copy) why=" + why + " requests=" + EffN(o.reqs) + " calls=" + EffN(o.calls) + " spent=" + EffF3(o.spent)
                 + " left=" + EffF3(o.budget) + " after " + EffN((long long)(::GetTickCount() - o.opened)) + " ms");
    g_own.erase(g_own.begin() + (ptrdiff_t)i);
}

int OwnFind(unsigned int target, unsigned int actor)
{
    for (size_t i = 0; i < g_own.size(); ++i) if (g_own[i].target == target && g_own[i].actor == actor) return (int)i;
    return -1;
}

float EffDistSq(::Character* a, ::Character* b)
{
    Ogre::Vector3 pa, pb;
    if (a == 0 || b == 0 || !SafeReadPosition(a, &pa) || !SafeReadPosition(b, &pb)) return -1.0f;
    const float dx = pa.x - pb.x, dy = pa.y - pb.y, dz = pa.z - pb.z;
    return dx * dx + dy * dy + dz * dz;
}

void EatOnRequest(const cooffect::EffectMsg& m, unsigned int fromKey, bool viaRelay)
{
    ++g_eqIn;
    g_eqBudgetIn += m.budget;
    ::Character* v = FindSpawned(m.targetUid);
    ::Character* a = FindSpawned(m.actorUid);
    const float dsq = EffDistSq(v, a);
    const int vdead = (v != 0) ? StoreIsDeadPod(v) : -1;
    int why = cooffect::EffectValidate(net::IsUidMine(m.targetUid), net::RemoteMayWriteStill(m.actorUid, fromKey), v != 0,
                                       a != 0, EngineWritesBlocked(), dsq);
    if (why == cooffect::kReasonNone && g_ownEatOff) { why = cooffect::kReasonState; ++g_eqOwnOffRefused; }
    const int at = OwnFind(m.targetUid, m.actorUid);
    const bool sentDead = (m.flags & cooffect::kEatFlagTargetDead) != 0;
    const bool deadMismatch = (vdead >= 0) && (sentDead != (vdead == 1));
    if (deadMismatch)
    {
        ++g_eqDeadDisagree;
        if ((at < 0 || !g_own[(size_t)at].deadMismatch) && EqLog())
            DebugLog(std::string("[EFFECT] eat dead-flag DISAGREES (owner's game) req=") + EffN(m.reqId) + " victim uid=" + EffN(m.targetUid)
                     + " eater uid=" + EffN(m.actorUid) + " - the eater's game says " + (sentDead ? "dead" : "alive") + ", here "
                     + (vdead == 1 ? "dead" : "alive"));
    }
    if (at >= 0) g_own[(size_t)at].deadMismatch = deadMismatch;
    if (why != cooffect::kReasonNone)
    {
        ++g_eqRefused;
        ++g_eqRefusedWhy[why];
        const bool answered = cooffect::ReasonAnswered(why);
        if (answered) EffAnswer(m.targetUid, m.actorUid, m.reqId, fromKey, viaRelay, cooffect::kResultRefused, (unsigned char)why);
        if (EqLog())
            DebugLog(std::string("[EFFECT] eat REFUSED (owner's game) req=") + EffN(m.reqId) + " victim uid=" + EffN(m.targetUid) + " eater uid="
                     + EffN(m.actorUid) + " why=" + cooffect::ReasonName(why) + " dist=" + (dsq >= 0.0f ? EffF3(std::sqrt(dsq)) : std::string("?"))
                     + " units from " + (viaRelay ? "the notebook (LIVE)" : "the session link")
                     + (why == cooffect::kReasonState ? " (owner-side eating is OFF after a caught fault)" : "")
                     + (answered ? " - REFUSED answered" : " - not answered (the sender does not drive that eater; its pair is left alone)"));
        if (at >= 0 && cooffect::RefusalEndsOwnerPair(why)) OwnEnd((size_t)at, std::string("refused: ") + cooffect::ReasonName(why));
        return;
    }
    const DWORD now = ::GetTickCount();
    int i = at;
    if (i < 0)
    {
        OwnPair o;
        o.target = m.targetUid; o.actor = m.actorUid; o.key = fromKey; o.reqId = m.reqId; o.viaRelay = viaRelay;
        o.rate = m.rate; o.budget = 0.0; o.spent = 0.0; o.opened = now; o.lastReq = now; o.reqs = 0; o.calls = 0;
        o.deadMismatch = deadMismatch;
        g_own.push_back(o);
        i = (int)g_own.size() - 1;
        ++g_eqOwnPairsOpened;
        if (EqLog())
            DebugLog(std::string("[EFFECT] eat START (owner's game) req=") + EffN(m.reqId) + " victim uid=" + EffN(m.targetUid)
                     + " (mine) eater uid=" + EffN(m.actorUid) + " (copy of the sender's) rate=" + EffF3(m.rate)
                     + ((m.flags & cooffect::kEatFlagTargetDead) != 0 ? " corpse on the eater's game" : " alive on the eater's game")
                     + " dist=" + EffF3(std::sqrt(dsq)) + " units via " + (viaRelay ? "the notebook (LIVE SLOT)" : "the session link")
                     + " - applied through the engine's own gettingEaten at the K2 safe point");
    }
    OwnPair& o = g_own[(size_t)i];
    o.key = fromKey; o.viaRelay = viaRelay; o.reqId = m.reqId; o.rate = m.rate; o.lastReq = now; ++o.reqs;
    o.budget += (double)m.budget;
    EffAnswer(m.targetUid, m.actorUid, m.reqId, fromKey, viaRelay, cooffect::kResultApplied, cooffect::kReasonNone);
}

void EatOnAnswer(const cooffect::EffectMsg& m, unsigned int fromKey)
{
    if (m.result >= 1 && m.result <= 4) ++g_eqAnsIn[m.result];
    for (int i = 0; i < kEatSlots; ++i)
    {
        EatSlot& s = g_eat[i];
        if (s.state != 2 || s.reqId != m.reqId || s.target != m.targetUid || s.eater != m.actorUid) continue;
        if (!net::RemoteMayWriteStill(m.targetUid, fromKey)) break;   /* only the victim's owner answers for it */
        if (cooffect::EatActorOnAnswer(m.result) == 0) { s.unansweredSince = 0; return; }
        if (m.result == cooffect::kResultDone) EatStop(s, kStopDone, "the owner's gettingEaten answered 1");
        else EatStop(s, kStopRefused, std::string("the owner refused: ") + cooffect::ReasonName(m.reason));
        return;
    }
    ++g_eqAnsStale;
}

// ==== A SNEAK KNOCKOUT (Task_StealthKO) OF ANOTHER GAME'S CHARACTER ====
//
// The engine road (1.0.65 disassembly of TaskStealthKO_update; build/decomp_34cd20.txt): the task walks the assassin up (stage 0),
// plays the knockout animation (stage 1) and, past the animation's threshold, rolls once (stage 2): chance =
// StealthKoChance(assassin, victim, 1), roll = RandomRange(0, StealthKoRollMax = 100); success when chance > roll. Success:
// "*Thunk*", experience, MedicalSystem::knockout(victim medical, skill 0x16 x StealthKoSkillScale) (return StealthKoKnockoutRet).
// Failure: "Failed!", the victim's AI alerted (0x38), the victim made to see the assassin (5 s), both remember each other, the victim turns on
// the assassin.
// Who decides: the VICTIM's game, for an NPC and for another player's character alike - it holds the victim's senses (whether it
// sees the assassin), the victim's true position and the victim itself, and its copy of the assassin carries the owner's stats
// (stats.cpp, MSG_STATS: the 44 saved values, the knockout skill - stat 0x16, CharStats +0xB8 - among them). The chance it rolls
// is the engine's own StealthKoChance(its copy of the assassin, its victim, 1).
// On the assassin's game the task's update is skipped in stage 1 while the victim is a copy (the knockout animation is not
// re-requested and the assassin is not turned to the victim meanwhile; the check repeats each frame) until the main thread has
// classified the victim and sent ONE KO request (decided 0 a player character, 4 an NPC: [STEALTHKO] ATTEMPT). The victim's
// game, at its next K2 safe point, refuses when the victim sees the assassin (the engine ends the task without a roll then:
// REFUSED STATE) or the two stand more than kFarUnits apart there (REFUSED FAR), answers ALREADY when the victim is down, else
// rolls, applies the outcome through the engine's own calls on its own character, answers APPLIED with it and pushes STATE
// ([STEALTHKO] DECIDE). The assassin's game then runs its task with the chance hook answering above or below every roll, so the
// engine's own branch, text, experience and failure reaction follow the verdict ([STEALTHKO] RESULT);
//   a refusal, ALREADY, 3 s with no answer, no road to the victim's owner, its kind unreadable, a world load, a link change, the
//                         copy gone -> the task ends through its own exit (no text, no experience), the engine's own quiet end.
// The engine's knockout of the copy stays refused by the knockout gate (medical.cpp, counted AWAITING STATE): the copy falls on its
// owner's STATE. The owner applies the outcome at the K2 safe point through the engine's own calls on its own character and
// pushes STATE.
// While a verdict is awaited or not yet read by the task (NEW, HELD, FORCED), the copy's knockdown from its owner's STATE waits
// (EffectKoHoldsKnockdown): the task ends without text or experience when it finds its victim down. The owner sends its APPLIED
// answer before pushing the STATE that carries the outcome; the two ride different roads (STATE unreliable), so the order they
// arrive in is not guaranteed - the knockdown hold is what keeps the task's verdict.
// Threads: the task and chance hooks run on the AI worker, the sight-check hook (FactionHostileEntry, acting only for the task's own
// call) on any thread - a fixed 32-slot interlocked table; no allocation, lock, log or main-thread call there.
unsigned long long kKoTaskRva = 0;       static AddrReg kKoTaskRva_reg("TaskStealthKO_update", &kKoTaskRva);         /* Steam_1.0.65 0x34CD20 */
unsigned long long kKoChanceRva = 0;     static AddrReg kKoChanceRva_reg("StealthKoChance", &kKoChanceRva);           /* Steam_1.0.65 0x884420 */
unsigned long long kKoChanceRet = 0;     static AddrReg kKoChanceRet_reg("StealthKoChanceRet", &kKoChanceRet);        /* Steam_1.0.65 0x34D02B */
unsigned long long kKoKnockRet = 0;      static AddrReg kKoKnockRet_reg("StealthKoKnockoutRet", &kKoKnockRet);        /* Steam_1.0.65 0x34D493 */
unsigned long long kKoRollRva = 0;       static AddrReg kKoRollRva_reg("RandomRange", &kKoRollRva);                   /* Steam_1.0.65 0x9B0DB0 */
unsigned long long kKoAlertRva = 0;      static AddrReg kKoAlertRva_reg("StealthKoFailAlert", &kKoAlertRva);          /* Steam_1.0.65 0x684BD0 */
unsigned long long kKoMarkRva = 0;       static AddrReg kKoMarkRva_reg("FactionMarkHostile", &kKoMarkRva);            /* Steam_1.0.65 0x858070: a character's senses record of another set to "sees it" (5 s) */
unsigned long long kKoHostileRva = 0;    static AddrReg kKoHostileRva_reg("FactionHostileEntry", &kKoHostileRva);     /* Steam_1.0.65 0x854960: a character's senses say it sees the other (record +0x38) */
unsigned long long kKoReactRva = 0;      static AddrReg kKoReactRva_reg("StealthKoFailReact", &kKoReactRva);          /* Steam_1.0.65 0x5C8800 */
unsigned long long kKoAttackRva = 0;     static AddrReg kKoAttackRva_reg("StealthKoFailAttack", &kKoAttackRva);       /* Steam_1.0.65 0x5CA610 */
unsigned long long kKoRollMaxRva = 0;    static AddrReg kKoRollMaxRva_reg("StealthKoRollMax", &kKoRollMaxRva);        /* Steam_1.0.65 0x1682250 (100.0) */
unsigned long long kKoSkillScaleRva = 0; static AddrReg kKoSkillScaleRva_reg("StealthKoSkillScale", &kKoSkillScaleRva); /* Steam_1.0.65 0x1681AD8 (0.01) */
unsigned long long kKoRememberRva = 0;   static AddrReg kKoRememberRva_reg("Character_remember", &kKoRememberRva);    /* Steam_1.0.65 0x673A10 */
unsigned long long kKoGetStatRva = 0;    static AddrReg kKoGetStatRva_reg("CharStatsGetStat", &kKoGetStatRva);       /* Steam_1.0.65 0x883BF0 */
unsigned long long kKoHandCharRva = 0;   static AddrReg kKoHandCharRva_reg("Hand_asCharacter", &kKoHandCharRva);      /* Steam_1.0.65 0x7974F0 */

typedef void  (*KoTaskFn)(void* task, void* ai);
typedef float (*KoChanceFn)(::Character* assassin, ::Character* victim, char flag);
typedef float (*KoRollFn)(float lo, float hi);
typedef ::Character* (*KoHandCharFn)(const void* hand);
typedef ::Character* (*KoAiCharFn)(void* ai);
typedef void  (*KoAiEndFn)(void* ai);
typedef void* (*KoFactionFn)(::Character* c);
typedef bool  (*KoHostileFn)(void* faction, ::Character* who);
typedef void* (*KoRecordFn)(void* senses, ::Character* who);
typedef void  (*KoMarkFn)(void* faction, ::Character* who, char on);
typedef unsigned long long (*KoAlertFn)(void* ai, ::Character* who, unsigned int what, char flag);
typedef void  (*KoRememberFn)(::Character* c, ::Character* who, int what);
typedef void  (*KoReactFn)(::Character* c, ::Character* who, char on, int zero);
typedef void  (*KoAttackFn)(::Character* c, ::Character* who);
typedef float (*KoGetStatFn)(void* stats, int stat, bool flag);

const size_t kKoTaskStage   = 0x78;    /* the task's stage, -1..3 */
const size_t kKoTaskHand    = 0x10;    /* the task's victim hand */
const size_t kKoAiChar      = 0x48;    /* the task owner's vtable: its character (the assassin) */
const size_t kKoAiEnd       = 0x28;    /* the task owner's vtable: the task's own exit (no text, no experience) */
const size_t kKoCharSenses = 0x68;    /* Character vtable: its senses (what it sees and hears of others) */
const size_t kKoCharAi      = 0x280;   /* Character: the object the alert is given to (the engine passes the pointer stored here) */
const size_t kKoCharStats   = 0x450;   /* Character: its stats */
const int    kKoStatSkill   = 0x16;    /* the skill the knockout scales by */

KoTaskFn     orig_ko_task = 0;
KoChanceFn   orig_ko_chance = 0;
KoChanceFn   g_koChanceFn = 0;         /* the chance as the owner calls it: the hook's trampoline, or the entry when not hooked */
KoRollFn     g_koRoll = 0;
KoHandCharFn g_koHandChar = 0;
KoHostileFn  g_koHostile = 0;
KoMarkFn     g_koMark = 0;
KoAlertFn    g_koAlert = 0;
KoRememberFn g_koRemember = 0;
KoReactFn    g_koReact = 0;
KoAttackFn   g_koAttack = 0;
KoGetStatFn  g_koGetStat = 0;
float g_koRollMax = 100.0f, g_koSkillScale = 0.01f;   /* read from the image at install */
bool g_koOn = false;                   /* the assassin's side: both hooks in and every row it needs */
bool g_koOwnOn = false;                /* the owner's side: every engine call it makes has its row (OFF after a caught fault) */
unsigned long long g_koChanceRetAbs = 0, g_koKnockRetAbs = 0;
KoHostileFn  orig_ko_hostile = 0;      /* the sight check's trampoline (the sees hook) */
KoRecordFn   g_koRecord = 0;           /* a senses' record of another (the call FactionHostileEntry makes at +0x14); 0 when not found */
unsigned long long g_koSeesRetAbs = 0; /* the return address of the task's own sight check (found in the task's code at install); 0 none */
bool g_koSeesOn = false;               /* the sees hook is in */
volatile LONG64 g_koSeesForced = 0;    /* the task's sight checks answered "does not see" for a slot holding its verdict (FORCED) */

/* ANY THREAD (interlocked) */
volatile LONG64 g_koStage1 = 0, g_koForced = 0, g_koTableFull = 0, g_koEndCalls = 0;
volatile LONG   g_koThread = 0;
/* MAIN THREAD */
long long g_koHeld = 0, g_koClassified = 0, g_koSent = 0, g_koKo = 0, g_koNoticed = 0, g_koEnded = 0, g_koReleased = 0;
long long g_koStale = 0, g_koAnsIn = 0, g_koAnsStale = 0, g_koLogged = 0;
long long g_koOwnIn = 0, g_koOwnRolled = 0, g_koOwnApplied = 0, g_koOwnKo = 0, g_koOwnNoticed = 0, g_koOwnRefused = 0, g_koOwnAlready = 0,
          g_koOwnClassDiffer = 0;
long long g_koOwnFault = 0;
/* an NPC victim: the assassin's game's attempts and their results; the victim's game's decisions */
long long g_koAttempt = 0, g_koResKo = 0, g_koResFailed = 0, g_koResRefused = 0, g_koResNone = 0;
long long g_koDecide = 0, g_koDecideKo = 0, g_koDecideFailed = 0, g_koDecideRefused = 0;
long long g_koStateHeld = 0;           /* slots that held the copy's knockdown for their verdict (once per slot) */
unsigned int g_koNextReq = 0;
const long long kKoLogLimit = 200;
bool KoLog() { if (g_koLogged >= kKoLogLimit) return false; ++g_koLogged; return true; }

struct KoSlot
{
    volatile LONG state;                   /* cooffect::kKo* */
    volatile unsigned int victim, assassin;
    volatile LONG lastSeen;                /* GetTickCount of the task update's last visit while the slot is live */
    volatile LONG outcome;                 /* kKoOutcome*: the verdict the victim's game answered (FORCED / FORCED_DONE) */
    volatile LONG reqId;                   /* set by the main thread before the slot leaves NEW */
    void* volatile senses;                 /* FORCED: the victim copy's senses and the assassin, stamped by the task's update before */
    ::Character* volatile assassinChar;    /* the engine's update runs - the sees hook matches the task's sight check on them */
    /* MAIN THREAD only */
    int pc;
    DWORD opened, sentAt;
    int ans;
    unsigned char ansOutcome, ansReason;
    bool stateHeld;                        /* this slot has held the copy's knockdown for its verdict (counted once) */
};
const int kKoSlots = 32;
KoSlot g_ko[kKoSlots];

bool KoLive(LONG st)
{
    return st == cooffect::kKoNew || st == cooffect::kKoHeld || st == cooffect::kKoForced || st == cooffect::kKoEnd;
}

/* ANY THREAD: the pair's LIVE slot (NEW, HELD, FORCED, END). A finished slot (ENDED, FORCED_DONE) never
   matches: a new attempt on the same pair (the task back in stage 1) takes a slot of its own. */
int KoFind(unsigned int vu, unsigned int au)
{
    for (int i = 0; i < kKoSlots; ++i)
    {
        const LONG st = g_ko[i].state;
        if (KoLive(st) && g_ko[i].victim == vu && g_ko[i].assassin == au) return i;
    }
    return -1;
}

int KoClaim(unsigned int vu, unsigned int au, LONG now)   /* ANY THREAD */
{
    for (int i = 0; i < kKoSlots; ++i)
    {
        KoSlot& s = g_ko[i];
        if (::InterlockedCompareExchange(&s.state, cooffect::kKoClaiming, cooffect::kKoFree) != cooffect::kKoFree) continue;
        s.victim = vu;
        s.assassin = au;
        ::InterlockedExchange(&s.reqId, 0);
        ::InterlockedExchange(&s.outcome, 0);
        s.senses = 0;
        s.assassinChar = 0;
        ::InterlockedExchange(&s.lastSeen, now);   /* claimed: no verdict yet */
        ::InterlockedExchange(&s.state, cooffect::kKoNew);
        return i;
    }
    return -1;
}

void KoEndTask(void* ai) { ((KoAiEndFn)((*(void***)ai)[kKoAiEnd / sizeof(void*)]))(ai); }

void* KoSensesOf(::Character* c);

// DETOUR, the AI worker: TaskStealthKO_update(task, its owner).
void detour_ko_task(void* task, void* ai)
{
    if (!g_koOn || task == 0 || ai == 0 || *(const volatile int*)((const char*)task + kKoTaskStage) != 1) { orig_ko_task(task, ai); return; }
    ::InterlockedIncrement64(&g_koStage1);
    if (g_koThread == 0) ::InterlockedExchange(&g_koThread, (LONG)::GetCurrentThreadId());
    ::Character* victim = g_koHandChar((const char*)task + kKoTaskHand);
    ::Character* assassin = ((KoAiCharFn)((*(void***)ai)[kKoAiChar / sizeof(void*)]))(ai);
    unsigned int vu = 0, au = 0;
    const int vOwn = (victim != 0) ? EffOwn(victim, &vu) : -1;
    if (victim == 0 || assassin == 0 || vOwn < 0 || EffOwn(assassin, &au) != 1) { orig_ko_task(task, ai); return; }
    const LONG now = (LONG)::GetTickCount();
    int i = KoFind(vu, au);
    /* a victim driven here: only a slot already open for the pair (the victim was handed over while its attempt waited) steers the
       task - its request is out, so this game does not roll a second time; with no slot the engine's own task runs */
    if (i < 0 && vOwn != 0) { orig_ko_task(task, ai); return; }
    if (i < 0) i = KoClaim(vu, au, now);
    if (i < 0) { ::InterlockedIncrement64(&g_koTableFull); KoEndTask(ai); return; }   /* 32 held at once: no roll anyone can stand behind */
    KoSlot& s = g_ko[i];
    const LONG st = s.state;
    if (KoLive(st)) ::InterlockedExchange(&s.lastSeen, now);
    const int act = cooffect::KoHookAct((int)st);
    if (act == cooffect::kKoActHold) return;
    if (act == cooffect::kKoActEndTask)
    {
        if (::InterlockedCompareExchange(&s.state, cooffect::kKoEnded, cooffect::kKoEnd) == cooffect::kKoEnd)
        {
            ::InterlockedIncrement64(&g_koEndCalls);
            KoEndTask(ai);
        }
        return;
    }
    /* FORCED: the sees hook answers this pair's sight check "does not see" until the chance hook has read the verdict */
    s.senses = KoSensesOf(victim);
    s.assassinChar = assassin;
    orig_ko_task(task, ai);
}

// DETOUR, the AI worker: StealthKoChance(assassin, victim, flag) - acts only for the task's own roll (StealthKoChanceRet).
float detour_ko_chance(::Character* assassin, ::Character* victim, char flag)
{
    const unsigned long long ret = (unsigned long long)(uintptr_t)_ReturnAddress();   /* first, before anything can disturb the frame */
    if (!g_koOn || ret != g_koChanceRetAbs) return orig_ko_chance(assassin, victim, flag);
    unsigned int vu = 0, au = 0;
    const int vOwn = EffOwn(victim, &vu);
    const int aOwn = EffOwn(assassin, &au);
    const int i = (vu != 0 && au != 0) ? KoFind(vu, au) : -1;
    /* the slot acts while the assassin is still driven here; a victim handed to this game after the request went out keeps its slot
       (the task's update steers it the same way): one attempt, one roll */
    if (i < 0 || vOwn < 0 || aOwn != 1) return orig_ko_chance(assassin, victim, flag);
    KoSlot& s = g_ko[i];
    if (::InterlockedCompareExchange(&s.state, cooffect::kKoForcedDone, cooffect::kKoForced) == cooffect::kKoForced)
    {
        ::InterlockedIncrement64(&g_koForced);
        return cooffect::KoForcedChance((unsigned char)s.outcome);
    }
    return orig_ko_chance(assassin, victim, flag);   /* not this slot's forced roll: the engine's own chance */
}

// DETOUR, any thread: FactionHostileEntry(senses, who) - acts only for the task's own sight check (g_koSeesRetAbs). While a slot holds
// the victim's game's verdict for the pair (FORCED), the task's copy of the victim does not see the assassin, so the task reaches its
// stage 2 and reads the verdict there; otherwise the engine's task ends quietly when its copy sees the assassin (decomp_34cd20.txt:81,
// the exit at :266) - no text, no experience, no crime - while the victim's game has already applied the verdict.
bool detour_ko_hostile(void* senses, ::Character* who)
{
    const unsigned long long ret = (unsigned long long)(uintptr_t)_ReturnAddress();   /* first, before anything can disturb the frame */
    if (g_koSeesRetAbs != 0 && ret == g_koSeesRetAbs && senses != 0 && who != 0)
    {
        for (int i = 0; i < kKoSlots; ++i)
        {
            const KoSlot& s = g_ko[i];
            if (s.state == cooffect::kKoForced && s.senses == senses && s.assassinChar == who)
            {
                ::InterlockedIncrement64(&g_koSeesForced);
                return false;
            }
        }
    }
    return orig_ko_hostile(senses, who);
}

const char* KoRoadName(int road) { return road == cooffect::kRoadLive ? "the notebook (LIVE SLOT)" : "the session link"; }
const char* KoOutcomeName(int oc)
{
    return oc == cooffect::kKoOutcomeKnockedOut ? "KNOCKED OUT" : (oc == cooffect::kKoOutcomeNoticed ? "NOTICED" : "?");
}

// MAIN THREAD: frees the slot unless a hook moved it meanwhile.
void KoFree(KoSlot& s, LONG expect)
{
    if (::InterlockedCompareExchange(&s.state, cooffect::kKoClaiming, expect) != expect) return;
    s.victim = 0; s.assassin = 0; s.pc = cooffect::kKoPcUnknown; s.opened = 0; s.sentAt = 0;
    s.ans = cooffect::kKoAnsNone; s.ansOutcome = 0; s.ansReason = 0; s.stateHeld = false;
    ::InterlockedExchange(&s.reqId, 0);
    ::InterlockedExchange(&s.outcome, 0);
    ::InterlockedExchange(&s.state, cooffect::kKoFree);
}

// A slot's request id for a log line; a request not sent (yet) says so.
std::string KoReqText(const KoSlot& s, bool notSent)
{
    const LONG rq = s.reqId;
    if (rq == 0) return "none (not sent yet)";
    return EffN((long long)rq) + (notSent ? " (not sent yet)" : "");
}

// MAIN THREAD: the task ends through its own exit at its next update.
void KoEnd(KoSlot& s, LONG expect, const std::string& why)
{
    if (::InterlockedCompareExchange(&s.state, cooffect::kKoEnd, expect) != expect) return;
    ++g_koEnded;
    if (KoLog()) DebugLog("[STEALTHKO] END req=" + KoReqText(s, expect == cooffect::kKoNew) + " why=" + why + " - task ended through the engine's own exit");
}

// Why a task is ended (the END line's why=): already, refused:<reason>, handed over, copy gone, unreadable, timeout - and, from their own callers,
// no road, world load, link change.
std::string KoEndWhy(const KoSlot& s, bool here)
{
    if (s.ans == cooffect::kKoAnsAlready) return "already";
    if (s.ans == cooffect::kKoAnsRefused) return std::string("refused:") + cooffect::ReasonName(s.ansReason);
    if (net::IsUidMine(s.victim)) return "handed over (the victim is driven here now; its request is out - no second roll)";
    if (!here || s.pc == -2) return "copy gone";
    if (s.pc == -1) return "unreadable";
    return "timeout";
}

// The knockout skill (stat 0x16: the one StealthKoChance and the knockout scale by) as this game reads it on `c`;
// kKoSkillUnread when it cannot. POD, __try (C2712).
float KoSkillPod(::Character* c)
{
    if (c == 0 || g_koGetStat == 0) return cooffect::kKoSkillUnread;
    __try
    {
        void* stats = *(void* const*)((const char*)c + kKoCharStats);
        if (stats == 0) return cooffect::kKoSkillUnread;
        const float k = g_koGetStat(stats, kKoStatSkill, false);
        return (k >= 0.0f && k <= cooffect::kKoSkillMax) ? k : cooffect::kKoSkillUnread;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return cooffect::kKoSkillUnread; }
}

// The DECIDE line's measurements (an NPC victim): sees / dist / chance / roll, '-' for a step not reached.
std::string KoDecideText(int sees, float distSq, bool rolled, float chance, float roll)
{
    return "sees=" + (sees < 0 ? std::string("-") : EffN(sees)) + " dist=" + (distSq >= 0.0f ? EffF3(std::sqrt(distSq)) : std::string("?"))
           + " chance=" + (rolled ? EffF3(chance) : std::string("-")) + " roll=" + (rolled ? EffF3(roll) : std::string("-")) + " ";
}

// The DECIDE line's zero-chance inputs (KoZeroInputsPod) and whether they put the engine's chance at 0: zeroBranch=1 when the victim
// is in melee combat mode, holds a senses record of the assassin and the copy's +0xE8 is not 1; 0 when one of them rules it out; '-'
// when a value was not read.
std::string KoZeroText(int e8, bool e8Ok, int melee, int record)
{
    int z = -1;
    if (melee == 0 || record == 0 || (e8Ok && e8 == 1)) z = 0;
    else if (melee == 1 && record == 1 && e8Ok) z = 1;
    return "e8=" + (e8Ok ? EffN((long long)e8) : std::string("-")) + " melee=" + (melee < 0 ? std::string("-") : EffN((long long)melee))
           + " record=" + (record < 0 ? std::string("-") : EffN((long long)record)) + " zeroBranch=" + (z < 0 ? std::string("-") : EffN((long long)z)) + " ";
}

int KoSend(const KoSlot& s, unsigned char decided, float skill)   /* MAIN THREAD: the road it went by, 0 none */
{
    cooffect::EffectMsg m;
    m.dir = cooffect::kDirRequest; m.kind = cooffect::kKindKo; m.reqId = (unsigned int)s.reqId; m.targetUid = s.victim;
    m.actorUid = s.assassin; m.source = cooffect::kKoSourceStealth; m.decided = decided; m.skill = skill;
    std::vector<char> b;
    if (!cooffect::EncodeEffect(&b, m)) return 0;
    const int road = net::SendEffectRequest(s.victim, b);
    if (road != 0) ++g_koSent;
    return road;
}

struct KoJob { unsigned int victim, actor, key, reqId; bool viaRelay; unsigned char decided; DWORD at; float skill; };
std::vector<KoJob> g_koJobs;          /* the owner's accepted requests, acted at the K2 safe point */
struct KoPending { unsigned int victim, actor, key, reqId; bool viaRelay; unsigned char result, reason, outcome; };
std::vector<KoPending> g_koAnsQ;      /* the K2 safe point's refusals and ALREADY answers, sent by EffectTick (an APPLIED answer is sent at once, before its STATE) */

void KoAnswer(const KoPending& p)
{
    cooffect::EffectMsg a;
    a.dir = cooffect::kDirAnswer; a.kind = cooffect::kKindKo; a.reqId = p.reqId; a.targetUid = p.victim; a.actorUid = p.actor;
    a.result = p.result; a.reason = p.reason; a.outcome = p.outcome;
    std::vector<char> b;
    if (!cooffect::EncodeEffect(&b, a)) { ++g_eqAnsFail; return; }
    if (net::SendEffectAnswer(p.key, p.viaRelay, b) != 0) ++g_eqAnsSent; else ++g_eqAnsFail;
}

void KoReply(const KoJob& j, bool queue, unsigned char result, unsigned char reason, unsigned char outcome)
{
    KoPending p = { j.victim, j.actor, j.key, j.reqId, j.viaRelay, result, reason, outcome };
    if (queue) g_koAnsQ.push_back(p); else KoAnswer(p);
}

// The owner's line: DECIDE for an NPC (the tail starts with KoDecideText), ROLL for a player character.
void KoOwnerLog(const KoJob& j, const std::string& tail)
{
    if (!KoLog()) return;
    if (j.decided == cooffect::kKoDecidedNpc)
        DebugLog("[STEALTHKO] DECIDE uid=" + EffN(j.victim) + " req=" + EffN(j.reqId) + " " + tail + " assassin uid=" + EffN(j.actor) + " (copy)");
    else
        DebugLog("[STEALTHKO] ROLL (owner's game, a player character) req=" + EffN(j.reqId) + " victim uid=" + EffN(j.victim)
                 + " (mine) assassin uid=" + EffN(j.actor) + " (copy) " + tail);
}

// mid: the DECIDE line's measurements (an NPC victim; not printed for a player character).
void KoOwnRefuse(const KoJob& j, int why, bool queue, const std::string& mid, const std::string& detail)
{
    ++g_koOwnRefused;
    ++g_eqRefusedWhy[why];
    const bool npc = (j.decided == cooffect::kKoDecidedNpc);
    if (npc) ++g_koDecideRefused;
    const bool answered = cooffect::ReasonAnswered(why);
    if (answered) KoReply(j, queue, cooffect::kResultRefused, (unsigned char)why, cooffect::kKoOutcomeNone);
    KoOwnerLog(j, (npc ? mid + "-> REFUSED " : std::string("-> REFUSED why=")) + cooffect::ReasonName(why) + detail
                  + (answered ? std::string() : std::string(" (not answered: the sender does not drive that assassin)")));
}

// MAIN THREAD (EffectOnNet): the victim's owner takes a KO request; it is decided at the K2 safe point.
void KoOnRequest(const cooffect::EffectMsg& m, unsigned int fromKey, bool viaRelay)
{
    ++g_koOwnIn;
    KoJob j;
    j.victim = m.targetUid; j.actor = m.actorUid; j.key = fromKey; j.reqId = m.reqId; j.viaRelay = viaRelay; j.decided = m.decided;
    j.at = ::GetTickCount(); j.skill = m.skill;
    if (j.decided == cooffect::kKoDecidedNpc) ++g_koDecide;
    ::Character* v = FindSpawned(m.targetUid);
    ::Character* a = FindSpawned(m.actorUid);
    const float dsq = EffDistSq(v, a);
    int why = cooffect::EffectValidate(net::IsUidMine(m.targetUid), net::RemoteMayWriteStill(m.actorUid, fromKey), v != 0, a != 0,
                                       EngineWritesBlocked(), dsq);
    const bool off = (why == cooffect::kReasonNone && !g_koOwnOn);
    if (off) why = cooffect::kReasonState;
    if (why != cooffect::kReasonNone)
    {
        KoOwnRefuse(j, why, false, KoDecideText(-1, dsq, false, 0.0f, 0.0f),
                    off ? std::string(" (the sneak knockout is OFF on this game: see the [STEALTHKO] install or FAULTED line)") : std::string());
        return;
    }
    g_koJobs.push_back(j);
}

// MAIN THREAD (EffectOnNet): the victim's owner answered this game's KO request; EffectTick acts on it.
void KoOnAnswer(const cooffect::EffectMsg& m, unsigned int fromKey)
{
    ++g_koAnsIn;
    for (int i = 0; i < kKoSlots; ++i)
    {
        KoSlot& s = g_ko[i];
        const LONG st = s.state;
        if (st != cooffect::kKoHeld || (unsigned int)s.reqId != m.reqId || s.victim != m.targetUid
            || s.assassin != m.actorUid) continue;
        if (!net::RemoteMayWriteStill(m.targetUid, fromKey)) break;   /* only the victim's owner answers for it */
        s.ansOutcome = m.outcome;
        s.ansReason = m.reason;
        s.ans = cooffect::KoAnswerKind(m.result, m.outcome);
        return;
    }
    ++g_koAnsStale;
}

// MAIN THREAD: the RESULT line of an NPC victim's held attempt that ends without a verdict (refused, ALREADY, no answer).
void KoResultEnd(const KoSlot& s, const std::string& why)
{
    if (s.pc != 0) return;
    if (s.ans == cooffect::kKoAnsRefused || s.ans == cooffect::kKoAnsAlready) ++g_koResRefused; else ++g_koResNone;
    if (KoLog()) DebugLog("[STEALTHKO] RESULT <- req=" + EffN((long long)s.reqId) + " "
                          + (s.ans == cooffect::kKoAnsRefused ? std::string("REFUSED ") + cooffect::ReasonName(s.ansReason)
                             : (s.ans == cooffect::kKoAnsAlready ? std::string("ALREADY") : "NONE " + why))
                          + " - the task ends through the engine's own exit (no roll)");
}

// MAIN THREAD, every frame: classify, send, hold, release and end the assassin's slots; send the safe point's answers.
void KoTick(DWORD now)
{
    for (size_t k = 0; k < g_koAnsQ.size(); ++k) KoAnswer(g_koAnsQ[k]);
    g_koAnsQ.clear();
    for (int i = 0; i < kKoSlots; ++i)
    {
        KoSlot& s = g_ko[i];
        const LONG st = s.state;
        if (st < cooffect::kKoNew) continue;
        if (st == cooffect::kKoNew && s.reqId == 0)
        {
            if (++g_koNextReq == 0) ++g_koNextReq;
            s.pc = cooffect::kKoPcUnknown; s.opened = now; s.sentAt = 0; s.ans = cooffect::kKoAnsNone; s.ansOutcome = 0; s.ansReason = 0;
            ::InterlockedExchange(&s.reqId, (LONG)g_koNextReq);
        }
        /* lastSeen may be stamped by the task's update after `now` was read: that counts as 0 ms, never as a wrapped ~49 days */
        if (cooffect::KoSlotDone((int)st, cooffect::EffSinceMs((unsigned int)now, (unsigned int)s.lastSeen)))
        {
            if (st == cooffect::kKoForced || st == cooffect::kKoEnd) ++g_koStale;
            KoFree(s, st);
            continue;
        }
        const bool mine = net::IsUidMine(s.victim);
        const bool here = FindSpawned(s.victim) != 0;
        const unsigned int waited = (s.sentAt != 0) ? (unsigned int)(now - s.sentAt) : 0u;
        int d = cooffect::KoActorDecide((int)st, s.pc, mine, here, s.ans, waited);
        if (d == cooffect::kKoActClassify)
        {
            s.pc = CopyIsPlayerCharacter(s.victim);
            ++g_koClassified;
            d = cooffect::KoActorDecide((int)st, s.pc, mine, here, s.ans, waited);
        }
        if (d == cooffect::kKoActHold) continue;
        if (d == cooffect::kKoActRunEngine)
        {
            ++g_koReleased;
            if (KoLog()) DebugLog("[STEALTHKO] RELEASED req=" + EffN((long long)s.reqId) + " - the victim is driven HERE now: the engine's own roll");
            KoFree(s, st);
            continue;
        }
        if (d == cooffect::kKoActEndTask)
        {
            const std::string why = KoEndWhy(s, here);
            if (st == cooffect::kKoHeld) KoResultEnd(s, why);
            KoEnd(s, st, why);
            continue;
        }
        if (d == cooffect::kKoActRunForced)
        {
            ::InterlockedExchange(&s.outcome, (LONG)s.ansOutcome);
            if (::InterlockedCompareExchange(&s.state, cooffect::kKoForced, st) != st) continue;
            const bool ko = (s.ansOutcome == cooffect::kKoOutcomeKnockedOut);
            if (ko) ++g_koKo; else ++g_koNoticed;
            if (s.pc == 0)
            {
                if (ko) ++g_koResKo; else ++g_koResFailed;
                if (KoLog()) DebugLog("[STEALTHKO] RESULT <- req=" + EffN((long long)s.reqId) + " " + (ko ? "KNOCKED OUT" : "FAILED") + " held="
                                      + EffN((long long)(now - s.opened)) + " ms - the engine's own task shows it");
            }
            else if (KoLog()) DebugLog("[STEALTHKO] VERDICT req=" + EffN((long long)s.reqId) + " -> " + KoOutcomeName(s.ansOutcome) + " held="
                                       + EffN((long long)(now - s.opened)) + " ms - the engine's own task shows it");
            continue;
        }
        if (d != cooffect::kKoActSend) continue;
        const unsigned char decided = cooffect::KoRequestDecided(s.pc);
        const float skill = KoSkillPod(FindSpawned(s.assassin));
        const int road = KoSend(s, decided, skill);
        if (road == 0) { KoEnd(s, st, "no road"); continue; }
        s.sentAt = now;
        if (::InterlockedCompareExchange(&s.state, cooffect::kKoHeld, st) != st) continue;
        ++g_koHeld;
        if (decided == cooffect::kKoDecidedNpc)
        {
            ++g_koAttempt;
            if (KoLog()) DebugLog("[STEALTHKO] ATTEMPT -> uid=" + EffN(s.victim) + " req=" + EffN((long long)s.reqId) + " assassin uid="
                                  + EffN(s.assassin) + " (mine) skill=" + EffF3(skill) + " - the knockout animation started; the victim's game decides"
                                  + " (via " + KoRoadName(road) + ")");
            continue;
        }
        if (KoLog()) DebugLog("[STEALTHKO] HOLD req=" + EffN((long long)s.reqId) + " victim uid=" + EffN(s.victim)
                              + " (a COPY here, player character) assassin uid=" + EffN(s.assassin)
                              + " (mine) - the knockout animation started; the victim's game rolls (via " + KoRoadName(road) + ")");
    }
}

// MAIN THREAD (EffClearTables): a world load or a link change - a held task ends; every other slot and the owner's jobs go.
void KoClearTables(const char* why)
{
    const char* w = (why != 0 && std::strstr(why, "world") != 0) ? "world load" : "link change";
    for (int i = 0; i < kKoSlots; ++i)
    {
        KoSlot& s = g_ko[i];
        const LONG st = s.state;
        if (st == cooffect::kKoNew || st == cooffect::kKoHeld)
        {
            if (st == cooffect::kKoHeld) KoResultEnd(s, w);
            KoEnd(s, st, w);
        }
        else if (st >= cooffect::kKoNew && st != cooffect::kKoEnd && st != cooffect::kKoEnded) KoFree(s, st);
    }
    g_koJobs.clear();
    g_koAnsQ.clear();
}

// ---- THE OWNER'S ENGINE CALLS (K2 safe point, worker paused). POD, __try (C2712): -1 a caught fault. ----
void* KoSensesOf(::Character* c) { return ((KoFactionFn)((*(void***)c)[kKoCharSenses / sizeof(void*)]))(c); }

int KoHostilePod(::Character* v, ::Character* a)   /* 1 the victim sees the assassin (the engine's task then ends without a roll), 0 not */
{
    __try
    {
        void* f = KoSensesOf(v);
        if (f == 0) return -1;
        return g_koHostile(f, a) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

int KoRollPod(::Character* a, ::Character* v, float* chance, float* roll)
{
    __try
    {
        *chance = g_koChanceFn(a, v, 1);
        *roll = g_koRoll(0.0f, g_koRollMax);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// The inputs of the engine's zero-chance branch (decomp_884420.txt:22-24,78-82: the chance is 0 when the victim is in melee combat
// mode, its senses hold a record of the assassin and the assassin's +0xE8 is not 1), read on this game's own victim and its copy of the
// assassin: e8 the copy's +0xE8; melee the victim's combat object's +0x130 (the byte 0x436270 tests for melee: Character +0x648, +8);
// record 1 when the victim's senses hold a record of the assassin (g_koRecord; -1 when it was not found). Logging only; -1 / e8Ok
// false for a value not read. POD, __try (C2712).
const size_t kKoCharE8 = 0xE8, kKoCharBody = 0x648, kKoBodyCombat = 0x8, kKoCombatMelee = 0x130;
int KoZeroInputsPod(::Character* v, ::Character* a, int* e8, bool* e8Ok, int* melee, int* record)
{
    *e8 = 0; *e8Ok = false; *melee = -1; *record = -1;
    __try
    {
        *e8 = *(const int*)((const char*)a + kKoCharE8);
        *e8Ok = true;
        const char* body = *(const char* const*)((const char*)v + kKoCharBody);
        const char* cc = (body != 0) ? *(const char* const*)(body + kKoBodyCombat) : 0;
        if (cc != 0) *melee = (*(const unsigned char*)(cc + kKoCombatMelee) != 0) ? 1 : 0;
        if (g_koRecord != 0)
        {
            void* f = KoSensesOf(v);
            if (f != 0) *record = (g_koRecord(f, a) != 0) ? 1 : 0;
        }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

int KoKnockOutPod(unsigned int vuid, ::Character* a, float* skill01, int* unc, float* koTimer)   /* 0 = not applied (unreadable) */
{
    __try
    {
        void* stats = *(void* const*)((const char*)a + kKoCharStats);
        const float k = g_koGetStat(stats, kKoStatSkill, false) * g_koSkillScale;
        *skill01 = k;
        *unc = StealthKnockOutOwned(vuid, k, koTimer);
        return (*unc >= 0) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

int KoNoticedPod(::Character* v, ::Character* a)   /* the engine's failure reaction on the victim, in its order */
{
    __try
    {
        void* ai = *(void* const*)((const char*)v + kKoCharAi);
        if (ai != 0) g_koAlert(ai, a, 0x38, 0);
        void* f = KoSensesOf(v);
        if (f != 0) g_koMark(f, a, 1);
        g_koRemember(v, a, 4);
        g_koReact(v, a, 1, 0);
        g_koAttack(v, a);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

void KoOwnerFault(const KoJob& j, const char* what)
{
    ++g_koOwnFault;
    g_koOwnOn = false;
    ErrorLog(std::string("[STEALTHKO] the owner's ") + what + " FAULTED req=" + EffN(j.reqId) + " victim uid=" + EffN(j.victim)
             + " - caught; the owner's side of the sneak knockout is now OFF until the game closes (later requests are refused STATE)");
    KoOwnRefuse(j, cooffect::kReasonState, true, std::string(), std::string(" (a caught fault)"));
}

// K2 safe point (MAIN THREAD, worker paused): the victim's game decides each accepted request on its own character - the request's
// own check (FAR from its copy of the assassin), the victim down (ALREADY), the victim seeing the assassin (REFUSED STATE), then
// the engine's own chance and roll with its copy of the assassin - applies the outcome and answers it.
void KoOwnerDrain()
{
    if (g_koJobs.empty()) return;
    std::vector<KoJob> jobs;
    jobs.swap(g_koJobs);
    const DWORD now = ::GetTickCount();
    for (size_t k = 0; k < jobs.size(); ++k)
    {
        KoJob j = jobs[k];
        const bool npc = (j.decided == cooffect::kKoDecidedNpc);
        ::Character* v = FindSpawned(j.victim);
        ::Character* a = FindSpawned(j.actor);
        const float dsq = EffDistSq(v, a);
        int why = cooffect::EffectValidate(net::IsUidMine(j.victim), net::RemoteMayWriteStill(j.actor, j.key), v != 0, a != 0, false, dsq);
        if (why == cooffect::kReasonNone && !g_koOwnOn) why = cooffect::kReasonState;
        /* past the cut (1.5 s after it arrived here): an outcome applied now could reach the assassin's game after its 3 s wait
           (counted from its send) has ended its task - refused BUSY, never applied */
        if (why == cooffect::kReasonNone && cooffect::KoOwnerPastCut(cooffect::EffSinceMs((unsigned int)now, (unsigned int)j.at))) why = cooffect::kReasonBusy;
        /* This game decides the same way whichever kind the assassin's game saw; a difference in how the two games classify the
           victim is said and counted. CopyIsPlayerCharacter reads this game's own character the same way: 1 = the watched player or
           a person of a player faction. */
        const int differ = (why == cooffect::kReasonNone) ? cooffect::KoClassDiffer(j.decided, CopyIsPlayerCharacter(j.victim))
                                                          : (int)cooffect::kKoClassSame;
        if (differ != cooffect::kKoClassSame) ++g_koOwnClassDiffer;
        const std::string note = (differ == cooffect::kKoClassOwnerSaysPc)
            ? std::string(" (the games disagree: the assassin's game called it an NPC; this game's own character is a player character - decided here all the same)")
            : (differ == cooffect::kKoClassOwnerSaysNpc)
            ? std::string(" (the games disagree: the assassin's game called it a player character; this game's own character is an NPC - rolled here)")
            : std::string();
        const bool down = (why == cooffect::kReasonNone) && (IsDownedCharacter(v) || StoreIsDeadPod(v) == 1);
        int sees = -1;
        if (why == cooffect::kReasonNone && !down)
        {
            sees = KoHostilePod(v, a);
            if (sees < 0) { KoOwnerFault(j, "hostile-entry check"); continue; }
        }
        const int check = cooffect::KoOwnerCheck(down, sees == 1, why);
        if (check == cooffect::kKoOwnAlready)
        {
            ++g_koOwnAlready;
            if (npc) ++g_koDecideRefused;
            KoReply(j, true, cooffect::kResultAlready, cooffect::kReasonNone, cooffect::kKoOutcomeNone);
            KoOwnerLog(j, (npc ? KoDecideText(sees, dsq, false, 0.0f, 0.0f) : std::string()) + "-> ALREADY" + note);
            continue;
        }
        if (check != cooffect::kKoOwnRoll)
        {
            KoOwnRefuse(j, check, true, KoDecideText(sees, dsq, false, 0.0f, 0.0f),
                        (sees == 1 ? std::string(" (the victim sees the assassin: the engine's task ends without a roll)") : std::string()) + note);
            continue;
        }
        int zE8 = 0, zMelee = -1, zRecord = -1;
        bool zE8Ok = false;
        if (npc) KoZeroInputsPod(v, a, &zE8, &zE8Ok, &zMelee, &zRecord);   /* logging only: a value not read says '-' */
        float ch = 0.0f, rl = 0.0f;
        if (KoRollPod(a, v, &ch, &rl) < 0) { KoOwnerFault(j, "roll"); continue; }
        ++g_koOwnRolled;
        const unsigned char oc = cooffect::KoDecidedOutcome(cooffect::KoDecide(check, ch, rl));
        const std::string rollText = npc ? (KoDecideText(sees, dsq, true, ch, rl) + KoZeroText(zE8, zE8Ok, zMelee, zRecord)) : ("chance=" + EffF3(ch) + " roll=" + EffF3(rl) + " ");
        std::string applied;
        if (oc == cooffect::kKoOutcomeKnockedOut)
        {
            float s01 = 0.0f, kt = 0.0f;
            int unc = -1;
            const int kr = KoKnockOutPod(j.victim, a, &s01, &unc, &kt);
            if (kr < 0) { KoOwnerFault(j, "knockout"); continue; }
            if (kr == 0) { KoOwnRefuse(j, cooffect::kReasonState, true, rollText, std::string(" (the knockout could not be applied: the medical system is unreadable)")); continue; }
            /* the answer first, sent from here (StatePush sends from this same safe point); the answer and the STATE ride different roads,
               so the assassin's game cannot count on the order - it holds the copy's knockdown until its task has read the verdict
               (EffectKoHoldsKnockdown) */
            KoReply(j, false, cooffect::kResultApplied, cooffect::kReasonNone, oc);
            const bool pushedKo = StatePush(j.victim);
            ++g_koOwnKo;
            if (npc) ++g_koDecideKo;
            applied = "-> KNOCKED OUT knockout(skill01=" + EffF3(s01) + ") unconcious=" + EffN(unc) + " koTimer=" + EffF3(kt)
                      + " pushed=" + EffN(pushedKo ? 1 : 0);
        }
        else
        {
            if (KoNoticedPod(v, a) < 0) { KoOwnerFault(j, "failure reaction"); continue; }
            KoReply(j, false, cooffect::kResultApplied, cooffect::kReasonNone, oc);   /* the answer first, as above */
            const bool pushedNo = StatePush(j.victim);
            ++g_koOwnNoticed;
            if (npc) ++g_koDecideFailed;
            applied = std::string(npc ? "-> FAILED" : "-> NOTICED") + ": AI alerted (0x38), the victim sees the assassin, attackingYou pushed="
                      + EffN(pushedNo ? 1 : 0);
        }
        ++g_koOwnApplied;
        /* the assassin's knockout skill as its own game read it, beside this game's copy of it (the copy carries the owner's stats) */
        const std::string skillText = npc ? (" skill sent=" + EffF3(j.skill) + " copy=" + EffF3(KoSkillPod(a))) : std::string();
        KoOwnerLog(j, rollText + applied + skillText + note);
    }
}

// INSTALL (main thread). POD, __try (C2712): the image's code bytes at `at`; 0 when unreadable.
int KoReadCode(uintptr_t at, unsigned char* out, int n)
{
    __try { for (int i = 0; i < n; ++i) out[i] = *(const unsigned char*)(at + i); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// The target of the rel32 CALL/JMP whose opcode is at `at` (its five bytes in b).
uintptr_t KoRel32(uintptr_t at, const unsigned char* b) { int rel = 0; std::memcpy(&rel, b + 1, 4); return at + 5 + (intptr_t)rel; }
// 1 when `target`, or one of up to two E9 stubs it starts, lands on `want`.
int KoReaches(uintptr_t target, uintptr_t want)
{
    for (int hop = 0; hop < 3 && target != 0; ++hop)
    {
        if (target == want) return 1;
        unsigned char b[5];
        if (!KoReadCode(target, b, 5) || b[0] != 0xE9) return 0;
        target = KoRel32(target, b);
    }
    return 0;
}
// The task's own sight check: the one CALL in TaskStealthKO_update before its chance call's return (StealthKoChanceRet) that reaches
// FactionHostileEntry; its return address, or 0 (none, or more than one). Read before the task's entry is hooked.
uintptr_t KoFindSeesRet(uintptr_t task, uintptr_t chanceRet, uintptr_t hostile)
{
    if (task == 0 || hostile == 0 || chanceRet <= task + 5 || chanceRet - task > 0x1000) return 0;
    const int n = (int)(chanceRet - task);
    unsigned char code[0x1000];
    if (!KoReadCode(task, code, n)) return 0;
    uintptr_t found = 0;
    int hits = 0;
    for (int i = 0; i + 5 <= n; ++i)
    {
        if (code[i] != 0xE8) continue;
        if (!KoReaches(KoRel32(task + (uintptr_t)i, code + i), hostile)) continue;
        ++hits;
        found = task + (uintptr_t)i + 5;
    }
    return (hits == 1) ? found : 0;
}
// The senses-record lookup FactionHostileEntry calls: its CALL at +0x14, followed by TEST RAX,RAX (the row's own signature
// pattern); the call's target, or 0 when the bytes differ.
uintptr_t KoFindRecordFn(uintptr_t hostile)
{
    unsigned char b[0x1C];
    if (hostile == 0 || !KoReadCode(hostile, b, (int)sizeof(b))) return 0;
    if (b[0x14] != 0xE8 || b[0x19] != 0x48 || b[0x1A] != 0x85 || b[0x1B] != 0xC0) return 0;
    return KoRel32(hostile + 0x14, b + 0x14);
}

void InstallStealthKoHooks()
{
    std::string missing;
    if (kKoTaskRva == 0) missing += " TaskStealthKO_update";
    if (kKoChanceRva == 0) missing += " StealthKoChance";
    if (kKoChanceRet == 0) missing += " StealthKoChanceRet";
    if (kKoKnockRet == 0) missing += " StealthKoKnockoutRet";
    if (kKoRollRva == 0) missing += " RandomRange";
    if (kKoAlertRva == 0) missing += " StealthKoFailAlert";
    if (kKoMarkRva == 0) missing += " FactionMarkHostile";
    if (kKoHostileRva == 0) missing += " FactionHostileEntry";
    if (kKoReactRva == 0) missing += " StealthKoFailReact";
    if (kKoAttackRva == 0) missing += " StealthKoFailAttack";
    if (kKoRollMaxRva == 0) missing += " StealthKoRollMax";
    if (kKoSkillScaleRva == 0) missing += " StealthKoSkillScale";
    if (kKoRememberRva == 0) missing += " Character_remember";
    if (kKoGetStatRva == 0) missing += " CharStatsGetStat";
    if (kKoHandCharRva == 0) missing += " Hand_asCharacter";
    if (!missing.empty())
    {
        ErrorLog("[STEALTHKO] address rows missing:" + missing + " - a sneak knockout of another game's character is OFF on this game");
        return;
    }
    g_koRollMax = *(const float*)(uintptr_t)AddrAbs(kKoRollMaxRva);
    g_koSkillScale = *(const float*)(uintptr_t)AddrAbs(kKoSkillScaleRva);
    g_koRoll = (KoRollFn)(uintptr_t)AddrAbs(kKoRollRva);
    g_koHandChar = (KoHandCharFn)(uintptr_t)AddrAbs(kKoHandCharRva);
    g_koHostile = (KoHostileFn)(uintptr_t)AddrAbs(kKoHostileRva);
    g_koMark = (KoMarkFn)(uintptr_t)AddrAbs(kKoMarkRva);
    g_koAlert = (KoAlertFn)(uintptr_t)AddrAbs(kKoAlertRva);
    g_koRemember = (KoRememberFn)(uintptr_t)AddrAbs(kKoRememberRva);
    g_koReact = (KoReactFn)(uintptr_t)AddrAbs(kKoReactRva);
    g_koAttack = (KoAttackFn)(uintptr_t)AddrAbs(kKoAttackRva);
    g_koGetStat = (KoGetStatFn)(uintptr_t)AddrAbs(kKoGetStatRva);
    g_koChanceRetAbs = AddrAbs(kKoChanceRet);
    g_koKnockRetAbs = AddrAbs(kKoKnockRet);
    /* the sees hook's call site and the record lookup, read from the image before any entry here is hooked */
    const uintptr_t koTaskAbs = (uintptr_t)AddrAbs(kKoTaskRva);
    const uintptr_t seesRet = KoFindSeesRet(koTaskAbs, (uintptr_t)g_koChanceRetAbs, (uintptr_t)AddrAbs(kKoHostileRva));
    g_koRecord = (KoRecordFn)KoFindRecordFn((uintptr_t)AddrAbs(kKoHostileRva));
    const HookStatus hc = AddHook((void*)(uintptr_t)AddrAbs(kKoChanceRva), (void*)&detour_ko_chance, (void**)&orig_ko_chance);
    const bool okChance = (hc == SUCCESS);
    bool okTask = false;
    if (okChance) okTask = (AddHook((void*)(uintptr_t)AddrAbs(kKoTaskRva), (void*)&detour_ko_task, (void**)&orig_ko_task) == SUCCESS);
    g_koChanceFn = okChance ? orig_ko_chance : (KoChanceFn)(uintptr_t)AddrAbs(kKoChanceRva);
    g_koOn = okChance && okTask;
    /* the task's sight check answers "does not see" for a pair whose verdict waits to be read (FORCED); without it the task can end
       quietly before it reads the verdict the victim's game has already applied */
    if (g_koOn && seesRet != 0)
        g_koSeesOn = (AddHook((void*)(uintptr_t)AddrAbs(kKoHostileRva), (void*)&detour_ko_hostile, (void**)&orig_ko_hostile) == SUCCESS);
    if (g_koSeesOn) { g_koHostile = orig_ko_hostile; g_koSeesRetAbs = (unsigned long long)seesRet; }
    DebugLog(std::string("[STEALTHKO] sight-check AddHook ")
             + (!g_koOn ? "not tried (the assassin's side is OFF)"
                : (seesRet == 0 ? "not tried (the task's sight-check call was not found exactly once)" : (g_koSeesOn ? "SUCCESS" : "FAILED")))
             + (seesRet != 0 ? " callRet=task+" + EffN((long long)(seesRet - koTaskAbs)) : std::string())
             + " recordLookup=" + (g_koRecord != 0 ? "found" : "NOT found (the DECIDE line says record=-)"));
    g_koOwnOn = true;
    DebugLog(std::string("[STEALTHKO] chance AddHook ") + (okChance ? "SUCCESS" : "FAILED") + ", Task_StealthKO update AddHook "
             + (okTask ? "SUCCESS" : (okChance ? "FAILED" : "not tried")) + " rollMax=" + EffF3(g_koRollMax) + " skillScale=" + EffF3(g_koSkillScale)
             + " - a sneak knockout of another game's character: assassin's side " + (g_koOn ? "ON" : "OFF") + ", owner's side ON");
}

// ---- THE eatbite LEVER (TEST-ONLY) ----
unsigned int g_ebEater = 0, g_ebVictim = 0;
DWORD g_ebStart = 0, g_ebForMs = 0;
long long g_ebRuns = 0, g_ebCalls = 0, g_ebRunCalls = 0;

void EatBiteEnd(const std::string& why)
{
    DebugLog("[EFFECT] eatbite END eater uid=" + EffN(g_ebEater) + " victim uid=" + EffN(g_ebVictim) + " calls=" + EffN(g_ebRunCalls)
             + " after " + EffN((long long)(::GetTickCount() - g_ebStart)) + " ms - " + why);
    g_ebEater = 0; g_ebVictim = 0;
}

void EatBiteStep()   /* K2 safe point */
{
    if (g_ebEater == 0) return;
    if (::GetTickCount() - g_ebStart > g_ebForMs) { EatBiteEnd("its time is up"); return; }
    ::Character* v = FindSpawned(g_ebVictim);
    ::Character* e = FindSpawned(g_ebEater);
    if (v == 0 || e == 0) { EatBiteEnd(v == 0 ? "the victim is gone (removed?)" : "the eater is gone"); return; }
    const int r = EffCallEat(g_eatEntry, v, 1.0f, e);
    ++g_ebCalls; ++g_ebRunCalls;
    if (r < 0) EatBiteEnd("the call FAULTED (caught)");
    else if (r == 1) EatBiteEnd("the call answered 1: the pair stopped here, or the engine finished eating");
}

// Review fold L7 (MAIN THREAD): a world load or a session-link change - the uids and pairs describe the old world / link.
void EffClearTables(const char* why)
{
    long long e = 0;
    for (int i = 0; i < kEatSlots; ++i)
    {
        const LONG st = g_eat[i].state;
        if (st == 2 || st == 3) { EatFree(g_eat[i]); ++e; }
    }
    KoClearTables(why);
    const long long o = (long long)g_own.size();
    g_own.clear();
    g_ansQ.clear();
    if (g_ebEater != 0) EatBiteEnd(std::string("cleared by ") + why);
    ++g_eqTablesCleared;
    if ((e > 0 || o > 0) && EqLog())
        DebugLog(std::string("[EFFECT] tables CLEARED on ") + why + ": eater pairs=" + EffN(e) + " owner pairs=" + EffN(o));
}

} // namespace

void InstallEffectHook()
{
    InstallStealthKoHooks();   /* the sneak knockout's two hooks: independent of the eating hook below */
    const unsigned long long fn = (kEffEatRva != 0) ? AddrAbs(kEffEatRva) : 0;
    if (fn == 0)
    {
        ErrorLog("[EFFECT] Character_beingEaten is not in the address table - eating another game's character is NOT caught");
        return;
    }
    const HookStatus st = AddHook((void*)(uintptr_t)fn, (void*)&detour_eat, (void**)&orig_eat);
    if (st == SUCCESS) { g_eatEntry = (EatFn)(uintptr_t)fn; g_eqInstalled = true; }
    DebugLog(std::string("[EFFECT] Character::gettingEaten AddHook ") + (st == SUCCESS ? "SUCCESS" : "FAILED")
             + " - T-327: a copy eaten by our eater is a MSG_EFFECT request to its owner (protocol 111)");
}

void EffectTick()
{
    g_mainThread = ::GetCurrentThreadId();
    {
        const long wg = StoreWorldGenNow(), lg = net::SessionLinkGen();
        if (g_eqGenSeen && (wg != g_eqWorldGen || lg != g_eqLinkGen)) EffClearTables(wg != g_eqWorldGen ? "a world load" : "a session link change");
        g_eqGenSeen = true; g_eqWorldGen = wg; g_eqLinkGen = lg;
    }
    const DWORD now = ::GetTickCount();
    for (size_t k = 0; k < g_ansQ.size(); ++k)
    {
        const PendingAnswer& p = g_ansQ[k];
        EffAnswer(p.target, p.actor, p.reqId, p.key, p.viaRelay, p.result, p.reason);
    }
    g_ansQ.clear();
    KoTick(now);   /* the sneak knockout: the assassin's slots and the owner's queued answers */
    for (int i = 0; i < kEatSlots; ++i)
    {
        EatSlot& s = g_eat[i];
        const LONG st = s.state;
        if (st != 2 && st != 3) continue;
        if (st == 2 && s.reqId == 0)
        {
            if (++g_eqNextReq == 0) ++g_eqNextReq;
            s.reqId = g_eqNextReq; s.opened = now; s.lastSend = 0; s.unansweredSince = 0; s.sent = 0; s.budgetSent = 0.0;
            ++g_eqPairsOpened;
            float r0 = 0.0f;
            const LONG rb0 = s.rateBits;
            std::memcpy(&r0, &rb0, 4);
            if (EqLog())
                DebugLog("[EFFECT] eat START (eater's game) req=" + EffN(s.reqId) + " victim uid=" + EffN(s.target) + " (a COPY here) eater uid="
                         + EffN(s.eater) + " (mine) engine rate=" + EffF3(r0)
                         + (cooffect::FloatIn(r0, 0.0f, cooffect::kEatRateMax) ? std::string() : std::string(" (OUTSIDE 0..1000: its bites count as 0)"))
                         + " - the bites are held off the copy and sent to the victim's owner 4 times a second");
        }
        if (st == 2 && net::IsUidMine(s.target))
        {
            ++g_eqStopWhy[kStopHandedHere];
            if (EqLog()) DebugLog("[EFFECT] eat END (eater's game) req=" + EffN(s.reqId) + " victim uid=" + EffN(s.target)
                                  + " - it is driven HERE now (a hand-over): the engine's own call runs from the next bite");
            EatFree(s);
            continue;
        }
        if (st == 2 && FindSpawned(s.target) == 0) { EatStop(s, kStopCopyGone, "the copy is no longer here"); continue; }
        const int d = cooffect::EatActorDecide(st == 3, now, (unsigned int)s.lastBite, s.lastSend, s.unansweredSince, s.micro > 0,
                                                    (unsigned int)s.stoppedAt);
        if (d == cooffect::kActorFree) { EatFree(s); continue; }
        if (d == cooffect::kActorStopTimeout) { EatStop(s, kStopTimeout, "3 s with no answer from the owner"); continue; }
        if (d == cooffect::kActorClose)
        {
            if (EqLog()) DebugLog("[EFFECT] eat END (eater's game) req=" + EffN(s.reqId) + " victim uid=" + EffN(s.target) + " eater uid=" + EffN(s.eater)
                                  + " why=idle (no bite for 1 s, everything sent answered) sent=" + EffN(s.sent) + " budgetSent=" + EffF3(s.budgetSent));
            EatFree(s);
            continue;
        }
        if (d != cooffect::kActorSend) continue;
        cooffect::EffectMsg m;
        m.dir = cooffect::kDirRequest; m.kind = cooffect::kKindEat; m.reqId = s.reqId; m.targetUid = s.target; m.actorUid = s.eater;
        const LONG rb = s.rateBits;
        std::memcpy(&m.rate, &rb, 4);
        m.budget = cooffect::EatMicroToBudget((long long)::InterlockedExchange64(&s.micro, 0));
        ::Character* vc = FindSpawned(s.target);
        m.flags = (vc != 0 && StoreIsDeadPod(vc) == 1) ? cooffect::kEatFlagTargetDead : (unsigned char)0;
        std::vector<char> b;
        if (!cooffect::EncodeEffect(&b, m)) { EatStop(s, kStopBad, "rate " + EffF3(m.rate) + " / budget " + EffF3(m.budget) + " not sendable"); continue; }
        const int road = net::SendEffectRequest(s.target, b);
        if (road == 0) { EatStop(s, kStopNoRoute, "no road reaches the victim's owner"); continue; }
        if (road == cooffect::kRoadLive) ++g_eqRoadLive; else ++g_eqRoadSession;
        ++g_eqSent; ++s.sent;
        s.budgetSent += m.budget; g_eqBudgetSent += m.budget;
        s.lastSend = now;
        if (s.unansweredSince == 0) s.unansweredSince = now;
    }
    for (size_t i = 0; i < g_own.size();)
    {
        const OwnPair& o = g_own[i];
        const DWORD quiet = now - o.lastReq;
        if ((quiet > 2000 && !(o.budget > 0.0)) || quiet > 5000)
        {
            const bool dropped = o.budget > 0.0;
            if (dropped) { ++g_eqBudgetDroppedPairs; g_eqBudgetDropped += o.budget; }
            OwnEnd(i, dropped ? "stale (5 s with no request; the unspent budget " + EffF3(o.budget) + " dropped)" : std::string("idle"));
            continue;
        }
        ++i;
    }
}

void EffectSafePointDrain()
{
    if (EngineWritesBlocked()) return;   /* review fold L2: the eatbite lever respects blocked writes too */
    EatBiteStep();
    KoOwnerDrain();   /* the sneak knockouts asked of this game's characters */
    if (g_own.empty()) return;
    const float dt = EffDt();
    for (size_t i = 0; i < g_own.size();)
    {
        OwnPair& o = g_own[i];
        if (!g_ownEatOff && !(o.budget > 0.0)) { ++i; continue; }
        int why = cooffect::kReasonNone;
        ::Character* v = 0;
        ::Character* a = 0;
        if (g_ownEatOff) { why = cooffect::kReasonState; ++g_eqOwnOffRefused; }
        else if (!net::IsUidMine(o.target)) why = cooffect::kReasonNotMine;
        else if ((v = FindSpawned(o.target)) == 0) why = cooffect::kReasonGone;
        else if ((a = FindSpawned(o.actor)) == 0) why = cooffect::kReasonNoActor;
        /* T-327 fold 2: no standing-victim refusal - every bite goes through the engine's own gettingEaten (which wakes a victim down only on the
           knock-out timer, as in single player); the eater's game's task decides when eating stops, seeing this game's get-up by STATE */
        if (why == cooffect::kReasonNone)
        {
            float r = 0.0f;
            const float spend = cooffect::EatSpendStep((float)o.budget, o.rate, dt, &r);
            if (!(spend > 0.0f)) { ++i; continue; }
            const int ret = EffCallEat(orig_eat, v, r, a);
            if (ret < 0)
            {
                ++g_eqFault;
                ErrorLog("[EFFECT] the owner's gettingEaten FAULTED victim uid=" + EffN(o.target) + " eater uid=" + EffN(o.actor)
                         + " - caught; owner-side eating is now OFF until the game closes (every pair and every later request is refused STATE)");
                g_ownEatOff = true;
                why = cooffect::kReasonState;
            }
            else
            {
                ++g_eqApplied; ++o.calls;
                o.budget -= spend; o.spent += spend; g_eqBudgetSpent += spend;
                if (o.budget < 1e-6) o.budget = 0.0;
                if (ret == 1)
                {
                    ++g_eqDone;
                    PendingAnswer p = { o.target, o.actor, o.key, o.reqId, o.viaRelay, cooffect::kResultDone, cooffect::kReasonNone };
                    g_ansQ.push_back(p);
                    OwnEnd(i, "DONE - the engine's gettingEaten answered 1 (eaten up; its own removal follows)");
                    continue;
                }
                ++i;
                continue;
            }
        }
        ++g_eqRefused; ++g_eqRefusedWhy[why];
        PendingAnswer p = { o.target, o.actor, o.key, o.reqId, o.viaRelay, cooffect::kResultRefused, (unsigned char)why };
        g_ansQ.push_back(p);
        OwnEnd(i, std::string("refused at the safe point: ") + cooffect::ReasonName(why));
    }
}

void EffectOnNet(const char* p, size_t n, unsigned int fromKey, bool viaRelay)
{
    cooffect::EffectMsg m;
    const int dc = cooffect::DecodeEffect(p, n, &m);
    if (dc != cooffect::kDecodeOk)
    {
        ++g_eqMalformed;
        if (EqLog()) ErrorLog("[EFFECT] malformed EFFECT (decode " + EffN(dc) + ", " + EffN((long long)n) + " bytes) from " + EffN(fromKey) + " - ignored");
        return;
    }
    if (m.kind == cooffect::kKindKo)
    {
        if (m.dir == cooffect::kDirAnswer) KoOnAnswer(m, fromKey);
        else KoOnRequest(m, fromKey, viaRelay);
        return;
    }
    if (m.dir == cooffect::kDirAnswer) EatOnAnswer(m, fromKey);
    else EatOnRequest(m, fromKey, viaRelay);
}

void ReportEffect()
{
    long long liveNow = 0, stoppedNow = 0;
    for (int i = 0; i < kEatSlots; ++i) { if (g_eat[i].state == 2) ++liveNow; else if (g_eat[i].state == 3) ++stoppedNow; }
    std::ostringstream ss;
    ss << "[EFFECT] REPORT eatReq[sent,in,applied,refused,done,timeout,stopped]=" << g_eqSent << "," << g_eqIn << "," << g_eqApplied << ","
       << g_eqRefused << "," << g_eqDone << "," << g_eqTimeout << "," << g_eqStopped
       << " refused[notMine,gone,noActor,notSenders,state,noBed,busy,far]=";
    for (int r = 1; r < cooffect::kReasonCount; ++r) ss << (r > 1 ? "," : "") << g_eqRefusedWhy[r];
    ss << " stop[done,refused,timeout,noRoute,handedHere,copyGone,notSendable]=";
    for (int w = 0; w < kStopCount; ++w) ss << (w > 0 ? "," : "") << g_eqStopWhy[w];
    ss << " eat[calls,ownBites,copyBites,suppressed,noActorUid,tableFull,stopReturns]=" << g_eatCalls << "," << g_eatOwnBites << ","
       << g_eatCopyBites << "," << g_eatSuppressed << "," << g_eatNoActorUid << "," << g_eatTableFull << "," << g_eatStopReturns
       << " road[live,session]=" << g_eqRoadLive << "," << g_eqRoadSession
       << " answersIn[applied,done,refused,already,stale]=" << g_eqAnsIn[1] << "," << g_eqAnsIn[2] << "," << g_eqAnsIn[3] << ","
       << g_eqAnsIn[4] << "," << g_eqAnsStale
       << " answersOut[sent,failed]=" << g_eqAnsSent << "," << g_eqAnsFail
       << " budget[sent,in,spent]=" << EffF3(g_eqBudgetSent) << "," << EffF3(g_eqBudgetIn) << "," << EffF3(g_eqBudgetSpent)
       << " pairs[eaterOpened,ownerOpened,eaterLive,eaterStopped,ownerNow]=" << g_eqPairsOpened << "," << g_eqOwnPairsOpened << ","
       << liveNow << "," << stoppedNow << "," << (long long)g_own.size()
       << " faults=" << g_eqFault << " ownerEat=" << (g_ownEatOff ? "OFF(fault)" : "on") << " ownerOffRefused=" << g_eqOwnOffRefused
       << " budgetDropped[pairs,amount]=" << g_eqBudgetDroppedPairs << "," << EffF3(g_eqBudgetDropped)
       << " deadFlagDisagree=" << g_eqDeadDisagree << " tablesCleared=" << g_eqTablesCleared
       << " malformed=" << g_eqMalformed
       << " lever[runs,calls]=" << g_ebRuns << "," << g_ebCalls
       << " ko[held,classified,sent,ko,noticed,ended,released,forced]=" << g_koHeld << "," << g_koClassified << "," << g_koSent
       << "," << g_koKo << "," << g_koNoticed << "," << g_koEnded << "," << g_koReleased << "," << g_koForced
       << " koOwn[in,rolled,applied,ko,noticed,refused,already,classDiffer]=" << g_koOwnIn << "," << g_koOwnRolled << "," << g_koOwnApplied << ","
       << g_koOwnKo << "," << g_koOwnNoticed << "," << g_koOwnRefused << "," << g_koOwnAlready << "," << g_koOwnClassDiffer
       << " koMore[stage1Calls,taskEnds,tableFull,stale,answersIn,answersStale,ownerFaults,stateHeldForVerdict]=" << g_koStage1
       << "," << g_koEndCalls << "," << g_koTableFull << "," << g_koStale << "," << g_koAnsIn << "," << g_koAnsStale << ","
       << g_koOwnFault << "," << g_koStateHeld
       << " koNpc[attempts,resultKo,resultFailed,resultRefused,resultNone,decided,decideKo,decideFailed,decideRefused]=" << g_koAttempt
       << "," << g_koResKo << "," << g_koResFailed << "," << g_koResRefused << "," << g_koResNone << "," << g_koDecide << "," << g_koDecideKo
       << "," << g_koDecideFailed << "," << g_koDecideRefused
       << " koHook=" << (g_koOn ? "on" : "OFF") << " koSees[hook,forced]=" << (g_koSeesOn ? "on" : "OFF") << "," << (long long)g_koSeesForced << " koOwner=" << (g_koOwnOn ? "on" : "OFF") << " koThread=" << (long long)g_koThread
       << " hook=" << (g_eqInstalled ? "on" : "OFF")
       << " thread[detour,main]=" << (long long)g_eatThread << "," << (long long)g_mainThread
       << (g_eatThread == 0 ? " (no call yet)" : ((DWORD)g_eatThread == g_mainThread ? " (the main thread)" : " (another thread)"));
    DebugLog(ss.str());
}

unsigned int EffectKoAwaitingReq(unsigned long long ret, unsigned int victimUid)
{
    if (!g_koOn || ret == 0 || ret != g_koKnockRetAbs || victimUid == 0) return 0;
    for (int i = 0; i < kKoSlots; ++i)
    {
        const LONG st = g_ko[i].state;
        if (st == cooffect::kKoForcedDone && g_ko[i].victim == victimUid
            && g_ko[i].outcome == cooffect::kKoOutcomeKnockedOut)
            return (unsigned int)g_ko[i].reqId;
    }
    return 0;
}

// MAIN THREAD (spawn.cpp KnockdownWaitsNow - the owner's STATE on a copy): true while a sneak knockout of this copy waits for its
// verdict or holds it unread (NEW, HELD, FORCED). The engine's task ends without text or experience when it finds its victim down
// (its victim-down test runs before stage 1's animation), so the owner's knockdown of the copy waits until the forced chance has been
// read; the slot's own limits end the wait (3 s for an answer, 2 s with no visit from the task), and the hold itself never lasts
// longer than kKoHoldKnockdownMaxMs from the slot's opening (a task kept in stage 1 without its animation moving on).
// Logged once per slot.
const unsigned int kKoHoldKnockdownMaxMs = 8000;   /* 3 s for the answer plus a knockout animation's length, with room */
bool EffectKoHoldsKnockdown(unsigned int victimUid)
{
    if (victimUid == 0) return false;
    bool held = false;
    for (int i = 0; i < kKoSlots; ++i)
    {
        KoSlot& s = g_ko[i];
        const LONG st = s.state;
        if ((st != cooffect::kKoNew && st != cooffect::kKoHeld && st != cooffect::kKoForced) || s.victim != victimUid) continue;
        if (s.opened != 0 && cooffect::EffSinceMs(::GetTickCount(), s.opened) > kKoHoldKnockdownMaxMs) continue;   /* past the hold's own limit: the knockdown shows */
        held = true;
        if (s.stateHeld) continue;
        s.stateHeld = true;
        ++g_koStateHeld;
        if (KoLog()) DebugLog("[STEALTHKO] HOLD KNOCKDOWN req=" + KoReqText(s, st == cooffect::kKoNew) + " victim uid=" + EffN(victimUid)
                              + " (a COPY here) - the owner's knockdown of the copy waits until the engine's task has read the verdict");
    }
    return held;
}

std::string EatBiteLever(const std::string& eaterArg, unsigned int victimUid, float seconds)
{
    if (g_eatEntry == 0) return "error eatbite: the gettingEaten hook is not installed (see the [EFFECT] AddHook line)";
    if (victimUid == 0 || !(seconds >= 1.0f && seconds <= 120.0f)) return "error eatbite: usage eatbite <eaterUid | animal> <victimUid> <seconds 1..120>";
    unsigned int eater = 0;
    std::string how = "named";
    if (eaterArg == "animal")
    {
        float dsq = -1.0f;
        eater = NearestOwnedAnimal(victimUid, 100.0f, &dsq);
        if (eater == 0) return "error eatbite: no living, standing animal this game drives within 100 m of uid " + EffN(victimUid);
        how = "the nearest animal this game drives, " + EffF3(std::sqrt(dsq) / 10.0) + " m away";
    }
    else eater = (unsigned int)std::strtoul(eaterArg.c_str(), 0, 10);
    if (eater == 0 || eater == victimUid) return "error eatbite: the eater must be a uid other than the victim's, or `animal`";
    if (FindSpawned(eater) == 0) return "error eatbite: no character for eater uid " + EffN(eater);
    if (FindSpawned(victimUid) == 0) return "error eatbite: no character for victim uid " + EffN(victimUid);
    if (g_ebEater != 0) EatBiteEnd("replaced by a new eatbite");
    g_ebEater = eater; g_ebVictim = victimUid; g_ebStart = ::GetTickCount(); g_ebForMs = (DWORD)(seconds * 1000.0f); g_ebRunCalls = 0;
    ++g_ebRuns;
    const std::string line = "eater uid=" + EffN(eater) + " (" + how + ", " + (net::IsUidMine(eater) ? "mine" : "a copy") + ") victim uid="
        + EffN(victimUid) + " (" + (net::IsUidMine(victimUid) ? "mine" : "a copy") + ") seconds=" + EffF3(seconds)
        + " rate=1.000 - victim->gettingEaten(1.0, eater) THROUGH the hooked entry once a frame at the K2 safe point";
    DebugLog("[EFFECT] eatbite ARMED (TEST) " + line);
    return "ok eatbite " + line;
}

} // namespace coop
