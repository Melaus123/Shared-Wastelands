// townver.cpp - ONE TOWN VERSION PER PLACED TOWN, THE SAME ON EVERY GAME (src/common/townverwire.h, TOWN_VER 87). A town's version
// (its normal record, destroyed, taken over) is decided by the game RUNNING the town's area (its holder) with the engine's own
// chooser, kept on the world server and put on every game's town each time its area is switched on, before its people are made.
//
// THE ENGINE (build/decomp_9fe6d0.txt, decomp_9fd8d0.txt, decomp_9fec00.txt). The applied version is one pointer, Town+0x338 (0 =
// the town's own record; the virtual record read 0x9510A0 answers it, else +0x40). ZoneMapContent::_activate 0x9FEC00 asks the
// chooser ZoneManager::checkForRepopulateTown 0x9FE6D0 for the town of each zone it switches on; a true answer rebuilds the zone's
// buildings from the version's list and clears its loose items. The chooser does nothing while a save loads (ZoneManager+8) or for
// a player-owned town (owner Faction+0x250); it picks the heaviest version of the town's CURRENT record's "override town" list
// whose world states all hold, sets it through Town::notifyRepopulation 0x9FD8D0 (pointer, foliage range, type +0xD8, public
// +0x380, owner through the town's setter vt+0xA0 when the version names another faction, +0x110 cleared so residents are re-read),
// and lists the town in ZoneManager+0x168148 - a list cleared only at shutdown; a listed town answers true at every later
// activation without choosing again.
//
// THAT ONCE-PER-RUN LIST. It is the engine's own done mark and this file never writes it. A game that does not run the town's area
// lets no version qualify (the world-state list fill answers "not met", decomp_9fe6d0: a 0 skips the candidate), so the engine
// neither switches nor lists the town there, and the game that later runs it still decides. The stored record is put on at EVERY
// activation (not once): a listed town is not chosen again, but the record still reaches it before the engine is asked.
//
// THREAD. Everything here runs on the MAIN THREAD or not at all; each entry checks the store's main thread id and, off it, runs the
// engine's function untouched (counted townOv offThread).
//
// THE CHOOSER DETOUR (TownVerChoose), cooptv::TvChooseMode decides what it does:
//   1. the stored record for this placed town differs from the town -> Town::notifyRepopulation with the stored version (the town's
//      own record when the stored version is "", then Town+0x338 = 0, which is exactly "no version"), then the stored owner through
//      the town's own setter when it still differs.
//   2. the engine's chooser runs with OWNER DECISION 260 in force (the isTrue / list-fill detours below); on a game that does not run
//      the area no version may qualify.
//   3. HOLDER, not while a save loads: a version or owner the chooser changed is published at once; any other difference from the
//      record (a save made before the record existed) waits until the world server's whole list is here (the seed, TownVerTick).
//      Any game on this path queues its save's version for the seed (cooptv::TvSeedQueue); only the game running the area when the
//      seed is due publishes it (cooptv::TvSeedDue). A game that never runs the area holds the town as its save has it until the
//      runner publishes, and then puts that on.
//   4. return: while a save loads, the engine's answer (nothing is rebuilt then; the town's "as built" version stays the one in the
//      area file's note, so the next visit rebuilds); otherwise the engine's answer OR cooptv::TvNoteRebuild - the area file this
//      load used carries a "built with" note naming another version for the town than it has now - so that file's buildings and
//      loose items are rebuilt from the applied version exactly as the engine's own switch does. No note for the town (an old file,
//      an area write off the main thread, the engine's base files) keeps the file's buildings, and an owner-only difference never
//      rebuilds: the owner goes on through the town's own setter. The rebuild's saved-building drop (0x9FD470) is hooked in
//      build.cpp: a saved house another player owns is held, and its owner and state are restored after the area's walk.
// THE "BUILT WITH" NOTE (store.cpp TownNote*, townverwire.h kTvNoteId, cooptv::TvNoteBook). Notes are kept per (area loaded, town):
// the engine asks the chooser from an area for towns placed there and for towns found from it in neighbouring areas, and a rebuild
// drops that town's buildings from the loaded area's file. Each main-thread read of an area file replaces that area's notes; the
// engine reads its base files first and this game's own file or the world server's copy last, and reads its own area's file just
// before asking the chooser, so the area read last is the one being switched on. When its last read is a base file, every town
// checked from it gets a note of "" (its base version), so a town whose version differs is rebuilt once. The chooser takes a
// (area, town) note once per read. Every area write on the main thread carries, for each town the chooser ran for from that area
// and each town its file named, the version its buildings there are built with (the version now when rebuilt or when there was no
// note, else the note's version) - no live town list is needed. An area read off the main thread (its chooser then runs as the
// engine alone) is forgotten on the main thread before the next note is taken or written.
// THE SWEEP (TownVerTick): a record that arrives for a town whose area is NOT loaded here is put on at once (pointer + owner); the
// area file keeps its note, so the next activation rebuilds when the version differs. A town loaded here waits for its next
// activation (no switch under a standing player).
//
// WORLDS AND LINKS. A WELCOME from another world drops every record, pending write and stop of the last world (TownVerWelcome):
// placed-town keys are the same in every world. This game's own write stays pending until the world server is seen to hold it (it
// sends nothing back for an accepted write); one still pending when a link's whole list is here was lost and is sent again.
//
// OWNER DECISION 260 - "ignore the player ally / enemy conditions": see townverwire.h TvIgnoreAction. Inside the chooser only,
// WorldEventStateQuery::isTrue (0x9A6F30) answers, per world state: no standing term -> the engine; standing terms only -> the
// expected bool of the reference that asked (read from the chooser's own scratch list, captured in the list-fill detour), so the
// reference is met; NPC + standing terms -> the engine with the two standing term sets emptied for that one call (their element
// counts +0xE0 / +0x120 set to 0 and restored in a __finally). The chooser's weight (number of references) is unchanged.
#include "townver.h"
#include "../common/townverwire.h"
#include "../common/townrefill.h"   /* SameWorld: the world the records belong to */
#include "zones.h"
#include "doors.h"   /* DoorsRecAreaRebuilt - a rebuilt town's stale door records */
#include "store.h"
#include "addresses.h"
#include "hooks.h"
#include "playerfaction.h"
#include "coop_log.h"
#include "game/GameWorld.h"
#include "game/Faction.h"

