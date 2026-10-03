// stats.cpp - S1 (read-stats, T260): a copy carries its owner's 44 saved stat values. See stats.h.
//
// WHERE THE VALUES LIVE (read-stats, Read): Character +0x450 is a CharStats* (`stats`,
// getStats 0xDEE40). The 44 offsets are coopstats::StatsOffset (src/common/statswire.h).
//
// THE RECALCULATION (verified offline, S1): CharStats::updateStats 0x64AC10 - the engine's load path - ends
//     0x64C03C  MOV RCX,[RDI+0x8] ; MOV RAX,[RCX] ; CALL [RAX+0x8]     the medical object's slot +8
//     0x64C05D  MOV R11,[RDI]     ; MOV RCX,RDI  ; CALL [R11+0x20]    CharStats slot +0x20
// with no other argument register set for either. Ghidra DumpVtable: CharStats vftable RVA 0x17232E8, slot
// +0x20 = thunk 0xAB28 -> 0x8857E0 (_recalculateStats); MedicalSystem vftable RVA 0x16F8BE8, slot +8 = thunk
// 0x4011F -> 0x644840 (MedicalSystem::updateStats).
//
// ANIMALS (S1-b, review-s1 MEDIUM): an animal's stats object carries a SECOND vftable, RVA 0x16F8D38 (slot 0
// = the animal init 0x64D1B0, +8 = the same periodicUpdate 0x882CA0). Its slot +0x20 = thunk 0x2FAC7 ->
// 0x885E10, an override that stores virtual +0x398 of the object at CharStats +0x10 into CharStats +0x34 and
// then calls 0x8857E0 itself - a `this`-only call, asking the same virtual 0x8857E0 already asks. So it is
// called the same way, through the object's own slot +0x20, and counted statsRecalcAnimal.
//
// All three vftables are address-table entries (CharStats_vftable, CharStatsAnimal_vftable,
// MedicalSystem_vftable) and an object's vftable pointer must EQUAL one of them before its slot is called;
// otherwise the fields are written only and counted statsRecalcSkipped.
//
// THE WRITE-ONLY FALLBACK IS NOT EQUIVALENT (S1-b, review-s1 MEDIUM). periodicUpdate 0x882CA0 recalculates
// only while the medical object's +0x161 and +0x164 are both 0, and even then it does not carry the top run
// speed (CharStats +0x17C) on to movement: only MedicalSystem::updateStats 0x644840 and 0x645dd0 copy it to
// AnimationClass +0x19C, which is where movement reads it. A copy left on the fallback keeps its old run
// speed until the engine itself runs one of those two.
//
// ORDER: _recalculateStats FIRST (it computes the top run speed into CharStats +0x17C through
// calculateMaxRunSpeed 0x885590), THEN MedicalSystem::updateStats (which pushes +0x17C on to AnimationClass
// +0x19C). The load path runs them the other way round.
//
// C++03 (VS2010 v100). Every __try lives in a function with no C++ object that has a destructor (C2712).

