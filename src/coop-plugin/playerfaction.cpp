// playerfaction.cpp - see playerfaction.h. Engine facts (Read, 2026-09-02):
//   FactionDirectory::findOrAddFaction(const std::string& id, const std::string& name) 0x2E77B0: walks the manager's array
//   (+0x10, count +8) comparing each faction's GameData stringID (GameData+0x58) with `id`; absent -> new Faction(name) +
//   GameDataContainer::getData(id, FACTION) (a LOOKUP - F471: with no record it runs Faction::setup(NULL) and the engine's
//   next faction update crashes on the half-built faction, T158). So the RECORD comes first: GameDataContainer::newRecord
//   (FACTION, forceID, name) 0x6C0400, then the findOrAddFaction(GameData*) overload 0x2E7650 (new Faction(gd->name) +
//   setup(gd)). Renames: Faction::setName 0x385670 + GameDataContainer::renameRecord 0x6BF820 (the record's name).
//   FactionRelations::treatsAsEnemy 0x6B2380: relation <= -30 (.rdata 0x16CBCFC); isAlly 0x6B22E0: >= +50 (.rdata 0x1682170) or the
//   entry's ally flag. A new faction's relations start at the default (read at creation and logged, not assumed).
#include "playerfaction.h"
#include "relations.h"   // RelationsSendSnapshot on a rename
#include "policy.h"      // E36 / decision 40: PROBE P028 and the pointer the OwnedByAPlayerFaction hook compares against
#include "net/session.h"  // SessionLinked, SendRelSync
#include "store.h"        /* stand1: StoreMySlot / StoreLastKnownSlot - my slot for the wire */
#include "../common/slotwire.h"   /* stand1: coop-p<n>, @slot:<n> */
#include "worldsync.h"    /* stand1 fold (1d): WorldSyncReannounceAll */
#include "tags.h"         // tags1: a peer-faction rename re-captions the name labels
#include "coop_log.h"
#include "addresses.h"   /* mig3: coop::GameWorldPtr() / OptionsPtr() - our rows for what `ou` / `options` imported */
#include "game/GameWorld.h"
#include "game/Faction.h"
#include "game/FactionRelations.h"
#include "game/GameData.h"
#include "game/GameDataManager.h"
#include "game/Enums.h"
#include <stdint.h>   /* mig3: what <core/Functions.h> also brought in */
#include <Windows.h>
#include <sstream>

