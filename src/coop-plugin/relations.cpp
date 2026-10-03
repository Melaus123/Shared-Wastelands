// relations.cpp - see relations.h. Engine facts (Read 2026-09-02, decompiled; RVAs from the DLL's exports):
//   FactionRelations: +0x08 owner Faction*, +0x20 unordered_map<Faction*, RelationData>, +0x60 default relation;
//   RelationData (node+8): +0 ally flag, +2 atWar flag, +4 relation, +8 trust, +0xC trustNeg (save keys "relation",
//   "trust", "trustNeg"). getRelationData 0x6B4910 returns the entry (created at the default when absent).
//   Changers: affectRelations(f, amount, mult) 0x6B2B50 (returns float); the by-event overload 0x6B29D0 (does not call
//   the first); declareWar 0x6B2CB0; setNoLongerEnemies 0x6B2C40; setEnemy 0x6B2E40; setRelation 0x6B4A30;
//   affectTrust 0x6B2210. Thresholds: enemy <= -30, ally >= +50 (or the ally flag).
//   Faction: +0x240 GameData* (stringID at GameData+0x58), +0x250 PlayerInterface* (the player faction), +0x78 relations.
#include "relations.h"
#include "addresses.h"   /* P8h: kXxxRva below is filled from the address table, not hard-coded */
#include "soak.h"   /* P7f (review-p6z M-3): GameplayRunning - the precondition the snapshot retry is actually waiting for */
#include "store.h"   /* ally1: EngineWritesBlocked - the ally switch is an engine write */
#include "playerfaction.h"
#include "tags.h"   /* TagsCaptionsDirty: a standing between this game and another player colours that player's name tags */
#include "../common/nametag.h"   /* the name tag's relation levels */
#include "ownstore.h"   /* mmo3: OwnNoteFactionChange, OwnWriterOn */
#include "../common/ownrec.h"   /* mmo3: the pp.faction record */
#include "../common/slotwire.h"   /* stand1: @slot:<n> wire names */
#include "../common/worldrelwire.h"   /* par24: the notebook's world-vs-world table (WORLD_REL) */
#include "net/session.h"
#include "../common/liveenvelope.h"   /* M7a fold F1: PeerInWorldAskDue */
#include "coop_log.h"
#include "game/GameWorld.h"
#include "game/Faction.h"
#include "game/FactionRelations.h"
#include "hooks.h"   /* mig1: coop::AddHook (own MinHook) */
#define WIN32_LEAN_AND_MEAN   /* without it rpcndr.h defines `small`, a field name below */
#include <Windows.h>
#include <sstream>
#include <string>
#include <vector>
#include <set>   /* par24 */

