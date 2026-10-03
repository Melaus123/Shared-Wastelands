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

#include "effect.h"
#include "spawn.h"
#include "store.h"         // EngineWritesBlocked, StoreIsDeadPod, StoreWorldGenNow
#include "replicate.h"     // nothing used here since T-327 fold 2 (IsDownedCharacter fed the removed standing-victim refusal)
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
       << " hook=" << (g_eqInstalled ? "on" : "OFF")
       << " thread[detour,main]=" << (long long)g_eatThread << "," << (long long)g_mainThread
       << (g_eatThread == 0 ? " (no call yet)" : ((DWORD)g_eatThread == g_mainThread ? " (the main thread)" : " (another thread)"));
    DebugLog(ss.str());
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
