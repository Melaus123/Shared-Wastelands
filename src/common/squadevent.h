/* squadevent.h - T-1084 step 4. A WORLD SQUAD'S DISCRETE EVENTS MARK ITS WORLD RECORD DUE ON THE HEARTBEAT'S EARLY ROAD.
 *
 * The heartbeat (store.cpp HeartbeatTick, hbmath.h) rebuilds an awake world squad's record on a 30 s floor, and publishes a
 * squad marked "dirty" within its 2 s gap instead. These are the decisions the event roads and the heartbeat take, as pure
 * functions, so the plugin and the offline suite (src/coop-test/test_main.cpp) run the same text:
 *   - a member's death (worldstate.cpp detour_declareDead), a squad's money taken or given (store.cpp detour_takeMoney,
 *     Ownerships::takeMoney), its home building set (spawn.cpp detour_setHomeBuilding) and a shop squad's restock (items.cpp
 *     detour_refreshBuildingInventory) run on whatever thread the engine calls them on: on the main thread the squad is
 *     marked at once; elsewhere an address goes into a fixed ring, tagged with what it is, and the main thread drains it
 *     (store.cpp SqEventDrainTick). The ring holds addresses only, and the drain only COMPARES them with the squads the
 *     heartbeat tracks - it never reads through a queued address;
 *   - a member joining or leaving (a recruit, a hire, a transfer between squads) is seen by the main thread's once-a-second walk
 *     of the awake world squads (store.cpp DeathWatchTick) as a change in the squad's member count or member set;
 *   - the mark is two flags, so any number of events before the squad's next visit publish its record once;
 *   - a home or a restock noted inside the squad's WAKE WINDOW (from its wake until its people are seen - the wake's people wait
 *     ends, or, for a wake with no wait, its first heartbeat visit - the time in which the engine reads the squad in) is the
 *     read, not a change: it takes no mark (WakeWindowHolds, NoteVerdict), so the
 *     wake's own rule - skip the publish when the wake applied the world's newest record - still holds. Every note carries the
 *     stamp of the event clock when it was taken, so a queued note is judged by when it was taken, not when it is drained.
 *
 * HEADER-ONLY (inline). No clock, no engine memory. C++03 (VS2010 v100): no auto, no nullptr, no <atomic>.
 */
#ifndef SQUADEVENT_H_INCLUDED
#define SQUADEVENT_H_INCLUDED

#include <cstddef>
#include <cstring>
#include "hbmath.h"
#include "ownrec.h"

#if defined(_MSC_VER)
extern "C" void* _InterlockedCompareExchangePointer(void* volatile* Destination, void* Exchange, void* Comparand);
extern "C" void* _InterlockedExchangePointer(void* volatile* Target, void* Value);
extern "C" long _InterlockedIncrement(long volatile* Addend);
extern "C" long _InterlockedDecrement(long volatile* Addend);
#pragma intrinsic(_InterlockedCompareExchangePointer, _InterlockedExchangePointer, _InterlockedIncrement, _InterlockedDecrement)
#endif

