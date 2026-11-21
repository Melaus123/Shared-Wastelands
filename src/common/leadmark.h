/* src/common/leadmark.h - T-1442 step 6 (F1480 design a): A SQUAD'S LEADER CHANGE GOES OUT WHEN THE ENGINE MAKES IT, NOT ON THE NEXT
 * 1 S LOOK.
 *
 * ONE RULE, deadevent.h's: nothing in here reads engine memory, reads a global, calls the operating system or includes a header, so
 * the offline suite drives it directly (the interlocked operations are compiler intrinsics, declared below - not calls).
 *
 * WHY IT EXISTS. The squad-lead announcement (handoff.cpp AnnounceLead: the engine's acting leader, the formal leader, the members
 * this game runs) went out only on HandoffTick's 1 s pass, so the other game's leader and member hand-over were up to ~2 s late after
 * a leader change. Now the engine's writers NOTE what they changed, after their original returns: set-leader 0x790C00 and
 * remove-member 0x794C00 note the squad (ActivePlatoon), the knock-out calls (medical.cpp detour_knockout /
 * detour_startKnockoutTimer) and the death call (worldstate.cpp detour_declareDead) note the character. The main thread takes the
 * notes every frame (handoff.cpp LeadEventTick, before HandoffTick's 1 s gate), finds the squad each names, and announces it with
 * live reads through the same per-squad function the 1 s pass uses. The 1 s pass stays as the backstop and counts what it finds
 * that no note announced (LeadBackstopKind).
 *
 * SHAPE. kLeadMarkSlots fixed slots, each one value: the noted address with the note's kind in its low three bits (engine objects are
 * at least 8-aligned; an address with any of them set is refused and counted 'bad'), and the tick of the note (a log value). Add: a
 * value already queued is not queued again; otherwise the first empty slot is taken by compare-exchange; no empty slot = overflow,
 * counted (the 1 s pass is the backstop). Take: each slot is emptied by exchange.
 *
 * THREADING CONTRACT. Any number of adders (any thread: LeadMarkAdd - the detours, where nothing may allocate, lock or log), ONE taker
 * (the main thread: LeadMarkTake, LeadMarkReset).
 *
 * C++03 (VS2010 v100, x64): no auto, no nullptr, no range-for, no <atomic>.
 */
#ifndef COOP_COMMON_LEADMARK_H
#define COOP_COMMON_LEADMARK_H

#if defined(_MSC_VER)
extern "C" __int64 _InterlockedCompareExchange64(__int64 volatile* Destination, __int64 Exchange, __int64 Comparand);
extern "C" __int64 _InterlockedExchange64(__int64 volatile* Target, __int64 Value);
extern "C" long _InterlockedExchange(long volatile* Target, long Value);
extern "C" long _InterlockedIncrement(long volatile* Addend);
extern "C" long _InterlockedDecrement(long volatile* Addend);
#pragma intrinsic(_InterlockedCompareExchange64, _InterlockedExchange64, _InterlockedExchange, _InterlockedIncrement, _InterlockedDecrement)
#endif

