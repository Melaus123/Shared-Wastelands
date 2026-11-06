#ifndef SW_COMMON_POSELAYER_H
#define SW_COMMON_POSELAYER_H
/* T-307: A HELD ANIMATION THAT PLAYS ONLY IN THE ANIMATION LAYER TABLE. Pure: no engine, no network - the offline suite tests
   every decision here.

   The pose road (spawn.cpp PoseWatchTick) sends an owned character's looped action from its current-action slot
   (AnimationClass +0x210). Some engine tasks play a looped animation without filling that slot: an NPC looting a knocked-out
   body (Task_Loot_AI, goal 'Collecting item') plays looped 'medic' only in the layer table *(AnimationClass +0xE0) - layer 1,
   weight 1.00, looped (T1630, Confirmed). When the slot is empty the owner reads the layer table instead:

   1. PICK (LayerPick): of the states read, the highest-weight LOOPED state on an allowed layer, at no less than the full-play
      weight; the first one read wins a tie. T1630 (Confirmed) shows why both limits: layer 0 holds the lower body's walk /
      jog / limp states and layer 2 overlays ('breathing noarms', 'head turning', 'hand2shoulder' at weight 1.00) - picked,
      they would become poses whenever the layer-1 state fades; layer 1 holds the held action ('medic', 'idle_stand_normal').
      A NaN weight never counts.
      T-890 TURRET (T1871, Confirmed): the one exception to 'layer 1 only' - a manned turret's gunner holds no layer-1 state;
      the turret-manning job plays 'combatstance' on LAYER 2 (weight 1.00, looped). A layer-2 state is taken when the turret
      job's code started it (State::turret, TurretStarted); every other layer-2 state (the overlays above) stays refused.
   2. SETTLE (CandStep): a picked name counts only after it has stayed the pick for kSettleMs - a blend between two states
      (a stop, an action ending) is never sent as a pose. A different name, or no pick, starts over.

   3. WHO STARTED IT (StartTable, LayerStartSendable): the picked state is a pose unless a starter on the REFUSE LIST put it in
      the layer table (owner decision 1236 a, 2026-10-31: every layer-1 animation is sent except the engine's background
      ones, so an animation a mod's new task plays copies without being listed).
      The engine puts an animation into a layer through one function (AnimationClass layer play, Steam 1.0.65 0x5B7030),
      called by the layer-only tasks ('medic' 0x34D8D0, 'chop down static' 0x353450, ...) and by the animation update's own
      controllers (idle chooser 0x51C920, walk / run blend 0x51CE50, head turn 0x5B79A0, 0x5F0C10, and the main-slot fill
      0x51E020, whose animation the main-slot road already sends). A layer state keeps no 'who started me' field, so a
      detour on the layer play notes, per (animation object, animation record), whether its caller lies inside a listed
      task's code, a listed controller's code, or neither (CallerKind) - the latest call wins. Layer 1 also holds looped
      upper-body locomotion ('jog upper' 1.00 looped - T1630, Confirmed), which the walk / run blend plays (Inferred from
      its decomp). The listed tasks are code; the animation names are not listed, so a mod's animation played by one of
      them is a pose. 'Controller' below means a function on the refuse list (the five AnimCtl* rows, the derived entries
      kRefuseDerived* and the bed entry RefuseBedStart); its state, or no note (the hook off, a state started before it, a note pushed out), refuses. Any other
      game code (an unlisted task, a mod's task) is allowed and counted (spawn.cpp poseLayerAllowedUnknown).

   The road's own rules still decide afterwards, as for +0x210: the excluded names, the 'idle' prefix, the moving owner. */

#if defined(_MSC_VER)
extern "C" long _InterlockedCompareExchange(long volatile* Destination, long Exchange, long Comparand);
extern "C" long _InterlockedExchange(long volatile* Target, long Value);
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedExchange)
#endif

