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
#include "../common/playerstab.h"   /* T-545: the other game's lines when another player's stance crosses the engine's lines */
#include "team.h"                   /* T-546 step 4: TeamSameAnyThread - a teammate's side is held at ally */
#include "peace.h"                  /* T-546 (owner 512): PeaceTeamPairAnyThread - the team pair */
#include "../common/teameffect.h"   /* T-546 step 4: the pin's value and the stance a teammate may be given */
#include "../common/teamstanding.h"   /* T-546 step 5: the team reason, a teammate's change taken, the either-side rule */
#include "ownstore.h"   /* mmo3: OwnNoteFactionChange, OwnWriterOn */
#include "../common/ownrec.h"   /* mmo3: the pp.faction record */
#include "../common/slotwire.h"   /* stand1: @slot:<n> wire names */
#include "../common/worldrelwire.h"   /* par24: the notebook's world-vs-world table (WORLD_REL) */
#include "../common/relside.h"   /* the entry a changer wrote: on the player faction's relations, the other faction's side */
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

namespace coop { void TeamMarkOwn(::Faction* owner, ::Faction* other); }   /* T-546 step 5: defined with the shared standing below */
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
bool g_teamWrite = false;   /* T-546 step 5: the write under way is the team's (the record written, a restore's NPC standing): Forward sends it with swteam::kRelReasonTeam and does not mark it as this game's own change */
bool g_noTeamMark = false;  /* T-546 step 5: the write under way is not this game's own change (the load's own-record restore, a departure's sides put back) */
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
          g_snapshots = 0, g_snapshotEntries = 0, g_sendFailed = 0, g_offNoLink = 0;
volatile LONG64 g_faults = 0;   /* bumped on worker threads too: always interlocked */
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
/* Player n's faction on this game: the stand-in met this session, else the coop-p<n> faction this world's save carries (a player
   not seen since the world loaded; the PLAYERS tab lists them as offline). MAIN THREAD. */
::Faction* StandInOrCarried(int n)
{
    ::Faction* f = coop::StandInForSlot(n);
    if (Plaus(f)) return f;
    if (!Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) return 0;
    f = coop::GameWorldPtr()->factionDirectory->findFactionById(coopslot::StandInId(n));
    return (Plaus(f) && coop::StandInRecordSlot(f) == n) ? f : 0;
}
/* another player's faction: a stand-in met this session, or a coop-p<n> the save carries for a slot that is not this game's.
   ANY THREAD (pointer compares and a guarded read of the record id). */