namespace coopleadmark {

enum { kLeadMarkSlots = 128 };
/* what a note names: a squad (the ActivePlatoon the engine changed) or a character (knocked out / dead - its squad found at the take) */
/* review fold S2 / S3: the acting-leader getter 0x791FF0 when its +0xA8 write changed the value (a squad); a dead character's squad
   taken BEFORE the death call (the death path detaches the character from its squad) */
/* T-1442 step 6 fix a (F1506): kNoteUidLeader - the adoption sweep gave a squad's formal leader (+0xA0) its mod uid (worldsync.cpp,
   after net::SetLocalOwner): the squad is announced the next frame, not at the next 1 s pass (which may have just sent it with leader 0) */
/* T-1442 step 6 fix attempt 3 (F1514): kNoteHeld - a squad whose formal (+0xA0) or acting (+0xA8) leader went into or out of a bed
   or cage (+0x2F8), was knocked down or stood up (+0xE0) or was picked up or put down (+0x3D4, which the engine's inRagdoll reads):
   the facts the mod's 'leader cannot lead' reads (handoff.cpp LeaderOut), noted at the engine's writers of them. The last kind the
   three low bits hold. */
enum { kNoteSetLeader = 0, kNoteRemoveMember = 1, kNoteKo = 2, kNoteDead = 3, kNoteDeadSquad = 4, kNoteActingGetter = 5, kNoteUidLeader = 6, kNoteHeld = 7, kNoteKinds = 8 };
enum { kMarkOverflow = 0, kMarkNoted = 1, kMarkAlready = 2, kMarkBad = -1 };

struct LeadMarkTable
{
    volatile __int64 v[kLeadMarkSlots];    /* address | kind (0 = empty) */
    volatile long tick[kLeadMarkSlots];    /* the note's tick (milliseconds, the caller's clock) - a log value */
    volatile long pending;                 /* values in the table now - the taker's fast path */
    volatile long noted[kNoteKinds];       /* adds that took a slot, by kind, all time */
    volatile long already;                 /* adds whose value was already queued, all time */
    volatile long overflow;                /* adds that found no empty slot, all time (the 1 s pass decides those) */
    volatile long bad;                     /* adds refused: address 0, not 8-aligned, or a kind out of range */
};

inline int NoteIsSquad(int kind) { return (kind == kNoteSetLeader || kind == kNoteRemoveMember || kind == kNoteDeadSquad || kind == kNoteActingGetter || kind == kNoteUidLeader || kind == kNoteHeld) ? 1 : 0; }
inline const char* NoteName(int kind)
{
    return kind == kNoteSetLeader ? "setLeader" : kind == kNoteRemoveMember ? "removeMember" : kind == kNoteKo ? "ko" : (kind == kNoteDead || kind == kNoteDeadSquad) ? "dead" : kind == kNoteActingGetter ? "actingGetter" : kind == kNoteUidLeader ? "uidLeader" : kind == kNoteHeld ? "held" : "?";
}

/* ANY THREAD (no allocation, no lock). */
inline int LeadMarkAdd(LeadMarkTable* t, unsigned long long addr, int kind, unsigned long tickMs)
{
    int i;
    if (t == 0) return kMarkBad;
    if (addr == 0 || (addr & 7ULL) != 0 || kind < 0 || kind >= (int)kNoteKinds) { _InterlockedIncrement(&t->bad); return kMarkBad; }
    const __int64 v = (__int64)(addr | (unsigned long long)kind);
    for (i = 0; i < (int)kLeadMarkSlots; ++i)   /* the duplicate scan by plain (volatile) reads - a miss only costs a second slot */
        if (t->v[i] == v) { _InterlockedIncrement(&t->already); return kMarkAlready; }
    for (i = 0; i < (int)kLeadMarkSlots; ++i)
    {
        if (t->v[i] != 0) continue;
        if (_InterlockedCompareExchange64(&t->v[i], v, 0) != 0) continue;
        _InterlockedExchange(&t->tick[i], (long)tickMs);   /* review nit: after the claim, so another adder's claim cannot overwrite it; a take
                                                             between the two reads the slot's earlier tick - a log value only */
        _InterlockedIncrement(&t->pending);
        _InterlockedIncrement(&t->noted[kind]);
        return kMarkNoted;
    }
    _InterlockedIncrement(&t->overflow);
    return kMarkOverflow;
}

/* MAIN THREAD. Empties the table into addrs/kinds/ticks[0..max); returns how many were taken. A slot past `max` stays for the next take. */
inline int LeadMarkTake(LeadMarkTable* t, unsigned long long* addrs, int* kinds, unsigned long* ticks, int max)
{
    int i, n = 0;
    if (t == 0 || addrs == 0 || kinds == 0 || max <= 0 || t->pending == 0) return 0;
    for (i = 0; i < (int)kLeadMarkSlots && n < max; ++i)
    {
        if (t->v[i] == 0) continue;
        const __int64 v = _InterlockedExchange64(&t->v[i], 0);
        if (v == 0) continue;
        _InterlockedDecrement(&t->pending);
        if (ticks != 0) ticks[n] = (unsigned long)t->tick[i];
        addrs[n] = (unsigned long long)v & ~7ULL;
        kinds[n] = (int)((unsigned long long)v & 7ULL);
        ++n;
    }
    return n;
}

/* MAIN THREAD (the world teardown; the suite). Empties every slot; returns how many notes were dropped. The counters are kept. */
inline int LeadMarkReset(LeadMarkTable* t)
{
    int i, n = 0;
    if (t == 0) return 0;
    for (i = 0; i < (int)kLeadMarkSlots; ++i)
        if (_InterlockedExchange64(&t->v[i], 0) != 0) { _InterlockedDecrement(&t->pending); ++n; }
    return n;
}

/* T-1442 step 6 fix a (F1506): the adoption sweep's uid notes the character's squad only when the character is its squad's formal
   leader (+0xA0) - a member's uid changes no leader field. haveSquad: the character's squad was read; isFormalLeader: +0xA0 is it.
   Whether this game announces that squad is decided at the take, as for every squad note (LeadMarkVerdict). */
inline int LeadUidGivenNotes(int haveSquad, int isFormalLeader) { return (haveSquad != 0 && isFormalLeader != 0) ? 1 : 0; }

/* T-1442 step 6 fix attempt 3 (F1514, H-T1442-leadermiss): the engine's writers of the facts behind 'this leader cannot lead'
   (handoff.cpp LeaderOutAnswer, the one copy of the rule: dead, or in a bed or cage +0x2F8, or knocked down +0xE0, or ragdolled - the
   engine's inRagdoll 0x7D08A0: carried +0x3D4 or the physics ragdoll on) note the character's squad when the character is its formal
   (+0xA0) or acting (+0xA8) leader and that answer changed across the engine's call - both when it starts and when it ends. Death keeps
   its own note. */
/* readOk     - the character's fields were read before and after the engine's call
   haveSquad  - the squad read (Character +0x658) is not empty
   isFormal / isActing - the character is that squad's +0xA0 / +0xA8
   outBefore / outAfter - LeaderOutAnswer before and after the call (1 out, 0 not; an unreadable answer is readOk 0)
   1 = note the squad (kNoteHeld). A squad this game does not run is dropped at the take (LeadMarkVerdict), as for every squad note. */
inline int LeadHeldNotes(int readOk, int haveSquad, int isFormal, int isActing, int outBefore, int outAfter)
{
    if (readOk == 0 || haveSquad == 0) return 0;
    if (isFormal == 0 && isActing == 0) return 0;
    return ((outBefore != 0) != (outAfter != 0)) ? 1 : 0;
}

/* ---- THE MARK SKIP: which noted squad does this game announce? ------------------------------------------------------------------
   ownCall     - the note came from the mod's own set-leader call on a copy squad (handoff.cpp SquadLeaderSafePointDrain, told apart
                 by its thread mark around the call): never noted
   squadFound  - the noted squad (or the noted character's squad) is one this game's pass groups: a squad with a member this game
                 runs that is not a player's (HandoffTick's grouping, P3)
   Squads this game only copies are not found: their members are all the other game's, which announces them. */
enum { kMarkGo = 0, kMarkOwnCall = 1, kMarkNotRun = 2 };
inline int LeadMarkVerdict(int ownCall, int squadFound)
{
    if (ownCall != 0) return kMarkOwnCall;
    if (squadFound == 0) return kMarkNotRun;
    return kMarkGo;
}

/* ---- THE BACKSTOP'S CLASSIFICATION: what the 1 s pass found when its announcement of a squad went out ---------------------------
   prevSent                - the squad had been announced before (MyLead.sent)
   prevActing, prevFormal  - what that announcement said
   acting, formal          - what this one says
   watched                 - a note for this squad is still in its watch window (the event road saw it; its send was held back)
   formalStanding          - the formal leader is not out now (LeaderOut false)
   kBsFirst   - the squad's first announcement (newly run here, a new link) - not a leader change
   kBsOther   - the leader fields are unchanged: the members run here, the area or the money changed - not a leader change
   kBsWatched - a leader change the event road has a note for, held back (the per-frame send cap or the link)
   kBsWake    - the formal leader stood up again (wake-up, out of a bed or cage) - noted since fix attempt 3 (kNoteHeld); kept for a
                stand-up no hooked writer saw (a ragdoll ending on its own)
   kBsMissed  - a leader change no note announced */
enum { kBsFirst = 0, kBsOther = 1, kBsWatched = 2, kBsWake = 3, kBsMissed = 4, kBsKinds = 5 };
inline int LeadBackstopKind(int prevSent, unsigned int prevActing, unsigned int prevFormal, unsigned int acting, unsigned int formal, int watched, int formalStanding)
{
    if (prevSent == 0) return kBsFirst;
    if (acting == prevActing && formal == prevFormal) return kBsOther;
    if (watched != 0) return kBsWatched;
    if (formal == prevFormal && formal != 0 && acting == formal && formalStanding != 0) return kBsWake;
    return kBsMissed;
}

/* the watch window: a squad noted at `notedAt` (seconds) is announced again each frame until `notedAt + kWatchSec` - a knock-out's
   fall (the downed read) and the engine's acting-leader write (+0xA8, the getter 0x791FF0) can land a few frames after the call */
const double kWatchSec = 2.0;
inline int LeadWatchHolds(double notedAt, double now) { return (now >= notedAt && now - notedAt <= kWatchSec) ? 1 : 0; }

/* the event road's hand-over decision runs when its announcement went out, or the other game's announcement for the squad arrived;
   an unchanged announcement on a watched frame decides nothing the last pass did not */
inline int LeadEventDecides(int sentNow, int arrival) { return (sentNow != 0 || arrival != 0) ? 1 : 0; }

}  // namespace coopleadmark

#endif