namespace {
const char* const kLegacyWirePrefix = "@player:";   /* stand1: the protocol-67 form - never sent now; a 67 game is refused at HELLO (68) */
/* stand1 (docs/design-profiles1.md s2 Required 1): ONE stand-in faction per player SLOT (the notebook slot, B13 slots.txt), record
   id coop-p<slot> (coopslot::StandInId). The table is written on the MAIN THREAD only; g_siPtr is the pointer copy another
   thread may compare against (IsPeerFaction - the policy, peace and crime detours run off the main thread). */
const int kMaxStandIns = coop::kStandInTableCap;   /* T-356: crime's sight cells are sized above it (crimewire.h kSightCellCap) */
struct StandIn { int slot; ::Faction* f; ::GameData* gd; std::string name, asked; bool placeholder; StandIn() : slot(-1), f(0), gd(0), placeholder(false) {} };   /* asked = the name last asked for (name differs when another faction record carries it); placeholder = made before its player was seen here (MakeAreaWriterStandIn), named "Player <n>" until that player's name arrives */
StandIn g_si[kMaxStandIns];
::Faction* volatile g_siPtr[kMaxStandIns];
int g_wireRelayedSlot = -1;   /* M5a fold 1: while a RELAYED message's names are resolved, its stamped sender slot; -1 = the session road */
int g_linkPeerSlot = -1;   /* the slot the one other game on the session link announced in its wire names; -1 unknown. Survives a world reload (it is the link's, and B13 slots are stable) */
unsigned g_wireSelfSlot = 0, g_wireNoSlot = 0, g_wireLegacy = 0, g_tableFull = 0;
unsigned g_placeholderMade = 0, g_placeholderNamed = 0, g_clashRenamed = 0;   /* stand-ins made before their player was seen / later named by that player / names moved by the clash rule */
::Faction* volatile g_legacyPeer = 0; bool g_legacyLooked = false;   /* stand1 fold (2b): a save's protocol-67 coop-peer, looked up once per world on the main thread */
volatile LONG g_slotHeld = 0; LONG g_slotHeldSeen = 0; long long g_slotReleases = 0; bool g_slotWaitSaid = false;   /* stand1 fold (1d): sends held for my slot */
bool g_anyMiss = false; long g_anyMissLinkGen = -1; long long g_anyMissCached = 0;   /* stand1 fold (6c): PeerFactionAny's negative answer, per world/link/table */
bool g_tickLinked = false;   /* stand1 fold (6b): the link edge that forgets the other game's slot */
unsigned g_wireSent = 0, g_wireResolved = 0, g_created = 0, g_byName = 0, g_byNameMissed = 0, g_mgrMissing = 0, g_renamed = 0, g_createFailed = 0;
float g_relationAtCreate = 0.0f; int g_relationRead = 0; unsigned g_renameRefused = 0;
std::string g_lastSeenName; ::Faction* g_lastSeenFaction = 0; bool g_worldWasTornDown = false, g_nameBaselined = false; long long g_worldRebasedLinked = 0; long long g_nameChanges = 0, g_renameVerb = 0, g_nameTickNoFaction = 0, g_nameTickBaseline = 0;   // the rename-as-state-change check (declared here: used by the verb and the tick)

template <class T> std::string S(const T& v) { std::ostringstream o; o << v; return o.str(); }
::Faction* CreatePeer(int slot, const std::string& name);
void RenamePeer(int idx, const std::string& name);
bool NameTaken(const std::string& name, ::Faction* except);
bool Plaus(const void* p) { return p != 0 && (uintptr_t)p > 0x10000 && (uintptr_t)p < 0x00007FFFFFFFFFFFull; }
// the module's own copy, as every module keeps one (identity.cpp's rule): the object and its vtable pointer both readable
bool PlausibleObject(const void* p)
{
    if (!Plaus(p) || ((uintptr_t)p & 7)) return false;
    __try { const void* vt = *(const void* const*)p; if (!Plaus(vt)) return false; volatile uintptr_t probe = *(const uintptr_t*)vt; (void)probe; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

int IsPlayerPod(::Faction* f)
{
    __try { return (*(void**)((char*)f + 0x250) != 0) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
::Faction* LocalPlayerFactionImpl()
{
    if (!Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) return 0;
    void** arr = 0; unsigned n = 0;
    __try { arr = *(void***)((char*)coop::GameWorldPtr()->factionDirectory + 0x10); n = *(unsigned*)((char*)coop::GameWorldPtr()->factionDirectory + 8); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (!Plaus(arr) || n > 4096) return 0;
    for (unsigned i = 0; i < n; ++i) { ::Faction* f = (::Faction*)arr[i]; if (Plaus(f) && IsPlayerPod(f) == 1) return f; }
    return 0;
}
int ReadFactionDataPod(::Faction* f, void** gd)
{
    __try { *gd = *(void**)((char*)f + 0x240); return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int ReadFactionArrayPod(void*** arr, unsigned* n)   /* stand1: the manager's array (+0x10, count +8), as LocalPlayerFactionImpl reads it */
{
    __try { *arr = *(void***)((char*)coop::GameWorldPtr()->factionDirectory + 0x10); *n = *(unsigned*)((char*)coop::GameWorldPtr()->factionDirectory + 8); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int ReadSidPod(::Faction* f, char* buf, int cap)   /* stand1: GameData+0x58 stringID, relations.cpp's reader's shape (v100 std::string) */
{
    __try
    {
        const void* gd = *(const void* const*)((const char*)f + 0x240);
        if (!Plaus(gd)) return 0;
        const char* s = (const char*)gd + 0x58;
        const size_t len = *(const size_t*)(s + 0x10);
        const size_t res = *(const size_t*)(s + 0x18);
        const char* p = (res >= 16) ? *(const char* const*)s : s;
        size_t k = len; if (k > (size_t)(cap - 1)) k = (size_t)(cap - 1);
        for (size_t i = 0; i < k; ++i) buf[i] = p[i];
        buf[k] = 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static unsigned long long kPfRelSetupPhase1Rva = 0; static coop::AddrReg kPfRelSetupPhase1Rva_reg("RelationsSetupPhase1", &kPfRelSetupPhase1Rva);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x6B3C30 */
typedef void (*RelSetupPhase1Fn)(void* relations, ::Faction* f);
int RelationsSetupPod(::Faction* f)
{
    __try
    {
        if (!Plaus(*(void**)((char*)f + 0x240))) return 0;   // review-p3o H3: the engine's own pass skips a faction without data
        void* rel = *(void**)((char*)f + 0x78);   // Faction::relations
        if (!Plaus(rel)) return 0;
        if (kPfRelSetupPhase1Rva == 0) return 0;
        ((RelSetupPhase1Fn)coop::AddrAbs(kPfRelSetupPhase1Rva))(rel, f);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int ReadRelationPod(::Faction* from, ::Faction* to, float* out)
{
    __try { if (!Plaus(from->relations)) return 0; *out = from->relations->relationTo(to); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
}

namespace {
// stand1: the table's helpers - MAIN THREAD writers; g_siPtr is what another thread compares against.
int FindStandIn(int slot) { for (int i = 0; i < kMaxStandIns; ++i) if (g_si[i].f != 0 && g_si[i].slot == slot) return i; return -1; }
int FindStandInPtr(const ::Faction* f) { if (f == 0) return -1; for (int i = 0; i < kMaxStandIns; ++i) if (g_siPtr[i] == f) return i; return -1; }
int FreeStandIn() { for (int i = 0; i < kMaxStandIns; ++i) if (g_si[i].f == 0) return i; return -1; }
int RegisterStandIn(int slot, ::Faction* f, ::GameData* gd, const std::string& name)
{
    int i = FindStandIn(slot); if (i < 0) i = FreeStandIn();
    if (i < 0) { ++g_tableFull; return -1; }
    g_si[i].slot = slot; g_si[i].f = f; g_si[i].gd = gd; g_si[i].name = name; g_si[i].asked = name; g_si[i].placeholder = false;
    g_siPtr[i] = f; g_anyMiss = false;   /* stand1 fold (6c) */
    return i;
}
std::string StandInsText()
{
    std::string t; int n = 0;
    for (int i = 0; i < kMaxStandIns; ++i)
        if (g_si[i].f != 0) { t += (n++ ? " " : "") + std::string("p") + S(g_si[i].slot) + "='" + g_si[i].name + "'@" + S((const void*)g_si[i].f); }
    return n ? "[" + t + "]" : std::string("none");
}
/* a stand-in's name when another faction record may carry the one wanted (coopslot::StandInClashName) - create and rename alike */
std::string ClashFreeName(const std::string& want, int slot, ::Faction* except)
{
    const bool wt = NameTaken(want, except);
    const bool pt = wt && NameTaken(want + " (peer)", except);
    return coopslot::StandInClashName(want, slot, wt, pt);
}
// F471: the record first, then the faction object from it - never findOrAddFaction(id, name).
// stand1: one per SLOT - record id coop-p<slot> (docs/design-profiles1.md s2 Required 1), through the same setupPhase1 path.
::Faction* CreatePeer(int slot, const std::string& wanted)
{
    std::string name(wanted);
    const std::string id = coopslot::StandInId(slot);
    if (FindStandIn(slot) < 0 && FreeStandIn() < 0) { ++g_tableFull; ErrorLog("[PF] stand-in for slot " + S(slot) + " refused: the table of " + S(kMaxStandIns) + " stand-ins is full"); return 0; }
    ::Faction* existing = coop::GameWorldPtr()->factionDirectory->findFactionById(id);
    ::GameData* gd = 0;
    if (PlausibleObject(existing))
    {
        void* d = 0; ReadFactionDataPod(existing, &d); gd = (::GameData*)d;
        DebugLog("[PF] peer faction already in the manager: id='" + id + "' (" + S((const void*)existing) + ", record " + S((const void*)gd) + ") - reused");
        const int idx = RegisterStandIn(slot, existing, gd, existing->getName());
        if (idx < 0) return 0;
        coop::TagsCaptionsDirty();   /* tags1 */
        coop::PolicyNotePeerFaction((void*)existing);   /* E36 / decision 40: the REUSE branch reaches a live stand-in too (P028 probe) */
        const int rs = RelationsSetupPod(existing);   // review-p3o M1: the reuse branch needs the relations setup too (idempotent: phase1 re-sets the same fields)
        DebugLog(std::string("[PF] relations setup on the reused peer faction: ") + (rs == 1 ? "phase1 ok" : rs == 0 ? "skipped" : "FAULTED"));
        if (g_si[idx].name != name) RenamePeer(idx, name);
        return existing;
    }
    name = ClashFreeName(name, slot, 0);   // review-p3-factions: never create a record under an existing faction name   /* stand1: two stand-ins may carry the same player name - " (peer)", then " (p<slot>)"; the rename uses the same rule */
    gd = coop::GameWorldPtr()->gamedata.newRecord(FACTION, id, name);
    if (!PlausibleObject(gd)) { ++g_createFailed; ErrorLog("[PF] newRecord(FACTION, '" + id + "', '" + name + "') returned " + S((const void*)gd)); return 0; }
    ::Faction* f = coop::GameWorldPtr()->factionDirectory->findOrAddFaction(gd);
    if (!PlausibleObject(f)) { ++g_createFailed; ErrorLog("[PF] findOrAddFaction(record " + S((const void*)gd) + ") returned " + S((const void*)f)); return 0; }
    void* d = 0; ReadFactionDataPod(f, &d);
    // H040 (T173/F485): a faction made at runtime never gets FactionDirectory::setupAndLinkAllFactions' per-faction pass, so its
    // FactionRelations has no owner (+8), no default relation (+0x60) and no self entry - the engine's event-driven relation
    // change then reads this->me+0x1D0 through NULL. FactionRelations::setupPhase1(this, Faction*) 0x6B3C30 (Read) sets all three.
    const int rs = RelationsSetupPod(f);
    RegisterStandIn(slot, f, gd, name); ++g_created;
    coop::TagsCaptionsDirty();   /* tags1 */
    coop::PolicyNotePeerFaction((void*)f);   /* E36 / decision 40: PROBE P028 - is the stand-in a player faction? `locked` mode rests on the answer being no */
    ::Faction* mine = LocalPlayerFactionImpl();
    float rel = 0.0f; g_relationRead = (mine != 0) ? ReadRelationPod(mine, f, &rel) : 0; g_relationAtCreate = rel;
    DebugLog("[PF] peer faction created: id='" + id + "' slot=" + S(slot) + " name='" + name + "' " + S((const void*)f) + " record " + S((const void*)gd)
             + " (faction data field " + S(d) + (d == (void*)gd ? " = the record" : " != the record!") + ") getName='" + f->getName() + "' isPlayer=" + S(IsPlayerPod(f))
             + " relation(mine->peer)=" + (g_relationRead == 1 ? S(rel) : std::string("unread")) + " (enemy<=-30, ally>=50)"
             + " relations setup: " + (rs == 1 ? "phase1 ok" : rs == 0 ? "NO relations object" : "phase1 FAULTED"));
    return f;
}
// review-p3h H2: the hazard is the record index (GameDataContainer::renameRecord), so the check is against RECORDS, not live factions
bool NameTaken(const std::string& name, ::Faction* except)
{
    if (!Plaus(coop::GameWorldPtr())) return false;
    ::GameData* gd = coop::GameWorldPtr()->gamedata.findRecordByName(name, FACTION);
    if (!PlausibleObject(gd)) return false;
    if (except != 0) { void* d = 0; ReadFactionDataPod(except, &d); if (d == (void*)gd) return false; }
    return true;
}
void RenamePeer(int idx, const std::string& want)
{
    if (idx < 0 || idx >= kMaxStandIns || g_si[idx].f == 0) return;
    StandIn& s = g_si[idx];
    s.asked = want;   /* ResolveStandIn asks again only when the wanted name changes */
    /* a name another faction record carries is resolved by the create path's rule, never refused: two players may pick one name, and
       a placeholder stand-in must still take its player's name */
    const std::string name = ClashFreeName(want, s.slot, s.f);
    if (name == s.name) return;
    const std::string before = s.name;
    s.f->setName(name);
    if (PlausibleObject(s.gd)) coop::GameWorldPtr()->gamedata.renameRecord(s.gd, name);
    s.name = name; ++g_renamed; if (name != want) ++g_clashRenamed;
    coop::TagsCaptionsDirty();   /* the name tags show this faction name */
    DebugLog("[PF] peer faction renamed (slot " + S(s.slot) + ") '" + before + "' -> '" + name + "'"
             + (name != want ? " ('" + want + "' is another faction record's name)" : std::string()) + " (getName now '" + s.f->getName() + "')");
}
// Named after that player's own faction; if that collides with OUR player faction's name (both "Nameless" by default) it is
// suffixed - name lookups elsewhere in this plugin must never land on the wrong player. The name is re-applied on every
// resolve, so a player renaming their faction mid-session is followed (the relations snapshot sent on a rename carries it too).
::Faction* ResolveStandIn(int slot, const std::string& raw)
{
    std::string name(raw);
    if (name.empty()) name = "Player 2";
    ::Faction* mine = LocalPlayerFactionImpl();
    if (mine != 0 && name == mine->getName()) name += " (2)";
    const int i = FindStandIn(slot);
    if (i >= 0 && PlausibleObject(g_si[i].f))
    {
        if (g_si[i].placeholder)
        {
            if (raw.empty()) return g_si[i].f;   /* no name from that player yet: the placeholder name stays */
            g_si[i].placeholder = false; ++g_placeholderNamed;
            DebugLog("[PF] stand-in " + coopslot::StandInId(slot) + " was made before its player was seen here; it takes that player's name '" + name + "' now");
        }
        if (name != g_si[i].asked) RenamePeer(i, name);
        return g_si[i].f;
    }
    return CreatePeer(slot, name);
}
void NoteLinkPeerSlot(int slot)
{
    if (slot == g_linkPeerSlot) return;
    DebugLog("[PF] the other game on the session link speaks for slot " + S(slot) + (g_linkPeerSlot >= 0 ? " (was " + S(g_linkPeerSlot) + ")" : std::string())
             + " - its stand-in is " + coopslot::StandInId(slot));
    g_linkPeerSlot = slot; g_anyMiss = false;   /* stand1 fold (6c) */
}
}

namespace coop {

bool IsPlayerFaction(::Faction* f) { return Plaus(f) && IsPlayerPod(f) == 1; }
bool IsPeerFaction(::Faction* f) { return FindStandInPtr(f) >= 0; }   /* stand1: ANY player's stand-in (coop-p<n>) created or reused here - pointer compares only, any thread */
bool IsStandInFaction(::Faction* f) { return f != 0 && (FindStandInPtr(f) >= 0 || f == g_legacyPeer); }   /* stand1 fold (review-stand1 2b): an old save's coop-peer too */
void NoteHeldForSlot() { ::InterlockedIncrement(&g_slotHeld); }   /* stand1 fold (1d) */
int StandInSlotOf(::Faction* f) { const int i = FindStandInPtr(f); return i >= 0 ? g_si[i].slot : -1; }
int StandInAnyAnyThread() { for (int i = 0; i < kMaxStandIns; ++i) if (g_siPtr[i] != 0) return 1; return 0; }   /* P26s6 fold 1: ANY THREAD - FindStandInPtr's own reads; S6-2 skips its retry while no stand-in exists */
/* inv4 fold (review-inv4): ANY THREAD, POD. The slot a faction's RECORD id names ("coop-p<n>"), whether or not the stand-in table
   has it yet (the table is empty after a world load, ResetPlayerFactionState). -2 = "coop-peer" (protocol 67), -1 = any other
   id, -3 = the record id cannot be read. */
int StandInRecordSlot(::Faction* f)
{
    if (!Plaus(f)) return -3;
    char id[64]; id[0] = 0;
    if (ReadSidPod(f, id, 64) == 0) return -3;
    const std::string s(id);
    if (s == coopslot::kLegacyPeerId) return -2;
    const std::string pre(coopslot::kStandInPrefix);
    if (s.size() <= pre.size() || s.compare(0, pre.size(), pre) != 0) return -1;
    int n = 0;
    for (size_t i = pre.size(); i < s.size(); ++i)
    {
        if (s[i] < '0' || s[i] > '9') return -1;
        n = n * 10 + (s[i] - '0');
        if (n > coopslot::kSlotMax) return -1;
    }
    return n;
}
int MySlotForWire() { const int s = coop::StoreMySlot(); return s >= 0 ? s : coop::StoreLastKnownSlot(); }
int LinkPeerSlot() { return g_linkPeerSlot; }
void NoteLinkPeerSlotAnnounced(int slot) { NoteLinkPeerSlot(slot); }   /* M5b: MSG_PEER_SLOT */
/* M5b fold 1 (review 2026-09-30 item 3): the link-down EDGE forgets the other game's slot. The tick below compared SessionLinked()
   tick to tick, which cannot see a drop and a re-connect drained in one Poll (the transport reads UP on both ticks). */
void PlayerFactionOnLinkDown()
{
    if (g_linkPeerSlot < 0) return;
    DebugLog("[PF] link down - forgetting the other game's slot " + S(g_linkPeerSlot));
    g_linkPeerSlot = -1; g_anyMiss = false;
}
int WireSenderSlot() { return g_wireRelayedSlot >= 0 ? g_wireRelayedSlot : g_linkPeerSlot; }   /* M5b */
void SetWireRelayedSender(int slot) { g_wireRelayedSlot = slot; }
::Faction* StandInForSlot(int slot) { const int i = FindStandIn(slot); return (i >= 0 && PlausibleObject(g_si[i].f)) ? g_si[i].f : 0; }
int StandInList(int* slots, ::Faction** out, int cap)   /* T-356: crime's sight cells - one per other player's faction */
{
    int n = 0;
    for (int i = 0; i < kMaxStandIns && n < cap; ++i)
        if (g_si[i].f != 0 && PlausibleObject(g_si[i].f)) { slots[n] = g_si[i].slot; out[n] = g_si[i].f; ++n; }
    return n;
}
bool StandInExistsForSlot(int slot)
{
    if (slot < 0) return false;
    if (StandInForSlot(slot) != 0) return true;
    if (!Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) return false;
    return PlausibleObject(coop::GameWorldPtr()->factionDirectory->findFactionById(coopslot::StandInId(slot)));
}
/* MAIN THREAD, engine writes allowed (store.cpp AreaWriterStandInsTick, at the head of the drain): a stand-in for player `slot`
   before that player has been seen here, so an area record that player wrote can be loaded with its owners translated. Made by
   CreatePeer, the path every stand-in takes (record first, then the faction, then its relations setup), under the placeholder
   name "Player <slot>"; ResolveStandIn finds it when that player's characters or names arrive and renames it - never a second
   one. 1 made; 0 one exists already (in the table, or a coop-p<slot> record the loaded world carries, which the area swap accepts
   and CreatePeer reuses later); -1 not made (*detail says why). */
int MakeAreaWriterStandIn(int slot, std::string* detail)
{
    const int me = MySlotForWire();
    if (slot < 0 || slot > coopslot::kSlotMax || me < 0 || slot == me) { if (detail) *detail = "not another player's number (mine " + S(me) + ")"; return -1; }
    if (!Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) { if (detail) *detail = "no faction manager"; return -1; }
    if (StandInExistsForSlot(slot)) return 0;
    std::string name = coopslot::PlaceholderName(slot);
    ::Faction* mine = LocalPlayerFactionImpl();
    if (mine != 0 && name == mine->getName()) name += " (2)";   /* never my own faction's name */
    ::Faction* f = CreatePeer(slot, name);
    const int i = FindStandIn(slot);
    if (f == 0 || i < 0) { if (detail) *detail = "CreatePeer made none - the [PF] line above says why"; return -1; }
    g_si[i].placeholder = true; ++g_placeholderMade;
    if (detail) *detail = "'" + g_si[i].name + "' " + S((const void*)f);
    return 1;
}
std::string StandInDisplayName(::Faction* f) { const int i = FindStandInPtr(f); return i >= 0 ? g_si[i].name : std::string(); }
::Faction* LocalPlayerFaction() { return LocalPlayerFactionImpl(); }
/* stand1: "the one other player" - the stand-in of the game on the session link (its slot learned from its wire names); before
   that slot is known, the first stand-in in the table. A caller that knows WHOSE character or building it holds asks that
   object's own faction or StandInForSlot instead. */
::Faction* PeerFaction()
{
    if (g_linkPeerSlot >= 0) return StandInForSlot(g_linkPeerSlot);
    for (int i = 0; i < kMaxStandIns; ++i) if (g_si[i].f != 0 && !g_si[i].placeholder && PlausibleObject(g_si[i].f)) return g_si[i].f;   /* never a placeholder: its player has not been seen, so it is not the game on the link */
    return 0;
}
std::string PeerFactionDisplayName() { return StandInDisplayName(PeerFaction()); }   /* tags1; stand1: the one other game on the link */
/* Z1-b / review-pvp1 4: MAIN THREAD. The one other player's stand-in: created or reused here, else a coop-p<n> (n not mine) the
   loaded world already carries (the manager is asked by id, never by name), else a protocol-67 save's `coop-peer`. */
::Faction* PeerFactionAny()
{
    if (PeerFaction() != 0) return PeerFaction();
    if (!Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) return 0;
    if (g_anyMiss && g_anyMissLinkGen == StorePeerEpoch()) { ++g_anyMissCached; return 0; }   /* M11 C2: a newcomer's stand-in may exist now */   /* stand1 fold (review-stand1 6c): walked already, nothing there */
    if (g_linkPeerSlot >= 0) { ::Faction* f = coop::GameWorldPtr()->factionDirectory->findFactionById(coopslot::StandInId(g_linkPeerSlot)); if (PlausibleObject(f)) return f; }
    const int me = MySlotForWire();
    void** arr = 0; unsigned n = 0;
    if (ReadFactionArrayPod(&arr, &n) && Plaus(arr) && n <= 4096)
        for (unsigned i = 0; i < n; ++i)
        {
            ::Faction* f = (::Faction*)arr[i]; char sid[64];
            if (!Plaus(f) || ReadSidPod(f, sid, 64) != 1) continue;
            const int s = coopslot::StandInSlotOfId(std::string(sid));
            if (s >= 0 && s != me && PlausibleObject(f)) return f;
        }
    ::Faction* legacy = coop::GameWorldPtr()->factionDirectory->findFactionById(std::string(coopslot::kLegacyPeerId));
    if (PlausibleObject(legacy)) return legacy;
    g_anyMiss = true; g_anyMissLinkGen = StorePeerEpoch();   /* M11 C2 */   /* stand1 fold (6c): not walked again until the world, the link or the table changes */
    return 0;
}

bool PeerFactionExists() { return PeerFactionAny() != 0; }

std::string WireFactionName(::Faction* f)
{
    if (!Plaus(f)) return std::string();
    const std::string name = f->getName();
    if (IsPlayerPod(f) == 1) { ++g_wireSent; return coopslot::SlotWire(MySlotForWire(), name); }   /* stand1: "@slot:<my slot>:<name>" ("@slot:?:<name>" before I have one) */
    return name;
}

::Faction* ResolveWireFaction(const std::string& wire)
{
    if (wire.empty() || !Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) { if (!wire.empty()) ++g_mgrMissing; return 0; }
    if (wire.compare(0, 8, kLegacyWirePrefix) == 0) { if (++g_wireLegacy <= 3) ErrorLog("[PF] protocol-67 wire name '" + wire + "' - not resolved (a 67 game is refused at HELLO; logged 3x)"); return 0; }
    int slot = -1; std::string name;
    if (!coopslot::ParseSlotWire(wire, &slot, &name, 0))
    {
        ::Faction* f = coop::GameWorldPtr()->factionDirectory->findFactionByName(wire);
        if (PlausibleObject(f)) ++g_byName; else ++g_byNameMissed;
        return f;
    }
    ++g_wireResolved;
    if (slot < 0)   /* the sender had no slot yet: the only game it can be is the one on this link */
    {
        ++g_wireNoSlot; slot = (g_wireRelayedSlot >= 0) ? g_wireRelayedSlot : g_linkPeerSlot;   /* M5a fold 1: a relayed sender is the notebook's stamp, not the session peer */
        if (slot < 0) { if (g_wireNoSlot <= 3) ErrorLog("[PF] wire '" + wire + "' carries no slot and the link peer's slot is not known yet - not resolved (logged 3x)"); return 0; }
    }
    if (slot == MySlotForWire()) { if (++g_wireSelfSlot <= 3) ErrorLog("[PF] wire '" + wire + "' names MY OWN slot " + S(slot) + " - refused: two games on one notebook slot? (logged 3x)"); return 0; }
    /* M5b (carried leftover): a name no longer moves LinkPeerSlot - a third player's name on the session road (or a relayed one)
       would re-key the session peer's claims; only the peer's own PEER_SLOT sets it (net/session.cpp OnPeerSlot) */   /* M5a fold 1: only the session road names the session peer - a relayed name is another player's */
    return ResolveStandIn(slot, name);
}

/* the 'peerfaction <name>' verb (T159): a stand-in without a peer - the link peer's slot, else the first slot that is not mine */
::Faction* PeerFactionForTest(const std::string& name)
{
    if (!Plaus(coop::GameWorldPtr()) || !Plaus(coop::GameWorldPtr()->factionDirectory)) return 0;
    int s = g_linkPeerSlot;
    if (s < 0) s = (MySlotForWire() == 1) ? 0 : 1;
    return ResolveStandIn(s, name);
}

bool RenameMyFaction(const std::string& name)
{
    ::Faction* mine = LocalPlayerFactionImpl();
    if (mine == 0 || name.empty()) return false;
    if (NameTaken(name, mine)) { ErrorLog("[PF] rename refused: a faction record named '" + name + "' already exists"); return false; }   // review-p3a S7 / p3h H2
    const std::string before = mine->getName();
    mine->setName(name);   // the tick's name check sends the snapshot (one path for the verb and the game's own UI)
    void* d = 0; ReadFactionDataPod(mine, &d);
    if (PlausibleObject(d)) coop::GameWorldPtr()->gamedata.renameRecord((::GameData*)d, name);
    DebugLog("[PF] my player faction renamed '" + before + "' -> '" + name + "' (getName now '" + mine->getName() + "')");
    ++g_renameVerb;        // review-p3s M1: the verb's renames are counted apart from the game's own
    PlayerFactionTick();   // the same state check the tick runs: sends the snapshot now
    return true;
}

/* names2a (investigations/names2-design.md Q3): my faction's name owed to my notebook profile row - set once per world at the
   name baseline (a rename made offline, a save whose name differs from the profile's) and when a rename could not be sent. */
bool g_facRowPending = false;

// review-p3r H1: Faction::setName 0x385670 has NO call sites in the engine's .text (inlined everywhere; T176 saw only the plugin's
// own calls), so a hook cannot see an in-game rename. The rename is a state change: the tick compares my player faction's live
// name with the last one seen and sends the standings snapshot on a change (the snapshot carries "@player:<new name>").

void PlayerFactionTick()
{
    /* stand1 fold (review-stand1 6b): the link dropped (the other player left) - its slot is forgotten; the next game to connect announces its own.
       M5b fold 1 (item 3): the forget is done on the link-down edge now (PlayerFactionOnLinkDown). This check is KEPT as a harmless
       backstop: after the edge has run g_linkPeerSlot is -1 and it does nothing. */
    const bool linkedNow = net::SessionLinked();
    if (g_tickLinked && !linkedNow && g_linkPeerSlot >= 0) { DebugLog("[PF] link down - forgetting the other game's slot " + S(g_linkPeerSlot)); g_linkPeerSlot = -1; g_anyMiss = false; }
    g_tickLinked = linkedNow;
    /* stand1 fold (review-stand1 1d): sends held while this game had no slot (SPAWN faction names, relation pairs) are released - my
       characters re-announced and my standings re-sent - the first tick it has one */
    const LONG held = g_slotHeld;
    if (held != g_slotHeldSeen)
    {
        if (MySlotForWire() < 0)
        {
            if (!g_slotWaitSaid) { g_slotWaitSaid = true; DebugLog("[PF] waiting for this game's player number before sharing characters (held so far " + S((long)held) + ")"); }
        }
        else
        {
            ++g_slotReleases; g_slotHeldSeen = held; g_slotWaitSaid = false;
            DebugLog("[PF] this game's player number is " + S(MySlotForWire()) + " - releasing " + S((long)held) + " held sends: re-announcing my characters and my standings");
            if (net::SessionLinked() || coop::StoreLiveReady()) { WorldSyncReannounceAll(); RelationsSendSnapshot(); }   /* on either road: the world server's (every game's road) or the old link */
        }
    }
    ::Faction* mine = LocalPlayerFactionImpl();
    if (!Plaus(mine)) { ++g_nameTickNoFaction; return; }   // no world (a load in progress): the baseline is reset by the teardown, not here
    if (!g_legacyLooked && Plaus(coop::GameWorldPtr()) && Plaus(coop::GameWorldPtr()->factionDirectory))   /* stand1 fold (2b): once per world, on the main thread */
    {
        g_legacyLooked = true;
        ::Faction* lf = coop::GameWorldPtr()->factionDirectory->findFactionById(std::string(coopslot::kLegacyPeerId));
        g_legacyPeer = PlausibleObject(lf) ? lf : 0;
        if (g_legacyPeer != 0) DebugLog("[PF] this world carries a protocol-67 coop-peer faction " + S((const void*)lf) + " - dormant; its buildings still count as another player's");
    }
    const std::string& now = mine->getName();
    // review-p3s M3: keyed on the faction OBJECT too - a different Faction* is a new baseline (a reload), not a rename
    if (mine != g_lastSeenFaction || !g_nameBaselined)   // review-s6 M4: an empty name must not re-arm the edge every frame
    {
        const bool rebuilt = (g_lastSeenFaction != 0 || g_worldWasTornDown);   // a NEW faction object after one was seen (or after a teardown): the world was rebuilt
        g_lastSeenFaction = mine; g_lastSeenName = now; g_nameBaselined = true; ++g_nameTickBaseline; g_worldWasTornDown = false; g_facRowPending = true;   /* names2a */
        if (rebuilt && (net::SessionLinked() || coop::StoreLiveReady()))   /* M5a fold 1 (#4): a notebook-only game re-bases too */
        {
            // review-p3s M4: the standings on both sides must be re-based on the new world - my owned entries go out, the peer's are asked for
            ++g_worldRebasedLinked;
            DebugLog("[PF] world rebuilt while linked - sending my standings snapshot and asking the peer for theirs (RELSYNC)");
            RelationsSendSnapshot(); net::SendRelSync();
        }
        return;
    }
    if (now == g_lastSeenName)
    {
        if (g_facRowPending && StoreProfileFactionName(now, "world load")) g_facRowPending = false;   /* names2a: once the link is up and a profile is picked */
        return;
    }
    ++g_nameChanges;
    DebugLog("[PF] my player faction's name changed '" + g_lastSeenName + "' -> '" + now + "' - sending the standings snapshot so the peer learns it now");
    g_lastSeenName = now;
    RelationsSendSnapshot();
    g_facRowPending = !StoreProfileFactionName(now, "rename");   /* names2a: the lobby's faction column follows the rename */
}

void ReportPlayerFaction()
{
    DebugLog("[PF] REPORT peer=" + StandInsText() + " created=" + S(g_created)
             + " wireSent=" + S(g_wireSent) + " wireResolved=" + S(g_wireResolved) + " byName=" + S(g_byName) + " byNameMissed=" + S(g_byNameMissed)
             + " mgrMissing=" + S(g_mgrMissing) + " renamed=" + S(g_renamed) + " nameChanges=" + S(g_nameChanges) + " renameVerb=" + S(g_renameVerb) + " nameTickBaseline=" + S(g_nameTickBaseline) + " nameTickNoFaction=" + S(g_nameTickNoFaction) + " worldRebasedLinked=" + S(g_worldRebasedLinked) + " createFailed=" + S(g_createFailed) + " mySlot=" + S(MySlotForWire()) + " linkPeerSlot=" + S(g_linkPeerSlot) + " wireSelfSlot=" + S(g_wireSelfSlot) + " wireNoSlot=" + S(g_wireNoSlot) + " wireLegacy=" + S(g_wireLegacy) + " tableFull=" + S(g_tableFull) + " placeholders[made,named,clashRenamed]=" + S(g_placeholderMade) + "," + S(g_placeholderNamed) + "," + S(g_clashRenamed) + " slotHeld=" + S((long)g_slotHeld) + " slotReleases=" + S(g_slotReleases) + " anyMissCached=" + S(g_anyMissCached) + " legacyPeer=" + S((const void*)g_legacyPeer) + " relationAtCreate=" + (g_relationRead == 1 ? S(g_relationAtCreate) : std::string("unread")));
}
}

namespace coop {
// review-p3-factions: a game reload frees every Faction; the peer pointers must not survive it (called from the world-init hook)
void ResetPlayerFactionState() { coop::PolicyForgetPeerFaction();   /* E36 / decision 40: the stand-in's address dies with the world - see policy.cpp */
                                for (int i = 0; i < kMaxStandIns; ++i) { g_siPtr[i] = 0; g_si[i] = StandIn(); }   /* stand1: every stand-in dies with the world; the link peer's slot does not */
                                g_legacyPeer = 0; g_legacyLooked = false; g_anyMiss = false;   /* stand1 fold */
                                g_renameRefused = 0; g_lastSeenName.clear(); g_lastSeenFaction = 0; g_nameBaselined = false; g_worldWasTornDown = true; }
}