namespace {
unsigned long long kAffectRva = 0; static coop::AddrReg kAffectRva_reg("Affect", &kAffectRva);   /* P8h: the address table fills this. Steam_1.0.65 0x6B2B50 */
unsigned long long kAffectEvRva = 0; static coop::AddrReg kAffectEvRva_reg("AffectEv", &kAffectEvRva);   /* P8h: the address table fills this. Steam_1.0.65 0x6B29D0 */
unsigned long long kDeclareWarRva = 0; static coop::AddrReg kDeclareWarRva_reg("DeclareWar", &kDeclareWarRva);   /* P8h: the address table fills this. Steam_1.0.65 0x6B2CB0 */
unsigned long long kNoLongerRva = 0; static coop::AddrReg kNoLongerRva_reg("NoLonger", &kNoLongerRva);   /* P8h: the address table fills this. Steam_1.0.65 0x6B2C40 */
unsigned long long kSetEnemyRva = 0; static coop::AddrReg kSetEnemyRva_reg("SetEnemy", &kSetEnemyRva);   /* P8h: the address table fills this. Steam_1.0.65 0x6B2E40 */
unsigned long long kSetRelationRva = 0; static coop::AddrReg kSetRelationRva_reg("SetRelation", &kSetRelationRva);   /* P8h: the address table fills this. Steam_1.0.65 0x6B4A30 */
unsigned long long kAffectTrustRva = 0; static coop::AddrReg kAffectTrustRva_reg("AffectTrust", &kAffectTrustRva);   /* P8h: the address table fills this. Steam_1.0.65 0x6B2210 */
unsigned long long kGetDataRva = 0; static coop::AddrReg kGetDataRva_reg("GetRelData", &kGetDataRva);   /* P8h: the address table fills this. Steam_1.0.65 0x6B4910 */
typedef float (*AffectFn)(void* self, ::Faction* f, float amount, float mult);
typedef void  (*AffectEvFn)(void* self, ::Faction* f, int ev, float mult);
typedef void  (*OneFn)(void* self, ::Faction* f);
typedef void  (*SetRelFn)(void* self, ::Faction* f, float v);
typedef void  (*TrustFn)(void* self, ::Faction* f, float amount, float mult);
typedef void* (*GetDataFn)(void* self, ::Faction* f);
AffectFn orig_affect = 0; AffectEvFn orig_affectEv = 0; OneFn orig_declareWar = 0, orig_noLonger = 0, orig_setEnemy = 0;
SetRelFn orig_setRelation = 0; TrustFn orig_affectTrust = 0;

bool g_on = true, g_installed = false, g_applying = false, g_wasLinked = false;
/* E38: the snapshot latch and its deferral count. `g_snapshotSent` is lowered on the link-DOWN edge, so a
   reconnect re-sends; `g_relationsSnapshotDeferredFrames` counts PUMP FRAMES in which the snapshot was owed and could not be
   taken, which is the number that distinguishes "there was nothing to send" from "we never got round to it". */
long g_relKeySess = -1, g_relKeyLive = -1; int g_relPeerRoad = -1; bool g_relSyncOwed = false;   /* M5a fold 1: the link key the snapshot and RELSYNC were last owed on, the road to the session peer, and whether a RELSYNC is owed */
cooplive::PeerWorldWatch g_relPeerWatch; long long g_relPeerInWorldAsks = 0;   /* M7a fold F1: the session peer's roster row, and the asks its entries into the world owed */
long long g_relSyncAsked = 0, g_relKeyChanges = 0, g_relRoadSnapshots = 0, g_relDroppedResync = 0;
bool g_snapshotSent = false;
long long g_relationsSnapshotDeferredFrames = 0;   /* P7f (review-p6z M-3): FRAMES, and the name says so - it is incremented once per pump tick, and the pump it rides runs at ~900 Hz at the title screen */
long long g_legacySkipped = 0;   /* stand1: pairs naming a protocol-67 `coop-peer` faction a save still carries - never forwarded */
long long g_refusedNotSender = 0;   /* M5b: pairs refused because they belong to another player than the sender (must read 0) */ long long g_refusedOwnedHere = 0;   /* rel1: remote writes refused because this game owns the pair (must read 0) */
long long g_changes = 0, g_forwarded = 0, g_notOwned = 0, g_echoSuppressed = 0, g_received = 0, g_applied = 0, g_unresolved = 0,
          g_snapshots = 0, g_snapshotEntries = 0, g_faults = 0, g_sendFailed = 0, g_offNoLink = 0;
std::string g_lastForward, g_lastApplied;

template <class T> std::string S(const T& v) { std::ostringstream o; o << v; return o.str(); }
bool Plaus(const void* p) { return p != 0 && (uintptr_t)p > 0x10000 && (uintptr_t)p < 0x00007FFFFFFFFFFFull; }
uintptr_t Base() { return (uintptr_t)::GetModuleHandleA(0); }

int CopyStdStringPod(const void* s, char* buf, int cap)
{
    __try
    {
        const size_t len = *(const size_t*)((const char*)s + 0x10);
        const size_t res = *(const size_t*)((const char*)s + 0x18);
        const char* p = (res >= 16) ? *(const char* const*)s : (const char*)s;
        size_t n = len; if (n > (size_t)(cap - 1)) n = (size_t)(cap - 1);
        for (size_t i = 0; i < n; ++i) buf[i] = p[i];
        buf[n] = 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int ReadSidPod(const ::Faction* f, char* buf, int cap)
{
    const void* gd = 0;
    __try { gd = *(const void* const*)((const char*)f + 0x240); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (!Plaus(gd)) return 0;
    return CopyStdStringPod((const char*)gd + 0x58, buf, cap);
}
// stand1 (docs/design-profiles1.md s2 Required 2): the wire name of a faction. A PLAYER faction travels by notebook SLOT - mine
// as "@slot:<my slot>:<my faction's name>" (the name lets the other game follow a rename), a stand-in as "@slot:<its slot>" -
// and every other faction by its stringID. Slots are absolute, so one pair has one name on both games (protocol 67's
// "@player:" / "@peer" meant opposite things at the two ends and had to be swapped). A player faction with no slot yet has
// no wire name: Snapshot waits for one, and a live change meanwhile counts as a fault.
std::string SidOf(::Faction* f)
{
    if (!Plaus(f)) return std::string();
    if (coop::IsPeerFaction(f)) return coopslot::SlotWire(coop::StandInSlotOf(f), std::string());
    if (coop::IsPlayerFaction(f)) { const int me = coop::MySlotForWire(); return me < 0 ? std::string() : coopslot::SlotWire(me, f->getName()); }
    char buf[160];
    if (!ReadSidPod(f, buf, 160)) return std::string();
    return std::string(buf);
}
// "@slot:<n>[:<name>]" on THIS game: my slot is my player faction; another slot is that slot's stand-in - created (the CreatePeer
// path) only when the name came with it, i.e. the sender's own player faction.
::Faction* FactionOfSlot(const std::string& sid, bool mayCreate)
{
    int n = -1; bool hasName = false;
    if (!coopslot::ParseSlotWire(sid, &n, 0, &hasName) || n < 0) return 0;
    if (n == coop::MySlotForWire()) return coop::LocalPlayerFaction();
    if (mayCreate && hasName) return coop::ResolveWireFaction(sid);
    return coop::StandInForSlot(n);
}
::Faction* FactionOf(const std::string& sid)
{
    if (sid.empty() || !Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) return 0;
    if (coopslot::IsSlotWire(sid)) return FactionOfSlot(sid, true);
    return coop::GameWorldPtr()->factionDirectory->findFactionById(sid);
}
// the same names produced by SidOf on THIS machine (the off-thread queue): never creates a stand-in
::Faction* FactionOfLocal(const std::string& sid)
{
    if (sid.empty() || !Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) return 0;
    if (coopslot::IsSlotWire(sid)) return FactionOfSlot(sid, false);
    return coop::GameWorldPtr()->factionDirectory->findFactionById(sid);
}
struct Entry { float relation, trust, trustNeg; unsigned flags; };
int ReadEntry(void* rel, ::Faction* other, Entry* e)
{
    __try
    {
        GetDataFn get = (GetDataFn)(Base() + kGetDataRva);
        char* d = (char*)get(rel, other);
        if (!Plaus(d)) return 0;
        e->relation = *(float*)(d + 4); e->trust = *(float*)(d + 8); e->trustNeg = *(float*)(d + 0xC);
        e->flags = (d[0] ? 1u : 0u) | (d[2] ? 2u : 0u);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int WriteEntry(void* rel, ::Faction* other, const Entry& e)
{
    __try
    {
        GetDataFn get = (GetDataFn)(Base() + kGetDataRva);
        char* d = (char*)get(rel, other);
        if (!Plaus(d)) return 0;
        *(float*)(d + 4) = e.relation; *(float*)(d + 8) = e.trust; *(float*)(d + 0xC) = e.trustNeg;
        d[0] = (e.flags & 1u) ? 1 : 0; d[2] = (e.flags & 2u) ? 1 : 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int ReadFactionArrayPod(void*** arr, unsigned* n)
{
    __try { *arr = *(void***)((char*)coop::GameWorldPtr()->factionDirectory + 0x10); *n = *(unsigned*)((char*)coop::GameWorldPtr()->factionDirectory + 8); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void* RelationsOfPod(::Faction* f)
{
    __try { return f->relations; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
::Faction* OwnerOf(void* rel)
{
    __try { return *(::Faction**)((char*)rel + 8); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* A standing between this game's player faction and another player's stand-in colours that player's name tags: they are
   re-coloured on the next frame. ANY THREAD - pointer compares, a guarded read and an interlocked bump. */
void TagNoteRelation(::Faction* a, ::Faction* b)
{
    if (!Plaus(a) || !Plaus(b)) return;
    if ((coop::IsPeerFaction(a) && coop::IsPlayerFaction(b)) || (coop::IsPeerFaction(b) && coop::IsPlayerFaction(a))) coop::TagsCaptionsDirty();
}
// Ownership: a standing involving MY player faction is mine; one involving the peer's is theirs; world-vs-world is the host's.
// review-p3-factions: the OWNER side decides first, so a player<->player pair has exactly one owner per direction
// (mine->peer is mine, peer->mine is theirs); then the other side; world-vs-world is the host's.
bool Owned(::Faction* a, ::Faction* b)
{
    if (coop::IsPlayerFaction(a)) return true;
    if (coop::IsPeerFaction(a)) return false;
    if (coop::IsPlayerFaction(b)) return true;
    if (coop::IsPeerFaction(b)) return false;
    return false;   /* par24 (decision 48): world-vs-world is no game's - it was `SessionIsHost()`; WorldPair routes it to the notebook */
}
/* par24 (parity P24): a pair with neither a player faction nor a player's stand-in on either side is WORLD-VS-WORLD. Its record
   is the NOTEBOOK's table (src/common/worldrelwire.h), not any one game's: a change this game's engine makes to it goes UP to
   the notebook (WorldRelNote -> WorldRelTick), and the notebook's rows are written here at the K2 safe point
   (WorldRelSafePointDrain). Player pairs keep the rules above and never become rows (decision 20 is untouched).
   review-par24 #1: IsPeerFaction knows only the stand-ins met THIS session (the table is emptied at every world load), so an
   offline player's stand-in coop-p<n> - and a protocol-67 coop-peer - is excluded by its RECORD id too (StandInRecordSlot:
   -1 = an ordinary id; a slot, -2 coop-peer or -3 unreadable = not a world pair). */
bool WorldPair(::Faction* a, ::Faction* b)
{
    return !coop::IsPlayerFaction(a) && !coop::IsPeerFaction(a) && !coop::IsPlayerFaction(b) && !coop::IsPeerFaction(b)
        && coop::StandInRecordSlot(a) == -1 && coop::StandInRecordSlot(b) == -1;
}
DWORD g_mainThread = 0; volatile LONG64 g_offThread = 0, g_queued = 0, g_queueDropped = 0; long long g_drained = 0, g_drainUnresolved = 0;
// F483: the relation changers fire mostly on the AI worker thread (254/197 per run). The wire and the maps are main-thread only,
// so an off-thread change is captured as POD (the entry is read there and then - the engine has just written it) and queued under
// a lock; the tick drains the queue and forwards. Pointers are not kept: the sids are read at capture time (SidOf is string work
// on the plugin's own heap, allocator-safe off-thread).
struct Queued { std::string a, b; unsigned reason; };   // review-p3n F4: sids only - the drain re-reads the entry live
// review-p3p #5: the last value the OWNER sent for a pair we do not own - when our own engine moves that entry (it sees the
// peer's puppets act), it is put back at once (main thread) or on the next tick (worker thread); no periodic re-send needed.
std::map<std::string, Entry> g_ownerValues;              // key: ownerSid + "|" + otherSid (RECEIVER-side sids as they arrived)
std::vector<Queued> g_revertQueue;                        // off-thread notOwned changes, drained on the tick
long long g_reverted = 0, g_revertQueued = 0, g_revertNoValue = 0;
std::string PairKey(const std::string& a, const std::string& b) { return a + "|" + b; }
// the key under which a pair this game does not own ARRIVED. stand1: slots are absolute, so it is this game's own names in
// key form (coopslot::SlotWireKey drops the player's name); protocol 67's marker swap is gone.
std::string RecvKeyOfLocal(::Faction* owner, ::Faction* other)
{
    return PairKey(coopslot::SlotWireKey(SidOf(owner)), coopslot::SlotWireKey(SidOf(other)));
}
// P081 (rel4): which pairs this game's engine moved and the mod put back - per pair: a count, the sum of the moves, and the
// first 3 lines with a move of at least 0.01 (T318: 40 lines of 1e-5 drift hid everything else)
struct P081Pair { long long n; long long small; double sum; int lines; };
std::map<std::string, P081Pair> g_p081;
int RevertPair(::Faction* owner, ::Faction* other)
{
    std::map<std::string, Entry>::const_iterator it = g_ownerValues.find(RecvKeyOfLocal(owner, other));
    if (it == g_ownerValues.end()) { ++g_revertNoValue; return 0; }
    void* rel = RelationsOfPod(owner); if (!Plaus(rel)) return -1;
    Entry engine; const int rd = ReadEntry(rel, other, &engine);   // P081: the value the engine just wrote
    g_applying = true; const int w = WriteEntry(rel, other, it->second); g_applying = false;
    if (w == 1) { ++g_reverted; TagNoteRelation(owner, other); }
    if (w == 1 && rd == 1 && engine.relation != it->second.relation)
    {
        const std::string pair = SidOf(owner) + "->" + SidOf(other);
        const float d = engine.relation - it->second.relation;
        P081Pair& pp = g_p081[pair];
        ++pp.n; pp.sum += d;
        if (d < 0.01f && d > -0.01f) ++pp.small;
        else if (pp.lines < 3 && g_p081.size() <= 20)
        {
            ++pp.lines;
            DebugLog("[REL] P081 reverted " + pair + " engine=" + S(engine.relation) + " owner=" + S(it->second.relation)
                     + " (this game's engine moved a pair the other game owns; the owner's value was put back)");
        }
    }
    return w;
}
CRITICAL_SECTION g_queueLock; bool g_queueLockInit = false;
std::vector<Queued> g_queue;
/* ---- par24: THE WORLD-VS-WORLD TABLE (the notebook's record; src/common/worldrelwire.h) ---------------------------------
   MAIN THREAD unless marked. g_wr is the notebook's rows as they arrived (a WELCOME empties it, its push refills it, every
   row the notebook takes arrives again); g_wrPending the rows still to be written here (the K2 safe point writes them);
   g_wrDirty the world pairs this game's engine moved since the last tick (sent up as CHANGE). A change made before this
   game has written the notebook's table over its save's values is not sent (wrChangeBeforeTable): it rests on the save's
   value, which the table is about to replace. */
coopwrel::Table g_wr;
std::set<std::string> g_wrPending, g_wrDirty, g_wrPendingChange;   /* g_wrPendingChange: pending rows that came down as another game's engine CHANGE (logged when written) */
std::vector<std::string> g_wrOffQueue;              /* any thread, under g_queueLock: off-thread engine changes (keys) */
std::vector<coopwrel::WireRow> g_wrSeedOut;         /* the SEED rows the walk found, sent 8 chunks a tick */
bool g_wrApplyAll = true, g_wrTableApplied = false;
int g_wrSeedState = 0;                               /* 0 owed (after the table is written here), 1 sending, 2 done */
long long g_wrRowsIn = 0, g_wrApplied = 0, g_wrSame = 0, g_wrUnresolved = 0, g_wrApplyFaults = 0, g_wrTableApplies = 0,
          g_wrChanges = 0, g_wrChangeBeforeTable = 0, g_wrSkipped = 0, g_wrDirtyDropped = 0, g_wrChangeRowsSent = 0,
          g_wrSendFailed = 0, g_wrSeedWalks = 0, g_wrSeedHeldHere = 0, g_wrSeedKnown = 0, g_wrSeedOffered = 0, g_wrSeedSent = 0,
          g_wrWalkFaults = 0, g_wrOnSession = 0, g_wrMalformed = 0, g_wrOffQueued = 0, g_wrOffDropped = 0, g_wrAppliedLines = 0,
          g_wrTestSets = 0, g_wrHeldForOwn = 0, g_wrEchoes = 0, g_wrUndone = 0;
std::string g_wrLastApplied, g_wrLastSent, g_wrLastTest;
coopwrel::SendBook g_wrSent;                         /* review-par24 #2: this game's CHANGE sends the notebook has not answered (ECHO), per pair */
bool g_wrSeedEndSent = false;                        /* S2-62: this walk's SEED_END has gone up */
std::map<std::string, float> g_wrUndo;               /* review-par24 #4: key -> the relation before the first `worldrel test/set` of that pair */
bool WorldSidOk(const std::string& a, const std::string& b)
{
    return coopwrel::SidOk(a) && coopwrel::SidOk(b) && a != b && !coopslot::IsLegacyPeerId(a) && !coopslot::IsLegacyPeerId(b);
}
void WorldRelNote(::Faction* owner, ::Faction* other)
{
    ++g_wrChanges;
    if (!g_wrTableApplied) { ++g_wrChangeBeforeTable; return; }
    const std::string a = SidOf(owner), b = SidOf(other);
    if (!WorldSidOk(a, b)) { ++g_wrSkipped; return; }
    if (g_wrDirty.size() < (size_t)coopwrel::kWrMaxTable) g_wrDirty.insert(coopwrel::Key(a, b)); else ++g_wrDirtyDropped;
}
/* any thread (QueueOffThread): the sids now, the entry is read on the tick */
void WorldRelQueueOff(::Faction* owner, ::Faction* other)
{
    const std::string a = SidOf(owner), b = SidOf(other);
    if (!WorldSidOk(a, b)) { ++g_wrSkipped; return; }
    if (!g_queueLockInit) { ++g_wrOffDropped; return; }
    ::EnterCriticalSection(&g_queueLock);
    if (g_wrOffQueue.size() < 4096) { g_wrOffQueue.push_back(coopwrel::Key(a, b)); ++g_wrOffQueued; } else ++g_wrOffDropped;
    ::LeaveCriticalSection(&g_queueLock);
}
/* MAIN THREAD: the off-thread world-pair changes, into the dirty set (the tick's DrainQueue, and the K2 safe point before it
   writes the notebook's rows - review-par24 #2: an off-thread change made since the tick is this game's newer value too) */
void WorldRelTakeOffQueue()
{
    if (!g_queueLockInit) return;
    std::vector<std::string> wk;
    ::EnterCriticalSection(&g_queueLock); wk.swap(g_wrOffQueue); ::LeaveCriticalSection(&g_queueLock);
    for (size_t i = 0; g_on && i < wk.size(); ++i)
    {
        ++g_wrChanges;
        if (!g_wrTableApplied) { ++g_wrChangeBeforeTable; continue; }
        if (g_wrDirty.size() < (size_t)coopwrel::kWrMaxTable) g_wrDirty.insert(wk[i]); else ++g_wrDirtyDropped;
    }
}
void QueueOffThread(void* rel, ::Faction* other, unsigned reason)
{
    ::Faction* owner = OwnerOf(rel);
    if (!Plaus(owner) || !Plaus(other)) { ++g_faults; return; }
    if (WorldPair(owner, other)) { WorldRelQueueOff(owner, other); return; }   /* par24: the notebook's, not the session's */
    if (!Owned(owner, other))
    {
        ++g_notOwned;
        Queued rq; rq.a = SidOf(owner); rq.b = SidOf(other); rq.reason = 2;   // review-p3p #5: revert on the tick
        if (g_queueLockInit) { ::EnterCriticalSection(&g_queueLock); if (g_revertQueue.size() < 4096) { g_revertQueue.push_back(rq); ++g_revertQueued; } ::LeaveCriticalSection(&g_queueLock); }
        return;
    }
    Queued q; q.a = SidOf(owner); q.b = SidOf(other); q.reason = reason;   // no engine map access off-thread beyond the engine's own
    if (coopslot::IsLegacyPeerId(q.a) || coopslot::IsLegacyPeerId(q.b)) { ++g_legacySkipped; return; }   /* stand1 */
    if ((q.a.empty() || q.b.empty()) && coop::MySlotForWire() < 0) { coop::NoteHeldForSlot(); return; }   /* stand1 fold (1d): held - the snapshot sent when my slot arrives carries it */
    if (q.a.empty() || q.b.empty()) { ++g_faults; return; }
    if (!g_queueLockInit) { ::InterlockedIncrement64(&g_queueDropped); return; }
    ::EnterCriticalSection(&g_queueLock);
    if (g_queue.size() < 4096) { g_queue.push_back(q); ::InterlockedIncrement64(&g_queued); } else ::InterlockedIncrement64(&g_queueDropped);
    ::LeaveCriticalSection(&g_queueLock);
}
void DrainQueue()
{
    if (!g_queueLockInit) return;
    std::vector<Queued> local, reverts;
    ::EnterCriticalSection(&g_queueLock); local.swap(g_queue); reverts.swap(g_revertQueue); ::LeaveCriticalSection(&g_queueLock);
    for (size_t i = 0; g_on && i < reverts.size(); ++i) { ::Faction* o = FactionOfLocal(reverts[i].a); ::Faction* t = FactionOfLocal(reverts[i].b); if (Plaus(o) && Plaus(t)) RevertPair(o, t); }   // review-p3r M5
    WorldRelTakeOffQueue();   /* par24: the off-thread world-pair changes, into the tick's dirty set */
    if (local.empty()) return;
    if (!g_on || (!coop::net::SessionLinked() && !coop::StoreLiveReady())) { g_offNoLink += (long long)local.size(); return; }   /* M5a fold 1 (#4): a notebook-only game sends too */
    for (size_t i = 0; i < local.size(); ++i)
    {
        const Queued& q = local[i];
        ::Faction* owner = FactionOfLocal(q.a); ::Faction* other = FactionOfLocal(q.b);   // sender-side sids (T173: FactionOf swapped the players)
        if (!Plaus(owner) || !Plaus(other)) { ++g_drainUnresolved; continue; }   // renamed or freed since capture
        void* rel = RelationsOfPod(owner); if (!Plaus(rel)) { ++g_faults; continue; }
        Entry e; if (ReadEntry(rel, other, &e) != 1) { ++g_faults; continue; }   // main thread, the newest value
        if (coop::net::SendRelation(q.a, q.b, e.relation, e.trust, e.trustNeg, e.flags, q.reason)) { ++g_forwarded; ++g_drained; g_lastForward = q.a + "->" + q.b + "=" + S(e.relation) + " (queued)"; }
        else ++g_sendFailed;
    }
}
/* mmo3: the pp.faction record's dirty mark - any of the seven detoured writers moved a pair with this game's player faction
   on either side (any thread: two plain reads and an interlocked store) */
void OwnFacNote(void* rel, ::Faction* other)
{
    if (!coop::OwnWriterOn()) return;
    ::Faction* owner = Plaus(rel) ? OwnerOf(rel) : 0;
    if ((Plaus(owner) && coop::IsPlayerFaction(owner)) || (Plaus(other) && coop::IsPlayerFaction(other))) coop::OwnNoteFactionChange();
}
void Forward(void* rel, ::Faction* other, unsigned reason)
{
    if (reason == 0) OwnFacNote(rel, other);   /* mmo3: every detoured writer comes through here with reason 0 (the snapshot passes 1) */
    if (reason == 0) TagNoteRelation(Plaus(rel) ? OwnerOf(rel) : 0, other);
    if (g_applying) { ++g_echoSuppressed; return; }
    if (g_mainThread != 0 && ::GetCurrentThreadId() != g_mainThread) { ::InterlockedIncrement64(&g_offThread); if (g_on) QueueOffThread(rel, other, reason); return; }
    ++g_changes;
    if (!g_on) return;
    ::Faction* owner = OwnerOf(rel);
    if (!Plaus(owner) || !Plaus(other)) { ++g_faults; return; }
    if (WorldPair(owner, other)) { WorldRelNote(owner, other); return; }   /* par24: world-vs-world goes to the notebook, never on the session link */
    if (!Owned(owner, other)) { ++g_notOwned; if (g_on) RevertPair(owner, other); return; }   // review-p3p #5: put the owner's value back now
    if (!coop::net::SessionLinked() && !coop::StoreLiveReady()) { ++g_offNoLink; return; }   /* M5a fold 1 (#4): a game linked only to the notebook (a third player) sends its standings too - session.cpp picks the road(s), one delivery per destination */
    Entry e; if (ReadEntry(rel, other, &e) != 1) { ++g_faults; return; }
    const std::string a = SidOf(owner), b = SidOf(other);
    if (coopslot::IsLegacyPeerId(a) || coopslot::IsLegacyPeerId(b)) { ++g_legacySkipped; return; }   /* stand1: a save's protocol-67 stand-in is dormant */
    if ((a.empty() || b.empty()) && coop::MySlotForWire() < 0) { coop::NoteHeldForSlot(); return; }   /* stand1 fold (1d): held - the snapshot sent when my slot arrives carries it */
    if (a.empty() || b.empty()) { ++g_faults; return; }
    if (coop::net::SendRelation(a, b, e.relation, e.trust, e.trustNeg, e.flags, reason)) { ++g_forwarded; g_lastForward = a + "->" + b + "=" + S(e.relation); }
    else ++g_sendFailed;
}

float detour_affect(void* self, ::Faction* f, float amount, float mult) { float r = orig_affect(self, f, amount, mult); Forward(self, f, 0); return r; }
// PROBE-START: P083
// P083 (rel5a): which by-event affectRelations (0x6B29D0) calls this game's engine makes on pairs with a player faction or the
// coop-peer faction on either side - event, multiplier, the entry before and after. Read-only (no writes, no sends); independent
// of g_on. Main thread: read + log (first 60 a run), then counted. Off the main thread: nothing read, counted per event only.
volatile LONG64 g_p083Ev[17] = {0}; volatile LONG64 g_p083OffThread = 0; long long g_p083Moved = 0; int g_p083Lines = 0;
bool P083Match(void* self, ::Faction* f)
{
    ::Faction* owner = Plaus(self) ? OwnerOf(self) : 0;
    if (Plaus(owner) && (coop::IsPlayerFaction(owner) || coop::IsPeerFaction(owner))) return true;
    return Plaus(f) && (coop::IsPlayerFaction(f) || coop::IsPeerFaction(f));
}
std::string P083Val(int rd, const Entry& e) { return rd == 1 ? S(e.relation) : std::string("n/a(") + S(rd) + ")"; }
void P083Log(void* self, ::Faction* f, int ev, float mult, int rdB, const Entry& b, int rdA, const Entry& a)
{
    if (rdB == 1 && rdA == 1 && a.relation != b.relation) ++g_p083Moved;
    if (g_p083Lines >= 60) return;
    ++g_p083Lines;
    ::Faction* owner = OwnerOf(self);
    DebugLog("[REL] P083 ev=" + S(ev) + " mult=" + S(mult) + " pair=" + SidOf(owner) + "->" + SidOf(f)
             + " before=" + P083Val(rdB, b) + " after=" + P083Val(rdA, a) + " applying=" + S(g_applying ? 1 : 0));
}
// PROBE-END: P083
void detour_affectEv(void* self, ::Faction* f, int ev, float mult)
{
    // PROBE-START: P083
    const bool p083 = P083Match(self, f);
    const bool p083Main = p083 && (g_mainThread == 0 || ::GetCurrentThreadId() == g_mainThread);   // as Forward decides it
    const bool p083Read = p083Main && Plaus(self) && Plaus(f);
    Entry p083B = {0, 0, 0, 0}; int p083RdB = 0;
    if (p083)
    {
        ::InterlockedIncrement64(&g_p083Ev[(ev >= 0 && ev <= 15) ? ev : 16]);
        if (!p083Main) ::InterlockedIncrement64(&g_p083OffThread);
        else if (p083Read) p083RdB = ReadEntry(self, f, &p083B);
    }
    // PROBE-END: P083
    orig_affectEv(self, f, ev, mult);
    // PROBE-START: P083
    if (p083Main) { Entry p083A = {0, 0, 0, 0}; const int p083RdA = p083Read ? ReadEntry(self, f, &p083A) : 0; P083Log(self, f, ev, mult, p083RdB, p083B, p083RdA, p083A); }
    // PROBE-END: P083
    Forward(self, f, 0);
}
void detour_declareWar(void* self, ::Faction* f) { orig_declareWar(self, f); Forward(self, f, 0); }
void detour_noLonger(void* self, ::Faction* f) { orig_noLonger(self, f); Forward(self, f, 0); }
void detour_setEnemy(void* self, ::Faction* f) { orig_setEnemy(self, f); Forward(self, f, 0); }
void detour_setRelation(void* self, ::Faction* f, float v) { orig_setRelation(self, f, v); Forward(self, f, 0); }
void detour_affectTrust(void* self, ::Faction* f, float amount, float mult) { orig_affectTrust(self, f, amount, mult); Forward(self, f, 0); }

// The owned snapshot: my player faction against every faction, both directions (2N entries; absent entries are created at
// the engine's default, which is what the engine would do on first contact anyway).
// E38 - THIS NOW REPORTS WHETHER IT ACTUALLY SENT ANYTHING. It used to return void, and RelationsTick fired it
// exactly once, on the link-up edge. Under decision 42 that edge happens AT THE TITLE SCREEN, where there is no
// world and therefore no local player faction, so the function returned at its second line and the standings
// snapshot was never sent again for the whole session (read-e38's defect against E38). Returning a bool is what
// lets the tick re-attempt: `true` means the walk ran to the end, `false` means a precondition was not met yet.
bool Snapshot()
{
    if (!g_on || (!coop::net::SessionLinked() && !coop::StoreLiveReady())) return false;   /* M5a fold 1 (#4) */
    if (coop::MySlotForWire() < 0) return false;   /* stand1: my player faction travels by slot - the snapshot is owed until the notebook gives me one */
    ::Faction* mine = coop::LocalPlayerFaction();
    if (!Plaus(mine) || !Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) return false;
    void** arr = 0; unsigned n = 0;
    if (!ReadFactionArrayPod(&arr, &n)) { ++g_faults; return false; }
    if (!Plaus(arr) || n > 4096) return false;
    long long sent = 0;
    for (unsigned i = 0; i < n; ++i)
    {
        ::Faction* f = (::Faction*)arr[i];
        if (!Plaus(f) || f == mine) continue;   // the peer faction stays in: mine->peer is my entry to send
        void* rm = RelationsOfPod(mine); void* rf = RelationsOfPod(f);
        if (Plaus(rm)) { long long before = g_forwarded; Forward(rm, f, 1); sent += (g_forwarded - before); }
        if (Plaus(rf)) { long long before = g_forwarded; Forward(rf, mine, 1); sent += (g_forwarded - before); }
    }
    ++g_snapshots; g_snapshotEntries += sent;
    DebugLog("[REL] snapshot: " + S(sent) + " owned entries sent (" + S(n) + " factions)");
    return true;
}

/* ---- par24: the world-vs-world table's game side (see g_wr above) ---- */
void SendWorldRows(int kind, const std::vector<coopwrel::WireRow>& rows, long long* sentCount, coopwrel::SendBook* book)   /* book: each row of a chunk that went up waits for its ECHO (review-par24 #2) */
{
    std::vector<coopwrel::WireRow> chunk;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        chunk.push_back(rows[i]);
        if (chunk.size() >= coopwrel::kWrMaxRows || i + 1 == rows.size())
        {
            std::vector<char> m;
            if (coopwrel::Encode(&m, kind, chunk) && coop::StoreSendWorldRel(m))
            {
                *sentCount += (long long)chunk.size();
                if (book) for (size_t j = 0; j < chunk.size(); ++j) book->Sent(coopwrel::Key(chunk[j].a, chunk[j].b));
            }
            else ++g_wrSendFailed;   /* the link is down: the next WELCOME's push writes the table back over this game's value */
            chunk.clear();
        }
    }
}
/* MAIN THREAD, every tick: this game's engine changes to world pairs (read live, one CHANGE message per 256 rows), then the
   seed rows the walk found (8 chunks a tick - the cap is on work per call, not a timer). */
void WorldRelTick()
{
    if (!g_on) { g_wrDirty.clear(); return; }
    if (!g_wrDirty.empty())
    {
        if (!g_wrTableApplied || !Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) { g_wrChangeBeforeTable += (long long)g_wrDirty.size(); g_wrDirty.clear(); }
        else
        {
            std::vector<coopwrel::WireRow> rows;
            for (std::set<std::string>::const_iterator it = g_wrDirty.begin(); it != g_wrDirty.end(); ++it)
            {
                coopwrel::WireRow w;
                if (!coopwrel::SplitKey(*it, &w.a, &w.b)) { ++g_wrSkipped; continue; }
                ::Faction* fa = coop::GameWorldPtr()->factionDirectory->findFactionById(w.a); ::Faction* fb = coop::GameWorldPtr()->factionDirectory->findFactionById(w.b);
                if (!Plaus(fa) || !Plaus(fb) || !WorldPair(fa, fb)) { ++g_wrUnresolved; continue; }
                void* rel = RelationsOfPod(fa); Entry e;
                if (!Plaus(rel) || ReadEntry(rel, fb, &e) != 1) { ++g_wrApplyFaults; continue; }
                w.row.relation = e.relation; w.row.trust = e.trust; w.row.trustNeg = e.trustNeg; w.row.flags = e.flags & 3u;
                if (!coopwrel::RowOk(w.a, w.b, w.row)) { ++g_wrSkipped; continue; }
                g_wrLastSent = w.a + "->" + w.b + "=" + S(w.row.relation);
                rows.push_back(w);
            }
            g_wrDirty.clear();
            if (!rows.empty()) SendWorldRows(coopwrel::kWrChange, rows, &g_wrChangeRowsSent, &g_wrSent);
        }
    }
    if (g_wrSeedState == 1)
    {
        for (int c = 0; c < 8 && !g_wrSeedOut.empty(); ++c)
        {
            const size_t take = g_wrSeedOut.size() < (size_t)coopwrel::kWrMaxRows ? g_wrSeedOut.size() : (size_t)coopwrel::kWrMaxRows;
            std::vector<coopwrel::WireRow> chunk(g_wrSeedOut.end() - take, g_wrSeedOut.end());
            std::vector<char> m;
            const int sk = (take == g_wrSeedOut.size()) ? (int)coopwrel::kWrSeedEnd : (int)coopwrel::kWrSeed;   /* S2-62: the last chunk ends the walk */
            if (!coopwrel::Encode(&m, sk, chunk)) { ++g_wrSendFailed; g_wrSeedOut.resize(g_wrSeedOut.size() - take); continue; }
            if (!coop::StoreSendWorldRel(m)) { ++g_wrSendFailed; break; }   /* kept: sent when the link is back (a WELCOME re-owes the walk anyway) */
            g_wrSeedSent += (long long)take;
            g_wrSeedOut.resize(g_wrSeedOut.size() - take);
            if (sk == coopwrel::kWrSeedEnd) g_wrSeedEndSent = true;
        }
        if (g_wrSeedOut.empty() && !g_wrSeedEndSent)
        {   /* S2-62: a walk that offered nothing (or whose last chunk did not encode) still ends - the notebook holds other games' seeds until the operator's walk ends */
            std::vector<char> m; const std::vector<coopwrel::WireRow> none;
            if (coopwrel::Encode(&m, coopwrel::kWrSeedEnd, none) && coop::StoreSendWorldRel(m)) g_wrSeedEndSent = true;
            else ++g_wrSendFailed;   /* retried on the next tick */
        }
        if (g_wrSeedOut.empty() && g_wrSeedEndSent)
        {
            g_wrSeedState = 2;
            DebugLog("[WREL] seed sent: " + S(g_wrSeedSent) + " rows this game's save holds and the notebook's table lacked (first writer wins at the notebook)");
        }
    }
}
/* 1 written, 0 already the same, -1 unresolved here, -2 fault. K2 safe point (worker paused). */
int WorldRelApplyRow(const std::string& key, const coopwrel::Row& r, bool bulk)   /* bulk: the whole table or a seed row - counted, not logged per row; the 20 lines are for another game's engine CHANGEs */
{
    std::string a, b;
    if (!coopwrel::SplitKey(key, &a, &b)) return -1;
    ::Faction* fa = coop::GameWorldPtr()->factionDirectory->findFactionById(a); ::Faction* fb = coop::GameWorldPtr()->factionDirectory->findFactionById(b);
    if (!Plaus(fa) || !Plaus(fb) || fa == fb || !WorldPair(fa, fb)) return -1;
    void* rel = RelationsOfPod(fa);
    if (!Plaus(rel)) return -2;
    Entry cur; const int rd = ReadEntry(rel, fb, &cur);
    if (rd == 1 && cur.relation == r.relation && cur.trust == r.trust && cur.trustNeg == r.trustNeg && (cur.flags & 3u) == r.flags) return 0;
    Entry e; e.relation = r.relation; e.trust = r.trust; e.trustNeg = r.trustNeg; e.flags = r.flags;
    g_applying = true; const int w = WriteEntry(rel, fb, e); g_applying = false;
    if (w != 1) return -2;
    g_wrLastApplied = a + "->" + b + "=" + S(r.relation);
    if (!bulk && g_wrAppliedLines < 20)
    {
        ++g_wrAppliedLines;
        DebugLog("[WREL] applied " + a + "->" + b + " relation " + (rd == 1 ? S(cur.relation) : std::string("n/a")) + " -> " + S(r.relation)
                 + " flags=" + S(r.flags) + " (the notebook's row, written at the K2 safe point)");
    }
    return 1;
}
void WorldRelCountApply(int rc) { if (rc == 1) ++g_wrApplied; else if (rc == 0) ++g_wrSame; else if (rc == -1) ++g_wrUnresolved; else ++g_wrApplyFaults; }
/* THE WALK (the seed): the engine's own map FactionRelations+0x20 (GameHashMap<Faction*, RelationData>, boost 1.60 -
   FactionRelations) read with the node layout crime.cpp's BmRead already reads (Confirmed there, T284): map +0x18
   bucket count, +0x20 size, +0x38 bucket array, first node = buckets[count], node +0 next, +0x10 key, +0x18 the value. Only
   READS - no entry is created. Returns the entries read, or -1 (a map this walk does not understand: nothing is used). */
int WalkRelMapPod(void* rel, void** keys, char** vals, int cap)
{
    __try
    {
        const char* m = (const char*)rel + 0x20;
        const size_t count = *(const size_t*)(m + 0x18);
        const size_t size = *(const size_t*)(m + 0x20);
        if (size == 0) return 0;
        if (size > (size_t)cap || count == 0 || count > ((size_t)1 << 20)) return -1;
        void* const* buckets = *(void* const* const*)(m + 0x38);
        if (buckets == 0) return -1;
        const char* node = (const char*)buckets[count];
        int n = 0;
        while (node != 0 && (size_t)n < size)
        {
            keys[n] = *(void* const*)(node + 0x10);
            vals[n] = (char*)node + 0x18;
            ++n;
            node = *(const char* const*)node;
        }
        return ((size_t)n == size) ? n : -1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* the self-check: for a key that IS a faction, the engine's own accessor must hand back the very value the walk found (an
   existing entry - nothing is created); 1 same, 0 not, -1 fault */
int WalkCheckPod(void* rel, ::Faction* key, char* val, Entry* e)
{
    __try
    {
        GetDataFn get = (GetDataFn)(Base() + kGetDataRva);
        char* d = (char*)get(rel, key);
        if (d != val) return 0;
        e->relation = *(float*)(d + 4); e->trust = *(float*)(d + 8); e->trustNeg = *(float*)(d + 0xC);
        e->flags = (d[0] ? 1u : 0u) | (d[2] ? 2u : 0u);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* K2 safe point: every world pair THIS game's save holds and the notebook's table lacks becomes a SEED row */
void WorldRelSeedWalk()
{
    ++g_wrSeedWalks;
    g_wrSeedOut.clear();
    void** arr = 0; unsigned n = 0;
    g_wrSeedEndSent = false;
    if (!ReadFactionArrayPod(&arr, &n) || !Plaus(arr) || n > 4096) { ++g_wrWalkFaults; g_wrSeedState = 1; DebugLog("[WREL] seed walk: no faction list - nothing offered (the walk's end still goes up, S2-62)"); return; }
    std::set<void*> known;
    for (unsigned i = 0; i < n; ++i) if (Plaus(arr[i])) known.insert(arr[i]);
    const int kCap = 4096;
    std::vector<void*> keys(kCap); std::vector<char*> vals(kCap);
    long long held = 0, inTable = 0, offered = 0, factions = 0;
    for (unsigned i = 0; i < n; ++i)
    {
        ::Faction* f = (::Faction*)arr[i];
        if (!Plaus(f) || coop::IsPlayerFaction(f) || coop::IsPeerFaction(f)) continue;
        const std::string a = SidOf(f);
        if (!coopwrel::SidOk(a) || coopslot::IsLegacyPeerId(a)) continue;
        void* rel = RelationsOfPod(f);
        if (!Plaus(rel)) continue;
        ++factions;
        const int got = WalkRelMapPod(rel, &keys[0], &vals[0], kCap);
        if (got < 0) { ++g_wrWalkFaults; continue; }
        for (int k = 0; k < got; ++k)
        {
            if (known.count(keys[k]) == 0) { ++g_wrWalkFaults; continue; }   /* not a faction: the layout is not what the walk thinks - skipped, never handed to the engine */
            ::Faction* o = (::Faction*)keys[k];
            if (o == f || !WorldPair(f, o)) continue;
            const std::string b = SidOf(o);
            if (!WorldSidOk(a, b)) continue;
            Entry e;
            if (WalkCheckPod(rel, o, vals[k], &e) != 1) { ++g_wrWalkFaults; continue; }
            ++held;
            const std::string key = coopwrel::Key(a, b);
            if (g_wr.count(key)) { ++inTable; continue; }
            coopwrel::WireRow w; w.a = a; w.b = b; w.row.relation = e.relation; w.row.trust = e.trust; w.row.trustNeg = e.trustNeg; w.row.flags = e.flags & 3u;
            if (!coopwrel::RowOk(w.a, w.b, w.row)) continue;
            if (g_wrSeedOut.size() >= (size_t)coopwrel::kWrMaxTable) break;
            g_wrSeedOut.push_back(w); ++offered;
        }
    }
    g_wrSeedHeldHere = held; g_wrSeedKnown = inTable; g_wrSeedOffered += offered;
    g_wrSeedState = 1;
    DebugLog("[WREL] seed walk: factions=" + S(factions) + " worldPairsHeldHere=" + S(held) + " alreadyInTable=" + S(inTable) + " offered=" + S(offered)
             + " walkFaults=" + S(g_wrWalkFaults) + " (the engine's own relation maps, read only; the notebook keeps the first offer of each pair)");
}
}

namespace coop {

void InstallRelations()
{
    if (g_installed) return;
    if (!g_queueLockInit) { ::InitializeCriticalSection(&g_queueLock); g_queueLockInit = true; }
    const uintptr_t base = Base();
    coop::HookStatus s[7];
    s[0] = coop::AddHook((void*)(base + kAffectRva), (void*)&detour_affect, (void**)&orig_affect);
    s[1] = coop::AddHook((void*)(base + kAffectEvRva), (void*)&detour_affectEv, (void**)&orig_affectEv);
    s[2] = coop::AddHook((void*)(base + kDeclareWarRva), (void*)&detour_declareWar, (void**)&orig_declareWar);
    s[3] = coop::AddHook((void*)(base + kNoLongerRva), (void*)&detour_noLonger, (void**)&orig_noLonger);
    s[4] = coop::AddHook((void*)(base + kSetEnemyRva), (void*)&detour_setEnemy, (void**)&orig_setEnemy);
    s[5] = coop::AddHook((void*)(base + kSetRelationRva), (void*)&detour_setRelation, (void**)&orig_setRelation);
    s[6] = coop::AddHook((void*)(base + kAffectTrustRva), (void*)&detour_affectTrust, (void**)&orig_affectTrust);
    std::string st; for (int i = 0; i < 7; ++i) st += (s[i] == coop::SUCCESS ? "ok " : "FAIL ");
    g_installed = true;
    DebugLog("[REL] hooks (affect, affectEv, declareWar, noLongerEnemies, setEnemy, setRelation, affectTrust): " + st);
}

void RelationsTick()
{
    if (g_mainThread == 0) { g_mainThread = ::GetCurrentThreadId(); }   // the tick is the main thread
    if (!g_queueLockInit) { ::InitializeCriticalSection(&g_queueLock); g_queueLockInit = true; }
    DrainQueue();
    WorldRelTick();   /* par24: world pairs up to the notebook */
    /* E38 - RECURRENCE-COVERED, NOT AN EDGE. Under decision 42 the session link comes up at the TITLE SCREEN, so
       the old link-up edge fired with no world and no player faction, Snapshot() returned at its second line, and
       the standings were never sent for the rest of the session. This is not a timer and not a periodic re-send:
       the condition is re-ASKED every tick while the snapshot is OWED (linked, and not yet sent on this link) and
       stops being asked the moment it succeeds. The user's 2026-09-03 "no time gating" rule is untouched - there
       is still no clock here and still exactly one snapshot per link. */
    const bool linked = net::SessionLinked();
    /* M5a FOLD 1 (review of 23f4216a #1, #4, #5) - THE LINK KEY. A snapshot of my own pairs AND a RELSYNC are owed at every
       change of (the session link generation while it is up, the welcomed notebook link generation while StoreLiveReady):
       a new session link, a new or re-welcomed notebook link (LinkUp can fall and return inside one generation -
       StoreLiveGen reads 0 while it is down), or either going away. My own changes sent into a notebook link that was
       dying but not yet seen to die are lost with it: the snapshot on the next key re-sends every one of them, and the
       RELSYNC asks every other game for theirs. A game linked only to the notebook (a third player) owes them too.
       Also owed: the SNAPSHOT when the road to the session peer changes (session.cpp SessionPeerRelayOk, or its slot
       becoming known) - sent on the new road after anything still in flight on the old one; and the RELSYNC when a
       relayed standing was dropped while this game had no running world (store.cpp StoreLiveArrive). */
    /* M11 C2: the session part of the key is the arrival epoch - it moves at every player entering the world (the old link's up
       edge is the session peer's) and at every old-link down, so in a two-game run exactly as the link generation did; kept while
       only the world-server link is up, so a newcomer through the world server owes the snapshot and the RELSYNC too */
    const long keyLive = coop::StoreLiveGen();
    const long keySess = (linked || keyLive != 0) ? coop::StorePeerEpoch() : 0;
    const long keySessLink = linked ? net::SessionLinkGen() : 0;   /* the M7a peer-in-world watch keeps the old link's own key */
    if (keySess != g_relKeySess || keyLive != g_relKeyLive) { g_relKeySess = keySess; g_relKeyLive = keyLive; g_snapshotSent = false; g_relSyncOwed = true; ++g_relKeyChanges; coop::StoreArrivalNoteServed(coop::kArrServeRelations); }
    {
        const int road = coop::net::SessionPeerRelayOk() ? 2 : (coop::LinkPeerSlot() >= 0 ? 1 : 0);
        if (road != g_relPeerRoad) { const bool matters = g_relPeerRoad >= 0 && linked && keyLive != 0; g_relPeerRoad = road; if (matters && g_snapshotSent) { g_snapshotSent = false; ++g_relRoadSnapshots; } }
    }
    {   /* M7a fold (review 2026-09-30 F1; T708): the session peer ENTERING THE WORLD owes a RELSYNC - its notebook copy is the proof the
           peer's relPeer ok is learnt from, and one sent while the peer was at the title was dropped by the world server (IN_WORLD only) */
        const int ps = coop::LinkPeerSlot();
        const bool peerIn = coop::StoreRosterSlotInWorld(ps) == 1;
        if (cooplive::PeerInWorldAskDue(&g_relPeerWatch, keySessLink, keyLive, ps, peerIn) != 0)
        {
            g_relSyncOwed = true; ++g_relPeerInWorldAsks;
            DebugLog("[REL] the session peer (slot " + S((long long)ps) + ") is IN_WORLD on the world server's roster - a RELSYNC is owed; its notebook copy tells that game this one reaches it through the world server (relPeerInWorldAsks " + S(g_relPeerInWorldAsks) + ")");
        }
    }
    const bool anyLink = linked || keyLive != 0;
    if (!anyLink) { /* no link: nothing to send on - the key above remembers what the next one owes */ }
    /* P7f (review-p6z M-3) - ASK THE PRECONDITION, NOT THE FRAME. Under E38 the link is up at the title screen
       and RelationsTick rides the title pump, which runs at ~900 Hz (F041). The retry was correct in shape and
       its counter was correct in value, but it counted TITLE FRAMES while its log line called them "how many
       ticks that took": a minute at the menu printed ~54,000 and read as a fault. GameplayRunning() (F337) is
       the condition the snapshot is actually waiting for - there is no local player faction until there is a
       world - so it is asked directly, and what still gets counted is named for what it is. This is not a timer
       and not a periodic re-send: the condition is re-asked every tick while the snapshot is owed. */
    else if (!GameplayRunning()) { /* no world yet: nothing to snapshot, and no number is owed for saying so */ }
    else if (!g_snapshotSent)
    {
        if (Snapshot()) g_snapshotSent = true;
        else
        {
            ++g_relationsSnapshotDeferredFrames;
            if (g_relationsSnapshotDeferredFrames == 1)
                DebugLog("[REL] snapshot DEFERRED: the link is up but there is no local player faction to take a"
                         " snapshot of yet - normal when the link came up at the title screen (E38). It will be"
                         " re-attempted every tick until it goes; relationsSnapshotDeferredFrames in the report"
                         " counts PUMP FRAMES, not seconds and not attempts at a rate anyone chose.");
        }
    }
    /* M5a fold 1 (#1): the RELSYNC owed on this link key goes right behind the snapshot - one ask per key (or per dropped
       relayed standing), on one road (session.cpp: the notebook's WORLD when up, else the session link); a failed send
       is asked again on the next tick while it is owed. */
    if (anyLink && g_snapshotSent && g_relSyncOwed && GameplayRunning() && coop::net::SendRelSync())
    {
        g_relSyncOwed = false; ++g_relSyncAsked;
        DebugLog("[REL] RELSYNC sent " + std::string(keyLive != 0 ? "through the notebook" : "on the session link") + " (link key " + S((long long)keySess) + "/" + S((long long)keyLive)
                 + ") - every other game re-sends its standings (relSyncAsked " + S(g_relSyncAsked) + ")");
    }
    g_wasLinked = linked;   /* E38: still the link-edge memory, but the snapshot no longer reads it - g_snapshotSent is what says whether this link has had one */
}

void SetRelationsOn(bool on) { g_on = on; DebugLog(std::string("[REL] ") + (on ? "ON" : "OFF")); }
void RelationsSendSnapshot() { Snapshot(); }
void RelationsNoteRelayedDropped() { g_relSyncOwed = true; ++g_relDroppedResync; }   /* M5a fold 1 (#2b) */
void RelationsForgetQueue() { g_wrApplyAll = true; g_wrTableApplied = false; g_wrSeedState = 0; g_wrSeedOut.clear(); g_wrDirty.clear(); g_wrPending.clear(); g_wrPendingChange.clear(); g_wrSent.Clear();   /* par24: the next world writes the notebook's whole table (g_wr is kept - the notebook's truth, not a pointer) and re-owes its walk */ if (!g_queueLockInit) return; ::EnterCriticalSection(&g_queueLock); g_queue.clear(); g_revertQueue.clear(); g_wrOffQueue.clear(); ::LeaveCriticalSection(&g_queueLock); }   // review-p3r M4: g_ownerValues (sids -> values) stays - the owner's truth, not a pointer   // review-p3o M3: old-world entries must not be re-resolved against the new world   // an event (a rename) - the owned entries carry "@player:<new name>"

void ApplyRemoteRelation(const std::string& ownerSid, const std::string& otherSid, float relation, float trust, float trustNeg,
                         unsigned int flags, unsigned int reason)
{
    ++g_received;
    if (!g_on) return;
    /* M5b (carried from M5a) - A PAIR BELONGS TO ITS SENDER. The player on the pair's owner side (else its other side - Owned())
       must be the sending player: the notebook's stamp for a relayed pair, the session peer's PEER_SLOT otherwise. Checked
       before any name is resolved, so a refused pair creates no stand-in. Unknown on either side: taken as before. */
    {
        const int sender = coop::WireSenderSlot(), owning = coopslot::PairOwnerSlot(ownerSid, otherSid);
        if (sender >= 0 && owning >= 0 && owning != sender)
        {
            if (++g_refusedNotSender <= 5) DebugLog("[REL] REFUSED '" + ownerSid + "' -> '" + otherSid + "' from slot " + S(sender) + ": that standing is slot " + S(owning) + "'s (M5b)");
            return;
        }
    }
    ::Faction* owner = FactionOf(ownerSid); ::Faction* other = FactionOf(otherSid);
    if (!Plaus(owner) || !Plaus(other)) { if (++g_unresolved <= 5) DebugLog("[REL] unresolved '" + ownerSid + "' -> '" + otherSid + "' (owner " + S((const void*)owner) + ", other " + S((const void*)other) + ")"); return; }
    /* par24: a world-vs-world pair is the NOTEBOOK's (WORLD_REL) - never taken from the session link (a pre-par24 host sent them) */
    if (WorldPair(owner, other))
    {
        if (++g_wrOnSession <= 5) DebugLog("[REL] REFUSED '" + ownerSid + "' -> '" + otherSid + "' on the session link: world-vs-world standings are the notebook's table (par24)");
        return;
    }
    /* rel1 (2026-09-24, .modding/investigations/faction-relations.md R2): a pair THIS game owns is never written from the
       wire - by the same Owned() rule the sender used. In the designed flow it cannot arrive (a sender never sends a pair it
       does not own); if a player faction ever travels under its raw stringID (both players' factions share 204-gamedata.base,
       F483) it would resolve HERE to this game's own player faction and overwrite this player's standing - refused and
       counted instead. */
    if (Owned(owner, other))
    {
        if (++g_refusedOwnedHere <= 5) DebugLog("[REL] REFUSED '" + ownerSid + "' -> '" + otherSid + "': this game owns that standing (rel1) - a remote write would overwrite it");
        return;
    }
    void* rel = RelationsOfPod(owner);
    if (!Plaus(rel)) { ++g_faults; return; }
    Entry e; e.relation = relation; e.trust = trust; e.trustNeg = trustNeg; e.flags = flags;
    const std::string ka = coopslot::SlotWireKey(ownerSid), kb = coopslot::SlotWireKey(otherSid);   /* stand1: keyed by SLOT - absolute on both games, and a rename keeps its key */
    g_ownerValues[PairKey(ka, kb)] = e;
    g_applying = true;
    const int w = WriteEntry(rel, other, e);
    g_applying = false;
    if (w == 1) TagNoteRelation(owner, other);
    if (w == 1) { ++g_applied; g_lastApplied = ka + "->" + kb + "=" + S(relation) + (reason ? " (snapshot)" : ""); if (reason == 0) DebugLog("[REL] applied " + g_lastApplied); }
    else ++g_faults;
}

/* The name tag's level between this game's player faction and another player's faction: each direction judged by the game's
   own ally / enemy levels, the worse of the two returned (nametag::WorseLevel). MAIN THREAD. Reading goes through the engine's
   own accessor, which adds a missing entry at the faction's default relation - the value the engine would use anyway. */
int RelationsTagLevel(::Faction* mine, ::Faction* theirs, int* reads)
{
    int ab = nametag::kUnknown, ba = nametag::kUnknown;
    if (kGetDataRva != 0 && Plaus(mine) && Plaus(theirs))
    {
        Entry e = { 0.0f, 0.0f, 0.0f, 0u };
        void* rm = RelationsOfPod(mine);
        if (Plaus(rm) && ReadEntry(rm, theirs, &e) == 1) ab = nametag::LevelOf(e.relation, (e.flags & 1u) != 0);
        void* rt = RelationsOfPod(theirs);
        if (Plaus(rt) && ReadEntry(rt, mine, &e) == 1) ba = nametag::LevelOf(e.relation, (e.flags & 1u) != 0);
    }
    if (reads != 0) *reads = (ab != nametag::kUnknown ? 1 : 0) + (ba != nametag::kUnknown ? 1 : 0);
    return nametag::WorseLevel(ab, ba);
}

// P079 (rel2): read-only. Reading through the engine's accessor seeds a missing entry at the faction's own default relation
// (F617) - the value the engine would use anyway, and the link-up snapshot already reads every pair the same way.
std::string RelationPairText(::Faction* a, ::Faction* b, const std::string& name)
{
    void* rel = Plaus(a) ? RelationsOfPod(a) : 0;
    Entry e;
    if (!Plaus(b) || !Plaus(rel) || ReadEntry(rel, b, &e) != 1) return " " + name + "=n/a";
    return " " + name + "[rel,trust,trustNeg,ally,war]=" + S(e.relation) + "," + S(e.trust) + "," + S(e.trustNeg) + ","
         + S((e.flags & 1u) ? 1 : 0) + "," + S((e.flags & 2u) ? 1 : 0);
}
std::string RelationProbe(const std::string& sid)
{
    ::Faction* f = (Plaus(coop::GameWorldPtr()) && Plaus(coop::GameWorldPtr()->factionDirectory)) ? coop::GameWorldPtr()->factionDirectory->findFactionById(sid) : 0;
    if (!Plaus(f)) { DebugLog("[REL] relation " + sid + ": no such faction here"); return "error relation not-found"; }
    ::Faction* me = coop::LocalPlayerFaction();
    ::Faction* peer = coop::PeerFaction();   /* stand1: ONE-OTHER-PLAYER, kept - a diagnostic of the game on the session link */
    DebugLog("[REL] relation " + sid + ":" + RelationPairText(f, me, "it->me") + RelationPairText(me, f, "me->it")
             + RelationPairText(f, peer, "it->peer") + RelationPairText(peer, f, "peer->it")
             + RelationPairText(me, peer, "me->peer") + RelationPairText(peer, me, "peer->me") + " (P079 read-only probe)");   /* rel3: the player pair */
    return "ok relation";
}
/* ally1 (T343; user decision 2026-09-26: joining another player's faction starts with "a simple ally switch").
   THE ENGINE'S OWN ALLY TEST (FactionRelations 0x6B22E0, reached from Character::isAllyOf 0x791830 through 0x6B2460) reads the
   entry's ally FLAG (+0) first and otherwise answers ally when relation >= 50.0 (.rdata 0x1682170, Confirmed from the exe bytes).
   This switch moves only the RELATION VALUE, through the engine's own setRelation 0x6B4A30 - the hooked setter, so the existing
   Forward sends MY "mine -> stand-in" entry and the other game writes it as "stand-in -> their faction" (ApplyRemoteRelation).
   The flag has no engine setter we know of, so it is never written here. MAIN THREAD (the command channel tick). */
namespace {
const float kAllyThreshold = 50.0f;   /* FactionRelations 0x6B22E0 compares against .rdata 0x1682170 = 50.0 */
const float kAllyOnValue = 100.0f;    /* the top of the scale, so ordinary event drift does not drop it under 50 */
bool g_allyHaveBefore = false; float g_allyBefore = 0.0f;
long long g_allyOn = 0, g_allyOff = 0, g_allyRefused = 0, g_allyFaults = 0;
std::string g_allyLast;
int CallSetRelationPod(void* rel, ::Faction* f, float v)
{
    __try { SetRelFn set = (SetRelFn)(Base() + kSetRelationRva); set(rel, f, v); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
bool EngineAlly(const Entry& e) { return (e.flags & 1u) != 0 || e.relation >= kAllyThreshold; }
/* relate1 (docs/design-profiles1.md s3/s4 step 1): the engine's ENEMY test on the stored relation is FactionRelations::_isEnemy
   0x6B2280 (build/decomp_6b2280.txt, Read): false for a null or the faction's own self; entry = getRelationData(other)
   (vtable +0x50, the same inserting accessor ReadEntry uses); relation <= .rdata 0x16CBCFC = -30.0 (Confirmed from the exe
   bytes). There is no atWar-flag term in it. This MIRRORS that test on the entry already read - it does not call 0x6B2280
   (calling it would need a new address-table row whose verify bytes come from the exe). */
const float kEnemyThreshold = -30.0f;
bool EngineEnemy(const Entry& e) { return e.relation <= kEnemyThreshold; }
/* relate1: user decisions 2026-09-26 - three levels, towards OTHER PLAYERS only (not towns or factions); fights may still
   move a chosen level afterwards (vanilla). */
const float kRelateAlly = 100.0f, kRelateNeutral = 0.0f, kRelateHostile = -100.0f;
long long g_relSet[3] = { 0, 0, 0 };   /* ally, neutral, hostile - through the relate verb */
long long g_relViaAlly = 0, g_relRefusedTarget = 0, g_relRefused = 0, g_relFaults = 0;
std::string g_relLast;
std::string LevelOf(float v) { return v >= kAllyThreshold ? std::string("ally") : v <= kEnemyThreshold ? std::string("hostile") : std::string("neutral"); }
struct WireMark { long long fwd, noLink, fail, queued; };
WireMark MarkWire() { WireMark m; m.fwd = g_forwarded; m.noLink = g_offNoLink; m.fail = g_sendFailed; m.queued = (long long)g_queued; return m; }
std::string WireSince(const WireMark& m)
{
    return !g_installed ? "NOT-HOOKED(nothing forwarded)"
         : g_forwarded > m.fwd ? "forwarded"
         : (long long)g_queued > m.queued ? "queued(off the main thread)"
         : g_offNoLink > m.noLink ? "not-linked(the next link-up snapshot carries it)"
         : g_sendFailed > m.fail ? "SEND-FAILED"
         : g_on ? "not-forwarded" : "relations-off(set here only)";
}
/* relate1: the plain line, plus the line the player could see later. setRelation 0x6B4A30 shows no message (design s3), so the
   mod words its own; for now it is a log line only - the on-screen text is build step 8 (relate2). */
void RelateAnnounce(float before, const Entry& a, int ra, const std::string& level, const std::string& wire, bool viaAlly, ::Faction* target)
{
    if (viaAlly) ++g_relViaAlly;
    const std::string after = ra == 1 ? S(a.relation) : std::string("n/a");
    g_relLast = level + " " + S(before) + "->" + after + " " + wire + (viaAlly ? " (ally verb)" : "");
    DebugLog("[RELATE] set me->stand-in " + S(before) + " -> " + after + " (" + level + ") wire=" + wire
             + " engineAlly=" + (ra == 1 ? S(EngineAlly(a) ? 1 : 0) : std::string("n/a"))
             + " engineEnemy=" + (ra == 1 ? S(EngineEnemy(a) ? 1 : 0) : std::string("n/a"))
             + (viaAlly ? " via=ally-verb" : ""));
    std::string who = coop::StandInDisplayName(target);   /* stand1: the stand-in the verb named, not "the peer" */
    if (who.empty()) who = "the other player";
    const std::string shown = level == "ally" ? "Ally" : level == "hostile" ? "Hostile" : "Neutral";
    DebugLog("[RELATE] player notice: 'You are now " + shown + " towards " + who + ".' (log only - the game shows no message for setRelation; on-screen text is step 8)");
}
::Faction* PairFaction(const std::string& t)
{
    if (t == "@me") return coop::LocalPlayerFaction();
    if (t == "@peer") return coop::PeerFaction();   /* stand1: kept as the alias for "the one other player" (the game on the session link) */
    if (coopslot::IsSlotWire(t)) { int n = -1; if (!coopslot::ParseSlotWire(t, &n, 0, 0) || n < 0) return 0; return n == coop::MySlotForWire() ? coop::LocalPlayerFaction() : coop::StandInForSlot(n); }   /* stand1: @slot:<n> */
    return (Plaus(coop::GameWorldPtr()) && Plaus(coop::GameWorldPtr()->factionDirectory)) ? coop::GameWorldPtr()->factionDirectory->findFactionById(t) : 0;
}
}

std::string AllySet(bool on)
{
    const std::string verb = on ? "on" : "off";
    if (!GameplayRunning() || EngineWritesBlocked()) { ++g_allyRefused; DebugLog("[ALLY] " + verb + " refused: no running world (or engine writes are blocked)"); return "error ally not-in-game"; }
    ::Faction* me = coop::LocalPlayerFaction();
    /* stand1: ONE-OTHER-PLAYER, kept - `ally on|off` is the alias for `relate @peer`, the game on the session link */
    ::Faction* standin = coop::PeerFaction();
    if (!Plaus(me) || !Plaus(standin)) { ++g_allyRefused; DebugLog("[ALLY] " + verb + " refused: me=" + S((const void*)me) + " stand-in=" + S((const void*)standin) + " (no stand-in faction on this game yet)"); return "error ally no-stand-in"; }
    if (kSetRelationRva == 0 || kGetDataRva == 0) { ++g_allyRefused; DebugLog("[ALLY] " + verb + " refused: setRelation/getRelationData address not in the table"); return "error ally no-address"; }
    void* rel = RelationsOfPod(me);
    Entry b;
    if (!Plaus(rel) || ReadEntry(rel, standin, &b) != 1) { ++g_allyFaults; DebugLog("[ALLY] " + verb + " FAULT: my relations entry for the stand-in could not be read"); return "error ally fault"; }
    float target = 0.0f;
    if (on) { if (b.relation < kAllyThreshold) { g_allyBefore = b.relation; g_allyHaveBefore = true; } target = kAllyOnValue; }
    else
    {
        /* review-ally1 4c: with nothing remembered, off only lowers an ALLY value to 0 - a standing already under the
           threshold (hostile, say) is left exactly as it is and nothing is sent. */
        if (!(g_allyHaveBefore && g_allyBefore < kAllyThreshold) && b.relation < kAllyThreshold)
        { g_allyHaveBefore = false; DebugLog("[ALLY] off: already not an ally (relation=" + S(b.relation) + ") - left as it is"); return "ok ally off (unchanged)"; }
        target = (g_allyHaveBefore && g_allyBefore < kAllyThreshold) ? g_allyBefore : 0.0f; g_allyHaveBefore = false;
    }
    const WireMark m0 = MarkWire();
    if (CallSetRelationPod(rel, standin, target) != 1) { ++g_allyFaults; DebugLog("[ALLY] " + verb + " FAULT: the engine's setRelation raised"); return "error ally fault"; }
    Entry a; const int ra = ReadEntry(rel, standin, &a);
    if (on) ++g_allyOn; else ++g_allyOff;
    const std::string wire = WireSince(m0);
    g_allyLast = verb + " " + S(b.relation) + "->" + (ra == 1 ? S(a.relation) : std::string("n/a")) + " " + wire;
    DebugLog("[ALLY] " + verb + ": me->stand-in relation " + S(b.relation) + " -> " + (ra == 1 ? S(a.relation) : std::string("n/a"))
             + " flag=" + S((ra == 1 && (a.flags & 1u)) ? 1 : 0) + " engineAllyTest=" + S((ra == 1 && EngineAlly(a)) ? 1 : 0)
             + " wire=" + wire + " (engine setRelation 0x6B4A30; the other game writes it as stand-in->them; their half is theirs to set)");
    RelateAnnounce(b.relation, a, ra, on ? std::string("ally") : (ra == 1 ? LevelOf(a.relation) : std::string("neutral")), wire, true, standin);   /* relate1: ally on|off is the alias */
    if (!on && ra == 1 && (a.flags & 1u))
        DebugLog("[ALLY] off: the entry's ally FLAG is set (not by this verb) - the engine still answers ally; this verb moves only the relation value");
    return on ? "ok ally on" : "ok ally off";
}

/* relate1: `relate <target> ally|neutral|hostile` - MY "mine -> stand-in" relation to +100 / 0 / -100 through the engine's own
   setRelation 0x6B4A30 (the hooked setter, exactly as AllySet), so the existing Forward carries it and the other game writes
   it as "stand-in -> their faction". REFUSED for any target that is not another player's stand-in (today: the one peer
   faction; @slot:<n> arrives with step 3). MAIN THREAD (the command channel tick). */
std::string RelateSet(const std::string& target, const std::string& level)
{
    int li = -1; float v = 0.0f;
    if (level == "ally") { li = 0; v = kRelateAlly; }
    else if (level == "neutral") { li = 1; v = kRelateNeutral; }
    else if (level == "hostile") { li = 2; v = kRelateHostile; }
    if (li < 0) { ++g_relRefused; DebugLog("[RELATE] refused: level '" + level + "' is not ally|neutral|hostile"); return "error relate usage"; }
    if (!GameplayRunning() || EngineWritesBlocked()) { ++g_relRefused; DebugLog("[RELATE] " + level + " refused: no running world (or engine writes are blocked)"); return "error relate not-in-game"; }
    ::Faction* me = coop::LocalPlayerFaction();
    ::Faction* t = PairFaction(target);
    ::Faction* standin = (Plaus(t) && coop::IsPeerFaction(t)) ? t : 0;   /* stand1: ANY player's stand-in - @peer (the one on the link) or @slot:<n> */
    if (!Plaus(t) || !Plaus(standin))
    {
        ++g_relRefusedTarget;
        DebugLog("[RELATE] refused: '" + target + "' is not another player's stand-in (resolved " + S((const void*)t) + ", stand-in " + S((const void*)standin)
                 + ") - relations are set towards other players only, not towns or factions");
        return "error relate not-a-player";
    }
    if (!Plaus(me)) { ++g_relRefused; DebugLog("[RELATE] " + level + " refused: no player faction on this game"); return "error relate no-faction"; }
    if (kSetRelationRva == 0 || kGetDataRva == 0) { ++g_relRefused; DebugLog("[RELATE] " + level + " refused: setRelation/getRelationData address not in the table"); return "error relate no-address"; }
    void* rel = RelationsOfPod(me);
    Entry b;
    if (!Plaus(rel) || ReadEntry(rel, standin, &b) != 1) { ++g_relFaults; DebugLog("[RELATE] " + level + " FAULT: my relations entry for the stand-in could not be read"); return "error relate fault"; }
    const WireMark m = MarkWire();
    if (CallSetRelationPod(rel, standin, v) != 1) { ++g_relFaults; DebugLog("[RELATE] " + level + " FAULT: the engine's setRelation raised"); return "error relate fault"; }
    Entry a; const int ra = ReadEntry(rel, standin, &a);
    g_allyHaveBefore = false;   /* a chosen level replaces whatever `ally off` would have restored */
    ++g_relSet[li];
    RelateAnnounce(b.relation, a, ra, level, WireSince(m), false, standin);
    if (ra == 1 && li != 0 && (a.flags & 1u))
        DebugLog("[RELATE] the entry's ally FLAG is set (not by this verb) - the engine still answers ally; this verb moves only the relation value");
    return "ok relate " + level;
}

/* ally1: `relation <a> <b>` - both directions of one pair. Tokens: @me (this game's player faction), @peer (the stand-in for
   the other player), else a faction stringID. READ-ONLY IN VALUE BUT NOT IN ROWS: there is no non-inserting read (F636), and
   the engine's own ally test reads through the same inserting accessor (FactionRelations vtable +0x50 = 0x6B4910, Confirmed
   from the exe's vtable). A missing row is created at the faction's default - the value the engine uses anyway - and no
   hooked setter runs, so nothing is forwarded. The player pair's rows already exist on a linked game (the link-up snapshot). */
std::string RelationPairProbe(const std::string& sa, const std::string& sb)
{
    ::Faction* fa = PairFaction(sa); ::Faction* fb = PairFaction(sb);
    if (!Plaus(fa) || !Plaus(fb) || fa == fb) { DebugLog("[REL] relation " + sa + " " + sb + ": not found here or the same faction (a=" + S((const void*)fa) + " b=" + S((const void*)fb) + ")"); return "error relation not-found"; }
    Entry e1, e2; void* ra = RelationsOfPod(fa); void* rb = RelationsOfPod(fb);
    const int r1 = Plaus(ra) ? ReadEntry(ra, fb, &e1) : 0; const int r2 = Plaus(rb) ? ReadEntry(rb, fa, &e2) : 0;
    DebugLog("[REL] relation " + sa + " " + sb + ":" + RelationPairText(fa, fb, "a->b") + " a->b.engineAlly=" + (r1 == 1 ? S(EngineAlly(e1) ? 1 : 0) : std::string("n/a"))
             + " a->b.engineEnemy=" + (r1 == 1 ? S(EngineEnemy(e1) ? 1 : 0) : std::string("n/a"))
             + RelationPairText(fb, fa, "b->a") + " b->a.engineAlly=" + (r2 == 1 ? S(EngineAlly(e2) ? 1 : 0) : std::string("n/a"))
             + " b->a.engineEnemy=" + (r2 == 1 ? S(EngineEnemy(e2) ? 1 : 0) : std::string("n/a"))
             + " (ally1 pair readout; engineEnemy mirrors FactionRelations::_isEnemy 0x6B2280: relation <= -30; a missing row is created at the default - no non-inserting read exists, F636)");
    return "ok relation";
}

void ReportRelations()
{
    for (std::map<std::string, P081Pair>::const_iterator p = g_p081.begin(); p != g_p081.end(); ++p)   // P081 (rel4)
        DebugLog("[REL] P081 pair " + p->first + " reverts=" + S(p->second.n) + " small=" + S(p->second.small) + " sumMoved=" + S(p->second.sum));
    // PROBE-START: P083
    {
        std::string h;
        for (int i = 0; i < 17; ++i) h += (i ? "," : "") + S((long long)g_p083Ev[i]);
        DebugLog("[REL] P083 ev[0..15,other]=" + h + " moved=" + S(g_p083Moved) + " offThread=" + S((long long)g_p083OffThread)
                 + " logged=" + S(g_p083Lines));
    }
    // PROBE-END: P083
    DebugLog("[REL] REPORT on=" + S(g_on ? 1 : 0) + " changes=" + S(g_changes) + " forwarded=" + S(g_forwarded) + " notOwned=" + S(g_notOwned)
             + " echoSuppressed=" + S(g_echoSuppressed) + " offThread=" + S((long long)g_offThread) + " queued=" + S((long long)g_queued) + " drained=" + S(g_drained) + " drainUnresolved=" + S(g_drainUnresolved) + " reverted=" + S(g_reverted) + " revertQueued=" + S(g_revertQueued) + " revertNoValue=" + S(g_revertNoValue) + " queueDropped=" + S((long long)g_queueDropped) + " offNoLink=" + S(g_offNoLink) + " sendFailed=" + S(g_sendFailed)
             + " | received=" + S(g_received) + " applied=" + S(g_applied) + " unresolved=" + S(g_unresolved) + " faults=" + S(g_faults) + " refusedOwnedHere=" + S(g_refusedOwnedHere) + " refusedNotSender=" + S(g_refusedNotSender) + " legacyPeerSkipped=" + S(g_legacySkipped)
             + " | snapshots=" + S(g_snapshots) + " entries=" + S(g_snapshotEntries) + " relSyncAsked=" + S(g_relSyncAsked) + " relPeerInWorldAsks=" + S(g_relPeerInWorldAsks) + " relSyncOwed=" + S(g_relSyncOwed ? 1 : 0) + " relKeyChanges=" + S(g_relKeyChanges) + " relRoadSnapshots=" + S(g_relRoadSnapshots) + " relDroppedResync=" + S(g_relDroppedResync) + " relationsSnapshotDeferredFrames=" + S(g_relationsSnapshotDeferredFrames) + " snapshotSent=" + S(g_snapshotSent ? 1 : 0) + " lastForward='" + g_lastForward + "' lastApplied='" + g_lastApplied + "'");
    DebugLog("[WREL] REPORT rows=" + S((long long)g_wr.size()) + " rowsIn=" + S(g_wrRowsIn) + " tableApplied=" + S(g_wrTableApplied ? 1 : 0) + " tableApplies=" + S(g_wrTableApplies)
             + " applied=" + S(g_wrApplied) + " same=" + S(g_wrSame) + " unresolved=" + S(g_wrUnresolved) + " applyFaults=" + S(g_wrApplyFaults) + " pending=" + S((long long)g_wrPending.size())
             + " | changes=" + S(g_wrChanges) + " changeBeforeTable=" + S(g_wrChangeBeforeTable) + " changeRowsSent=" + S(g_wrChangeRowsSent) + " offQueued=" + S(g_wrOffQueued) + " offDropped=" + S(g_wrOffDropped)
             + " dirtyDropped=" + S(g_wrDirtyDropped) + " skipped=" + S(g_wrSkipped) + " sendFailed=" + S(g_wrSendFailed)
             + " echoes=" + S(g_wrEchoes) + " awaitingEcho=" + S((long long)g_wrSent.Size()) + " heldForOwnChange=" + S(g_wrHeldForOwn) + " undoHeld=" + S((long long)g_wrUndo.size()) + " undone=" + S(g_wrUndone)
             + " | seedState=" + S(g_wrSeedState) + " seedWalks=" + S(g_wrSeedWalks) + " worldPairsHeldHere=" + S(g_wrSeedHeldHere) + " seedAlreadyInTable=" + S(g_wrSeedKnown) + " seedOffered=" + S(g_wrSeedOffered) + " seedSent=" + S(g_wrSeedSent) + " walkFaults=" + S(g_wrWalkFaults)
             + " | onSessionRefused=" + S(g_wrOnSession) + " malformed=" + S(g_wrMalformed) + " testSets=" + S(g_wrTestSets)
             + " lastApplied='" + g_wrLastApplied + "' lastSent='" + g_wrLastSent + "' lastTest='" + g_wrLastTest + "'");
    DebugLog("[ALLY] REPORT on=" + S(g_allyOn) + " off=" + S(g_allyOff) + " refused=" + S(g_allyRefused) + " faults=" + S(g_allyFaults)
             + " haveBefore=" + S(g_allyHaveBefore ? 1 : 0) + " last='" + g_allyLast + "'");
    DebugLog("[RELATE] REPORT ally=" + S(g_relSet[0]) + " neutral=" + S(g_relSet[1]) + " hostile=" + S(g_relSet[2]) + " viaAllyVerb=" + S(g_relViaAlly)
             + " refusedTarget=" + S(g_relRefusedTarget) + " refused=" + S(g_relRefused) + " faults=" + S(g_relFaults) + " last='" + g_relLast + "'");
}
// crime3 (crime.cpp): the same sid mapping relations uses, for a crime's victim faction.
std::string RelationsWireSid(::Faction* f) { return SidOf(f); }
::Faction* RelationsFactionFromWire(const std::string& sid) { return FactionOf(sid); }

/* ---- mmo3 (e47-mmo-design.md 1.1 row E): THE PLAYER FACTION ROW ----------------------------------------------------
   Read: this game's player faction, its name, platoonIDs (Faction+0x260, F696) and for every other faction that is not a
   player or stand-in faction its stringID and both entries (mine->it, it->mine) through getRelationData 0x6B4910 - the
   read Snapshot makes (a missing entry is created at the engine's default, F617). Restore: the relation through
   setRelation 0x6B4A30 (a plain store, no notice - the hooked setter, so Forward carries it to a linked peer as an own
   change), trust / trustNeg stored directly as the game's loader 0x6B3580 does; NEVER affectRelations / declareWar /
   setEnemy (they toast through 0x6B25A0). The name is recorded and compared only (a rename is playerfaction.cpp's). */
int OwnPlatoonIdsPod(::Faction* f, int op, int* v)
{
    __try { int* p = (int*)((char*)f + 0x260); if (op == 1) *p = *v; else *v = *p; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int OwnTrustWritePod(void* rel, ::Faction* other, float trust, float trustNeg, int flags)   /* mmo3 fold 7: flags < 0 = not recorded */
{
    __try
    {
        GetDataFn get = (GetDataFn)(Base() + kGetDataRva);
        char* d = (char*)get(rel, other);
        if (!Plaus(d)) return 0;
        *(float*)(d + 8) = trust; *(float*)(d + 0xC) = trustNeg;
        if (flags >= 0) { d[0] = (flags & 1) ? 1 : 0; d[2] = (flags & 2) ? 1 : 0; }   /* mmo3 fold 7: ally / atWar, as WriteEntry */
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
bool RelationsOwnRecord(coopown::FactionRec* out, std::string* why)
{
    ::Faction* mine = coop::LocalPlayerFaction();
    if (!Plaus(mine) || !Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) { *why = "no-player-faction"; return false; }
    if (kGetDataRva == 0) { *why = "no-address"; return false; }
    void* rm = RelationsOfPod(mine);
    if (!Plaus(rm)) { *why = "no-relations"; return false; }
    int pid = 0;
    if (OwnPlatoonIdsPod(mine, 0, &pid) != 1) { *why = "fault"; return false; }
    void** arr = 0; unsigned n = 0;
    if (!ReadFactionArrayPod(&arr, &n) || !Plaus(arr) || n > 4096) { *why = "no-faction-list"; return false; }
    out->name = mine->getName(); out->platoonIds = pid; out->rows.clear();
    char buf[160];
    for (unsigned i = 0; i < n; ++i)
    {
        ::Faction* f = (::Faction*)arr[i];
        if (!Plaus(f) || f == mine || coop::IsPlayerFaction(f) || coop::IsPeerFaction(f)) continue;
        if (!ReadSidPod(f, buf, 160) || buf[0] == 0) continue;
        void* rf = RelationsOfPod(f);
        if (!Plaus(rf)) continue;
        Entry a, b;
        if (ReadEntry(rm, f, &a) != 1 || ReadEntry(rf, mine, &b) != 1) continue;
        coopown::FacRow r;
        r.sid = buf; r.rel = a.relation; r.trust = a.trust; r.trustNeg = a.trustNeg; r.relBack = b.relation; r.trustBack = b.trust; r.trustNegBack = b.trustNeg;
        r.flags = (int)(a.flags & 3u); r.flagsBack = (int)(b.flags & 3u);   /* mmo3 fold 7 */
        out->rows.push_back(r);
    }
    std::sort(out->rows.begin(), out->rows.end(), coopown::FacRowLess);
    return true;
}
/* mmo3 fold 5: trust / trustNeg (and fold 7's flags) are stored BEFORE setRelation, so the entry setRelation's detour
   forwards carries them; a change of trust or flags only is forwarded as well (Forward reads the entry live). */
void OwnRestorePair(void* rel, ::Faction* other, const Entry& cur, float relation, float trust, float trustNeg, int flags,
                    long long* relSet, long long* trustSet, long long* faults)
{
    const bool flagsDiffer = flags >= 0 && (unsigned)flags != (cur.flags & 3u);
    const bool trustDiffers = cur.trust != trust || cur.trustNeg != trustNeg || flagsDiffer;
    if (trustDiffers) { if (OwnTrustWritePod(rel, other, trust, trustNeg, flags) == 1) ++*trustSet; else ++*faults; }
    if (cur.relation != relation) { if (CallSetRelationPod(rel, other, relation) == 1) ++*relSet; else ++*faults; }
    else if (trustDiffers) Forward(rel, other, 0);
}
bool RelationsOwnRestore(const coopown::FactionRec& rec, std::string* why, std::string* detail)
{
    ::Faction* mine = coop::LocalPlayerFaction();
    if (!Plaus(mine) || !Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) { *why = "no-player-faction"; return false; }
    if (kGetDataRva == 0 || kSetRelationRva == 0) { *why = "no-address"; return false; }
    void* rm = RelationsOfPod(mine);
    if (!Plaus(rm)) { *why = "no-relations"; return false; }
    long long rows = 0, missing = 0, relSet = 0, trustSet = 0, faults = 0;
    for (size_t i = 0; i < rec.rows.size(); ++i)
    {
        const coopown::FacRow& r = rec.rows[i];
        ::Faction* f = coop::GameWorldPtr()->factionDirectory->findFactionById(r.sid);
        if (!Plaus(f) || f == mine || coop::IsPlayerFaction(f) || coop::IsPeerFaction(f)) { ++missing; continue; }
        void* rf = RelationsOfPod(f);
        if (!Plaus(rf)) { ++missing; continue; }
        ++rows;
        Entry a, b;
        if (ReadEntry(rm, f, &a) != 1 || ReadEntry(rf, mine, &b) != 1) { ++faults; continue; }
        OwnRestorePair(rm, f, a, r.rel, r.trust, r.trustNeg, r.flags, &relSet, &trustSet, &faults);   /* mmo3 fold 5/7 */
        OwnRestorePair(rf, mine, b, r.relBack, r.trustBack, r.trustNegBack, r.flagsBack, &relSet, &trustSet, &faults);
    }
    int pid = 0, pidAfter = 0;
    if (OwnPlatoonIdsPod(mine, 0, &pid) == 1)
    {
        pidAfter = coopown::PlatoonIdsAfterRestore(pid, rec.platoonIds);
        if (pidAfter != pid && OwnPlatoonIdsPod(mine, 1, &pidAfter) != 1) ++faults;
    }
    else ++faults;
    const std::string liveName = mine->getName();
    *detail += " faction[rows,missing,relationSet,trustSet,faults]=" + S(rows) + "," + S(missing) + "," + S(relSet) + "," + S(trustSet) + "," + S(faults)
             + " platoonIDs[live,record,after]=" + S(pid) + "," + S(rec.platoonIds) + "," + S(pidAfter)
             + " name[record,live]='" + rec.name + "','" + liveName + "'" + (rec.name == liveName ? std::string() : std::string(" (differs - not renamed here)"));
    if (rows > 0 && faults >= rows) { *why = "fault"; return false; }
    return true;
}

/* ---- par24: the notebook side of the world-vs-world table (store.cpp's drain, the K2 safe point, the test verb) ---- */
void WorldRelTableReset()
{
    /* a WELCOME: the push that follows re-sends every row of THIS notebook's world; the table is written over this game's
       values again once the push is complete, and the walk is owed again after that */
    g_wr.clear(); g_wrPending.clear(); g_wrPendingChange.clear(); g_wrDirty.clear(); g_wrSeedOut.clear(); g_wrSent.Clear();   /* review-par24 #2: a new link answers no send of the old one */
    g_wrApplyAll = true; g_wrTableApplied = false; g_wrSeedState = 0; g_wrSeedEndSent = false;
}
void WorldRelNoteRows(const std::vector<char>& payload)
{
    int kind = 0; std::vector<coopwrel::WireRow> in;
    if (coopwrel::Decode(payload.empty() ? 0 : &payload[0], payload.size(), &kind, &in) == 0)
    { if (++g_wrMalformed <= 5) DebugLog("[WREL] a malformed WORLD_REL from the notebook - ignored"); return; }
    for (size_t i = 0; i < in.size(); ++i)
    {
        ++g_wrRowsIn;
        const std::string key = coopwrel::Key(in[i].a, in[i].b);
        if (kind == coopwrel::kWrEcho) { ++g_wrEchoes; g_wrSent.Echo(key); }   /* review-par24 #2: one of this game's own sends answered - re-check A: counted BEFORE the merge, so an echo row this game's full table refuses still answers the send (the pair would otherwise wait until the next link) */
        if (coopwrel::Merge(&g_wr, in[i].a, in[i].b, in[i].row, coopwrel::kWrRows) < 0) { ++g_wrMalformed; continue; }
        if (!coopwrel::MayApply(g_wrDirty.count(key) != 0, g_wrSent.Waiting(key))) ++g_wrHeldForOwn;   /* held at the drain: this game has a newer change of that pair unsent or unanswered */
        if (kind == coopwrel::kWrChange) g_wrPendingChange.insert(coopwrel::Key(in[i].a, in[i].b));
        g_wrPending.insert(coopwrel::Key(in[i].a, in[i].b));   /* written at the next safe point (after the whole table, the first time) */
    }
}
/* MAIN THREAD, K2 safe point (combat.cpp, worker paused): the notebook's rows written into this game's engine */
void WorldRelSafePointDrain()
{
    if (!g_on || !GameplayRunning() || EngineWritesBlocked()) return;
    if (StoreWelcomePushDone() == 0) return;   /* no notebook, or its table has not all arrived: this game keeps its save's values meanwhile */
    if (!Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory) || kGetDataRva == 0) return;
    if (g_wrApplyAll)
    {
        long long a0 = g_wrApplied, s0 = g_wrSame, u0 = g_wrUnresolved, f0 = g_wrApplyFaults;
        for (coopwrel::Table::const_iterator it = g_wr.begin(); it != g_wr.end(); ++it) WorldRelCountApply(WorldRelApplyRow(it->first, it->second, true));
        g_wrPending.clear(); g_wrPendingChange.clear(); g_wrApplyAll = false; g_wrTableApplied = true; ++g_wrTableApplies;
        DebugLog("[WREL] table applied: rows=" + S((long long)g_wr.size()) + " written=" + S(g_wrApplied - a0) + " same=" + S(g_wrSame - s0)
                 + " unresolved=" + S(g_wrUnresolved - u0) + " faults=" + S(g_wrApplyFaults - f0)
                 + " (the notebook's world-vs-world standings over this save's; engine changes to world pairs go to the notebook from now on)");
    }
    else if (!g_wrPending.empty())
    {
        /* review-par24 #2: a row for a pair this game changed and has not sent (dirty - the off-thread queue first), or whose
           last send the notebook has not answered, is older than this game's value: it stays pending, and is written once
           the echo of the last send is back (g_wr then holds the notebook's latest row for it) */
        WorldRelTakeOffQueue();
        std::set<std::string> keep, keepChange;
        for (std::set<std::string>::const_iterator it = g_wrPending.begin(); it != g_wrPending.end(); ++it)
        {
            if (!coopwrel::MayApply(g_wrDirty.count(*it) != 0, g_wrSent.Waiting(*it))) { keep.insert(*it); if (g_wrPendingChange.count(*it)) keepChange.insert(*it); continue; }
            coopwrel::Table::const_iterator r = g_wr.find(*it); if (r != g_wr.end()) WorldRelCountApply(WorldRelApplyRow(r->first, r->second, g_wrPendingChange.count(*it) == 0));
        }
        g_wrPending.swap(keep); g_wrPendingChange.swap(keepChange);
    }
    if (g_wrTableApplied && g_wrSeedState == 0) WorldRelSeedWalk();
}
/* TEST-ONLY verb `worldrel [set <a> <b> <value> | test | get <a> <b> | undo]` (decision 17: a lever, not a gate). `set` moves ONE
   world-vs-world standing on THIS game through the engine's own setRelation 0x6B4A30 - the hooked setter, so the change goes
   the way an engine change does (Forward -> WorldRelNote -> the notebook -> every game). `test` picks the notebook table's
   FIRST row (key order - the same pair on every game) that resolves here and sets it to -37.5 (12.5 when it already is).
   `get` prints the live entry and the table's row. Bare: the report line. MAIN THREAD (the command channel tick).
   review-par24 #4: -37.5 is past the engine's -30 enemy line (peace.cpp: FactionRelations::_isEnemy), so `test` makes two NPC
   factions ENEMIES for every game of the world; `undo` puts every pair `test` / `set` moved back to its value before the
   first move, through the same setRelation (so it travels the same way). */
std::string WorldRelCommand(const std::string& args)
{
    std::istringstream is(args);
    std::string sub, a, b, v;
    is >> sub >> a >> b >> v;
    if (sub.empty()) { ReportRelations(); return "ok worldrel"; }
    if (!GameplayRunning() || EngineWritesBlocked() || !Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) { DebugLog("[WREL] " + sub + " refused: no running world (or engine writes are blocked)"); return "error worldrel not-in-game"; }
    if (kSetRelationRva == 0 || kGetDataRva == 0) return "error worldrel no-address";
    if (sub == "undo")
    {
        if (g_wrUndo.empty()) { DebugLog("[WREL] undo: nothing to put back (no worldrel test/set since the last undo)"); return "error worldrel nothing-to-undo"; }
        std::map<std::string, float> failed;
        int put = 0;
        for (std::map<std::string, float>::const_iterator it = g_wrUndo.begin(); it != g_wrUndo.end(); ++it)
        {
            std::string ua, ub;
            if (!coopwrel::SplitKey(it->first, &ua, &ub)) continue;
            ::Faction* ua_f = coop::GameWorldPtr()->factionDirectory->findFactionById(ua); ::Faction* ub_f = coop::GameWorldPtr()->factionDirectory->findFactionById(ub);
            void* urel = (Plaus(ua_f) && Plaus(ub_f) && ua_f != ub_f && WorldPair(ua_f, ub_f)) ? RelationsOfPod(ua_f) : 0;
            Entry cur;
            if (!Plaus(urel) || ReadEntry(urel, ub_f, &cur) != 1 || CallSetRelationPod(urel, ub_f, it->second) != 1) { failed.insert(*it); continue; }
            ++put; ++g_wrUndone;
            DebugLog("[WREL] UNDO " + ua + "->" + ub + " relation " + S(cur.relation) + " -> " + S(it->second)
                     + " (the value before the first worldrel test/set of this pair; the engine's setRelation, so it travels as an engine change)");
        }
        g_wrUndo.swap(failed);
        DebugLog("[WREL] undo: put back=" + S((long long)put) + " not put back=" + S((long long)g_wrUndo.size()) + " (kept for another undo)");
        return g_wrUndo.empty() ? "ok worldrel undo" : "error worldrel undo-partial";
    }
    float value = 0.0f;
    if (sub == "test")
    {
        a.clear(); b.clear();
        for (coopwrel::Table::const_iterator it = g_wr.begin(); it != g_wr.end() && a.empty(); ++it)
        {
            std::string ta, tb; if (!coopwrel::SplitKey(it->first, &ta, &tb)) continue;
            ::Faction* fa = coop::GameWorldPtr()->factionDirectory->findFactionById(ta); ::Faction* fb = coop::GameWorldPtr()->factionDirectory->findFactionById(tb);
            if (Plaus(fa) && Plaus(fb) && fa != fb && WorldPair(fa, fb)) { a = ta; b = tb; value = (it->second.relation == -37.5f) ? 12.5f : -37.5f; }
        }
        if (a.empty()) { DebugLog("[WREL] test refused: the notebook's table has no row that resolves here (rows=" + S((long long)g_wr.size()) + ")"); return "error worldrel no-row"; }
    }
    else if (sub == "set" || sub == "get")
    {
        if (a.empty() || b.empty() || (sub == "set" && v.empty())) return "error worldrel usage";
        if (sub == "set") { char* end = 0; const double d = std::strtod(v.c_str(), &end); if (end == 0 || *end != 0 || !(d >= -100.0 && d <= 100.0)) return "error worldrel value"; value = (float)d; }
    }
    else return "error worldrel usage";
    ::Faction* fa = coop::GameWorldPtr()->factionDirectory->findFactionById(a); ::Faction* fb = coop::GameWorldPtr()->factionDirectory->findFactionById(b);
    if (!Plaus(fa) || !Plaus(fb) || fa == fb || !WorldPair(fa, fb)) { DebugLog("[WREL] " + sub + " refused: '" + a + "' -> '" + b + "' is not a world-vs-world pair here (a player's faction or stand-in, unknown, or the same faction)"); return "error worldrel not-a-world-pair"; }
    void* rel = RelationsOfPod(fa);
    Entry before;
    if (!Plaus(rel) || ReadEntry(rel, fb, &before) != 1) return "error worldrel fault";
    coopwrel::Table::const_iterator row = g_wr.find(coopwrel::Key(a, b));
    const std::string tableText = row == g_wr.end() ? std::string("none") : S(row->second.relation);
    if (sub == "get")
    {
        DebugLog("[WREL] get " + a + "->" + b + " live=" + S(before.relation) + " table=" + tableText + " flags=" + S(before.flags));
        return "ok worldrel get";
    }
    if (g_wrUndo.count(coopwrel::Key(a, b)) == 0) g_wrUndo[coopwrel::Key(a, b)] = before.relation;   /* review-par24 #4: the value `worldrel undo` puts back */
    if (CallSetRelationPod(rel, fb, value) != 1) return "error worldrel fault";
    Entry after; const int ra = ReadEntry(rel, fb, &after);
    ++g_wrTestSets;
    g_wrLastTest = a + "->" + b + "=" + S(value);
    DebugLog("[WREL] TEST set " + a + "->" + b + " relation " + S(before.relation) + " -> " + (ra == 1 ? S(after.relation) : std::string("n/a"))
             + " table=" + tableText + " dirty=" + S(g_wrDirty.count(coopwrel::Key(a, b)) ? 1 : 0) + " tableApplied=" + S(g_wrTableApplied ? 1 : 0)
             + " (TEST-ONLY lever; the engine's setRelation, so it travels as an engine change: this game -> the notebook -> every game)");
    return "ok worldrel " + sub;
}
}
