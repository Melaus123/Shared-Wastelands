// peace.cpp - see peace.h. E42, the dev-only peace lever. Engine facts (Read 2026-09-05, build/read-hostility.md;
// every RVA resolved from build/exports_all.txt - F530):
//   Character::isEnemyOf(Character* who, bool factorInDisguises) 0x79BDB0, vtable +0x3E8. Its FIRST statement is
//     "if (!who) return false", and its last is FactionRelations::_isEnemy (relation <= -30.0f, .rdata 0x16CBCFC).
//     Race hate, bounty, escaped-slave status, disguises, occupied towns and the temporary-enemy memory tags all
//     enter through this one function - there is no separate player-side hostility path.
//   Character::isAllyOf(Character* who, bool factorInDisguises) 0x791830, vtable +0x3F0. It is NOT the negation
//     of the first: a neutral is neither. Two combat consumers filter by NOT-ALLY rather than by IS-ENEMY
//     (CombatClass::getNearestEnemyInAttackZone 0x608DA0, AI::findMeleeOpponent 0x59EFE0), so making nothing an
//     enemy would leave those two picking us. Making everyone an ally closes them. Both hooks are needed.
//   RootObjectBase::getOwnerFaction is vtable +0x58 (confirmed twice: RootObjectBase.h:52 and the .rdata vtable dump).
//   Faction+0x250 is the PlayerInterface*, non-zero for a player faction. Faction+0x78 is FactionRelations.
//   FactionRelations::getRelationData 0x6B4910 returns the entry; entry+0x00 ally flag, +0x02 atWar, +0x04 relation.
//
// DELIBERATELY NOT HOOKED: AI::treatsAsEnemy(RootObjectBase*) 0x5993A0. It does not route through isEnemyOf; it is the
// GOAP state predicate an attack task must satisfy before a plan is produced, and it has exactly one caller
// (AI::isTargetEnemy 0x59ACD0). Hooking it would disable the harness's own attack verb (F101/F102).
//
// NOT SHIPPED: the optional third hook, BountyManager::getPercievedBounty 0x853100, which would close the two
// bounty-driven arrest routes isEnemyOf does not cover (build/read-hostility.md section 4.3). read-hostility.md
// gives NO verified prologue for it, so it is left out; the counters below are what say whether anything still engages.
//
// KNOWN AND DISCLOSED: MapScreen::getMarkerColor 0x48E890, ContextMenu::showContextMenu 0x7A6020 and
// PlayerInterface::characterSelected 0x7FA870 all ask isEnemyOf, so under the lever the map and the cursor read
// friendly while the stored relation is still -100. That is a UI-vs-behaviour mismatch by construction and it is
// stated here rather than left to be discovered.
#include "peace.h"
#include "addresses.h"   /* P8h: kXxxRva below is filled from the address table, not hard-coded */
#include "playerfaction.h"
#include "../common/teamally.h"   /* T-546 (owner 512): the team pair - ally / not-enemy as for one faction */
#include "coop_log.h"
#include "game/GameWorld.h"
#include "game/Faction.h"
#include "game/FactionRelations.h"
#include "hooks.h"   /* mig1: coop::AddHook (own MinHook) */
#include <Windows.h>
#include <intrin.h>   /* _ReturnAddress: WHICH caller is asking the ally question (F619, the kClickRets pattern) */
#include <cstring>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace {

unsigned long long kIsEnemyRva = 0; static coop::AddrReg kIsEnemyRva_reg("IsEnemy", &kIsEnemyRva);   /* P8h: the address table fills this. Steam_1.0.65 0x79BDB0 */   // Character::isEnemyOf, vtable +0x3E8
unsigned long long kIsAllyRva = 0; static coop::AddrReg kIsAllyRva_reg("IsAlly", &kIsAllyRva);   /* P8h: the address table fills this. Steam_1.0.65 0x791830 */   // Character::isAllyOf,  vtable +0x3F0
unsigned long long kGetRelDataRva = 0; static coop::AddrReg kGetRelDataRva_reg("GetRelData", &kGetRelDataRva);   /* P8h: the address table fills this. Steam_1.0.65 0x6B4910 */   // FactionRelations::getRelationData (the call relations.cpp wraps)
const unsigned  kGetFactionSlot = 0x58 / 8;   // RootObjectBase::getOwnerFaction, vtable +0x58
const float     kEnemyRelation  = -30.0f;     // the engine's own cut-off, .rdata 0x16CBCFC

