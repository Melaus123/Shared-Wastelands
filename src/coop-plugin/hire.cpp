// hire.cpp - recruit1 R0-a (docs/design-recruit1.md 1-5): hiring a recruit the other game runs.
//
// Roles: the PERSON (the recruit), the OWNER (the game where net::IsUidMine(person) is true), the HIRER (the game whose player
// hires). Both games run this same code.
//   hirer: HOLD - pending[uid] + REQ (the lever enters here; R0-b: so does a real hire reply, via the 0x67FAD0 detour HoldLine)
//   owner: live check at the K2 safe point, in arrival order; pass -> reserved[uid] + OK, fail -> NO {reason}
//   hirer: on OK - live re-check (hirer character, purse), TakeLocalOwner + UnpuppetForOwnership, PlayerInterface::recruit
//          0x693010, then Ownerships::takeMoney (vt slot 0) for the price; the emptied context platoon is forgotten; DONE
//   owner: on DONE taken=1 - joined: Character vt+0xA0 setFaction 0x5CB030 (peer faction, the hirer copy's platoon);
//          then ReleaseLocalOwner + WorldStateOnOwnershipReleased + AdoptRemotePuppet; reserved cleared
// setFaction read (2026-09-25, Read, scratchpad decompiles 5cb030/593950/6b9500/6ba5d0/7966a0/794c00): old faction's
// removeMember -> old ActivePlatoon vt+0x18 (list removal, leader reset, char platoon cleared, handle events queued - NO
// free); char+0x10 = faction; new faction's addMember -> platoon vt+0x10 (a null platoon makes one); then the AI goals are
// rebuilt (0x6214F0 / 0x6210E0). An emptied platoon is left to the engine's own faction update (and store.cpp's
// removeSquad detour already forgets a context platoon it frees).
// No timers and no resends: every message is reliable; a player leaving is the one loss, handled as an event for that player's rows
// (HireForgetPeer: the requests this game made of it, the promises this game made to it, its messages not yet drained).
#include "hire.h"
#include "addresses.h"
#include "spawn.h"          // FindSpawned, FindSpawnedUid, MirrorSlot, SafeReadPosition, ForgetContextPlatoon, CopySquadIdCheckHired
#include "tags.h"           // TagsNoteCopy - a recruit of another player gets that player's name tag
#include "store.h"          // EngineWritesBlocked, StoreMainThreadId, StoreUnbindPlatoon
#include "net/session.h"    // IsUidMine, TakeLocalOwner, ReleaseLocalOwner, SendHire
#include "playerfaction.h"  // IsPlayerFaction, IsPeerFaction, LocalPlayerFaction, StandInForSlot
#include "replicate.h"      // UnpuppetForOwnership, AdoptRemotePuppet, IsDownedCharacter
#include "worldstate.h"     // WorldStateOnOwnershipReleased
#include "speech.h"         // SayOnCopy (the refusal line)
#include "handoff.h"        // SquadActivePlatoonOf - reads a character's squad without making one
#include "coop_log.h"
#include "../common/peergone.h"   // PlayerGoneTakesRow: a player who leaves takes only its own rows
#include "game/Character.h"
#include "game/Faction.h"
#include "game/hand.h"
#include "hooks.h"   /* mig1: coop::AddHook (own MinHook) */
#include "worldsync.h"   /* PauseSnapshot: the paused flag and the speed for the editorstate line */
#include "../common/editorstate.h"
#include <mygui/MyGUI_Widget.h>
#include <float.h>   /* _finite: a non-finite speed prints ? */
#include <Windows.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <map>
#include <vector>
#include <sstream>