namespace sqevent {

/* ---- what a note does -------------------------------------------------------------------------------------------------- */
/* On the main thread: marked now, or skipped while there is nothing to mark (no tracked squad) or the world is loading or
   closing (a squad being read in is not a change). Off the main thread: queued - the squad registry is a main-thread map
   (bug classes "an engine callback assumed to run on the main thread", "a main-thread-only container read from an engine
   worker detour"). */
const int kActSkip = 0, kActMarkNow = 1, kActQueue = 2;
inline int NoteAction(int onMainThread, int registryEmpty, int writesBlocked)
{
    if (onMainThread == 0) return kActQueue;
    if (registryEmpty != 0 || writesBlocked != 0) return kActSkip;
    return kActMarkNow;
}
/* What the drain does with what it took: matched (1), or dropped (0) during a load or a teardown, or with no tracked squad -
   the addresses then belong to no squad tracked now. */
inline int DrainUses(int writesBlocked, int registryEmpty) { return (writesBlocked == 0 && registryEmpty == 0) ? 1 : 0; }

/* ---- the mark ----------------------------------------------------------------------------------------------------------- */
/* THE MARK ON ONE TRACKED SQUAD (store.cpp HbMarkDirty): the gate (MarkAccepted), then the flags (MarkFlags). 1 = taken. */
inline int MarkAccepted(int registered, int foreign);
inline void MarkFlags(int isWake, int* dirty, int* dirtyRecord);
inline int MarkEntry(int registered, int foreign, int isWake, int* dirty, int* dirtyRecord)
{
    if (MarkAccepted(registered, foreign) == 0) return 0;
    MarkFlags(isWake, dirty, dirtyRecord);
    return 1;
}
/* IS THE MARK TAKEN? Only for a squad the heartbeat tracks (`registered`) that this game writes (`foreign` 0). A squad another
   game writes is that game's to publish: an event seen here sends nothing (the publish re-judges the writer as well). */
inline int MarkAccepted(int registered, int foreign) { return (registered != 0 && foreign == 0) ? 1 : 0; }

/* THE FLAGS A MARK SETS (store.cpp HbMarkDirty). A wake sets `dirty` alone: waking is not a change to the record. Every other
   cause - an item move, a death, a member in or out, money, a home - is a change to the record and sets `dirtyRecord` too, so the
   giant wait and an older record's budget debt do not hold it (DueForVisit; hbmath.h BudgetAllowsGroup). Flags, set and never
   counted: N events before the next visit leave the entry as one event does, and that visit clears them and publishes once. */
inline void MarkFlags(int isWake, int* dirty, int* dirtyRecord)
{
    *dirty = 1;
    if (isWake == 0) *dirtyRecord = 1;
}

/* MAY A TRACKED SQUAD PUBLISH AT THIS VISIT? (store.cpp HbDue). `dirtyPass` = the visit is the dirty pass (true) or the cursor's
   floor turn (false). A squad whose last serialise was slow keeps a 5x floor; a dirty squad marked by a wake alone (no change to
   its record, no change of writer, no budget deferral) waits as on the floor when it is a giant; any change to the record uses
   the 2 s gap whatever the size. */
inline bool DueForVisit(bool dirtyPass, int slow, int dirty, int dirtyRecord, int dirtyWriter, int owed,
                        unsigned int sinceLastMs, unsigned int lastBytes)
{
    if (!dirtyPass && slow != 0) return coopown::TurnDue(true, sinceLastMs, coophb::kPeriodMs, coopown::kSlowMul, coophb::kDirtyGapMs);
    const bool wakeOnly = dirtyPass && dirty != 0 && dirtyRecord == 0 && dirtyWriter == 0 && owed == 0;
    return coophb::DueOnTurn(dirtyPass && !wakeOnly, sinceLastMs, lastBytes, coophb::kPeriodMs, coophb::kDirtyGapMs, coophb::kGiantBytes, coophb::kGiantMultiplier);
}

/* ---- the member set ----------------------------------------------------------------------------------------------------- */
/* A squad's member list is read up to LivingMembersPod's own bound; a longer list is not matched (counted by the caller). */
const int kMaxMembers = 512;

/* A FINGERPRINT OF THE MEMBER SET: the members' addresses mixed and summed, order-free. Compared, never read through. */
inline unsigned long long MemberPrint(void* const* members, int n)
{
    unsigned long long sum = 0, mix = 0;
    if (members == 0) return 0;
    for (int i = 0; i < n; ++i)
    {
        const unsigned long long v = (unsigned long long)(size_t)members[i];
        sum += v;
        mix ^= v * 0x9E3779B97F4A7C15ULL;
    }
    return sum ^ (mix << 1) ^ (mix >> 63);
}

/* DID THE SQUAD'S MEMBERS CHANGE SINCE THE LAST LOOK? `seenCount` -1 = not looked at yet: the first look only records (a
   squad's arrival is the wake's own mark); -1 now = not read. A count change is a member out (a recruit, a hire, a transfer)
   or in; the same count with another fingerprint is a leave and a join between two looks. A death leaves the corpse in a world
   squad's list (the declareDead road marks it). */
inline int MembersChanged(int seenCount, unsigned long long seenPrint, int count, unsigned long long print)
{
    if (seenCount < 0 || count < 0) return 0;
    return (seenCount != count || seenPrint != print) ? 1 : 0;
}

/* IS `who` ONE OF THESE MEMBERS? The index, or -1. An address compare only: `who` may be a character the engine has freed
   since it was queued, and it is never read. */
inline int MemberIndex(void* const* members, int n, const void* who)
{
    if (members == 0 || who == 0) return -1;
    for (int i = 0; i < n; ++i) if (members[i] == who) return i;
    return -1;
}

/* DOES THIS TRACKED SQUAD HOLD THE DEAD CHARACTER? (store.cpp SqEventMarkHolder, per entry.) 1 holds; 0 not (another game writes
   it, its list was not read, or the character is not in it); -1 its list is longer than kMaxMembers and was not looked at. */
inline int EntryHolds(int foreign, int readOk, int count, void* const* members, const void* who)
{
    if (foreign != 0 || readOk == 0 || count <= 0 || members == 0) return 0;
    if (count > kMaxMembers) return -1;
    return MemberIndex(members, count, who) >= 0 ? 1 : 0;
}

/* WHICH TRACKED SQUAD DOES A RESTOCK NOTE NAME? (store.cpp SqEventMatch, per entry.) The engine's restock hands over the shop
   squad's own address (the tracked key) on the roads seen so far; its active part (Platoon+0x1D8) is matched as well. Both are
   address compares, nothing is read through. 2 = the entry IS the noted squad; 1 = the entry's active part is the noted address
   (`readOk` 0 = its active part was not read); 0 = neither, or another game writes it. */
inline int RestockNames(int foreign, const void* squad, int readOk, const void* activePart, const void* noted)
{
    if (foreign != 0 || noted == 0) return 0;
    if (squad == noted) return 2;
    return (readOk != 0 && activePart == noted) ? 1 : 0;
}

/* ---- money and home: the squad's pot ------------------------------------------------------------------------------------ */
/* A squad's Ownerships (its money at +0x88, its home building's hand at +0x38) is embedded at Platoon+0x148 (05-findings
   'T-1084 STEP 4 - SQUAD MONEY AND HOME WRITERS'). The platoon a pot would belong to - an address computed, never read; it
   names a squad only when the heartbeat tracks that address. A character's own pot names none. */
const size_t kPotInPlatoon = 0x148;
inline void* PotPlatoon(const void* pot)
{
    if ((size_t)pot <= kPotInPlatoon) return 0;
    return (void*)((const char*)pot - kPotInPlatoon);
}
/* Ownerships::takeMoney (negative amount = given): a change only when the call took (returned true) a non-zero amount. */
inline int MoneyMarks(int returned, int amount) { return (returned != 0 && amount != 0) ? 1 : 0; }
/* setHomeBuilding: a change only when the 28 bytes of the home hand differ after the call (the engine fills only an empty home). */
const int kHomeHandBytes = 28;
inline int HomeChanged(const unsigned char* before, const unsigned char* after)
{
    if (before == 0 || after == 0) return 0;
    return std::memcmp(before, after, kHomeHandBytes) != 0 ? 1 : 0;
}

/* ---- the wake window ---------------------------------------------------------------------------------------------------- */
/* THE EVENT CLOCK: a 16-bit count the main thread advances when a tracked squad's wake window opens (its wake) and when it closes
   (its first heartbeat visit after the wake). A note is stamped with the clock as it is taken, on any thread. */
const unsigned int kStampMask = 0xFFFFu;
/* WAS A NOTE STAMPED `stamp` TAKEN INSIDE THIS SQUAD'S WAKE WINDOW? `hasWindow` 0 = the squad never woke here (no window); `open` 1 =
   the window has not closed yet; `openAt` / `closeAt` = the clock when it opened / closed. Modular in 16 bits: a note taken
   before the window opened is half the clock's range behind it. */
inline int WakeWindowHolds(int hasWindow, unsigned int openAt, int open, unsigned int closeAt, unsigned int stamp)
{
    if (hasWindow == 0) return 0;
    const unsigned int since = (stamp - openAt) & kStampMask;
    if (since >= 0x8000u) return 0;
    if (open != 0) return 1;
    return since < ((closeAt - openAt) & kStampMask) ? 1 : 0;
}

/* ONE SQUAD'S WAKE WINDOW (store.cpp HbEntry). Opened at its wake; closed when its people are seen - its wake's people wait ends
   (seen, or given up), or, for a wake that set no wait, at its first heartbeat visit (WindowClosesAtVisit; while a wait runs the
   heartbeat does not visit it). A closed window still judges the notes queued before it closed, at the next drain; after that
   drain it is gone (WindowAfterDrain), so a note taken later is never judged inside it, however far the 16-bit clock has turned. */
struct WakeWindow { int has, open, closing; unsigned int openAt, closeAt; };
inline void WindowReset(WakeWindow* w) { w->has = 0; w->open = 0; w->closing = 0; w->openAt = 0; w->closeAt = 0; }
inline void WindowOpen(WakeWindow* w, unsigned int stamp) { w->has = 1; w->open = 1; w->closing = 0; w->openAt = stamp & kStampMask; w->closeAt = 0; }
/* 1 = it was open and is closed now (the caller advances the clock for `stamp` only then) */
inline int WindowClose(WakeWindow* w, unsigned int stamp)
{
    if (w->has == 0 || w->open == 0) return 0;
    w->open = 0; w->closing = 1; w->closeAt = stamp & kStampMask;
    return 1;
}
/* after a drain has judged every note queued before the close: the window ends */
inline void WindowAfterDrain(WakeWindow* w) { if (w->closing != 0) WindowReset(w); }
inline int WindowHolds(const WakeWindow* w, unsigned int stamp) { return WakeWindowHolds(w->has, w->openAt, w->open, w->closeAt, stamp); }
/* A heartbeat visit while the wake's people wait runs is held (no publish of a squad not built yet, the window stays open); a visit
   with no wait closes the window. And the prune of a squad with no people keeps one whose people are still coming. */
inline int VisitHeldForPeople(int peopleWaitRuns) { return peopleWaitRuns != 0 ? 1 : 0; }
inline int WindowClosesAtVisit(int peopleWaitRuns) { return peopleWaitRuns == 0 ? 1 : 0; }
inline int PruneKeeps(int peopleWaitRuns) { return peopleWaitRuns != 0 ? 1 : 0; }

/* ---- the off-thread queue ----------------------------------------------------------------------------------------------- */
/* What a queued address is: its low bits carry the kind (the engine's objects are 8-byte aligned) and its top 16 bits the stamp
   (a user-mode address on x64 is below 2^48). */
const int kKindCharacter = 0;   /* a dead character: the tracked squad whose member list holds it */
const int kKindPotMoney  = 1;   /* a pot whose money changed: the squad PotPlatoon names */
const int kKindPotHome   = 2;   /* a pot whose home building changed: the squad PotPlatoon names */
const int kKindRestock   = 3;   /* a shop squad restocked, named by its own address or its active part: the tracked squad it names (RestockNames) */
const int kKindCount     = 4;
inline void* TagAddr(const void* p, int kind, unsigned int stamp)
{
    const unsigned long long a = (unsigned long long)(size_t)p;
    if (p == 0 || kind < 0 || kind >= kKindCount || (a & 7ULL) != 0 || (a >> 48) != 0) return 0;
    return (void*)(size_t)(a | (unsigned long long)kind | ((unsigned long long)(stamp & kStampMask) << 48));
}
inline void* UntagAddr(const void* v, int* kind, unsigned int* stamp)
{
    const unsigned long long a = (unsigned long long)(size_t)v;
    *kind = (int)(a & 7ULL);
    *stamp = (unsigned int)(a >> 48) & kStampMask;
    return (void*)(size_t)(a & 0x0000FFFFFFFFFFF8ULL);
}

/* WHAT A NOTE DOES TO THE SQUAD IT NAMES. Not this game's to write, or no tracked squad: nothing (counted). A home or a restock
   inside the squad's wake window: the read, not a change - nothing (counted). Otherwise the record is marked. A death or money is
   never the read (a state load writes money without takeMoney). */
const int kVerdictNotOurs = 0, kVerdictMark = 1, kVerdictWakeRead = 2;
inline int NoteVerdict(int kind, int accepted, int inWakeWindow)
{
    if (accepted == 0) return kVerdictNotOurs;
    if ((kind == kKindPotHome || kind == kKindRestock) && inWakeWindow != 0) return kVerdictWakeRead;
    return kVerdictMark;
}

/* A fixed ring of tagged addresses: any thread pushes, the main thread takes. `used` counts the slots reserved or filled, `next`
   spreads the pushes' starting slots. A zero-filled ring is empty, so a ring with static storage needs no set-up. */
const int kRingCap = 256;
const int kRingProbe = 32;   /* a push looks at no more slots than this */
struct EventRing { void* volatile slot[kRingCap]; volatile long used; volatile long next; };
/* MAY A NOTE KEEP THE SLOT IT RESERVED? `reserved` = the count with this note's reservation in it. A money note (the most frequent:
   every NPC shopper's trade, most on no tracked squad) keeps it only within the first half - deaths, homes and restocks keep the
   other half; no note past the whole ring. The reservation is made first (RingPush), so concurrent pushes cannot pass the limit. */
inline int ReserveAdmits(int kind, long reserved)
{
    if (reserved > (long)kRingCap) return 0;
    if (kind == kKindPotMoney && reserved > (long)(kRingCap / 2)) return 0;
    return 1;
}
const int kPushQueued = 1, kPushFull = 0, kPushHalfRefused = -1;
inline int RingPush(EventRing* r, void* p, int kind)
{
    if (r == 0 || p == 0) return kPushFull;
    const long reserved = _InterlockedIncrement(&r->used);
    if (ReserveAdmits(kind, reserved) == 0)
    {
        _InterlockedDecrement(&r->used);
        return (kind == kKindPotMoney && reserved <= (long)kRingCap) ? kPushHalfRefused : kPushFull;
    }
    const unsigned int start = (unsigned int)_InterlockedIncrement(&r->next);
    for (int k = 0; k < kRingProbe; ++k)
    {
        const int i = (int)((start + (unsigned int)k) % (unsigned int)kRingCap);
        if (_InterlockedCompareExchangePointer(&r->slot[i], p, 0) == 0) return kPushQueued;
    }
    _InterlockedDecrement(&r->used);
    return kPushFull;
}
/* Every queued address out of the ring (at most `cap`; what does not fit stays for the next take). The count taken. */
inline int RingTake(EventRing* r, void** out, int cap)
{
    if (r == 0 || out == 0) return 0;
    int n = 0;
    for (int i = 0; i < kRingCap && n < cap; ++i)
    {
        void* const p = _InterlockedExchangePointer(&r->slot[i], 0);
        if (p != 0) { out[n++] = p; _InterlockedDecrement(&r->used); }
    }
    return n;
}

}   // namespace sqevent

#endif