/* F619 / review-p7x Q2 - THE ALLY ANSWER IS FORCED AT TWO CALL SITES AND NOWHERE ELSE.
   Forcing isAllyOf true globally buys peace and pays for it in MOTION: the same answer gates medic goals
   (AI::_findWoundedAllies 0x99E7C0, findSplintableAllies 0x99D370), body carrying (scorePutCarriedDudeInBed
   0x5A02C0 and four more), looting (findBodyWithResourceToLoot 0x8CD650), trespass, turrets, stealth
   scoring, squad re-parenting and the map's own marker colours - about fifty sites in all, and the first
   three groups WALK CHARACTERS AWAY FROM WHERE A RUN PARKED THEM. The answer only has to be forced where it
   is used to PICK A FIGHT, and there are exactly two such consumers - they filter by NOT-ALLY rather than by
   IS-ENEMY, so making nothing an enemy would leave them picking us (build/read-hostility.md 1.2 / 4.1).
   This is policy.cpp's kClickRets pattern applied here: the detour reads its own return address and answers
   only at an enumerated allowlist, and every other caller gets the engine's own answer, COUNTED rather than
   silently absent.
   BOTH ADDRESSES WERE RE-READ OUT OF build/bin/kenshi_x64.exe FOR THIS PATCH (an exhaustive scan of .text
   for `FF /2 disp32` with disp 0x3F0, bounded by the next exported RVA - the .pdata RUNTIME_FUNCTION row for
   0x608DA0 covers only 0x608DA0..0x608DCB, so that function is split across entries and the pdata row alone
   is NOT a usable bound):
     CombatClass::getNearestEnemyInAttackZone 0x608DA0 (exports_all.txt:2210)
       0x608EC7: 41 B0 01           mov r8b,1          (factorInDisguises)
       0x608ECA: 48 8B CF           mov rcx,rdi        (self)
       0x608ECD: FF 90 F0 03 00 00  call [rax+0x3F0]   =>  returns to 0x608ED3, then 84 C0 test al,al
     AI::findMeleeOpponent 0x59EFE0 (exports_all.txt:349)
       0x59F106: 41 B0 01           mov r8b,1
       0x59F109: 48 8B D3           mov rdx,rbx        (who)
       0x59F10C: FF 90 F0 03 00 00  call [rax+0x3F0]   =>  returns to 0x59F112, then 84 C0 test al,al
     Each function contains EXACTLY ONE such site, which is what read-hostility.md 4.1 says of both.
   WHY THE RETURN ADDRESS IS THE CALLER'S AND NOT A THUNK'S: vtable slot +0x3F0 holds RVA 0x16888, whose
   bytes are `E9 ...` - a JMP to 0x791830, not a call - so the thunk consumes no stack and _ReturnAddress()
   inside the detour is the return address of the `call [rax+0x3F0]` itself. Verified for all three Character
   vtables (0x16F8D88, 0x16F1718, 0x16F10D8); MinHook's own patch sits at 0x791830 and is likewise a jump. */
static unsigned long long kAllyRets[2] = { 0, 0 };   /* stage 7/9: the address table fills these (AllyRet1/AllyRet2). Steam_1.0.65 0x608ED3, 0x59F112 */
static coop::AddrReg kAllyRet1_reg("AllyRet1", &kAllyRets[0]);
static coop::AddrReg kAllyRet2_reg("AllyRet2", &kAllyRets[1]);
/* P8a, folding F639 / review-p7y HIGH-3 - THE ALLOWLIST IS VERIFIED AT INSTALL, LIKE THE TWO PROLOGUES.
   Both hooks in this file check their prologue bytes before AddHook and report installed / not-installed from
   bytes read BACK; the allowlist got no such check, so if either call site moved - a different Kenshi build
   (Steam and non-Steam differ, which is why the fingerprint-table effort is on the board) or another mod
   rewriting getNearestEnemyInAttackZone / findMeleeOpponent - every ally call would fall to allyPassedThrough,
   allyForced would be 0 for the rest of the process, and the two lines that say the lever is armed would still
   print.  The peace lever's COMBAT half would be off while a run's premise said it was on.
   THE CHECK: each return address is the instruction AFTER `call [rax+0x3F0]`, which is six bytes long, so the
   six bytes at kAllyRets[i] - 6 must be FF 90 F0 03 00 00.  Read through the same SEH-guarded ReadBytesPod the
   prologue check uses. */
const unsigned char kAllyCallBytes[6] = { 0xFF, 0x90, 0xF0, 0x03, 0x00, 0x00 };
int g_allyRetVerified = -1;               /* -1 install not reached; otherwise how many of the sites matched */
volatile LONG g_peaceAllowlistMissing = 0;   /* 1 = at least one site did NOT match: the combat half of the lever is OFF */
/* DERIVED, never kept in step by hand - review-p6u LOW-3's rule, applied here for the same reason. */
const unsigned int kAllyRetCount = (unsigned int)(sizeof(kAllyRets) / sizeof(kAllyRets[0]));
/* THE COST OF THE VANILLA PROBE, BOUNDED AND STATED (F618). The probe makes a SECOND orig_isEnemy call, so
   it doubles the engine's hostility work for the calls it fires on. It fires only while the lever is on,
   only in the ambiguous case, and only this many times; after that peaceVanillaProbeCapped reads 1 and the
   counter's meaning is "over the first kVanillaProbeCap ambiguous calls", which the report line says. */
const LONG64    kVanillaProbeCap = 200000;

typedef char  (*IsEnemyFn)(void* self, void* who, char factorInDisguises);
typedef char  (*IsAllyFn) (void* self, void* who, char factorInDisguises);
typedef void* (*GetFactionFn)(void* self);
typedef void* (*GetRelDataFn)(void* self, ::Faction* other);

IsEnemyFn orig_isEnemy = 0;
IsAllyFn  orig_isAlly  = 0;

/* THE GATE. 0 = the engine answers, unmodified. Written only by SetPeaceOn (main thread), read by the two
   detours on the AI worker thread. */
volatile LONG g_peaceOn = 0;

/* The counters. Every one of them is touched from the AI worker thread, so every one is interlocked and none of
   them is a std::string. What each MEASURES is spelled out in ReportPeace's own line, because
   queriesAnswered counts calls the lever was in scope for and NOT calls whose answer it changed. */