#include <Windows.h>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

unsigned long long kTvSetVersionRva = 0; coop::AddrReg kTvSetVersionRva_reg("Town_setVersion", &kTvSetVersionRva);                 /* Steam_1.0.65 0x9FD8D0 */
unsigned long long kTvHoldsRva = 0;      coop::AddrReg kTvHoldsRva_reg("WorldStateQuery_holds", &kTvHoldsRva);                       /* Steam_1.0.65 0x9A6F30 */
unsigned long long kTvFillRva = 0;       coop::AddrReg kTvFillRva_reg("WorldStateQueryList_fill", &kTvFillRva);                     /* Steam_1.0.65 0x9A9700 */

typedef void (*TvNotifyFn)(void* town, void* version);
typedef char (*TvIsTrueFn)(void* q, void* a2, void* a3, void* a4);
typedef char (*TvSetupFromFn)(void* list, void* gd, void* name, void* a4);
typedef void (*TvSetFactionFn)(void* town, void* faction, void* a3);
TvIsTrueFn orig_tvIsTrue = 0;
TvSetupFromFn orig_tvSetupFrom = 0;

std::map<std::string, cooptv::TownVer> g_tvCache;   /* the world server's records this game knows, by placed-town key */
cooptv::TvNoteBook g_tvBook;                        /* the "built with" notes per (area loaded, placed town) - townverwire.h */
volatile LONG g_tvAreaUnknown[4096];                /* ANY THREAD: areas whose file was read off the main thread (forgotten at the next drain) */
volatile LONG g_tvUnknownPending = 0;
std::set<std::string> g_tvPending;                  /* arrived records the sweep has not looked at yet */
std::set<std::string> g_tvSeed;                     /* towns whose save gave them a version the world server has no record of */
std::set<std::string> g_tvNoRepublish;              /* towns the world server refused at revision 0 - not written again by this game */
std::map<std::string, cooptv::TownVer> g_tvMine;    /* this game's own writes the world server has not been seen to hold yet (cooptv::TvMineKeep) */
std::string g_tvWorld;                              /* the world (StoreWorldStamp) the records, the pending writes and the stops belong to; "" none yet */
volatile LONG g_tvChooserDepth = 0;                 /* inside the engine's chooser, called from TownVerChoose (main thread) */
int g_tvRefuseDepth = 0;                            /* ... where no version may qualify */
void* g_tvScratch = 0;                              /* the chooser's world-state list (the list fill's first argument) */
int g_tvFeedBegun = 0, g_tvListComplete = 0;        /* this world load's feed BEGIN arrived / its first page ended (the whole list is here) */
DWORD g_tvSeedAt = 0;                               /* GetTickCount of the last seed pass (the unsigned difference is wrap-safe) */

long long g_tvOffThread = 0, g_tvNoHook = 0, g_tvFaults = 0, g_tvPlayerTown = 0, g_tvSolo = 0, g_tvStandInTown = 0;
long long g_tvApplied = 0, g_tvAppliedLoad = 0, g_tvAppliedActivate = 0, g_tvAppliedSweep = 0, g_tvFacApplied = 0, g_tvFacMissing = 0, g_tvNoData = 0;
long long g_tvDecided = 0, g_tvPublished = 0, g_tvPublishNoLink = 0, g_tvSendFailed = 0, g_tvEncodeFailed = 0;
long long g_tvStoredIn = 0, g_tvStale = 0, g_tvRefusedByServer = 0, g_tvMalformed = 0, g_tvFeedBegins = 0;
long long g_tvNonHolderSkipped = 0, g_tvRebuildByNote = 0, g_tvKeptNoNote = 0, g_tvKeptSame = 0, g_tvRebuildDeferredLoad = 0, g_tvSweepNoTown = 0, g_tvSweepDeferredLoaded = 0;
long long g_tvStandingOnly = 0, g_tvStandingStripped = 0, g_tvScratchMiss = 0, g_tvQueryFault = 0;
long long g_tvNoPublishApplyFailed = 0, g_tvRevZeroStopped = 0, g_tvLoadClears = 0, g_tvSeedQueued = 0, g_tvSeedPublished = 0, g_tvSeedDropped = 0;
long long g_tvNotesWritten = 0, g_tvNotesFailed = 0, g_tvNotesRead = 0, g_tvNotesMissing = 0, g_tvNotesBase = 0, g_tvMineSettled = 0, g_tvResent = 0, g_tvWorldClears = 0;
int g_tvLogBudget = 160;   /* [TOWNOV] lines: capped for the whole process; the counters keep counting */
volatile LONG64 g_tvNotesOffThread = 0;   /* area writes off the main thread: no note added (any thread) */
volatile LONG64 g_tvNotesReadOffThread = 0;   /* area reads off the main thread: the area's notes forgotten (any thread) */
int g_tvNoteLogBudget = 60;               /* [TOWNOV] note written / read lines */

std::string TvN(long long v) { std::ostringstream o; o << v; return o.str(); }
void TvLog(const std::string& s) { if (g_tvLogBudget <= 0) return; --g_tvLogBudget; DebugLog("[TOWNOV] " + s + (g_tvLogBudget == 0 ? " (log cap reached - counters only from here)" : "")); }
std::string TvQ(const std::string& s) { return s.empty() ? std::string("-") : ("'" + s + "'"); }

