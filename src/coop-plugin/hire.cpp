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
#include "../common/squadtest.h"   /* squadtest: the lever's argument shape */
#include "speech.h"         // SayOnCopy (the refusal line)
#include "handoff.h"        // SquadActivePlatoonOf - reads a character's squad without making one
#include "coop_log.h"
#include "../common/peergone.h"   // PlayerGoneTakesRow: a player who leaves takes only its own rows
#include "../common/talkcursor.h"  // TalkKnockedOutOrDead: the person's own check before a recruit

#include "../common/recruitguard.h"   // another player's character is never recruited (every road to recruit 0x693010)
#include "appearance.h"   // T-1228: GroundRaceName - the NotMine line's race
#include "items.h"        // T-1228: TradeWindowSideNow - the NotMine line's trade window
#include <intrin.h>       // T-1228: _ReturnAddress - which engine call site recruits
#include "../common/prisonwire.h"     // T-980: the join hand-off's messages and decisions
#include "../common/givewire.h"       // T-1135: giving characters - the wire, the conditions, the guard exception
#include "../common/talkwire.h"       // T-478 step 6: the buying confirm's gate (TalkBuyConfirmGate, TalkBuyRunGate)
#include "resurrect.h"        // T-1135 review fold 1: OwnLivingAndLimit - room at the game's character limit before a give's take
#include "chat.h"           // ChatNoticeLine - the refused player's grey line
#include "team.h"           // TeamPlayerName - the owning player's name for that line
#include "game/Character.h"
#include "game/Faction.h"
#include "game/hand.h"
#include "hooks.h"   /* mig1: coop::AddHook (own MinHook) */
#include "worldsync.h"   /* PauseSnapshot: the paused flag and the speed for the editorstate line */
#include "../common/editorstate.h"
#include "peace.h"          // T-1194 (T2029): the peace lever's flag, gate and talk counters
#include "../common/peacegate.h"   // T-1194 (T2029): TalkAttackMask / TalkAttackDrop - a talk line's attack results under peace
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
RecruitFn g_recruitOrig = 0;
int g_recruitHook = 0;     // the 0x693010 detour (recruitguard.h): 1 armed, -1 AddHook failed, -2 no table address
int g_ownRecruitDepth = 0;   // MAIN THREAD: above 0 while RecruitPod runs - this file's roads, which tell the other games themselves

struct Pending { unsigned int reqId; unsigned int hirerUid; int price; int joinType; std::string faction; int ownerSlot; };   // ownerSlot: the player the REQ went to (the person's owner then; -1 unknown)
struct Reserved { unsigned int reqId; unsigned int hirerUid; unsigned int peer; int slot; };   // slot: the player the promise is made to (the asker; -1 unknown)
struct Inbound { coophire::HireMsg m; unsigned int peer; };

std::map<unsigned int, Pending>  g_pending;    // hirer: person uid -> my request in flight
std::map<unsigned int, Reserved> g_reserved;   // owner: person uid -> the request that won
std::vector<Inbound>             g_inbound;    // MAIN THREAD: arrival order, drained at the K2 safe point
unsigned int g_nextReqId = 1;

// the lever (MAIN THREAD: armed by the verb, drained at the safe point)
int g_testArmed = 0; int g_testMode = 0; unsigned int g_testUid = 0; int g_testPrice = kDefaultPrice;   /* mode 1 near, 2 uid, 3 near same, 4 nearestpeer */
int g_testJoinType = 18;   /* the hire's join type: 18 no editor; 3 (hiretest editor ...) recruit with the editor, as the dialogue's act 3 */
float g_testX = kSameX, g_testZ = kSameZ;
float g_testRadius = kNearMax;   /* mode 4 (nearestpeer): the search radius around the hirer */

long long g_reqOut = 0, g_reqIn = 0, g_okOut = 0, g_okIn = 0, g_noIn = 0, g_doneOut = 0, g_doneIn = 0, g_taken = 0;
long long g_noOut[coophire::kNoReasonMax + 1] = { 0 };
long long g_recruitRefused = 0, g_purseShort = 0, g_released = 0, g_factionSet = 0, g_joinedApplied = 0, g_lateOk = 0;
long long g_cancelledLinkDown = 0, g_otherActs = 0, g_bad = 0, g_sendFailed = 0, g_ctxForgotten = 0, g_hirerGone = 0;
long long g_localHires = 0, g_localRefused = 0, g_testRuns = 0, g_testNoPerson = 0, g_testNoHirer = 0, g_noPeerFaction = 0;
long long g_linesSeen = 0, g_linesPlayer = 0, g_linesLogged = 0, g_linesUnread = 0, g_setFactionFault = 0;
volatile LONGLONG g_linesOffMain = 0;   // any thread (the detour runs wherever the engine calls it)
long long g_ownedHeld = 0, g_ownedOwnerNo = 0, g_ownedTake = 0, g_ownedLocal = 0, g_ownedNoticed = 0;   // recruitguard.h refusals: hire line, owner NO, hirer re-check, lever / talk; lines shown
volatile LONGLONG g_ownedEngine = 0;   // the 0x693010 detour refused (any thread)
/* the engine's own recruits (detour_recruit, recruitguard.h EngineRecruitDecide): verdicts by kind, DONEs sent / not sent, queued
   people gone by the safe point, and the full queue reported so far (MAIN THREAD); queued, not queued (queue full) and refused off
   the main thread (any thread) */
long long g_engVerdict[recruitguard::kEngineRecruitMax + 1] = { 0 };
long long g_engSent = 0, g_engSendFailed = 0, g_engGone = 0, g_engFullLogged = 0;
recruitguard::NotMineLineSeen g_engNotMineSeen;   /* T-1228: the NotMine line's cap (one per uid per caller, 50 a session). MAIN THREAD */
long long g_engNotMineLines = 0;
volatile LONGLONG g_engQueued = 0, g_engQueueFull = 0, g_engOffMainRefused = 0;
long long g_linesHeld = 0, g_linesOwn = 0, g_linesNoUid = 0, g_linesNoHirer = 0, g_linesNoAddr = 0, g_endFault = 0;
std::map<int, long long> g_otherActTypes;   // held lines: action type -> count (not run)