bool OtherPlayersFaction(::Faction* f)
{
    if (!Plaus(f)) return false;
    if (coop::IsPeerFaction(f)) return true;
    const int s = coop::StandInRecordSlot(f);
    return s >= 0 && s != coop::MySlotForWire();
}
long long g_relNotices = 0, g_relNoticesEngine = 0;   /* sentences shown here for another player's change; crossings this game's engine announced itself */
long long g_relTeamQuiet = 0;   /* another player's changes not shown: the team's own pin, unpin or restore writes */
playerstab::NoticeBook g_noticeBook;   /* what this player has been told about each other player's side towards them (MAIN THREAD) */
volatile LONG64 g_playerPairEpoch = 0;   /* RelationsPlayerPairEpoch */
// stand1 (docs/design-profiles1.md s2 Required 2): the wire name of a faction. A PLAYER faction travels by notebook SLOT - mine
// as "@slot:<my slot>:<my faction's name>" (the name lets the other game follow a rename), a stand-in - met this session, or the
// coop-p<n> faction the save carries - as "@slot:<its slot>", and every other faction by its stringID. Slots are absolute, so one pair has one name on both games (protocol 67's
// "@player:" / "@peer" meant opposite things at the two ends and had to be swapped). A player faction with no slot yet has
// no wire name: Snapshot waits for one, and a live change meanwhile counts as a fault.
std::string SidOf(::Faction* f)
{
    if (!Plaus(f)) return std::string();
    if (coop::IsPeerFaction(f)) return coopslot::SlotWire(coop::StandInSlotOf(f), std::string());
    if (coop::IsPlayerFaction(f)) { const int me = coop::MySlotForWire(); return me < 0 ? std::string() : coopslot::SlotWire(me, f->getName()); }
    char buf[160];
    if (!ReadSidPod(f, buf, 160)) return std::string();
    return coopslot::StandInIdAsSlotWire(std::string(buf), coop::MySlotForWire());
}
// "@slot:<n>[:<name>]" on THIS game: my slot is my player faction; another slot is that slot's stand-in - created (the CreatePeer
// path) only when the name came with it, i.e. the sender's own player faction.
::Faction* FactionOfSlot(const std::string& sid, bool mayCreate)
{
    int n = -1; bool hasName = false;
    if (!coopslot::ParseSlotWire(sid, &n, 0, &hasName) || n < 0) return 0;
    if (n == coop::MySlotForWire()) return coop::LocalPlayerFaction();
    if (mayCreate && hasName) return coop::ResolveWireFaction(sid);
    return StandInOrCarried(n);
}
::Faction* FactionOf(const std::string& sid)
{
    if (sid.empty() || !Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) return 0;
    const std::string w = coopslot::StandInIdAsSlotWire(sid, coop::MySlotForWire());   /* a carried player's record id names that player's slot */
    if (coopslot::IsSlotWire(w)) return FactionOfSlot(w, true);
    return coop::GameWorldPtr()->factionDirectory->findFactionById(sid);
}
// the same names produced by SidOf on THIS machine (the off-thread queue): never creates a stand-in
::Faction* FactionOfLocal(const std::string& sid)
{
    if (sid.empty() || !Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) return 0;
    const std::string w = coopslot::StandInIdAsSlotWire(sid, coop::MySlotForWire());
    if (coopslot::IsSlotWire(w)) return FactionOfSlot(w, false);
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
/* the entry's ally flag (+0) to `on` - a plain store, as WriteEntry's */
int SetAllyFlagPod(void* rel, ::Faction* other, bool on)
{
    __try
    {
        GetDataFn get = (GetDataFn)(Base() + kGetDataRva);
        char* d = (char*)get(rel, other);
        if (!Plaus(d)) return 0;
        d[0] = on ? 1 : 0;
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
    if (coop::IsPlayerFaction(a) || coop::IsPlayerFaction(b)) ::InterlockedIncrement64(&g_playerPairEpoch);
    if ((coop::IsPeerFaction(a) && coop::IsPlayerFaction(b)) || (coop::IsPeerFaction(b) && coop::IsPlayerFaction(a))) coop::TagsCaptionsDirty();
}
// Ownership: a standing involving MY player faction is mine; one involving the peer's is theirs; world-vs-world is the host's.
// review-p3-factions: the OWNER side decides first, so a player<->player pair has exactly one owner per direction
// (mine->peer is mine, peer->mine is theirs); then the other side; world-vs-world is the host's.
/* ANY THREAD: pointer compares and guarded reads (IsPlayerFaction, OtherPlayersFaction, StandInRecordSlot) */
relside::Side FacSide(::Faction* f)
{
    relside::Side r;
    r.mine = coop::IsPlayerFaction(f);
    r.otherPlayers = OtherPlayersFaction(f);   /* another player's faction - met this session or carried by the save - is that player's side */
    r.world = !r.mine && !coop::IsPeerFaction(f) && coop::StandInRecordSlot(f) == -1;
    return r;
}
bool Owned(::Faction* a, ::Faction* b)
{
    return relside::OwnsPair(FacSide(a), FacSide(b));   /* par24 (decision 48): world-vs-world is no game's; WorldPair routes it to the notebook */
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
    return relside::WorldPair(FacSide(a), FacSide(b));
}
DWORD g_mainThread = 0; volatile LONG64 g_offThread = 0, g_queued = 0, g_queueDropped = 0; long long g_drained = 0, g_drainUnresolved = 0;
// F483: the relation changers fire mostly on the AI worker thread (254/197 per run). The wire and the maps are main-thread only,
// so an off-thread change is captured as POD (the entry is read there and then - the engine has just written it) and queued under
// a lock; the tick drains the queue and forwards. Pointers are not kept: the sids are read at capture time (SidOf is string work
// on the plugin's own heap, allocator-safe off-thread).
struct Queued { std::string a, b; unsigned reason; bool noticed; };   /* noticed: a put-back of a move whose changer printed the engine's notice */   // review-p3n F4: sids only - the drain re-reads the entry live
// review-p3p #5: the last value the OWNER sent for a pair we do not own - when our own engine moves that entry (it sees the
// peer's puppets act), it is put back at once (main thread) or on the next tick (worker thread); no periodic re-send needed.
std::map<std::string, Entry> g_ownerValues;              // key: ownerSid + "|" + otherSid (RECEIVER-side sids as they arrived)
std::vector<Queued> g_revertQueue;                        // off-thread notOwned changes, drained on the tick
long long g_reverted = 0, g_revertQueued = 0, g_revertNoValue = 0;
std::string PairKey(const std::string& a, const std::string& b) { return a + "|" + b; }
// the key under which a pair this game does not own ARRIVED. stand1: slots are absolute, so it is this game's own names in
// key form (coopslot::SlotWireKey drops the player's name); protocol 67's marker swap is gone.
std::string SideKey(const std::string& sid) { return coopslot::RelationSideKey(sid, coop::MySlotForWire()); }
std::string RecvKeyOfLocal(::Faction* owner, ::Faction* other)
{
    return PairKey(SideKey(SidOf(owner)), SideKey(SidOf(other)));
}
// P081 (rel4): which pairs this game's engine moved and the mod put back - per pair: a count, the sum of the moves, and the
// first 3 lines with a move of at least 0.01 (T318: 40 lines of 1e-5 drift hid everything else)
struct P081Pair { long long n; long long small; double sum; int lines; };
std::map<std::string, P081Pair> g_p081;
/* This game's engine moved the other player's side towards this player through a changer that prints its notice: the player
   has been told about the engine's value (NoticeBook::EngineSaid). `from` is the owner's value the engine moved it from.
   MAIN THREAD. */
void NoteEngineNotice(::Faction* owner, ::Faction* other, const Entry& from, const Entry& to)
{
    if (!coop::IsPlayerFaction(other) || !OtherPlayersFaction(owner)) return;
    if (g_noticeBook.EngineSaid(RecvKeyOfLocal(owner, other), from.relation, to.relation, (to.flags & 1u) != 0)) ++g_relNoticesEngine;
}
/* The mod put the other player's side towards this player back to its owner's value: the book holds the value put back, with
   no line, so it equals what this game shows and the owner's next change is counted from it. MAIN THREAD. */
void NotePutBack(::Faction* owner, ::Faction* other, const Entry& put)
{
    if (!coop::IsPlayerFaction(other) || !OtherPlayersFaction(owner)) return;
    g_noticeBook.Quiet(RecvKeyOfLocal(owner, other), put.relation, (put.flags & 1u) != 0);
}
/* `noticed`: the engine's changer that moved it calls the engine's notice (Forward's caller says) */
int RevertPair(::Faction* owner, ::Faction* other, bool noticed)
{
    std::map<std::string, Entry>::const_iterator it = g_ownerValues.find(RecvKeyOfLocal(owner, other));
    if (it == g_ownerValues.end()) { ++g_revertNoValue; return 0; }
    void* rel = RelationsOfPod(owner); if (!Plaus(rel)) return -1;
    Entry engine; const int rd = ReadEntry(rel, other, &engine);   // P081: the value the engine just wrote
    if (noticed && rd == 1) NoteEngineNotice(owner, other, it->second, engine);
    g_applying = true; const int w = WriteEntry(rel, other, it->second); g_applying = false;
    if (w == 1) { ++g_reverted; TagNoteRelation(owner, other); NotePutBack(owner, other, it->second); }
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
void QueueOffThread(void* rel, ::Faction* other, unsigned reason, bool noticed)
{
    ::Faction* owner = OwnerOf(rel);
    if (!Plaus(owner) || !Plaus(other)) { ::InterlockedIncrement64(&g_faults); return; }
    const int road = relside::PairRoad(FacSide(owner), FacSide(other));
    if (road == relside::kRoadWorldTable) { WorldRelQueueOff(owner, other); return; }   /* par24: the notebook's, not the session's */
    if (road == relside::kRoadPutBack)
    {
        ++g_notOwned;
        Queued rq; rq.a = SidOf(owner); rq.b = SidOf(other); rq.reason = 2; rq.noticed = noticed;   // review-p3p #5: revert on the tick
        if (g_queueLockInit) { ::EnterCriticalSection(&g_queueLock); if (g_revertQueue.size() < 4096) { g_revertQueue.push_back(rq); ++g_revertQueued; } ::LeaveCriticalSection(&g_queueLock); }
        return;
    }
    Queued q; q.a = SidOf(owner); q.b = SidOf(other); q.reason = reason; q.noticed = false;   // no engine map access off-thread beyond the engine's own
    if (coopslot::IsLegacyPeerId(q.a) || coopslot::IsLegacyPeerId(q.b)) { ++g_legacySkipped; return; }   /* stand1 */
    if ((q.a.empty() || q.b.empty()) && coop::MySlotForWire() < 0) { coop::NoteHeldForSlot(); return; }   /* stand1 fold (1d): held - the snapshot sent when my slot arrives carries it */
    if (q.a.empty() || q.b.empty()) { ::InterlockedIncrement64(&g_faults); return; }
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
    for (size_t i = 0; g_on && i < reverts.size(); ++i) { ::Faction* o = FactionOfLocal(reverts[i].a); ::Faction* t = FactionOfLocal(reverts[i].b); if (Plaus(o) && Plaus(t)) RevertPair(o, t, reverts[i].noticed); }   // review-p3r M5
    WorldRelTakeOffQueue();   /* par24: the off-thread world-pair changes, into the tick's dirty set */
    if (local.empty()) return;
    for (size_t i = 0; i < local.size(); ++i)   /* T-546 step 5: the engine's own changes on the worker threads, marked whatever the link */
        if (local[i].reason == 0) { ::Faction* o = FactionOfLocal(local[i].a); ::Faction* t = FactionOfLocal(local[i].b); if (Plaus(o) && Plaus(t)) coop::TeamMarkOwn(o, t); }
    if (!g_on || (!coop::net::SessionLinked() && !coop::StoreLiveReady())) { g_offNoLink += (long long)local.size(); return; }   /* M5a fold 1 (#4): a notebook-only game sends too */
    for (size_t i = 0; i < local.size(); ++i)
    {
        const Queued& q = local[i];
        ::Faction* owner = FactionOfLocal(q.a); ::Faction* other = FactionOfLocal(q.b);   // sender-side sids (T173: FactionOf swapped the players)
        if (!Plaus(owner) || !Plaus(other)) { ++g_drainUnresolved; continue; }   // renamed or freed since capture
        void* rel = RelationsOfPod(owner); if (!Plaus(rel)) { ::InterlockedIncrement64(&g_faults); continue; }
        Entry e; if (ReadEntry(rel, other, &e) != 1) { ::InterlockedIncrement64(&g_faults); continue; }   // main thread, the newest value
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
/* changer calls on the player faction's relations that wrote the other faction's entry towards the player (WrittenPair) */
volatile LONG64 g_wroteOtherSide = 0;
/* noticed: the hooked changer is one that calls the engine's notice 0x6B25A0 (affectRelations both overloads,
   setNoLongerEnemies, declareWar, setEnemy - build/decomp_6b2b50 / 6b29d0 / 6b2c40 / 6b2cb0 / 6b2e40); setRelation and
   affectTrust print nothing.
   changer: which hooked changer moved the entry (relside::Changer), or kNotAChanger for a read of the pair as named. A changer
   called on the player faction's relations wrote X -> player (src/common/relside.h): that pair is the one forwarded, put back
   or recorded. ANY THREAD up to the thread test: guarded reads only. */
void Forward(void* rel, ::Faction* other, unsigned reason, bool noticed = false, int changer = relside::kNotAChanger)
{
    if (changer != relside::kNotAChanger && Plaus(rel) && Plaus(other))
    {
        ::Faction* owner = OwnerOf(rel);
        ::Faction* wOwner = owner; ::Faction* wOther = other;
        relside::WrittenPair(changer, Plaus(owner) && coop::IsPlayerFaction(owner), owner, other, &wOwner, &wOther);
        if (wOwner != owner)
        {
            void* wRel = RelationsOfPod(wOwner);
            if (!Plaus(wRel)) { ::InterlockedIncrement64(&g_faults); return; }
            ::InterlockedIncrement64(&g_wroteOtherSide);
            rel = wRel; other = wOther;
            noticed = false;   /* the engine's notice prints only when the faction it was called towards is the player's - here it was not */
        }
    }
    if (reason == 0) OwnFacNote(rel, other);   /* mmo3: every detoured writer comes through here with reason 0 (the snapshot passes 1) */
    if (reason == 0) TagNoteRelation(Plaus(rel) ? OwnerOf(rel) : 0, other);
    const int moment = relside::ForwardMoment(g_applying, g_mainThread != 0, ::GetCurrentThreadId() == g_mainThread);
    if (moment == relside::kMomentEcho) { ++g_echoSuppressed; return; }
    if (moment == relside::kMomentQueue) { ::InterlockedIncrement64(&g_offThread); if (g_on) QueueOffThread(rel, other, reason, noticed); return; }
    ++g_changes;
    if (reason == 0 && !g_teamWrite && !g_noTeamMark) coop::TeamMarkOwn(OwnerOf(rel), other);   /* T-546 step 5: this game's own change */
    if (!g_on) return;
    ::Faction* owner = OwnerOf(rel);
    if (!Plaus(owner) || !Plaus(other)) { ::InterlockedIncrement64(&g_faults); return; }
    const int road = relside::PairRoad(FacSide(owner), FacSide(other));
    if (road == relside::kRoadWorldTable) { WorldRelNote(owner, other); return; }   /* par24: world-vs-world goes to the notebook, never on the session link */
    if (road == relside::kRoadPutBack) { ++g_notOwned; if (g_on) RevertPair(owner, other, noticed); return; }   // review-p3p #5: put the owner's value back now
    if (!coop::net::SessionLinked() && !coop::StoreLiveReady()) { ++g_offNoLink; return; }   /* M5a fold 1 (#4): a game linked only to the notebook (a third player) sends its standings too - session.cpp picks the road(s), one delivery per destination */
    Entry e; if (ReadEntry(rel, other, &e) != 1) { ::InterlockedIncrement64(&g_faults); return; }
    const std::string a = SidOf(owner), b = SidOf(other);
    if (coopslot::IsLegacyPeerId(a) || coopslot::IsLegacyPeerId(b)) { ++g_legacySkipped; return; }   /* stand1: a save's protocol-67 stand-in is dormant */
    if ((a.empty() || b.empty()) && coop::MySlotForWire() < 0) { coop::NoteHeldForSlot(); return; }   /* stand1 fold (1d): held - the snapshot sent when my slot arrives carries it */
    if (a.empty() || b.empty()) { ::InterlockedIncrement64(&g_faults); return; }
    if (coop::net::SendRelation(a, b, e.relation, e.trust, e.trustNeg, e.flags, (g_teamWrite && reason == 0) ? swteam::kRelReasonTeam : reason)) { ++g_forwarded; g_lastForward = a + "->" + b + "=" + S(e.relation); }
    else ++g_sendFailed;
}

float detour_affect(void* self, ::Faction* f, float amount, float mult) { float r = orig_affect(self, f, amount, mult); Forward(self, f, 0, true, relside::kAffect); return r; }
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
/* T-546 (owner 512): standing changes by event (a hit, a shot) between two factions of this player's team that this game's engine
   would otherwise make - the shooter's game moving a teammate's stand-in towards this player when its copy is shot. Not passed to
   the engine, as the player's own relations take none (PlayerFactionRelations' slot +0x28 is empty, H071): no move, no notice, no
   forward, no revert. ANY THREAD (an interlocked count). */
volatile LONG64 g_teamEvSkipped = 0;
void detour_affectEv(void* self, ::Faction* f, int ev, float mult)
{
    if (coop::PeaceTeamPairAnyThread(Plaus(self) ? OwnerOf(self) : 0, f)) { ::InterlockedIncrement64(&g_teamEvSkipped); return; }
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
    Forward(self, f, 0, true, relside::kAffectEvent);
}
void detour_declareWar(void* self, ::Faction* f) { orig_declareWar(self, f); Forward(self, f, 0, true, relside::kDeclareWar); }
void detour_noLonger(void* self, ::Faction* f) { orig_noLonger(self, f); Forward(self, f, 0, true, relside::kNoLongerEnemies); }
void detour_setEnemy(void* self, ::Faction* f) { orig_setEnemy(self, f); Forward(self, f, 0, true, relside::kSetEnemy); }
void detour_setRelation(void* self, ::Faction* f, float v) { orig_setRelation(self, f, v); Forward(self, f, 0, false, relside::kSetRelation); }
void detour_affectTrust(void* self, ::Faction* f, float amount, float mult) { orig_affectTrust(self, f, amount, mult); Forward(self, f, 0, false, relside::kAffectTrust); }

// The owned snapshot: every faction's standing towards my player faction, and my player faction's own row towards every other
// faction. This game's engine never reads that own row (the player's lookup reads X -> me), but the other games keep it as the
// value their stand-in for this player puts back to - and the stand-in's table IS read there - so it is still sent; absent
// entries are created at the engine's default, which is what the engine would do on first contact.
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
    if (!ReadFactionArrayPod(&arr, &n)) { ::InterlockedIncrement64(&g_faults); return false; }
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

/* T-546 step 5 (the shared standing block below RelationsOwnRestore): another player's side towards this game's team */
int TeamFoldTheirs(::Faction* theirs, int level);

void SetRelationsOn(bool on) { g_on = on; DebugLog(std::string("[REL] ") + (on ? "ON" : "OFF")); }
void RelationsSendSnapshot() { Snapshot(); }
void RelationsNoteRelayedDropped() { g_relSyncOwed = true; ++g_relDroppedResync; }   /* M5a fold 1 (#2b) */
void RelationsForgetQueue() { RelationsTeamForgetBase(); g_noticeBook.Clear(); g_wrApplyAll = true; g_wrTableApplied = false; g_wrSeedState = 0; g_wrSeedOut.clear(); g_wrDirty.clear(); g_wrPending.clear(); g_wrPendingChange.clear(); g_wrSent.Clear();   /* par24: the next world writes the notebook's whole table (g_wr is kept - the notebook's truth, not a pointer) and re-owes its walk */ if (!g_queueLockInit) return; ::EnterCriticalSection(&g_queueLock); g_queue.clear(); g_revertQueue.clear(); g_wrOffQueue.clear(); ::LeaveCriticalSection(&g_queueLock); }   // review-p3r M4: g_ownerValues (sids -> values) stays - the owner's truth, not a pointer   // review-p3o M3: old-world entries must not be re-resolved against the new world   // an event (a rename) - the owned entries carry "@player:<new name>"

void ApplyRemoteRelation(const std::string& ownerSid, const std::string& otherSid, float relation, float trust, float trustNeg,
                         unsigned int flags, unsigned int reason)
{
    ++g_received;
    if (!g_on) return;
    /* M5b (carried from M5a) - A PAIR BELONGS TO ITS SENDER. The player on the pair's owner side (else its other side - Owned())
       must be the sending player: the notebook's stamp for a relayed pair, the session peer's PEER_SLOT otherwise. Checked
       before any name is resolved, so a refused pair creates no stand-in. Unknown on either side: taken as before. */
    {
        const int me = coop::MySlotForWire();   /* a carried player's record id names that player's slot, as the receiver keys it */
        const int sender = coop::WireSenderSlot(),
                  owning = coopslot::PairOwnerSlot(coopslot::StandInIdAsSlotWire(ownerSid, me), coopslot::StandInIdAsSlotWire(otherSid, me));
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
    if (!Plaus(rel)) { ::InterlockedIncrement64(&g_faults); return; }
    Entry e; e.relation = relation; e.trust = trust; e.trustNeg = trustNeg; e.flags = flags;
    const std::string ka = SideKey(ownerSid), kb = SideKey(otherSid);   /* stand1: keyed by SLOT - absolute on both games, and a rename keeps its key */
    g_ownerValues[PairKey(ka, kb)] = e;
    Entry before; const int rb = ReadEntry(rel, other, &before);
    g_applying = true;
    const int w = WriteEntry(rel, other, e);
    g_applying = false;
    if (w == 1) TagNoteRelation(owner, other);
    /* Another player's side towards this player. The engine prints its sentences only on the game whose engine moved the standing
       (its changers -> 0x6B25A0), fights included; this write is a plain store, so a live change is announced here in the engine's
       words: one line per change of level from what this player was last told (g_noticeBook, which follows what this game shows:
       a change one road already brought adds none, and an engine sentence the mod then put back counts from the value put back).
       A snapshot's re-send only updates what was told. A teammate's side (or one within teamscreen::kMateQuietMs of the two
       joining or parting) moved by the team's own pin, unpin or restore is told nothing either: only the faction lines are. */
    if (w == 1 && coop::IsPlayerFaction(other) && OtherPlayersFaction(owner))
    {
        const std::string key = PairKey(ka, kb);
        const int meQ = coop::MySlotForWire();
        const int ownerSlot = coopslot::PairOwnerSlot(coopslot::StandInIdAsSlotWire(ownerSid, meQ), std::string());
        const bool teamWrite = reason == 0 && rb == 1 && ownerSlot >= 0 && coop::TeamMateQuietAnyThread(meQ, ownerSlot);
        if (teamWrite)
        {
            ++g_relTeamQuiet;
            g_noticeBook.Quiet(key, relation, (flags & 1u) != 0);
            if (g_relTeamQuiet <= 40) DebugLog("[RELATE] their change not shown: slot " + S(ownerSlot) + "'s side " + S(before.relation) + " -> " + S(relation)
                                               + " is the team's own write (teammates, or joined / parted moments ago) (quiet " + S(g_relTeamQuiet) + ")");
        }
        else if (reason != 0 || rb != 1) g_noticeBook.Quiet(key, relation, (flags & 1u) != 0);
        else
        {
            std::string name = coop::StandInDisplayName(owner);
            if (name.empty()) name = owner->getName();
            const std::vector<std::string> lines = g_noticeBook.Live(key, before.relation, (before.flags & 1u) != 0, relation, (flags & 1u) != 0, name);
            for (size_t i = 0; i < lines.size(); ++i)
            {
                const int shown = coop::StoreShowPlayerLine(lines[i]);
                ++g_relNotices;
                DebugLog("[RELATE] their change shown: '" + lines[i] + "' (" + ka + "->" + kb + " " + S(before.relation) + " -> " + S(relation)
                         + (shown == 1 ? ")" : ", message line NOT shown " + S(shown) + ")"));
            }
        }
    }
    /* logged: another game's own change, and a team's write (the record written, a restore) between two players' factions - the
       copy of a member's side that the team's record moved on that member's game */
    if (w == 1)
    {
        ++g_applied;
        const bool teamRow = reason == swteam::kRelReasonTeam;
        g_lastApplied = ka + "->" + kb + "=" + S(relation) + (teamRow ? " (the team's write)" : reason ? " (snapshot)" : "");
        if (reason == 0 || (teamRow && OtherPlayersFaction(owner) && (OtherPlayersFaction(other) || coop::IsPlayerFaction(other)))) DebugLog("[REL] applied " + g_lastApplied);
    }
    else ::InterlockedIncrement64(&g_faults);
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
        ba = TeamFoldTheirs(theirs, ba);   /* T-546 step 5 (476): its side towards any member of this game's team counts */
    }
    if (reads != 0) *reads = (ab != nametag::kUnknown ? 1 : 0) + (ba != nametag::kUnknown ? 1 : 0);
    return nametag::WorseLevel(ab, ba);
}

long long RelationsPlayerPairEpoch() { return (long long)g_playerPairEpoch; }

/* T-545: both directions between this game's player faction and another player's faction, each as the name tag judges it
   (kUnknown when unread). MAIN THREAD - the same inserting accessor as RelationsTagLevel. */
void RelationsStanceLevels(::Faction* mine, ::Faction* theirs, int* you, int* them)
{
    *you = nametag::kUnknown; *them = nametag::kUnknown;
    if (kGetDataRva == 0 || !Plaus(mine) || !Plaus(theirs)) return;
    Entry e = { 0.0f, 0.0f, 0.0f, 0u };
    void* rm = RelationsOfPod(mine);
    if (Plaus(rm) && ReadEntry(rm, theirs, &e) == 1) *you = nametag::LevelOf(e.relation, (e.flags & 1u) != 0);
    void* rt = RelationsOfPod(theirs);
    if (Plaus(rt) && ReadEntry(rt, mine, &e) == 1) *them = nametag::LevelOf(e.relation, (e.flags & 1u) != 0);
    *them = TeamFoldTheirs(theirs, *them);   /* T-546 step 5 (476): its side towards any member of this game's team counts */
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
long long g_relViaAlly = 0, g_relRefusedTarget = 0, g_relRefused = 0, g_relFaults = 0, g_relRefusedTeammate = 0, g_relRefusedFounders = 0, g_relFanOut = 0;
bool g_relFanning = false;   /* T-546 step 5: RelateSet is setting the same stance towards the target's teammates */
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
/* relate1: the plain line. setRelation 0x6B4A30 shows no message (design s3): the PLAYERS tab shows this player's own line
   (playerstab.cpp SetStance) and the other game shows the engine's sentence (ApplyRemoteRelation); the relate verb shows none. */
void RelateAnnounce(float before, const Entry& a, int ra, const std::string& level, const std::string& wire, bool viaAlly, ::Faction* target)
{
    if (viaAlly) ++g_relViaAlly;
    const std::string after = ra == 1 ? S(a.relation) : std::string("n/a");
    g_relLast = level + " " + S(before) + "->" + after + " " + wire + (viaAlly ? " (ally verb)" : "");
    DebugLog("[RELATE] set me->stand-in " + S(before) + " -> " + after + " (" + level + ") wire=" + wire
             + " engineAlly=" + (ra == 1 ? S(EngineAlly(a) ? 1 : 0) : std::string("n/a"))
             + " engineEnemy=" + (ra == 1 ? S(EngineEnemy(a) ? 1 : 0) : std::string("n/a"))
             + (viaAlly ? " via=ally-verb" : ""));
    (void)target;
}
::Faction* PairFaction(const std::string& t)
{
    if (t == "@me") return coop::LocalPlayerFaction();
    if (t == "@peer") return coop::PeerFaction();   /* stand1: kept as the alias for "the one other player" (the game on the session link) */
    if (coopslot::IsSlotWire(t)) { int n = -1; if (!coopslot::ParseSlotWire(t, &n, 0, 0) || n < 0) return 0; return n == coop::MySlotForWire() ? coop::LocalPlayerFaction() : StandInOrCarried(n); }   /* stand1: @slot:<n> */
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
    ::Faction* standin = OtherPlayersFaction(t) ? t : 0;   /* ANY other player's stand-in - @peer (the one on the link), @slot:<n> met this session or carried by the save */
    if (!Plaus(t) || !Plaus(standin))
    {
        ++g_relRefusedTarget;
        DebugLog("[RELATE] refused: '" + target + "' is not another player's stand-in (resolved " + S((const void*)t) + ", stand-in " + S((const void*)standin)
                 + ") - relations are set towards other players only, not towns or factions");
        return "error relate not-a-player";
    }
    /* T-546 step 4: towards a player who shares this player's faction (a team on the world server's table) only ally - the pin
       (team.cpp) holds this side at ally while both are in it */
    {
        int ts = coop::StandInSlotOf(standin);
        if (ts < 0) ts = coop::StandInRecordSlot(standin);
        if (!swteam::StanceAllowedTowards(coop::TeamSameAnyThread(coop::MySlotForWire(), ts), li))
        {
            ++g_relRefusedTeammate;
            DebugLog("[RELATE] " + level + " refused: s" + S(ts) + " shares this player's faction - this game's side towards a teammate is held at ally while both are in it");
            return "error relate teammate";
        }
        /* T-546 step 5 (476): a member's stance towards a player outside its team is the founder's to set */
        if (coop::TeamStanceIsFounders(ts))
        {
            ++g_relRefusedFounders;
            DebugLog("[RELATE] " + level + " refused: this player is a member of a faction - its stance towards s" + S(ts) + " is the founder's to set");
            return "error relate founder-sets";
        }
    }
    if (!Plaus(me)) { ++g_relRefused; DebugLog("[RELATE] " + level + " refused: no player faction on this game"); return "error relate no-faction"; }
    if (kSetRelationRva == 0 || kGetDataRva == 0) { ++g_relRefused; DebugLog("[RELATE] " + level + " refused: setRelation/getRelationData address not in the table"); return "error relate no-address"; }
    void* rel = RelationsOfPod(me);
    Entry b;
    if (!Plaus(rel) || ReadEntry(rel, standin, &b) != 1) { ++g_relFaults; DebugLog("[RELATE] " + level + " FAULT: my relations entry for the stand-in could not be read"); return "error relate fault"; }
    /* A NEUTRAL or HOSTILE choice clears this side's ally FLAG (+0), which answers ally whatever the value (0x6B22E0): a plain
       store BEFORE setRelation, so the entry Forward sends carries it cleared. The engine has no setter that clears it between two
       factions - setNoLongerEnemies 0x6B2C40 clears the atWar byte (+2) only, and the flag is set only on a faction's own entry
       (0x6B3C30 / 0x6B3D80). If setRelation faults the flag is set again: a refused choice leaves the entry as it was. */
    const bool flagCleared = playerstab::ClearsAllyFlag(li, (b.flags & 1u) != 0) && SetAllyFlagPod(rel, standin, false) == 1;
    const WireMark m = MarkWire();
    if (CallSetRelationPod(rel, standin, v) != 1)
    {
        ++g_relFaults;
        const int back = flagCleared ? SetAllyFlagPod(rel, standin, true) : 1;
        DebugLog("[RELATE] " + level + " FAULT: the engine's setRelation raised" + (flagCleared ? (back == 1 ? " - the ally FLAG set again" : " - the ally FLAG could NOT be set again") : ""));
        return "error relate fault";
    }
    Entry a; const int ra = ReadEntry(rel, standin, &a);
    g_allyHaveBefore = false;   /* a chosen level replaces whatever `ally off` would have restored */
    ++g_relSet[li];
    RelateAnnounce(b.relation, a, ra, level, WireSince(m), false, standin);
    if (flagCleared) DebugLog("[RELATE] the entry's ally FLAG was set - cleared with this " + level + " choice (the engine's ally test, the table and the other game's sentence follow the value)");
    if (ra == 1 && li != 0 && (a.flags & 1u))
        DebugLog("[RELATE] the entry's ally FLAG is STILL set after the " + level + " choice - the engine still answers ally");
    /* T-546 step 5 (476, either side): a stance set towards a member of a faction this player is not in is set towards every
       member of that faction - the whole team is in effect what this player chose */
    if (!g_relFanning)
    {
        int ts = coop::StandInSlotOf(standin);
        if (ts < 0) ts = coop::StandInRecordSlot(standin);
        const std::vector<int> more = coop::TeamFanOutTargets(ts);
        std::string said;
        g_relFanning = true;
        for (size_t i = 0; i < more.size(); ++i)
        {
            const std::string r = RelateSet(coopslot::kSlotWirePrefix + coopslot::SlotNum(more[i]), level);
            ++g_relFanOut;
            said += (said.empty() ? "" : ", ") + std::string("s") + S((long long)more[i]) + " " + r;
        }
        g_relFanning = false;
        if (!more.empty()) DebugLog("[RELATE] " + level + " towards s" + S(ts) + " set towards the rest of its faction too (476): " + said);
    }
    return "ok relate " + level;
}

int RelationsMineTowards(int slot, float* v)
{
    if (slot < 0 || slot == coop::MySlotForWire() || !GameplayRunning() || EngineWritesBlocked() || kGetDataRva == 0) return 0;
    ::Faction* me = coop::LocalPlayerFaction();
    ::Faction* t = StandInOrCarried(slot);
    if (!Plaus(me) || !Plaus(t)) return 0;
    void* rel = RelationsOfPod(me);
    Entry e;
    if (!Plaus(rel) || ReadEntry(rel, t, &e) != 1) return 0;
    *v = e.relation;
    return 1;
}
int RelationsPinAlly(int slot, float* before, std::string* why)
{
    float v = 0.0f;
    if (RelationsMineTowards(slot, &v) != 1) { *why = "this game's side towards that player cannot be read now - no world, or no faction of that player here yet"; return -1; }
    *before = v;
    if (!swteam::PinNeedsWrite(true, v)) return 0;
    const std::string r = RelateSet(coopslot::kSlotWirePrefix + coopslot::SlotNum(slot), "ally");
    if (r.compare(0, 3, "ok ") == 0) return 1;
    *why = "the relate road answered '" + r + "'";
    return -2;
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

/* TEST-ONLY lever `relation change crime|war|peace <faction>` and its read-only readout `relation of <faction>`: the engine's own
   changers on an NPC faction X and this game's player faction, as the engine's roads call them -
   crime: X's relations affectRelations(this player's faction, -20, 1) - the witnessed-crime road (SensoryData::assessCrimes
          0x853E40: the witness's faction towards the offender's, .rdata 0x16B0860 = -20.0, 0x167B308 = 1.0);
   war / peace: declareWar / setNoLongerEnemies on THIS PLAYER'S faction's relations towards X - the functions a virtual call on
          a PlayerFactionRelations lands in (vtable +0x40 / +0x38 are not overridden); they write X -> this player.
   Each goes through the hooked function, so the forward runs as for any engine change. MAIN THREAD. */
namespace {
long long g_changeLevers = 0;
int CallChangerPod(int kind, void* rel, ::Faction* f)
{
    __try
    {
        if (kind == 0) { AffectFn fn = (AffectFn)(Base() + kAffectRva); fn(rel, f, -20.0f, 1.0f); }
        else if (kind == 1) { OneFn fn = (OneFn)(Base() + kDeclareWarRva); fn(rel, f); }
        else { OneFn fn = (OneFn)(Base() + kNoLongerRva); fn(rel, f); }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* an NPC faction here by its stringID or its name (never this player's faction or another player's) */
::Faction* NpcFactionNamed(const std::string& key)
{
    if (key.empty() || !Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) return 0;
    ::Faction* f = coop::GameWorldPtr()->factionDirectory->findFactionById(key);
    if (!Plaus(f))
    {
        f = 0;
        void** arr = 0; unsigned n = 0;
        if (!ReadFactionArrayPod(&arr, &n) || !Plaus(arr) || n > 4096) return 0;
        for (unsigned i = 0; i < n && f == 0; ++i) { ::Faction* c = (::Faction*)arr[i]; if (Plaus(c) && c->getName() == key) f = c; }
    }
    if (!Plaus(f) || coop::IsPlayerFaction(f) || OtherPlayersFaction(f)) return 0;
    return f;
}
std::string EntryText(::Faction* a, ::Faction* b)
{
    void* rel = (Plaus(a) && Plaus(b)) ? RelationsOfPod(a) : 0;
    Entry e;
    if (!Plaus(rel) || ReadEntry(rel, b, &e) != 1) return "n/a";
    return S(e.relation) + "/" + S(e.trust) + "/" + S(e.trustNeg) + ((e.flags & 2u) ? " war" : "");
}
std::string FactionWords(::Faction* x) { return "'" + x->getName() + "' (" + SidOf(x) + ")"; }
}
std::string RelationChangeLever(const std::string& kind, const std::string& faction)
{
    const int k = kind == "crime" ? 0 : kind == "war" ? 1 : kind == "peace" ? 2 : -1;
    if (k < 0) return "error relation change usage: relation change crime|war|peace <faction stringID or name>";
    if (!GameplayRunning() || EngineWritesBlocked()) return "error relation change: no running world";
    ::Faction* mine = coop::LocalPlayerFaction();
    ::Faction* x = NpcFactionNamed(faction);
    if (!Plaus(mine) || !Plaus(x)) { DebugLog("[REL] change " + kind + ": no NPC faction '" + faction + "' here (or no player faction)"); return "error relation change not-found"; }
    if (kGetDataRva == 0 || (k == 0 ? kAffectRva : k == 1 ? kDeclareWarRva : kNoLongerRva) == 0) return "error relation change: no address";
    void* rel = RelationsOfPod(k == 0 ? x : mine);
    if (!Plaus(rel)) return "error relation change: the relations could not be read";
    const std::string itMine = EntryText(x, mine), mineIt = EntryText(mine, x);
    const long long other0 = (long long)g_wroteOtherSide;
    const WireMark m = MarkWire();
    const int r = CallChangerPod(k, rel, k == 0 ? mine : x);
    ++g_changeLevers;
    const std::string what = k == 0 ? "affectRelations(-20) on " + FactionWords(x) + "'s relations towards this player's faction"
                                    : std::string(k == 1 ? "declareWar" : "setNoLongerEnemies") + " on this player's faction's relations towards " + FactionWords(x);
    DebugLog("[REL] change " + kind + " (TEST): the engine's " + what + (r == 1 ? std::string() : std::string(" RAISED"))
             + " - it->mine " + itMine + " -> " + EntryText(x, mine) + ", mine->it " + mineIt + " -> " + EntryText(mine, x)
             + " (relation/trust/trustNeg) wire=" + WireSince(m) + " wroteOtherSide+" + S((long long)g_wroteOtherSide - other0) + " levers=" + S(g_changeLevers));
    return r == 1 ? "ok relation change " + kind : "error relation change: the engine's changer raised";
}
std::string RelationOfProbe(const std::string& faction)
{
    ::Faction* mine = coop::LocalPlayerFaction();
    ::Faction* x = NpcFactionNamed(faction);
    if (!Plaus(mine) || !Plaus(x)) { DebugLog("[REL] of '" + faction + "': no NPC faction by that name here (or no player faction)"); return "error relation of not-found"; }
    std::string line = "[REL] of " + FactionWords(x) + ": s" + S((long long)coop::MySlotForWire()) + "(me) it->mine " + EntryText(x, mine) + " mine->it " + EntryText(mine, x);
    int slots[coop::kStandInTableCap]; ::Faction* sf[coop::kStandInTableCap];
    const int ns = coop::StandInList(slots, sf, coop::kStandInTableCap);   /* every other player's faction met here */
    for (int i = 0; i < ns; ++i)
        if (Plaus(sf[i])) line += "; s" + S((long long)slots[i]) + " it->its " + EntryText(x, sf[i]) + " its->it " + EntryText(sf[i], x);
    DebugLog(line + " (relation/trust/trustNeg as this game holds them; read-only)");
    return "ok relation of";
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
             + " teamEventSkipped=" + S((long long)g_teamEvSkipped)
             + " echoSuppressed=" + S(g_echoSuppressed) + " wroteOtherSide=" + S((long long)g_wroteOtherSide) + " offThread=" + S((long long)g_offThread) + " queued=" + S((long long)g_queued) + " drained=" + S(g_drained) + " drainUnresolved=" + S(g_drainUnresolved) + " reverted=" + S(g_reverted) + " revertQueued=" + S(g_revertQueued) + " revertNoValue=" + S(g_revertNoValue) + " queueDropped=" + S((long long)g_queueDropped) + " offNoLink=" + S(g_offNoLink) + " sendFailed=" + S(g_sendFailed)
             + " | received=" + S(g_received) + " applied=" + S(g_applied) + " unresolved=" + S(g_unresolved) + " faults=" + S((long long)g_faults) + " refusedOwnedHere=" + S(g_refusedOwnedHere) + " refusedNotSender=" + S(g_refusedNotSender) + " legacyPeerSkipped=" + S(g_legacySkipped)
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
             + " refusedTarget=" + S(g_relRefusedTarget) + " refusedTeammate=" + S(g_relRefusedTeammate) + " refusedFounderSets=" + S(g_relRefusedFounders) + " fanOut=" + S(g_relFanOut) + " refused=" + S(g_relRefused) + " faults=" + S(g_relFaults) + " theirChangesShown=" + S(g_relNotices) + " theirChangesByEngine=" + S(g_relNoticesEngine) + " teamWritesQuiet=" + S(g_relTeamQuiet) + " last='" + g_relLast + "'");
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
    /* T-546 step 5: a load's own-record restore is never this game's own change (the team's record is written over it after) */
    struct NoMark { bool was; NoMark() : was(g_noTeamMark) { if (!g_teamWrite) g_noTeamMark = true; } ~NoMark() { g_noTeamMark = was; } } noMark;
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
        /* X -> mine (the entry this game's engine reads) and mine -> X (the other games keep it as this player's stand-in's table) */
        OwnRestorePair(rf, mine, b, r.relBack, r.trustBack, r.trustNegBack, r.flagsBack, &relSet, &trustSet, &faults);
        OwnRestorePair(rm, f, a, r.rel, r.trust, r.trustNeg, r.flags, &relSet, &trustSet, &faults);   /* mmo3 fold 5/7 */
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

/* ---- T-546 step 5: SHARED STANDING (src/common/teamstanding.h; the record is teamwire.h TeamRec) ------------------------
   The record is written onto this game's faction through the engine's own setRelation (and the direct trust / flag stores the
   pp.faction restore uses) under g_teamWrite: Forward sends those rows with the team reason, and they are never marked as this
   game's own change. Every other write of a pair of this game's faction with an NPC faction or another player's faction - the
   engine's changers on any thread, a stance pressed - is marked (TeamMarkOwn), except the load's own-record restore and the
   restore of a departure (g_noTeamMark). The marked NPC pairs are reported as differences from the base (the value last written
   from the record or last reported). MAIN THREAD. */
namespace {
long long g_teamApplies = 0, g_teamApplyFaults = 0, g_teamDeltas = 0, g_teamUncollected = 0, g_teamSets = 0, g_teamSideWrites = 0, g_teamMarks = 0;
std::set<std::string> g_teamDirtyNpc;          /* "<sid>|<dir>" of this game's NPC pairs moved by its own engine since the last gather */
std::set<int> g_teamDirtySide;                 /* slots of the players this game's own side towards moved */
unsigned long long g_sideNo = 0;               /* this game's own side changes counted (RelationsSideNoNow) */
std::map<int, unsigned long long> g_sideOwnNo; /* slot -> the number of this game's latest own change of its side towards that player */
struct BaseVal { swteam::Standing v; unsigned flags; };
std::map<std::string, BaseVal> g_teamBase;     /* "<sid>|<dir>" -> the value last written from the record or last reported */
bool g_teamBaseValid = false;                  /* the record has been written onto this world's faction (or the founder's SEED is the base) */
struct Prior { bool had; BaseVal v; };
std::map<std::string, Prior> g_teamLastPrior;  /* the last gather's bases before it moved them: a gather not sent is put back exactly */
/* an NPC faction: not this game's player faction, not another player's, not a carried player record */
bool NpcFaction(::Faction* f)
{
    return Plaus(f) && !coop::IsPlayerFaction(f) && !OtherPlayersFaction(f) && coop::StandInRecordSlot(f) == -1;
}
int SlotOfPlayerFaction(::Faction* f)
{
    int s = coop::StandInSlotOf(f);
    if (s < 0) s = coop::StandInRecordSlot(f);
    return s;
}
std::string DirKey(const std::string& sid, unsigned dir) { return sid + (dir ? "|1" : "|0"); }
/* `who`'s standing with every NPC faction, both directions (the pp.faction record's rows): X -> who (relBack) is the entry this
   game's engine reads for a player faction, who -> X (rel) the one the other games keep in its stand-in's table
   (swteam::NpcEntriesOf); platoonIDs read only for this game's own faction */
bool RecordOfFaction(::Faction* who, bool own, coopown::FactionRec* out, std::string* why)
{
    if (!Plaus(who) || !Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) { *why = "no-faction"; return false; }
    if (kGetDataRva == 0) { *why = "no-address"; return false; }
    void* rw = RelationsOfPod(who);
    if (!Plaus(rw)) { *why = "no-relations"; return false; }
    int pid = 0;
    if (own && OwnPlatoonIdsPod(who, 0, &pid) != 1) { *why = "fault"; return false; }
    void** arr = 0; unsigned n = 0;
    if (!ReadFactionArrayPod(&arr, &n) || !Plaus(arr) || n > 4096) { *why = "no-faction-list"; return false; }
    out->name = who->getName(); out->platoonIds = pid; out->rows.clear();
    char buf[160];
    for (unsigned i = 0; i < n; ++i)
    {
        ::Faction* f = (::Faction*)arr[i];
        if (f == who || !NpcFaction(f)) continue;
        if (!ReadSidPod(f, buf, 160) || buf[0] == 0) continue;
        void* rf = RelationsOfPod(f);
        if (!Plaus(rf)) continue;
        Entry a, b;
        if (ReadEntry(rw, f, &a) != 1 || ReadEntry(rf, who, &b) != 1) continue;
        coopown::FacRow r;
        r.sid = buf; r.rel = a.relation; r.trust = a.trust; r.trustNeg = a.trustNeg; r.relBack = b.relation; r.trustBack = b.trust; r.trustNegBack = b.trustNeg;
        r.flags = (int)(a.flags & 3u); r.flagsBack = (int)(b.flags & 3u);
        out->rows.push_back(r);
    }
    std::sort(out->rows.begin(), out->rows.end(), coopown::FacRowLess);
    return true;
}
/* one side of this game's faction set to a value and flags: the flags (and trust, kept) stored first, then setRelation, so the
   row Forward sends carries them. flags < 0: only the ally flag cleared when the value is below the ally line. */
int WriteSide(void* rel, ::Faction* t, const Entry& cur, float v, int flags)
{
    if (flags >= 0)
    {
        if ((unsigned)flags != (cur.flags & 3u) && OwnTrustWritePod(rel, t, cur.trust, cur.trustNeg, flags) != 1) return -2;
    }
    else if (swteam::FlagClearedFor(v, (cur.flags & 1u) != 0)) SetAllyFlagPod(rel, t, false);
    if (cur.relation != v && CallSetRelationPod(rel, t, v) != 1) return -2;
    if (cur.relation == v && flags >= 0 && (unsigned)flags != (cur.flags & 3u)) Forward(rel, t, 0);
    return 1;
}
}   // namespace

/* MAIN THREAD (Forward on the main thread; DrainQueue for the worker threads' changes): a pair of this game's player faction
   moved by something other than the team's own writes. (owner, other) is the WRITTEN pair - Forward resolved it through
   relside::WrittenPair before marking or queueing - so the direction marked is the entry that moved (swteam::NpcDirOfWritten). */
void TeamMarkOwn(::Faction* owner, ::Faction* other)
{
    ::Faction* mine = coop::LocalPlayerFaction();
    if (!Plaus(mine) || !Plaus(owner) || !Plaus(other)) return;
    ::Faction* x = owner == mine ? other : other == mine ? owner : 0;
    if (!Plaus(x)) return;
    const unsigned dir = owner == mine ? 0u : 1u;
    const int npcDir = swteam::NpcDirOfWritten(owner == mine, other == mine, NpcFaction(x));
    if (npcDir != swteam::kDirNone)
    {
        char buf[160];
        if (ReadSidPod(x, buf, 160) && buf[0] != 0 && g_teamDirtyNpc.size() < 8192) { g_teamDirtyNpc.insert(DirKey(buf, (unsigned)npcDir)); ++g_teamMarks; }
    }
    else if (dir == 0 && OtherPlayersFaction(x))
    {
        const int s = SlotOfPlayerFaction(x);
        if (s >= 0) { g_teamDirtySide.insert(s); ++g_teamMarks; g_sideOwnNo[s] = ++g_sideNo; }
    }
}
/* 476: another player's side towards THIS game's team - its side towards this game's player (`level`) and towards every
   teammate's faction here, the worst of them. A teammate's own faction is left as it is (the pin's). */
int TeamFoldTheirs(::Faction* theirs, int level)
{
    const int me = coop::MySlotForWire();
    if (me < 0 || !OtherPlayersFaction(theirs)) return level;
    const int ts = SlotOfPlayerFaction(theirs);
    if (ts < 0 || coop::TeamSameAnyThread(me, ts)) return level;
    const std::vector<int> mates = coop::TeamMatesOfMine();
    if (mates.empty()) return level;
    void* rt = RelationsOfPod(theirs);
    if (!Plaus(rt)) return level;
    std::vector<int> lv(1, level);
    for (size_t i = 0; i < mates.size(); ++i)
    {
        ::Faction* m = StandInOrCarried(mates[i]);
        Entry e;
        if (Plaus(m) && m != theirs && ReadEntry(rt, m, &e) == 1) lv.push_back(nametag::LevelOf(e.relation, (e.flags & 1u) != 0));
    }
    return swteam::TheirSideTowardsTeam(lv);
}
bool RelationsNpcRecordOf(int slot, coopown::FactionRec* out, std::string* why)
{
    const int me = coop::MySlotForWire();
    const bool own = slot < 0 || slot == me;
    ::Faction* who = own ? coop::LocalPlayerFaction() : StandInOrCarried(slot);
    if (!Plaus(who)) { *why = own ? "no-player-faction" : "no-faction-of-that-player-here"; return false; }
    return RecordOfFaction(who, own, out, why);
}
bool RelationsTeamSet(const coopown::FactionRec& rec, std::string* why, std::string* detail)
{
    g_teamWrite = true;
    const bool ok = RelationsOwnRestore(rec, why, detail);
    g_teamWrite = false;
    if (ok) ++g_teamSets;
    return ok;
}
void RelationsPlayerSides(std::vector<swteam::PlayerSide>* out)
{
    out->clear();
    ::Faction* mine = coop::LocalPlayerFaction();
    if (!Plaus(mine) || kGetDataRva == 0 || !Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) return;
    void* rm = RelationsOfPod(mine);
    void** arr = 0; unsigned n = 0;
    if (!Plaus(rm) || !ReadFactionArrayPod(&arr, &n) || !Plaus(arr) || n > 4096) return;
    const int me = coop::MySlotForWire();
    std::set<int> seen;
    for (unsigned i = 0; i < n; ++i)
    {
        ::Faction* f = (::Faction*)arr[i];
        if (!Plaus(f) || f == mine || !OtherPlayersFaction(f)) continue;
        const int s = SlotOfPlayerFaction(f);
        if (s < 0 || s == me || seen.count(s) != 0) continue;
        Entry e;
        if (ReadEntry(rm, f, &e) != 1) continue;
        seen.insert(s);
        swteam::PlayerSide p; p.slot = (unsigned)s; p.rel = e.relation; p.flags = (int)(e.flags & 3u);
        out->push_back(p);
    }
}
int RelationsMineTowardsFull(int slot, float* v, unsigned* flags)
{
    if (slot < 0 || slot == coop::MySlotForWire() || !GameplayRunning() || EngineWritesBlocked() || kGetDataRva == 0) return 0;
    ::Faction* me = coop::LocalPlayerFaction();
    ::Faction* t = StandInOrCarried(slot);
    if (!Plaus(me) || !Plaus(t)) return 0;
    void* rel = RelationsOfPod(me);
    Entry e;
    if (!Plaus(rel) || ReadEntry(rel, t, &e) != 1) return 0;
    *v = e.relation; *flags = e.flags & 3u;
    return 1;
}
int RelationsSetMineTowards(int slot, float v, int flags, bool asTeam, float* before, std::string* why)
{
    ::Faction* me = coop::LocalPlayerFaction();
    if (!GameplayRunning() || EngineWritesBlocked() || !Plaus(me) || kSetRelationRva == 0 || kGetDataRva == 0
        || !Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory))
    { *why = "this game's side towards that player cannot be written now - no world, or an engine address missing"; return -3; }
    ::Faction* t = (slot >= 0 && slot != coop::MySlotForWire()) ? StandInOrCarried(slot) : 0;
    if (!Plaus(t)) { *why = "no faction of that player here"; return -1; }   /* the world and its faction list are read: absent */
    void* rel = RelationsOfPod(me);
    Entry b;
    if (!Plaus(rel) || ReadEntry(rel, t, &b) != 1) { *why = "this game's entry towards that player would not read"; return -3; }
    *before = b.relation;
    if (asTeam) g_teamWrite = true; else g_noTeamMark = true;
    const int w = WriteSide(rel, t, b, v, flags);
    g_teamWrite = false; g_noTeamMark = false;
    if (w != 1) { *why = "the engine's setter raised"; return -2; }
    ++g_teamSideWrites;
    return 1;
}
bool RelationsTeamApplyRecord(const swteam::TeamRec& rec, const std::vector<int>& teammates, std::string* detail)
{
    ::Faction* mine = coop::LocalPlayerFaction();
    if (!Plaus(mine) || !Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory) || kGetDataRva == 0 || kSetRelationRva == 0)
    { *detail = "no player faction or no address"; ++g_teamApplyFaults; return false; }
    void* rm = RelationsOfPod(mine);
    if (!Plaus(rm)) { *detail = "no relations"; ++g_teamApplyFaults; return false; }
    long long rows = 0, missing = 0, relSet = 0, trustSet = 0, faults = 0, stances = 0, stancesSet = 0, stancesAbsent = 0;
    g_teamWrite = true;
    for (size_t i = 0; i < rec.rows.size(); ++i)
    {
        const swteam::RecRow& r = rec.rows[i];
        ::Faction* x = coop::GameWorldPtr()->factionDirectory->findFactionById(r.sid);
        void* rx = NpcFaction(x) ? RelationsOfPod(x) : 0;
        if (!Plaus(rx)) { ++missing; continue; }
        ++rows;
        Entry a, b;
        /* X -> mine (the entry this game's engine reads) first, then mine -> X (the other games' stand-in table) - each when the
           record holds it (swteam::NpcEntriesOf). The base follows a pair only when its write went through; a pair that faulted
           keeps its old base and the record stays owed (the caller tries again) */
        const unsigned w = swteam::NpcEntriesOf(r.have);
        if (w & swteam::kWriteItMine)
        {
            const long long f0 = faults;
            if (ReadEntry(rx, mine, &b) == 1) OwnRestorePair(rx, mine, b, r.relBack, r.trustBack, r.trustNegBack, (int)(r.flagsBack & 3u), &relSet, &trustSet, &faults); else ++faults;
            if (faults == f0) { BaseVal bv; bv.v = swteam::Standing(r.relBack, r.trustBack, r.trustNegBack); bv.flags = r.flagsBack & 3u; g_teamBase[DirKey(r.sid, 1)] = bv; }
        }
        if (w & swteam::kWriteMineIt)
        {
            const long long f0 = faults;
            if (ReadEntry(rm, x, &a) == 1) OwnRestorePair(rm, x, a, r.rel, r.trust, r.trustNeg, (int)(r.flags & 3u), &relSet, &trustSet, &faults); else ++faults;
            if (faults == f0) { BaseVal bv; bv.v = swteam::Standing(r.rel, r.trust, r.trustNeg); bv.flags = r.flags & 3u; g_teamBase[DirKey(r.sid, 0)] = bv; }
        }
    }
    const int me = coop::MySlotForWire();
    for (size_t i = 0; i < rec.stances.size(); ++i)
    {
        const int s = (int)rec.stances[i].slot;
        if (s == me || std::find(teammates.begin(), teammates.end(), s) != teammates.end()) continue;
        ++stances;
        ::Faction* t = StandInOrCarried(s);
        Entry e;
        if (!Plaus(t) || ReadEntry(rm, t, &e) != 1) { ++stancesAbsent; continue; }
        if (e.relation == rec.stances[i].rel && (e.flags & 3u) == (rec.stances[i].flags & 3u)) continue;
        if (WriteSide(rm, t, e, rec.stances[i].rel, (int)(rec.stances[i].flags & 3u)) == 1) ++stancesSet; else ++faults;
    }
    g_teamWrite = false;
    g_teamBaseValid = true;   /* marks made since the gather the caller ran just before stay: the next gather measures them from this base */
    if (faults == 0) ++g_teamApplies; else ++g_teamApplyFaults;
    *detail = "gen " + S((long long)rec.gen) + ": NPC factions " + S(rows) + " (missing here " + S(missing) + "), relationSet " + S(relSet) + ", trustSet " + S(trustSet)
            + "; stances " + S(stances) + " (set " + S(stancesSet) + ", no faction here " + S(stancesAbsent) + "); faults " + S(faults);
    return faults == 0;
}
void RelationsTeamBaseFromRec(const swteam::TeamRec& rec)
{
    g_teamBase.clear();
    for (size_t i = 0; i < rec.rows.size(); ++i)
    {
        const swteam::RecRow& r = rec.rows[i];
        BaseVal f; f.v = swteam::Standing(r.rel, r.trust, r.trustNeg); f.flags = r.flags & 3u;
        BaseVal k; k.v = swteam::Standing(r.relBack, r.trustBack, r.trustNegBack); k.flags = r.flagsBack & 3u;
        if (r.have & swteam::kHaveFwd) g_teamBase[DirKey(r.sid, 0)] = f;
        if (r.have & swteam::kHaveBack) g_teamBase[DirKey(r.sid, 1)] = k;
    }
    g_teamBaseValid = true;
}
void RelationsTeamCollect(std::vector<swteam::Delta>* npc, std::vector<int>* sideSlots)
{
    npc->clear(); sideSlots->clear(); g_teamLastPrior.clear();
    DrainQueue();   /* the worker threads' changes are marked first */
    if (!g_teamBaseValid || !GameplayRunning() || EngineWritesBlocked()) { g_teamDirtyNpc.clear(); g_teamDirtySide.clear(); return; }
    ::Faction* mine = coop::LocalPlayerFaction();
    void* rm = Plaus(mine) ? RelationsOfPod(mine) : 0;
    if (!Plaus(rm) || !Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) return;
    for (std::set<std::string>::const_iterator it = g_teamDirtyNpc.begin(); it != g_teamDirtyNpc.end() && npc->size() < swteam::kMaxDeltas; ++it)
    {
        const std::string& key = *it;
        const unsigned dir = key[key.size() - 1] == '1' ? 1u : 0u;
        const std::string sid = key.substr(0, key.size() - 2);
        ::Faction* x = coop::GameWorldPtr()->factionDirectory->findFactionById(sid);
        void* rx = NpcFaction(x) ? RelationsOfPod(x) : 0;
        if (!Plaus(rx)) continue;
        Entry e;
        if ((dir == 0 ? ReadEntry(rm, x, &e) : ReadEntry(rx, mine, &e)) != 1) continue;
        const swteam::Standing cur(e.relation, e.trust, e.trustNeg);
        std::map<std::string, BaseVal>::iterator b = g_teamBase.find(key);
        BaseVal base; base.v = cur; base.flags = e.flags & 3u;
        if (b != g_teamBase.end()) base = b->second;
        swteam::Delta d;
        /* no base: the record held no value of this pair when it was written here - this game's value is sent to be taken as it is */
        if (b == g_teamBase.end()) { d.sid = sid; d.dir = dir | swteam::kDirAbsolute; d.rel = cur.rel; d.trust = cur.trust; d.trustNeg = cur.trustNeg; d.flags = e.flags & 3u; npc->push_back(d); }
        else if (swteam::DeltaOf(sid, dir, cur, e.flags & 3u, base.v, base.flags, &d)) npc->push_back(d);
        else continue;
        Prior pr; pr.had = b != g_teamBase.end(); pr.v = base; g_teamLastPrior[key] = pr;
        BaseVal nb; nb.v = cur; nb.flags = e.flags & 3u; g_teamBase[key] = nb;
    }
    g_teamDirtyNpc.clear();
    for (std::set<int>::const_iterator it = g_teamDirtySide.begin(); it != g_teamDirtySide.end(); ++it) sideSlots->push_back(*it);
    g_teamDirtySide.clear();
    g_teamDeltas += (long long)npc->size();
}
void RelationsTeamUncollect(const std::vector<swteam::Delta>& npc)
{
    for (size_t i = 0; i < npc.size(); ++i)
    {
        const std::string key = DirKey(npc[i].sid, npc[i].dir & 1u);
        std::map<std::string, Prior>::const_iterator pr = g_teamLastPrior.find(key);
        if (pr != g_teamLastPrior.end()) { if (pr->second.had) g_teamBase[key] = pr->second.v; else g_teamBase.erase(key); }
        g_teamDirtyNpc.insert(key);
    }
    g_teamLastPrior.clear();
    g_teamUncollected += (long long)npc.size();
    g_teamDeltas -= (long long)npc.size();
}
void RelationsTeamForgetBase() { g_teamBaseValid = false; g_teamBase.clear(); g_teamDirtyNpc.clear(); g_teamDirtySide.clear(); }
int RelationsTeamDropSideMarks(const std::vector<unsigned>& slots)
{
    int n = 0;
    for (size_t i = 0; i < slots.size(); ++i) n += (int)g_teamDirtySide.erase((int)slots[i]);
    return n;
}
unsigned long long RelationsSideOwnNo(int slot)
{
    const std::map<int, unsigned long long>::const_iterator it = g_sideOwnNo.find(slot);
    return it == g_sideOwnNo.end() ? (unsigned long long)0 : it->second;
}
unsigned long long RelationsSideNoNow() { return g_sideNo; }
bool RelationsTeamBaseValid() { return g_teamBaseValid; }
long long RelationsTeamEventSkipped() { return (long long)g_teamEvSkipped; }
std::string RelationsTeamText()
{
    return "applies=" + S(g_teamApplies) + " applyFaults=" + S(g_teamApplyFaults) + " baseValid=" + S(g_teamBaseValid ? 1 : 0) + " deltasSent=" + S(g_teamDeltas)
         + " uncollected=" + S(g_teamUncollected) + " marks=" + S(g_teamMarks) + " teamSets=" + S(g_teamSets) + " sideWrites=" + S(g_teamSideWrites);
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
