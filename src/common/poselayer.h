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
   2. SETTLE (CandStep): a picked name counts only after it has stayed the pick for kSettleMs - a blend between two states
      (a stop, an action ending) is never sent as a pose. A different name, or no pick, starts over.

   3. WHO STARTED IT (StartTable, LayerStartSendable): the picked state is a pose only when a TASK put it in the layer table.
      The engine puts an animation into a layer through one function (AnimationClass layer play, Steam 1.0.65 0x5B7030),
      called by the layer-only tasks ('medic' 0x34D8D0, 'chop down static' 0x353450, ...) and by the animation update's own
      controllers (idle chooser 0x51C920, walk / run blend 0x51CE50, head turn 0x5B79A0, 0x5F0C10, and the main-slot fill
      0x51E020, whose animation the main-slot road already sends). A layer state keeps no 'who started me' field, so a
      detour on the layer play notes, per (animation object, animation record), whether its caller lies inside a listed
      task's code, a listed controller's code, or neither (CallerKind) - the latest call wins. Layer 1 also holds looped
      upper-body locomotion ('jog upper' 1.00 looped - T1630, Confirmed), which the walk / run blend plays (Inferred from
      its decomp). The listed tasks are code; the animation names are not listed, so a mod's animation played by one of
      them is a pose. An unknown caller, or no note (the hook off, a state started before it, a note pushed out), refuses.

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

/* 1 a layer state with this caller type may be sent as a pose: only a task's; a controller's, an unknown caller's or none
   (no note) never */
inline int LayerStartSendable(int kind) { return kind == kCallerTask ? 1 : 0; }

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
   first free one, else the oldest note that is not a task's, else the oldest task note (bounded: a full table pushes out
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
            if (k == kCallerTask) { if (oldTaskAt < 0 || age > oldTaskAge) { oldTaskAt = (int)at; oldTaskSeq = s1; oldTaskAge = age; } }
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
};

/* the index of the state to use, -1 none */
inline int LayerPick(const State* s, int n, unsigned int layerMask, float minWeight)
{
    int best = -1;
    for (int i = 0; i < n; ++i)
    {
        if (s[i].layer < 0 || s[i].layer > 31 || (layerMask & (1u << s[i].layer)) == 0) continue;
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