bool TvOnMain() { const unsigned long mt = coop::StoreMainThreadId(); return mt != 0 && ::GetCurrentThreadId() == mt; }
TvNotifyFn TvNotify() { return kTvSetVersionRva != 0 ? (TvNotifyFn)(uintptr_t)coop::AddrAbs(kTvSetVersionRva) : 0; }

/* ---- POD reads (SEH; no C++ objects in these functions) ---- */
struct TvTownPod { void* base; void* fac; int playerOwned; };
int TvTownPodRead(void* town, TvTownPod* o)
{
    __try
    {
        o->base = *(void**)((char*)town + 0x40);                      /* RootObjectBase::data - the placed town's own record */
        o->fac = *(void**)((char*)town + 0x10);                       /* RootObjectBase::owner */
        o->playerOwned = (o->fac != 0 && *(long long*)((char*)o->fac + 0x250) != 0) ? 1 : 0;   /* the chooser's own player gate (Faction+0x250) */
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int TvLoadPassPod(void* zm, int* out) { __try { *out = *(char*)((char*)zm + 8) != 0 ? 1 : 0; return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }   /* the chooser's load gate */
int TvClearReplPod(void* town) { __try { *(void**)((char*)town + 0x338) = 0; return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }   /* the applied-version pointer */
int TvQueryCountsPod(void* q, unsigned long long* npc, unsigned long long* standing)
{
    __try
    {   /* decomp_9a6f30: the four term sets' element counts - NPC is +0x20, NPC is NOT +0x60, player ally +0xE0, player enemy +0x120 */
        *npc = *(unsigned long long*)((char*)q + 0x20) + *(unsigned long long*)((char*)q + 0x60);
        *standing = *(unsigned long long*)((char*)q + 0xE0) + *(unsigned long long*)((char*)q + 0x120);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* decomp_9fe6d0: the chooser's list - bucket count +0x18, size +0x20, buckets +0x38, first node = buckets[bucket count];
   node [0] next, [2] the query, byte at [3] the reference's expected answer. */
int TvScratchExpectedPod(void* list, void* q, char* expected)
{
    __try
    {
        if (*(unsigned long long*)((char*)list + 0x20) == 0) return 0;
        const unsigned long long bc = *(unsigned long long*)((char*)list + 0x18);
        void** buckets = *(void***)((char*)list + 0x38);
        void** node = (void**)buckets[bc];
        for (int n = 0; node != 0 && n < 256; ++n)
        {
            if (node[2] == q) { *expected = *(char*)(node + 3); return 1; }
            node = (void**)node[0];
        }
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
char TvIsTrueStripped(void* q, void* a2, void* a3, void* a4, int* fault)
{
    unsigned long long ally = 0, enemy = 0;
    __try
    {
        ally = *(unsigned long long*)((char*)q + 0xE0); enemy = *(unsigned long long*)((char*)q + 0x120);
        *(unsigned long long*)((char*)q + 0xE0) = 0; *(unsigned long long*)((char*)q + 0x120) = 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *fault = 1; return orig_tvIsTrue(q, a2, a3, a4); }
    char r = 0;
    __try { r = orig_tvIsTrue(q, a2, a3, a4); }
    __finally { *(unsigned long long*)((char*)q + 0xE0) = ally; *(unsigned long long*)((char*)q + 0x120) = enemy; }
    return r;
}

/* ---- the placed town, as the readout sees it ---- */
struct TvPlaced { std::string key, repl, fac; float x, y, z; TvPlaced() : x(0), y(0), z(0) {} };
bool TvRead(void* town, TvPlaced* p) { return coop::TownPlacedRead(town, &p->key, &p->repl, &p->fac, &p->x, &p->y, &p->z) && !p->key.empty(); }

void* TvFindData(const std::string& sid)
{
    if (sid.empty() || coop::GameWorldPtr() == 0) return 0;
    return (void*)coop::GameWorldPtr()->gamedata.getData(sid);
}
void* TvFindFaction(const std::string& sid)
{
    if (sid.empty() || coop::GameWorldPtr() == 0 || coop::GameWorldPtr()->factionDirectory == 0) return 0;
    return (void*)coop::GameWorldPtr()->factionDirectory->findFactionById(sid);
}

struct TvDepth { volatile LONG* d; explicit TvDepth(volatile LONG* p) : d(p) { InterlockedIncrement(d); } ~TvDepth() { InterlockedDecrement(d); } };
struct TvIntDepth { int* d; explicit TvIntDepth(int* p) : d(p) { if (d) ++*d; } ~TvIntDepth() { if (d) --*d; } };

/* 1 = this game runs the area at (x, z) as its zone is being switched on here: the area map's holder rule with "loaded here" taken
   as true (the chooser runs exactly when this game switches the zone on). Held by another game answers 0, no fresh map not 1. */
int TvHolderAtActivation(float x, float z)
{
    int held = 0, mine = 0, oth = 0, here = 0, r1 = 0, pr1 = 0, plow = -2, pe = 0, pf = 1;
    coop::AreaViewTS(coop::SectorOf(x, z), &held, &mine, &oth, &here, &r1, &pr1, &plow, &pe, &pf);
    return coop::MayInventFromView(held, mine, 1, r1, pr1, coop::MySlotLower(plow), pe, pf) == 1 ? 1 : 0;
}

/* The stored record onto this town: version first, then the owner. 1 = something changed. */
int TvApply(void* town, void* base, const cooptv::TownVer& rec, const TvPlaced& pl, const char* at, int* failed)
{
    const TvNotifyFn notify = TvNotify();
    if (notify == 0) return 0;
    int did = 0;
    if (rec.ver != pl.repl)
    {
        void* v = rec.ver.empty() ? base : TvFindData(rec.ver);
        if (v == 0)
        {
            ++g_tvNoData;
            if (failed != 0) *failed = 1;
            TvLog("NOT applied town='" + pl.key + "' ver=" + TvQ(rec.ver) + " rev=" + TvN(rec.rev) + " - the version is not in this game's data (townOv noData)");
            return 0;
        }
        notify(town, v);
        if (rec.ver.empty()) TvClearReplPod(town);   /* back to the town's own record: exactly "no version" (the engine reads +0x40 then) */
        did = 1;
    }
    if (!rec.fac.empty())
    {
        TvPlaced now;
        if (TvRead(town, &now) && now.fac != rec.fac)
        {
            void* f = TvFindFaction(rec.fac);
            if (f != 0) { void** vt = *(void***)town; ((TvSetFactionFn)vt[0xA0 / 8])(town, f, 0); ++g_tvFacApplied; did = 1; }
            else { ++g_tvFacMissing; if (failed != 0) *failed = 1; }
        }
    }
    if (did)
    {
        ++g_tvApplied;
        if (at[0] == 'l') ++g_tvAppliedLoad; else if (at[0] == 's') ++g_tvAppliedSweep; else ++g_tvAppliedActivate;
        TvPlaced now; TvRead(town, &now);
        TvLog("applied town='" + pl.key + "' ver=" + TvQ(rec.ver) + " fac=" + TvQ(rec.fac) + " rev=" + TvN(rec.rev) + " slot=" + TvN(rec.slot)
              + " at=" + at + " was repl=" + TvQ(pl.repl) + " fac=" + TvQ(pl.fac) + " now repl=" + TvQ(now.repl) + " fac=" + TvQ(now.fac));
    }
    return did;
}

/* The town's version and owner as they are now, to the world server when they differ from the record. `why` is logged. */
void TvPublish(void* town, const std::string& key, const char* why)
{
    TvPlaced after;
    if (!TvRead(town, &after) || after.key != key) return;
    if (g_tvNoRepublish.count(key) != 0) return;   /* the world server cannot store this town: no rewrite on every visit */
    std::map<std::string, cooptv::TownVer>::const_iterator it = g_tvCache.find(key);
    const int have = it != g_tvCache.end() ? 1 : 0;
    const unsigned int rev = cooptv::TvPublishRev(have, have ? it->second : cooptv::TownVer(), after.repl, after.fac);
    if (rev == 0) return;
    if (!coop::StoreTownVerLinkUp()) { ++g_tvPublishNoLink; return; }
    cooptv::TownVer w; w.key = key; w.ver = after.repl; w.fac = after.fac; w.rev = rev; w.slot = coop::StoreMySlot();
    std::vector<char> b;
    if (!cooptv::TvEncode(&b, cooptv::kTvWrite, w)) { ++g_tvEncodeFailed; return; }
    if (!coop::StoreSendTownVer(b)) { ++g_tvSendFailed; return; }
    g_tvCache[key] = w; g_tvMine[key] = w; ++g_tvPublished;   /* pending until the world server is seen to hold it */
    TvLog("published town='" + key + "' ver=" + TvQ(w.ver) + " fac=" + TvQ(w.fac) + " rev=" + TvN(w.rev) + " slot=" + TvN(w.slot) + " by=holder (" + why + ")");
}

/* A link's whole list is here: this game's own writes it did not confirm were lost on the way (the link dropped first) and go again
   with their revision; the world server's revision rule decides each (a refusal brings the stored record). They stay pending. */
void TvResendMine()
{
    for (std::map<std::string, cooptv::TownVer>::const_iterator it = g_tvMine.begin(); it != g_tvMine.end(); ++it)
    {
        if (!coop::StoreTownVerLinkUp()) { ++g_tvPublishNoLink; return; }
        std::vector<char> b;
        if (!cooptv::TvEncode(&b, cooptv::kTvWrite, it->second)) { ++g_tvEncodeFailed; continue; }
        if (!coop::StoreSendTownVer(b)) { ++g_tvSendFailed; return; }
        ++g_tvResent;
        TvLog("sent again town='" + it->first + "' ver=" + TvQ(it->second.ver) + " fac=" + TvQ(it->second.fac) + " rev=" + TvN(it->second.rev)
              + " - the world server's list did not hold this game's write (townOv resent)");
    }
}

char detour_tvIsTrue(void* q, void* a2, void* a3, void* a4)
{
    if (g_tvChooserDepth <= 0 || q == 0 || !TvOnMain()) return orig_tvIsTrue(q, a2, a3, a4);
    unsigned long long npc = 0, standing = 0;
    if (!TvQueryCountsPod(q, &npc, &standing)) { ++g_tvQueryFault; return orig_tvIsTrue(q, a2, a3, a4); }
    const int act = cooptv::TvIgnoreAction(npc, standing);
    if (act == cooptv::kTvEngine) return orig_tvIsTrue(q, a2, a3, a4);
    if (act == cooptv::kTvExpected)
    {
        char e = 0;
        if (g_tvScratch != 0 && TvScratchExpectedPod(g_tvScratch, q, &e)) { ++g_tvStandingOnly; return e; }
        ++g_tvScratchMiss; return orig_tvIsTrue(q, a2, a3, a4);
    }
    ++g_tvStandingStripped;
    int fault = 0; const char r = TvIsTrueStripped(q, a2, a3, a4, &fault);
    if (fault) ++g_tvQueryFault;
    return r;
}
char detour_tvSetupFrom(void* list, void* gd, void* name, void* a4)
{
    if (g_tvChooserDepth > 0 && TvOnMain())
    {
        /* a game that does not run the area lets no candidate qualify: the engine neither switches nor lists the town as decided
           (decomp_9fe6d0: a 0 here skips the candidate; notifyRepopulation and the +0x168148 insert run only when one qualified) */
        if (g_tvRefuseDepth > 0) { ++g_tvNonHolderSkipped; return 0; }
        g_tvScratch = list;
    }
    return orig_tvSetupFrom(list, gd, name, a4);
}

void TvHook(unsigned long long rva, void* detour, void** orig, const char* what)
{
    const unsigned long long abs = rva != 0 ? coop::AddrAbs(rva) : 0;
    if (abs == 0) { ErrorLog(std::string("[TOWNOV] ") + what + " is not in the address table - NOT HOOKED (town versions run as the engine alone)"); return; }
    const coop::HookStatus s = coop::AddHook((void*)(uintptr_t)abs, detour, orig);
    DebugLog(std::string("[TOWNOV] ") + what + (s == coop::SUCCESS ? " AddHook SUCCESS" : " AddHook FAILED"));
    if (s != coop::SUCCESS) *orig = 0;
}

/* Areas whose file was read off the main thread: forgotten here, on the main thread, before any note is taken or written. */
void TvDrainUnknown()
{
    if (::InterlockedExchange(&g_tvUnknownPending, 0) == 0) return;
    for (int a = 0; a < 4096; ++a) if (::InterlockedExchange(&g_tvAreaUnknown[a], 0) != 0) g_tvBook.Forget(a);
}
/* THE "BUILT WITH" NOTE of the area read last, for this town: 1 = the chooser's first call for it since that read */
int TvNoteTake(const std::string& key, int* has, std::string* ver) { TvDrainUnknown(); return g_tvBook.Take(key, has, ver); }
/* A chooser call off the version path (the engine alone, another player's town): no rule, the note bookkeeping only. */
void TvNoteSettle(void* town, const TvPlaced& pl, int rebuilt)
{
    std::string noteVer; int hasNote = 0;
    if (!TvNoteTake(pl.key, &hasNote, &noteVer)) return;
    TvPlaced fin; if (!TvRead(town, &fin)) fin = pl;
    g_tvBook.Built(pl.key, rebuilt, hasNote, noteVer, fin.repl);
}

/* The engine's chooser with this game's refusal (or not) in force. */
bool TvRunChooser(void* zm, void* town, coop::TvChooserFn orig, int refuse)
{
    bool r = false;
    TvDepth d(&g_tvChooserDepth);
    TvIntDepth rf(refuse ? &g_tvRefuseDepth : 0);
    g_tvScratch = 0;
    r = orig(zm, town);
    g_tvScratch = 0;
    return r;
}

}   // namespace

namespace coop {

void TownVerInstall()
{
    if (TvNotify() == 0) ErrorLog("[TOWNOV] Town_setVersion is not in the address table - town versions run as the engine alone");
    TvHook(kTvHoldsRva, (void*)&detour_tvIsTrue, (void**)&orig_tvIsTrue, "WorldEventStateQuery::isTrue");
    TvHook(kTvFillRva, (void*)&detour_tvSetupFrom, (void**)&orig_tvSetupFrom, "WorldEventStateQueryList::setupFrom");
}

bool TownVerChoose(void* zm, void* town, TvChooserFn orig)
{
    if (orig == 0) return false;
    const int onMain = (zm != 0 && town != 0 && TvOnMain()) ? 1 : 0;
    const int hooked = (TvNotify() != 0 && orig_tvIsTrue != 0 && orig_tvSetupFrom != 0) ? 1 : 0;
    int loadPass = 0; TvTownPod tp; tp.base = 0; tp.fac = 0; tp.playerOwned = 0; TvPlaced pl;
    const int readOk = (onMain && hooked && TvLoadPassPod(zm, &loadPass) && TvTownPodRead(town, &tp) && TvRead(town, &pl)) ? 1 : 0;
    int standIn = 0;
    if (readOk && tp.fac != 0 && !tp.playerOwned) { const int s = StandInRecordSlot((::Faction*)tp.fac); standIn = (s >= 0 || s == -2 || s == -3) ? 1 : 0; }
    const int inWorld = StoreMySlot() >= 0 ? 1 : 0;
    const int holder = readOk ? TvHolderAtActivation(pl.x, pl.z) : 0;
    const int mode = cooptv::TvChooseMode(onMain, hooked, readOk, tp.playerOwned, standIn, inWorld, holder);
    if (mode == cooptv::kTvEngineAlone)
    {
        if (!onMain) ++g_tvOffThread; else if (!hooked) ++g_tvNoHook; else if (!readOk) ++g_tvFaults; else if (tp.playerOwned) ++g_tvPlayerTown; else ++g_tvSolo;
        const bool re = orig(zm, town);
        if (readOk) TvNoteSettle(town, pl, re ? 1 : 0);
        return re;
    }
    if (mode == cooptv::kTvStandIn) { ++g_tvStandInTown; const bool rs = TvRunChooser(zm, town, orig, 1); TvNoteSettle(town, pl, rs ? 1 : 0); return rs; }

    int applied = 0, applyFailed = 0;   /* applyFailed: this visit could not put the stored record on (noData / facMissing) */
    std::map<std::string, cooptv::TownVer>::const_iterator rec = g_tvCache.find(pl.key);
    const int haveRec = rec != g_tvCache.end() ? 1 : 0;
    if (haveRec) applied = TvApply(town, tp.base, rec->second, pl, loadPass ? "load" : "activate", &applyFailed);
    TvPlaced mid = pl; if (applied) TvRead(town, &mid);
    if (cooptv::TvSeedQueue(1, haveRec, pl.repl.empty() ? 0 : 1) && g_tvSeed.insert(pl.key).second) ++g_tvSeedQueued;   /* the save gave it a version the world server has no record of; only the area's runner publishes it (TownVerTick) */

    bool r = TvRunChooser(zm, town, orig, mode == cooptv::kTvRefuse ? 1 : 0);
    std::string noteVer; int hasNote = 0;
    const int noteFresh = TvNoteTake(pl.key, &hasNote, &noteVer);   /* the "built with" note of the area file this load used, for this town */
    if (loadPass)
    {   /* nothing is rebuilt while a save loads: the area stays built with its note's version, so the next visit rebuilds */
        TvPlaced lp; if (!TvRead(town, &lp)) lp = mid;
        if (!r && cooptv::TvNoteRebuild(hasNote, noteVer, lp.repl))
        {
            ++g_tvRebuildDeferredLoad;
            TvLog("town='" + pl.key + "' area file built with " + TvQ(noteVer) + ", the town is now " + TvQ(lp.repl) + " - not rebuilt while a save loads, the next visit rebuilds (townOv rebuildDeferredLoad)");
        }
        if (noteFresh) g_tvBook.Built(pl.key, r ? 1 : 0, hasNote, noteVer, lp.repl);
        return r;
    }
    if (mode == cooptv::kTvDecide)
    {
        TvPlaced after;
        const bool decidedNow = TvRead(town, &after) && (after.repl != mid.repl || after.fac != mid.fac);
        if (decidedNow)
        {
            ++g_tvDecided;
            TvLog("decided town='" + pl.key + "' by the engine's chooser on the holder: repl " + TvQ(mid.repl) + " -> " + TvQ(after.repl) + " fac " + TvQ(mid.fac) + " -> " + TvQ(after.fac));
        }
        if (applyFailed) ++g_tvNoPublishApplyFailed;   /* a game missing the record's data must not overwrite the world's record */
        else if (decidedNow) TvPublish(town, pl.key, "the engine switched it");
        else if (g_tvListComplete && haveRec) TvPublish(town, pl.key, "differs from the record");
        /* else: a save's own version with no record yet waits for the whole list (the seed, TownVerTick) */
    }
    /* THE REBUILD (cooptv::TvNoteRebuild): the area file this load used was built with another version than the town has now -> its
       buildings and loose items are rebuilt from the applied version; no note for the town -> kept. */
    TvPlaced fin; if (!TvRead(town, &fin)) fin = mid;
    if (!r)
    {
        if (cooptv::TvNoteRebuild(hasNote, noteVer, fin.repl))
        {
            ++g_tvRebuildByNote; r = true;
            TvLog("REBUILT town='" + pl.key + "': its area file was built with " + TvQ(noteVer) + ", the town is now " + TvQ(fin.repl) + " (townOv rebuildByNote)");
        }
        else if (!hasNote) ++g_tvKeptNoNote;
        else ++g_tvKeptSame;
    }
    if (noteFresh) g_tvBook.Built(pl.key, r ? 1 : 0, hasNote, noteVer, fin.repl);
    if (r)
    {   /* the town's saved buildings were replaced: the area's door records that name no door are removed by its holder (doors.cpp) */
        const Sector ts = SectorOf(pl.x, pl.z);
        DoorsRecAreaRebuilt(ts.x * 64 + ts.y);
    }
    return r;
}

void TownVerArrive(const std::vector<char>& payload)
{
    cooptv::TvMsg m;
    if (payload.empty() || !cooptv::TvDecode(&payload[0], payload.size(), &m) || m.kind == (unsigned int)cooptv::kTvWrite)
    {
        ++g_tvMalformed;
        if (g_tvMalformed <= 3) ErrorLog("[TOWNOV] a malformed (or up-only) TOWN_VER from the world server - ignored (townOv malformed)");
        return;
    }
    const std::string key = m.rec.key;
    std::map<std::string, cooptv::TownVer>::iterator it = g_tvCache.find(key);
    const int have = it != g_tvCache.end() ? 1 : 0;
    std::map<std::string, cooptv::TownVer>::iterator mine = g_tvMine.find(key);
    if (mine != g_tvMine.end() && !cooptv::TvMineKeep(m.kind, m.rec.rev, mine->second.rev)) { g_tvMine.erase(mine); ++g_tvMineSettled; }   /* the world server holds it or a later write, or refused it */
    if (!cooptv::TvCacheTake(have, have ? it->second.rev : 0u, m.kind, m.rec.rev)) { ++g_tvStale; return; }   /* a lower revision: a late duplicate, or a list this game's pending write did not reach */
    if (m.kind == (unsigned int)cooptv::kTvRefused)
    {
        ++g_tvRefusedByServer;
        if (m.rec.rev == 0) { g_tvCache.erase(key); if (g_tvNoRepublish.insert(key).second) ++g_tvRevZeroStopped; }   /* rev 0 = the world server stores nothing for it */
        else g_tvCache[key] = m.rec;
        TvLog("the world server REFUSED this game's write for town='" + key + "' - it holds rev=" + TvN(m.rec.rev) + " ver=" + TvQ(m.rec.ver) + " fac=" + TvQ(m.rec.fac)
              + " (taken as the truth; put on where the town is not loaded, else at its next activation)");
    }
    else
    {
        ++g_tvStoredIn; g_tvCache[key] = m.rec;
        g_tvNoRepublish.erase(key);
        TvLog("world server record town='" + key + "' ver=" + TvQ(m.rec.ver) + " fac=" + TvQ(m.rec.fac) + " rev=" + TvN(m.rec.rev) + " slot=" + TvN(m.rec.slot));
    }
    g_tvPending.insert(key);
}

/* The feed's first page carries every record right after its BEGIN. The cache is not emptied here (a record is never removed on the
   world server); a WELCOME from another world empties it (TownVerWelcome). Once the whole list is here, this game's own writes it
   did not confirm are sent again (TvResendMine). */
void TownVerFeedBegin() { if (!TvOnMain()) return; ++g_tvFeedBegins; g_tvFeedBegun = 1; g_tvListComplete = 0; }
void TownVerFeedPageEnd() { if (!TvOnMain()) return; if (g_tvFeedBegun && !g_tvListComplete) { g_tvListComplete = 1; TvResendMine(); } }
/* A world load / teardown begins: the arrived-but-unswept records and the seeds belong to the zones being destroyed (the next world's
   activations and feed bring them back); the note book is only marked ended (cooptv::TvNoteBook::WorldEnded). */
void TownVerWorldLoadBegan()
{
    if (!TvOnMain()) return;
    TvDrainUnknown();
    if (!g_tvPending.empty() || !g_tvSeed.empty()) ++g_tvLoadClears;
    g_tvPending.clear(); g_tvSeed.clear();
    g_tvBook.WorldEnded();   /* not emptied here: the engine's teardown runs after this and still writes area files, whose notes come from the book */
    g_tvFeedBegun = 0; g_tvListComplete = 0;
}
/* The engine's teardown / load call returned: the last world's area writes are done, so its notes go (cooptv::TvNoteBook::TeardownDone). */
void TownVerTeardownDone() { if (!TvOnMain()) return; g_tvBook.TeardownDone(); }

/* AT A WELCOME (store pump, after the WELCOME's world is decided): one from another world drops every record, revision, pending
   write and stop of the last world - placed-town keys are the same in every world, so they would land on this world's towns and
   drop its lower revisions as stale. A world not known yet (no stamp) drops nothing. */
void TownVerWelcome()
{
    if (!TvOnMain()) return;
    const std::string now = StoreWorldStamp();
    if (now.find((char)0x1F) == std::string::npos) return;
    if (!g_tvWorld.empty() && townrefill::SameWorld(g_tvWorld, now) == 0)
    {
        ++g_tvWorldClears;
        TvLog("a WELCOME from another world: " + TvN((long long)g_tvCache.size()) + " record(s), " + TvN((long long)g_tvMine.size()) + " pending write(s) and "
              + TvN((long long)g_tvNoRepublish.size()) + " stop(s) of the last world dropped (townOv worldClears)");
        g_tvCache.clear(); g_tvMine.clear(); g_tvNoRepublish.clear(); g_tvPending.clear(); g_tvSeed.clear();   /* the area notes stay: they belong to this world's areas (a world load clears them) */
    }
    g_tvWorld = now;
}

void TownVerTick()
{
    if (!TvOnMain() || EngineWritesBlocked()) return;
    if (TvNotify() == 0) { g_tvPending.clear(); g_tvSeed.clear(); return; }
    int budget = 8;
    for (std::set<std::string>::iterator it = g_tvPending.begin(); it != g_tvPending.end() && budget > 0; --budget)
    {
        const std::string key = *it;
        g_tvPending.erase(it++);
        std::map<std::string, cooptv::TownVer>::const_iterator rec = g_tvCache.find(key);
        if (rec == g_tvCache.end()) continue;
        void* town = TownFindByKey(key);
        if (town == 0) { ++g_tvSweepNoTown; continue; }
        TvPlaced pl; TvTownPod tp;
        if (!TvRead(town, &pl) || !TvTownPodRead(town, &tp) || tp.playerOwned) continue;
        if (pl.repl == rec->second.ver && (rec->second.fac.empty() || pl.fac == rec->second.fac)) continue;
        if (IsPositionLoadedHereTri(pl.x, pl.y, pl.z) != 0) { ++g_tvSweepDeferredLoaded; continue; }   /* loaded here (or could not ask): its next activation puts it on */
        TvApply(town, tp.base, rec->second, pl, "sweep", 0);   /* the area file here keeps its note: the next activation compares it */
    }
    /* THE SEED (a world saved before its towns had records), once a second: published only from the area's holder, with the whole
       list here and nothing stored for the town. A town another game holds waits; one with a record, refused or gone is dropped. */
    const DWORD now = ::GetTickCount();   /* VS2010-era headers have no GetTickCount64 */
    if (g_tvSeed.empty() || !g_tvListComplete || (DWORD)(now - g_tvSeedAt) < 1000u) return;
    g_tvSeedAt = now;
    for (std::set<std::string>::iterator it = g_tvSeed.begin(); it != g_tvSeed.end(); )
    {
        const std::string key = *it;
        void* town = TownFindByKey(key);
        TvPlaced pl;
        const int haveRec = g_tvCache.count(key) != 0 ? 1 : 0, stopped = g_tvNoRepublish.count(key) != 0 ? 1 : 0;
        if (town == 0 || !TvRead(town, &pl) || haveRec || stopped || pl.repl.empty()) { g_tvSeed.erase(it++); ++g_tvSeedDropped; continue; }
        const int holder = MayInventHereTS(SectorOf(pl.x, pl.z)) == 1 ? 1 : 0;
        if (!cooptv::TvSeedDue(g_tvListComplete, haveRec, stopped, 1, holder)) { ++it; continue; }
        const long long before = g_tvPublished;
        TvPublish(town, key, "this game's save, no record yet");
        if (g_tvPublished != before) ++g_tvSeedPublished;
        g_tvSeed.erase(it++);
    }
}

/* THE "BUILT WITH" NOTE (store.cpp TownNote*): what an area's next write notes (cooptv::TvNoteBook::For) - no live town list is needed */
int TownVerNoteTowns(int sx, int sy, std::vector<std::pair<std::string, std::string> >* out)
{
    out->clear();
    const int area = cooptv::TvAreaIndex(sx, sy);
    if (!TvOnMain() || area < 0) return -1;
    TvDrainUnknown();
    g_tvBook.For(area, out);
    return (int)out->size();
}
void TownVerNoteWritten(const std::string& zoneId, int ok, int reused, const std::vector<std::pair<std::string, std::string> >& towns)
{
    if (ok) ++g_tvNotesWritten; else ++g_tvNotesFailed;
    if (g_tvNoteLogBudget <= 0) return;
    --g_tvNoteLogBudget;
    std::string list;
    for (size_t i = 0; i < towns.size(); ++i) list += " " + towns[i].first + "=" + (towns[i].second.empty() ? std::string("-") : towns[i].second);
    DebugLog("[TOWNOV] note " + std::string(ok ? "written" : "NOT written (a field write failed - the file carries no note)") + " zone=" + zoneId
             + " towns=" + TvN((long long)towns.size()) + (reused ? " (record reused)" : "") + " [" + list + " ]");
}
void TownVerNoteFailed(const std::string& zoneId, const char* why)
{
    ++g_tvNotesFailed;
    if (g_tvNoteLogBudget <= 0) return;
    --g_tvNoteLogBudget;
    DebugLog("[TOWNOV] note NOT written zone=" + zoneId + ": " + why + " (townOv notesFailed)");
}
void TownVerNoteOffThread() { ::InterlockedIncrement64(&g_tvNotesOffThread); }
void TownVerNoteOffThreadRead(int sx, int sy)
{
    ::InterlockedIncrement64(&g_tvNotesReadOffThread);
    const int area = cooptv::TvAreaIndex(sx, sy);
    if (area < 0) return;
    ::InterlockedExchange(&g_tvAreaUnknown[area], 1);
    ::InterlockedExchange(&g_tvUnknownPending, 1);
}
void TownVerNoteRead(int sx, int sy, const std::string& zoneId, int loaded, int source, int unreadable, const std::vector<std::pair<std::string, std::string> >& notes, const std::string& path)
{
    if (!TvOnMain()) return;
    TvDrainUnknown();
    g_tvBook.Read(cooptv::TvAreaIndex(sx, sy), loaded, source, notes);
    if (!loaded) return;
    if (source == cooptv::kTvNoteBase) ++g_tvNotesBase; else if (source == cooptv::kTvNoteFile) ++g_tvNotesRead; else ++g_tvNotesMissing;
    if (source != cooptv::kTvNoteFile || g_tvNoteLogBudget <= 0) return;
    --g_tvNoteLogBudget;
    std::string list;
    for (size_t i = 0; i < notes.size(); ++i) list += " " + notes[i].first + "=" + (notes[i].second.empty() ? std::string("-") : notes[i].second);
    DebugLog("[TOWNOV] note read zone=" + zoneId + " towns=" + TvN((long long)notes.size()) + " [" + list + " ]"
             + (unreadable > 0 ? " unreadable=" + TvN(unreadable) : std::string()) + " path=" + path);
}

std::string TownVerReportToken()
{
    std::ostringstream o;
    o << "townOv[cache=" << g_tvCache.size() << " decided=" << g_tvDecided << " published=" << g_tvPublished << " publishNoLink=" << g_tvPublishNoLink
      << " sendFailed=" << g_tvSendFailed << " storedIn=" << g_tvStoredIn << " stale=" << g_tvStale << " refusedByServer=" << g_tvRefusedByServer
      << " applied=" << g_tvApplied << " appliedLoad=" << g_tvAppliedLoad << " appliedActivate=" << g_tvAppliedActivate << " appliedSweep=" << g_tvAppliedSweep
      << " facApplied=" << g_tvFacApplied << " facMissing=" << g_tvFacMissing << " noData=" << g_tvNoData << " nonHolderSkipped=" << g_tvNonHolderSkipped
      << " rebuildByNote=" << g_tvRebuildByNote << " keptNoNote=" << g_tvKeptNoNote << " keptSame=" << g_tvKeptSame << " rebuildDeferredLoad=" << g_tvRebuildDeferredLoad
      << " notesWritten=" << g_tvNotesWritten << " notesOffThread=" << (long long)::InterlockedCompareExchange64(&g_tvNotesOffThread, 0, 0) << " notesFailed=" << g_tvNotesFailed
      << " notesRead=" << g_tvNotesRead << " notesMissing=" << g_tvNotesMissing << " notesBase=" << g_tvNotesBase << " notesReadOffThread=" << (long long)::InterlockedCompareExchange64(&g_tvNotesReadOffThread, 0, 0) << " asBuilt=" << g_tvBook.built.size() << " pending=" << g_tvPending.size()
      << " sweepNoTown=" << g_tvSweepNoTown << " sweepDeferredLoaded=" << g_tvSweepDeferredLoaded
      << " standingOnly=" << g_tvStandingOnly << " standingStripped=" << g_tvStandingStripped << " scratchMiss=" << g_tvScratchMiss << " queryFault=" << g_tvQueryFault
      << " offThread=" << g_tvOffThread << " noHook=" << g_tvNoHook << " faults=" << g_tvFaults << " playerTown=" << g_tvPlayerTown << " standInTown=" << g_tvStandInTown
      << " solo=" << g_tvSolo << " malformed=" << g_tvMalformed << " encodeFailed=" << g_tvEncodeFailed << " feedBegins=" << g_tvFeedBegins << " listComplete=" << g_tvListComplete
      << " noPublishApplyFailed=" << g_tvNoPublishApplyFailed << " revZeroStopped=" << g_tvRevZeroStopped << " noRepublish=" << g_tvNoRepublish.size() << " loadClears=" << g_tvLoadClears
      << " seed=" << g_tvSeed.size() << " seedQueued=" << g_tvSeedQueued << " seedPublished=" << g_tvSeedPublished << " seedDropped=" << g_tvSeedDropped
      << " mine=" << g_tvMine.size() << " mineSettled=" << g_tvMineSettled << " resent=" << g_tvResent << " worldClears=" << g_tvWorldClears << "]";
    return o.str();
}

}   // namespace coop