namespace poselayer {

const unsigned int kStrMax = 48;              /* as the pose wire's kPoseStrMax: a longer name is not a pose */
const unsigned int kLayerMask = 1u << 1;      /* the layers a pose may come from: layer 1 (see 1 above) */
const unsigned int kTurretLayerMask = 1u << 2; /* T-890 TURRET: the layer the turret job's state may also come from (see 1) */
const int kLayers = 3;                        /* the layer table's layers the reader may visit: 0-2 */
const int kStateCap = 32;                     /* states read per character per frame, all layers together */
const unsigned int kLayerCountMax = 4096;     /* a layer state count above this is not plausible: its states are not read */
const float kFullWeight = 0.95f;              /* a state at least this heavy is fully played (T1630: 'medic' 1.00) */
const unsigned int kSettleMs = 300;           /* a pick must hold this long before it is a pose */

/* ---- 3. who started a layer state ------------------------------------------------------------------------------------ */

enum { kCallerNone = 0, kCallerTask = 1, kCallerController = 2, kCallerUnknown = 3 };

/* one code range [begin, end) of the running image (a function, or one of its separated code pieces) */
struct Range
{
    unsigned long long begin;
    unsigned long long end;
};
const int kRangeCap = 64;   /* per kind (tasks, controllers) */

inline int InRanges(unsigned long long a, const Range* r, int n)
{
    for (int i = 0; i < n; ++i)
        if (a >= r[i].begin && a < r[i].end) return 1;
    return 0;
}

/* the caller type of a layer-play call returning to ret: outside the game image [imageBegin, imageEnd) = none (another
   module's code), inside a controller range = controller, inside a task range = task, else unknown */
inline int CallerKind(unsigned long long ret, unsigned long long imageBegin, unsigned long long imageEnd,
                      const Range* task, int nTask, const Range* ctl, int nCtl)
{
    if (ret < imageBegin || ret >= imageEnd) return kCallerNone;
    if (InRanges(ret, ctl, nCtl)) return kCallerController;
    if (InRanges(ret, task, nTask)) return kCallerTask;
    return kCallerUnknown;
}

/* another mod's hook on the layer play puts its own code at the return address: the caller type of the first of the
   stack's return addresses (frames[0..n), innermost first) inside the game image; *at its index (-1 and kCallerNone: none) */
inline int FirstGameFrameKind(const unsigned long long* frames, int n, unsigned long long imageBegin, unsigned long long imageEnd,
                              const Range* task, int nTask, const Range* ctl, int nCtl, int* at)
{
    *at = -1;
    for (int i = 0; i < n; ++i)
    {
        const int k = CallerKind(frames[i], imageBegin, imageEnd, task, nTask, ctl, nCtl);
        if (k != kCallerNone) { *at = i; return k; }
    }
    return kCallerNone;
}

/* 1 a layer state with this caller type may be sent as a pose (owner decision 1236 a): a listed task's, or any other game
   code's (unknown: an unlisted task, a mod's task); a refuse-list starter's (kCallerController) or none (no note) never */
inline int LayerStartSendable(int kind) { return (kind == kCallerTask || kind == kCallerUnknown) ? 1 : 0; }

/* T-890 TURRET (owner decisions 251, 1375): a manned turret's gunner pose is shown. The turret-manning job (task row
   AnimTaskCombatStance, 1.0.65 0x347490 / 1.0.68 0x3474F0 - T1854 install line 'layer play caller 0x3474F0 task') plays
   'combatstance' only in the layer table, the action slot empty (decomp_347490.txt:53-60). The pose road's name filter
   (spawn.cpp PoseActionExcluded) refuses any name holding 'combat' or 'stance' - the fight stances, which T-1101 and the
   stance buttons send. 1 the name filter is skipped for this layer pick: a listed task's state whose starter's return
   address (startRva, image offset) lies in the turret job's code (turret[0..nTurret), image addresses), or - a busy frame
   kept the settled pick without its starter (kind none, startRva 0) - keptIsSent: the kept name is the action the owner
   already sends. Anything else 0: the filter decides as before (a fight job's 'combatstance' is refused by name; a
   refuse-list starter's never reaches here - LayerStartSendable). */
/* T-890 TURRET (T1871): 1 a layer state's starter is the turret-manning job - a listed task's note whose return address
   (startRva, image offset) lies in the turret job's code pieces (turret[0..nTurret), image addresses); else 0 */
inline int TurretStarted(int kind, unsigned int startRva, unsigned long long imageBegin, const Range* turret, int nTurret)
{
    return (kind == kCallerTask && startRva != 0) ? InRanges(imageBegin + startRva, turret, nTurret) : 0;
}

inline int LayerNameFilterSkipped(int kind, unsigned int startRva, unsigned long long imageBegin, const Range* turret,
                                  int nTurret, int keptIsSent)
{
    if (kind == kCallerTask && startRva != 0) return TurretStarted(kind, startRva, imageBegin, turret, nTurret);
    if (kind == kCallerNone && startRva == 0) return keptIsSent != 0 ? 1 : 0;
    return 0;
}

/* THE REFUSE LIST (owner decision 1236 a). The five table rows AnimCtl* (spawn.cpp: idle chooser, walk / run blend, head
   turn, 0x5F0C10 - role unread, kept as background -, the main-slot fill, whose action the main-slot road sends) and these
   engine animation-update functions, each a caller of the layer play (T1695 install line, 1.0.68) whose layer-1 state is
   not a task's held action:
     0 base animation   1.0.65 0x51C130 / 1.0.68 0x51CBC0 - the base animation, every frame (UNKNOWN CALLERS READ)
     1 paired partner   0x51CA80 / 0x51D510 - plays the carried character's half of a two-character animation ('carry me',
                        return address 0x51D58D in 1.0.68): the state comes from being carried, not from an action the
                        carried character holds (the carrier's own lift 'pickup' is excluded too); sent as a pose, the
                        carried copy would be held at its owner's spot instead of following its carrier (Inferred)
     2 idle / stance    0x51CFF0 / 0x51DA80 - the random idle and guard-stance chooser ('guard 1h', 'guard polearm',
                        'guard5', 'MA idle1'; T1665, T1695 ret 0x51DCD4): background fidgets
     3 movement blend   0x51D3E0 / 0x51DE70 - blends movement states (UNKNOWN CALLERS READ)
     4 aim / reload     0x51DA50 / 0x51E4E0 - the ranged aim 'aimH' (ret 0x51E7DF) and the reload motion: T-330's own road
                        copies the aim; never a pose here
     5 held-slot replay 0x51DDF0 / 0x51E880 - replays the held action slot ('sitting chair', 'sitting idle', 'medic',
                        'chop down static'; ret 0x51E96E): that action is the main-slot road's to send; let through, the same
                        action would reach the copy by a second road
     6 arm poses        0x51F570 / 0x520000 - arm overlays (UNKNOWN CALLERS READ)
   Not on it (allowed; role unread): 0x3343F0 / 0x334430 (no caller found) - named by the 'layer send allowed unknown'
   line when it first flows. The bed's state animation is refused by its own entry below (RefuseBedStart).
   The engine's animation-update code moved as one block between Steam 1.0.65 and 1.0.68 (+0xA90 for every function above
   and for the AnimCtlIdle / LocoBlend / MainSlotFill rows), so each entry's start is the AnimCtlIdle row's start plus a fixed
   offset; the install checks each is a calling function of the layer play, else the hook stays off (every pick refused). */
const int kRefuseDerived = 7;
const int kRefuseDerivedDelta[kRefuseDerived] = { -0x7F0, 0x160, 0x6D0, 0xAC0, 0x1130, 0x14D0, 0x2C50 };
const char* const kRefuseDerivedName[kRefuseDerived] = { "RefuseBaseAnim", "RefusePairedPartner", "RefuseIdleStance",
                                                         "RefuseMoveBlend", "RefuseAimReload", "RefuseHeldSlotReplay",
                                                         "RefuseArmPoses" };

/* the start of derived refuse-list entry i from the AnimCtlIdle row's start: 0 no row, i out of range, or no valid start */
inline unsigned int RefuseDerivedStart(unsigned int idleRow, int i)
{
    if (idleRow == 0 || i < 0 || i >= kRefuseDerived) return 0;
    const long long s = (long long)idleRow + (long long)kRefuseDerivedDelta[i];
    if (s <= 0 || s > 0xFFFFFFFFll) return 0;
    return (unsigned int)s;
}

/* THE BED ENTRY: 1.0.65 0x33A170 / 1.0.68 0x33A1B0 plays the bed's state animation on the character's layer; the
   every-frame animation update calls it while the character is in a bed (+0x2F8 == 1, decomp_51fa70.txt:31-42). That
   state belongs to the bed, which the bed road sends; it is never a pose. This function moved by +0x40 between 1.0.65 and
   1.0.68, not with the animation-update block above, so its start is placed from the CombatClass_currentTarget row
   (1.0.65 0x33A2A0 / 1.0.68 0x33A2E0), 0x130 before it in both builds; the install checks it as it checks the entries
   above (a calling function of the layer play, else the hook stays off). */
const int kRefuseBedDelta = -0x130;
const char* const kRefuseBedName = "RefuseBedState";

/* the bed entry's start from the CombatClass_currentTarget row's start: 0 no row or no valid start */
inline unsigned int RefuseBedStart(unsigned int combatTargetRow)
{
    if (combatTargetRow == 0) return 0;
    const long long s = (long long)combatTargetRow + (long long)kRefuseBedDelta;
    if (s <= 0 || s > 0xFFFFFFFFll) return 0;
    return (unsigned int)s;
}

/* ---- the install's reads of the game's function table (pure over a copy of it) ---- */

/* one function-table entry: [begin, end) image offsets and the start of the function it belongs to (its own begin, or
   the entry its unwind chain leads back to for a separated piece). A table is sorted by begin, as the image's is. */
struct FnPiece
{
    unsigned int begin;
    unsigned int end;
    unsigned int root;
};

/* the index of the piece holding rva, -1 none */
inline int PieceFind(const FnPiece* p, int n, unsigned int rva)
{
    int lo = 0, hi = n - 1;
    while (lo <= hi)
    {
        const int mid = lo + (hi - lo) / 2;
        if (rva < p[mid].begin) hi = mid - 1;
        else if (rva >= p[mid].end) lo = mid + 1;
        else return mid;
    }
    return -1;
}

/* the ranges of every piece whose function starts at one of starts[0..ns), as image addresses (base + offset):
   the count written, -1 more than cap */
inline int CollectRanges(const FnPiece* p, int n, const unsigned int* starts, int ns, unsigned long long base, Range* out, int cap)
{
    int k = 0;
    for (int i = 0; i < n; ++i)
        for (int s = 0; s < ns; ++s)
        {
            if (starts[s] == 0 || p[i].root != starts[s]) continue;
            if (k >= cap) return -1;
            out[k].begin = base + p[i].begin;
            out[k].end = base + p[i].end;
            ++k;
        }
    return k;
}

/* the kind of a calling function from its start: a listed controller, a listed task, else unknown */
inline int KindOfFunction(unsigned int fn, const unsigned int* task, int nTask, const unsigned int* ctl, int nCtl)
{
    for (int i = 0; i < nCtl; ++i) if (ctl[i] != 0 && ctl[i] == fn) return kCallerController;
    for (int i = 0; i < nTask; ++i) if (task[i] != 0 && task[i] == fn) return kCallerTask;
    return kCallerUnknown;
}

/* the install's caller list: one row per calling function (fn 0 = a call site in no function-table entry) */
struct CallerTally
{
    unsigned int fn;
    int calls;
};

/* count one call site at rva into tally[0..*n), grouped by its function (PieceFind -> root): 0 the list is full */
inline int TallyCallSite(const FnPiece* p, int np, unsigned int rva, CallerTally* tally, int* n, int cap)
{
    const int at = PieceFind(p, np, rva);
    const unsigned int fn = (at >= 0) ? p[at].root : 0u;
    for (int i = 0; i < *n; ++i)
        if (tally[i].fn == fn) { ++tally[i].calls; return 1; }
    if (*n >= cap) return 0;
    tally[*n].fn = fn;
    tally[*n].calls = 1;
    ++*n;
    return 1;
}

/* THE START TABLE. Written by the layer-play detour on whatever thread the engine plays an animation (the main thread or
   the AI worker), read by the pose road on the main thread: fixed size, no allocation, no lock. Each slot carries a
   sequence number - even = settled, odd = one writer holds it; a writer takes a slot by moving it from even to odd with
   one compare-exchange and publishes with the next even number; a reader keeps a slot's fields only when the number was
   even and unchanged around its reads. A pair lives in one of kStartProbe slots from its hash: its own slot, else the
   first free one, else the oldest note that is not sendable, else the oldest sendable note (bounded: a full table pushes out
   a note, never grows; a task note written once outlives controller notes rewritten every frame). A call that would
   write what its slot already holds (same kind and caller, written less than kStartRefreshMs ago) writes nothing. */
const unsigned int kStartSlots = 4096;   /* a power of two */
const unsigned int kStartProbe = 8;
const int kStartTries = 4;               /* compare-exchange / unsettled-read retries before a call gives up */
const unsigned int kStartRefreshMs = 1000;   /* an unchanged note's age is refreshed at most this often */

struct StartSlot
{
    volatile long seq;
    volatile unsigned int stamp;          /* when it was last written (a GetTickCount value) */
    const void* volatile anim;            /* the AnimationClass (0 = free slot) */
    const void* volatile rec;             /* the AnimationData played */
    volatile long kind;                   /* kCallerTask / kCallerController */
    volatile unsigned int retRva;         /* the caller's return address in the image (0 = outside it) */
};

struct StartTable
{
    StartSlot s[kStartSlots];
};

/* StartNote's results */
enum { kNoteUpdated = 1, kNoteNew = 2, kNoteEvicted = 3, kNoteDropped = 0, kNoteSame = 4 };

inline long StartCas(volatile long* p, long want, long seen)
{
#if defined(_MSC_VER)
    return _InterlockedCompareExchange(p, want, seen);
#else
    return __sync_val_compare_and_swap(p, seen, want);
#endif
}
inline void StartPublish(volatile long* p, long v)
{
#if defined(_MSC_VER)
    _InterlockedExchange(p, v);
#else
    __sync_lock_test_and_set(p, v);
    __sync_synchronize();
#endif
}
inline long StartNext(long s) { return (long)((unsigned long)s + 1u); }

/* MAIN THREAD, before the detour is installed (StartForgetAll once it may run) */
inline void StartClear(StartTable* t)
{
    for (unsigned int i = 0; i < kStartSlots; ++i)
    {
        StartSlot& x = t->s[i];
        x.seq = 0; x.stamp = 0; x.anim = 0; x.rec = 0; x.kind = kCallerNone; x.retRva = 0;
    }
}

inline unsigned int StartHash(const void* anim, const void* rec)
{
    unsigned long long h = (unsigned long long)anim * 0x9E3779B97F4A7C15ull;
    h ^= (unsigned long long)rec + 0x632BE59BD9B4E019ull + (h << 6) + (h >> 2);
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 32;
    return (unsigned int)h & (kStartSlots - 1u);
}

/* ANY THREAD: note that a call of type kind (returning to retRva) played rec on anim at time now - the latest call wins.
   kNoteDropped: every slot of the pair's window stayed busy with other writers for kStartTries rounds (nothing written). */
inline int StartNote(StartTable* t, const void* anim, const void* rec, int kind, unsigned int retRva, unsigned int now)
{
    if (anim == 0) return kNoteDropped;
    const unsigned int h = StartHash(anim, rec);
    for (int attempt = 0; attempt < kStartTries; ++attempt)
    {
        int mine = -1, freeAt = -1, oldAt = -1, oldTaskAt = -1;
        long mineSeq = 0, freeSeq = 0, oldSeq = 0, oldTaskSeq = 0;
        unsigned int oldAge = 0, oldTaskAge = 0;
        for (unsigned int i = 0; i < kStartProbe; ++i)
        {
            const unsigned int at = (h + i) & (kStartSlots - 1u);
            StartSlot& x = t->s[at];
            const long s1 = x.seq;
            if ((s1 & 1) != 0) continue;
            const void* a = x.anim;
            const void* r = x.rec;
            const unsigned int st = x.stamp;
            const long k = x.kind;
            const unsigned int rv = x.retRva;
            if (x.seq != s1) continue;
            if (a == anim && r == rec)
            {
                if (k == kind && rv == retRva && now - st < kStartRefreshMs) return kNoteSame;
                mine = (int)at; mineSeq = s1; break;
            }
            if (a == 0) { if (freeAt < 0) { freeAt = (int)at; freeSeq = s1; } continue; }
            const unsigned int age = now - st;
            if (LayerStartSendable((int)k)) { if (oldTaskAt < 0 || age > oldTaskAge) { oldTaskAt = (int)at; oldTaskSeq = s1; oldTaskAge = age; } }
            else if (oldAt < 0 || age > oldAge) { oldAt = (int)at; oldSeq = s1; oldAge = age; }
        }
        int at = -1, result = kNoteDropped;
        long seen = 0;
        if (mine >= 0) { at = mine; seen = mineSeq; result = kNoteUpdated; }
        else if (freeAt >= 0) { at = freeAt; seen = freeSeq; result = kNoteNew; }
        else if (oldAt >= 0) { at = oldAt; seen = oldSeq; result = kNoteEvicted; }
        else if (oldTaskAt >= 0) { at = oldTaskAt; seen = oldTaskSeq; result = kNoteEvicted; }
        if (at < 0) continue;
        StartSlot& x = t->s[at];
        if (StartCas(&x.seq, StartNext(seen), seen) != seen) continue;   /* another writer took it: look again */
        x.anim = anim;
        x.rec = rec;
        x.kind = kind;
        x.retRva = retRva;
        x.stamp = now;
        StartPublish(&x.seq, StartNext(StartNext(seen)));
        return result;
    }
    return kNoteDropped;
}

/* MAIN THREAD (any thread is safe): the caller type last noted for (anim, rec), kCallerNone when there is no settled note;
   *retRva its caller (0 none) */
inline int StartFind(const StartTable* t, const void* anim, const void* rec, unsigned int* retRva)
{
    if (retRva != 0) *retRva = 0;
    if (anim == 0) return kCallerNone;
    const unsigned int h = StartHash(anim, rec);
    for (unsigned int i = 0; i < kStartProbe; ++i)
    {
        const StartSlot& x = t->s[(h + i) & (kStartSlots - 1u)];
        for (int tries = 0; tries < kStartTries; ++tries)
        {
            const long s1 = x.seq;
            if ((s1 & 1) != 0) continue;
            const void* a = x.anim;
            const void* r = x.rec;
            const long k = x.kind;
            const unsigned int rv = x.retRva;
            if (x.seq != s1) continue;
            if (a == anim && r == rec)
            {
                if (retRva != 0) *retRva = rv;
                return (k == kCallerTask || k == kCallerController || k == kCallerUnknown) ? (int)k : kCallerNone;
            }
            break;   /* a settled slot holding another pair */
        }
    }
    return kCallerNone;
}

struct State
{
    int layer;
    float weight;
    int looped;
    int turret;   /* T-890 TURRET: 1 the turret-manning job started it (TurretStarted); read only for a kTurretLayerMask layer */
};

/* the index of the state to use, -1 none. A state on a layerMask layer, or - T-890 TURRET - one on a kTurretLayerMask layer
   that the turret job started; then repeating and at least minWeight, as before */
inline int LayerPick(const State* s, int n, unsigned int layerMask, float minWeight)
{
    int best = -1;
    for (int i = 0; i < n; ++i)
    {
        if (s[i].layer < 0 || s[i].layer > 31) continue;
        const unsigned int bit = 1u << s[i].layer;
        if ((layerMask & bit) == 0 && !(s[i].turret != 0 && (kTurretLayerMask & bit) != 0)) continue;
        if (s[i].looped == 0) continue;
        const float w = s[i].weight;
        if (!(w >= minWeight)) continue;   /* NaN fails */
        if (best < 0 || w > s[best].weight) best = i;
    }
    return best;
}

struct Cand
{
    char name[kStrMax + 1];
    unsigned int since;   /* when this name became the pick (a GetTickCount value) */
    int active;           /* 1 it has held kSettleMs: it is the pose */
};

inline void CandClear(Cand* c) { c->name[0] = 0; c->since = 0; c->active = 0; }

/* one frame's pick (name "" = none) at time now: 1 the name is the pose now, 0 not (yet) */
inline int CandStep(Cand* c, const char* name, unsigned int now, unsigned int settleMs)
{
    if (name == 0 || name[0] == 0) { CandClear(c); return 0; }
    unsigned int n = 0;
    while (n < kStrMax && name[n] != 0) ++n;
    if (name[n] != 0) { CandClear(c); return 0; }   /* too long: not a pose */
    bool same = true;
    for (unsigned int i = 0; i <= n; ++i) if (c->name[i] != name[i]) { same = false; break; }
    if (!same)
    {
        for (unsigned int i = 0; i <= n; ++i) c->name[i] = name[i];
        c->since = now;
        c->active = 0;
        return 0;
    }
    if (c->active) return 1;
    if (now - c->since < settleMs) return 0;
    c->active = 1;
    return 1;
}

/* ANY THREAD (a world's teardown): every note forgotten, each slot taken as a writer takes it, so a detour writing at the
   same time is never torn; a slot another writer holds through kStartTries tries is left (its note is the newest). The
   count of slots left. */
inline int StartForgetAll(StartTable* t)
{
    int left = 0;
    for (unsigned int i = 0; i < kStartSlots; ++i)
    {
        StartSlot& x = t->s[i];
        int done = 0;
        for (int tries = 0; tries < kStartTries && done == 0; ++tries)
        {
            const long s1 = x.seq;
            if ((s1 & 1) != 0) continue;
            if (x.anim == 0) { done = 1; break; }
            if (StartCas(&x.seq, StartNext(s1), s1) != s1) continue;
            x.anim = 0; x.rec = 0; x.kind = kCallerNone; x.retRva = 0; x.stamp = 0;
            StartPublish(&x.seq, StartNext(StartNext(s1)));
            done = 1;
        }
        if (done == 0) ++left;
    }
    return left;
}

}   // namespace poselayer

#endif