// squadtest (TEST-ONLY): the one armed request, drained at the K2 safe point (MAIN THREAD). ownerUid 0 = `first`.
struct SquadTest { int armed; unsigned int ownerUid; int n; std::string templateSid; };
SquadTest g_squadTest = { 0, 0, 0, std::string() };
const float kSquadRing = 15.0f;               // the new members stand on a ring this far round their leader
const char* const kSquadMadeIn = "Drifters";  // the faction a new member is made in before this game's player hires it

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
int RecruitCallPod(void* pi, ::Character* c, bool edit)   // 1 joined, 0 refused (squad full / limit), -1 fault
{
    __try { RecruitFn fn = (RecruitFn)(Base() + (uintptr_t)kRecruitRva); return fn(pi, c, edit) != 0 ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* This file's own recruit roads - the own hire (LocalHire: a DONE when joined), TakeAndHire (its DONE), HireRecruitNoEdit (the
   brought-back character is announced by its SPAWN right after, resurrect.cpp) - each tells the other games itself, so the 0x693010
   detour sends nothing while the depth is above 0. The depth is kept outside the __try: a fault still brings it back. MAIN THREAD. */
int RecruitPod(void* pi, ::Character* c, bool edit)   // 1 joined, 0 refused (squad full / limit), -1 fault
{
    ++g_ownRecruitDepth;
    const int rc = RecruitCallPod(pi, c, edit);
    --g_ownRecruitDepth;
    return rc;
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
// T-980: the person to be recruited is there, not knocked out (+0x5B9) and not dead (+0x5BC) - the engine's own talk-start "down"
// (cooptalk::TalkKnockedOutOrDead). Alive() also counts the prone / ragdoll pose, which a caged or shackled prisoner lies in awake.
// Unreadable counts as down. No C++ object.
bool PersonUp(::Character* c)
{
    if (!Plaus(c)) return false;
    __try { const unsigned char* const p = (const unsigned char*)c; return cooptalk::TalkKnockedOutOrDead(p[0x5B9], p[0x5BC]) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

/* a faction's name for the log: another player's faction is named by that player, so only printable ASCII, at most 64 bytes */
std::string FacLog(::Faction* f)
{
    const std::string n = FactionName(f);
    std::string o;
    for (size_t i = 0; i < n.size() && o.size() < 64; ++i) { const unsigned char c = (unsigned char)n[i]; o += (c >= 0x20 && c <= 0x7E && c != '\'') ? (char)c : '?'; }
    return o;
}

// recruitguard.h: the person as this game sees it - kRecruitRefuseOwned when it is in a player's faction that is not this game's own
// player's (another player's stand-in, or a recorded stand-in). An NPC, or a faction pointer that cannot be read, is allowed. ANY THREAD.
int OwnedVerdict(::Character* c)
{
    ::Faction* f = Plaus(c) ? FactionOfPod(c) : 0;
    if (!Plaus(f)) return recruitguard::kRecruitAllow;
    return recruitguard::LocalRecruitDecide(AnyPlayersFaction(f) ? 1 : 0, IsPlayerFaction(f) ? 1 : 0);
}
// The refused player's grey chat line, naming the player the person belongs to when this game knows it. MAIN THREAD.
void NoticeOwned(::Character* c)
{
    ::Faction* f = Plaus(c) ? FactionOfPod(c) : 0;
    int slot = Plaus(f) ? StandInSlotOf(f) : -1;
    if (slot < 0 && Plaus(f)) { const int r = StandInRecordSlot(f); slot = r >= 0 ? r : -1; }
    const std::string name = TeamPlayerName(slot);
    std::string fac = Plaus(f) ? StandInDisplayName(f) : std::string();
    if (fac.empty() && Plaus(f) && !IsPlayerFaction(f) && AnyPlayersFaction(f)) fac = FactionName(f);
    ChatNoticeLine(slot, name, recruitguard::OwnedLine(name, fac));
    ++g_ownedNoticed;
}

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
    if (OwnedVerdict(person) != recruitguard::kRecruitAllow)
    {
        ++g_ownedLocal;
        DebugLog("[HIRE] own hire uid=" + N(uid) + " refused: another player's character (fac='" + FacLog(FactionOfPod(person)) + "')");
        NoticeOwned(person);
        return;
    }
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
    if (OwnedVerdict(person) != recruitguard::kRecruitAllow)   /* recruitguard.h: never asked for another player's character */
    {
        ++g_ownedLocal;
        DebugLog("[HIRE] hold uid=" + N(uid) + " refused: another player's character (fac='" + FacLog(FactionOfPod(person)) + "') - not asked");
        NoticeOwned(person);
        return;
    }
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

// ---- T-980: A PRISONER THIS GAME RUNS JOINS ANOTHER PLAYER'S CHARACTER (the join hand-off) ----
// A prisoner another player's character freed reaches its join line (act 18, DA_JOIN_SQUAD_FAST) on THIS game, which runs it, with
// this game's copy of that character as the one it joins. Run here, the act takes the engine's NPC branch for the copy's stand-in
// faction (it has no player controller) and puts the prisoner in that stand-in on this game only. So the line is held - none of its
// actions run, the chat is closed by endDialogue as HoldLine does - on whichever thread runs it (JoinHandOffLine, ANY THREAD: plain
// numbers into a ring under one leaf lock), and at the safe point the freer's game is asked (kPrisonJoinAsk). That game starts the
// hire handshake for the prisoner (HireForTalk: REQ to this game, the owner check, take + recruit through its own player, DONE).
// Every ask ends: the freer's game's REQ for the prisoner arrives (OwnerCheck), its kPrisonJoinNo arrives, that game leaves
// (HireForgetPeer), or kJoinAskWaitMs pass - each logged and counted; the prisoner then joins nobody.
struct JoinPend { unsigned int freerUid; int slot; DWORD at; };
struct JoinAskIn { unsigned int uid; unsigned int freerUid; unsigned int peer; };
cooprison::PrisonNoteRing g_joinRing;   // under g_joinLock
SRWLOCK g_joinLock = SRWLOCK_INIT;
volatile LONG64 g_joinHeld = 0, g_joinEndFault = 0;
std::map<unsigned int, JoinPend> g_joinPend;   // the prisoner's owner, MAIN THREAD: prisoner uid -> the ask in flight
std::vector<JoinAskIn> g_joinAsks;             // the freer's game, MAIN THREAD: asks waiting for the safe point
const size_t kJoinAsksMax = 16;
long long g_joinAsked = 0, g_joinSendFailed = 0, g_joinDup = 0, g_joinBlocked = 0, g_joinStale = 0, g_joinLostLogged = 0,
          g_joinReqArrived = 0, g_joinNoRecv = 0, g_joinNoStale = 0, g_joinTimedOut = 0, g_joinPeerGone = 0,
          g_joinAskRecv = 0, g_joinAskFull = 0, g_joinHireAsked = 0, g_joinRefusedOut[cooprison::kJoinNoMax + 1] = { 0 };

// ANY THREAD, from detour_doActions before the line runs: true = the line was held (the original must NOT run). The person and the
// target are read as HoldLine reads them; uids by address compares; ownership from the lock-free owned mirror; the player-character
// classification from spawn.cpp's lock-free table.
bool JoinHandOffLine(void* dlg, void* line)
{
    int types[16], values[16], total = 0; ::Character* person = 0; ::Character* target = 0;
    const int k = ReadLinePod(dlg, line, types, values, 16, &total, &person, &target);
    if (k <= 0) return false;
    bool has = false, money = false;
    for (int i = 0; i < k; ++i) { if (types[i] == cooprison::kPrisonActJoin) has = true; if (types[i] == 8) money = true; }
    if (!has) return false;
    const unsigned int su = Plaus(person) ? FindSpawnedUid(person) : 0u;
    const unsigned int tu = Plaus(target) ? FindSpawnedUid(target) : 0u;
    if (!cooprison::JoinHandOffHold(has, money, su, su != 0 && net::IsUidMineAnyThread(su), tu, tu != 0 && net::IsUidMineAnyThread(tu),
                                    tu != 0 ? IsUidPlayerCharAnyThread(tu) : -1, su != 0 ? IsUidPlayerCharAnyThread(su) : -1, kEndDialogueRva != 0))
        return false;
    cooprison::PrisonNote n;
    n.ev = cooprison::kPrisonActJoin; n.uid = su; n.freerUid = tu;
    n.nActs = (k < cooprison::kPrisonNoteActs) ? k : cooprison::kPrisonNoteActs; n.actTotal = total;   /* the line's actions, for the log: none of them runs */
    for (int i = 0; i < n.nActs; ++i) { n.actType[i] = types[i]; n.actValue[i] = values[i]; }
    ::AcquireSRWLockExclusive(&g_joinLock);
    cooprison::PrisonNotePush(&g_joinRing, n);   /* a full ring counts it (reported at the safe point); the line is held either way */
    ::ReleaseSRWLockExclusive(&g_joinLock);
    ::InterlockedIncrement64(&g_joinHeld);
    if (EndDialoguePod(dlg) != 1) ::InterlockedIncrement64(&g_joinEndFault);   /* the address is in the table: JoinHandOffHold requires it */
    return true;
}

// The prisoner's owner, MAIN THREAD (HireSafePointDrain): each held join line asked of the freer's game; asks with no answer for
// kJoinAskWaitMs dropped.
void JoinHeldDrain(bool blocked, DWORD now)
{
    cooprison::PrisonNote batch[cooprison::kPrisonNoteCap];
    ::AcquireSRWLockExclusive(&g_joinLock);
    const int n = cooprison::PrisonNoteTake(&g_joinRing, batch, cooprison::kPrisonNoteCap);
    const long long lost = g_joinRing.lost;
    ::ReleaseSRWLockExclusive(&g_joinLock);
    if (lost != g_joinLostLogged)
    {
        ErrorLog("[HIRE] join hand-off: " + N(lost - g_joinLostLogged) + " held join line(s) were not kept (ring full, " + N(cooprison::kPrisonNoteCap)
                 + ") - the freer's game was not asked; those prisoners join nobody");
        g_joinLostLogged = lost;
    }
    for (int i = 0; i < n; ++i)
    {
        const cooprison::PrisonNote& x = batch[i];
        std::string acts;   /* the held line's actions as type:value, as HoldLine logs them - none of them ran here */
        for (int j = 0; j < x.nActs && j < cooprison::kPrisonNoteActs; ++j) acts += (j ? "," : "") + N(x.actType[j]) + ":" + N(x.actValue[j]);
        const std::string who = "uid=" + N(x.uid) + " freer=" + N(x.freerUid) + " acts=[" + acts + "] total=" + N(x.actTotal);
        if (blocked) { ++g_joinBlocked; DebugLog("[HIRE] join hand-off " + who + " NOT asked: the world is loading or tearing down - the prisoner joins nobody"); continue; }
        if (!net::IsUidMine(x.uid) || !Plaus(FindSpawned(x.freerUid)) || net::IsUidMine(x.freerUid))
        {
            ++g_joinStale;
            DebugLog("[HIRE] join hand-off " + who + " NOT asked: the prisoner is no longer ours or the freer's copy is gone - the prisoner joins nobody");
            continue;
        }
        if (g_joinPend.find(x.uid) != g_joinPend.end()) { ++g_joinDup; DebugLog("[HIRE] join hand-off " + who + " - already asked; this line held too, not asked twice"); continue; }
        cooprison::PrisonMsg m;
        m.uid = x.uid; m.kind = cooprison::kPrisonJoinAsk; m.freerUid = x.freerUid;
        if (!net::SendPrison(m))
        {
            ++g_joinSendFailed;
            ErrorLog("[HIRE] join hand-off " + who + " NOT asked: no road to the freer's game - the prisoner joins nobody");
            continue;
        }
        JoinPend p; p.freerUid = x.freerUid; p.slot = net::OwnerSlotOf(x.freerUid); p.at = now;
        g_joinPend[x.uid] = p;
        ++g_joinAsked;
        DebugLog("[HIRE] join hand-off " + who + " - the join line (act 18) and its other actions were not run here; the freer's game (slot " + N(p.slot)
                 + ") is asked to hire the prisoner through the hire handshake");
    }
    for (std::map<unsigned int, JoinPend>::iterator it = g_joinPend.begin(); it != g_joinPend.end(); )
    {
        if (!cooprison::JoinAskLost(now - it->second.at)) { ++it; continue; }
        ++g_joinTimedOut;
        ErrorLog("[HIRE] join hand-off uid=" + N(it->first) + " freer=" + N(it->second.freerUid) + " LOST: no hire request and no refusal from the freer's game in "
                 + N(cooprison::kJoinAskWaitMs / 1000) + " s - the prisoner joins nobody");
        g_joinPend.erase(it++);
    }
}

// The prisoner's owner, MAIN THREAD (OwnerCheck): a hire request for a prisoner whose join was asked ends that ask - the hire
// handshake answers from here on.
void JoinPendReqArrived(unsigned int uid, unsigned int hirerUid, unsigned int reqId)
{
    std::map<unsigned int, JoinPend>::iterator it = g_joinPend.find(uid);
    if (it == g_joinPend.end()) return;
    ++g_joinReqArrived;
    DebugLog("[HIRE] join hand-off uid=" + N(uid) + " freer=" + N(it->second.freerUid) + " reached the hire road: request req=" + N(reqId) + " hirer=" + N(hirerUid)
             + (hirerUid == it->second.freerUid ? std::string() : std::string(" (a different hirer than the freer)")));
    g_joinPend.erase(it);
}

// The freer's game, MAIN THREAD (HireSafePointDrain): each ask from the prisoner's owner - the hire asked through HireForTalk, or a
// kPrisonJoinNo sent back with the reason.
void JoinAskDrain(bool blocked)
{
    if (g_joinAsks.empty()) return;
    std::vector<JoinAskIn> batch;
    batch.swap(g_joinAsks);
    for (size_t i = 0; i < batch.size(); ++i)
    {
        const JoinAskIn& a = batch[i];
        unsigned char reason = cooprison::JoinAskDecide(blocked, net::IsUidMine(a.freerUid), Plaus(FindSpawned(a.uid)), net::IsUidMine(a.uid));
        std::string why;
        if (reason == cooprison::kJoinNoNone)
        {
            if (HireForTalk(a.uid, a.freerUid, 0, cooprison::kPrisonActJoin, &why) == 1)
            {
                ++g_joinHireAsked;
                DebugLog("[HIRE] join hand-off uid=" + N(a.uid) + " freer=" + N(a.freerUid) + " - the prisoner's owner asked; hire request sent to it");
                continue;
            }
            reason = cooprison::kJoinNoNotAsked;
        }
        ++g_joinRefusedOut[reason];
        cooprison::PrisonMsg m;
        m.uid = a.uid; m.kind = cooprison::kPrisonJoinNo; m.freerUid = a.freerUid; m.joinNo = reason;
        const bool sent = net::SendPrison(m, a.peer);
        DebugLog("[HIRE] join hand-off uid=" + N(a.uid) + " freer=" + N(a.freerUid) + " NOT asked: reason=" + N(reason) + (why.empty() ? std::string() : " (" + why + ")")
                 + (sent ? std::string(" - refusal sent to the prisoner's owner") : std::string(" - refusal NOT sent (no road); the owner's ask ends by its time limit")));
    }
}

// Owner, design step 4: the live check, first request wins.
void OwnerCheck(const coophire::HireMsg& m, unsigned int peer)
{
    ++g_reqIn;
    JoinPendReqArrived(m.uid, m.hirerUid, m.reqId);   /* T-980: a join hand-off asked of the hirer's game ends here */
    int reason = coophire::kNoNone;
    ::Character* c = 0;
    if (!net::IsUidMine(m.uid)) reason = coophire::kNoNotMine;
    else if (!Plaus(c = FindSpawned(m.uid))) reason = coophire::kNoGone;
    else if (g_reserved.find(m.uid) != g_reserved.end()) reason = coophire::kNoReserved;
    else
    {
        // The hirer's game already ran the game's own dialogue, which offered the hire; the owner refuses a person whose faction,
        // cage state or slave state it cannot read - and (recruitguard.h) a person in ANY player's faction: its own player's recruit
        // (the hire dialogue still runs on the other games' copies) or a stand-in's. A player's recruit is never hired away.
        ::Faction* f = FactionOfPod(c);
        if (!Plaus(f)) reason = coophire::kNoFaction;
        else if (recruitguard::OwnerHireReason(AnyPlayersFaction(f) ? 1 : 0) != coophire::kNoNone) { reason = coophire::kNoFaction; ++g_ownedOwnerNo; }
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

// ---- T-478 step 6 (design C, 2026-11-05): BUYING A SLAVE (42) OR AN ANIMAL (48) WHOSE CHARACTER ANOTHER GAME RUNS ----
// The buying window opens on the BUYER's game (speech.cpp TkBuyOpenLocal for a forwarded talk; in a talk the router did not forward,
// the engine's own case opens it here the same way) and lists this game's view of the trader's slaves / animals - another game's
// copies when that game runs the trader. The engine's confirm (vtable entry 0: SlaveBuyingConfirm 0x6AE510 / AnimalBuyingConfirm
// 0x6AB410, Read decomp_6ae510 / 6ab410) charges every ticked row's price from this game's purse - a missing character's too - and
// frees / recruits each one it finds; run on copies it would charge for characters this game does not run and recruit stand-ins
// (T-1228). So the confirm is held (BuyConfirm, cooptalk::TalkBuyConfirmGate): each ticked row another game runs is asked of that
// game on the hire road (Hold: REQ with join type 42 / 48; OwnerCheck reserves it there - a second buyer gets NO reserved) and the
// window stays open (0). BuyDrain, at the safe point: when every owner said OK and the window and its ticked rows are still the same
// (TalkBuyRunGate), this game takes each character (TakeLocalOwner, as TakeAndHire) and runs the engine's own confirm ONCE on them -
// it charges this purse and recruits / frees exactly as single player - then closes the window by the cancel button's road (its
// vt +0x68) and sends one DONE {taken 1, joined as read after} per row to every game. Any NO, an ask its owner's game left unanswered,
// a closed window, changed rows or a closed road: nothing is taken, nothing charged; each OK'd owner's promise is cleared (DONE
// taken 0). The engine's own confirm runs inside this file's own-road depth, so the recruit guard (detour_recruit) does not send a
// second DONE for the animals it recruits - the hire road's DONE is the only one. MAIN THREAD (the window's button; the K2 drain).
typedef unsigned long long (*BuyConfirmFn)(void* self);
static unsigned long long kBuySlaveRva = 0;  static coop::AddrReg kBuySlaveRva_reg("SlaveBuyingConfirm", &kBuySlaveRva);     /* Steam_1.0.65 0x6AE510 (1.0.68 0x6ADD20): CharacterTrading_SlaveBuying vtable 0x16FE930 entry 0 */
static unsigned long long kBuyAnimalRva = 0; static coop::AddrReg kBuyAnimalRva_reg("AnimalBuyingConfirm", &kBuyAnimalRva);  /* Steam_1.0.65 0x6AB410 (1.0.68 0x6AAC20): CharacterTrading_AnimalBuying vtable 0x16FE9A8 entry 0 */
static unsigned long long kBuyWinRva = 0;    static coop::AddrReg kBuyWinRva_reg("TradingWindowCurrent", &kBuyWinRva);       /* Steam_1.0.65 0x2131BE0: the engine's one current trading window; its handler at +0xD0 (725ca0) */
static unsigned long long kBuyHandCharRva = 0; static coop::AddrReg kBuyHandCharRva_reg("Hand_asCharacter", &kBuyHandCharRva); /* Steam_1.0.65 0x7974F0 hand::getCharacter - a row starts with its hand */
BuyConfirmFn g_buySlaveOrig = 0, g_buyAnimalOrig = 0;
int g_buySlaveHook = 0, g_buyAnimalHook = 0;   // 1 armed, -1 AddHook failed, -2 no table address
const int kBuyRowsCap = 32;
typedef cooptalk::TalkBuyRow BuyRow;       // state 0 asked, 1 OK, 2 NO, 3 lost
typedef cooptalk::TalkBuyBatch BuyBatch;   // every field set by its constructor and Reset() (run T2051: `BuyBatch()` left them as they were)
BuyBatch g_buy;
void* g_buySpentHandler = 0; void* g_buySpentWin = 0;   // the window whose purchase ran (its close did not take): never charged again
long long g_buyCalls = 0, g_buyEngine = 0, g_buyAsked = 0, g_buyRefused = 0, g_buyAsking = 0, g_buySpent = 0, g_buyRan = 0,
          g_buyAbandoned = 0, g_buyEngineRefusedAfterTake = 0, g_buyCloseFault = 0, g_buyOffMain = 0, g_buyLateOk = 0,
          g_buyOwnDone = 0, g_buyCloseSkipped = 0;
unsigned int g_buyLastUid = 0;   /* T-1228 TEST-ONLY aim: the newest row a BOUGHT confirm put in this game's player faction (renametest <name> bought) */

BuyConfirmFn BuyOrigOf(int joinType) { return joinType == 42 ? g_buySlaveOrig : g_buyAnimalOrig; }
const char* BuyWord(int joinType) { return joinType == 42 ? "slave" : "animal"; }
void* BuyReadPtrPod(const void* at, int* ok)   // no C++ object (C2712)
{
    __try { *ok = 1; return *(void* const*)at; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *ok = 0; return 0; }
}
// 1 = the engine's current trading window holds this handler (+0xD0); *win = that window.
int BuyWindowOf(void* handler, void** win)
{
    *win = 0;
    if (kBuyWinRva == 0 || handler == 0) return 0;
    int ok = 0;
    void* const w = BuyReadPtrPod((const void*)(Base() + (uintptr_t)kBuyWinRva), &ok);
    if (!ok || w == 0) return 0;
    void* const h = BuyReadPtrPod((const char*)w + 0xD0, &ok);
    if (!ok || h != handler) return 0;
    *win = w;
    return 1;
}
// The ticked rows as 0x725690 walks them (+0x20 rows, +0x28 count, 0x68 each, ticked byte +0x24, price +0x20). 1 read, -1 faulted,
// -2 more ticked rows than the cap.
int BuyTickedRowsPod(void* handler, void** rows, int* prices, int cap, int* n)
{
    *n = 0;
    __try
    {
        const char* const h = (const char*)handler;
        char* const base = *(char* const*)(h + 0x20);
        const unsigned long long cnt = *(const unsigned long long*)(h + 0x28);
        if (cnt > 4096ull) return -1;
        for (unsigned long long i = 0; base != 0 && i < cnt; ++i)
        {
            char* const r = base + i * 0x68;
            if (*(const unsigned char*)(r + 0x24) == 0) continue;
            if (*n >= cap) return -2;
            rows[*n] = r; prices[*n] = *(const int*)(r + 0x20); ++*n;
        }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
::Character* BuyRowCharPod(void* row, int* ok)
{
    typedef ::Character* (*HandCharFn)(const void* self);
    *ok = 0;
    if (kBuyHandCharRva == 0) return 0;
    __try { ::Character* const c = ((HandCharFn)(Base() + (uintptr_t)kBuyHandCharRva))(row); *ok = 1; return c; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
unsigned long long BuyOrigPod(BuyConfirmFn fn, void* self, int* fault)
{
    *fault = 0;
    __try { return fn(self); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *fault = 1; return 0; }
}
int BuyCloseWindowPod(void* win)   // the cancel button's road (0x721820 -> the window's vt +0x68: close 0x725D00 -> destructor 0x727550)
{
    typedef void (*CloseFn)(void* self);
    __try { void** const vt = *(void***)win; ((CloseFn)vt[0x68 / 8])(win); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
struct BuyScan { int read; int n; int nOwn, nOther, nReserved, nNoUid, nGone; unsigned int uids[kBuyRowsCap]; int prices[kBuyRowsCap]; int cls[kBuyRowsCap]; };
void BuyScanRows(void* handler, BuyScan* s)
{
    std::memset(s, 0, sizeof(*s));
    void* rows[kBuyRowsCap];
    if (BuyTickedRowsPod(handler, rows, s->prices, kBuyRowsCap, &s->n) != 1) { s->read = 0; s->n = 0; return; }
    s->read = 1;
    for (int i = 0; i < s->n; ++i)
    {
        int ok = 0;
        ::Character* const c = BuyRowCharPod(rows[i], &ok);
        if (!ok) { s->read = 0; return; }
        const int found = Plaus(c) ? 1 : 0;
        const unsigned int uid = found ? FindSpawnedUid(c) : 0u;
        const int mine = (uid != 0 && net::IsUidMine(uid)) ? 1 : 0;
        const int reserved = (mine && g_reserved.find(uid) != g_reserved.end()) ? 1 : 0;
        const int k = cooptalk::TalkBuyRowClass(found, uid, mine, reserved);
        s->uids[i] = uid; s->cls[i] = k;
        if (k == cooptalk::kTalkBuyRowOwn) ++s->nOwn;
        else if (k == cooptalk::kTalkBuyRowOther) ++s->nOther;
        else if (k == cooptalk::kTalkBuyRowReserved) ++s->nReserved;
        else if (k == cooptalk::kTalkBuyRowNoUid) ++s->nNoUid;
        else ++s->nGone;
    }
}
std::string BuyScanWords(const BuyScan& s)
{
    return "ticked=" + N(s.n) + " own=" + N(s.nOwn) + " otherGames=" + N(s.nOther) + " promisedToAnother=" + N(s.nReserved)
           + " noUid=" + N(s.nNoUid) + " noCharacter=" + N(s.nGone);
}
// The animal window's room at the game's character limit for n more (1 room, 0 none, -1 unread); a slave window has no limit check.
int BuyRoom(int joinType, int n)
{
    if (joinType != 48) return 1;
    int alive = 0, limit = 0;
    const int limitRead = OwnLivingAndLimit(&alive, &limit);
    return swgive::RoomForAll(limitRead, limit, alive, n);
}
std::string BuyUids(const BuyBatch& b)
{
    std::string u;
    for (size_t i = 0; i < b.rows.size(); ++i) u += (u.empty() ? "" : ",") + N(b.rows[i].uid);
    return u;
}
// Nothing is taken: each OK'd owner's promise is cleared (DONE taken 0); each ask still out is dropped here, so its late OK is
// answered DONE taken 0 by TakeAndHire's late-OK road. The window stays as it is.
void BuyAbandon(const std::string& why)
{
    for (size_t i = 0; i < g_buy.rows.size(); ++i)
    {
        const BuyRow& r = g_buy.rows[i];
        if (r.state == 1) SendDone(r.peer, r.reqId, r.uid, g_buy.hirerUid, 0, 0);
        else if (r.state == 0)
        {
            std::map<unsigned int, Pending>::iterator it = g_pending.find(r.uid);
            if (it != g_pending.end() && it->second.reqId == r.reqId) g_pending.erase(it);
        }
    }
    ++g_buyAbandoned;
    ErrorLog("[HIRE] T478 buy " + std::string(BuyWord(g_buy.joinType)) + " confirm rows=[" + BuyUids(g_buy) + "] buyer uid=" + N(g_buy.hirerUid)
             + " REFUSED: " + why + " - no money, nobody moved (each owner's promise cleared)");
    g_buy.Reset();
}
unsigned long long BuyConfirm(int joinType, void* self)
{
    ++g_buyCalls;
    const BuyConfirmFn orig = BuyOrigOf(joinType);
    const unsigned long mt = StoreMainThreadId();
    if (mt != 0 && ::GetCurrentThreadId() != mt)   /* the window's button runs on the main thread; any other caller is refused */
    {
        ++g_buyOffMain;
        return 0;
    }
    void* win = 0;
    const int tied = BuyWindowOf(self, &win);
    const int spent = (g_buySpentHandler != 0 && self == g_buySpentHandler && (!tied || win == g_buySpentWin)) ? 1 : 0;
    if (g_buy.active && g_buy.handler != self && !spent) BuyAbandon("a newer buying window's confirm was clicked");
    const int asking = (g_buy.active && g_buy.handler == self) ? 1 : 0;
    BuyScan s;
    std::memset(&s, 0, sizeof(s));
    if (!spent && !asking) BuyScanRows(self, &s);
    unsigned int hirerUid = 0;
    ::Character* const hirer = MyHirer(&hirerUid);
    const int linked = net::SideRoadOpen() ? 1 : 0;
    const int room = (s.read && s.nOther > 0) ? BuyRoom(joinType, s.n) : 1;
    /* a session: the road or a dial is up, or a ticked row has a uid - a missing row is then refused, never charged for nobody */
    const int session = (linked || net::SessionUpOrDialing() || s.nOwn + s.nOther + s.nReserved > 0) ? 1 : 0;
    const int g = cooptalk::TalkBuyConfirmGate(spent, asking, s.read, s.nOther, s.nReserved, s.nNoUid, s.nGone, linked, tied, hirer != 0 ? 1 : 0, room, session);
    const std::string head = "[HIRE] T478 buy " + std::string(BuyWord(joinType)) + " confirm ";
    if (g == cooptalk::kTalkBuyEngine)
    {
        ++g_buyEngine;
        /* owner 1496 a (2026-11-05): a BUY-BACK - this game's own character(s) bought from a trader another game runs - stays decided
           here, as single player: the engine's confirm takes the price from this purse and pays no one (decomp_6ae510 :150-151). The
           trader is the handler's +0x8 (6ae510 reads its faction there). Log only. */
        int tOk = 0;
        void* const trader = BuyReadPtrPod((const char*)self + 0x8, &tOk);
        const unsigned int traderUid = (tOk && Plaus((::Character*)trader)) ? FindSpawnedUid((::Character*)trader) : 0u;
        const int buyBack = (s.read && s.nOwn > 0 && traderUid != 0 && !net::IsUidMine(traderUid)) ? 1 : 0;
        void* const ownB = (buyBack && hirer != 0) ? OwnershipsPod(FactionOfPod(hirer)) : 0;
        int pBefore = -1, pAfter = -1;
        if (ownB == 0 || !MoneyPod(ownB, &pBefore)) pBefore = -1;
        const unsigned long long res = orig(self);
        if (buyBack)
        {
            if (ownB == 0 || !MoneyPod(ownB, &pAfter)) pAfter = -1;
            std::string rows; long long price = 0;
            for (int i = 0; i < s.n; ++i)
            {
                if (s.cls[i] != cooptalk::kTalkBuyRowOwn) continue;
                rows += (rows.empty() ? "" : ",") + N(s.uids[i]) + ":" + N(s.prices[i]);
                price += s.prices[i];
            }
            DebugLog(head + "BUY-BACK rows=[" + rows + "] (uid:price) buyer uid=" + N(hirerUid) + " price=" + N(price) + " purse " + N(pBefore) + " -> " + N(pAfter)
                     + " engineConfirm=" + N((long long)(res & 0xFF)) + " trader uid=" + N(traderUid) + " run by slot " + N(net::OwnerSlotOf(traderUid))
                     + " - this game's own character(s) bought back, decided on this game (owner 1496 a); the trader is not paid (single player pays no one)");
        }
        return res;
    }
    if (g == cooptalk::kTalkBuySpent)
    {
        ++g_buySpent;
        DebugLog(head + "SPENT: " + cooptalk::TalkBuyGateName(g));
        return 1;
    }
    if (g == cooptalk::kTalkBuyAsking)
    {
        ++g_buyAsking;
        DebugLog(head + "rows=[" + BuyUids(g_buy) + "] HELD: " + cooptalk::TalkBuyGateName(g) + " - nothing charged");
        return 0;
    }
    if (g != cooptalk::kTalkBuyAsk)
    {
        ++g_buyRefused;
        ErrorLog(head + BuyScanWords(s) + " linked=" + N(linked) + " session=" + N(session) + " tied=" + N(tied) + " room=" + N(room) + " REFUSED: " + cooptalk::TalkBuyGateName(g)
                 + " - no money, nobody moved; the window stays open");
        return 0;
    }
    BuyBatch b;
    b.active = 1; b.joinType = joinType; b.handler = self; b.win = win; b.hirerUid = hirerUid; b.nRows = s.n;
    g_buy = b;
    std::string fail;
    for (int i = 0; i < s.n && fail.empty(); ++i)
    {
        if (s.cls[i] != cooptalk::kTalkBuyRowOther) continue;
        const unsigned int uid = s.uids[i];
        ::Character* const c = FindSpawned(uid);
        const unsigned int reqBefore = g_nextReqId;
        if (Plaus(c)) Hold(uid, c, hirerUid, s.prices[i], joinType);
        std::map<unsigned int, Pending>::iterator it = g_pending.find(uid);
        if (it == g_pending.end() || it->second.reqId < reqBefore || it->second.joinType != joinType)
        {
            fail = "uid " + N(uid) + " could not be asked of its owner (another request for it is out, another player's character, or no road)";
            break;
        }
        BuyRow r; r.uid = uid; r.reqId = it->second.reqId; r.price = s.prices[i]; r.state = 0; r.gen = 0; r.peer = 0; r.noReason = 0;
        g_buy.rows.push_back(r);
    }
    if (!fail.empty()) { ++g_buyRefused; BuyAbandon(fail); return 0; }
    ++g_buyAsked;
    DebugLog(head + BuyScanWords(s) + " buyer uid=" + N(hirerUid) + " rows=[" + BuyUids(g_buy) + "] HELD: asked each row's owner game (join type "
             + N(joinType) + ") - nothing charged until every owner says yes; the window stays open");
    return 0;
}
unsigned long long detour_buySlaveConfirm(void* self) { return BuyConfirm(42, self); }
unsigned long long detour_buyAnimalConfirm(void* self) { return BuyConfirm(48, self); }

// TakeAndHire / Refused, for a request of the buying road: the owner's answer is recorded in the batch (0 = not this batch's).
int BuyOkSeen(const coophire::HireMsg& m, unsigned int peer)
{
    for (size_t i = 0; g_buy.active && i < g_buy.rows.size(); ++i)
    {
        BuyRow& r = g_buy.rows[i];
        if (r.uid != m.uid || r.reqId != m.reqId || r.state != 0) continue;
        r.state = 1; r.gen = m.gen; r.peer = peer;
        DebugLog("[HIRE] T478 buy uid=" + N(m.uid) + " req=" + N(m.reqId) + " OK from its owner (promised to this game)");
        return 1;
    }
    return 0;
}
int BuyNoSeen(const coophire::HireMsg& m)
{
    for (size_t i = 0; g_buy.active && i < g_buy.rows.size(); ++i)
    {
        BuyRow& r = g_buy.rows[i];
        if (r.uid != m.uid || r.reqId != m.reqId || r.state != 0) continue;
        r.state = 2; r.noReason = m.reason;
        return 1;
    }
    return 0;
}
// Every owner said yes: the take and the engine's own confirm, once.
void BuyRun()
{
    const BuyBatch b = g_buy;
    void* win = 0;
    const int windowSame = (BuyWindowOf(b.handler, &win) && win == b.win) ? 1 : 0;
    BuyScan s;
    std::memset(&s, 0, sizeof(s));
    if (windowSame) BuyScanRows(b.handler, &s);
    std::vector<unsigned int> now, asked;
    for (int i = 0; i < s.n; ++i) if (s.cls[i] == cooptalk::kTalkBuyRowOther) now.push_back(s.uids[i]);
    for (size_t i = 0; i < b.rows.size(); ++i) asked.push_back(b.rows[i].uid);
    const int rowsSame = (s.read && s.nReserved == 0 && s.nNoUid == 0 && s.nGone == 0 && cooptalk::TalkBuyUidsSame(now, asked)) ? 1 : 0;
    int allUp = 1;
    for (size_t i = 0; i < b.rows.size(); ++i)
    {
        ::Character* const c = FindSpawned(b.rows[i].uid);
        if (!PersonUp(c) || OwnedVerdict(c) != recruitguard::kRecruitAllow) allUp = 0;
    }
    ::Character* const hirer = FindSpawned(b.hirerUid);
    if (!Alive(hirer) || !net::IsUidMine(b.hirerUid)) allUp = 0;
    const int linked = net::SideRoadOpen() ? 1 : 0;
    const int room = BuyRoom(b.joinType, s.n);
    const int r = cooptalk::TalkBuyRunGate(windowSame, rowsSame, linked, allUp, room);
    if (r != cooptalk::kTalkBuyRunGo) { BuyAbandon(cooptalk::TalkBuyRunName(r)); return; }
    void* const own = OwnershipsPod(FactionOfPod(hirer));
    int before = -1, after = -1;
    if (own == 0 || !MoneyPod(own, &before)) before = -1;
    /* this game's OWN ticked rows (the re-scan's, re-validated above) and whether each was in this game's player faction before the
       confirm: the confirm runs inside the own-road depth, so the recruit guard sends no DONE for them - sent below (T-1228) */
    std::vector<unsigned int> ownUids;
    std::vector<int> ownWasIn;
    for (int i = 0; i < s.n; ++i)
    {
        if (s.cls[i] != cooptalk::kTalkBuyRowOwn) continue;
        ::Character* const c = FindSpawned(s.uids[i]);
        ownUids.push_back(s.uids[i]);
        ownWasIn.push_back((Plaus(c) && IsPlayerFaction(FactionOfPod(c))) ? 1 : 0);
    }
    for (size_t i = 0; i < b.rows.size(); ++i)
    {
        net::TakeLocalOwner(b.rows[i].uid, net::GenForTake(b.rows[i].uid, b.rows[i].gen));   /* as TakeAndHire: above the OK's gen */
        UnpuppetForOwnership(b.rows[i].uid);
        ++g_taken;
    }
    g_buy.Reset();
    int fault = 0;
    ++g_ownRecruitDepth;   /* this file's own road: the recruit guard sends no DONE of its own for the animals the engine recruits */
    const unsigned long long res = BuyOrigPod(BuyOrigOf(b.joinType), b.handler, &fault);
    --g_ownRecruitDepth;
    if (own == 0 || !MoneyPod(own, &after)) after = -1;
    std::string joinedWords;
    unsigned int lastJoined = 0;
    for (size_t i = 0; i < b.rows.size(); ++i)
    {
        ::Character* const c = FindSpawned(b.rows[i].uid);
        const int joined = (Plaus(c) && IsPlayerFaction(FactionOfPod(c))) ? 1 : 0;
        if (joined) lastJoined = b.rows[i].uid;
        joinedWords += (joinedWords.empty() ? "" : ",") + N(b.rows[i].uid) + ":" + N(joined);
        SendDone(net::kHireNoAsker, b.rows[i].reqId, b.rows[i].uid, b.hirerUid, 1, joined, net::MineGenOf(b.rows[i].uid));   /* every game: the owner releases it, the others re-file their copies */
    }
    std::string ownWords;
    for (size_t i = 0; i < ownUids.size(); ++i)
    {
        ::Character* const c = FindSpawned(ownUids[i]);
        const int nowIn = (Plaus(c) && IsPlayerFaction(FactionOfPod(c))) ? 1 : 0;
        const int done = cooptalk::TalkBuyOwnRowDone(cooptalk::kTalkBuyRowOwn, ownWasIn[i], nowIn);
        ownWords += (ownWords.empty() ? "" : ",") + N(ownUids[i]) + ":" + N(nowIn) + (done ? "+done" : "");
        if (done) { SendDone(net::kHireNoAsker, 0, ownUids[i], b.hirerUid, 0, 1); ++g_buyOwnDone; }   /* as LocalHire: every game re-files its copy (G3) */
    }
    const int ran = (!fault && (res & 0xFF) != 0) ? 1 : 0;
    int closed = 0;
    if (ran)
    {
        ++g_buyRan;
        if (lastJoined != 0) g_buyLastUid = lastJoined;
        g_buySpentHandler = b.handler; g_buySpentWin = b.win;
        void* winNow = 0;
        if (BuyWindowOf(b.handler, &winNow) && winNow == b.win)   /* the engine's confirm may have ended or replaced the window */
        {
            closed = BuyCloseWindowPod(b.win);
            if (closed != 1) ++g_buyCloseFault;
        }
        else { closed = 2; ++g_buyCloseSkipped; }
    }
    else ++g_buyEngineRefusedAfterTake;
    const std::string line = "[HIRE] T478 buy " + std::string(BuyWord(b.joinType)) + " confirm rows=[" + BuyUids(b) + "] buyer uid=" + N(b.hirerUid)
        + " every owner said yes: taken=" + N((long long)b.rows.size()) + " engineConfirm=" + N((long long)(res & 0xFF)) + (fault ? " (FAULTED)" : "")
        + " purse " + N(before) + " -> " + N(after) + " joined=[" + joinedWords + "] ownRows=[" + ownWords + "] windowClosed=" + N(closed)
        + (closed == 2 ? std::string(" (not closed here: the window had already ended or changed)") : std::string())
        + (ran ? std::string(" - BOUGHT: the engine's own purchase ran once on this game's own characters; hire done sent to every game")
               : std::string(" - the engine refused after the take (its own check, e.g. the character limit): nothing charged; the characters are this game's now; confirm again"));
    if (ran) DebugLog(line); else ErrorLog(line);
}
// MAIN THREAD, every frame (HireSafePointDrain, before its quiet return): a spent window that ended is forgotten; the batch is
// abandoned when its window ended, refused at any NO or lost ask, and run once every owner said yes (not while the world is blocked).
void BuyDrain(bool blocked)
{
    if (g_buySpentHandler != 0)
    {
        void* w = 0;
        if (!BuyWindowOf(g_buySpentHandler, &w) || w != g_buySpentWin) { g_buySpentHandler = 0; g_buySpentWin = 0; }
    }
    if (!g_buy.active) return;
    void* w = 0;
    if (!BuyWindowOf(g_buy.handler, &w) || w != g_buy.win) { BuyAbandon(cooptalk::TalkBuyRunName(cooptalk::kTalkBuyRunWindowGone)); return; }
    int ok = 0, no = 0, lost = 0, firstNo = -1;
    unsigned int noUid = 0;
    for (size_t i = 0; i < g_buy.rows.size(); ++i)
    {
        BuyRow& r = g_buy.rows[i];
        if (r.state == 0)
        {
            std::map<unsigned int, Pending>::const_iterator it = g_pending.find(r.uid);
            if (it == g_pending.end() || it->second.reqId != r.reqId) r.state = 3;   /* its owner's game left (HireForgetPeer) */
        }
        if (r.state == 1) ++ok;
        else if (r.state == 2) { ++no; if (firstNo < 0) { firstNo = r.noReason; noUid = r.uid; } }
        else if (r.state == 3) ++lost;
    }
    const int v = cooptalk::TalkBuyBatchVerdict((int)g_buy.rows.size(), ok, no, lost);
    if (v == cooptalk::kTalkBuyWait) return;
    if (v == cooptalk::kTalkBuyFailed)
    {
        BuyAbandon(no > 0 ? "the owner of uid " + N(noUid) + " said no (reason " + N(firstNo) + (firstNo == coophire::kNoReserved ? ": promised to another buyer" : "") + ")"
                          : std::string("an owner's game left before it answered"));
        return;
    }
    if (blocked) return;
    BuyRun();
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
    if (p.joinType == 42 || p.joinType == 48)   /* T-478 step 6: the buying road's OK - recorded; BuyDrain takes every row at once */
    {
        if (!BuyOkSeen(m, peer)) { ++g_buyLateOk; SendDone(peer, m.reqId, m.uid, p.hirerUid, 0, 0); DebugLog("[HIRE] T478 buy uid=" + N(m.uid) + " late OK req=" + N(m.reqId) + " - the purchase ended, answered DONE taken=0"); }
        return;
    }
    ::Character* person = FindSpawned(m.uid);
    ::Character* hirer = FindSpawned(p.hirerUid);
    // review-recruit1 3c: the person is re-checked LIVE too - a death or knockout between OK and here is never recruited (PersonUp:
    // lying down awake, as a caged prisoner does, is not down); DONE {taken 0} clears the owner's promise and the owner keeps the person.
    if (!PersonUp(person) || !Alive(hirer) || !net::IsUidMine(p.hirerUid) || !IsPlayerFaction(FactionOfPod(hirer)))
    {
        ++g_hirerGone;
        DebugLog("[HIRE] take uid=" + N(m.uid) + " abandoned: person or hirer gone / down / not mine - DONE taken=0");
        SendDone(peer, m.reqId, m.uid, p.hirerUid, 0, 0);
        return;
    }
    // recruitguard.h: re-checked before the take - the person joined another player between REQ and OK (its copy is in that
    // player's stand-in here): nothing is taken, DONE {taken 0} clears the owner's promise.
    if (OwnedVerdict(person) != recruitguard::kRecruitAllow)
    {
        ++g_ownedTake;
        DebugLog("[HIRE] take uid=" + N(m.uid) + " abandoned: another player's character (fac='" + FacLog(FactionOfPod(person)) + "') - DONE taken=0");
        SendDone(peer, m.reqId, m.uid, p.hirerUid, 0, 0);
        NoticeOwned(person);   /* the grey line only: another player's character is not given a spoken line by this game */
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
    const int joinType = mine ? it->second.joinType : 0;
    if (mine) g_pending.erase(it);
    DebugLog("[HIRE] refused uid=" + N(m.uid) + " reason=" + N(m.reason) + " req=" + N(m.reqId) + (mine ? "" : " (nothing pending)"));
    if (joinType == 42 || joinType == 48) { BuyNoSeen(m); return; }   /* T-478 step 6: the buying road - BuyDrain refuses the whole purchase */
    bool owned = false;
    if (mine && m.reason == coophire::kNoFaction)   /* recruitguard.h: the owner's word for a player's character - or for a faction it cannot read */
    {
        /* "belongs to <who>" only when this game's own copy shows the person in another player's faction; otherwise the neutral
           line (the owner sends the same reason when it cannot read the person's faction) */
        ::Character* c = FindSpawned(m.uid);
        if (Plaus(c) && OwnedVerdict(c) != recruitguard::kRecruitAllow) { NoticeOwned(c); owned = true; }
        else { ChatNoticeLine(-1, std::string(), recruitguard::NotHireableLine()); ++g_ownedNoticed; }
    }
    if (mine && !owned) SayRefusal(m.uid);   /* another player's character is not given a spoken line by this game */
}

void GiveDoneSeen(unsigned int uid, unsigned int giveId);   /* T-1135: defined with the give road, below in this namespace */
void GiveHandBackSeen(unsigned int uid, unsigned int gen);   /* T-1135 review fold 1: defined with the give road, below in this namespace */
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
        /* T-1135: a given character's copy on a third game is in the GIVER's stand-in - it moves to the receiver's stand-in too */
        else if (Plaus(c) && (!IsPeerFaction(FactionOfPod(c)) || (m.giveId != 0 && FactionOfPod(c) != peerFac)))
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
    if (released == 1 && m.giveId != 0) GiveDoneSeen(m.uid, m.giveId);   /* T-1135: the giver's game saves too */
    if (m.taken == 0 && m.giveId != 0 && m.gen != 0 && net::IsUidMine(m.uid) && m.gen > net::MineGenOf(m.uid)) GiveHandBackSeen(m.uid, m.gen);   /* T-1135 review fold 1: handed back */
    DebugLog("[HIRE] finish uid=" + N(m.uid) + " taken=" + N(m.taken) + " joined=" + N(m.joined) + " released=" + N(released)
             + " factionSet=" + N(factionSet) + " faction=" + (Plaus(c) ? FactionName(FactionOfPod(c)) : std::string("?")));
    const std::string q(1, (char)34);
    SW_DEVLOG("[VERDICT] {" + q + "ev" + q + ":" + q + "hire_done" + q + "," + q + "uid" + q + ":" + N(m.uid) + "," + q + "taken" + q + ":" + N(m.taken)
             + "," + q + "joined" + q + ":" + N(m.joined) + "," + q + "released" + q + ":" + N(released) + "," + q + "factionSet" + q + ":" + N(factionSet) + "}");
}

// squadtest (TEST-ONLY), MAIN THREAD at the K2 safe point. Each member is made near the leader in the Drifters faction and hired
// by this game's player through the engine's PlayerInterface::recruit (the hire a bar recruit takes); if the engine put it in
// another of the player's squads it is moved into the leader's through the engine's setFaction. Linked to a world server with a
// leader that has a uid: the member is made under a uid owned here and announced to the other games as it stands (the adoption
// sweep's announcement). No link (single player) or no leader uid: made with no uid and announced to nobody.
// A named (unique) template is one person (n 1): a loaded body of it is the named character himself and is hired as he is; with
// none loaded and the engine's 'already made' slot empty a body is made and the slot written as the engine's own character maker
// writes it (so this world never makes a second one); with the slot held and no body loaded here he lives elsewhere in this
// world and nothing is made. One [SQUAD] line per member.
void RunSquadTest(const SquadTest& t)
{
    const std::string leaderWord = t.ownerUid != 0 ? N(t.ownerUid) : std::string("first");
    const std::string head = "[SQUAD] squadtest add leader=" + leaderWord + " n=" + N(t.n) + " template=" + t.templateSid;
    ::Character* leader = t.ownerUid != 0 ? FindSpawned(t.ownerUid) : SpawnFirstPlayerCharacter();
    const unsigned int leaderUid = t.ownerUid != 0 ? t.ownerUid : (Plaus(leader) ? FindSpawnedUid(leader) : 0);
    void* pi = PlayerInterfacePod();
    std::string name;
    void* gd = SpawnCharacterRecord(t.templateSid, &name);
    const int unique = gd != 0 ? WsTemplateIsUnique(gd) : -1;
    const char* why = 0;
    if (t.ownerUid != 0 && !net::IsUidMine(t.ownerUid)) why = "not this game's own character";
    else if (!Plaus(leader)) why = t.ownerUid != 0 ? "no character for that uid" : "no living character in this game's player faction";
    else if (!IsPlayerFaction(FactionOfPod(leader))) why = "the character is not in this game's player faction";
    else if (kRecruitRva == 0 || pi == 0) why = "recruit / PlayerInterface not in the address table";
    else if (gd == 0) why = "the sid names no CHARACTER record";
    else if (unique < 0) why = "the template's unique flag is unreadable";
    else if (unique == 1 && t.n != 1) why = "a named (unique) character is one person - n must be 1";
    if (why != 0) { DebugLog(head + " refused: " + why); return; }
    const bool linked = coop::StoreWelcomedThisLink() != 0 && coop::StoreMySlot() >= 0 && leaderUid != 0 && net::IsUidMine(leaderUid);
    std::string leaderName;
    if (!NameOf(leader, &leaderName)) leaderName = "?";
    DebugLog(head + " '" + name + "' unique=" + N(unique) + " leaderName='" + leaderName + "' leaderUid=" + N(leaderUid)
             + (linked ? " linked=1 (made under a uid owned here, then announced)" : " linked=0 (made with no uid, announced to nobody)"));
    ::Faction* fac = FactionOfPod(leader);
    void* squad = PlatoonOfPod(leader);
    int joinedN = 0, sentN = 0;
    for (int i = 0; i < t.n; ++i)
    {
        const std::string tag = "[SQUAD] member " + N(i + 1) + "/" + N(t.n) + " '" + name + "' template=" + t.templateSid + " unique=" + N(unique);
        const int madeBefore = unique == 1 ? WsUniqueSlotPod(gd) : 0;
        unsigned int uid = 0; ::Character* c = 0;
        bool original = false;
        if (unique == 1)
        {
            int dead = -1;
            ::Character* orig = (::Character*)WsLoadedBodyOf(gd, &dead);
            if (Plaus(orig))
            {
                const unsigned int ou = FindSpawnedUid(orig);
                const char* no = 0;
                if (dead != 0) no = "is dead or unreadable";
                else if (IsPlayerFaction(FactionOfPod(orig))) no = "is already in this game's player faction";
                else if (ou != 0 && !net::IsUidMine(ou)) no = "is run by another game";
                if (no != 0)
                {
                    DebugLog(tag + " alreadyMade=" + N(madeBefore) + " - the loaded named character " + no + "; nothing made");
                    continue;
                }
                c = orig; uid = ou; original = true;
            }
            else if (madeBefore != 0)
            {
                DebugLog(tag + " alreadyMade=" + N(madeBefore) + " - this world already made him and no body of him is loaded here;"
                         " no second one is made");
                continue;
            }
        }
        if (c == 0)
        {
            const float ang = 6.2831853f * (float)i / (float)t.n;
            const float dx = kSquadRing * cosf(ang), dz = kSquadRing * sinf(ang);
            if (linked)
            {
                if (!SpawnOwnLocalNear(name, t.templateSid, leaderUid, dx, dz, kSquadMadeIn, &uid, &c) || !Plaus(c)) c = 0;
            }
            else c = SpawnPlainNear(t.templateSid, name, leader, dx, dz, kSquadMadeIn);
            if (!Plaus(c))
            {
                DebugLog(tag + " alreadyMade=" + N(madeBefore) + " not made (the [M1] line says why)");
                continue;
            }
        }
        const int mark = unique == 1 ? WsUniqueMarkMade(gd) : 0;
        const int rec = RecruitPod(pi, c, false);
        int moved = 0;
        if (rec == 1 && Plaus(squad) && PlatoonOfPod(c) != squad) moved = SetFactionPod(c, fac, squad) == 1 ? 1 : -1;
        const bool joined = rec == 1 && FactionOfPod(c) == fac && PlatoonOfPod(c) == squad;
        const bool sent = linked && !original && uid != 0 && SpawnAnnounceOwn(uid);
        if (joined) ++joinedN;
        if (sent) ++sentN;
        const int madeAfter = unique == 1 ? WsUniqueSlotPod(gd) : 0;
        std::string shown;
        if (!NameOf(c, &shown)) shown = "?";
        DebugLog(tag + " name='" + shown + "' uid=" + (uid != 0 ? N(uid) : std::string("none")) + " body=" + (original ? "original" : "new")
                 + " leader=" + leaderWord + " recruit=" + N(rec) + " moved=" + N(moved) + " joined=" + N(joined ? 1 : 0)
                 + " spawnSent=" + N(sent ? 1 : 0) + " faction='" + FactionName(FactionOfPod(c)) + "' inLeaderSquad="
                 + N(PlatoonOfPod(c) == squad ? 1 : 0) + " squadCount=" + N(PlatoonCountPod(PlatoonOfPod(c)))
                 + " alreadyMade=" + N(madeBefore) + "->" + N(madeAfter) + " markWrite=" + N(mark));
    }
    DebugLog(head + " done: joined=" + N(joinedN) + " spawnSent=" + N(sentN) + " (of " + N(t.n) + ")");
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
    else if (g_testMode == 4)
    {
        /* T-1022 nearestpeer (TEST-ONLY): the nearest living character of ANOTHER player to the hirer - OwnedVerdict, the test every
           hire road refuses by - then the ordinary road below (Hold for a copy, LocalHire for a person this game runs) */
        Ogre::Vector3 hp;
        if (!SafeReadPosition(hirer, &hp)) { ++g_testNoPerson; DebugLog("[HIRE] hiretest skipped: the hirer's position is unreadable"); return; }
        float best = g_testRadius;
        const int cap = MirrorCapacity();
        for (int i = 0; i < cap; ++i)
        {
            unsigned int u = 0; ::Character* c = 0; Ogre::Vector3 q;
            if (!MirrorSlot(i, &u, &c) || u == 0 || !Plaus(c) || c == hirer) continue;
            if (OwnedVerdict(c) == recruitguard::kRecruitAllow) continue;
            if (!Alive(c) || !SafeReadPosition(c, &q)) continue;
            const float dx = q.x - hp.x, dz = q.z - hp.z, d = sqrtf(dx * dx + dz * dz);
            if (d < best || (d == best && person != 0 && u < uid)) { best = d; uid = u; person = c; }
        }
        if (person == 0) { ++g_testNoPerson; DebugLog("[HIRE] hiretest error nearestpeer: none within " + N((long long)g_testRadius) + " u of the hirer"); return; }
        DebugLog("[HIRE] hiretest nearestpeer pick uid=" + N(uid) + " faction=otherPlayer fac='" + FacLog(FactionOfPod(person)) + "' mine=" + N(net::IsUidMine(uid) ? 1 : 0)
                 + " dist=" + N((long long)best) + " radius=" + N((long long)g_testRadius));
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
// the engine runs the line unchanged - except for another player's character (recruitguard.h), whose hire line is never run
// (below). Returns true when the line was held (the original must NOT run). MAIN THREAD.
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
    /* recruitguard.h: a hire reply on ANOTHER PLAYER'S character (its copy here is in that player's stand-in) is not run - none of
       its actions (the price included) - and the chat is closed: the hire dialogue the copy still carries never takes a player's
       recruit. Without endDialogue in the table the line runs and the 0x693010 detour refuses the recruit itself. */
    if (kEndDialogueRva != 0 && Plaus(person) && OwnedVerdict(person) != recruitguard::kRecruitAllow)
    {
        ++g_ownedHeld;
        DebugLog("[HIRE] line person=" + N(uid) + " mine=" + N(mine ? 1 : 0) + " target=" + N(hirerUid) + " acts=[" + acts + "] REFUSED - another player's character (fac='"
                 + FacLog(FactionOfPod(person)) + "'); not run, chat closed by endDialogue");
        if (EndDialoguePod(dlg) != 1) { ++g_endFault; ErrorLog("[HIRE] endDialogue 0x673DA0 faulted on a refused hire of another player's character uid=" + N(uid)); }
        NoticeOwned(person);   /* the grey line only: another player's character is not given a spoken line by this game */
        return true;
    }
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

// T-1194 (T2029), TEST-ONLY peace lever: ANY THREAD, from detour_doActions before the line runs (both thread branches): true = the
// line's results are dropped (the original must NOT run). A talk line carrying attack action 6 / 55 / 57 gives the speaker a direct
// attack order on the conversation's target that never asks the planner test (T2029 Confirmed: plan asked 0, the tower's Dust
// Bandits attacked after "Intruders!"). Dropped when the lever is on and its gate (PeaceGate, the other hooks' rule) covers the
// speaker and the target; otherwise the line runs unchanged. Lever off: one flag read and nothing else. THE TALK IS NOT ENDED:
// the engine's own attack branch does not end it, the next lines run through this same check, and endDialogue mid-sayLine off
// the main thread would be a new road; only the line's actions are skipped. The person and the target are read as HoldLine reads
// them (ReadLinePod, guarded, any thread - JoinHandOffLine's read); uids by address compares. No allocation, no log here.
bool PeaceTalkAttackLine(void* dlg, void* line, int offMain)
{
    if (!coop::PeaceOnAnyThread()) return false;
    int types[16], values[16], total = 0; ::Character* person = 0; ::Character* target = 0;
    const int k = ReadLinePod(dlg, line, types, values, 16, &total, &person, &target);
    if (k < 0) { coop::PeaceTalkNote(0, -1, 1, 0u, 0u, types, values, 0, 0, offMain); return false; }
    const unsigned mask = swpeacegate::TalkAttackMask(types, k);
    if (mask == 0) return false;
    const int gate = Plaus(person) ? coop::PeaceGateAnyThread(person, Plaus(target) ? target : 0) : -1;
    const int drop = swpeacegate::TalkAttackDrop(1, mask, gate);
    const unsigned su = (drop != 0 && Plaus(person)) ? FindSpawnedUid(person) : 0u;
    const unsigned tu = (drop != 0 && Plaus(target)) ? FindSpawnedUid(target) : 0u;
    coop::PeaceTalkNote(drop, gate, 0, su, tu, types, values, k, total, offMain);
    return drop != 0;
}

void detour_doActions(void* dlg, void* line, void* a3, void* a4)
{
    const unsigned long mt = StoreMainThreadId();
    if (mt != 0 && ::GetCurrentThreadId() == mt)
    {
        if (TalkSkipDoActions(dlg)) return;   /* P26 stages 1-3: the mirrored NPC line's actions already ran on the NPC's game */
        if (PeaceTalkAttackLine(dlg, line, 0)) return;   /* T-1194 (T2029): peace - a talk attack on a covered pair, before the router forwards it */
        if (TalkDeferTargetActs(dlg, line)) return;   /* P26 stage 5: the router - a forwarded conversation's line: NPC-side here, target-side as ACT */
        if (JoinHandOffLine(dlg, line)) return;   /* T-980: our prisoner joining another player's character - that player's game hires it */
        if (HoldLine(dlg, line)) return;
    }
    else
    {
        ::InterlockedIncrement64(&g_linesOffMain);
        TalkNoteOffMainActs(dlg, line);   /* log-only: a window-opening action off the main thread */
        if (PeaceTalkAttackLine(dlg, line, 1)) return;   /* T-1194 (T2029): peace - the same drop off the main thread */
        if (JoinHandOffLine(dlg, line)) return;   /* T-980: the same hand-off when the join line runs off the main thread */
    }
    void* const s6Target = TalkS6LeaderTarget(dlg, line);   /* P26s6 S6-3: resolved before the line runs (it may end the conversation) */
    g_doActionsOrig(dlg, line, a3, a4);
    if (s6Target != 0) TalkS6LeaderOrder(dlg, s6Target);   /* P26s6 S6-3: the walk-over the engine gives a player target */
}

// ---- the engine's own recruit of a person this game runs: the other games move their copies ----
// A dialogue hire of a person this game runs (HoldLine lets it run unchanged), a freed prisoner, any other engine road: the other
// games learned the person's faction when it was announced and still show the old one. The one road for a faction change between
// games is the hire handshake's DONE {taken 0, joined 1} to every game - what the own hire sends after its recruit; Finish there
// moves the copy into this game's stand-in faction.

/* The hirer for that DONE: an own-uid character of this game's player faction in the squad the engine just put the person in, 0
   when none (Finish on the other game then uses the sender's stand-in and a new squad). MAIN THREAD. */
unsigned int SquadHirerUid(::Character* person, unsigned int personUid)
{
    void* const squad = Plaus(person) ? coop::SquadActivePlatoonOf(person) : 0;
    if (squad == 0) return 0;
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int u = 0; ::Character* c = 0;
        if (!MirrorSlot(i, &u, &c) || u == 0 || u == personUid || c == person || !net::IsUidMine(u) || !Plaus(c)) continue;
        if (!IsPlayerFaction(FactionOfPod(c))) continue;
        if (coop::SquadActivePlatoonOf(c) == squad) return u;
    }
    return 0;
}

/* The verdict on one engine recruit (recruitguard.h EngineRecruitDecide) and its DONE when it is sent. uid 0: not a character this
   game replicates - its faction is not read (a queued pointer may be stale). MAIN THREAD.
   T-1228: ret = the engine's return address into 0x693010, wasFac = the person's faction before the call, talkOpen = the talk window
   then (TalkWindowOpenNow) - the NotMine line names them (recruitguard.h NotMineTail). */
unsigned long long EngImageHi()
{
    static unsigned long long hi = 0;
    if (hi == 0)
    {
        const unsigned char* img = (const unsigned char*)::GetModuleHandleA(0);
        const IMAGE_NT_HEADERS64* nt = (const IMAGE_NT_HEADERS64*)(img + ((const IMAGE_DOS_HEADER*)img)->e_lfanew);
        hi = (unsigned long long)(uintptr_t)img + nt->OptionalHeader.SizeOfImage;
    }
    return hi;
}
std::string EngineRecruitNotMineTail(::Character* c, unsigned int uid, bool queued, unsigned long long ret, ::Faction* wasFac, int talkOpen)
{
    recruitguard::NotMineFacts f;
    f.caller = recruitguard::CallerWords(ret, (unsigned long long)Base(), EngImageHi());
    f.mainThread = queued ? 0 : 1;
    if (!NameOf(c, &f.name)) f.name.clear();
    f.race = GroundRaceName(c);
    f.templateSid = TemplateSidOf(c);
    f.wasFac = Plaus(wasFac) ? FacLog(wasFac) : std::string();
    f.squad = (unsigned long long)(uintptr_t)coop::SquadActivePlatoonOf(c);
    f.ownerSlot = net::OwnerSlotOf(uid);
    f.talkOpen = talkOpen;
    void* side = 0;
    f.tradeOpen = TradeWindowSideNow(&side);
    f.tradeWithUid = (f.tradeOpen == 1 && side != 0) ? FindSpawnedUid((::Character*)side) : 0u;
    return recruitguard::NotMineTail(f);
}
void EngineRecruitSettle(::Character* c, unsigned int uid, int wasInMine, int depth, int recruited, bool queued,
                         unsigned long long ret, ::Faction* wasFac, int talkOpen)
{
    const int nowInMine = (uid != 0 && Plaus(c) && IsPlayerFaction(FactionOfPod(c))) ? 1 : 0;
    const int uidMine = (uid != 0 && net::IsUidMine(uid)) ? 1 : 0;
    const int v = recruitguard::EngineRecruitDecide(recruited, depth, uid, uidMine, wasInMine, nowInMine);
    ++g_engVerdict[v];
    if (v == recruitguard::kEngineRecruitOwnRoad || v == recruitguard::kEngineRecruitRefused) return;   /* counted only */
    const std::string fac = (uid != 0 && Plaus(c)) ? FacLog(FactionOfPod(c)) : std::string("?");
    const std::string where = queued ? std::string(" (recruited off the main thread, settled at the safe point)") : std::string();
    if (v != recruitguard::kEngineRecruitSend)
    {
        /* T-1228: the NotMine line names the caller, the thread, the person and the windows - once per uid per caller, 50 a session */
        std::string tail;
        if (v == recruitguard::kEngineRecruitNotMine)
        {
            if (recruitguard::NotMineLineTake(&g_engNotMineSeen, uid, ret) != recruitguard::kNotMineLineWrite) return;
            ++g_engNotMineLines;
            tail = EngineRecruitNotMineTail(c, uid, queued, ret, wasFac, talkOpen);
        }
        DebugLog("[HIRE] engine recruit uid=" + N(uid) + " hirer=0 fac='" + fac + "' - hire done NOT sent: " + recruitguard::EngineRecruitWhy(v) + where + tail);
        return;
    }
    const unsigned int hirerUid = SquadHirerUid(c, uid);
    const long long before = g_doneOut;
    SendDone(net::kHireNoAsker, 0, uid, hirerUid, 0, 1);   // to every game: each moves its copy into this game's stand-in (Finish)
    const bool sent = g_doneOut != before;
    if (sent) ++g_engSent; else ++g_engSendFailed;
    DebugLog("[HIRE] engine recruit uid=" + N(uid) + " hirer=" + N(hirerUid) + " fac='" + fac + "' - "
             + (sent ? std::string("hire done sent to the other games") : std::string("hire done NOT sent: no road to the other games (send failed)")) + where);
}

/* The hand-over for a recruit the engine ran off the main thread (a dialogue line can run there): the person's pointer, its uid
   then, and whether it was in this game's player faction before the call. Filled on ANY THREAD, emptied by HireSafePointDrain on
   the MAIN THREAD, both under g_engQLock. A full queue is counted and reported at the safe point. */
struct EngRecruit { ::Character* c; unsigned int uid; int wasInMine; unsigned long long ret; ::Faction* wasFac; int talkOpen; };   /* T-1228: ret, wasFac, talkOpen for the NotMine line */
const int kEngQCap = 32;
EngRecruit g_engQ[kEngQCap];
volatile LONG g_engQN = 0;
SRWLOCK g_engQLock = SRWLOCK_INIT;

void EngineRecruitQueue(::Character* c, int wasInMine, unsigned long long ret, ::Faction* wasFac, int talkOpen)   // ANY THREAD: POD only, nothing allocated
{
    const unsigned int uid = Plaus(c) ? FindSpawnedUid(c) : 0u;   /* address compares only (spawn.h): safe on any thread */
    bool full = false;
    ::AcquireSRWLockExclusive(&g_engQLock);
    if (g_engQN < kEngQCap) { g_engQ[g_engQN].c = c; g_engQ[g_engQN].uid = uid; g_engQ[g_engQN].wasInMine = wasInMine; g_engQ[g_engQN].ret = ret; g_engQ[g_engQN].wasFac = wasFac; g_engQ[g_engQN].talkOpen = talkOpen; ++g_engQN; }
    else full = true;
    ::ReleaseSRWLockExclusive(&g_engQLock);
    if (full) ::InterlockedIncrement64(&g_engQueueFull); else ::InterlockedIncrement64(&g_engQueued);
}

void EngineRecruitDrain()   // MAIN THREAD, at the safe point
{
    EngRecruit batch[kEngQCap];
    int n = 0;
    ::AcquireSRWLockExclusive(&g_engQLock);
    n = (int)g_engQN;
    for (int i = 0; i < n; ++i) batch[i] = g_engQ[i];
    g_engQN = 0;
    ::ReleaseSRWLockExclusive(&g_engQLock);
    for (int i = 0; i < n; ++i)
    {
        const EngRecruit& q = batch[i];
        /* the pointer is read only while the spawn table still maps it to the same uid: a person gone since is not read */
        if (q.uid != 0 && FindSpawnedUid(q.c) != q.uid)
        {
            ++g_engGone;
            DebugLog("[HIRE] engine recruit uid=" + N(q.uid) + " hirer=0 fac='?' - hire done NOT sent: the person is no longer in this game's character table (recruited off the main thread)");
            continue;
        }
        EngineRecruitSettle(q.c, q.uid, q.wasInMine, 0, 1, true, q.ret, q.wasFac, q.talkOpen);
    }
    const long long full = (long long)g_engQueueFull;
    if (full != g_engFullLogged)
    {
        ErrorLog("[HIRE] engine recruit: " + N(full - g_engFullLogged) + " recruit(s) off the main thread were not queued (queue full, " + N(kEngQCap)
                 + ") - hire done NOT sent; the other games keep the old faction on those copies");
        g_engFullLogged = full;
    }
}

// ---- the 0x693010 detour (recruitguard.h): the engine's own recruit refuses another player's character ----
// Every road ends here - a dialogue line's act 3 / 18 that ran unheld, this file's own RecruitPod (the lever, TakeAndHire,
// HireRecruitNoEdit), any other engine caller. A refusal answers 0, the engine's own "not recruited" answer (squad full / limit).
// The roads above refuse earlier, so this one fires only for a road they do not see. An NPC, or this game's own character, goes to
// the engine unchanged. ANY THREAD (the log line and the chat line only on the main thread). After the engine's call, a recruit
// that did not come through RecruitPod is settled by EngineRecruitSettle - at once on the main thread, else through the queue.
// ---- T-1135: GIVING CHARACTERS TO ANOTHER PLAYER (src/common/givewire.h; owner 1349 a / 1350 a) ----
// The giver's game: the PLAYERS tab's GIVE button reads the selection (GiveButtonRead); its confirm box or the lever arms the offer
// (GiveOfferArm) and the safe point sends it (OFFER) after the same live checks. At the receiver's ACCEPT every character is checked
// live at one instant - any one that no longer qualifies ends the whole offer (RESULT failed) - else each is promised to the receiver
// (g_reserved with request id 0, as the hire owner's OK promises a person) and RESULT go carries each one's generation.
// The receiver's game: the offer waits for its box (60 s, then ANSWER timeout); ACCEPT records the accepted offer (g_giveAccepted, the
// guard exception's list) and answers; at RESULT go every copy is checked at one instant, then each person is taken over and recruited
// through the engine's own PlayerInterface::recruit at no price (the hire road's take) and the hire DONE {taken 1, joined, giveId} goes
// to every game: the giver releases the person and moves its copy into the receiver's stand-in, every other game moves its copy too
// (Finish). Both games then save (OwnGiveSaveArm: the save after character creation's road). Money is the faction's and stays.
// MAIN THREAD throughout: the PLAYERS tab's frame, the message dispatch and the K2 safe point.
struct GiveOut { unsigned int offerId; int toSlot; int state; DWORD at; std::vector<unsigned int> uids; std::vector<std::string> names; std::vector<unsigned int> gens; GiveOut() : offerId(0), toSlot(-1), state(0), at(0) {} };   /* state 1 offered (waiting for the answer), 2 go sent (waiting for done) */
struct GiveIn { swgive::GiveMsg offer; int state; DWORD at; GiveIn() : state(0), at(0) {} };   /* state 1 waiting for its box, 2 accepted (waiting for go) */
struct GiveInbound { swgive::GiveMsg m; unsigned int peer; };
struct GiveArmed { int armed; int slot; std::vector<unsigned int> uids; std::string from; GiveArmed() : armed(0), slot(-1) {} };
const size_t kGiveInMax = 4;    /* offers waiting here at once; a fifth is answered "failed" at once */
GiveOut g_giveOut;                                 /* the giver: this player's one offer in flight (offerId 0 = none) */
std::vector<GiveIn> g_giveIn;                      /* the receiver: offers waiting for the box, or accepted and waiting for go */
std::vector<GiveInbound> g_giveInbound;            /* arrival order, drained at the K2 safe point */
std::vector<swgive::Accepted> g_giveAccepted;    /* the receiver: accepted offers - the only road past the recruit guard */
GiveArmed g_giveArmed;                             /* an offer the confirm box or the lever armed, sent at the safe point */
/* T-1135 review fold 6: the giver's offers whose promises were KEPT after GO with no "done" heard (swgive::KeptRelease decides each
   character on the receiver's late "failed" or its departure); the receiver's "failed" words owed for accepted offers a world change
   dropped (sent at the safe point, tried once a second, for kGiveOwedMs) */
struct GiveKept { unsigned int offerId; int toSlot; DWORD goAt; int receiverGone; std::vector<unsigned int> uids; std::vector<unsigned int> gens; GiveKept() : offerId(0), toSlot(-1), goAt(0), receiverGone(0) {} };
struct GiveOwed { unsigned int offerId; int giverSlot; int receiverSlot; DWORD at; DWORD tried; GiveOwed() : offerId(0), giverSlot(-1), receiverSlot(-1), at(0), tried(0) {} };
const size_t kGiveKeptMax = 16;
const size_t kGiveOwedMax = 16;
const unsigned long kGiveOwedMs = 300000;
std::vector<GiveKept> g_giveKept;
std::vector<GiveOwed> g_giveOwed;
int g_giveLeverShow = 0, g_giveLeverShowSlot = -1, g_giveLeverAnswer = 0;   /* givetest show / answer (1 accept, 2 refuse), run at the safe point */
unsigned int g_giveNextId = 0;
/* the one take running now (GiveTakeAll around RecruitPod): detour_recruit lets this uid through only while it matches an accepted offer */
unsigned int g_giveTakeOffer = 0, g_giveTakeUid = 0;
int g_giveTakeGiver = -1, g_giveTakeReceiver = -1;
long long g_giveOffersOut = 0, g_giveNotSent = 0, g_giveOffersIn = 0, g_giveAccepts = 0, g_giveRefusals = 0, g_giveTimeouts = 0, g_giveRanOut = 0,
          g_giveGo = 0, g_giveFailed = 0, g_giveTakenN = 0, g_giveJoinedN = 0, g_giveDoneSeen = 0, g_giveSendFailed = 0, g_giveStale = 0,
          g_giveGuardPass = 0, g_giveCompleted = 0, g_givePeerGone = 0,
          g_giveNoRoom = 0, g_giveHandedBack = 0, g_giveHandBackSeen = 0, g_giveLateGo = 0, g_giveKeptUnknown = 0;   /* T-1135 review fold: no room at the limit, recruits handed back (receiver / giver), GOs answered failed, promises kept with the outcome unknown */

std::string GiveUids(const std::vector<unsigned int>& u)
{
    std::string s;
    for (size_t i = 0; i < u.size(); ++i) s += (i == 0 ? "" : ",") + N(u[i]);
    return s;
}
/* a character's two bytes: knocked out (+0x5B9) and dead (+0x5BC), as PersonUp reads them. 1 read, 0 fault. No C++ object. */
int KoDeadPod(::Character* c, int* ko, int* dead)
{
    __try { const unsigned char* const p = (const unsigned char*)c; *ko = p[0x5B9] != 0 ? 1 : 0; *dead = p[0x5BC] != 0 ? 1 : 0; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* what keeps one character from being given now (swgive::CharBlock): knocked out, combat mode, carried (+0x3D4), bed / cage (+0x2F8) */
int GiveCharBlock(::Character* c)
{
    if (!Plaus(c)) return swgive::kBlockGone;
    int ko = -1, dead = -1;
    if (KoDeadPod(c, &ko, &dead) != 1) { ko = -1; dead = -1; }
    return swgive::CharBlock(ko, dead, InCombatPod(c), CharCarried(c), InSomethingPod(c));
}
std::string GiveNameOf(::Character* c)
{
    std::string s;
    if (!Plaus(c) || !NameOf(c, &s)) s.clear();
    if (s.size() > swgive::kGiveNameMax) s.resize(swgive::kGiveNameMax);
    return s;
}
/* this game's own characters that are not dead: the ones the at-least-one rule counts */
int GiveOwnCount()
{
    int n = 0;
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int u = 0; ::Character* c = 0;
        if (!MirrorSlot(i, &u, &c) || u == 0 || !net::IsUidMine(u) || !Plaus(c) || !IsPlayerFaction(FactionOfPod(c))) continue;
        int ko = 0, dead = 0;
        if (KoDeadPod(c, &ko, &dead) == 1 && dead != 0) continue;
        ++n;
    }
    return n;
}
/* each uid read live: its block reason (not this game's own player character, not loaded, or already promised: kBlockGone) and name */
void GiveReadUids(const std::vector<unsigned int>& uids, std::vector<int>* reasons, std::vector<std::string>* names)
{
    reasons->clear(); names->clear();
    for (size_t i = 0; i < uids.size(); ++i)
    {
        ::Character* c = uids[i] != 0 ? FindSpawned(uids[i]) : 0;
        const bool mine = uids[i] != 0 && net::IsUidMine(uids[i]) && Plaus(c) && IsPlayerFaction(FactionOfPod(c));
        const bool promised = g_reserved.find(uids[i]) != g_reserved.end() || g_pending.find(uids[i]) != g_pending.end();
        reasons->push_back((!mine || promised) ? swgive::kBlockGone : GiveCharBlock(c));
        names->push_back(GiveNameOf(c));
    }
}
/* the other player is in the world (the PLAYERS roster's IN_WORLD) and is not this player */
bool GiveReceiverInWorld(int slot)
{
    return slot >= 0 && slot != StoreMySlot() && StoreRosterSlotInWorld(slot) == 1;
}
std::string GiveMyName() { return TeamPlayerName(StoreMySlot()); }
void GiveLine(int slot, const std::string& sentence)
{
    if (EngineWritesBlocked()) return;
    ChatNoticeLine(slot, TeamPlayerName(slot), sentence);
}
bool GiveSend(const swgive::GiveMsg& m, int toSlot)
{
    if (net::SendGive(m, toSlot)) return true;
    ++g_giveSendFailed;
    DebugLog("[GIVE] send kind=" + N(m.kind) + " offer=" + N(m.offerId) + " to slot " + N(toSlot) + " FAILED (no road, or the player is not in the world)");
    return false;
}
void GiveSendWord(int kind, unsigned int offerId, int giverSlot, int receiverSlot, int word, int index, int toSlot)
{
    swgive::GiveMsg m; m.kind = kind; m.offerId = offerId; m.giverSlot = giverSlot; m.receiverSlot = receiverSlot; m.word = word; m.index = index;
    GiveSend(m, toSlot);
}
/* the giver: the promises made at RESULT go that no DONE has cleared */
void GiveReleaseReserved()
{
    for (size_t i = 0; i < g_giveOut.uids.size(); ++i)
    {
        std::map<unsigned int, Reserved>::iterator r = g_reserved.find(g_giveOut.uids[i]);
        if (r != g_reserved.end() && r->second.reqId == 0 && r->second.slot == g_giveOut.toSlot) g_reserved.erase(r);
    }
}
void GiveOutClear() { g_giveOut = GiveOut(); }
void GiveForgetAccepted(unsigned int offerId, int giverSlot)
{
    for (size_t i = 0; i < g_giveAccepted.size(); )
    {
        if (g_giveAccepted[i].offerId == offerId && g_giveAccepted[i].giverSlot == giverSlot) g_giveAccepted.erase(g_giveAccepted.begin() + (std::ptrdiff_t)i);
        else ++i;
    }
}
/* T-1135 review fold 6: the giver's offer ends after GO with no word on the take (timed out, or the receiver left): its kept promises
   are listed so the receiver's late "failed" or its departure can release them. An offer past "done" lists nothing (each character's
   hire DONE decides it); past kGiveKeptMax the oldest entry goes and its promises simply stay kept. */
void GiveKeptAdd(int receiverGone)
{
    if (g_giveOut.offerId == 0 || g_giveOut.state != swgive::kOutGoSent) return;
    if (g_giveKept.size() >= kGiveKeptMax)
    {
        DebugLog("[GIVE] kept list full: kept offer=" + N(g_giveKept[0].offerId) + " leaves the list - its promises stay kept for this world");
        g_giveKept.erase(g_giveKept.begin());
    }
    GiveKept k;
    k.offerId = g_giveOut.offerId; k.toSlot = g_giveOut.toSlot; k.goAt = g_giveOut.at; k.receiverGone = receiverGone; k.uids = g_giveOut.uids; k.gens = g_giveOut.gens;
    g_giveKept.push_back(k);
}
/* one kept offer's characters that swgive::KeptRelease lets go: their promises erased here; the uids released are added to *list */
int GiveKeptRelease(const GiveKept& k, int failedWord, int windowOver, std::string* list)
{
    int n = 0;
    for (size_t i = 0; i < k.uids.size(); ++i)
    {
        const unsigned int uid = k.uids[i];
        std::map<unsigned int, Reserved>::iterator r = g_reserved.find(uid);
        const int promised = (r != g_reserved.end() && r->second.reqId == 0 && r->second.slot == k.toSlot) ? 1 : 0;
        const int genSame = (i < k.gens.size() && k.gens[i] != 0 && net::MineGenOf(uid) == k.gens[i]) ? 1 : 0;
        if (swgive::KeptRelease(0, promised, net::IsUidMine(uid) ? 1 : 0, genSame, net::OwnerMovedClaimed(uid), failedWord, k.receiverGone, windowOver) != 1) continue;
        g_reserved.erase(r);
        ++n;
        *list += (list->empty() ? "" : ",") + N(uid);
    }
    return n;
}
/* the receiver's "failed" for an offer whose promises were kept: it took nothing of that offer, so the kept promises the rule lets go
   are released and the entry ends */
void GiveKeptOnFailed(unsigned int offerId, int fromSlot)
{
    for (size_t i = 0; i < g_giveKept.size(); ++i)
    {
        if (g_giveKept[i].offerId != offerId || g_giveKept[i].toSlot != fromSlot) continue;
        const GiveKept k = g_giveKept[i];
        g_giveKept.erase(g_giveKept.begin() + (std::ptrdiff_t)i);
        std::string rel;
        const int n = GiveKeptRelease(k, 1, 0, &rel);
        DebugLog("[GIVE] late failed for kept offer=" + N(offerId) + " from slot " + N(fromSlot) + " - the receiver took nothing of it: " + N(n) + " of "
                 + N((long long)k.uids.size()) + " kept promise(s) released [" + rel + "]; any other was cleared by its hire DONE, moved, or claimed, and stays as it is");
        return;
    }
}
/* every frame: an entry none of whose characters is still promised (each hire DONE came) ends; an entry whose receiver left is decided
   once the GO's wait is over - a word of the receiver's still on its way has had that long to arrive */
void GiveKeptSweep(DWORD now)
{
    for (size_t i = 0; i < g_giveKept.size(); )
    {
        const GiveKept k = g_giveKept[i];
        int left = 0;
        for (size_t j = 0; j < k.uids.size(); ++j)
        {
            std::map<unsigned int, Reserved>::const_iterator r = g_reserved.find(k.uids[j]);
            if (r != g_reserved.end() && r->second.reqId == 0 && r->second.slot == k.toSlot) ++left;
        }
        const bool windowOver = swgive::Expired(now, k.goAt, swgive::kOfferMs + swgive::kGraceMs);
        if (left != 0 && !(k.receiverGone != 0 && windowOver)) { ++i; continue; }
        g_giveKept.erase(g_giveKept.begin() + (std::ptrdiff_t)i);
        if (left == 0) { DebugLog("[GIVE] kept offer=" + N(k.offerId) + " to slot " + N(k.toSlot) + ": every kept promise was cleared by its hire DONE - the entry ends"); continue; }
        std::string rel;
        const int n = GiveKeptRelease(k, 0, 1, &rel);
        DebugLog("[GIVE] kept offer=" + N(k.offerId) + " to slot " + N(k.toSlot) + ": the receiver left and no hire DONE or OWNER_MOVED came by the end of the GO's wait - "
                 + N(n) + " of " + N(left) + " kept promise(s) released [" + rel + "]; any other stays kept (no longer run here at its GO generation, or claimed by another game)");
    }
}
/* the receiver, at the safe point: each owed "failed" sent (tried once a second while there is no road); one not sent in kGiveOwedMs
   is dropped - the giver's departure rule (GiveKeptSweep) then decides its promises */
void GiveOwedSend(DWORD now)
{
    for (size_t i = 0; i < g_giveOwed.size(); )
    {
        const GiveOwed o = g_giveOwed[i];
        const std::string who = "offer=" + N(o.offerId) + " from slot " + N(o.giverSlot);
        if (swgive::Expired(now, o.at, kGiveOwedMs))
        {
            g_giveOwed.erase(g_giveOwed.begin() + (std::ptrdiff_t)i);
            DebugLog("[GIVE] owed failed for " + who + " not sent in " + N((long long)(kGiveOwedMs / 1000)) + " s - dropped");
            continue;
        }
        if (!swgive::Expired(now, o.tried, 1000)) { ++i; continue; }
        g_giveOwed[i].tried = now;
        swgive::GiveMsg m;
        m.kind = swgive::kGiveAnswer; m.offerId = o.offerId; m.giverSlot = o.giverSlot; m.receiverSlot = o.receiverSlot; m.word = swgive::kAnsFailed; m.index = swgive::kNoIndex;
        if (!net::SendGive(m, o.giverSlot)) { ++i; continue; }
        g_giveOwed.erase(g_giveOwed.begin() + (std::ptrdiff_t)i);
        DebugLog("[GIVE] owed failed sent for " + who + " - nothing of it was taken here");
    }
}

/* The giver, at the safe point: the armed offer sent after the button's own checks, read again now. */
void GiveSendOffer(const GiveArmed& a)
{
    std::vector<int> reasons; std::vector<std::string> names;
    GiveReadUids(a.uids, &reasons, &names);
    const bool inWorld = GiveReceiverInWorld(a.slot);
    const bool busy = g_giveOut.offerId != 0;
    const swgive::ButtonView v = swgive::ButtonFor(reasons, names, GiveOwnCount(), inWorld, busy);
    const std::string who = "slot " + N(a.slot) + " uids=[" + GiveUids(a.uids) + "] (" + a.from + ")";
    if (v.state != swgive::kButtonLit)
    {
        ++g_giveNotSent;
        const std::string why = !v.line.empty() ? v.line : busy ? std::string("an offer of this player's still waits") : !inWorld ? std::string("that player is not in the world")
                              : std::string("nothing to give, or over ") + N(swgive::kGiveMax) + " characters";
        DebugLog("[GIVE] offer to " + who + " NOT sent: " + why);
        return;
    }
    if (g_giveNextId == 0) g_giveNextId = (unsigned int)::GetTickCount() | 1u;   /* a new number each process: an old offer's answer never matches */
    swgive::GiveMsg m;
    m.kind = swgive::kGiveOffer; m.offerId = g_giveNextId++; m.giverSlot = StoreMySlot(); m.receiverSlot = a.slot; m.uids = a.uids; m.names = names;
    if (g_giveNextId == 0) g_giveNextId = 1;
    if (!GiveSend(m, a.slot)) { ++g_giveNotSent; DebugLog("[GIVE] offer to " + who + " NOT sent: no road"); return; }
    g_giveOut.offerId = m.offerId; g_giveOut.toSlot = a.slot; g_giveOut.state = 1; g_giveOut.at = ::GetTickCount(); g_giveOut.uids = a.uids; g_giveOut.names = names;
    ++g_giveOffersOut;
    DebugLog("[GIVE] offer=" + N(m.offerId) + " sent to " + who + " names='" + swgive::NamesList(names) + "' - waiting for the answer");
}

/* The receiver: a box answer (or the lever's) on the offer at index i. */
int GiveAnswerAt(size_t i, bool accept, const std::string& from)
{
    const GiveIn g = g_giveIn[i];
    swgive::GiveMsg a;
    a.kind = swgive::kGiveAnswer; a.offerId = g.offer.offerId; a.giverSlot = g.offer.giverSlot; a.receiverSlot = g.offer.receiverSlot;
    a.word = accept ? swgive::kAnsAccept : swgive::kAnsRefuse;
    const bool sent = GiveSend(a, g.offer.giverSlot);
    const std::string who = "offer=" + N(g.offer.offerId) + " from slot " + N(g.offer.giverSlot) + " uids=[" + GiveUids(g.offer.uids) + "] (" + from + ")";
    if (sent && accept)
    {
        swgive::Accepted acc;
        acc.offerId = g.offer.offerId; acc.giverSlot = g.offer.giverSlot; acc.receiverSlot = g.offer.receiverSlot; acc.uids = g.offer.uids;
        g_giveAccepted.push_back(acc);
        g_giveIn[i].state = 2; g_giveIn[i].at = ::GetTickCount();
        ++g_giveAccepts;
        DebugLog("[GIVE] ACCEPT " + who + " - waiting for the giver's game to check them");
        return 1;
    }
    g_giveIn.erase(g_giveIn.begin() + (std::ptrdiff_t)i);
    if (!sent) { GiveLine(g.offer.giverSlot, swgive::FailedLine(swgive::NameAt(g.offer.names, 0))); DebugLog("[GIVE] answer to " + who + " NOT sent - the offer ends here"); return 0; }
    ++g_giveRefusals;
    GiveLine(g.offer.giverSlot, swgive::RefusedLine(GiveMyName(), g.offer.names));
    DebugLog("[GIVE] REFUSE " + who);
    return 1;
}

/* The receiver, at RESULT go: every copy checked at one instant, and room at the game's character limit for all of them (the engine's
   recruit refuses when the player faction's living characters are not below that limit - swgive::RoomForAll), so a refusal never
   splits the offer (T-1135 review fold 1); then each person is taken over and recruited (the hire road's take). A recruit the engine
   refuses all the same (its count differs from this game's) hands that person straight back to the giver - ownership released to the
   giver's game at this game's gen + 1, the copy a puppet again - and nobody after it is taken: each of their hire DONEs says taken 0,
   so the giver keeps them. The ones recruited before it stay joined: undoing a finished recruit would be a second give the other way.
   Every character of the offer gets its hire DONE - the giver's promise of each is cleared only by it (review fold 5).
   giverPeer: the key the giver's RESULT came from - the hand-back's new owner. */
void GiveTakeAll(const swgive::GiveMsg& offer, const std::vector<unsigned int>& gens, unsigned int giverPeer)
{
    const int me = StoreMySlot();
    const std::string who = "offer=" + N(offer.offerId) + " from slot " + N(offer.giverSlot);
    void* pi = PlayerInterfacePod();
    int fail = -1;
    std::string why;
    for (size_t i = 0; i < offer.uids.size() && fail < 0; ++i)
    {
        const unsigned int uid = offer.uids[i];
        ::Character* c = FindSpawned(uid);
        if (!Plaus(c) || !PersonUp(c) || net::IsUidMine(uid) || net::OwnerSlotOf(uid) != offer.giverSlot
            || !swgive::GiveGuardPass(g_giveAccepted, offer.offerId, offer.giverSlot, me, uid))
        {
            fail = (int)i;
            why = "uid " + N(uid) + " is not here, down, or no longer the giver's";
        }
    }
    if (fail < 0 && (pi == 0 || kRecruitRva == 0 || !net::SideRoadOpen()))
    {
        fail = swgive::kNoIndex;
        why = "no PlayerInterface / recruit address, or no road to the other games";
    }
    if (fail < 0)
    {
        int alive = 0, limit = 0;
        const int limitRead = OwnLivingAndLimit(&alive, &limit);
        const int room = swgive::RoomForAll(limitRead, limit, alive, (int)offer.uids.size());
        if (room == 0)
        {
            fail = swgive::kNoIndex; ++g_giveNoRoom;
            why = "the game's character limit: " + N(alive) + " living here + " + N((long long)offer.uids.size()) + " given would pass the limit " + N(limit);
        }
        else if (room < 0) DebugLog("[GIVE] take " + who + ": the game's character limit did not read - not judged; a recruit the engine refuses is handed back");
    }
    if (fail >= 0)
    {
        ++g_giveFailed;
        GiveForgetAccepted(offer.offerId, offer.giverSlot);
        GiveSendWord(swgive::kGiveAnswer, offer.offerId, offer.giverSlot, me, swgive::kAnsFailed, fail, offer.giverSlot);
        GiveLine(offer.giverSlot, swgive::FailedLine(swgive::NameAt(offer.names, fail)));
        DebugLog("[GIVE] take " + who + " NOT done: " + why + " - nothing taken");
        return;
    }
    swgive::GiveMsg done;
    done.kind = swgive::kGiveAnswer; done.offerId = offer.offerId; done.giverSlot = offer.giverSlot; done.receiverSlot = me; done.word = swgive::kAnsDone;
    std::vector<std::string> notJoined;
    int refusedAt = -1;   /* the character whose recruit the engine refused: it and everyone after it stay the giver's */
    int takenHere = 0;
    for (size_t i = 0; i < offer.uids.size(); ++i)
    {
        const unsigned int uid = offer.uids[i];
        const std::string nm = i < offer.names.size() ? offer.names[i] : std::string();
        coophire::HireMsg d;
        d.kind = coophire::kHireDone; d.reqId = 0; d.uid = uid; d.giveId = offer.offerId;
        if (refusedAt >= 0)
        {   /* never taken: the giver's game keeps this one; its DONE clears the promise there */
            d.taken = 0; d.joined = 0; d.gen = 0;
            if (Send(d, giverPeer)) ++g_doneOut;
            notJoined.push_back(nm);
            DebugLog("[GIVE] take " + who + " uid=" + N(uid) + " NOT taken: the recruit of uid " + N(offer.uids[(size_t)refusedAt]) + " was refused - DONE taken=0 to the giver");
            continue;
        }
        ::Character* c = FindSpawned(uid);
        void* oldPlatoon = PlatoonOfPod(c);
        net::TakeLocalOwner(uid, net::GenForTake(uid, i < gens.size() ? gens[i] : 0u));
        UnpuppetForOwnership(uid);
        ++g_taken; ++g_giveTakenN;
        g_giveTakeOffer = offer.offerId; g_giveTakeGiver = offer.giverSlot; g_giveTakeReceiver = me; g_giveTakeUid = uid;
        const int rec = RecruitPod(pi, c, false);
        g_giveTakeOffer = 0; g_giveTakeUid = 0; g_giveTakeGiver = -1; g_giveTakeReceiver = -1;
        const bool inPlayerFaction = IsPlayerFaction(FactionOfPod(c));
        if (rec != 1 && !inPlayerFaction)
        {   /* the engine refused: handed straight back - this game stops running it; the giver's game holds it at the gen in the DONE */
            ++g_recruitRefused; ++g_giveHandedBack;
            refusedAt = (int)i;
            net::ReleaseLocalOwner(uid, giverPeer, 0);
            WorldStateOnOwnershipReleased(uid, c);
            AdoptRemotePuppet(uid);
            d.taken = 0; d.joined = 0; d.gen = net::CopyGenOf(uid);
            if (Send(d, giverPeer)) ++g_doneOut;
            notJoined.push_back(nm);
            DebugLog("[GIVE] take " + who + " uid=" + N(uid) + " recruit=" + N(rec) + " REFUSED by the engine - handed back to the giver at gen " + N(d.gen)
                     + "; nobody after it is taken - DONE taken=0 to the giver");
            continue;
        }
        ++takenHere;
        if (rec == 1) ForgetEmptiedCopySquad(oldPlatoon); else ++g_recruitRefused;
        const int joined = (rec == 1 && inPlayerFaction) ? 1 : 0;
        const unsigned int hirerUid = joined ? SquadHirerUid(c, uid) : 0u;
        d.hirerUid = hirerUid; d.taken = 1; d.joined = joined; d.gen = net::MineGenOf(uid);
        if (Send(d, net::kHireNoAsker)) ++g_doneOut;   /* taken: to every game (coophire::HireDoneToEveryGame) */
        DebugLog("[GIVE] take " + who + " uid=" + N(uid) + " recruit=" + N(rec) + " joined=" + N(joined) + " squadMate=" + N(hirerUid)
                 + " fac='" + FacLog(FactionOfPod(c)) + "' - DONE to every game");
        if (joined) { ++g_giveJoinedN; done.uids.push_back(uid); done.names.push_back(nm); }
        else notJoined.push_back(nm);
    }
    GiveForgetAccepted(offer.offerId, offer.giverSlot);
    GiveSend(done, offer.giverSlot);
    ++g_giveCompleted;
    if (!done.names.empty()) GiveLine(offer.giverSlot, swgive::JoinedLine(done.names, GiveMyName()));
    for (size_t i = 0; i < notJoined.size(); ++i) GiveLine(offer.giverSlot, swgive::FailedLine(notJoined[i]));
    if (takenHere > 0) OwnGiveSaveArm("received offer " + N(offer.offerId) + " from slot " + N(offer.giverSlot));
}

/* The giver: the receiver's answer. Each word counts only in the state it answers (swgive::AnswerFits): ACCEPT, REFUSE and the box's
   timeout while the offer waits for its answer; "failed" until "done" (the receiver's game takes nothing after it: the promises go);
   "done" only after GO - the offer then waits in state 3 until every character's hire DONE has cleared its promise (Finish), so the
   GIVE button stays busy and no character of it is offered again while this game may still run it (T-1135 review fold 5). */
void GiveOnAnswer(const swgive::GiveMsg& m, int fromSlot)
{
    if (g_giveOut.offerId == 0 || m.offerId != g_giveOut.offerId || fromSlot != g_giveOut.toSlot)
    {
        ++g_giveStale;
        DebugLog("[GIVE] answer word=" + N(m.word) + " for offer=" + N(m.offerId) + " from slot " + N(fromSlot) + " - no such offer of this player's waits");
        if (m.word == swgive::kAnsAccept) GiveSendWord(swgive::kGiveResult, m.offerId, m.giverSlot, m.receiverSlot, swgive::kResFailed, swgive::kNoIndex, fromSlot);
        if (m.word == swgive::kAnsFailed) GiveKeptOnFailed(m.offerId, fromSlot);   /* T-1135 review fold 6: a late failed releases kept promises */
        return;
    }
    const int to = g_giveOut.toSlot;
    const std::string recv = TeamPlayerName(to);
    const std::string who = "offer=" + N(m.offerId) + " to slot " + N(to) + " uids=[" + GiveUids(g_giveOut.uids) + "]";
    if (!swgive::AnswerFits(g_giveOut.state, m.word))
    {
        ++g_giveStale;
        DebugLog("[GIVE] answer word=" + N(m.word) + " for " + who + " in state " + N(g_giveOut.state) + " - ignored");
        return;
    }
    if (m.word == swgive::kAnsAccept)
    {
        std::vector<int> reasons; std::vector<std::string> names;
        GiveReadUids(g_giveOut.uids, &reasons, &names);
        const int fail = swgive::HandOverFails(reasons, GiveOwnCount(), GiveReceiverInWorld(to));
        if (fail >= 0)
        {
            ++g_giveFailed;
            GiveSendWord(swgive::kGiveResult, m.offerId, m.giverSlot, to, swgive::kResFailed, fail, to);
            GiveLine(to, swgive::FailedLine(swgive::NameAt(g_giveOut.names, fail)));
            DebugLog("[GIVE] ACCEPT for " + who + " - uid " + N((size_t)fail < g_giveOut.uids.size() ? g_giveOut.uids[(size_t)fail] : 0u) + " no longer qualifies (reason "
                     + N((size_t)fail < reasons.size() ? reasons[(size_t)fail] : -1) + ") - nothing is given");
            GiveOutClear();
            return;
        }
        swgive::GiveMsg go;
        go.kind = swgive::kGiveResult; go.offerId = m.offerId; go.giverSlot = m.giverSlot; go.receiverSlot = to; go.word = swgive::kResGo;
        go.uids = g_giveOut.uids; go.names = g_giveOut.names;
        for (size_t i = 0; i < g_giveOut.uids.size(); ++i)
        {
            Reserved r; r.reqId = 0; r.hirerUid = 0; r.peer = net::kHireNoAsker; r.slot = to;   /* request id 0: a give's promise */
            g_reserved[g_giveOut.uids[i]] = r;
            go.gens.push_back(net::MineGenOf(g_giveOut.uids[i]));
        }
        if (!GiveSend(go, to))
        {
            GiveReleaseReserved();
            GiveLine(to, swgive::FailedLine(swgive::NameAt(g_giveOut.names, 0)));
            DebugLog("[GIVE] ACCEPT for " + who + " - the go could not be sent; nothing is given");
            GiveOutClear();
            return;
        }
        g_giveOut.state = swgive::kOutGoSent; g_giveOut.at = ::GetTickCount(); g_giveOut.gens = go.gens;   /* the gens the GO named: a kept promise is released only at the same gen (review fold 6) */
        ++g_giveGo;
        DebugLog("[GIVE] ACCEPT for " + who + " - every character passed; promised and GO sent (gens " + GiveUids(go.gens) + ")");
        return;
    }
    if (m.word == swgive::kAnsRefuse) { ++g_giveRefusals; GiveLine(to, swgive::RefusedLine(recv, g_giveOut.names)); DebugLog("[GIVE] REFUSED " + who); GiveOutClear(); return; }
    if (m.word == swgive::kAnsTimeout) { ++g_giveTimeouts; GiveLine(to, swgive::RanOutLine(g_giveOut.names)); DebugLog("[GIVE] the receiver's box ran out for " + who); GiveOutClear(); return; }
    if (m.word == swgive::kAnsFailed)
    {
        ++g_giveFailed;
        GiveReleaseReserved();
        GiveLine(to, swgive::FailedLine(swgive::NameAt(g_giveOut.names, m.index)));
        DebugLog("[GIVE] the receiver's game could not take " + who + " (index " + N(m.index) + ") - promises released, nothing given");
        GiveOutClear();
        return;
    }
    /* kAnsDone: the uids that joined; the rest could not be given (each one's hire DONE taken 0 keeps it here) */
    std::vector<std::string> joined, rest;
    for (size_t i = 0; i < g_giveOut.uids.size(); ++i)
    {
        bool in = false;
        for (size_t k = 0; k < m.uids.size(); ++k) if (m.uids[k] == g_giveOut.uids[i]) in = true;
        (in ? joined : rest).push_back(i < g_giveOut.names.size() ? g_giveOut.names[i] : std::string());
    }
    if (!joined.empty()) GiveLine(to, swgive::JoinedLine(joined, recv));
    for (size_t i = 0; i < rest.size(); ++i) GiveLine(to, swgive::FailedLine(rest[i]));
    g_giveOut.state = swgive::kOutDoneHeard; g_giveOut.at = ::GetTickCount();
    DebugLog("[GIVE] DONE " + who + " joined=[" + GiveUids(m.uids) + "] notJoined=" + N((long long)rest.size()) + " - the offer ends when every character's hire DONE has cleared its promise");
}

/* The receiver: the giver's result. A GO is carried out only for an offer accepted here whose accepted wait has not run out
   (swgive::GoTakes); a GO for any other offer - its wait ran out and was answered "failed", or it is not known here - is answered
   "failed" (again), and nothing is taken here after that word, so the giver's promises may go (T-1135 review fold 2). peer: the key
   the result came from (the giver's game). */
void GiveOnResult(const swgive::GiveMsg& m, int fromSlot, unsigned int peer, DWORD now)
{
    size_t at = g_giveIn.size();
    for (size_t i = 0; i < g_giveIn.size(); ++i)
        if (g_giveIn[i].offer.offerId == m.offerId && g_giveIn[i].offer.giverSlot == fromSlot) { at = i; break; }
    if (at == g_giveIn.size())
    {
        ++g_giveStale;
        const bool go = m.word == swgive::kResGo;
        if (go) { ++g_giveLateGo; GiveSendWord(swgive::kGiveAnswer, m.offerId, fromSlot, m.receiverSlot, swgive::kAnsFailed, swgive::kNoIndex, fromSlot); }
        DebugLog("[GIVE] result word=" + N(m.word) + " for offer=" + N(m.offerId) + " from slot " + N(fromSlot) + " - no such offer here"
                 + (go ? std::string(" - answered failed, nothing taken") : std::string()));
        return;
    }
    const GiveIn g = g_giveIn[at];
    g_giveIn.erase(g_giveIn.begin() + (std::ptrdiff_t)at);
    const std::string who = "offer=" + N(m.offerId) + " from slot " + N(fromSlot);
    if (m.word == swgive::kResOver) { ++g_giveRanOut; GiveForgetAccepted(m.offerId, fromSlot); GiveLine(fromSlot, swgive::RanOutLine(g.offer.names)); DebugLog("[GIVE] the giver's offer ran out: " + who); return; }
    if (m.word == swgive::kResFailed)
    {
        ++g_giveFailed;
        GiveForgetAccepted(m.offerId, fromSlot);
        GiveLine(fromSlot, swgive::FailedLine(swgive::NameAt(g.offer.names, m.index)));
        DebugLog("[GIVE] the giver's check failed for " + who + " (index " + N(m.index) + ") - nothing given");
        return;
    }
    if (!swgive::GoTakes(g.state, now, g.at) || m.uids != g.offer.uids)
    {
        ++g_giveStale;
        if (g.state == 2) ++g_giveLateGo;
        GiveForgetAccepted(m.offerId, fromSlot);
        GiveSendWord(swgive::kGiveAnswer, m.offerId, fromSlot, g.offer.receiverSlot, swgive::kAnsFailed, swgive::kNoIndex, fromSlot);
        GiveLine(fromSlot, swgive::FailedLine(swgive::NameAt(g.offer.names, 0)));
        DebugLog("[GIVE] GO for " + who + " that was not accepted here, came after the accepted wait ran out, or names other characters - answered failed, nothing taken");
        return;
    }
    GiveTakeAll(g.offer, m.gens, peer);
}

/* The giver, from Finish: the hire DONE of a given person released it here - this game saves too. */
void GiveDoneSeen(unsigned int uid, unsigned int giveId)
{
    ++g_giveDoneSeen;
    DebugLog("[GIVE] DONE seen uid=" + N(uid) + " offer=" + N(giveId) + " - released to the receiver; this game's save is armed");
    OwnGiveSaveArm("gave uid " + N(uid) + " (offer " + N(giveId) + ")");
}

/* The giver, from Finish (T-1135 review fold 1): the receiver's game handed a given character back (the engine refused its recruit) and
   released it to this game at gen - this game, which never stopped running it, holds that gen from now, so every game's copy record of
   it (the receiver's release word named that gen) agrees with what this game sends. */
void GiveHandBackSeen(unsigned int uid, unsigned int gen)
{
    ++g_giveHandBackSeen;
    net::TakeLocalOwner(uid, gen);
    DebugLog("[GIVE] uid=" + N(uid) + " handed back by the receiver's game (its recruit was refused) - this game keeps it, now at gen " + N(gen));
}

/* the lever's [GIVE] show lines */
void GiveShowLines(int slot)
{
    if (slot >= 0)
    {
        swgive::ButtonView v; std::vector<unsigned int> uids; std::vector<std::string> names;
        GiveButtonRead(slot, &v, &uids, &names);
        std::string sel;
        for (size_t i = 0; i < uids.size(); ++i) sel += (i == 0 ? "" : ",") + N(uids[i]) + ":" + (i < names.size() ? names[i] : std::string());
        DebugLog("[GIVE] show slot=" + N(slot) + " button=" + (v.state == swgive::kButtonLit ? "lit" : v.state == swgive::kButtonGrey ? "greyed" : "hidden")
                 + " caption='" + v.caption + "' line='" + v.line + "' selected=[" + sel + "] own=" + N(GiveOwnCount()) + " inWorld=" + N(GiveReceiverInWorld(slot) ? 1 : 0));
    }
    DebugLog("[GIVE] show out offer=" + N(g_giveOut.offerId) + " to=" + N(g_giveOut.toSlot) + " state=" + (g_giveOut.state == swgive::kOutOffered ? "offered" : g_giveOut.state == swgive::kOutGoSent ? "go-sent"
             : g_giveOut.state == swgive::kOutDoneHeard ? "done-heard" : "none")
             + " uids=[" + GiveUids(g_giveOut.uids) + "] ageMs=" + N(g_giveOut.offerId != 0 ? (long long)(DWORD)(::GetTickCount() - g_giveOut.at) : 0LL));
    for (size_t i = 0; i < g_giveIn.size(); ++i)
        DebugLog("[GIVE] show in offer=" + N(g_giveIn[i].offer.offerId) + " from=" + N(g_giveIn[i].offer.giverSlot) + " state=" + (g_giveIn[i].state == 1 ? "waiting" : "accepted")
                 + " uids=[" + GiveUids(g_giveIn[i].offer.uids) + "] names='" + swgive::NamesList(g_giveIn[i].offer.names) + "' ageMs=" + N((long long)(DWORD)(::GetTickCount() - g_giveIn[i].at)));
    if (g_giveIn.empty()) DebugLog("[GIVE] show in none");
    DebugLog("[GIVE] show accepted=" + N((long long)g_giveAccepted.size()));
}

/* the giver: how many characters of its offer are still promised (request id 0, to the offer's receiver) */
int GiveStillPromised()
{
    int n = 0;
    for (size_t i = 0; i < g_giveOut.uids.size(); ++i)
    {
        std::map<unsigned int, Reserved>::const_iterator r = g_reserved.find(g_giveOut.uids[i]);
        if (r != g_reserved.end() && r->second.reqId == 0 && r->second.slot == g_giveOut.toSlot) ++n;
    }
    return n;
}

/* the giver's offer after the receiver's "done" (T-1135 review fold 5): over once no character of it is still promised - each promise
   cleared by that character's hire DONE in Finish, after Finish released it or kept it - then this game saves. Hire DONEs still
   missing 60 s after the "done": their promises are KEPT for this world (the take may have happened; Finish clears each one if its
   DONE comes) and the offer ends. */
void GiveSettle(DWORD now)
{
    if (g_giveOut.offerId == 0 || g_giveOut.state != swgive::kOutDoneHeard) return;
    const int left = GiveStillPromised();
    const std::string who = "offer=" + N(g_giveOut.offerId) + " to slot " + N(g_giveOut.toSlot);
    if (left == 0)
    {
        ++g_giveCompleted;
        DebugLog("[GIVE] " + who + " settled: every character's hire DONE came - this game's save is armed");
        GiveOutClear();
        OwnGiveSaveArm("gave " + who);
        return;
    }
    if (!swgive::Expired(now, g_giveOut.at, swgive::kOfferMs)) return;
    if (swgive::PromisesKept(g_giveOut.state, swgive::kEndTimedOut) != 0) ++g_giveKeptUnknown;
    DebugLog("[GIVE] " + who + ": " + N(left) + " hire DONE(s) did not come in 60 s after the receiver's done - their promises are KEPT for this world"
             " (the take may have happened); the offer ends here");
    GiveOutClear();
}

/* the 60 s limits, run BEFORE the messages each frame (T-1135 review fold 2): a GO that comes after the receiver's accepted wait ran
   out finds no offer here and is answered "failed" */
void GiveExpire(DWORD now)
{
    for (size_t i = 0; i < g_giveIn.size(); )
    {
        const GiveIn g = g_giveIn[i];
        if (!swgive::Expired(now, g.at, swgive::kOfferMs)) { ++i; continue; }
        g_giveIn.erase(g_giveIn.begin() + (std::ptrdiff_t)i);
        if (g.state == 1)
        {
            ++g_giveTimeouts;
            GiveSendWord(swgive::kGiveAnswer, g.offer.offerId, g.offer.giverSlot, g.offer.receiverSlot, swgive::kAnsTimeout, swgive::kNoIndex, g.offer.giverSlot);
            GiveLine(g.offer.giverSlot, swgive::RanOutLine(g.offer.names));
            DebugLog("[GIVE] offer=" + N(g.offer.offerId) + " from slot " + N(g.offer.giverSlot) + " - its box closed itself after 60 s; the giver is told");
        }
        else
        {
            ++g_giveFailed;
            GiveForgetAccepted(g.offer.offerId, g.offer.giverSlot);
            GiveSendWord(swgive::kGiveAnswer, g.offer.offerId, g.offer.giverSlot, g.offer.receiverSlot, swgive::kAnsFailed, swgive::kNoIndex, g.offer.giverSlot);
            GiveLine(g.offer.giverSlot, swgive::FailedLine(swgive::NameAt(g.offer.names, 0)));
            DebugLog("[GIVE] offer=" + N(g.offer.offerId) + " from slot " + N(g.offer.giverSlot) + " - accepted, but no go came in 60 s; nothing taken, and nothing will be - the giver is told failed");
        }
    }
    if (g_giveOut.offerId != 0 && g_giveOut.state == swgive::kOutOffered && swgive::Expired(now, g_giveOut.at, swgive::kOfferMs + swgive::kGraceMs))
    {
        ++g_giveRanOut;
        GiveSendWord(swgive::kGiveResult, g_giveOut.offerId, StoreMySlot(), g_giveOut.toSlot, swgive::kResOver, swgive::kNoIndex, g_giveOut.toSlot);
        GiveLine(g_giveOut.toSlot, swgive::RanOutLine(g_giveOut.names));
        DebugLog("[GIVE] offer=" + N(g_giveOut.offerId) + " to slot " + N(g_giveOut.toSlot) + " ran out: no answer in " + N((long long)((swgive::kOfferMs + swgive::kGraceMs) / 1000)) + " s");
        GiveOutClear();
    }
    else if (g_giveOut.offerId != 0 && g_giveOut.state == swgive::kOutGoSent && swgive::Expired(now, g_giveOut.at, swgive::kOfferMs + swgive::kGraceMs))
    {
        /* the receiver's own wait (from its ACCEPT, before this GO left) ends first and answers "failed": no word by now says nothing
           about the take */
        ++g_giveFailed;
        if (swgive::PromisesKept(g_giveOut.state, swgive::kEndTimedOut) != 0) { ++g_giveKeptUnknown; GiveKeptAdd(0); }   /* review fold 6: listed for a late failed / departure */
        GiveLine(g_giveOut.toSlot, swgive::FailedLine(swgive::NameAt(g_giveOut.names, 0)));
        DebugLog("[GIVE] offer=" + N(g_giveOut.offerId) + " to slot " + N(g_giveOut.toSlot) + " - GO sent, but no answer in " + N((long long)((swgive::kOfferMs + swgive::kGraceMs) / 1000))
                 + " s - the take may have happened: the promises are KEPT for this world (Finish clears each one if its DONE comes)");
        GiveOutClear();
    }
}

long g_giveWorld = 0;   /* the world load the give state belongs to (StoreWorldGenNow) */
/* a world change (a load, or the world closing): every offer out and in, every accepted offer, every undrained message, the armed offer
   and every give promise (request id 0) belong to the world before - dropped together (T-1135 review fold 2) */
void GiveWorldCheck()
{
    const long w = StoreWorldGenNow();
    if (w == g_giveWorld) return;
    g_giveWorld = w;
    long long promises = 0;
    for (std::map<unsigned int, Reserved>::iterator it = g_reserved.begin(); it != g_reserved.end(); )
    {
        if (it->second.reqId == 0) { g_reserved.erase(it++); ++promises; }
        else ++it;
    }
    const bool any = g_giveOut.offerId != 0 || !g_giveIn.empty() || !g_giveAccepted.empty() || !g_giveInbound.empty() || g_giveArmed.armed != 0 || promises != 0 || !g_giveKept.empty();
    /* T-1135 review fold 6: an offer accepted here and not taken owes the giver "failed" (swgive::FailedOwedAtDrop) - dropping it
       silently left the giver's promises kept for its whole world. Sent at the safe point (GiveOwedSend): the road may be down now. */
    for (size_t i = 0; i < g_giveIn.size(); ++i)
    {
        if (swgive::FailedOwedAtDrop(g_giveIn[i].state) == 0) continue;
        const std::string who = "offer=" + N(g_giveIn[i].offer.offerId) + " from slot " + N(g_giveIn[i].offer.giverSlot);
        if (g_giveOwed.size() >= kGiveOwedMax) { DebugLog("[GIVE] " + who + " accepted and not taken - NOT owed failed: " + N((long long)kGiveOwedMax) + " words are owed already"); continue; }
        GiveOwed o;
        o.offerId = g_giveIn[i].offer.offerId; o.giverSlot = g_giveIn[i].offer.giverSlot; o.receiverSlot = g_giveIn[i].offer.receiverSlot;
        o.at = ::GetTickCount(); o.tried = o.at - 1000;
        g_giveOwed.push_back(o);
        DebugLog("[GIVE] " + who + " accepted and not taken - the world changed: failed is owed to the giver");
    }
    g_giveKept.clear();   /* the promises they listed were erased above */
    GiveOutClear();
    g_giveIn.clear(); g_giveAccepted.clear(); g_giveInbound.clear(); g_giveArmed = GiveArmed();
    g_giveLeverShow = 0; g_giveLeverAnswer = 0;
    if (any) DebugLog("[GIVE] the world changed (load " + N((long long)w) + ") - every offer, accepted offer, waiting message and give promise (" + N(promises) + ") of the world before is dropped");
}

/* MAIN THREAD, the K2 safe point (HireSafePointDrain), every frame: a world change, the lever, the armed offer, the 60 s limits, the
   messages, the giver's settle. Held while the world loads or tears down (the world change is checked before that hold). */
void GiveDrain(bool blocked, DWORD now)
{
    GiveWorldCheck();
    if (blocked) return;
    GiveOwedSend(now);   /* T-1135 review fold 6 */
    if (g_giveLeverShow != 0) { g_giveLeverShow = 0; GiveShowLines(g_giveLeverShowSlot); }
    if (g_giveLeverAnswer != 0)
    {
        const bool accept = g_giveLeverAnswer == 1;
        g_giveLeverAnswer = 0;
        size_t i = 0;
        while (i < g_giveIn.size() && g_giveIn[i].state != 1) ++i;
        if (i < g_giveIn.size()) GiveAnswerAt(i, accept, "givetest");
        else DebugLog("[GIVE] givetest answer: no offer waits for an answer here");
    }
    if (g_giveArmed.armed != 0) { const GiveArmed a = g_giveArmed; g_giveArmed = GiveArmed(); GiveSendOffer(a); }
    GiveExpire(now);
    std::vector<GiveInbound> batch;
    batch.swap(g_giveInbound);
    const int me = StoreMySlot();
    for (size_t i = 0; i < batch.size(); ++i)
    {
        const swgive::GiveMsg& m = batch[i].m;
        const int from = net::PlayerSlotOfKey(batch[i].peer);
        if (m.kind == swgive::kGiveOffer)
        {
            if (m.receiverSlot != me || m.giverSlot != from) { ++g_giveStale; DebugLog("[GIVE] offer=" + N(m.offerId) + " from slot " + N(from) + " names slots " + N(m.giverSlot) + " -> " + N(m.receiverSlot) + " - not this game's; ignored"); continue; }
            bool dup = false;
            for (size_t k = 0; k < g_giveIn.size(); ++k) if (g_giveIn[k].offer.offerId == m.offerId && g_giveIn[k].offer.giverSlot == m.giverSlot) dup = true;
            if (dup) { ++g_giveStale; continue; }
            if (g_giveIn.size() >= kGiveInMax)
            {
                GiveSendWord(swgive::kGiveAnswer, m.offerId, m.giverSlot, me, swgive::kAnsFailed, swgive::kNoIndex, from);
                DebugLog("[GIVE] offer=" + N(m.offerId) + " from slot " + N(from) + " answered failed: " + N((long long)kGiveInMax) + " offers already wait here");
                continue;
            }
            GiveIn g; g.offer = m; g.state = 1; g.at = now;
            g_giveIn.push_back(g);
            ++g_giveOffersIn;
            DebugLog("[GIVE] offer=" + N(m.offerId) + " in from slot " + N(from) + " uids=[" + GiveUids(m.uids) + "] names='" + swgive::NamesList(m.names) + "' - its box waits for an answer (60 s)");
        }
        else if (m.kind == swgive::kGiveAnswer)
        {
            if (m.giverSlot != me || m.receiverSlot != from) { ++g_giveStale; continue; }
            GiveOnAnswer(m, from);
        }
        else
        {
            if (m.receiverSlot != me || m.giverSlot != from) { ++g_giveStale; continue; }
            GiveOnResult(m, from, batch[i].peer, now);
        }
    }
    GiveSettle(now);
    GiveKeptSweep(now);   /* T-1135 review fold 6 */
}

/* a player left: this player's offer to it ends - after GO the take may have happened, so its promises are KEPT for this world
   (swgive::PromisesKept; T-1135 review fold 3) and Finish clears each one if its DONE still comes; before GO none were made. Its
   offers here end; its messages not drained are dropped. */
void GiveForgetPeer(int slot)
{
    if (g_giveOut.offerId != 0 && cooppg::PlayerGoneTakesRow(g_giveOut.toSlot, slot))
    {
        ++g_givePeerGone;
        const bool kept = swgive::PromisesKept(g_giveOut.state, swgive::kEndPeerGone) != 0;
        if (kept) { ++g_giveKeptUnknown; GiveKeptAdd(1); } else GiveReleaseReserved();   /* review fold 6: listed, the receiver gone */
        if (g_giveOut.state != swgive::kOutDoneHeard) GiveLine(g_giveOut.toSlot, swgive::FailedLine(swgive::NameAt(g_giveOut.names, 0)));
        DebugLog("[GIVE] offer=" + N(g_giveOut.offerId) + " ended: the receiver (slot " + N(g_giveOut.toSlot) + ") left"
                 + (kept ? std::string(" after GO - its promises are KEPT for this world (the take may have happened)") : std::string()));
        GiveOutClear();
    }
    for (size_t i = 0; i < g_giveKept.size(); ++i)   /* T-1135 review fold 6: earlier kept offers to the player who left - GiveKeptSweep decides them */
    {
        if (g_giveKept[i].receiverGone != 0 || !cooppg::PlayerGoneTakesRow(g_giveKept[i].toSlot, slot)) continue;
        g_giveKept[i].receiverGone = 1;
        DebugLog("[GIVE] kept offer=" + N(g_giveKept[i].offerId) + " to slot " + N(g_giveKept[i].toSlot) + ": the receiver left - decided at the end of the GO's wait");
    }
    for (size_t i = 0; i < g_giveIn.size(); )
    {
        if (!cooppg::PlayerGoneTakesRow(g_giveIn[i].offer.giverSlot, slot)) { ++i; continue; }
        ++g_givePeerGone;
        const GiveIn g = g_giveIn[i];
        g_giveIn.erase(g_giveIn.begin() + (std::ptrdiff_t)i);
        GiveForgetAccepted(g.offer.offerId, g.offer.giverSlot);
        GiveLine(g.offer.giverSlot, swgive::FailedLine(swgive::NameAt(g.offer.names, 0)));
        DebugLog("[GIVE] offer=" + N(g.offer.offerId) + " from slot " + N(g.offer.giverSlot) + " ended: the giver left");
    }
    for (size_t i = 0; i < g_giveInbound.size(); )
    {
        if (cooppg::PlayerGoneTakesRow(net::PlayerSlotOfKey(g_giveInbound[i].peer), slot)) g_giveInbound.erase(g_giveInbound.begin() + (std::ptrdiff_t)i);
        else ++i;
    }
}

/* detour_recruit's give exception: only on the main thread, only inside GiveTakeAll's recruit, only for the uid being taken, and only
   while that uid is in an accepted offer with the same offer id, giver slot and receiver slot (swgive::GiveGuardPass). */
bool GiveRecruitPass(::Character* c)
{
    const unsigned long mt = StoreMainThreadId();
    if (mt == 0 || ::GetCurrentThreadId() != mt || g_giveTakeUid == 0 || !Plaus(c)) return false;
    if (FindSpawnedUid(c) != g_giveTakeUid) return false;
    if (!swgive::GiveGuardPass(g_giveAccepted, g_giveTakeOffer, g_giveTakeGiver, g_giveTakeReceiver, g_giveTakeUid)) return false;
    ++g_giveGuardPass;
    return true;
}

char detour_recruit(void* pi, ::Character* c, bool edit)
{
    if (Plaus(c) && OwnedVerdict(c) != recruitguard::kRecruitAllow && !GiveRecruitPass(c))   /* T-1135: an accepted give offer's take only */
    {
        ::InterlockedIncrement64(&g_ownedEngine);
        const unsigned long mt = StoreMainThreadId();
        if (mt != 0 && ::GetCurrentThreadId() == mt)
        {
            DebugLog("[HIRE] recruit 0x693010 REFUSED: another player's character uid=" + N(FindSpawnedUid(c)) + " fac='" + FacLog(FactionOfPod(c)) + "'");
            NoticeOwned(c);
        }
        return 0;
    }
    /* read before the call: a person already in this game's player faction changes no faction the other games show */
    ::Faction* const wasFac = Plaus(c) ? FactionOfPod(c) : 0;   /* T-1228: the faction the person leaves, for the NotMine line */
    const int wasInMine = (wasFac != 0 && IsPlayerFaction(wasFac)) ? 1 : 0;
    const unsigned long long ret = (unsigned long long)(uintptr_t)_ReturnAddress();   /* T-1228: the engine's call site */
    const int talkOpen = TalkWindowOpenNow();   /* T-1228: the talk window at the call (any thread) */
    const char rc = g_recruitOrig(pi, c, edit);
    const unsigned long mt = StoreMainThreadId();
    if (mt != 0 && ::GetCurrentThreadId() == mt) EngineRecruitSettle(c, Plaus(c) ? FindSpawnedUid(c) : 0u, wasInMine, g_ownRecruitDepth, rc != 0 ? 1 : 0, false, ret, wasFac, talkOpen);
    else if (g_ownRecruitDepth > 0) ++g_engVerdict[recruitguard::kEngineRecruitOwnRoad];   /* the main thread before its id is known: one of this file's roads, which tells the other games itself */
    else if (rc != 0) EngineRecruitQueue(c, wasInMine, ret, wasFac, talkOpen);   /* RecruitPod runs on the main thread only: off it, every recruit is the engine's own */
    else ::InterlockedIncrement64(&g_engOffMainRefused);
    return rc;
}

} // namespace

int HireDoActionsHookState() { return g_doActionsHook; }   /* P26s1 fold 1 M3 (declared in speech.h, namespace coop) */
/* T-478 step 6 (declared in speech.h): 1 = both buying confirms are hooked and the trading window and a row's character read; else
   the hook's state (-1 / -2), -3 a needed row is missing, -4 the trading window global does not read. */
int HireBuyConfirmHookState()
{
    if (g_buySlaveHook != 1) return g_buySlaveHook;
    if (g_buyAnimalHook != 1) return g_buyAnimalHook;
    if (kBuyWinRva == 0 || kBuyHandCharRva == 0) return -3;
    int ok = 0;
    (void)BuyReadPtrPod((const void*)(Base() + (uintptr_t)kBuyWinRva), &ok);
    return ok ? 1 : -4;
}

/* P26 stage 5 (speech.cpp TkRunViewPod, MAIN THREAD): the engine's own Dialogue::_doActions through the hook's trampoline - this
   file's detour (TalkSkipDoActions / TalkDeferTargetActs / HoldLine) is not re-entered. a3 is not read and a4 is written before it
   is read (Read 67fad0:4, 180-181), so both go as 0. 1 ran, 0 no trampoline, -1 the engine faulted. */
int HireCallDoActions(void* dlg, void* line)
{
    if (g_doActionsHook != 1 || g_doActionsOrig == 0) return 0;
    __try { g_doActionsOrig(dlg, line, 0, 0); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* talkprompt joinact (speech.cpp TkLeverDrain, MAIN THREAD, the K2 safe point): the engine's own Dialogue::_doActions through its
   ENTRY, so this file's detour (and with it HoldLine and the recruit detour) runs exactly as for a clicked reply's line - the call
   replyClicked(string) 0x683170 makes (Read decomp_683170:29). a3 / a4 go as 0, as HireCallDoActions. 1 ran, 0 the hook or its
   address is missing, -1 the engine faulted (caught). */
int HireCallDoActionsEntry(void* dlg, void* line)
{
    if (g_doActionsHook != 1 || kDoActionsRva == 0) return 0;
    const DoActionsFn fn = (DoActionsFn)(Base() + (uintptr_t)kDoActionsRva);
    __try { fn(dlg, line, 0, 0); return 1; }
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
    if (OwnedVerdict(person) != recruitguard::kRecruitAllow) { ++g_ownedLocal; NoticeOwned(person); *why = "another player's character (recruitguard.h)"; return 0; }
    DebugLog("[HIRE] talk hire uid=" + N(npcUid) + " hirer=" + N(hirerUid) + " price=" + N(price) + " type=" + N(joinType)
             + " - the other game's NPC joins this game's squad through the owner's REQ (P26 stage 5)");
    Hold(npcUid, person, hirerUid, price, joinType);
    if (g_pending.find(npcUid) == g_pending.end()) { *why = "the request did not go out (link down)"; return 0; }
    return 1;
}

/* T-980 (spawn.cpp ApplyRemotePrison, MAIN THREAD): the freer's game - the prisoner's owner asks it to hire the prisoner; queued for
   the safe point (a full queue answers at once). */
void HireJoinAskRecv(unsigned int uid, unsigned int freerUid, unsigned int fromPeer)
{
    ++g_joinAskRecv;
    if (g_joinAsks.size() >= kJoinAsksMax)
    {
        ++g_joinAskFull;
        cooprison::PrisonMsg m;
        m.uid = uid; m.kind = cooprison::kPrisonJoinNo; m.freerUid = freerUid; m.joinNo = cooprison::kJoinNoNotAsked;
        const bool sent = net::SendPrison(m, fromPeer);
        ErrorLog("[HIRE] join hand-off uid=" + N(uid) + " freer=" + N(freerUid) + " NOT asked: " + N((long long)kJoinAsksMax) + " asks already wait"
                 + (sent ? std::string(" - refusal sent") : std::string(" - refusal NOT sent (no road)")));
        return;
    }
    JoinAskIn a; a.uid = uid; a.freerUid = freerUid; a.peer = fromPeer;
    g_joinAsks.push_back(a);
}

/* T-980 (spawn.cpp ApplyRemotePrison, MAIN THREAD): the prisoner's owner - the freer's game did not ask for the hire; the ask ends. */
void HireJoinNoRecv(unsigned int uid, unsigned int freerUid, int reason, unsigned int fromPeer)
{
    std::map<unsigned int, JoinPend>::iterator it = g_joinPend.find(uid);
    if (it == g_joinPend.end() || it->second.freerUid != freerUid)
    {
        ++g_joinNoStale;
        DebugLog("[HIRE] join hand-off uid=" + N(uid) + " freer=" + N(freerUid) + " refusal reason=" + N(reason) + " from game " + N(fromPeer) + " - no such ask in flight (ended already)");
        return;
    }
    ++g_joinNoRecv;
    DebugLog("[HIRE] join hand-off uid=" + N(uid) + " freer=" + N(freerUid) + " REFUSED by the freer's game: reason=" + N(reason) + " - the prisoner joins nobody");
    g_joinPend.erase(it);
}

void InstallHire()
{
    if (kDoActionsRva == 0) g_doActionsHook = -2;
    else g_doActionsHook = (coop::AddHook((void*)(Base() + (uintptr_t)kDoActionsRva), (void*)&detour_doActions, (void**)&g_doActionsOrig) == coop::SUCCESS) ? 1 : -1;
    if (kRecruitRva == 0) g_recruitHook = -2;
    else g_recruitHook = (coop::AddHook((void*)(Base() + (uintptr_t)kRecruitRva), (void*)&detour_recruit, (void**)&g_recruitOrig) == coop::SUCCESS) ? 1 : -1;
    if (kBuySlaveRva == 0) g_buySlaveHook = -2;   /* T-478 step 6 */
    else g_buySlaveHook = (coop::AddHook((void*)(Base() + (uintptr_t)kBuySlaveRva), (void*)&detour_buySlaveConfirm, (void**)&g_buySlaveOrig) == coop::SUCCESS) ? 1 : -1;
    if (kBuyAnimalRva == 0) g_buyAnimalHook = -2;
    else g_buyAnimalHook = (coop::AddHook((void*)(Base() + (uintptr_t)kBuyAnimalRva), (void*)&detour_buyAnimalConfirm, (void**)&g_buyAnimalOrig) == coop::SUCCESS) ? 1 : -1;
    const std::string line = "[HIRE] hooks: Dialogue::_doActions 0x67FAD0=" + N(g_doActionsHook) + " (R0-b hold; 1 armed, -1 AddHook failed, -2 no table address)"
        + " recruit 0x693010=" + N(kRecruitRva != 0 ? 1 : 0) + " recruitGuard=" + N(g_recruitHook) + " PlayerInterface=" + N(kPlayerIfaceRva != 0 ? 1 : 0) + " endDialogue 0x673DA0=" + N(kEndDialogueRva != 0 ? 1 : 0)
        + " buyConfirm slave 0x6AE510=" + N(g_buySlaveHook) + " animal 0x6AB410=" + N(g_buyAnimalHook);
    if (g_doActionsHook == 1 && g_recruitHook == 1 && kRecruitRva != 0 && kPlayerIfaceRva != 0 && kEndDialogueRva != 0 && g_buySlaveHook == 1 && g_buyAnimalHook == 1) DebugLog(line); else ErrorLog(line);
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
    if (g_squadTest.armed != 0 && !EngineWritesBlocked()) { const SquadTest t = g_squadTest; g_squadTest.armed = 0; RunSquadTest(t); }
    {
        const bool blocked = EngineWritesBlocked();
        JoinHeldDrain(blocked, ::GetTickCount());   /* T-980: held join lines asked of the freer's game; unanswered asks end */
        JoinAskDrain(blocked);                      /* T-980: the prisoner's owner's asks - the hire asked, or the refusal sent back */
        GiveDrain(blocked, ::GetTickCount());       /* T-1135: giving characters - the armed offer, the messages, the 60 s limits */
        BuyDrain(blocked);                          /* T-478 step 6: the buying confirm's asks - run once every owner said yes, else refused */
        coop::PeaceTalkDrain();                     /* T-1194 (T2029): the dropped talk attacks' log lines (log only) */
    }
    if ((g_testArmed == 0 && g_inbound.empty() && g_engQN == 0) || EngineWritesBlocked()) return;   // held while loading: runs every frame
    if (g_engQN != 0) EngineRecruitDrain();   /* the engine's recruits off the main thread: their DONE goes out here */
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
        if (it->second.reqId == 0) { ++it; continue; }   /* T-1135 review fold 3: request id 0 is a give's promise, made at GO - the take may have happened, so it is kept (GiveForgetPeer) */
        DebugLog("[HIRE] promise released uid=" + N(it->first) + " req=" + N(it->second.reqId) + " (the asker, slot " + N(it->second.slot) + ", left) - the person may be hired again");
        g_reserved.erase(it++);
    }
    for (std::map<unsigned int, JoinPend>::iterator it = g_joinPend.begin(); it != g_joinPend.end(); )   // T-980: join asks to that player end
    {
        if (!cooppg::PlayerGoneTakesRow(it->second.slot, slot)) { ++it; continue; }
        ++g_joinPeerGone;
        DebugLog("[HIRE] join hand-off uid=" + N(it->first) + " freer=" + N(it->second.freerUid) + " ended: the freer's game (slot " + N(it->second.slot) + ") left - the prisoner joins nobody");
        g_joinPend.erase(it++);
    }
    for (size_t i = 0; i < g_joinAsks.size(); )   // T-980: asks from that player not drained yet - nobody is left to answer
    {
        if (cooppg::PlayerGoneTakesRow(net::PlayerSlotOfKey(g_joinAsks[i].peer), slot)) { ++g_joinPeerGone; g_joinAsks.erase(g_joinAsks.begin() + (std::ptrdiff_t)i); }
        else ++i;
    }
    GiveForgetPeer(slot);   /* T-1135: an offer to or from that player ends */
    for (size_t i = 0; i < g_inbound.size(); )   // its messages not drained yet: an OK, NO or DONE from a player who left is never acted on
    {
        if (cooppg::PlayerGoneTakesRow(net::PlayerSlotOfKey(g_inbound[i].peer), slot)) g_inbound.erase(g_inbound.begin() + (std::ptrdiff_t)i);
        else ++i;
    }
}

/* T-1135, MAIN THREAD (the PLAYERS tab's frame, the lever): the GIVE button for the player in receiverSlot - this player's own
   characters in the world's selection, each read live (givewire.h ButtonFor). Their uids and names come back; returns how many. */
int GiveButtonRead(int receiverSlot, swgive::ButtonView* view, std::vector<unsigned int>* uids, std::vector<std::string>* names)
{
    uids->clear(); names->clear();
    void* sel[64];
    const int n = SelectedCharacters(sel, 64);
    for (int i = 0; i < n; ++i)
    {
        const unsigned int u = Plaus(sel[i]) ? FindSpawnedUid(sel[i]) : 0u;
        if (u == 0 || !net::IsUidMine(u) || !IsPlayerFaction(FactionOfPod((::Character*)sel[i]))) continue;   /* another player's copy in the selection is not offered */
        bool dup = false;
        for (size_t k = 0; k < uids->size(); ++k) if ((*uids)[k] == u) dup = true;
        if (!dup) uids->push_back(u);
    }
    std::vector<int> reasons;
    GiveReadUids(*uids, &reasons, names);
    *view = swgive::ButtonFor(reasons, *names, GiveOwnCount(), GiveReceiverInWorld(receiverSlot), g_giveOut.offerId != 0);
    return (int)uids->size();
}
void GiveOfferArm(int receiverSlot, const std::vector<unsigned int>& uids, const std::string& from)
{
    g_giveArmed.armed = 1; g_giveArmed.slot = receiverSlot; g_giveArmed.uids = uids; g_giveArmed.from = from;
}
bool GiveOfferWaiting(unsigned int* offerId, int* giverSlot, std::vector<std::string>* names)
{
    for (size_t i = 0; i < g_giveIn.size(); ++i)
        if (g_giveIn[i].state == 1) { *offerId = g_giveIn[i].offer.offerId; *giverSlot = g_giveIn[i].offer.giverSlot; *names = g_giveIn[i].offer.names; return true; }
    return false;
}
bool GiveOfferStillWaiting(unsigned int offerId, int giverSlot)
{
    for (size_t i = 0; i < g_giveIn.size(); ++i)
        if (g_giveIn[i].state == 1 && g_giveIn[i].offer.offerId == offerId && g_giveIn[i].offer.giverSlot == giverSlot) return true;
    return false;
}
int GiveOfferAnswer(unsigned int offerId, int giverSlot, bool accept)
{
    for (size_t i = 0; i < g_giveIn.size(); ++i)
        if (g_giveIn[i].state == 1 && g_giveIn[i].offer.offerId == offerId && g_giveIn[i].offer.giverSlot == giverSlot) return GiveAnswerAt(i, accept, "the offer box");
    return 0;
}
void GiveNoteRecv(const swgive::GiveMsg& m, unsigned int fromPeer)
{
    GiveInbound in; in.m = m; in.peer = fromPeer;
    g_giveInbound.push_back(in);
}
std::string GiveTestArm(const std::string& arg)
{
    const swgive::Lever l = swgive::ParseLever(arg);
    if (l.kind == swgive::kLeverOffer)
    {
        GiveOfferArm(l.slot, l.uids, "givetest");
        return "ok givetest offer armed: slot " + N(l.slot) + " uids=[" + GiveUids(l.uids) + "] - sent at the next safe point after the GIVE button's checks ([GIVE] lines)";
    }
    if (l.kind == swgive::kLeverAnswer)
    {
        g_giveLeverAnswer = l.accept ? 1 : 2;
        return std::string("ok givetest answer ") + (l.accept ? "accept" : "refuse") + " armed - the first offer waiting here, at the next safe point ([GIVE] lines)";
    }
    if (l.kind == swgive::kLeverShow) { g_giveLeverShow = 1; g_giveLeverShowSlot = l.slot; return "ok givetest show armed - [GIVE] show lines at the next safe point"; }
    return "error givetest: usage givetest offer <slot> <uid> [<uid>..] | givetest answer accept|refuse | givetest show [<slot>]";
}

std::string HireTestArm(const std::string& arg)
{
    std::istringstream is(arg);
    std::string mode; is >> mode;
    /* a leading 'editor': the hire opens the character editor - recruit(edit = true), the call the dialogue's join-with-edit
       reply makes - instead of join type 18 */
    const bool editor = mode == "editor";
    if (editor) { mode.clear(); is >> mode; }
    const std::string usage = "error hiretest: usage hiretest [editor] near [price] | [editor] near same [<x> <z> [price]] | [editor] uid <n> [price] | [editor] nearestpeer [<radius>]";
    std::vector<std::string> t; std::string w;
    while (is >> w) t.push_back(w);
    unsigned int uid = 0; int price = kDefaultPrice; bool same = false; float sx = kSameX, sz = kSameZ;
    size_t ip = 0;   /* index of the optional price token */
    if (mode == "nearestpeer")
    {
        /* T-1022 nearestpeer [<radius>] - TEST-ONLY: the nearest living character of ANOTHER player to my first player character
           (OwnedVerdict, the test every hire road refuses by), then the ordinary hire road (RunTestLever: Hold, or LocalHire) */
        float r = kNearMax;
        if (t.size() > 1) return usage;
        if (t.size() == 1)
        {
            char* e = 0; const double v = strtod(t[0].c_str(), &e);
            if (e == t[0].c_str() || *e != 0 || !(v >= 1.0) || v > 20000.0) return "error hiretest: nearestpeer radius 1..20000";
            r = (float)v;
        }
        g_testMode = 4; g_testUid = 0; g_testPrice = price; g_testRadius = r; g_testJoinType = editor ? 3 : 18; g_testArmed = 1;
        DebugLog("[HIRE] hiretest " + std::string(editor ? "editor " : "") + "nearestpeer radius=" + N((long long)r) + " price=" + N(price)
                 + " join=" + N(g_testJoinType) + " ARMED - runs at the next safe point");
        return "ok hiretest armed";
    }
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

std::string SquadTestArm(const std::string& arg)
{
    unsigned int uid = 0; int n = 0; std::string sid;
    if (!swsquad::SquadTestParse(arg, &uid, &n, &sid))
    {
        DebugLog("[cmd] usage: squadtest add <ownUid|first> <n 1.." + N(swsquad::kSquadAddMax) + "> [templateSid]");
        return "error squadtest usage";
    }
    const std::string leaderWord = uid != 0 ? N(uid) : std::string("first");
    if (uid != 0 && !net::IsUidMine(uid))
    {
        DebugLog("[SQUAD] squadtest add leader=" + leaderWord + " refused: not this game's own character");
        return "error squadtest: not this game's own character";
    }
    if (g_squadTest.armed != 0)
    {
        DebugLog("[SQUAD] squadtest add leader=" + leaderWord + " refused: one request is already waiting for the safe point");
        return "error squadtest: one request is already waiting";
    }
    g_squadTest.armed = 1; g_squadTest.ownerUid = uid; g_squadTest.n = n; g_squadTest.templateSid = sid;
    DebugLog("[SQUAD] squadtest add leader=" + leaderWord + " n=" + N(n) + " template=" + sid + " ARMED - runs at the next safe point");
    return "ok squadtest armed";
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
    DebugLog("[GIVE] REPORT offersOut=" + N(g_giveOffersOut) + " notSent=" + N(g_giveNotSent) + " offersIn=" + N(g_giveOffersIn) + " accepts=" + N(g_giveAccepts)
             + " refusals=" + N(g_giveRefusals) + " timeouts=" + N(g_giveTimeouts) + " ranOut=" + N(g_giveRanOut) + " go=" + N(g_giveGo) + " failed=" + N(g_giveFailed)
             + " taken=" + N(g_giveTakenN) + " joined=" + N(g_giveJoinedN) + " completed=" + N(g_giveCompleted) + " doneSeen=" + N(g_giveDoneSeen)
             + " guardPass=" + N(g_giveGuardPass) + " sendFailed=" + N(g_giveSendFailed) + " stale=" + N(g_giveStale) + " peerGone=" + N(g_givePeerGone)
             + " noRoom=" + N(g_giveNoRoom) + " handedBack=" + N(g_giveHandedBack) + " handBackSeen=" + N(g_giveHandBackSeen) + " lateGo=" + N(g_giveLateGo) + " keptUnknown=" + N(g_giveKeptUnknown)
             + " outNow=" + N(g_giveOut.offerId != 0 ? 1 : 0) + " inNow=" + N((long long)g_giveIn.size()) + " acceptedNow=" + N((long long)g_giveAccepted.size()));
    std::string no;
    for (int r = 1; r <= coophire::kNoReasonMax; ++r) no += (r > 1 ? "/" : "") + N(g_noOut[r]);
    DebugLog("[HIRE] REPORT reqOut=" + N(g_reqOut) + " reqIn=" + N(g_reqIn) + " okOut=" + N(g_okOut) + " okIn=" + N(g_okIn)
             + " noOut=" + no + " noIn=" + N(g_noIn) + " doneOut=" + N(g_doneOut) + " doneIn=" + N(g_doneIn) + " taken=" + N(g_taken)
             + " recruitRefused=" + N(g_recruitRefused) + " owned[held,ownerNo,take,local,engine,noticed]=" + N(g_ownedHeld) + "," + N(g_ownedOwnerNo) + ","
             + N(g_ownedTake) + "," + N(g_ownedLocal) + "," + N((long long)g_ownedEngine) + "," + N(g_ownedNoticed) + " purseShort=" + N(g_purseShort) + " released=" + N(g_released)
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
             + " linesOffMain=" + N((long long)g_linesOffMain) + " linesUnread=" + N(g_linesUnread) + " hook=" + N(g_doActionsHook)
             + " | engineRecruit[sent,sendFailed,queued,queueFull,gone,offMainRefused]=" + N(g_engSent) + "," + N(g_engSendFailed) + ","
             + N((long long)g_engQueued) + "," + N((long long)g_engQueueFull) + "," + N(g_engGone) + "," + N((long long)g_engOffMainRefused)
             + " engineRecruitSkip[ownRoad,refused,noUid,notJoined,wasPlayers,notMine]=" + N(g_engVerdict[recruitguard::kEngineRecruitOwnRoad]) + ","
             + N(g_engVerdict[recruitguard::kEngineRecruitRefused]) + "," + N(g_engVerdict[recruitguard::kEngineRecruitNoUid]) + ","
             + N(g_engVerdict[recruitguard::kEngineRecruitNotJoined]) + "," + N(g_engVerdict[recruitguard::kEngineRecruitWasPlayers]) + ","
             + N(g_engVerdict[recruitguard::kEngineRecruitNotMine])
             + " engineRecruitNotMineLines[written,repeat,overCap]=" + N(g_engNotMineLines) + "," + N(g_engNotMineSeen.repeats) + "," + N(g_engNotMineSeen.overCap)
             + " | joinHandOff[held,endFault,asked,sendFailed,dup,blocked,stale,ringLost,reqArrived,refusedIn,refusedStale,timedOut,peerGone,pending]="
             + N((long long)g_joinHeld) + "," + N((long long)g_joinEndFault) + "," + N(g_joinAsked) + "," + N(g_joinSendFailed) + "," + N(g_joinDup) + ","
             + N(g_joinBlocked) + "," + N(g_joinStale) + "," + N(g_joinLostLogged) + "," + N(g_joinReqArrived) + "," + N(g_joinNoRecv) + ","
             + N(g_joinNoStale) + "," + N(g_joinTimedOut) + "," + N(g_joinPeerGone) + "," + N((long long)g_joinPend.size())
             + " joinAsked[recv,full,hireAsked,freerNotOurs,noCopy,prisonerOurs,notAsked,blocked]=" + N(g_joinAskRecv) + "," + N(g_joinAskFull) + ","
             + N(g_joinHireAsked) + "," + N(g_joinRefusedOut[cooprison::kJoinNoFreerNotOurs]) + "," + N(g_joinRefusedOut[cooprison::kJoinNoNoCopy]) + ","
             + N(g_joinRefusedOut[cooprison::kJoinNoPrisonerOurs]) + "," + N(g_joinRefusedOut[cooprison::kJoinNoNotAsked]) + ","
             + N(g_joinRefusedOut[cooprison::kJoinNoBlocked]));
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

/* TEST-ONLY lever `splitsquad [n] [at <x> <z>]` (at: the new squad is made at that point and its members are put on the terrain there, 3 apart): the last n members (default 1) of this game's first player squad - the squad of its first own
   living player-faction character (MyHirer's rule) - are moved into a NEW squad of the player faction, so a test can make a player
   squad that the last engine save does not hold. The verb only ARMS it (MAIN THREAD, the command channel); SplitSquadDrain runs it at
   the K2 safe point 0x7D17E0 (combat.cpp, the AI worker paused) and writes the result to the status file.
   The new squad is made as the squad screen's "Add squad" button makes one: that handler (0x4910C0) calls PlayerInterface 0x7F3D70,
   which is Faction::spawnEmptySquad 0x7F3880 (the player faction, no template, permanent, a position) and takes the result's
   ActivePlatoon (Platoon+0x1D8). Each character is then moved by Character vt+0xA0 setFaction(its own faction, the new
   ActivePlatoon): the old squad's removal (ActivePlatoon vt+0x18) and the new squad's addMember (vt+0x10 0x7966A0) - the engine's own
   join the hire flow uses. The squad screen's drag-and-drop mover 0x795A80 makes the same old-squad removal and an insert at a
   chosen place; it has no address-table row, so the lever does not call it. */
int GroundTerrainHeightAt(float x, float z, float* out);   /* combat.cpp: the terrain height at (x,z) - 1 height, 0 no terrain, -1 no row / faulted. MAIN THREAD */
namespace {
typedef void* (*SsSpawnEmptySquadFn)(void* faction, void* squadTemplate, bool permanent, const Ogre::Vector3* p);
typedef std::string* (*SsPlatoonSidFn)(const void* platoon, std::string* out);
unsigned long long kSsSpawnEmptySquadRva = 0; static AddrReg kSsSpawnEmptySquadRva_reg("Faction_spawnEmptySquad", &kSsSpawnEmptySquadRva);   /* Steam_1.0.65 0x7F3880 Faction::spawnEmptySquad */
unsigned long long kSsPlatoonSidRva = 0; static AddrReg kSsPlatoonSidRva_reg("PlatoonSid", &kSsPlatoonSidRva);   /* Steam_1.0.65 0x384AF0 Platoon::getPlatoonStringID */
const size_t kSsActivePlatoonOff = 0x1D8;   /* Platoon -> ActivePlatoon (handoff.h, the squad reader's back-pointer) */
const int kSsMaxN = kSquadViewCap - 1;
const int kSsWaitMax = 600;                 /* safe points to wait while engine writes are blocked */
int g_ssArmed = 0, g_ssN = 1, g_ssWaits = 0;
int g_ssAt = 0; float g_ssAtX = 0.0f, g_ssAtZ = 0.0f;   /* `at <x> <z>`: the new squad is made there and its members are put on the ground there */
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
    if (g_ssAt != 0)
    {
        float gy = 0.0f;
        if (GroundTerrainHeightAt(g_ssAtX, g_ssAtZ, &gy) != 1) return SsRefuse("no terrain height at " + N((long long)g_ssAtX) + "," + N((long long)g_ssAtZ));
        pos = Ogre::Vector3(g_ssAtX, gy, g_ssAtZ);
    }
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
    std::string body = "from=" + fromSid + " squad=" + newSid + " uids=" + uidList + std::string(b);
    if (moved != n) return SsRefuse("not every member reached the new squad: " + body);
    if (g_ssAt != 0)
    {
        int put = 0;
        for (size_t i = 0; i < moving.size(); ++i)
        {
            const float px = g_ssAtX + 3.0f * (float)i;
            float gy = 0.0f;
            if (GroundTerrainHeightAt(px, g_ssAtZ, &gy) == 1 && TeleportCharacter(moving[i], px, gy, g_ssAtZ)) ++put;
        }
        char pb[96]; _snprintf(pb, 95, " placed=%d/%d at %.0f,%.0f", put, (int)moving.size(), g_ssAtX, g_ssAtZ); pb[95] = 0;
        body += pb;
    }
    DebugLog("[SQUAD] splitsquad " + body);
    return "ok splitsquad " + body;
}
} // namespace

/* TEST-ONLY (`spawncontainer new`, spawn.cpp SpawnTemplate): a fresh empty squad of faction f at pos, made by the engine's
   Faction::spawnEmptySquad as splitsquad makes one (no template, permanent). *activeOut = its ActivePlatoon (0 when none),
   *sid = its id ("?" when unread). 1 made, 0 not (*why says why). MAIN THREAD. */
int SpawnEmptySquadFor(::Faction* f, const Ogre::Vector3& pos, void** activeOut, std::string* sid, std::string* why)
{
    *activeOut = 0; *sid = "?"; *why = "";
    if (kSsSpawnEmptySquadRva == 0) { *why = "Faction_spawnEmptySquad is not in the address table"; return 0; }
    if (!Plaus(f)) { *why = "no faction"; return 0; }
    void* ap = 0;
    void* platoon = SsSpawnEmptySquadPod((void*)f, &pos, &ap);
    if (!Plaus(platoon) || !Plaus(ap)) { *why = "Faction::spawnEmptySquad made no squad (platoon " + N((long long)(uintptr_t)platoon) + ")"; return 0; }
    if (kSsPlatoonSidRva != 0) *sid = SsSid(platoon);
    *activeOut = ap;
    return 1;
}

void CommandStatusLate(const std::string& text);   /* command_channel.cpp (ui.h): the status file, written after the verb returned */

std::string SplitSquadArm(const std::string& arg)
{
    int n = 1, at = 0; float ax = 0.0f, az = 0.0f;
    if (!arg.empty())
    {
        char extra = 0;
        const int k = std::sscanf(arg.c_str(), "%d at %f %f %c", &n, &ax, &az, &extra);
        if (k == 3) at = 1;
        else if (k != 1 || std::sscanf(arg.c_str(), "%d %c", &n, &extra) != 1) n = -1;
        if (n < 1 || n > kSsMaxN)
            return SsRefuse("usage splitsquad [n] [at <x> <z>] (members to move, 1.." + N(kSsMaxN) + ", default 1; at: the new squad made and put on the ground there)");
    }
    if (kSsSpawnEmptySquadRva == 0 || kSsPlatoonSidRva == 0) return SsRefuse("Faction_spawnEmptySquad / PlatoonSid not in the address table");
    g_ssArmed = 1; g_ssN = n; g_ssWaits = 0; g_ssAt = at; g_ssAtX = ax; g_ssAtZ = az;
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

unsigned int HireLastBoughtUid() { return g_buyLastUid; }

} // namespace coop