volatile LONG64 g_queriesAnswered = 0;      /* every isEnemyOf/isAllyOf call made while the lever was ON */
volatile LONG64 g_hostileSuppressed = 0;    /* of those, treatsAsEnemy answers the lever flipped from true to false */
volatile LONG64 g_allyForced = 0;           /* of those, isAlly answers the lever flipped from false to true */
volatile LONG64 g_fallthrough = 0;          /* a pointer read gave DOUBT: the engine's own answer stands */
volatile LONG64 g_enemyCalls = 0, g_allyCalls = 0;   /* the same two events, kept apart */
volatile LONG64 g_neitherSidePlayer = 0;    /* the gate was ASKED and answered no - not a doubt, a decision */
/* F619: the return-address allowlist, both halves. gatedByRet counts ally calls the allowlist ADMITTED (the
   two melee finders - these are the only calls whose answer the lever can change); passedThrough counts ally
   calls from every OTHER site, which now get the engine's own answer. Their sum is allyCalls. */
volatile LONG64 g_allyGatedByRet = 0, g_allyPassedThrough = 0;
bool PlausiblePtrPod(const void* p) { return p != 0 && (uintptr_t)p > 0x10000 && (uintptr_t)p < 0x00007FFFFFFFFFFFull; }
/* F618: the treatsAsEnemy flip measured against an engine with BOTH hooks bypassed on this thread. hostileSuppressed
   alone cannot say whether the lever flipped an answer or whether the ally hook had already done the work
   upstream - isEnemyOf calls isAllyOf twice through iShouldntAggravateThisTarget 0x855AD0 - and a zero
   that cannot be interrogated is not a measurement (6a lesson 12). */
volatile LONG64 g_hostileSuppressedVsVanilla = 0, g_vanillaProbeCalls = 0;
volatile LONG   g_vanillaProbeCapped = 0;
/* The module base, cached at install on the main thread. The detours run on the AI worker thread millions of
   times a run and must not call GetModuleHandleA per query. 0 means "not installed yet", and the allowlist
   test fails CLOSED to the engine's own answer in that case. */
uintptr_t g_moduleBase = 0;
/* THE PROBE'S THREAD-LOCAL BYPASS. While this slot is non-zero on the calling thread BOTH detours hand the
   engine's own answer straight back, so the second orig_isEnemy call measures vanilla even though
   orig_isEnemy calls back into our ally hook through 0x855AD0. TlsAlloc/TlsGetValue allocate nothing and
   take no lock, which is what the AI thread needs; __declspec(thread) is not used because this DLL is loaded
   after process start. kTlsUnset means TlsAlloc was never reached and the probe is simply never taken. */
const DWORD kTlsUnset = 0xFFFFFFFFu;
DWORD g_tlsBypass = kTlsUnset;
int PeaceHooksBypassedHere()
{
    if (g_tlsBypass == kTlsUnset) return 0;
    return (::TlsGetValue(g_tlsBypass) != 0) ? 1 : 0;
}

/* 1 = this return address is one of the two fight-picking call sites. A return address below the module base
   or an uninstalled base answers 0 - the engine's own answer stands, which is the safe direction. */
int PeaceAllyRetAnswered(const void* ret)
{
    if (g_moduleBase == 0 || (uintptr_t)ret <= g_moduleBase) return 0;
    {
        const uintptr_t rva = (uintptr_t)ret - g_moduleBase;
        unsigned int i;
        for (i = 0; i < kAllyRetCount; ++i) if (kAllyRets[i] != 0 && rva == (uintptr_t)kAllyRets[i]) return 1;   /* an empty row is no caller */
    }
    return 0;
}

/* T-546 (owner 512): THE TEAM SET - this game's player faction and its teammates' factions (swteamally::CellsWrite), written by
   the main thread at every write of the team cells (policy.cpp PolicyTeammateFactions, from team.cpp), read by any thread as
   plain pointer compares; never followed. g_teamHeld 0 = this game's player is in no team: one load and the detours go on as
   before. teamAlly / teamNotEnemy count the answers given for a team pair; teamDoubt a faction that would not read (the
   engine's own answer stands). */
void* volatile g_teamCells[swteamally::kCells];
volatile LONG g_teamHeld = 0;
volatile LONG64 g_teamAlly = 0, g_teamNotEnemy = 0, g_teamDoubt = 0, g_teamWrites = 0;

/* The owner faction of a character through its own vtable slot +0x58 - the call isAllyOf / isEnemyOf make first. 1 read, 0 a
   fault. AI-THREAD SAFE: no allocation, no string, no lock. */