namespace coop {
namespace {

unsigned long long kDoActionsRva = 0; static AddrReg kDoActionsRva_reg("DialogueDoActions", &kDoActionsRva);           /* Steam_1.0.65 0x67FAD0 Dialogue::_doActions */
unsigned long long kRecruitRva = 0; static AddrReg kRecruitRva_reg("PlayerInterfaceRecruit", &kRecruitRva);             /* Steam_1.0.65 0x693010 PlayerInterface::recruit */
unsigned long long kPlayerIfaceRva = 0; static AddrReg kPlayerIfaceRva_reg("PlayerInterfaceGlobal", &kPlayerIfaceRva);  /* Steam_1.0.65 0x2133630 (var) */
unsigned long long kEndDialogueRva = 0; static AddrReg kEndDialogueRva_reg("DialogueEndDialogue", &kEndDialogueRva);   /* Steam_1.0.65 0x673DA0 Dialogue::endDialogue(dlg, 1) */

const size_t kCharFactionOff   = 0x10;    // RootObject+0x10 Faction* (Read 593bb0: getOwnerFaction returns it)
const size_t kCharBodyOff      = 0x648;   // Character -> CharBody (combat.cpp CombatOf)
const size_t kCombatModeOff    = 0x130;   // CombatClass combatModeActive (combat.cpp F204)
const size_t kFacOwnershipsOff = 0x80;    // Faction -> Ownerships (answers R3, Confirmed bytes 0x682865)
const size_t kOwnMoneyOff      = 0x88;    // Ownerships money (items.cpp, Confirmed)
const size_t kPlatoonCountOff  = 0x58;    // ActivePlatoon member count (Read 794c00 / 7966a0)
const size_t kSetFactionSlot   = 0xA0;    // Character vt+0xA0 = setFaction 0x5CB030 (Confirmed, vtable_Character)
const size_t kLineCountOff     = 0x210;   // DialogLineData action count (Read 67fad0:712)
const size_t kLineActsOff      = 0x218;   // DialogLineData action array of {int type, int value}* (Read 67fad0:715)
const size_t kDlgMeOff         = 0x150;   // Dialogue -> the character who owns the line (the person)
const size_t kDlgTargetOff     = 0x158;   // Dialogue -> hand of the conversation target
const size_t kInSomethingOff   = 0x2F8;   // Character int inSomething (1 bed, 2 cage) - spawn.cpp kCarryInSomethingOff
const size_t kDlgTargetTypeOff = 0x160;   // the target hand's type (1 = character, Read 67fad0:287)
const float  kNearMax          = 3000.0f;   /* T349: the bar's recruits were not within 300 u of the player at the start spot (test lever only) */
const int    kDefaultPrice     = 100;
const float  kSameX = -50978.0f, kSameZ = 2932.0f;   /* hiretest near same: The Hub (T335 townlist; the harness teleport point) */
const int    kLogCap           = 60;
const char*  kRefusalLine      = "Sorry, I've just agreed to go with someone else.";

typedef void (*DoActionsFn)(void* dlg, void* line, void* a3, void* a4);
typedef char (*RecruitFn)(void* playerInterface, ::Character* c, bool edit);
typedef bool (*TakeMoneyFn)(void* ownerships, int amount);
typedef void (*SetFactionFn)(::Character* c, ::Faction* f, void* platoon);
typedef void (*EndDialogueFn)(void* dlg, char a2);   /* 0x673DA0 (Read decomp_673da0) */

DoActionsFn g_doActionsOrig = 0;
int g_doActionsHook = 0;   // 1 armed, -1 AddHook failed, -2 no table address

struct Pending { unsigned int reqId; unsigned int hirerUid; int price; int joinType; std::string faction; int ownerSlot; };   // ownerSlot: the player the REQ went to (the person's owner then; -1 unknown)
struct Reserved { unsigned int reqId; unsigned int hirerUid; unsigned int peer; int slot; };   // slot: the player the promise is made to (the asker; -1 unknown)
struct Inbound { coophire::HireMsg m; unsigned int peer; };

std::map<unsigned int, Pending>  g_pending;    // hirer: person uid -> my request in flight
std::map<unsigned int, Reserved> g_reserved;   // owner: person uid -> the request that won
std::vector<Inbound>             g_inbound;    // MAIN THREAD: arrival order, drained at the K2 safe point
unsigned int g_nextReqId = 1;

// the lever (MAIN THREAD: armed by the verb, drained at the safe point)
int g_testArmed = 0; int g_testMode = 0; unsigned int g_testUid = 0; int g_testPrice = kDefaultPrice;   /* mode 1 near, 2 uid, 3 near same */
int g_testJoinType = 18;   /* the hire's join type: 18 no editor; 3 (hiretest editor ...) recruit with the editor, as the dialogue's act 3 */
float g_testX = kSameX, g_testZ = kSameZ;

long long g_reqOut = 0, g_reqIn = 0, g_okOut = 0, g_okIn = 0, g_noIn = 0, g_doneOut = 0, g_doneIn = 0, g_taken = 0;
long long g_noOut[coophire::kNoReasonMax + 1] = { 0 };
long long g_recruitRefused = 0, g_purseShort = 0, g_released = 0, g_factionSet = 0, g_joinedApplied = 0, g_lateOk = 0;
long long g_cancelledLinkDown = 0, g_otherActs = 0, g_bad = 0, g_sendFailed = 0, g_ctxForgotten = 0, g_hirerGone = 0;
long long g_localHires = 0, g_localRefused = 0, g_testRuns = 0, g_testNoPerson = 0, g_testNoHirer = 0, g_noPeerFaction = 0;
long long g_linesSeen = 0, g_linesPlayer = 0, g_linesLogged = 0, g_linesUnread = 0, g_setFactionFault = 0;
volatile LONGLONG g_linesOffMain = 0;   // any thread (the detour runs wherever the engine calls it)
long long g_linesHeld = 0, g_linesOwn = 0, g_linesNoUid = 0, g_linesNoHirer = 0, g_linesNoAddr = 0, g_endFault = 0;
std::map<int, long long> g_otherActTypes;   // held lines: action type -> count (not run)

std::string N(long long v) { std::ostringstream o; o << v; return o.str(); }
bool Plaus(const void* p) { return p != 0 && (uintptr_t)p > 0x10000 && (uintptr_t)p < 0x00007FFFFFFFFFFFull; }
uintptr_t Base() { return (uintptr_t)::GetModuleHandleA(0); }

// ---- POD reads/calls (no objects with destructors in a __try frame: C2712) ----
::Faction* FactionOfPod(::Character* c)
{
    __try { return *(::Faction**)((char*)c + kCharFactionOff); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int InCombatPod(::Character* c)   // 1 / 0, -1 unreadable
{
    __try
    {
        void* body = *(void**)((char*)c + kCharBodyOff);
        if (!Plaus(body)) return -1;
        void* cc = *(void**)((char*)body + 8);   // getCombat: [[c+0x648]+8]
        if (!Plaus(cc)) return -1;
        return *((unsigned char*)cc + kCombatModeOff) != 0 ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
void* PlatoonOfPod(::Character* c)
{
    __try { return c->getSquad(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int InSomethingPod(::Character* c)   // -1 unreadable
{
    __try { return *(int*)((char*)c + kInSomethingOff); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int PlatoonCountPod(void* platoon)   // -1 unreadable
{
    if (!Plaus(platoon)) return -1;
    __try { return (int)*(unsigned int*)((char*)platoon + kPlatoonCountOff); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
void* OwnershipsPod(::Faction* f)
{
    if (!Plaus(f)) return 0;
    __try { void* o = *(void**)((char*)f + kFacOwnershipsOff); return Plaus(o) ? o : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int MoneyPod(void* own, int* out)
{
    __try { *out = *(int*)((char*)own + kOwnMoneyOff); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int TakeMoneyPod(void* own, int amount)   // 1 paid, 0 refused (short), -1 fault
{
    __try { TakeMoneyFn fn = (TakeMoneyFn)(*(void***)own)[0]; return fn(own, amount) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int RecruitPod(void* pi, ::Character* c, bool edit)   // 1 joined, 0 refused (squad full / limit), -1 fault
{
    __try { RecruitFn fn = (RecruitFn)(Base() + (uintptr_t)kRecruitRva); return fn(pi, c, edit) != 0 ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
void* PlayerInterfacePod()
{
    if (kPlayerIfaceRva == 0) return 0;
    __try { void* pi = *(void**)(Base() + (uintptr_t)kPlayerIfaceRva); return Plaus(pi) ? pi : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int SetFactionPod(::Character* c, ::Faction* f, void* platoon)   // 1 called, 0 fault
{
    __try { SetFactionFn fn = (SetFactionFn)(*(void***)c)[kSetFactionSlot / 8]; fn(c, f, platoon); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int EndDialoguePod(void* dlg)   // 1 called, 0 fault
{
    __try { EndDialogueFn fn = (EndDialogueFn)(Base() + (uintptr_t)kEndDialogueRva); fn(dlg, 1); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// the detour's read of one dialogue line: its {type,value} pairs, the person and (hand type 1) the target character
int ReadLinePod(void* dlg, void* line, int* types, int* values, int cap, int* total, ::Character** person, ::Character** target)
{
    __try
    {
        const unsigned int n = *(unsigned int*)((char*)line + kLineCountOff);
        int** acts = *(int***)((char*)line + kLineActsOff);
        *total = (int)n;
        int k = 0;
        for (unsigned int i = 0; i < n && k < cap; ++i)
        {
            int* a = acts[i];
            if (!Plaus(a)) continue;
            types[k] = a[0]; values[k] = a[1]; ++k;
        }
        *person = *(::Character**)((char*)dlg + kDlgMeOff);
        *target = 0;
        if (*(int*)((char*)dlg + kDlgTargetTypeOff) == 1)
            *target = ((const hand*)((char*)dlg + kDlgTargetOff))->getCharacter();
        return k;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// ---- helpers ----
std::string FactionName(::Faction* f) { return Plaus(f) ? f->getName() : std::string(); }
bool Alive(::Character* c) { return Plaus(c) && !IsDownedCharacter(c); }

bool Send(const coophire::HireMsg& m, unsigned int toPeer)
{
    if (net::SendHire(m, toPeer)) return true;
    ++g_sendFailed;
    DebugLog("[HIRE] send kind=" + N(m.kind) + " uid=" + N(m.uid) + " FAILED (no road, or nobody to address)");
    return false;
}
void SendDone(unsigned int toPeer, unsigned int reqId, unsigned int uid, unsigned int hirerUid, int taken, int joined, unsigned int gen = 0)
{
    coophire::HireMsg d; d.kind = coophire::kHireDone; d.reqId = reqId; d.uid = uid; d.hirerUid = hirerUid; d.taken = taken; d.joined = joined;
    d.gen = gen;   /* M7a A1 build 1 [a1b1-hi0] [review F10]: taken=1 - the gen this game holds now; the owner records the same */
    if (Send(d, toPeer)) ++g_doneOut;
}
void SayRefusal(unsigned int uid)
{
    ::Character* c = FindSpawned(uid);
    if (Plaus(c)) SayOnCopy(c, kRefusalLine);
}
// The emptied COPY squad the person left (spawn.cpp PrepareContextForSpawn built it on this game): design risk 1.
// review-recruit1 2a: `oldActive` is what getSquad() answers - the ActivePlatoon; the context map and the store bind are
// keyed by the Platoon behind it (Platoon+0x1D8 -> ActivePlatoon), so the Platoon is looked up first. Only a squad the
// map holds is touched, and only a real erase is counted.
void ForgetEmptiedCopySquad(void* oldActive)
{
    if (!Plaus(oldActive) || PlatoonCountPod(oldActive) != 0) return;
    void* platoon = ContextPlatoonForActive(oldActive);
    if (platoon == 0) return;
    ForgetContextPlatoon(platoon);
    StoreUnbindPlatoon(platoon);
    ++g_ctxForgotten;
}

// My first own player-faction character (the hirer), 0 if none.
::Character* MyHirer(unsigned int* uidOut)
{
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int u = 0; ::Character* c = 0;
        if (!MirrorSlot(i, &u, &c) || u == 0 || !net::IsUidMine(u) || !Plaus(c)) continue;
        if (!IsPlayerFaction(FactionOfPod(c)) || !Alive(c)) continue;
        *uidOut = u;
        return c;
    }
    return 0;
}

// Hire on the game that runs the person (design step 2, and the lever's positive control). MAIN THREAD, K2.
void LocalHire(unsigned int uid, ::Character* person, ::Character* hirer, unsigned int hirerUid, int price, int joinType)
{
    std::map<unsigned int, Reserved>::iterator r = g_reserved.find(uid);
    if (r != g_reserved.end())
    {
        ++g_localRefused;
        DebugLog("[HIRE] own hire uid=" + N(uid) + " refused: reserved for the other game's request " + N(r->second.reqId));
        SayRefusal(uid);
        return;
    }
    ::Faction* mine = FactionOfPod(hirer);
    void* own = OwnershipsPod(mine); int money = 0;
    if (own == 0 || !MoneyPod(own, &money) || money < price) { ++g_purseShort; DebugLog("[HIRE] own hire uid=" + N(uid) + " refused: purse " + N(money) + " < price " + N(price)); return; }
    void* pi = PlayerInterfacePod();
    const int rec = pi ? RecruitPod(pi, person, joinType == 3) : -1;
    int paid = 0;
    if (rec == 1) paid = TakeMoneyPod(own, price);
    else ++g_recruitRefused;
    ++g_localHires;
    const bool joined = rec == 1 && IsPlayerFaction(FactionOfPod(person));
    DebugLog("[HIRE] own hire uid=" + N(uid) + " recruit=" + N(rec) + " paid=" + N(paid == 1 ? price : 0) + " joined=" + N(joined ? 1 : 0)
             + " (this game runs the person: vanilla, no ownership moves)");
    if (joined) SendDone(net::kHireNoAsker, 0, uid, hirerUid, 0, 1);   // the other game re-files its copy (G3)
}

// Hirer, design step 3: HOLD - record and ask the owner.
void Hold(unsigned int uid, ::Character* person, unsigned int hirerUid, int price, int joinType)
{
    if (g_pending.find(uid) != g_pending.end()) { DebugLog("[HIRE] hold uid=" + N(uid) + " already pending - not asked twice"); return; }
    Pending p; p.reqId = g_nextReqId++; p.hirerUid = hirerUid; p.price = price; p.joinType = joinType; p.faction = FactionName(FactionOfPod(person));
    p.ownerSlot = net::OwnerSlotOf(uid);
    coophire::HireMsg m; m.kind = coophire::kHireReq; m.reqId = p.reqId; m.uid = uid; m.hirerUid = hirerUid; m.price = price; m.joinType = joinType;
    m.faction = p.faction.size() > coophire::kHireNameMax ? p.faction.substr(0, coophire::kHireNameMax) : p.faction;
    p.faction = m.faction;
    if (!Send(m, net::kHireNoAsker)) return;   // a REQ goes to the person's owner
    ++g_reqOut;
    g_pending[uid] = p;
    DebugLog("[HIRE] hold uid=" + N(uid) + " price=" + N(price) + " type=" + N(joinType) + " req=" + N(p.reqId) + " fac='" + p.faction + "'");
}

// Owner, design step 4: the live check, first request wins.
void OwnerCheck(const coophire::HireMsg& m, unsigned int peer)
{
    ++g_reqIn;
    int reason = coophire::kNoNone;
    ::Character* c = 0;
    if (!net::IsUidMine(m.uid)) reason = coophire::kNoNotMine;
    else if (!Plaus(c = FindSpawned(m.uid))) reason = coophire::kNoGone;
    else if (g_reserved.find(m.uid) != g_reserved.end()) reason = coophire::kNoReserved;
    else
    {
        // The hirer's game already ran the game's own dialogue, which offered the hire; the owner adds no rules of its own and
        // refuses only a person whose faction, cage state or slave state it cannot read.
        ::Faction* f = FactionOfPod(c);
        if (!Plaus(f)) reason = coophire::kNoFaction;
        else if (InSomethingPod(c) < 0) reason = coophire::kNoCaged;
        else if (SlaveStateOfCharacter(c) < 0) reason = coophire::kNoSlave;
    }
    DebugLog("[HIRE] owner check uid=" + N(m.uid) + " free=" + N(reason == 0 ? 1 : 0) + " reason=" + N(reason) + " req=" + N(m.reqId) + " peer=" + N(peer));
    coophire::HireMsg a; a.reqId = m.reqId; a.uid = m.uid; a.hirerUid = m.hirerUid; a.price = m.price; a.joinType = m.joinType;
    if (reason == 0)
    {
        a.kind = coophire::kHireOk;
        a.gen = net::MineGenOf(m.uid);   /* M7a A1 build 1 [a1b1-hi1]: the owner's gen - the hirer holds it + 1 */
        Reserved r; r.reqId = m.reqId; r.hirerUid = m.hirerUid; r.peer = peer; r.slot = net::PlayerSlotOfKey(peer);
        g_reserved[m.uid] = r;
        if (Send(a, peer)) ++g_okOut; else g_reserved.erase(m.uid);
    }
    else
    {
        a.kind = coophire::kHireNo; a.reason = reason;
        if (Send(a, peer)) ++g_noOut[reason];
    }
}

// Hirer, design step 5: take over and hire.
void TakeAndHire(const coophire::HireMsg& m, unsigned int peer)   // peer: the game whose OK this is - every DONE goes back to it
{
    ++g_okIn;
    std::map<unsigned int, Pending>::iterator it = g_pending.find(m.uid);
    if (it == g_pending.end() || it->second.reqId != m.reqId)
    {
        ++g_lateOk;
        DebugLog("[HIRE] late OK uid=" + N(m.uid) + " req=" + N(m.reqId) + " - nothing pending, answered DONE taken=0");
        SendDone(peer, m.reqId, m.uid, m.hirerUid, 0, 0);
        return;
    }
    const Pending p = it->second;
    g_pending.erase(it);
    ::Character* person = FindSpawned(m.uid);
    ::Character* hirer = FindSpawned(p.hirerUid);
    // review-recruit1 3c: the person is re-checked LIVE too - a death or knockout between OK and here is never recruited;
    // DONE {taken 0} clears the owner's promise and the owner keeps the person.
    if (!Alive(person) || !Alive(hirer) || !net::IsUidMine(p.hirerUid) || !IsPlayerFaction(FactionOfPod(hirer)))
    {
        ++g_hirerGone;
        DebugLog("[HIRE] take uid=" + N(m.uid) + " abandoned: person or hirer gone / down / not mine - DONE taken=0");
        SendDone(peer, m.reqId, m.uid, p.hirerUid, 0, 0);
        return;
    }
    void* own = OwnershipsPod(FactionOfPod(hirer)); int money = 0;
    void* pi = PlayerInterfacePod();
    if (own == 0 || !MoneyPod(own, &money) || money < p.price || pi == 0)
    {
        ++g_purseShort;
        DebugLog("[HIRE] take uid=" + N(m.uid) + " abandoned: purse " + N(money) + " < price " + N(p.price) + (pi ? "" : " (no PlayerInterface)") + " - DONE taken=0");
        SendDone(peer, m.reqId, m.uid, p.hirerUid, 0, 0);
        SayRefusal(m.uid);
        return;
    }
    // take only with a road open - the DONE must be able to leave, or nobody runs the person twice over.
    // A DONE lost in transit AFTER the take (the link drops between this line and delivery) is the same class as a squad
    // hand-off's lost ACK: both games briefly claim the person until the peer-gone path drops the other side's copies.
    if (!net::SideRoadOpen())
    {
        ++g_cancelledLinkDown;
        DebugLog("[HIRE] take uid=" + N(m.uid) + " cancelled: no road to the other games - nothing taken");
        return;
    }
    void* oldPlatoon = PlatoonOfPod(person);
    net::TakeLocalOwner(m.uid, net::GenForTake(m.uid, m.gen));   /* M7a A1 build 1 [a1b1-hi2]: above the OK's gen and this game's copy record */
    UnpuppetForOwnership(m.uid);
    ++g_taken;
    const int rec = RecruitPod(pi, person, p.joinType == 3);
    int paid = 0;
    if (rec == 1) { paid = TakeMoneyPod(own, p.price); ForgetEmptiedCopySquad(oldPlatoon); }   // the hirer: the copy squad the mod built
    else ++g_recruitRefused;
    const int joined = (rec == 1 && IsPlayerFaction(FactionOfPod(person))) ? 1 : 0;
    DebugLog("[HIRE] take uid=" + N(m.uid) + " recruit=" + N(rec) + " paid=" + N(paid == 1 ? p.price : 0) + " joined=" + N(joined)
             + " fac='" + FactionName(FactionOfPod(person)) + "' oldPlatoonLeft=" + N(PlatoonCountPod(oldPlatoon)));
    /* [a1b1-hi3] the person moved: the owner releases it on this DONE, and every other game moves its copy into this game's stand-in
       (coophire::HireDoneToEveryGame) */
    SendDone(coophire::HireDoneToEveryGame(1, joined) != 0 ? net::kHireNoAsker : peer, m.reqId, m.uid, p.hirerUid, 1, joined, net::MineGenOf(m.uid));
}

// Hirer, design step 6.
void Refused(const coophire::HireMsg& m)
{
    ++g_noIn;
    std::map<unsigned int, Pending>::iterator it = g_pending.find(m.uid);
    const bool mine = it != g_pending.end() && it->second.reqId == m.reqId;
    if (mine) g_pending.erase(it);
    DebugLog("[HIRE] refused uid=" + N(m.uid) + " reason=" + N(m.reason) + " req=" + N(m.reqId) + (mine ? "" : " (nothing pending)"));
    if (mine) SayRefusal(m.uid);
}

// Owner (and a copy's game for an own-game hire), design step 7.
void Finish(const coophire::HireMsg& m, unsigned int peer)
{
    ++g_doneIn;
    std::map<unsigned int, Reserved>::iterator r = g_reserved.find(m.uid);
    unsigned int hirerUid = m.hirerUid;
    if (r != g_reserved.end()) { if (hirerUid == 0) hirerUid = r->second.hirerUid; g_reserved.erase(r); }
    ::Character* c = FindSpawned(m.uid);
    int factionSet = 0, released = 0;
    if (m.joined == 1)
    {
        ::Character* hirerCopy = hirerUid != 0 ? FindSpawned(hirerUid) : 0;
        /* stand1: the stand-in of the HIRER's slot - the faction the hirer's own copy is in here; when that copy is not found, the
           stand-in of the game that sent this DONE (a joined DONE always comes from the hirer's game) */
        ::Faction* hirerFac = Plaus(hirerCopy) ? FactionOfPod(hirerCopy) : 0;
        const int senderSlot = net::PlayerSlotOfKey(peer);
        ::Faction* peerFac = (Plaus(hirerFac) && IsPeerFaction(hirerFac)) ? hirerFac : (senderSlot >= 0 ? StandInForSlot(senderSlot) : 0);
        void* platoon = Plaus(hirerCopy) ? PlatoonOfPod(hirerCopy) : 0;
        if (!Plaus(peerFac)) ++g_noPeerFaction;
        else if (Plaus(c) && !IsPeerFaction(FactionOfPod(c)))
        {
            // review-recruit1 2a: NO squad record is released here. On the OWNER (taken=1) the old squad is its own saved
            // squad and its notebook entry still lists the person - unbinding it would bring the person back as a copy
            // later. On a copy's game (taken=0, joined=1) the emptied copy squad stays recorded; if the engine frees it,
            // store.cpp's removeSquad detour forgets it (review-p3o H1).
            if (SetFactionPod(c, peerFac, Plaus(platoon) ? platoon : 0))
            {
                factionSet = 1; ++g_factionSet; TagsNoteCopy(m.uid);
                /* no squad given: the stand-in's addMember made a NEW squad, numbered by the stand-in's own counter */
                if (!Plaus(platoon)) CopySquadIdCheckHired(peerFac, coop::SquadActivePlatoonOf(c));
            }
            else ++g_setFactionFault;
        }
        if (m.taken == 0) ++g_joinedApplied;
    }
    if (m.taken == 1 && net::IsUidMine(m.uid))
    {
        net::ReleaseLocalOwner(m.uid, peer, m.gen);   /* M7a A1 build 1 [a1b1-hi4]: the gen the hirer holds (0 = this game's + 1) */
        WorldStateOnOwnershipReleased(m.uid, c);
        AdoptRemotePuppet(m.uid);
        released = 1; ++g_released;
    }
    DebugLog("[HIRE] finish uid=" + N(m.uid) + " taken=" + N(m.taken) + " joined=" + N(m.joined) + " released=" + N(released)
             + " factionSet=" + N(factionSet) + " faction=" + (Plaus(c) ? FactionName(FactionOfPod(c)) : std::string("?")));
    const std::string q(1, (char)34);
    SW_DEVLOG("[VERDICT] {" + q + "ev" + q + ":" + q + "hire_done" + q + "," + q + "uid" + q + ":" + N(m.uid) + "," + q + "taken" + q + ":" + N(m.taken)
             + "," + q + "joined" + q + ":" + N(m.joined) + "," + q + "released" + q + ":" + N(released) + "," + q + "factionSet" + q + ":" + N(factionSet) + "}");
}

void RunTestLever()
{
    ++g_testRuns;
    unsigned int hirerUid = 0;
    ::Character* hirer = MyHirer(&hirerUid);
    if (hirer == 0) { ++g_testNoHirer; DebugLog("[HIRE] hiretest skipped: no own living player-faction character"); return; }
    unsigned int uid = 0; ::Character* person = 0;
    std::string seen; int seenN = 0;   /* T349: on a miss, name the factions that WERE near, so the match string can be checked */
    if (g_testMode == 2)
    {
        uid = g_testUid; person = FindSpawned(uid);
        if (!Plaus(person)) person = 0;
    }
    else
    {
        Ogre::Vector3 hp;
        if (g_testMode == 3) { hp.x = g_testX; hp.y = 0.0f; hp.z = g_testZ; }   /* near same: one fixed map point, so both games pick one person */
        else if (!SafeReadPosition(hirer, &hp)) { ++g_testNoPerson; DebugLog("[HIRE] hiretest skipped: the hirer's position is unreadable"); return; }
        float best = kNearMax;
        const int cap = MirrorCapacity();
        for (int i = 0; i < cap; ++i)
        {
            unsigned int u = 0; ::Character* c = 0; Ogre::Vector3 q;
            if (!MirrorSlot(i, &u, &c) || u == 0 || !Plaus(c)) continue;
            const std::string fn = FactionName(FactionOfPod(c));
            if (fn.find("Drifter") == std::string::npos)
            {
                if (seenN < 8 && seen.find("'" + fn + "'") == std::string::npos)
                {
                    Ogre::Vector3 q2;
                    if (SafeReadPosition(c, &q2) && (q2.x - hp.x) * (q2.x - hp.x) + (q2.z - hp.z) * (q2.z - hp.z) < kNearMax * kNearMax) { seen += " '" + fn + "'"; ++seenN; }
                }
                continue;
            }
            if (!Alive(c) || !SafeReadPosition(c, &q)) continue;
            const float dx = q.x - hp.x, dz = q.z - hp.z, d = sqrtf(dx * dx + dz * dz);
            if (d < best || (d == best && person != 0 && u < uid)) { best = d; uid = u; person = c; }   /* a tie goes to the lower uid on both games */
        }
    }
    if (person == 0) { ++g_testNoPerson; DebugLog("[HIRE] hiretest skipped: no person (" + std::string(g_testMode == 2 ? "uid " + N(g_testUid) + " not here" : "no Drifter within 3000 u; factions near:" + (seen.empty() ? std::string(" none") : seen)) + ")"); return; }
    DebugLog("[HIRE] hiretest uid=" + N(uid) + " mode=" + N(g_testMode) + " hirer=" + N(hirerUid) + " price=" + N(g_testPrice) + " mine=" + N(net::IsUidMine(uid) ? 1 : 0)
             + " fac='" + FactionName(FactionOfPod(person)) + "' join=" + N(g_testJoinType));
    if (net::IsUidMine(uid)) LocalHire(uid, person, hirer, hirerUid, g_testPrice, g_testJoinType);
    else Hold(uid, person, hirerUid, g_testPrice, g_testJoinType);
}

// ---- the 0x67FAD0 detour: R0-b HOLD (docs/design-recruit1.md 1 step 3) ----
// A reply line of the player's own conversation on THIS game that carries a hire action (3 JOIN_SQUAD_WITH_EDIT / 18
// JOIN_SQUAD_FAST) on a person the OTHER game runs is NOT run: no action of the line runs (8 TAKE_MONEY included, so no
// money moves and nothing needs a refund; any other action is counted otherActs by type and not run either). The chat is
// closed with the engine's own endDialogue 0x673DA0(dlg, 1) - the call _doActions itself makes, then returns, when recruit
// refuses (Read 67fad0:1225-1229) - and Hold() starts the same REQ the lever starts, at the price the line's action 8
// carries (the engine hands that value to Ownerships::takeMoney, Confirmed bytes 0x682865). OK -> TakeAndHire, exactly
// the lever's take + recruit + pay. A person this game runs, a line with no 3/18, an NPC target, a person with no uid:
// the engine runs the line unchanged. Returns true when the line was held (the original must NOT run). MAIN THREAD.
bool HoldLine(void* dlg, void* line)
{
    int types[16], values[16], total = 0; ::Character* person = 0; ::Character* target = 0;
    const int k = ReadLinePod(dlg, line, types, values, 16, &total, &person, &target);
    if (k < 0) { ++g_linesUnread; return false; }
    int joinType = 0;
    for (int i = 0; i < k; ++i) if (joinType == 0 && (types[i] == 3 || types[i] == 18)) joinType = types[i];
    if (joinType == 0) return false;
    ++g_linesSeen;
    if (!Plaus(target) || !IsPlayerFaction(FactionOfPod(target))) return false;   // an NPC-to-NPC hire
    ++g_linesPlayer;
    std::string acts, others;
    int price = 0, nOther = 0;
    for (int i = 0; i < k; ++i)
    {
        acts += (i ? "," : "") + N(types[i]) + ":" + N(values[i]);
        if (types[i] == 8) price += values[i];
        else if (types[i] != 3 && types[i] != 18) { others += (nOther ? "," : "") + N(types[i]); ++nOther; }
    }
    const unsigned int uid = Plaus(person) ? FindSpawnedUid(person) : 0;
    const unsigned int hirerUid = FindSpawnedUid(target);
    const bool mine = uid != 0 && net::IsUidMine(uid);
    /* review-recruit2 3: the OWNER's own dialogue hire of a person RESERVED for the other game's pending request is
       refused (the lever's own-hire path already checks this) - otherwise both games hire the same person. */
    if (mine && kEndDialogueRva != 0 && g_reserved.find(uid) != g_reserved.end())
    {
        ++g_localRefused;
        DebugLog("[HIRE] own hire refused uid=" + N(uid) + " - reserved for the other game's pending request");
        if (EndDialoguePod(dlg) != 1) { ++g_endFault; ErrorLog("[HIRE] endDialogue 0x673DA0 faulted on a refused own hire uid=" + N(uid)); }
        SayRefusal(uid);
        return true;
    }
    std::string vanilla;
    if (uid == 0) { ++g_linesNoUid; vanilla = "the person has no uid"; }
    else if (mine) { ++g_linesOwn; vanilla = "this game runs the person"; }
    else if (hirerUid == 0 || !net::IsUidMine(hirerUid)) { ++g_linesNoHirer; vanilla = "the player's character has no own uid"; }
    else if (kEndDialogueRva == 0 || kRecruitRva == 0 || kPlayerIfaceRva == 0) { ++g_linesNoAddr; vanilla = "endDialogue / recruit / PlayerInterface not in the address table"; }
    if (!vanilla.empty())
    {
        if (g_linesLogged < kLogCap)
        {
            ++g_linesLogged;
            DebugLog("[HIRE] line person=" + N(uid) + " mine=" + N(mine ? 1 : 0) + " target=" + N(hirerUid) + " acts=[" + acts + "] total=" + N(total)
                     + " - the engine runs the line unchanged (" + vanilla + ")");
        }
        return false;
    }
    ++g_linesHeld;
    g_otherActs += nOther;
    for (int i = 0; i < k; ++i) if (types[i] != 8 && types[i] != 3 && types[i] != 18) ++g_otherActTypes[types[i]];
    DebugLog("[HIRE] line person=" + N(uid) + " mine=0 target=" + N(hirerUid) + " acts=[" + acts + "] total=" + N(total)
             + " HELD - not run: price=" + N(price) + " type=" + N(joinType) + " otherActs=[" + others + "] (not run either); chat closed by endDialogue");
    if (EndDialoguePod(dlg) != 1) { ++g_endFault; ErrorLog("[HIRE] endDialogue 0x673DA0 faulted on a held line uid=" + N(uid)); }
    Hold(uid, person, hirerUid, price, joinType);
    if (g_pending.find(uid) == g_pending.end()) SayRefusal(uid);   // not asked (link down): nobody is hired, nothing is taken
    return true;
}

void detour_doActions(void* dlg, void* line, void* a3, void* a4)
{
    const unsigned long mt = StoreMainThreadId();
    if (mt != 0 && ::GetCurrentThreadId() == mt)
    {
        if (TalkSkipDoActions(dlg)) return;   /* P26 stages 1-3: the mirrored NPC line's actions already ran on the NPC's game */
        if (TalkDeferTargetActs(dlg, line)) return;   /* P26 stage 5: the router - a forwarded conversation's line: NPC-side here, target-side as ACT */
        if (HoldLine(dlg, line)) return;
    }
    else
    {
        ::InterlockedIncrement64(&g_linesOffMain);
        TalkNoteOffMainActs(dlg, line);   /* log-only: a window-opening action off the main thread */
    }
    void* const s6Target = TalkS6LeaderTarget(dlg, line);   /* P26s6 S6-3: resolved before the line runs (it may end the conversation) */
    g_doActionsOrig(dlg, line, a3, a4);
    if (s6Target != 0) TalkS6LeaderOrder(dlg, s6Target);   /* P26s6 S6-3: the walk-over the engine gives a player target */
}

} // namespace

int HireDoActionsHookState() { return g_doActionsHook; }   /* P26s1 fold 1 M3 (declared in speech.h, namespace coop) */

/* P26 stage 5 (speech.cpp TkRunViewPod, MAIN THREAD): the engine's own Dialogue::_doActions through the hook's trampoline - this
   file's detour (TalkSkipDoActions / TalkDeferTargetActs / HoldLine) is not re-entered. a3 is not read and a4 is written before it
   is read (Read 67fad0:4, 180-181), so both go as 0. 1 ran, 0 no trampoline, -1 the engine faulted. */
int HireCallDoActions(void* dlg, void* line)
{
    if (g_doActionsHook != 1 || g_doActionsOrig == 0) return 0;
    __try { g_doActionsOrig(dlg, line, 0, 0); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* P26 stage 5 (speech.cpp TkApplyAct, MAIN THREAD): the OTHER game's NPC, in its conversation with this game's character, hires
   itself into this game's player's squad (DialogActionEnum 3 / 18). The SAME road a real hire reply here takes for a person the
   other game runs: HoldLine -> Hold (REQ to the NPC's owner, pending[uid]); its OK -> TakeAndHire (the price off this game's purse
   + PlayerInterface::recruit + DONE), its refusal -> Refused. 1 asked, 0 not asked (*why says why). */
int HireForTalk(unsigned int npcUid, unsigned int hirerUid, int price, int joinType, std::string* why)
{
    ::Character* person = npcUid != 0 ? FindSpawned(npcUid) : 0;
    if (!Plaus(person)) { *why = "no copy of the NPC here"; return 0; }
    if (net::IsUidMine(npcUid)) { *why = "this game runs the NPC"; return 0; }
    if (hirerUid == 0 || !net::IsUidMine(hirerUid)) { *why = "the addressed character is not this game's"; return 0; }
    if (kEndDialogueRva == 0 || kRecruitRva == 0 || kPlayerIfaceRva == 0) { *why = "endDialogue / recruit / PlayerInterface not in the address table"; return 0; }
    if (g_pending.find(npcUid) != g_pending.end()) { *why = "a hire of that NPC is already pending"; return 0; }
    DebugLog("[HIRE] talk hire uid=" + N(npcUid) + " hirer=" + N(hirerUid) + " price=" + N(price) + " type=" + N(joinType)
             + " - the other game's NPC joins this game's squad through the owner's REQ (P26 stage 5)");
    Hold(npcUid, person, hirerUid, price, joinType);
    if (g_pending.find(npcUid) == g_pending.end()) { *why = "the request did not go out (link down)"; return 0; }
    return 1;
}

void InstallHire()
{
    if (kDoActionsRva == 0) g_doActionsHook = -2;
    else g_doActionsHook = (coop::AddHook((void*)(Base() + (uintptr_t)kDoActionsRva), (void*)&detour_doActions, (void**)&g_doActionsOrig) == coop::SUCCESS) ? 1 : -1;
    const std::string line = "[HIRE] hooks: Dialogue::_doActions 0x67FAD0=" + N(g_doActionsHook) + " (R0-b hold; 1 armed, -1 AddHook failed, -2 no table address)"
        + " recruit 0x693010=" + N(kRecruitRva != 0 ? 1 : 0) + " PlayerInterface=" + N(kPlayerIfaceRva != 0 ? 1 : 0) + " endDialogue 0x673DA0=" + N(kEndDialogueRva != 0 ? 1 : 0);
    if (g_doActionsHook == 1 && kRecruitRva != 0 && kPlayerIfaceRva != 0 && kEndDialogueRva != 0) DebugLog(line); else ErrorLog(line);
}

void HireNoteRecv(const coophire::HireMsg& m, unsigned int fromPeer)
{
    Inbound in; in.m = m; in.peer = fromPeer;
    g_inbound.push_back(in);
}

void HireNoteBad() { ++g_bad; }

/* EDITOR STATE (TEST-ONLY, read-only): the character editor that a hire with editing opens (dialogue act 3 -> recruit(edit)
   -> showCharacterEditor 0x6E2FA0 on the GUI object, mode 1). Read at the safe point; nothing is written to the game.
   GUI +0x1C0: the editor window - 0x6E2FA0 deletes the old one and stores the new CharacterEditWindow there (decomp :10-20;
     0x7FED10 passes the GUI object itself, :218); the GUI update 0x6E98D0 runs the editor's update while it is set (:45).
   window +0x1C0 byte: 1 when the window was made in mode 1 (ctor 0x5F4340 :175 writes mode == 1; the mode itself is not kept).
   window +0x8: the layout's main widget - the ctor hides it (:345, vt+0x18 with 0), 0x5EF930 shows it (:646, vt+0x18 with 1)
     right before it pauses the game (:648) and shows the tab widgets (:662-667).
   window +0x260 / +0x268: the recruits (pointer array / count; the ctor fills them from the hire list, :317-342).
   window +0x278: the recruit on show - set by 0x5EF2D0 (:46), which 0x5EF930 calls (:647); the update 0x5F0C10 runs the
     body wait and then 0x5EF930 only while it is 0 (:23-45).
   a recruit's body: character +0x448 -> +0xE8; the update waits while its entity (+0xD8) or its mesh is missing, unless no
     entity is there, none is pending (+0xE0) and the done byte (+0xE8) is set (0x5F0C10 :27-41). */
static unsigned long long kEdGuiRva = 0; static AddrReg kEdGuiRva_reg("ForgottenGui", &kEdGuiRva);   /* Steam_1.0.65 0x2132750: the GUI object itself */
const size_t kGuiEditorWin = 0x1C0, kEdModeOne = 0x1C0, kEdMainWidget = 0x8, kEdRecruits = 0x260, kEdRecruitCount = 0x268, kEdOnShow = 0x278;
const size_t kChrAnim = 0x448, kAnimBody = 0xE8, kBodyEntity = 0xD8, kBodyPending = 0xE0, kBodyDone = 0xE8;
const unsigned long long kEdRecruitCap = 64;   /* a larger count is not read as recruits */

struct EdSnap { void* win; int modeOne; int shown; long long recruits; int ready; void* onShow; int fault; };

/* MAIN THREAD. Guarded reads (and MyGUI's getVisible on the main widget); no C++ object in this frame. -1 = not read. */
static void EdReadPod(EdSnap* s)
{
    s->win = 0; s->modeOne = -1; s->shown = -1; s->recruits = -1; s->ready = -1; s->onShow = 0; s->fault = 0;
    if (kEdGuiRva == 0) return;
    __try
    {
        const char* const gui = (const char*)(Base() + (uintptr_t)kEdGuiRva);
        const char* const w = *(char* const*)(gui + kGuiEditorWin);
        s->win = (void*)w;
        if (w == 0) return;
        s->modeOne = *(const unsigned char*)(w + kEdModeOne);
        s->onShow = *(void* const*)(w + kEdOnShow);
        MyGUI::Widget* const mw = *(MyGUI::Widget* const*)(w + kEdMainWidget);
        if (mw != 0) s->shown = mw->getVisible() ? 1 : 0;
        const unsigned long long n = *(const unsigned long long*)(w + kEdRecruitCount);
        s->recruits = (long long)n;
        if (n > kEdRecruitCap) return;
        const char* const arr = *(char* const*)(w + kEdRecruits);
        int ready = 0;
        for (unsigned long long i = 0; i < n; ++i)
        {
            const char* const c = *(char* const*)(arr + i * 8);
            const char* const anim = (c != 0) ? *(char* const*)(c + kChrAnim) : 0;
            const char* const body = (anim != 0) ? *(char* const*)(anim + kAnimBody) : 0;
            if (body == 0) continue;
            ready += coopeditor::EditorBodyReady(*(void* const*)(body + kBodyEntity) != 0 ? 1 : 0, *(void* const*)(body + kBodyPending) != 0 ? 1 : 0,
                                                 *(const unsigned char*)(body + kBodyDone));
        }
        s->ready = ready;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { s->fault = 1; }
}

static long g_edArmed = 0;
static int g_edWatchLeft = 0;      /* watched lines still to write */
static DWORD g_edNextMs = 0;

static std::string EdNum(long long v) { if (v < 0) return "?"; char b[32]; std::sprintf(b, "%lld", v); return b; }

static void EdLine(const char* why)
{
    if (EngineWritesBlocked()) { DebugLog(std::string("[EDITOR] state (") + why + ") not read: the world is loading or tearing down"); return; }
    EdSnap s;
    EdReadPod(&s);
    bool paused = false; float mul = 0.0f;
    const bool ps = PauseSnapshot(&paused, &mul);
    char b[320];
    std::sprintf(b, "[EDITOR] state window=%p mode=%s widgetsShown=%s recruits=%s bodiesReady=%s paused=%s speed=",
                 s.win, s.modeOne < 0 ? "?" : (s.modeOne == 1 ? "1" : "not1"), s.shown < 0 ? "?" : (s.shown ? "1" : "0"),
                 EdNum(s.recruits).c_str(), EdNum(s.ready).c_str(), ps ? (paused ? "1" : "0") : "?");
    char t[160];
    if (ps && _finite(mul)) std::sprintf(t, "%.2f", mul); else std::sprintf(t, "?");
    char u[200];
    std::sprintf(u, " onShow=%p%s (%s%s)", s.onShow, s.fault ? " readFault=1" : "", why, kEdGuiRva == 0 ? "; no ForgottenGui row" : "");
    DebugLog(std::string(b) + t + u);
}

std::string EditorStateArm(const std::string& arg)
{
    int secs = 0;
    if (!coopeditor::EditorStateParse(arg, &secs)) return "error editorstate usage: editorstate | editorstate watch <1..120>";
    if (secs > 0) { g_edWatchLeft = secs; g_edNextMs = ::GetTickCount(); DebugLog("[EDITOR] state watch armed: a line every second for " + EdNum(secs) + " s"); }
    else ::InterlockedExchange(&g_edArmed, 1);
    return secs > 0 ? "ok editorstate watch armed" : "ok editorstate armed";
}

void EditorStateTick()
{
    if (::InterlockedExchange(&g_edArmed, 0) != 0) EdLine("once");
    if (g_edWatchLeft <= 0) return;
    const DWORD now = ::GetTickCount();
    if ((long)(now - g_edNextMs) < 0) return;
    g_edNextMs = now + 1000;
    --g_edWatchLeft;
    EdLine(g_edWatchLeft > 0 ? "watch" : "watch, last");
}

/* RECRUIT BODIES WHILE PAUSED. The editor's setup 0x5F4340 reloads each recruit: 0x52C760 drops the body (appearance entity
   +0xD8 and pending +0xE0 cleared, rebuild flag +0x143 set) and the editor's update 0x5F0C10 waits for the new body. The only
   rebuild is the appearance update (AppearanceBase vt+0x8, 0x5382A0: vt+0x58 createBody while +0x143 is set), which the
   animation update (AnimationClass vt+0x30: 0x5B5E30, AnimationClassHuman 0x5C4CC0) runs while anim +0x88 or +0x89 is set.
   The engine's paused-frame update (Character vt+0x270: 1.0.65 0x5C7090, 1.0.68 0x5C7B20) makes that call at zero time,
   anim->vt+0x30(anim, 0.0f), for every character except one whose faction's +0x250 is set while the editor window (GUI +0x1C0)
   is open - and the hire has already moved the recruit into the player's faction, so its body is never rebuilt while paused.
   The talk window's closing unpause stays blocked in a session (store.cpp), so instead, while the game is paused and the hire
   editor still waits (mode 1, no recruit on show), this makes that same call once per frame for each recruit the engine skips
   whose body is not ready, here at the safe point (main thread, worker paused). It stops when the body is ready, the editor
   shows a recruit or closes, the game unpauses, or after kBodyStepGiveUpFrames frames of calls for one window. Nothing moves,
   nothing is unpaused, nothing is sent to the other games. The call goes through the recruit's own animation object's vtable,
   as the engine's does, so it needs no table row. */
const size_t kFacEditorSkip  = 0x250;   /* Faction: non-zero = the paused-frame update skips its members while the editor is open */
const size_t kAnimUpdateSlot = 0x30;    /* AnimationClass vt+0x30: the animation update; the time is the second argument */
typedef void (*AnimUpdateFn)(void* anim, float dt);

struct BsFrame
{
    void* win; int modeOne; int onShowSet; int windowDue; int n; int fault;
    void* chars[kEdRecruitCap]; unsigned char ready[kEdRecruitCap]; unsigned char stepped[kEdRecruitCap];
    unsigned char animFlags[kEdRecruitCap];   /* bit 0: anim +0x88 set, bit 1: anim +0x89 set (the animation update reaches the body update only with one of them); 0xFF unread */
};

/* An engine object pointer: inside the user address range and 8-aligned. */
static bool BsObj(const void* p) { return Plaus(p) && ((uintptr_t)p & 7) == 0; }

/* MAIN THREAD, worker paused. Guarded reads of the editor and its recruits and, when the window may be stepped (paused, mode 1,
   no recruit on show, under the frame limit - framesForWin counts for lastWin only), the animation update call for each
   recruit that needs it. No C++ object in this frame. */
static void BsPod(int paused, void* lastWin, int framesForWin, BsFrame* f)
{
    f->win = 0; f->modeOne = 0; f->onShowSet = 0; f->windowDue = 0; f->n = 0; f->fault = 0;
    __try
    {
        const char* const gui = (const char*)(Base() + (uintptr_t)kEdGuiRva);
        const char* const w = *(char* const*)(gui + kGuiEditorWin);
        if (!BsObj(w)) return;
        f->win = (void*)w;
        f->modeOne = (*(const unsigned char*)(w + kEdModeOne) == 1) ? 1 : 0;
        f->onShowSet = (*(void* const*)(w + kEdOnShow) != 0) ? 1 : 0;
        f->windowDue = coopeditor::EditorBodyStepDue(paused, f->modeOne, f->onShowSet, (w == (const char*)lastWin) ? framesForWin : 0);
        const unsigned long long n = *(const unsigned long long*)(w + kEdRecruitCount);
        if (n > kEdRecruitCap) return;
        const char* const arr = *(char* const*)(w + kEdRecruits);
        if (n != 0 && !BsObj(arr)) return;
        f->n = (int)n;
        for (int i = 0; i < f->n; ++i)
        {
            char* const c = *(char* const*)(arr + (size_t)i * 8);
            f->chars[i] = c; f->ready[i] = 0; f->stepped[i] = 0; f->animFlags[i] = 0xFF;
            if (!BsObj(c)) continue;
            char* const anim = *(char* const*)(c + kChrAnim);
            if (!BsObj(anim)) continue;
            f->animFlags[i] = (unsigned char)(((*(const unsigned char*)(anim + 0x88) != 0) ? 1 : 0) | ((*(const unsigned char*)(anim + 0x89) != 0) ? 2 : 0));
            const char* const body = *(char* const*)(anim + kAnimBody);
            const int bodyHere = BsObj(body) ? 1 : 0;
            if (bodyHere != 0)
                f->ready[i] = (unsigned char)coopeditor::EditorBodyReady(*(void* const*)(body + kBodyEntity) != 0 ? 1 : 0,
                                                                         *(void* const*)(body + kBodyPending) != 0 ? 1 : 0,
                                                                         *(const unsigned char*)(body + kBodyDone));
            if (f->windowDue == 0) continue;
            const char* const fac = *(char* const*)(c + kCharFactionOff);
            const int skips = (BsObj(fac) && *(void* const*)(fac + kFacEditorSkip) != 0) ? 1 : 0;
            if (coopeditor::EditorRecruitStepDue(skips, bodyHere, f->ready[i]) == 0) continue;
            void* const* const vt = *(void* const* const*)anim;
            if (!BsObj(vt)) continue;
            const AnimUpdateFn fn = (AnimUpdateFn)vt[kAnimUpdateSlot / sizeof(void*)];
            if (!Plaus((const void*)fn)) continue;
            fn(anim, 0.0f);   /* the call 0x5C7090 makes: rcx = the animation object, xmm1 = 0.0f */
            f->stepped[i] = 1;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { f->fault = 1; }
}

static void* g_bsWin = 0;                     /* the editor window the counts below belong to */
static int g_bsFrames = 0;                    /* frames with at least one call, for g_bsWin */
static int g_bsGaveUpSaid = 0;
static void* g_bsChar[kEdRecruitCap];         /* per recruit slot: the character ... */
static int g_bsCharFrames[kEdRecruitCap];     /* ... and its frames of calls (-1 = its ready line was written) */
/* " anim88=<0/1> anim89=<0/1>" for a recruit's animation flags, or " anim88/89=unread". */
static std::string BsFlagsText(unsigned char fl)
{
    if (fl == 0xFF) return " anim88/89=unread";
    return std::string(" anim88=") + ((fl & 1) ? "1" : "0") + " anim89=" + ((fl & 2) ? "1" : "0");
}
static long long g_bsSteps = 0, g_bsCalls = 0, g_bsRebuilt = 0, g_bsGiveUps = 0, g_bsFaults = 0;

/* MAIN THREAD, worker paused (HireSafePointDrain), every frame. */
static void EditorBodyStep()
{
    if (kEdGuiRva == 0 || EngineWritesBlocked()) return;
    bool paused = false; float mul = 0.0f;
    const int p = (PauseSnapshot(&paused, &mul) && paused) ? 1 : 0;
    BsFrame f;
    BsPod(p, g_bsWin, g_bsFrames, &f);
    if (f.win != g_bsWin)
    {
        g_bsWin = f.win; g_bsFrames = 0; g_bsGaveUpSaid = 0;
        for (unsigned long long i = 0; i < kEdRecruitCap; ++i) { g_bsChar[i] = 0; g_bsCharFrames[i] = 0; }
    }
    if (f.fault != 0)
    {
        ++g_bsFaults;
        if (f.win != 0 && g_bsFrames < coopeditor::kBodyStepGiveUpFrames)
        {
            g_bsFrames = coopeditor::kBodyStepGiveUpFrames; g_bsGaveUpSaid = 1;   /* no more calls for this window */
            ErrorLog("[HIRE] editor body step faulted - no more animation calls for this editor window");
        }
        return;
    }
    if (f.win == 0) return;
    int any = 0, notReady = 0, firstNotReady = -1;
    for (int i = 0; i < f.n; ++i)
    {
        if (g_bsChar[i] != f.chars[i])
        {   /* another recruit in this slot: a new editor at the same window address starts its own frame count */
            if (g_bsChar[i] != 0) { g_bsFrames = 0; g_bsGaveUpSaid = 0; }
            g_bsChar[i] = f.chars[i]; g_bsCharFrames[i] = 0;
        }
        if (f.stepped[i] != 0)
        {
            any = 1; ++g_bsCalls;
            g_bsCharFrames[i] = (g_bsCharFrames[i] < 0 ? 0 : g_bsCharFrames[i]) + 1;
        }
        if (f.ready[i] == 0) { if (firstNotReady < 0) firstNotReady = i; ++notReady; continue; }
        if (g_bsCharFrames[i] > 0)   /* stepped before and ready now: said once */
        {
            ++g_bsRebuilt;
            std::string name;
            if (!NameOf(f.chars[i], &name) || name.empty()) name = "?";
            DebugLog(std::string(p != 0 ? "[HIRE] editor body rebuilt while paused: " : "[HIRE] editor body ready after the unpause: ")
                     + name + " after " + N(g_bsCharFrames[i]) + " frames" + BsFlagsText(f.animFlags[i]));
            g_bsCharFrames[i] = -1;
        }
    }
    if (any != 0) { ++g_bsFrames; ++g_bsSteps; }
    if (g_bsGaveUpSaid == 0 && coopeditor::EditorBodyGiveUpDue(p, f.modeOne, f.onShowSet, g_bsFrames, notReady) != 0)
    {
        g_bsGaveUpSaid = 1; ++g_bsGiveUps;
        ErrorLog("[HIRE] editor body rebuild while paused gave up: " + N(notReady) + " of " + N(f.n) + " recruits still without a ready body after "
                 + N(g_bsFrames) + " frames of animation calls - the editor still waits" + (firstNotReady >= 0 ? BsFlagsText(f.animFlags[firstNotReady]) : std::string()));
    }
}

void HireSafePointDrain()
{
    EditorStateTick();
    EditorBodyStep();   /* every frame, before the held-while-loading return below: it checks the world itself */
    if ((g_testArmed == 0 && g_inbound.empty()) || EngineWritesBlocked()) return;   // held while loading: runs every frame
    if (g_testArmed != 0 && (kRecruitRva == 0 || kPlayerIfaceRva == 0)) { g_testArmed = 0; ++g_testNoHirer; DebugLog("[HIRE] hiretest skipped: recruit / PlayerInterface not in the address table"); }
    if (g_testArmed != 0) { g_testArmed = 0; RunTestLever(); }
    std::vector<Inbound> batch;
    batch.swap(g_inbound);
    for (size_t i = 0; i < batch.size(); ++i)
    {
        const coophire::HireMsg& m = batch[i].m;
        switch (m.kind)
        {
        case coophire::kHireReq:  OwnerCheck(m, batch[i].peer); break;
        case coophire::kHireOk:   TakeAndHire(m, batch[i].peer); break;
        case coophire::kHireNo:   Refused(m); break;
        case coophire::kHireDone: Finish(m, batch[i].peer); break;
        default: ++g_bad; break;
        }
    }
}

void HireForgetPeer(int slot)
{
    const bool say = !EngineWritesBlocked();
    for (std::map<unsigned int, Pending>::iterator it = g_pending.begin(); it != g_pending.end(); )   // my requests to that player: no answer will come
    {
        if (!cooppg::PlayerGoneTakesRow(it->second.ownerSlot, slot)) { ++it; continue; }
        ++g_cancelledLinkDown;
        DebugLog("[HIRE] cancelled uid=" + N(it->first) + " req=" + N(it->second.reqId) + " (the person's owner, slot " + N(it->second.ownerSlot) + ", left)");
        if (say) SayRefusal(it->first);
        g_pending.erase(it++);
    }
    for (std::map<unsigned int, Reserved>::iterator it = g_reserved.begin(); it != g_reserved.end(); )   // my promises to that player: released
    {
        if (!cooppg::PlayerGoneTakesRow(it->second.slot, slot)) { ++it; continue; }
        DebugLog("[HIRE] promise released uid=" + N(it->first) + " req=" + N(it->second.reqId) + " (the asker, slot " + N(it->second.slot) + ", left) - the person may be hired again");
        g_reserved.erase(it++);
    }
    for (size_t i = 0; i < g_inbound.size(); )   // its messages not drained yet: an OK, NO or DONE from a player who left is never acted on
    {
        if (cooppg::PlayerGoneTakesRow(net::PlayerSlotOfKey(g_inbound[i].peer), slot)) g_inbound.erase(g_inbound.begin() + (std::ptrdiff_t)i);
        else ++i;
    }
}

std::string HireTestArm(const std::string& arg)
{
    std::istringstream is(arg);
    std::string mode; is >> mode;
    /* a leading 'editor': the hire opens the character editor - recruit(edit = true), the call the dialogue's join-with-edit
       reply makes - instead of join type 18 */
    const bool editor = mode == "editor";
    if (editor) { mode.clear(); is >> mode; }
    const std::string usage = "error hiretest: usage hiretest [editor] near [price] | [editor] near same [<x> <z> [price]] | [editor] uid <n> [price]";
    std::vector<std::string> t; std::string w;
    while (is >> w) t.push_back(w);
    unsigned int uid = 0; int price = kDefaultPrice; bool same = false; float sx = kSameX, sz = kSameZ;
    size_t ip = 0;   /* index of the optional price token */
    if (mode == "uid") { if (t.empty() || (uid = (unsigned int)strtoul(t[0].c_str(), 0, 10)) == 0) return usage; ip = 1; }
    else if (mode != "near") return usage;
    else if (!t.empty() && t[0] == "same")
    {
        /* near same: the Drifter nearest to ONE fixed map point (default The Hub -50978,2932), so both games contend for one person */
        same = true; ip = 1;
        if (t.size() == 2) return usage;
        if (t.size() >= 3) { sx = (float)atof(t[1].c_str()); sz = (float)atof(t[2].c_str()); ip = 3; }
    }
    if (t.size() > ip + 1) return usage;
    if (t.size() == ip + 1)
    {
        char* e = 0; const long p = strtol(t[ip].c_str(), &e, 10);
        if (e == t[ip].c_str() || *e != 0 || p < 0 || p > 1000000) return "error hiretest: price 0..1000000";
        price = (int)p;
    }
    g_testMode = mode == "uid" ? 2 : (same ? 3 : 1); g_testUid = uid; g_testPrice = price; g_testX = sx; g_testZ = sz; g_testJoinType = editor ? 3 : 18; g_testArmed = 1;
    DebugLog("[HIRE] hiretest " + std::string(editor ? "editor " : "") + mode + (uid ? " " + N(uid) : std::string()) + (same ? " same point=(" + N((long long)sx) + "," + N((long long)sz) + ")" : std::string())
             + " price=" + N(price) + " join=" + N(g_testJoinType) + " ARMED - runs at the next safe point");
    return "ok hiretest armed";
}

void OwnSquadUids(std::vector<unsigned int>* out)
{
    out->clear();
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int u = 0; ::Character* c = 0;
        if (!MirrorSlot(i, &u, &c) || u == 0 || !net::IsUidMine(u) || !Plaus(c)) continue;
        if (IsPlayerFaction(FactionOfPod(c))) out->push_back(u);
    }
}

std::string OtherActTypes()   // "type:count,..." of the held lines' other actions
{
    std::string s;
    for (std::map<int, long long>::const_iterator it = g_otherActTypes.begin(); it != g_otherActTypes.end(); ++it)
        s += (s.empty() ? "" : ",") + N(it->first) + ":" + N(it->second);
    return s;
}

void ReportHire()
{
    std::string no;
    for (int r = 1; r <= coophire::kNoReasonMax; ++r) no += (r > 1 ? "/" : "") + N(g_noOut[r]);
    DebugLog("[HIRE] REPORT reqOut=" + N(g_reqOut) + " reqIn=" + N(g_reqIn) + " okOut=" + N(g_okOut) + " okIn=" + N(g_okIn)
             + " noOut=" + no + " noIn=" + N(g_noIn) + " doneOut=" + N(g_doneOut) + " doneIn=" + N(g_doneIn) + " taken=" + N(g_taken)
             + " recruitRefused=" + N(g_recruitRefused) + " purseShort=" + N(g_purseShort) + " released=" + N(g_released)
             + " factionSet=" + N(g_factionSet) + " joinedApplied=" + N(g_joinedApplied) + " lateOk=" + N(g_lateOk)
             + " cancelledLinkDown=" + N(g_cancelledLinkDown) + " otherActs=" + N(g_otherActs) + " otherActTypes=[" + OtherActTypes() + "]"
             + " pendingNow=" + N((long long)g_pending.size()) + " reservedNow=" + N((long long)g_reserved.size())
             + " | localHires=" + N(g_localHires) + " localRefused=" + N(g_localRefused) + " hirerGone=" + N(g_hirerGone)
             + " ctxForgotten=" + N(g_ctxForgotten) + " noPeerFaction=" + N(g_noPeerFaction) + " setFactionFault=" + N(g_setFactionFault)
             + " sendFailed=" + N(g_sendFailed) + " bad=" + N(g_bad) + " tests=" + N(g_testRuns) + " testNoPerson=" + N(g_testNoPerson)
             + " testNoHirer=" + N(g_testNoHirer) + " | lines=" + N(g_linesSeen) + " linesPlayer=" + N(g_linesPlayer)
             + " linesHeld=" + N(g_linesHeld) + " linesOwn=" + N(g_linesOwn) + " linesNoUid=" + N(g_linesNoUid) + " linesNoHirer=" + N(g_linesNoHirer)
             + " linesNoAddr=" + N(g_linesNoAddr) + " endFault=" + N(g_endFault)
             + " | editorBody[frames,calls,rebuilt,gaveUp,fault]=" + N(g_bsSteps) + "," + N(g_bsCalls) + "," + N(g_bsRebuilt) + ","
             + N(g_bsGiveUps) + "," + N(g_bsFaults)
             + " linesOffMain=" + N((long long)g_linesOffMain) + " linesUnread=" + N(g_linesUnread) + " hook=" + N(g_doActionsHook));
}

// T-556 (resurrect.cpp), MAIN THREAD: PlayerInterface::recruit(c, edit = false). 1 joined, 0 refused, -1 fault, -2 no table
// address for recruit, -3 no PlayerInterface.
int HireRecruitNoEdit(::Character* c)
{
    if (kRecruitRva == 0) return -2;
    void* pi = PlayerInterfacePod();
    if (pi == 0) return -3;
    return RecruitPod(pi, c, false);
}
// T-556, MAIN THREAD: Character vt+0xA0 setFaction(f, platoon). 1 called, 0 fault.
int HireSetFaction(::Character* c, ::Faction* f, void* platoon) { return SetFactionPod(c, f, platoon); }
/* TEST-ONLY lever `copysquad <uid>`, MAIN THREAD: a copy of another game's world character (this game does not run it) is moved by this
   game's engine into a NEW squad of the copy's own faction - Character vt+0xA0 setFaction(its faction, no platoon): the faction's
   addMember makes the squad, numbered by this game's engine in this game's block. The sleep write and the heartbeat publish then meet a
   squad numbered here whose known members are all another game's (store.cpp, BlockSquadWrite). [CMD] copysquad line. */
std::string CopySquadLever(unsigned int uid)
{
    ::Character* c = FindSpawned(uid);
    ::Faction* f = 0;
    const char* why = 0;
    if (StoreMainThreadId() == 0 || StoreMainThreadId() != ::GetCurrentThreadId()) why = "not on the main thread";
    else if (EngineWritesBlocked()) why = "engine writes are blocked (a world is loading)";
    else if (!Plaus(c)) why = "no character for this uid is present on this game";
    else if (net::IsUidMine(uid)) why = "this game runs it (the lever takes a copy of another game's character)";
    else
    {
        f = FactionOfPod(c);
        if (!Plaus(f)) why = "its faction could not be read";
        else if (IsPlayerFaction(f) || IsPeerFaction(f)) why = "a player's character (the lever takes a world character's copy)";
    }
    const std::string head = "copysquad uid=" + N(uid);
    if (why != 0)
    {
        DebugLog("[CMD] " + head + " -> REFUSED - " + why);
        return "error " + head + " refused: " + why;
    }
    void* before = PlatoonOfPod(c);
    if (SetFactionPod(c, f, 0) == 0)
    {
        ++g_setFactionFault;
        DebugLog("[CMD] " + head + " -> setFaction FAULTED");
        return "error " + head + " setFaction faulted";
    }
    void* after = PlatoonOfPod(c);
    char b[200];
    _snprintf(b, 199, " squad %p -> %p (new=%d, context=%d)", before, after, (after != 0 && after != before) ? 1 : 0, ContextPlatoonForActive(after) != 0 ? 1 : 0); b[199] = 0;
    DebugLog("[CMD] " + head + " -> moved by this game's engine into a squad of its own faction:" + std::string(b));
    return "ok " + head + std::string(b);
}
/* TEST-ONLY lever `copysquad nearest`, MAIN THREAD: the living copy of another game's world character (not run here, not a player's or
   another player's faction) nearest to this game's first living player-faction character, within 2000 units (a tie goes to the lower
   uid), is moved exactly as `copysquad <uid>` moves it. [CMD] copysquad nearest line names the uid picked and its distance. */
std::string CopySquadNearestLever()
{
    unsigned int anchorUid = 0;
    ::Character* anchorC = MyHirer(&anchorUid);
    Ogre::Vector3 ap;
    if (anchorC == 0 || !SafeReadPosition(anchorC, &ap))
    {
        DebugLog("[CMD] copysquad nearest -> REFUSED - no own living player-faction character with a readable position");
        return "error copysquad nearest refused: no own living player-faction character with a readable position";
    }
    const float kMax = 2000.0f;
    float best = kMax; unsigned int uid = 0; int candidates = 0;
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int u = 0; ::Character* c = 0; Ogre::Vector3 q;
        if (!MirrorSlot(i, &u, &c) || u == 0 || !Plaus(c) || net::IsUidMine(u)) continue;
        ::Faction* f = FactionOfPod(c);
        if (!Plaus(f) || IsPlayerFaction(f) || IsPeerFaction(f)) continue;
        if (!Alive(c) || !SafeReadPosition(c, &q)) continue;
        const float dx = q.x - ap.x, dz = q.z - ap.z, d = sqrtf(dx * dx + dz * dz);
        if (d > kMax) continue;
        ++candidates;
        if (d < best || (d == best && uid != 0 && u < uid)) { best = d; uid = u; }
    }
    if (uid == 0)
    {
        DebugLog("[CMD] copysquad nearest -> REFUSED - no living copy of another game's world character within 2000 units of uid=" + N(anchorUid));
        return "error copysquad nearest refused: no copy within 2000 units";
    }
    char b[200];
    _snprintf(b, 199, "[CMD] copysquad nearest picked uid=%u dist=%.1f anchor=%u candidates=%d", uid, best, anchorUid, candidates); b[199] = 0;
    DebugLog(std::string(b));
    return CopySquadLever(uid);
}

/* TEST-ONLY lever `splitsquad [n]`: the last n members (default 1) of this game's first player squad - the squad of its first own
   living player-faction character (MyHirer's rule) - are moved into a NEW squad of the player faction, so a test can make a player
   squad that the last engine save does not hold. The verb only ARMS it (MAIN THREAD, the command channel); SplitSquadDrain runs it at
   the K2 safe point 0x7D17E0 (combat.cpp, the AI worker paused) and writes the result to the status file.
   The new squad is made as the squad screen's "Add squad" button makes one: that handler (0x4910C0) calls PlayerInterface 0x7F3D70,
   which is Faction::spawnEmptySquad 0x7F3880 (the player faction, no template, permanent, a position) and takes the result's
   ActivePlatoon (Platoon+0x1D8). Each character is then moved by Character vt+0xA0 setFaction(its own faction, the new
   ActivePlatoon): the old squad's removal (ActivePlatoon vt+0x18) and the new squad's addMember (vt+0x10 0x7966A0) - the engine's own
   join the hire flow uses. The squad screen's drag-and-drop mover 0x795A80 makes the same old-squad removal and an insert at a
   chosen place; it has no address-table row, so the lever does not call it. */
namespace {
typedef void* (*SsSpawnEmptySquadFn)(void* faction, void* squadTemplate, bool permanent, const Ogre::Vector3* p);
typedef std::string* (*SsPlatoonSidFn)(const void* platoon, std::string* out);
unsigned long long kSsSpawnEmptySquadRva = 0; static AddrReg kSsSpawnEmptySquadRva_reg("Faction_spawnEmptySquad", &kSsSpawnEmptySquadRva);   /* Steam_1.0.65 0x7F3880 Faction::spawnEmptySquad */
unsigned long long kSsPlatoonSidRva = 0; static AddrReg kSsPlatoonSidRva_reg("PlatoonSid", &kSsPlatoonSidRva);   /* Steam_1.0.65 0x384AF0 Platoon::getPlatoonStringID */
const size_t kSsActivePlatoonOff = 0x1D8;   /* Platoon -> ActivePlatoon (handoff.h, the squad reader's back-pointer) */
const int kSsMaxN = kSquadViewCap - 1;
const int kSsWaitMax = 600;                 /* safe points to wait while engine writes are blocked */
int g_ssArmed = 0, g_ssN = 1, g_ssWaits = 0;
long long g_ssRuns = 0, g_ssRefused = 0;

/* POD only (C2712): the engine call; 0 = faulted or no squad made */
void* SsSpawnEmptySquadPod(void* faction, const Ogre::Vector3* pos, void** activeOut)
{
    *activeOut = 0;
    __try
    {
        void* platoon = ((SsSpawnEmptySquadFn)(Base() + (uintptr_t)kSsSpawnEmptySquadRva))(faction, 0, true, pos);
        if (Plaus(platoon)) *activeOut = *(void**)((char*)platoon + kSsActivePlatoonOff);
        return platoon;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *activeOut = 0; return 0; }
}
/* POD only: the squad's id ("<faction name>_<number>"); 0 = faulted */
int SsPlatoonSidPod(const void* platoon, std::string* out)
{
    __try { ((SsPlatoonSidFn)(Base() + (uintptr_t)kSsPlatoonSidRva))(platoon, out); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
std::string SsSid(const void* platoon)
{
    std::string s;
    if (!Plaus(platoon) || !SsPlatoonSidPod(platoon, &s)) return "?";
    return s.empty() ? std::string("?") : s;
}
/* one [SQUAD] line and the status text for a refusal */
std::string SsRefuse(const std::string& why)
{
    ++g_ssRefused;
    DebugLog("[SQUAD] splitsquad REFUSED - " + why);
    return "error splitsquad: " + why;
}
/* MAIN THREAD (the K2 safe point, the AI worker paused). The status text. */
std::string SsRun(int n)
{
    if (kSsSpawnEmptySquadRva == 0 || kSsPlatoonSidRva == 0) return SsRefuse("Faction_spawnEmptySquad / PlatoonSid not in the address table");
    unsigned int firstUid = 0;
    ::Character* first = MyHirer(&firstUid);
    if (first == 0) return SsRefuse("no own living player-faction character");
    ::Faction* f = FactionOfPod(first);
    SquadView sv;
    if (!Plaus(f) || ReadSquadOf(first, &sv) != 1 || !Plaus(sv.ap) || !Plaus(sv.platoon)) return SsRefuse("no player squad (uid " + N(firstUid) + " has no readable squad)");
    if (sv.count > sv.stored) return SsRefuse("the player squad has " + N(sv.count) + " members, more than the reader holds (" + N(kSquadViewCap) + ")");
    if (sv.count < n + 1) return SsRefuse("the player squad has " + N(sv.count) + " members, fewer than n+1 = " + N(n + 1));
    std::vector< ::Character*> moving;
    std::vector<unsigned int> uids;
    for (int i = sv.count - n; i < sv.count; ++i)
    {
        ::Character* c = sv.members[i];
        const unsigned int u = Plaus(c) ? FindSpawnedUid(c) : 0u;
        if (u == 0 || !net::IsUidMine(u)) return SsRefuse("member " + N(i) + " of the player squad is not a character this game runs");
        if (FactionOfPod(c) != f) return SsRefuse("member " + N(i) + " (uid " + N(u) + ") is not in the player faction");
        moving.push_back(c); uids.push_back(u);
    }
    Ogre::Vector3 pos(sv.x, sv.y, sv.z);
    if (!SafeReadPosition(moving[0], &pos) && !sv.posUsable) pos = Ogre::Vector3(0.0f, 0.0f, 0.0f);
    const std::string fromSid = SsSid(sv.platoon);
    void* newAp = 0;
    void* newPlatoon = SsSpawnEmptySquadPod((void*)f, &pos, &newAp);
    if (!Plaus(newPlatoon) || !Plaus(newAp)) return SsRefuse("Faction::spawnEmptySquad made no squad (platoon " + N((long long)(uintptr_t)newPlatoon) + ")");
    std::string uidList;
    int moved = 0;
    for (size_t i = 0; i < moving.size(); ++i)
    {
        if (SetFactionPod(moving[i], f, newAp) == 0)
        {
            ++g_setFactionFault;
            return SsRefuse("setFaction FAULTED on uid " + N(uids[i]) + " after " + N(moved) + " moved into " + SsSid(newPlatoon));
        }
        if (SquadActivePlatoonOf(moving[i]) == newAp) ++moved;
        uidList += (uidList.empty() ? "" : ",") + N(uids[i]);
    }
    SquadView nv, ov;
    const int newCount = ReadSquadAt(newAp, &nv) == 1 ? nv.count : -1;
    const int oldCount = ReadSquadAt(sv.ap, &ov) == 1 ? ov.count : -1;
    const std::string newSid = SsSid(newPlatoon);
    ++g_ssRuns;
    char b[160];
    _snprintf(b, 159, " moved=%d/%d members=%d left=%d pos=%.0f,%.0f,%.0f runs=%lld", moved, n, newCount, oldCount, pos.x, pos.y, pos.z, g_ssRuns); b[159] = 0;
    const std::string body = "from=" + fromSid + " squad=" + newSid + " uids=" + uidList + std::string(b);
    if (moved != n) return SsRefuse("not every member reached the new squad: " + body);
    DebugLog("[SQUAD] splitsquad " + body);
    return "ok splitsquad " + body;
}
} // namespace

void CommandStatusLate(const std::string& text);   /* command_channel.cpp (ui.h): the status file, written after the verb returned */

std::string SplitSquadArm(const std::string& arg)
{
    int n = 1;
    if (!arg.empty())
    {
        char extra = 0;
        if (std::sscanf(arg.c_str(), "%d %c", &n, &extra) != 1 || n < 1 || n > kSsMaxN)
            return SsRefuse("usage splitsquad [n] (members to move, 1.." + N(kSsMaxN) + ", default 1)");
    }
    if (kSsSpawnEmptySquadRva == 0 || kSsPlatoonSidRva == 0) return SsRefuse("Faction_spawnEmptySquad / PlatoonSid not in the address table");
    g_ssArmed = 1; g_ssN = n; g_ssWaits = 0;
    return "ok splitsquad armed n=" + N(n) + " (runs at the next safe point; the result replaces this line)";
}

void SplitSquadDrain()
{
    if (g_ssArmed == 0) return;
    if (EngineWritesBlocked())
    {
        if (++g_ssWaits > kSsWaitMax) { g_ssArmed = 0; CommandStatusLate(SsRefuse("engine writes stayed blocked for " + N(kSsWaitMax) + " safe points")); }
        return;
    }
    g_ssArmed = 0;
    CommandStatusLate(SsRun(g_ssN));
}

} // namespace coop