#include "stats.h"
#include "game/Character.h"        /* T-189 runboost: animation, stats, movement */
#include "game/CharMovement.h"
#include "game/AnimationClass.h"   /* applySpeedCap, movementLimits.speedCap */
#include "game/CharStats.h"        /* runSpeed */
#include "spawn.h"          /* FindSpawned */
#include "store.h"          /* EngineWritesBlocked */
#include "worldsync.h"      /* AnnouncedToPeer (H030) */
#include "addresses.h"      /* the two vftables come from the address table */
#include "net/session.h"    /* IsUidMine, OwnedUidsSnapshot, SendStats */
#include "../common/statswire.h"
#include "../common/copykeep.h"   /* copy1: BloodRestoreWanted, BloodPushDelta */
#include "coop_log.h"
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace coop {
namespace {

unsigned long long kCharStatsVftRva = 0; static coop::AddrReg kCharStatsVftRva_reg("CharStats_vftable", &kCharStatsVftRva);   /* Steam_1.0.65 0x17232E8: slot +0x20 -> _recalculateStats 0x8857E0 */
unsigned long long kCharStatsAnimalVftRva = 0; static coop::AddrReg kCharStatsAnimalVftRva_reg("CharStatsAnimal_vftable", &kCharStatsAnimalVftRva);   /* Steam_1.0.65 0x16F8D38: slot +0x20 -> 0x885E10 (sets +0x34, then _recalculateStats 0x8857E0) */
unsigned long long kMedicalVftRva = 0; static coop::AddrReg kMedicalVftRva_reg("MedicalSystem_vftable", &kMedicalVftRva);   /* Steam_1.0.65 0x16F8BE8: slot +8 -> MedicalSystem::updateStats 0x644840 */

const size_t kCharStatsPtr   = 0x450;   /* Character::stats (CharStats*) */
const size_t kStatsMedical   = 0x8;     /* CharStats -> the medical object updateStats 0x64AC10 calls slot +8 on */
const size_t kSlotRecalc     = 0x20;    /* CharStats vftable: _recalculateStats */
const size_t kSlotMedUpdate  = 0x8;     /* MedicalSystem vftable: updateStats */
const size_t kMedBlood       = 0x70;    /* copy1: MedicalSystem::blood (spawn.cpp ApplyHealth writes the same field) */

const int   kStatsMaxPerTick  = 16;     /* N: at most this many MSG_STATS per StatsTick */
const int   kStatsScanPerTick = 128;    /* owned characters read per StatsTick (round-robin) */
const DWORD kStatsResendMs    = 30000;  /* a full resend per character, even when nothing changed */
const long long kStatsLogFirst = 8;     /* the first applies are logged with athletics before -> after */

volatile LONG64 g_statsSent = 0;          /* MSG_STATS put on the wire (owner) */
volatile LONG64 g_statsSpawnCarried = 0;  /* SPAWNs that carried a block (owner) */
volatile LONG64 g_statsApplied = 0;       /* blocks written into a copy (SPAWN or MSG_STATS) */
volatile LONG64 g_statsNoBlock = 0;       /* inbound SPAWN without a block */
volatile LONG64 g_statsBadBlock = 0;      /* inbound block that did not decode */
volatile LONG64 g_statsRecalcCalled = 0;  /* _recalculateStats called through slot +0x20 */
volatile LONG64 g_statsMedCalled = 0;     /* MedicalSystem::updateStats called through slot +8 */
volatile LONG64 g_statsRecalcAnimal = 0;  /* of statsRecalcCalled: through the animal vftable's slot +0x20 (0x885E10) */
volatile LONG64 g_statsMedFault = 0;      /* the medical push faulted AFTER the recalc ran (still counted statsRecalcCalled) */
volatile LONG64 g_statsRecalcSkipped = 0; /* fields written only, NOT recalculated and run speed not pushed (not equivalent - see the header): vftable neither expected one / no table entry */
volatile LONG64 g_statsMineRefused = 0;   /* a block named a character this game drives - never applied */
volatile LONG64 g_statsNoChar = 0;        /* no local copy for the uid */
volatile LONG64 g_statsBlocked = 0;       /* engine writes blocked (a load) - dropped, the 30 s resend repairs it */
volatile LONG64 g_statsTickBlocked = 0;   /* owner: StatsTick skipped whole while EngineWritesBlocked() (load / teardown) */
volatile LONG64 g_statsFault = 0;         /* a guarded read / write / call faulted */
volatile LONG64 g_statsUnreadable = 0;    /* owner: its own character's block unreadable or non-finite */
volatile LONG64 g_statsBloodKept = 0;     /* copy1 (COPY1): a copy's blood written back after MedicalSystem::updateStats 0x644840, which sets it to max blood */
volatile LONG64 g_statsBloodKeptMoved = 0; /* copy1: of statsBloodKept, the push HAD moved blood (by more than 0.01) - the refill that was undone */
float g_statsBloodKeptDeltaMax = 0.0f;    /* copy1 (MAIN THREAD, ApplyRemoteStats): the largest |blood after the push - blood before| undone */
const long long kBloodLogFirst = 8;       /* copy1: the first undone refills are logged before -> push -> restored */

struct StatsLast
{
    unsigned int raw[coopstats::kStatsCount];
    DWORD sentMs;
};
std::map<unsigned int, StatsLast> g_statsLast;   /* MAIN THREAD: what the owner last sent, per uid */
std::vector<unsigned int> g_statsScan;           /* MAIN THREAD: this pass's owned uids */
size_t g_statsScanPos = 0;

bool PlausibleHeap(const void* p)
{
    const uintptr_t v = (uintptr_t)p;
    return v >= 0x10000 && v < 0x00007FFFFFFFFFFFULL && (v & 7) == 0;
}

/* The CharStats* of a character, or 0. */
static char* StatsObjPod(const void* c)
{
    __try { return *(char* const*)((const char*)c + kCharStatsPtr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static int StatsReadPod(const char* s, unsigned int* raw)
{
    __try
    {
        int i;
        for (i = 0; i < coopstats::kStatsCount; ++i)
            raw[i] = *(const unsigned int*)(s + coopstats::StatsOffset(i));
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static int StatsWritePod(char* s, const unsigned int* raw, float* athBefore, float* athAfter)
{
    __try
    {
        const size_t ath = coopstats::StatsOffset(coopstats::kStatsAthleticsIndex);
        *athBefore = *(const float*)(s + ath);
        int i;
        for (i = 0; i < coopstats::kStatsCount; ++i)
            *(unsigned int*)(s + coopstats::StatsOffset(i)) = raw[i];
        *athAfter = *(const float*)(s + ath);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

typedef void (*ThisOnlyFn)(void* self);

/* 1 = both calls made, 2 = recalc made and the medical object was not the expected one (not called),
   0 = the stats vftable was neither expected one (nothing called), -1 = faulted before the recalc returned,
   -2 = faulted in the medical push AFTER the recalc ran. *animal = 1 when the object carried the animal
   vftable (animalVft 0 = no table entry, never matched).
   copy1 (COPY1): with keepBlood (a COPY only), the medical object's blood is read straight before the push and written
   back straight after it - 0x644840 sets it to max blood (chaos-levers-and-brains.md, Read, decomp); *bloodKept = 1 when
   it was written back. That reading names blood only (plus the run-speed push), so limb health is not saved. */
static int StatsRecalcPod(char* s, uintptr_t statsVft, uintptr_t animalVft, uintptr_t medVft, int* animal,
                          int keepBlood, float* bloodBefore, float* bloodAfterPush, int* bloodKept)
{
    volatile int recalcRan = 0;
    __try
    {
        const uintptr_t vft = *(const uintptr_t*)s;
        if (vft != statsVft && (animalVft == 0 || vft != animalVft)) return 0;
        *animal = (vft == statsVft) ? 0 : 1;
        ThisOnlyFn recalc = *(ThisOnlyFn*)(vft + kSlotRecalc);
        recalc(s);
        recalcRan = 1;
        char* med = *(char**)(s + kStatsMedical);
        if (!PlausibleHeap(med) || *(const uintptr_t*)med != medVft) return 2;
        ThisOnlyFn medUpdate = *(ThisOnlyFn*)(medVft + kSlotMedUpdate);
        float* blood = (float*)(med + kMedBlood);
        const float bloodWas = *blood;
        *bloodBefore = bloodWas;
        medUpdate(med);
        *bloodAfterPush = *blood;
        if (coopkeep::BloodRestoreWanted(keepBlood, 1, bloodWas) != 0) { *blood = bloodWas; *bloodKept = 1; }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return recalcRan ? -2 : -1; }
}

std::string L(long long v) { std::ostringstream o; o << v; return o.str(); }
std::string F(float v) { std::ostringstream o; o.imbue(std::locale::classic()); o << v; return o.str(); }

void Remember(unsigned int uid, const unsigned int* raw, DWORD now)
{
    StatsLast& e = g_statsLast[uid];
    std::memcpy(e.raw, raw, coopstats::kStatsBlockBytes);
    e.sentMs = now;
}

} // namespace

bool StatsReadOwned(const void* character, unsigned int* raw44)
{
    if (character == 0 || raw44 == 0) return false;
    char* s = StatsObjPod(character);
    if (!PlausibleHeap(s)) { InterlockedIncrement64(&g_statsUnreadable); return false; }
    unsigned int tmp[coopstats::kStatsCount];
    if (!StatsReadPod(s, tmp)) { InterlockedIncrement64(&g_statsFault); return false; }
    if (coopstats::StatsFirstBadFloat(tmp) >= 0) { InterlockedIncrement64(&g_statsUnreadable); return false; }
    std::memcpy(raw44, tmp, coopstats::kStatsBlockBytes);
    return true;
}

void StatsNoteSpawnCarried(unsigned int uid, const unsigned int* raw44)
{
    InterlockedIncrement64(&g_statsSpawnCarried);
    if (raw44 != 0) Remember(uid, raw44, ::GetTickCount());
}

void StatsNoteNoBlock()  { InterlockedIncrement64(&g_statsNoBlock); }
void StatsNoteBadBlock() { InterlockedIncrement64(&g_statsBadBlock); }

bool ApplyRemoteStats(unsigned int uid, const unsigned int* raw44, const char* why)
{
    if (raw44 == 0) return false;
    if (net::IsUidMine(uid)) { InterlockedIncrement64(&g_statsMineRefused); return false; }   // never for a character this game drives
    if (EngineWritesBlocked()) { InterlockedIncrement64(&g_statsBlocked); return false; }
    ::Character* c = FindSpawned(uid);
    if (c == 0) { InterlockedIncrement64(&g_statsNoChar); return false; }
    char* s = StatsObjPod(c);
    if (!PlausibleHeap(s)) { InterlockedIncrement64(&g_statsFault); return false; }

    float before = 0.0f, after = 0.0f;
    if (!StatsWritePod(s, raw44, &before, &after)) { InterlockedIncrement64(&g_statsFault); return false; }
    const long long n = InterlockedIncrement64(&g_statsApplied);

    int rc = 0, animal = 0, bloodKept = 0;
    float bloodBefore = 0.0f, bloodAfterPush = 0.0f;
    if (kCharStatsVftRva != 0 && kMedicalVftRva != 0)
    {
        const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
        rc = StatsRecalcPod(s, base + (uintptr_t)kCharStatsVftRva,
                            kCharStatsAnimalVftRva != 0 ? base + (uintptr_t)kCharStatsAnimalVftRva : 0,
                            base + (uintptr_t)kMedicalVftRva, &animal,
                            net::IsUidMine(uid) ? 0 : 1, &bloodBefore, &bloodAfterPush, &bloodKept);   /* copy1: never on a character this game drives */
    }
    const bool recalcRan = rc == 1 || rc == 2 || rc == -2;   /* S1-b (review-s1 LOW): a medical-push fault does not undo the recalc */
    if (recalcRan) InterlockedIncrement64(&g_statsRecalcCalled);
    if (recalcRan && animal != 0) InterlockedIncrement64(&g_statsRecalcAnimal);
    if (rc == 1)  InterlockedIncrement64(&g_statsMedCalled);
    if (rc == 0)  InterlockedIncrement64(&g_statsRecalcSkipped);
    if (rc == -1) InterlockedIncrement64(&g_statsFault);
    if (rc == -2) InterlockedIncrement64(&g_statsMedFault);
    if (bloodKept != 0)   /* copy1 (COPY1) */
    {
        InterlockedIncrement64(&g_statsBloodKept);
        const float d = coopkeep::BloodPushDelta(bloodBefore, bloodAfterPush);
        if (d > g_statsBloodKeptDeltaMax) g_statsBloodKeptDeltaMax = d;
        if (d > 0.01f)
        {
            const long long m = InterlockedIncrement64(&g_statsBloodKeptMoved);
            if (m <= kBloodLogFirst)
                DebugLog("[S1] copy blood kept #" + L(m) + " uid=" + L((long long)uid) + " blood " + F(bloodBefore)
                         + " -> after the medical push " + F(bloodAfterPush) + " -> restored " + F(bloodBefore));
        }
    }

    if (n <= kStatsLogFirst)
        DebugLog("[S1] stats applied #" + L(n) + " uid=" + L((long long)uid) + " (" + (why ? why : "?") + ")"
                 + " athletics " + F(before) + " -> " + F(after) + (animal != 0 ? " [animal]" : "")
                 + (rc == 1 ? " recalc+medical"
                    : rc == 2 ? " recalc only (medical object not the expected one - run speed not pushed to movement)"
                    : rc == 0 ? " fields only, NOT recalculated (vftable not an expected one / no table entry - run speed stays stale)"
                    : rc == -2 ? " recalc ran, MEDICAL PUSH FAULTED" : " RECALC FAULTED"));
    return true;
}

void StatsTick()
{
    if (EngineWritesBlocked()) { InterlockedIncrement64(&g_statsTickBlocked); return; }   /* S1-b (review-s1 LOW): load / teardown - no character is read */
    if (g_statsScanPos >= g_statsScan.size())
    {
        net::OwnedUidsSnapshot(&g_statsScan);
        g_statsScanPos = 0;
        /* forget what was sent for uids this game no longer drives */
        std::vector<unsigned int> sorted(g_statsScan);
        std::sort(sorted.begin(), sorted.end());
        std::map<unsigned int, StatsLast>::iterator it = g_statsLast.begin();
        while (it != g_statsLast.end())
        {
            if (!std::binary_search(sorted.begin(), sorted.end(), it->first)) g_statsLast.erase(it++);
            else ++it;
        }
        if (g_statsScan.empty()) return;
    }
    const DWORD now = ::GetTickCount();
    int sent = 0, scanned = 0;
    while (g_statsScanPos < g_statsScan.size() && scanned < kStatsScanPerTick && sent < kStatsMaxPerTick)
    {
        const unsigned int uid = g_statsScan[g_statsScanPos++];
        ++scanned;
        if (!net::IsUidMine(uid) || !AnnouncedToPeer(uid)) continue;   // H030: never before its SPAWN
        ::Character* c = FindSpawned(uid);
        if (c == 0) continue;
        unsigned int raw[coopstats::kStatsCount];
        if (!StatsReadOwned(c, raw)) continue;
        std::map<unsigned int, StatsLast>::const_iterator it = g_statsLast.find(uid);
        const bool due = it == g_statsLast.end()
                         || coopstats::StatsWorthSending(it->second.raw, raw)
                         || (DWORD)(now - it->second.sentMs) >= kStatsResendMs;
        if (!due) continue;
        if (!net::SendStats(uid, raw)) { --g_statsScanPos; return; }   // link down: retry this uid next tick
        Remember(uid, raw, now);
        InterlockedIncrement64(&g_statsSent);
        ++sent;
    }
}

std::string StatsReportFields()
{
    std::ostringstream ss;
    ss << " statsSent=" << g_statsSent
       << " statsSpawnCarried=" << g_statsSpawnCarried
       << " statsApplied=" << g_statsApplied
       << " statsNoBlock=" << g_statsNoBlock
       << " statsBadBlock=" << g_statsBadBlock
       << " statsRecalcCalled=" << g_statsRecalcCalled
       << " statsMedCalled=" << g_statsMedCalled
       << " statsRecalcAnimal=" << g_statsRecalcAnimal
       << " statsMedFault=" << g_statsMedFault
       << " statsRecalcSkipped=" << g_statsRecalcSkipped
       << " statsMineRefused=" << g_statsMineRefused
       << " statsNoChar=" << g_statsNoChar
       << " statsBlocked=" << g_statsBlocked
       << " statsTickBlocked=" << g_statsTickBlocked
       << " statsFault=" << g_statsFault
       << " statsUnreadable=" << g_statsUnreadable
       << " bloodKept=" << g_statsBloodKept              /* copy1 */
       << " bloodKeptMoved=" << g_statsBloodKeptMoved
       << " bloodKeptDelta=" << F(g_statsBloodKeptDeltaMax)
       << " statsMaxPerTick=" << kStatsMaxPerTick;
    return ss.str();
}

// T-189 runboost (H051) - no C++ object in these three (C2712): __try around the reads and the engine call.
static bool RbSane(float v) { return v > 0.0f && v < 1.0e6f; }

bool RunCeilingRead(const void* character, float* run, float* animMax)
{
    const ::Character* ch = (const ::Character*)character;
    __try
    {
        if (ch == 0 || ch->stats == 0 || ch->animation == 0) return false;
        const float r = ch->stats->runSpeed;
        const float a = ch->animation->movementLimits.speedCap;
        if (!RbSane(r)) return false;
        *run = r; *animMax = a;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool RunCeilingWrite(void* character, float v)
{
    ::Character* ch = (::Character*)character;
    if (!RbSane(v)) return false;
    __try
    {
        if (ch == 0 || ch->animation == 0 || ch->movement == 0) return false;
        ch->animation->applySpeedCap(v);   // 0 row: calls nothing, and the read-back below says so
        if (ch->animation->movementLimits.speedCap != v) return false;
        ch->movement->speedCap = v;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool RunCeilingRestore(void* character)
{
    float run = 0.0f, animMax = 0.0f;
    if (!RunCeilingRead(character, &run, &animMax)) return false;
    return RunCeilingWrite(character, run);
}

} // namespace coop