int OwnerFactionPod(void* obj, void** f)
{
    *f = 0;
    __try
    {
        if (((uintptr_t)obj & 7) != 0) return 0;
        void** vtbl = *(void***)obj;
        if (!PlausiblePtrPod(vtbl)) return 0;
        GetFactionFn gf = (GetFactionFn)vtbl[kGetFactionSlot];
        if (!PlausiblePtrPod((const void*)gf)) return 0;
        *f = gf(obj);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* 1 = self and who belong to two different factions both in this player's team (the engine's same-faction exit is answered for
   them), 0 = they do not (or no team), -1 = a faction would not read. ANY THREAD. */
int TeamPairPod(void* self, void* who)
{
    if (g_teamHeld == 0 || self == 0 || who == 0) return 0;
    void* fa = 0; void* fb = 0;
    if (OwnerFactionPod(self, &fa) == 0 || OwnerFactionPod(who, &fb) == 0) return -1;
    return swteamally::TeamPair(fa, fb, g_teamCells, swteamally::kCells) ? 1 : 0;
}

bool g_installed = false;
int  g_enemyHook = -1, g_allyHook = -1;     /* -1 install not reached, 0 NOT installed, 1 prologue verified and patched */
long long g_onEdges = 0, g_offEdges = 0, g_walks = 0, g_walkNoWorld = 0;

template <class T> std::string S(const T& v) { std::ostringstream o; o << v; return o.str(); }
std::string F1(float v)
{
    std::ostringstream o; o.imbue(std::locale::classic());
    o.setf(std::ios::fixed); o.precision(1); o << v; return o.str();
}
bool PlausiblePtr(const void* p) { return p != 0 && (uintptr_t)p > 0x10000 && (uintptr_t)p < 0x00007FFFFFFFFFFFull; }
uintptr_t Base() { return (uintptr_t)::GetModuleHandleA(0); }

/* Returns 1 if the n bytes were read, 0 if the read itself raised: a failed READ must never be able to
   masquerade as an unchanged prologue (the E30-3 lesson, store.cpp ReadPrologue). */
int ReadBytesPod(const void* p, unsigned char* out, unsigned n)
{
    __try { std::memcpy(out, p, n); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* 1 = this object's faction is a PLAYER faction (the engine's own class bit at Faction+0x250, or this game's
   stand-in for the other player), 0 = it definitely is not, -1 = a read gave doubt.
   The faction is read through the object's OWN vtable slot +0x58 - the same call the original already makes -
   so the detour dereferences nothing the callee would not. AI-THREAD SAFE: no allocation, no string, no lock. */
int PlayerFactionSidePod(void* obj)
{
    void* f = 0;
    __try
    {
        if (obj == 0) return 0;                       /* the engine's own first statement is: if (!who) return false */
        if (((uintptr_t)obj & 7) != 0) return -1;
        void** vtbl = *(void***)obj;
        if (!PlausiblePtr(vtbl)) return -1;
        GetFactionFn gf = (GetFactionFn)vtbl[kGetFactionSlot];
        if (!PlausiblePtr((const void*)gf)) return -1;
        f = gf(obj);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    if (f == 0) return 0;
    if (!PlausiblePtr(f)) return -1;
    int isPlayer = 0;
    __try { isPlayer = (*(void**)((char*)f + 0x250) != 0) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    if (isPlayer) return 1;
    if (coop::IsStandInFaction((::Faction*)f)) return 1;   /* stand1 fold (2b): a save's coop-peer too */    /* stand1: ANY player's stand-in faction (coop-p<n>), made by playerfaction.cpp */
    return 0;
}

/* ONE predicate, asked by BOTH detours, so the two cannot be kept in step by hand and drift apart (lesson 11).
   1 = the lever's gate is met, 0 = it is not, -1 = doubt (fail open to the engine's own answer). */
int PeaceGate(void* self, void* who)
{
    const int a = PlayerFactionSidePod(self);
    if (a == 1) return 1;
    const int b = PlayerFactionSidePod(who);
    if (b == 1) return 1;
    if (a < 0 || b < 0) return -1;
    return 0;
}

char detour_isEnemy(void* self, void* who, char disguises)
{
    /* THE PROBE'S OWN CALL COMES THROUGH HERE TOO (orig_isEnemy can ask the enemy question of other objects),
       and while the bypass is up this detour is a pass-through as well - otherwise the probe would measure an
       engine we are still modifying, which is the very thing it exists to escape. */
    if (PeaceHooksBypassedHere() != 0) return orig_isEnemy(self, who, disguises);
    if (g_teamHeld != 0)
    {
        /* T-546 (owner 512): a team pair is answered as the engine answers one faction - not an enemy, before anything else */
        const int team = TeamPairPod(self, who);
        if (team == 1) { ::InterlockedIncrement64(&g_teamNotEnemy); return 0; }
        if (team < 0) ::InterlockedIncrement64(&g_teamDoubt);
    }
    {
        const char engine = orig_isEnemy(self, who, disguises);   /* ask the ENGINE first: it does all the null and
                                                                     plausibility work, and how many answers the lever
                                                                     actually changed then falls out for free */
        if (g_peaceOn == 0) return engine;
        ::InterlockedIncrement64(&g_queriesAnswered);
        ::InterlockedIncrement64(&g_enemyCalls);
        {
            const int gate = PeaceGate(self, who);
            if (gate < 0) { ::InterlockedIncrement64(&g_fallthrough); return engine; }
            if (gate == 0) { ::InterlockedIncrement64(&g_neitherSidePlayer); return engine; }
            if (engine != 0)
            {
                ::InterlockedIncrement64(&g_hostileSuppressed);
                /* THE ENGINE STILL SAID ENEMY WITH OUR ALLY HOOK ACTIVE, so vanilla said enemy too - forcing
                   an ally answer TRUE can only make the engine less hostile, never more. No probe needed. */
                ::InterlockedIncrement64(&g_hostileSuppressedVsVanilla);
            }
            else if (g_tlsBypass != kTlsUnset && g_vanillaProbeCalls < kVanillaProbeCap)
            {
                /* THE AMBIGUOUS CASE, AND THE ONLY ONE THE PROBE PAYS FOR (F618 / review-p7x H-2): in scope,
                   and the engine already answered "not an enemy". That reads as "the world was already
                   peaceful" and it can equally mean "the ally hook flipped it upstream" - isEnemyOf asks
                   the ally question twice through iShouldntAggravateThisTarget 0x855AD0. Ask again with both
                   hooks off on this thread and the two become separable. */
                ::TlsSetValue(g_tlsBypass, (LPVOID)1);
                {
                    const char vanilla = orig_isEnemy(self, who, disguises);
                    ::TlsSetValue(g_tlsBypass, (LPVOID)0);
                    ::InterlockedIncrement64(&g_vanillaProbeCalls);
                    if (g_vanillaProbeCalls >= kVanillaProbeCap) ::InterlockedExchange(&g_vanillaProbeCapped, 1);
                    if (vanilla != 0) ::InterlockedIncrement64(&g_hostileSuppressedVsVanilla);
                }
            }
            return 0;                                              /* not an enemy */
        }
    }
}

char detour_isAlly(void* self, void* who, char disguises)
{
    /* THE RETURN ADDRESS IS TAKEN FIRST, before anything else can disturb the frame. */
    const void* const ret = _ReturnAddress();
    if (PeaceHooksBypassedHere() != 0) return orig_isAlly(self, who, disguises);
    if (g_teamHeld != 0)
    {
        /* T-546 (owner 512): a team pair is answered as the engine answers one faction - an ally, before anything else, for
           every caller (the fight pickers, rememberCharacter's mark, medics, carrying and the rest alike) */
        const int team = TeamPairPod(self, who);
        if (team == 1) { ::InterlockedIncrement64(&g_teamAlly); return 1; }
        if (team < 0) ::InterlockedIncrement64(&g_teamDoubt);
    }
    {
        const char engine = orig_isAlly(self, who, disguises);
        if (g_peaceOn == 0) return engine;
        ::InterlockedIncrement64(&g_queriesAnswered);
        ::InterlockedIncrement64(&g_allyCalls);
        /* F619: ONLY THE TWO FIGHT-PICKING CALL SITES GET THE LEVER'S ANSWER. Everything else - medics, body
           carrying, looting, trespass, turrets, stealth, squad bookkeeping, the map's marker colours - gets
           the engine's own, which is what stops the lever walking a run's characters off across the map. */
        if (PeaceAllyRetAnswered(ret) == 0) { ::InterlockedIncrement64(&g_allyPassedThrough); return engine; }
        ::InterlockedIncrement64(&g_allyGatedByRet);
        {
            const int gate = PeaceGate(self, who);
            if (gate < 0) { ::InterlockedIncrement64(&g_fallthrough); return engine; }
            if (gate == 0) { ::InterlockedIncrement64(&g_neitherSidePlayer); return engine; }
            if (engine == 0) ::InterlockedIncrement64(&g_allyForced);
            return 1;                                              /* an ally */
        }
    }
}

/* ---- the ON-edge evidence walk. MAIN THREAD ONLY (it is reached from the command channel tick). --------- */

int ReadFactionArrayPod(void*** arr, unsigned* n)
{
    __try { *arr = *(void***)((char*)coop::GameWorldPtr()->factionDirectory + 0x10); *n = *(unsigned*)((char*)coop::GameWorldPtr()->factionDirectory + 8); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void* RelationsOfPod(::Faction* f)
{
    __try { return f->relations; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* 1 = read, 0 = no entry, -1 = the read raised. The same call and the same field offsets relations.cpp uses, so
   the numbers printed here are the numbers relations.cpp forwards. */
int ReadRelationPod(void* rel, ::Faction* other, float* relation, unsigned* flags)
{
    __try
    {
        GetRelDataFn get = (GetRelDataFn)(Base() + kGetRelDataRva);
        char* d = (char*)get(rel, other);
        if (!PlausiblePtr(d)) return 0;
        *relation = *(float*)(d + 4);
        *flags = (d[0] ? 1u : 0u) | (d[2] ? 2u : 0u);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* WHAT THIS WALK MEASURES, and it must not be read as anything else: these are the values the lever is MASKING.
   The lever writes nothing, so a second reading after it is turned on is identical BY CONSTRUCTION. It is not a
   before/after diff and it is not a change (the six-recurrence lesson: a number that reads as one thing and
   reports another is worse than no number). */
void PeaceWalk()
{
    ++g_walks;
    ::Faction* mine = coop::LocalPlayerFaction();
    if (!PlausiblePtr(mine) || !PlausiblePtr(coop::GameWorldPtr()) || !PlausiblePtr(coop::GameWorldPtr()->factionDirectory))
    {
        ++g_walkNoWorld;
        DebugLog("[PEACE] peace on: 0 factions, 0 of them hostile toward the player faction (values masked, not written)"
                 " - THE WALK DID NOT RUN: there is no world or no local player faction yet, so the two zeros are"
                 " not-measured and not nothing-is-hostile. The lever itself IS on; only this reading is missing.");
        return;
    }
    void** arr = 0; unsigned n = 0;
    if (!ReadFactionArrayPod(&arr, &n) || !PlausiblePtr(arr) || n > 4096)
    {
        ++g_walkNoWorld;
        DebugLog("[PEACE] peace on: 0 factions, 0 of them hostile toward the player faction (values masked, not written)"
                 " - THE WALK DID NOT RUN: the faction array at factionDirectory+0x10 (count +0x08) did not read as a"
                 " plausible array. The lever itself IS on; only this reading is missing.");
        return;
    }
    unsigned hostile = 0, unread = 0, pairsRead = 0;
    std::vector<std::string> lines;
    for (unsigned i = 0; i < n; ++i)
    {
        ::Faction* f = (::Faction*)arr[i];
        if (!PlausiblePtr(f) || f == mine) continue;
        void* rm = RelationsOfPod(mine);
        void* rf = RelationsOfPod(f);
        float m2t = 0.0f, t2m = 0.0f; unsigned fm = 0, ff = 0;
        /* F617: EACH OF THESE TWO CALLS CAN CREATE A ROW - see the disclosure below. Counted so the upper
           bound the walk touched is a number on the line rather than arithmetic in a finding. */
        const int a = PlausiblePtr(rm) ? ReadRelationPod(rm, f, &m2t, &fm) : 0;
        if (PlausiblePtr(rm)) ++pairsRead;
        const int b = PlausiblePtr(rf) ? ReadRelationPod(rf, mine, &t2m, &ff) : 0;
        if (PlausiblePtr(rf)) ++pairsRead;
        if (a != 1 || b != 1) { ++unread; continue; }
        const bool atWar = ((ff & 2u) != 0);
        if (!(t2m <= kEnemyRelation || atWar)) continue;
        ++hostile;
        if (lines.size() < 60)
            lines.push_back("[PEACE]   " + f->getName() + "  mine->them=" + F1(m2t) + " them->mine=" + F1(t2m)
                            + (atWar ? " atWar" : "") + "  HOSTILE");
    }
    DebugLog("[PEACE] peace on: " + S(n) + " factions, " + S(hostile)
             + " of them hostile toward the player faction (values masked, not written)");
    DebugLog("[PEACE]   hostile = the ENGINE's own test, them->mine <= " + F1(kEnemyRelation)
             + " or the entry's atWar flag (FactionRelations::_isEnemy 0x6B2280, threshold .rdata 0x16CBCFC)."
               " These are the values the lever MASKS at read time. Nothing was SENT and nothing reached the"
               " save, and a second reading after the lever is on is identical by construction."
               " WHAT THIS WALK DOES WRITE, stated here because this line used to deny it (F617 /"
               " review-p7x H-1): it reads every pair through the engine's own"
               " FactionRelations::getRelationData 0x6B4910, and that call is NOT an accessor - on a MISS"
               " it INSERTS an entry initialised from the faction's own defaultRelation (+0x60) and returns"
               " that. So up to relationPairsRead entries can be created in the two FactionRelations maps by"
               " this walk alone. The value is the one the engine would have used anyway - an absent entry"
               " already means 'use the default' - and getRelationData is not one of relations.cpp's"
               " seven hooked setters, so nothing is forwarded and nothing goes on the wire. In a linked run"
               " relations.cpp's own Snapshot has already made the same reads; IN A -Solo RUN, OR WITH"
               " relations off, THIS WALK IS THE FIRST WRITER."
               " relationPairsRead=" + S((long long)pairsRead)
             + " unreadableEntries=" + S(unread) + " listed=" + S((long long)lines.size()) + " of " + S(hostile));
    for (size_t i = 0; i < lines.size(); ++i) DebugLog(lines[i]);
}

void InstallHooks()
{
    const uintptr_t base = Base();
    g_moduleBase = base;                       /* F619: the allowlist is compared as an RVA, on the AI thread */
    g_tlsBypass = ::TlsAlloc();                /* F618: the probe's bypass. TLS_OUT_OF_INDEXES leaves it unset and the probe is never taken */
    if (g_tlsBypass == kTlsUnset)
        ErrorLog("[PEACE] TlsAlloc failed: peaceHostileSuppressedVsVanilla cannot be measured in this process"
                 " and will read 0 for a reason that is not 'nothing was flipped'. Read peaceVanillaProbeCalls"
                 " beside it - a 0 THERE says the probe never ran.");
    /* The prologue bytes THIS patch was written against, read out of build/bin/kenshi_x64.exe on 2026-09-05
       (that file is byte-identical to the running exe, so these are the bytes the game presents):
         0x79BDB0  48 89 74 24 18 48 89 7C 24 20 41 54 48 83 EC 20  mov [rsp+18],rsi / mov [rsp+20],rdi / push r12 / sub rsp,20
         0x791830  48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20     mov [rsp+10],rbp / mov [rsp+18],rsi / push rdi / sub rsp,20
       Both are ordinary non-relative frame setups of more than five bytes, so MinHook can relocate them.
       A function whose prologue does not verify is LEFT ALONE and its absence is STATED: an unhooked lever is a
       run that still fights, which is visible; a hook placed on the wrong bytes is arbitrary code (F020/F038).
       Installed / not-installed is reported from the bytes READ BACK, never from what AddHook returned - the
       E30-3 case where AddHook said SUCCESS and patched nothing. */
    const unsigned char sigEnemy[10] = { 0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20 };
    const unsigned char sigAlly[10]  = { 0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18 };
    unsigned char pre[16], post[16];
    const uintptr_t aEnemy = base + kIsEnemyRva, aAlly = base + kIsAllyRva;

    if (ReadBytesPod((const void*)aEnemy, pre, 16) == 0 || std::memcmp(pre, sigEnemy, 10) != 0)
    {
        g_enemyHook = 0;
        ErrorLog("[PEACE] isEnemyOf hook=NOT-INSTALLED at rva 0x79BDB0: the prologue is not the"
                 " 48 89 74 24 18 48 89 7C 24 20 this build was read against, so that address is not"
                 " Character::isEnemyOf here. The lever will NOT stop anyone treating the players as enemies.");
    }
    else
    {
        coop::AddHook((void*)aEnemy, (void*)&detour_isEnemy, (void**)&orig_isEnemy);
        g_enemyHook = (ReadBytesPod((const void*)aEnemy, post, 16) != 0 && std::memcmp(pre, post, 8) != 0) ? 1 : 0;
    }
    if (ReadBytesPod((const void*)aAlly, pre, 16) == 0 || std::memcmp(pre, sigAlly, 10) != 0)
    {
        g_allyHook = 0;
        ErrorLog("[PEACE] isAllyOf hook=NOT-INSTALLED at rva 0x791830: the prologue is not the"
                 " 48 89 6C 24 10 48 89 74 24 18 this build was read against. Two combat consumers pick by"
                 " NOT-ALLY rather than by IS-ENEMY, so with this hook missing the lever is INCOMPLETE even if"
                 " the treatsAsEnemy hook armed.");
    }
    else
    {
        coop::AddHook((void*)aAlly, (void*)&detour_isAlly, (void**)&orig_isAlly);
        g_allyHook = (ReadBytesPod((const void*)aAlly, post, 16) != 0 && std::memcmp(pre, post, 8) != 0) ? 1 : 0;
    }
    /* P8a (F639): verify the two call sites before anything claims the allowlist is armed. */
    {
        unsigned int i = 0; int okSites = 0;
        std::string bad;
        for (i = 0; i < kAllyRetCount; ++i)
        {
            unsigned char six[6];
            const uintptr_t site = base + (uintptr_t)kAllyRets[i] - 6;
            if (kAllyRets[i] > 6 && ReadBytesPod((const void*)site, six, 6) != 0 && std::memcmp(six, kAllyCallBytes, 6) == 0) ++okSites;
            else bad += (bad.empty() ? "" : ",") + S((long long)kAllyRets[i]);
        }
        g_allyRetVerified = okSites;
        if (okSites != (int)kAllyRetCount)
        {
            ::InterlockedExchange(&g_peaceAllowlistMissing, 1);
            ErrorLog("[PEACE] allyRetSites=" + S((long long)okSites) + "/" + S((long long)kAllyRetCount)
                     + " VERIFIED - the six bytes FF 90 F0 03 00 00 are NOT at every allowlisted return address"
                       " minus six (failed return rvas: " + bad + "). The two melee finders pick a fight target by"
                       " NOT-ALLY, so with the allowlist unmatched every ally call falls through to the engine's own"
                       " answer and THE COMBAT HALF OF THE PEACE LEVER IS OFF - allyForced will read 0 for the rest"
                       " of this process while every 'peace on' line still prints. Logged once, at install.");
        }
    }
    DebugLog("[PEACE] allyReturnAllowlist: allyRetSites=" + S((long long)g_allyRetVerified) + "/" + S((long long)kAllyRetCount)
             + " verified. The isAlly answer is forced ONLY at "
             + S((long long)kAllyRetCount) + " return addresses -"
             " 0x608ED3 (CombatClass::getNearestEnemyInAttackZone 0x608DA0) and"
             " 0x59F112 (AI::findMeleeOpponent 0x59EFE0), the two consumers that pick a fight target by"
             " NOT-ALLY. Every other isAllyOf caller - medics, body carrying, looting, trespass, turrets,"
             " stealth scoring, squad bookkeeping and the map's marker colours - gets the ENGINE's own answer"
             " and is counted as allyPassedThrough (F619). Widening this list walks a run's characters away"
             " from where it parked them, so it is a decision and not a default.");
    DebugLog("[PEACE] hooks: isEnemyOf 0x79BDB0=" + S((long long)g_enemyHook)
             + " isAllyOf 0x791830=" + S((long long)g_allyHook)
             + " (1 = the prologue matched the bytes this patch was read against AND the bytes changed after"
               " AddHook; 0 = NOT installed). The lever is OFF by default and both detours pass every call"
               " straight to the engine until the peace verb turns it on.");
}
}   // namespace

namespace coop {

void PeaceTeamFactions(void* own, void* const* mates, int n)
{
    const int held = swteamally::CellsWrite(g_teamCells, swteamally::kCells, own, mates, n);
    ::InterlockedExchange(&g_teamHeld, (LONG)held);
    ::InterlockedIncrement64(&g_teamWrites);
}

bool PeaceTeamPairAnyThread(const void* a, const void* b)
{
    if (g_teamHeld == 0) return false;
    return swteamally::TeamPair(a, b, g_teamCells, swteamally::kCells);
}

int PeaceTeamPairOfCharacters(void* a, void* b)
{
    return TeamPairPod(a, b);
}

std::string PeaceTeamTokens()
{
    return " team[held,ally,notEnemy,doubt,writes]=" + S((long long)g_teamHeld) + "," + S((long long)g_teamAlly) + "," + S((long long)g_teamNotEnemy)
         + "," + S((long long)g_teamDoubt) + "," + S((long long)g_teamWrites);
}

void InstallPeace()
{
    if (g_installed) return;
    g_installed = true;
    InstallHooks();
}

void SetPeaceOn(bool on)
{
    const bool was = (g_peaceOn != 0);
    ::InterlockedExchange(&g_peaceOn, on ? 1 : 0);
    if (on)
    {
        if (!was) ++g_onEdges;
        if (g_enemyHook != 1 || g_allyHook != 1)
            ErrorLog("[PEACE] the peace verb was accepted but a hook is NOT armed (treatsAsEnemy=" + S((long long)g_enemyHook)
                     + " isAlly=" + S((long long)g_allyHook) + "): the flag is set and it will change nothing"
                     " through the missing hook. Read the [PEACE] hooks line at start-up.");
        PeaceWalk();
    }
    else
    {
        if (was) ++g_offEdges;
        DebugLog("[PEACE] peace off - the engine answers its own is-enemy and is-ally questions again."
                 " Nothing is undone because nothing was written; the closing counters follow.");
        ReportPeace();
    }
}

void ReportPeace()
{
    DebugLog("[PEACE] REPORT peace[" + S((long long)(g_peaceOn != 0 ? 1 : 0)) + "," + S((long long)g_queriesAnswered)
             + "," + S((long long)g_hostileSuppressed) + "," + S((long long)g_allyForced)
             + "," + S((long long)g_fallthrough) + "]"
             + " enemyCalls=" + S((long long)g_enemyCalls) + " allyCalls=" + S((long long)g_allyCalls)
             + " neitherSidePlayer=" + S((long long)g_neitherSidePlayer)
             + " allyRetSites=" + S((long long)g_allyRetVerified) + "/" + S((long long)kAllyRetCount)
             + " peaceAllowlistMissing=" + S((long long)g_peaceAllowlistMissing)
             + " allyGatedByRet=" + S((long long)g_allyGatedByRet)
             + " allyPassedThrough=" + S((long long)g_allyPassedThrough)
             + " hostileSuppressedVsVanilla=" + S((long long)g_hostileSuppressedVsVanilla)
             + " vanillaProbeCalls=" + S((long long)g_vanillaProbeCalls)
             + " vanillaProbeCapped=" + S((long long)g_vanillaProbeCapped)
             + " hooks(treatsAsEnemy,isAlly)=" + S((long long)g_enemyHook) + "," + S((long long)g_allyHook)
             + " onEdges=" + S(g_onEdges) + " offEdges=" + S(g_offEdges) + " walks=" + S(g_walks)
             + " walksWithNoWorld=" + S(g_walkNoWorld)
             + PeaceTeamTokens()
             + "  |  team[...]: this game's player faction and its teammates' factions are answered ally / not-enemy for each other"
               " before the engine is asked, as it answers one faction (T-546, owner 512); held = factions in the team set (0 = no team)."
             + "  |  peace[on,queriesAnswered,hostileSuppressed,allyForced,fallthrough]."
               " queriesAnswered counts EVERY isEnemyOf/isAllyOf call made while the lever was on - including"
               " the ones it left to the engine (neitherSidePlayer + fallthrough) - and NOT the ones whose answer"
               " it changed. hostileSuppressed and allyForced are the answers it actually FLIPPED."
               " fallthrough is doubt: a faction pointer would not read, so the engine's own answer stood."
               " allyGatedByRet + allyPassedThrough = allyCalls: the ally answer is forced ONLY at the two"
               " fight-picking return addresses (F619) and passedThrough is every other caller, which now"
               " gets the engine's own answer - a LARGE passedThrough is the design working, not a miss."
               " P8a (F639): BUT gatedByRet = 0 WITH allyCalls > 0 IS THE ALLOWLIST MISSING, not the design -"
               " read allyRetSites, which is how many of the two call sites still carry FF 90 F0 03 00 00 six bytes"
               " before the allowlisted return address. peaceAllowlistMissing=1 means the COMBAT HALF OF THIS LEVER IS"
               " OFF: the two melee finders pick by NOT-ALLY, so they go back to choosing player characters, and"
               " every other line in this file will still say the lever is on."
               " hostileSuppressedVsVanilla is hostileSuppressed measured against an engine with BOTH hooks"
               " bypassed on the calling thread, which is the number hostileSuppressed cannot give on its own:"
               " isEnemyOf asks the ally question twice through iShouldntAggravateThisTarget 0x855AD0, so a"
               " hostileSuppressed of 0 could always have meant 'the ally hook did it upstream' (F618)."
               " It costs a SECOND orig_isEnemy call, taken only while the lever is on, only where the gate"
               " passed and the engine already said no, and only for the first " + S((long long)kVanillaProbeCap)
             + " such calls - vanillaProbeCalls is that cost and vanillaProbeCapped=1 means the counter covers"
               " only those first calls. The lever writes NO relation value, sends nothing and touches no"
               " save; the peace-on evidence walk DOES create default-valued relation rows as a side effect of"
               " reading them, which its own line states.");
}
}
