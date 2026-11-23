// handoff.cpp - M-D step 1. See handoff.h.
#include "handoff.h"
#include "identity.h"   /* T-1046 fold 2: CaptureObjId - a put-away person's hand, read while the body is whole */
#include "playerfaction.h"   // P3: IsPlayerFaction - player squads are never handed off
#include "zones.h"
#include "store.h"   /* PlayersPresent, StorePeerEpoch: another player here, and each arrival */
#include "spawn.h"
#include "worldstate.h"   /* E27 / review-p5q HIGH-3: the world-state shadow must be told when we give ownership away */
#include "replicate.h"
#include "ai_spike.h"
#include "worldsync.h"   /* inv7e2: ForgetSweepAdopt - a uid received by XFER is not this game's sweep adoption */
#include "coop_log.h"
#include "addresses.h"   /* mig3: coop::GameWorldPtr() / OptionsPtr() - our rows for what `ou` / `options` imported; F479: the camera follows the teleport */
#include "game/GameWorld.h"
#include "game/Character.h"
#include "game/CharMovement.h"
#include <ogre/OgreVector3.h>
#include <Windows.h>
#include <map>
#include <vector>
#include <set>
#include <sstream>
#include <cmath>
#include <cstring>
#include "../common/squadwriter.h"   /* T-1 B1: FollowAction / XferTakeMember / KeepCopySquadAwake / SquadPosUsable - offline-tested */
#include "../common/squadlead.h"   /* T-1 B1 restructure: MSG_SQUAD_LEAD - the announcement and the book of the other game's */
#include "../common/liveenvelope.h"   /* M7a3f7 [m7a3f7-hi0]: PlayerKeyOf / SamePlayer - the giver and the receiver as player keys */
#include "../common/liveowner.h"   /* M7a A1 build 1 [a1b1-hd0]: AgreedSquadLeaderN */
#include "../common/peergone.h"    /* PlayerGoneTakesRow: a player who leaves takes back only the squads handed to it */
#include "../common/twinsleep.h"   /* T-761: the withdrawal kinds - a REVOKE with no winner takes a save-born twin only as the last resort */
#include "../common/raidsquad.h"   /* T-1122 awake hand (owner 1524): AwakeTakeOrder - a taken raid squad's carried goal */
#include "../common/carryhand.h"   /* T-1467: a carried body handed, alone, to its carrier's game - offline-tested */
#include "../common/leadmark.h"   /* T-1442 step 6: the squad-leader change's note list, mark skip and backstop classification - offline-tested */
#include "hooks.h"   /* T-1442 step 6: coop::AddHook - set-leader 0x790C00 and remove-member 0x794C00 */
#include "../common/orphanpurge.h"   /* T-1268: AnnounceArea - the sweep's own area answer, so a re-woken squad is judged as the sweep judges it */
namespace coop { int KeeperPot(void* character, void** ownOut, int* catsOut); int KeeperPotWritePod(void* own, int cats);
                 int KeeperPotsInFlight(std::vector<void*>* pots, int* unresolved); }   /* T-1 B3 restructure: items.cpp - the squad's pot */
namespace coop { int KeeperPotAnnounced(void* character, void** ownOut, int* catsOut); }
// PROBE-START: P133 - items.cpp: a copy write that changes a keeper group's money is logged
namespace coop { void P133KeeperCopyWrite(unsigned int leader, int from, int to); }
// PROBE-END: P133
namespace coop { long long CopyJoinGivenCount(); long long CopyJoinCtxGivenCount(); long long WakeDupKeptGivenCount(); long long CopyJoinSkippedIgnoredCount(); }   /* T-1318 attempt 5 (D1-R2): spawn.cpp / store.cpp - the worldWhole8 REPORT token */
namespace coop { long long CtxSupersedeKeptNotGiven(); }   /* T-1318 attempt 4 (design A, site 8): spawn.cpp - sleeping copies of a handed-over squad kept, a record not given */
namespace coop { std::string StoreWorldIdOf(void* platoon); }   /* M7a3f3 T-425 [m7a3f3-hc0]: store.cpp (store.h) - the squad's engine id, "" if unreadable */
namespace coop { int GroundTerrainHeightAt(float x, float z, float* out); }   /* P25 fold 2: combat.cpp - 1 height, 0 no terrain, -1 no row / faulted. MAIN THREAD */
namespace coop { int ItTraderHomePos(void* ap, void* platoon, void* leader, float* x, float* z); }   /* T-164 B4-3 (M4): items.cpp - a trader squad's home building position */   /* T-1 B3 fold (M2): items.cpp - net of this game's in-flight predictions */
namespace coop { bool StoreLiveReady(); int StoreRosterSlotInWorld(int slot); int StoreMySlot(); bool EngineWritesBlocked(); }   /* M7a A1 build 2 [a1b2-hd9]: store.cpp (store.h) */
#include <algorithm>
namespace coop { int ShopKeeperRecorded(void* character, int countIt); }   /* items.cpp: 1 = the shop record carries that keeper's money */

namespace coop {
namespace {

const int kTickEvery = 118;         // 1 Hz
const int kRing = 1;                // a squad outside the 3x3 around my player is leaving my loaded area
const double kResendSec = 1.0;
const int kMaxResends = 10;
const int kMaxSquad = 64;


std::string N(long long v) { std::ostringstream o; o << v; return o.str(); }
std::string F1(float v) { std::ostringstream o; o.precision(1); o << std::fixed << v; return o.str(); }
double NowSec() { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (double)c.QuadPart / (double)f.QuadPart; }
bool Plaus(const void* p) { return p != 0 && (uintptr_t)p > 0x10000 && (uintptr_t)p < 0x00007FFFFFFFFFFFull; }

::Character* LeaderOf(void* activePlatoon)
{
    __try { return *(::Character**)((char*)activePlatoon + 0xA0); }   // ActivePlatoon::squadleader
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool ReadPos(::Character* c, float* x, float* y, float* z)
{
    __try { Ogre::Vector3 p = c->worldPosition(); *x = p.x; *y = p.y; *z = p.z; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// T146: the movement-only teleport left a 'Relaxing' (sitting) character where it was - the animation layer put it back.
// `Character::teleport(const Vector3&)` 0x5C9BF0 (F459 decompile) writes the logical position, the movement AND the
// animation object (+0x448): the engine's own whole-character teleport.
static unsigned long long kHoCharTeleportRva = 0; static coop::AddrReg kHoCharTeleportRva_reg("CharacterTeleport", &kHoCharTeleportRva);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x5C9BF0 */
typedef void (*CharTeleportFn)(::Character* c, const Ogre::Vector3* pos);
bool Teleport(::Character* c, float x, float y, float z)
{
    if (kHoCharTeleportRva == 0) return false;
    __try { Ogre::Vector3 p(x, y, z); CharTeleportFn fn = (CharTeleportFn)coop::AddrAbs(kHoCharTeleportRva); fn(c, &p); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

/* T-1 B1: the squad reader's guarded halves - POD only (C2712). Offsets: handoff.h SquadView. */
int SquadApOfPod(::Character* c, void** ap)
{
    __try { *ap = *(void**)((char*)c + 0x658); return 1; }   // Character+0x658 = ActivePlatoon* (null = no squad)
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int SquadReadPod(void* ap, SquadView* o)
{
    __try
    {
        char* a = (char*)ap;
        void* pl = *(void**)(a + 0x78);                                      // Platoon*
        if (!Plaus(pl) || *(void**)((char*)pl + 0x1D8) != ap) return 0;     // Platoon+0x1D8 must point back
        const int n = *(int*)(a + 0x58);
        ::Character** arr = *(::Character***)(a + 0x60);
        if (n < 0 || n > 4096 || (n > 0 && !Plaus(arr))) return -1;
        o->ap = ap; o->platoon = pl; o->leader = *(::Character**)(a + 0xA0); o->count = n;
        o->acting = *(::Character**)(a + 0xA8);                              // the engine's acting leader (review M1)
        const int k = n < kSquadViewCap ? n : kSquadViewCap;
        for (int i = 0; i < k; ++i) o->members[i] = arr[i];
        o->stored = k;
        o->x = *(float*)(a + 0xB4); o->y = *(float*)(a + 0xB8); o->z = *(float*)(a + 0xBC);
        o->playerFlag = (*(unsigned long long*)(a + 0xE8) != 0ull) ? 1 : 0;   // review L1: the engine compares the qword
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* T-1318 attempt 4: a Platoon's ActivePlatoon (Platoon+0x1D8; ReadSquadAt checks it points back) - POD, guarded */
int PlatoonApPod(void* pl, void** ap)
{
    __try { *ap = *(void**)((char*)pl + 0x1D8); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *ap = 0; return 0; }
}
/* step 5: ActivePlatoon::setSquadLeader(Character*) 0x790C00 (Steam_1.0.65.br slot 88); it resets the old leader's member type, gives the new one type 2 and writes +0xA0. 1 = +0xA0 is `c` after. */
typedef void (*ApSetSquadLeaderFn)(void* activePlatoon, ::Character* c);
int SetSquadLeaderPod(void* ap, ::Character* c, uintptr_t fn)
{
    __try { ((ApSetSquadLeaderFn)fn)(ap, c); return (*(::Character**)((char*)ap + 0xA0) == c) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* review L2: setSquadLeader reaches 0x620B30, which dereferences [c+0x650] (the AI) and [[c+0x650]+0x20] with no null check,
   and the old leader's member type is reset FIRST - a fault there leaves the squad leaderless. 1 = both plausible. */
int LeaderAiPlausiblePod(::Character* c)
{
    __try { void* ai = *(void**)((char*)c + 0x650); if (!Plaus(ai)) return 0; return Plaus(*(void**)((char*)ai + 0x20)) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* owner 110: the faction's awake-squad list (Faction+0x218 Platoon* array, +0x210 count - store.cpp's ReadActiveListPod read),
   each entry's ActivePlatoon (Platoon+0x1D8), and the stay-awake timer write (ActivePlatoon+0xB0 = 4.0, the engine's value). */
int FactionActiveListPod(void* faction, void*** items, unsigned int* count)
{
    __try { *items = *(void***)((char*)faction + 0x218); *count = *(unsigned int*)((char*)faction + 0x210); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int ActiveOfListPod(void** items, unsigned int i, void** ap)
{
    __try { void* p = items[i]; *ap = Plaus(p) ? *(void**)((char*)p + 0x1D8) : 0; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int StayAwakeWritePod(void* ap)
{
    __try { *(float*)((char*)ap + 0xB0) = 4.0f; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
unsigned long long kApSetSquadLeaderRva = 0; static coop::AddrReg kApSetSquadLeaderRva_reg("ActivePlatoon_setLeader", &kApSetSquadLeaderRva);   /* T-1 B1: the address table fills this. Steam_1.0.65 0x790C00 */
unsigned long long kApLeaderGetRva = 0; static coop::AddrReg kApLeaderGetRva_reg("ActivePlatoon_leader", &kApLeaderGetRva);   /* T-1442 step 6 review fold S2: the acting-leader getter 0x791FF0 (the row speech.cpp also registers), hooked */
unsigned long long kApRemoveMemberRva = 0; static coop::AddrReg kApRemoveMemberRva_reg("ActivePlatoon_removeMember", &kApRemoveMemberRva);   /* T-1442 step 6: ActivePlatoon::removeMember(Character*), Steam_1.0.65 0x794C00 (1.0.68: gen_table_1068) */
/* T-1442 step 6 fix attempt 3 (F1514): the engine's writers of a character's 'held' (+0x2F8: 1 bed, 2 cage), knocked-down (+0xE0) and
   carried (+0x3D4) facts - each hooked below (LeadOutHooks). The first four rows are shared with spawn.cpp / build.cpp / gamecalls.cpp, which
   call them; the last four are this fix's own. Steam_1.0.65 addresses. */
static unsigned long long kLhBedModeRva = 0;    static coop::AddrReg kLhBedModeRva_reg("SetBedMode", &kLhBedModeRva);               /* 0x32E2B0 Character::setBedMode(bool, UseableStuff*): +0x2F8 0 / 1 */
static unsigned long long kLhPrisonModeRva = 0; static coop::AddrReg kLhPrisonModeRva_reg("SetPrisonMode", &kLhPrisonModeRva);      /* 0x3305C0 Character::setPrisonMode(bool, UseableStuff*): +0x2F8 0 / 2 (the cage-use start 0x357FE0, the release, the carry) */
static unsigned long long kLhPoseRva = 0;       static coop::AddrReg kLhPoseRva_reg("Character_setPoseState", &kLhPoseRva);         /* 0x5C7390 the only runtime writer of +0xE0 (03-systems/medical.md) */
static unsigned long long kLhDroppedRva = 0;    static coop::AddrReg kLhDroppedRva_reg("GetDropped", &kLhDroppedRva);                /* 0x5CC110 Character::getDropped(bool, bool): a carried body put down */
static unsigned long long kLhPickedUpRva = 0;   static coop::AddrReg kLhPickedUpRva_reg("Character_getPickedUp", &kLhPickedUpRva);   /* 0x5CE300 (carried, carrier): +0x3D4 = 1, the bed and cage left (+0x2F8 = 0) */
static unsigned long long kLhOutOfBedRva = 0;   static coop::AddrReg kLhOutOfBedRva_reg("TaskGetOutOfBedStep", &kLhOutOfBedRva);     /* 0x3455C0 (task, Character*): Task_GetOutOfBed's step - +0x2F8 = 0 without setBedMode */
static unsigned long long kLhFloorStopRva = 0;  static coop::AddrReg kLhFloorStopRva_reg("SleepOnFloorStop", &kLhFloorStopRva);      /* 0x347E80 (task, who): Task_SleepOnFloor vtable slot 2 - +0x2F8 = 0 */
static unsigned long long kLhFloorEndRva = 0;   static coop::AddrReg kLhFloorEndRva_reg("SleepOnFloorEnd", &kLhFloorEndRva);         /* 0x348010 (task, who): Task_SleepOnFloor vtable slot 3 - +0x2F8 = 0 */
/* review fold (attempt 3): the physics ragdoll's on and off. inRagdoll 0x7D08A0 reads the ragdoll (AnimationClass +0x2E8, getRagdoll
   0x5C4B00) and its +0x59 / +0x5A; their only writers are the two one-line setters 0x7D0790 / 0x7D07A0, called only from
   AnimationClass::ragdollModeUT 0x5B8F80 (disassembly sweep + callers, 2026-11-19), which also makes and frees the ragdoll. It is the
   AnimationClass vtable slot +0x50 (two vtables; a third's slot is 0x5B95C0, which calls it), called by Character::_ragdollMode
   0x5D0320 with the Character as its 6th argument ([rsp+0x28] = this, 0x5D0600). */
static unsigned long long kLhRagdollRva = 0;    static coop::AddrReg kLhRagdollRva_reg("AnimationClass_ragdollModeUT", &kLhRagdollRva); /* 0x5B8F80 (anim, on, pos*, mask, bone, Character*) */

typedef std::pair<void*, std::string> HoKey;   /* (faction, the engine's squad id): a squad's key (SquadKeyOf) */
struct Pending { std::vector<net::XferMember> members; std::map<unsigned int, HoKey> keyOf; /* [a1b2-hc1]: each member's squad key, read at the send */ std::map<unsigned int, std::string> handOf; /* T-1046 leg-N fold 2: each member's hand (cooplo::HandKey), read at the send while the body is whole */ unsigned int reason; double sentAt; int sends; void* ap; int logged; int targetSlot; int awakeWhole; int lentWhole; /* T-1122 lent flag (protocol 224): the message says "lent squad - whole or none" (kept over its resends) */ int worldWhole; /* T-1318 attempt 2 (D3, protocol 226): cooplo::kWholeBitWorld on a forced XFER - "world squad, whole or none" (kept over its resends); 0 on a follow-leader XFER */ coopsquad::FollowAnnounceMark mark; /* T-1239: both announcements of the squad when a follow XFER went */ Pending() : reason(0), sentAt(0.0), sends(0), ap(0), logged(0), targetSlot(-1), awakeWhole(1), lentWhole(0), worldWhole(0) {} };   /* awakeWhole (T-1122 re-check fold 5): 1 = no member was held back from this hand-over (its raid may go on with it) */   /* M7a A1 build 1 [a1b1-hd1] [review F11]: targetSlot - fixed at the first send; every resend and the ACK's sender test use it */   /* protocol 84: reason = coopsquad::kXferReason*. T-1 B1 restructure: ap = my squad the members were read from (the crossing test), logged = its xfer_out line was written (N5: its ACK line is written exactly then) */
long long g_ackWrongSender = 0;   /* M7a A1 build 1 [a1b1-hd1]: XFER_ACKs from a game that was not the XFER's target - ignored */
std::map<unsigned int, Pending> g_pending;   // leader uid -> squad in flight (owner side)
/* ==== M7a A1 build 2 [a1b2-hc0] (design 2.2, 2.3, 2.5) - RETIRED here: the owed hand-overs, the held UNLOADs, the put-away batch and
   flush and their cover judgement, the member notes, the handed-over marks, the left-behind notes, the receiver's hand-back record and
   MSG_HANDBACK. In their place: THE SQUAD INDEX (per person) and THE RELEASE RECORDS (one open offer each). MAIN THREAD, all of it. ==== */
/* 2.3 the squad index: per squad key, the people of it another game runs (given: uid -> that game's key) and the people of it this
   game's engine put away that nobody adopted (asleepHere); g_idxOf: uid -> its squad key (one entry per indexed person) */
struct SquadIdx { std::map<unsigned int, unsigned int> given; std::set<unsigned int> asleepHere; std::set<unsigned int> keptHere; int liveHere; SquadIdx() : liveHere(0) {} };   /* [a1b2f1-hd0]: keptHere, liveHere */
std::map<HoKey, SquadIdx> g_squadIdx;
std::map<unsigned int, std::string> g_givenHandOf;   /* T-1046 fold 2: a given person's hand (read at its put-away), for the sleeping squad's person-by-person test; goes with its index entry */
long long g_idxSeq = 0;   /* T-1046 fold 2: bumped by every index change - the keep question's cached answer is asked again after one */
std::map<unsigned int, HoKey> g_idxOf;
/* T-1318 attempt 5 (owner 1626, D1-R1): a kept / asleep-here person's hand, read when it is kept (IdxKeepKey, RelAsleep's asleep settle) -
   a re-wake erases only the people whose hand is in the woken squad's engine list (cooplo::KeptRemadeByHands). Bounded; goes when the
   person is given or leaves the index, at the peer-gone and the world teardown (IdxDropAll). */
static std::map<unsigned int, std::string> g_keptHandOf;
static const size_t kKeptHandCap = 65536;
static void KeptHandNote(unsigned int uid, const std::string& hand)
{
    if (uid == 0 || hand.empty()) return;
    if (g_keptHandOf.count(uid) != 0 || g_keptHandOf.size() < kKeptHandCap) g_keptHandOf[uid] = hand;
}
/* keptStay - squads a re-wake left with kept / asleep people kept (not in the woken squad's engine list), once per squad per wake (a5 fold 1:
   g_keptStayAp - the woken squad each squad key was last counted for); keptGhost - of those people, the ones with no recorded hand (no
   re-wake can show them re-made: a person put away off-thread, dead or deleted - they hold the squad here whole); keptRemade - people
   erased because their hand was in the list; keptStayLines - the capped lines written */
static long long g_wwKeptStay = 0, g_wwKeptRemade = 0, g_wwKeptStayLines = 0, g_wwKeptGhost = 0;
static std::map<HoKey, const void*> g_keptStayAp;
/* T-1318 attempt 5 (owner 1626, D1-R3): per squad key with people given away, the hands of ANOTHER game's living copies seen in the squad
   (its engine list at a put-away of this game's person, a copy's own put-away by this engine) -> that copy's uid (a5 fold 1, review
   MEDIUM-2: its owner is read NOW, at the taken test - a uid taken back is skipped). The sleeping squad's taken test counts them as taken
   by that game (cooplo::SlotOfHandAddOther). Bounded; a squad's notes go with its index entry, a uid's at its take-back; cleared at the
   peer-gone and the world teardown. */
static std::map<HoKey, std::map<std::string, unsigned int> > g_otherHandsOf;
static const size_t kOtherHandsSquadCap = 8192, kOtherHandsPerSquadCap = 256;
static long long g_wwOtherHandsNoted = 0, g_wwOtherHandsCapHit = 0;
long long g_idxGiven = 0, g_idxAsleepHere = 0, g_idxNoKey = 0, g_idxClearedByOwnerUnload = 0, g_idxClearedByOwnerDespawn = 0, g_idxTakenBack = 0;
long long g_idxKeptOnAnnounce = 0;   /* announce-pass UNLOADs of a person in `given` - kept (the owner still runs it) */
long long g_idxSquadCleared = 0, g_idxAsleepRebuilt = 0, g_idxPeerGoneCleared = 0;
/* fold 1 of A1 build 2 [a1b2f1-hd0] [review F5]: keptHere = people of the squad this game put away and did not offer, or whose offer a
   re-wake cancelled - they exist only in this game's world data; liveHere = this game's live people of it (the 1 Hz scan). g_keptRecent =
   such people of a squad not (yet) indexed, merged when it is (an adoption of others of the same put-away comes later); g_keyCache = uid ->
   squad key of this game's live people of indexed squads (the 1 Hz scan) - a put-away whose body cannot be read is still attributed. */
struct KeptRecent { std::set<unsigned int> uids; double at; KeptRecent() : at(0.0) {} };
static std::map<HoKey, KeptRecent> g_keptRecent;
static std::map<unsigned int, HoKey> g_keyCache;
static long long g_idxKept = 0, g_idxKeptUnknown = 0, g_idxRebuiltDropped = 0, g_idxScans = 0;
/* [a1b2f1-hd1] [review F1]: > 0 while a walk of (or a reference into) the release / index state is live - ReleaseForgetDrain then defers
   to the outermost entry's next drain (as build 1's g_owedTickDepth did). MAIN THREAD. */
static int g_relWalkDepth = 0;
static long long g_relDrainDeferred = 0;
struct RelWalk { RelWalk() { ++g_relWalkDepth; } ~RelWalk() { --g_relWalkDepth; } };
/* 2.5 the release records. A record has a stable id (its END line names it once) and ONE candidate at a time; the gen offered to the k-th
   candidate is baseGen + 1 + k, so the tried list says which gen a late adopter holds. Dropped members of a record whose other members
   were deferred move to a NEW record (split). */
struct RelMember { net::XferMember m; HoKey key; int hasKey; unsigned int baseGen; std::string hand; /* T-1046 fold 2: the person's hand (cooplo::HandKey), "" unread */ RelMember() : hasKey(0), baseGen(0) { std::memset(&m, 0, sizeof(m)); } };
struct ReleaseRec
{
    unsigned int id, keyUid, awakeKey; Sector sector; std::vector<RelMember> mem; std::vector<int> tried;   /* awakeKey (T-1122 re-check fold 2): the key its raid's AWAKE block goes under (0: keyUid) */
    int candidate; double sentAt, clock, lastTick; int sends, defers, flight, held, lentWhole;   /* lentWhole (T-1122 lent flag, protocol 224): the offer says "lent squad - whole or none" (set by the gate; kept over its resends and candidates) */
    float px, pz;   /* the squad's decision position at the put-away - the receivers' keep rule is asked there */
    long long nAdopted, nAsleep, nCancelled, nMoved;
    ReleaseRec() : id(0), keyUid(0), awakeKey(0), candidate(-1), sentAt(0.0), clock(0.0), lastTick(0.0), sends(0), defers(0), flight(0), held(0), lentWhole(0),
                   nAdopted(0), nAsleep(0), nCancelled(0), nMoved(0), px(0.0f), pz(0.0f) { sector.x = -1; sector.y = -1; }
};
std::map<unsigned int, ReleaseRec> g_release;   /* id -> open record */
unsigned int g_releaseNextId = 0;
long long g_hoOpenSeq = 0;   /* T-1318 fold 2 (re-check B1): bumped when an offer closes (a release record ends, a member is accepted or settles, an XFER flight ends) - the given-wait looks again (HandoffOpenSeq) */
/* T-1318 attempt 2 (design D3): a world squad is handed over whole or not at all - the sender's not-offered roads, the receiver's world
   judge, and the tripwire (handoff.cpp's [RELEASE] token, worldWhole[...]) */
static long long g_wwRelKeptOut = 0, g_wwRelOver64 = 0, g_wwXferHeldPart = 0, g_wwRelAll = 0, g_wwRelNone = 0, g_wwXferAll = 0, g_wwXferNone = 0;
static long long g_wwRelNoneLeaderless = 0, g_wwXferNoneLeaderless = 0, g_wwPartGiven = 0, g_wwLines = 0, g_wwGivenAsleepKept = 0;
static const long long kWwLineCap = 200;   /* the sender's not-offered lines: the first 200 (each road also counted; the receivers' whole-refusal lines are the [RELEASE] / [XFER] lines) */
/* T-1318 attempt 2 fold 1 (review): followHeldPart - follow-leader XFERs not sent because a member was held back (per pass, as xferHeldPart);
   partGivenAwake - squads the tripwire found given AND awake here; ownKeptAsleep / ownKeptHandUnread - people of a wholly given squad's
   not-offered put-away whose hand is not a given hand (this game's own) or did not read: kept asleep here, indexed (HIGH-1 b) */
static long long g_wwFollowHeldPart = 0, g_wwPartGivenAwake = 0, g_wwOwnKeptAsleep = 0, g_wwOwnKeptHandUnread = 0;
/* T-1318 attempt 3 (owner 1615 a) - worldWhole3[..]: xferKeptAsleep / followKeptAsleep - passes a forced / follow-leader XFER was NOT sent
   because people of the squad are kept asleep here (cooplo::kWxKeptAsleep); rebuiltKeptGiven - re-wakes whose own part left the index while
   its given part stayed (cooplo::kSrDropOwn); rewokeNotPerson - re-wake tests that found the squad id awake here but not the person
   (cooplo::RewokeBodyIs - the old squad-id test would have said re-woken). */
static long long g_wwXferKeptAsleep = 0, g_wwFollowKeptAsleep = 0, g_wwRebuiltKeptGiven = 0, g_wwRewokeNotPerson = 0;
static std::set<std::pair<int, unsigned int> > g_wwHeldLogged;   /* review LOW-1: (0 forced / 1 follow-leader, leader uid) whose NOT SENT line was written - one line per squad, then counted only */
static long long g_wwOwnKeptLogged = 0;
static long long g_relGivenNotOffered = 0, g_relGivenNotOfferedLogged = 0;   /* T-1318 fold 2: put-aways of a squad still wholly given at an accepted ACK, not offered again (ReleaseFlush) */
/* the frame's put-aways per engine squad (NotifyDespawn's main-thread branch), opened by ReleaseFlush at the next frame */
/* T-1318 attempt 3 fold 1 (review MED-1): awakeKey / flight - a batch that carries the put-aways of an ended XFER flight (ReleaseOpenFromFlight)
   keeps the XFER leader its raid's AWAKE block went under and the "from an XFER flight" mark, through the hold, into its offer */
/* T-1318 attempt 4 (owner 1626, design A; T2343, F-T2343-UNNUMBERED) - the engine's own member list at each put-away: unaccAwake - living
   members of the squad's engine list with no uid (never numbered) or this game's uid, seen at a put-away (ReleaseOnPutAway through ReadSquadOf;
   another game's people are run there - not counted, a4 fold 1 HIGH-2); over64 - that list held over 64 (never whole); chars / known - the
   list's count and its members with a uid, for the lines. Each object once (sets). T-1318 a4 fold 1 (review HIGH-1 / LOW-1): handsAcc - the
   hands of the list's corpses and of another game's people (accounted: never KEPT by the squad's sleeping records, RelRecordsKept); ap - the
   engine squad (ActivePlatoon) the put-aways came from, 0 unread (cooplo::RebuiltIsRewake). The despawn hook's never-numbered road (attempt
   4 site 3) is gone: NotifyDespawn never runs for a body with no mirror / index row, so it never saw one. */
struct RelBatch { unsigned int keyUid, awakeKey; int flight; Sector sector; std::vector<RelMember> mem; float px, pz; std::set<const void*> unaccAwake; std::set<std::string> handsAcc; void* ap; int over64, chars, known; RelBatch() : keyUid(0), awakeKey(0), flight(0), px(0.0f), pz(0.0f), ap(0), over64(0), chars(-1), known(-1) { sector.x = -1; sector.y = -1; } };
std::map<void*, RelBatch> g_relBatch;
/* T-1318 a4 fold 1 (review MEDIUM-1): per squad key, the hands of its corpses seen in its engine list at a put-away - a corpse's sleeping
   record counts as neither given nor kept (cooplo::SleepersTakenSlot, RelRecordsKept). Bounded; cleared at the world teardown. */
static std::map<HoKey, std::set<std::string> > g_deadHandsOf;
static const size_t kDeadHandsSquadCap = 8192, kDeadHandsPerSquadCap = 64;
/* T-1318 a4 fold 1 (review MEDIUM-2): squads woken here with given people (HandoffNoteWokeWithGiven) - the sweep removes their re-made
   bodies whose own hand is given. Bounded; cleared at the world teardown. */
static std::set<HoKey> g_wokeWithGiven;
static long long g_wokeWithGivenNoted = 0, g_wokeWithGivenFull = 0;
/* worldWhole6[..] (T-1318 a4 fold 1): recordsRead / recordsUnread - RELEASE decisions with no awake list that read the squad's own sleeping
   records / found them unread (the members seen listed then count as kept); relUnaccKeptOut / relUnaccHeld - put-away batches settled
   asleep / held because of unaccounted people of their squad (kept by its records or seen listed / living and listed); relOver64 is
   worldWhole's; xferUnacc / followUnacc - forced / follow-leader XFER passes not sent because the engine's list holds living members outside
   the people this game runs (or over 64); rebuiltSameSquad - sweep meetings of a never-numbered member of a squad whose held put-aways came
   from that same awake squad (no re-wake, LOW-1); unaccLines - the capped lines written */
static long long g_wwRecRead = 0, g_wwRecUnread = 0, g_wwRelUnaccKeptOut = 0, g_wwRelUnaccHeld = 0, g_wwXferUnacc = 0, g_wwFollowUnacc = 0, g_wwRebuiltSameSquad = 0, g_unaccLines = 0;
/* T-1318 a4 fold 2 (re-check LOW-A / LOW-B), worldWhole7: recDeadSkipped / recDeadUnread - records not accounted for whose body read dead at the
   RELEASE (neither kept nor given) / whose death did not read (kept, the safe side), per decide; recUnreadHeld - decides HELD because no awake
   list and no records read where the numbered people alone would go whole; recUnreadRead - such holds later decided by their records;
   recUnreadGone / recUnreadCap - such holds ended settled asleep here, whole (the squad gone / the try cap) */
static long long g_wwRecDeadSkipped = 0, g_wwRecDeadUnread = 0, g_wwRecHold = 0, g_wwRecHoldRead = 0, g_wwRecHoldGone = 0, g_wwRecHoldCap = 0;
static const long long kUnaccLineCap = 100;
static char g_relFlightBatchKey[256];   /* T-1318 attempt 3 fold 1: g_relBatch keys for an ended flight's put-aways with no batch of their squad this frame (a static address is never an engine squad) */
static long long g_relFlightBatched = 0, g_relFlightBatchFull = 0;
/* T-1318 attempt 3 fold 2 (re-check MED-A) - worldWhole5[..]: batchesMerged - a frame's put-away batches that joined another batch of their
   squad before the decision (ReleaseFlush, cooplo::ReleaseMergeBase); heldOpen - batches held because an offer / XFER of their squad was still
   open (cooplo::kHdHold on openOfSquad); heldGivenSettled - held people settled asleep here because another game took their squad while they
   waited (cooplo::kHdGiven) */
static long long g_relBatchesMerged = 0, g_wwHeldOpen = 0, g_wwHeldGivenSettled = 0;
/* T-1318 attempt 3c (owner 1617 a): put-away batches of a world squad HELD UNDECIDED while people of that squad are awake here - per squad key,
   the held people with the data read once at each put-away while the body was whole (RelMember: uid, position / facing, key, hand, gen) and
   the last batch's key uid / sector / position. Nothing in it points at a body: the engine holds the people asleep as it put them away. A held
   person is this game's (RelHolds: its UNLOAD stays held, a ROSTER CHECK is answered PENDING; IdxOpenHere). Decided by events only:
   RelHeldDecide (a later batch of the squad, or the 1 Hz re-check RelHeldTick), the sweep's re-wake (HandedOverLeftBehindRebuilt), the
   world teardown (ReleaseForgetDrain). */
struct RelHeld { RelBatch b; int batches, recTries; RelHeld() : batches(0), recTries(0) {} };   /* recTries - T-1318 a4 fold 2 (re-check LOW-A): decides of this hold that met the squad's records unread (cooplo::RecordsUnreadStep) */
static std::map<HoKey, RelHeld> g_relHeldSq;
static long long g_wwHeldBatches = 0, g_wwHeldPeople = 0, g_wwOfferedTogether = 0, g_wwSettledAfterHold = 0, g_wwDroppedOnWake = 0, g_wwHeldNotMine = 0;
/* late adopters of a uid whose later offer is still open - their REVOKE waits for the settle */
struct RelLate { int slot; unsigned int peer, gen; };
std::map<unsigned int, std::vector<RelLate> > g_relLate;
/* how a uid's release ended: the winner's slot (-1 = asleep / cancelled) and gen - a later late ACK is answered by it */
struct RelDone { int slot; unsigned int gen; double at; };
std::map<unsigned int, RelDone> g_relDone;
const double kRelDoneKeepSec = 300.0;
const long long kRelLogCap = 5000;
/* REPORT release[sent,adopted,dropped,deferred,asleep,resent,revoked,cancelled]: sent = people entering a release (once each); adopted /
   asleep / cancelled = how each ended (sent = adopted + asleep + cancelled + still open + left as no longer ours); dropped / deferred = the
   ACK verdicts read; resent = offers sent again; revoked = REVOKEs sent. */
long long g_relSent = 0, g_relAdopted = 0, g_relDropped = 0, g_relDeferred = 0, g_relAsleep = 0, g_relResent = 0, g_relRevoked = 0, g_relCancelled = 0;
long long g_relOffers = 0, g_relHeld = 0, g_relNext = 0, g_relLateAccepted = 0, g_relAckWrongSender = 0, g_relAckStale = 0, g_relAckUnknown = 0;
long long g_relDupAck = 0, g_relNotMine = 0, g_relLogged = 0, g_relWorldCleared = 0;   /* T-650 fold 1: cornerGiver / cornerRecv went with both corner rules */
long long g_relSkipOffThread = 0, g_relSkipUnread = 0, g_relSkipPlayer = 0, g_relSkipNotAnnounced = 0, g_relSkipInFlight = 0, g_relFlightNotTaken = 0, g_relFlightAbandoned = 0;
/* the people a same-id retire withdraws (handoff.h SameIdWithdrawMark): uid -> kSameIdYield / kSameIdReload, at most kSameIdWdCap (a uid
   not marked when full goes the release road). g_relSkipYield / g_relSkipReload: their put-aways not offered, by kind; g_relFlightSameId:
   their held UNLOADs sent at the flight's end; g_sameIdWdMarked / g_sameIdWdFull: marked, refused for the cap */
static std::map<unsigned int, int> g_sameIdWd;
static const size_t kSameIdWdCap = 512;
static long long g_relSkipYield = 0, g_relSkipReload = 0, g_relFlightSameId = 0, g_sameIdWdMarked = 0, g_sameIdWdFull = 0;
/* the TEST-ONLY clearnear lever's marks (kSameIdClearNear): its removed squads' put-aways not offered to another game */
static long long g_relSkipClearNear = 0;
/* the receiver side: offers read and each member's verdict; REVOKEs read and what they did */
long long g_relInOffers = 0, g_relInAdopted = 0, g_relInDup = 0, g_relInDropped = 0, g_relInStale = 0, g_relInDeferred = 0;
long long g_relInRevokes = 0, g_relInPuppeted = 0, g_relInRemoved = 0, g_relInRevokeIgnored = 0;
/* fold 1 [a1b2f1-hd2] [review F4]: a REVOKE not sent stays OWED and the 1 Hz release tick sends it again until it goes; a REVOKE read while
   this game's engine writes are blocked WAITS for the next safe point (HandoffTick). Neither is dropped (a 4096 cap guards memory: counted). */
struct RelRevokeOwed { int slot; unsigned int uid; int winnerSlot; unsigned int winnerGen, lateGen; };
static std::vector<RelRevokeOwed> g_relRevokeOwed;
struct RelRevokeWait { unsigned int uid, wgen, winnerSlot, fromPeer; int from; };
static std::vector<RelRevokeWait> g_relRevokeWait;
static long long g_relRevokeOwedAdded = 0, g_relRevokeRetrySent = 0, g_relInRevokeWaited = 0, g_relInRevokeWaitApplied = 0, g_relRevokeCapDropped = 0;
static long long g_relRewokeAtAck = 0, g_relRewokeAtSettle = 0, g_relSkipNoKey = 0, g_relFlightNoKey = 0, g_puppetNoBody = 0, g_puppetNoBodyLogged = 0;
/* fold 2 [a1b2f2-hd0] [review G1/G2]: rewokeAcceptAtAck / rewokeLateAccepted = adoptions after a re-wake ACCEPTED by the area rule (another game
   holds the area, or its holder is unknown: rewokeHolderUnknown); revoke{Wait,Owed}WorldCleared = REVOKEs dropped by the world teardown;
   revoke{Wait,Owed}PlayerGone = dropped because the player they came from / were owed to left; goneSlotUnknown = a player gone with no slot */
static long long g_relRewokeAcceptAtAck = 0, g_relRewokeLateAccepted = 0, g_relRewokeHolderUnknown = 0;
/* T-1268 fold 1 (review M2): a re-woken member whose late adopter came while nobody holds its area yet - set aside, decided again by the
   next applied area map. kind 0 = an adoption ACK (from / fromPeer / gen), kind 1 = a settle with late adopters (late). */
struct RelRewakeWait { int kind; RelMember rm; int from; unsigned int fromPeer, gen, settleGen, seq; std::vector<RelLate> late; };
static std::vector<RelRewakeWait> g_relRewakeWait;
static long long g_relRewakeWaitPut = 0, g_relRewakeWaitDecided = 0, g_relRewakeWaitStillNobody = 0, g_relRewakeWaitNotMine = 0, g_relRewakeWaitWorldCleared = 0;
static long long g_relRewakeWaitLateAdded = 0, g_relRewakeWaitPlayerGone = 0;   /* T-1268 fold 2 */
static long long g_relRevokeWaitWorldCleared = 0, g_relRevokeOwedWorldCleared = 0, g_relRevokeWaitPlayerGone = 0, g_relRevokeOwedPlayerGone = 0, g_relRevokeGoneSlotUnknown = 0;
static std::vector<int> g_relRevokeGoneSlots;   /* MAIN THREAD: players gone whose REVOKEs are still to drop (ReleaseForgetDrain) */
static std::vector<int> g_idxGoneSlots;         /* MAIN THREAD: players gone whose squad-index rows (people handed to them) are still to drop (ReleaseForgetDrain) */
static long long g_idxPlayerGoneSquads = 0, g_idxPlayerGonePeople = 0;   /* squads that left the index / people given to a player who left */
/* T-650 fold 1: forced-XFER squads (by key) whose KEEP (it left this game's loaded list and no other in-world game has the area loaded, or
   the map is not fresh) was logged / whose loaded-but-not-in-world game was counted - once per squad until it is back in the loaded list */
std::set<unsigned int> g_keepNoted, g_niwNoted;
/* T-650 fold 1 (review MED-1): the last forced hand-over of each squad (by key) - its receiver and the squad's sector at the send; refused =
   the receiver's ACK kept a member. A refused one is not sent again to the same receiver while the squad stands in the same sector (an
   event clears it: the sector or the receiver changes), so a receiver that cannot take is not offered the same squad every second. */
struct ForcedTry { int target, x, y, refused; ForcedTry() : target(-1), x(-1), y(-1), refused(0) {} };
std::map<unsigned int, ForcedTry> g_forcedTry;
/* REPORT releaseHolder[offer,none,hold,keep,keepUnknown,leave,notInWorld,backoff]: offer = RELEASE offers to a game that has the area
   loaded; none = releases that ended asleep with no game offered (no other in-world game has the area loaded); hold = releases held for a
   fresh area map; keep / keepUnknown = squads that left this game's loaded list, kept because no other in-world game has the area loaded /
   the map is not fresh (once per squad per departure); leave = forced hand-overs sent; notInWorld = a game with the area loaded skipped as
   not IN_WORLD (RELEASE: per offer or asleep decision; forced: once per squad per departure); backoff = forced hand-overs refused (a member
   kept) and backed off */
long long g_holderOffer = 0, g_holderNone = 0, g_holderHold = 0, g_holderKeep = 0, g_holderKeepUnknown = 0, g_holderLeave = 0, g_holderNotInWorld = 0, g_holderBackoff = 0;
/* T-650 fold 2 REPORT keep[notKeep,asleepNoKeep,readFailTake,readFailForced,forcedListed]: notKeep = offer / asleep / forced-keep decisions in
   which a game with the area loaded was passed over because its engine would not keep the squad at its spot (cooplo::EngineKeepsAt);
   asleepNoKeep = releases that ended asleep for that reason; readFailTake = takes whose live keep read was unreadable (refused);
   readFailForced = forced passes skipped on an unreadable keep read (decided again next tick); forcedListed = forced hand-overs sent while
   this game's loaded list still named the area (the squad stood in its edge strip) */
long long g_keepNotKeep = 0, g_keepAsleepNoKeep = 0, g_keepReadFailTake = 0, g_keepReadFailForced = 0, g_keepForcedListed = 0;
/* uid -> the squad key of a refused forced hand-over naming it (HandoffForcedRefusedFor reads g_forcedTry through it) */
std::map<unsigned int, unsigned int> g_forcedRefusedUid;
/* world teardown / the other player gone: interlocked requests (the teardown may run OFF the main thread inside a __finally - POD only);
   the main thread empties the state at its next use (ReleaseForgetDrain) */
volatile LONG g_relForgetReq = 0, g_idxPeerGoneReq = 0;
/* M7a2 fold 3 [m7a2h-hc0] (kept): passHeld = people the per-second forced XFER or the follow-leader XFER left out because another XFER in
   flight names them, counted once per (person, holding flight); passHeldEmpty = such XFERs not sent at all (every member held). */
static long long g_passHeld = 0, g_passHeldEmpty = 0, g_passHeldLogged = 0;
static std::set<std::pair<unsigned int, unsigned int> > g_passHeldSeen;   /* (uid, holding flight's key) already counted; pruned when that flight settles */
/* T-1 B1 */
const double kFollowBackoffSec = 5.0;        // a follow the receiver did not take is offered again after this
std::map<unsigned int, double> g_followBackoff;   // leader uid -> not before (owner side)
/* T-1239 fix attempt 1 (owner side): per leader uid the receiver named, the follow-leader members it refused as 'not in the leader's
   group here' (its own engine groups the copies otherwise) and both announcements of the squad when that XFER went. While a refused
   member is run here, that leader is still the agreed one and both announcements are as marked, the group goes by position (keep
   check, area hand-over) and is not sent by follow-leader; the first pass that sees either announcement changed lifts the block. */
struct FollowGroupBlock { std::set<unsigned int> members; coopsquad::FollowAnnounceMark mark; };
std::map<unsigned int, FollowGroupBlock> g_followGroupBlock;
long long g_followNotInGroup = 0, g_followBlockSet = 0, g_followBlockedPass = 0, g_followUnblocked = 0;   /* receiver refusals (members); blocks set; passes a send / wait went by position; blocks lifted by an announcement change */
/* T-1 B1 restructure (re-check N1-N5 of ff655c5): MSG_SQUAD_LEAD (src/common/squadlead.h). SENDER: one record per squad this game
   runs members of (keyed by its ActivePlatoon*) - the key it is announced under and what was last SENT; it is announced again when
   any of that changes, to every new link, and (n = 0) when this game runs no member of it any more. A send that fails stays due and
   goes again next second (N3a). RECEIVER: g_peerLead, the other game's announcements (the newest seq per its key). */
struct MyLead { unsigned int key; unsigned int acting, formal; std::vector<unsigned int> members; int sent; int has; int cats; int areaKey; unsigned int seq; };   /* T-1239: seq - the seq of the last send (0 none), the sender's half of FollowAnnounceMark */   /* T-1 B3 restructure: has / cats - the money last sent. Fold 1 [a1b1f1-hd0] [F3]: areaKey - the leader's area key the last announcement went with (-1 none: WORLD) */
std::map<void*, MyLead> g_myLead;
unsigned int g_myLeadKeyNext = 0, g_myLeadSeq = 0;
const int kLeadSendsPerPass = 64;            // announcements per 1-Hz pass; the rest go next second
coopsquad::PeerLeadBook g_peerLead;
/* T-1 B3 restructure (protocol 89): the squad's money. g_catsAdopt: leader uid this game has TAKEN -> the other game's announcement
   last written into its pot (point 3); retired when that game no longer announces the leader or this game no longer runs it. */
struct CatsAdopt { unsigned int seq; int cats; };
std::map<unsigned int, CatsAdopt> g_catsAdopt;
/* T-1 B3 fold (M2): leader uid TAKEN here -> the other game's newest announcement of it, snapshotted at the XFER and refreshed while
   the book has a newer one, so a take-over write held for this game's in-flight trades survives the giver's n = 0. Ends at the adopt. */
std::map<unsigned int, CatsAdopt> g_catsTakeSnap;
long long g_catsOwnPotSkipped = 0, g_catsAdoptHeld = 0, g_catsFinalSent = 0, g_catsUndoNotRunner = 0, g_catsUndoPotChanged = 0, g_catsUndoShort = 0;   /* T-1 B3 fold */
std::map<unsigned int, unsigned int> g_catsUnreadSeq;   /* leader -> the announcement seq its unreadable pot was counted for (0 = own read) */
struct CatsCopyPot { void* own; unsigned int leader, seq; int cats, cur; };
long long g_catsSent = 0, g_catsApplied = 0, g_catsHeld = 0, g_catsHandoverAdopted = 0, g_catsUndoRefused = 0, g_catsPotUnread = 0;
long long g_catsHandoverDelta = 0, g_catsUndoFailed = 0, g_catsLogIn = 0;
int g_catsDue = 1, g_catsInFlightLast = 0;
DWORD g_catsDrainAt = 0;
long g_leadLinkGen = -1;                     // the session link generation both books belong to
std::map<unsigned int, double> g_crossRefused;   // point 4: an incoming follow key refused as a crossing -> until (its resends are refused too)
std::map<void*, DWORD> g_keepAwakeAt;        // faction -> last keep-awake pass (1 Hz)
long long g_followedLeader = 0, g_leaderUnknownFellBack = 0, g_waitedLeader = 0, g_xferFollowSent = 0, g_xferFollowTaken = 0;
long long g_leaderSet = 0, g_leaderSetMiss = 0, g_keepAwakeRefresh = 0, g_leaderSetLogged = 0;
long long g_leaderSetPrecond = 0, g_agreedDiffered = 0, g_followLogQuiet = 0, g_leaderActing = 0;
long long g_squadLeadSent = 0, g_squadLeadRecv = 0, g_squadLeadApplied = 0, g_squadLeadStale = 0, g_crossingResolved = 0, g_crossingRefused = 0, g_squadLeadFull = 0;
std::map<unsigned long long, long long> g_followLogSig;   // (side << 32 | leader uid) -> what was last logged
/* review M1: a refused follow is offered again every 5 s - its lines are logged only when what they say changes.
   side 0 = the follow send, 1 = its ACK, 2 = the receiver's take. true = log. */
bool FollowLogChanged(unsigned int side, unsigned int leader, long long sig)
{
    const unsigned long long k = ((unsigned long long)side << 32) | (unsigned long long)leader;
    std::map<unsigned long long, long long>::iterator it = g_followLogSig.find(k);
    if (it != g_followLogSig.end() && it->second == sig) { ++g_followLogQuiet; return false; }
    if (g_followLogSig.size() > 8192) g_followLogSig.clear();
    g_followLogSig[k] = sig;
    return true;
}
void FollowLogReset(unsigned int side, unsigned int leader) { g_followLogSig.erase(((unsigned long long)side << 32) | (unsigned long long)leader); }
/* review M1: the engine's own "cannot lead" reads (0x791FF0), read not called: dead = Character::hasDied's byte +0x5BC (hasDied
   0x620B20; appearance.cpp DeadPod), in a bed or cage = inSomething +0x2F8 (the int 0x791FF0 tests). 1 out, 0 not, -1 unreadable. */
int LeaderDeadOrHeldPod(::Character* c)
{
    __try { return (*(unsigned char*)((char*)c + 0x5BC) != 0 || *(int*)((char*)c + 0x2F8) != 0) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* THE can't-lead rule, one copy (T-1442 attempt 3 review fold): LeaderOut and the engine writers' hooks (LeadOutReadPod) both ask it.
   1 out, 0 not, -1 unreadable. Dead or in a bed or cage (LeaderDeadOrHeldPod), else knocked down or ragdolled - the mod's one downed
   predicate (replicate.cpp IsDownedCharacter: prone +0xE0, or the engine's inRagdoll 0x7D08A0 - carried +0x3D4 or the physics ragdoll
   on), for the engine's vt+0x30 test. ANY THREAD: guarded reads and the engine's read-only inRagdoll only. */
int LeaderOutAnswer(::Character* c)
{
    if (!Plaus(c)) return -1;
    const int dh = LeaderDeadOrHeldPod(c);
    if (dh != 0) return dh;
    return IsDownedCharacter(c) ? 1 : 0;
}
bool LeaderOut(::Character* c)
{
    return LeaderOutAnswer(c) != 0;   // unreadable fails closed
}
/* Both books belong to one set of players. The first to see a new epoch - another player entered the world, or the old link went
   down (StorePeerEpoch; the 1-Hz pass, or an arriving announcement) - forgets the others' announcements, forgets what this game
   announced (so all of it goes again, to whoever is here now), and forgets refused crossings. */
void LeadLinkEdge()
{
    const long gen = coop::StorePeerEpoch();
    if (gen == g_leadLinkGen) return;
    g_leadLinkGen = gen;
    g_peerLead.Clear(); g_myLead.clear(); g_crossRefused.clear();
    g_followGroupBlock.clear();   /* T-1239: every announcement is heard again - a change */
    g_catsAdopt.clear(); g_catsUnreadSeq.clear(); g_catsDue = 1;   /* T-1 B3 restructure: the money is heard again too */
    g_catsTakeSnap.clear();   /* T-1 B3 fold (M2): a snapshot belongs to its link */
}
/* The other game's announcement for THIS squad: the newest among the announcements that list a member of it this game does not run.
   Used AS IS - no standing re-judgement on the copies here (N1). 0 = the other game announced nothing for it. */
const coopsquad::PeerLeadEntry* PeerAnnouncedFor(const SquadView& sv, const unsigned int* uids)
{
    const coopsquad::PeerLeadEntry* best = 0;
    for (int k = 0; k < sv.stored; ++k)
    {
        if (uids[k] == 0 || net::IsUidMine(uids[k])) continue;
        const coopsquad::PeerLeadEntry* e = g_peerLead.ForUid(uids[k]);
        if (e == 0) continue;
        if (best == 0) { best = e; continue; }
        if (e->sender == best->sender) { if (e->seq > best->seq) best = e; continue; }   /* one game's announcements: the newest */
        const unsigned int other[1] = { e->acting };   /* M7a A1 build 1 [a1b1-hd11] (3.12): several games - AgreedSquadLeaderN's pick */
        if (cooplo::AgreedSquadLeaderN(best->acting, other, 1) != best->acting) best = e;
    }
    return best;
}
unsigned int NextLeadSeq() { if (++g_myLeadSeq == 0) ++g_myLeadSeq; return g_myLeadSeq; }
/* Announce one of my squads when what it says differs from what was last sent (or nothing was sent).
   T-1 B3 restructure (protocol 89): haveCats 1 = this game runs the squad's formal leader and read its pot (`cats`) - the
   announcement carries the squad's money, and the formal leader is kept in the list (the receiver checks the sender runs it).
   Returns 1 when what was last sent is current (sent now, or unchanged), 0 when it is due and did not go (the cap or the link). */
int AnnounceLead(void* ap, unsigned int acting, unsigned int formal, const std::vector<unsigned int>& run, int haveCats, int cats, int* sends)
{
    std::vector<unsigned int> mem(run);
    std::sort(mem.begin(), mem.end());
    if (mem.size() > (size_t)coopsquad::kSquadLeadMaxMembers)
    {
        mem.resize(coopsquad::kSquadLeadMaxMembers);
        if (haveCats != 0 && formal != 0 && !std::binary_search(mem.begin(), mem.end(), formal)) { mem.back() = formal; std::sort(mem.begin(), mem.end()); }
    }
    const int has = (haveCats != 0 && coopsquad::LeadCarriesCats(formal, mem)) ? 1 : 0;
    const int money = (has != 0) ? cats : 0;
    std::map<void*, MyLead>::iterator it = g_myLead.find(ap);
    if (it == g_myLead.end())
    {
        MyLead r; if (++g_myLeadKeyNext == 0) ++g_myLeadKeyNext;
        r.key = g_myLeadKeyNext; r.acting = 0; r.formal = 0; r.sent = 0; r.has = 0; r.cats = 0; r.areaKey = -1; r.seq = 0;
        it = g_myLead.insert(std::make_pair(ap, r)).first;
    }
    MyLead& r = it->second;
    int areaKey = acting != 0 ? coop::CharAreaKeyNow(acting) : -1;   /* [a1b1f1-hd1] [F3]: SendSquadLead's own choice - the acting leader's sector, else the first readable member's */
    for (size_t i = 0; areaKey < 0 && i < mem.size(); ++i) areaKey = coop::CharAreaKeyNow(mem[i]);
    if (!cooplo::LeadAnnounceDue(r.sent, (r.acting == acting && r.formal == formal && r.members == mem && r.has == has && r.cats == money) ? 1 : 0, r.areaKey, areaKey)) return 1;
    if (*sends >= kLeadSendsPerPass) return 0;   // the rest next second
    coopsquad::SquadLeadMsg m; m.squadKey = r.key; m.acting = acting; m.formal = formal; m.members = mem; m.seq = NextLeadSeq();
    m.cats = money; m.hasCats = (unsigned char)has;
    if (!net::SendSquadLead(m)) return 0;         // not sent: due again next second (N3a)
    ++*sends; ++g_squadLeadSent; if (has != 0) ++g_catsSent;
    r.acting = acting; r.formal = formal; r.members.swap(mem); r.sent = 1; r.has = has; r.cats = money; r.areaKey = areaKey; r.seq = m.seq;
    return 1;
}
/* T-1 B3 restructure: a pot that could not be read (or written) - counted once per leader and announcement. */
void CatsPotUnread(unsigned int leader, unsigned int seq)
{
    std::map<unsigned int, unsigned int>::iterator u = g_catsUnreadSeq.find(leader);
    if (u != g_catsUnreadSeq.end() && u->second == seq) return;
    if (g_catsUnreadSeq.size() > 8192) g_catsUnreadSeq.clear();
    g_catsUnreadSeq[leader] = seq; ++g_catsPotUnread;
}
/* T-1 B3 restructure, point 3 (F1): what this game, which runs `leader` of squad `ap`, owes the other game's announcement of it -
   kCatsAbsolute when it has just TAKEN the squad (the other game's last announcement still names that leader and carries its
   pot), kCatsDelta for the giver's own change inside the hand-over round trip, kCatsNone otherwise. A squad this game has been
   announcing with its money itself is never overwritten by the other game's claim (a hand-back in flight: its ACK settles it). */
int CatsAdoptAction(void* ap, unsigned int leader, long long* delta, unsigned int* seqOut, int* catsOut)
{
    *delta = 0; *seqOut = 0; *catsOut = 0;
    const coopsquad::PeerLeadEntry* e = g_peerLead.ForLeader(leader);
    /* T-1 B3 fold (M2): the take snapshot - refreshed from a newer announcement, used once the giver's n = 0 has removed it */
    std::map<unsigned int, CatsAdopt>::iterator s = g_catsTakeSnap.find(leader);
    if (e != 0 && s != g_catsTakeSnap.end() && e->seq > s->second.seq) { s->second.seq = e->seq; s->second.cats = e->cats; }
    unsigned int seq = 0; int cats = 0;
    if (e != 0) { seq = e->seq; cats = e->cats; }
    else if (s != g_catsTakeSnap.end()) { seq = s->second.seq; cats = s->second.cats; }
    else return coopsquad::kCatsNone;
    std::map<unsigned int, CatsAdopt>::const_iterator a = g_catsAdopt.find(leader);
    const int hasRec = (a != g_catsAdopt.end()) ? 1 : 0;
    if (hasRec == 0)
    {
        std::map<void*, MyLead>::const_iterator r = g_myLead.find(ap);
        if (r != g_myLead.end() && r->second.sent != 0 && r->second.has != 0) return coopsquad::kCatsNone;
    }
    *seqOut = seq; *catsOut = cats;
    return coopsquad::HandoverCatsAction(seq, cats, hasRec, hasRec != 0 ? a->second.seq : 0u, hasRec != 0 ? a->second.cats : 0, delta);
}
/* T-1 B3 fold (M2 / M3): record `r` sent again with its formal leader's pot as this game announces it now (KeeperPotAnnounced),
   when that differs from the last sent - the same key, leaders and members. 1 = sent. */
int CatsResendRecord(MyLead& r)
{
    ::Character* f = FindSpawned(r.formal);
    if (!Plaus(f) || !net::IsUidMine(r.formal)) return 0;
    void* own = 0; int cats = 0;
    const int readOk = (KeeperPotAnnounced((void*)f, &own, &cats) == 1) ? 1 : 0;
    if (coopsquad::FinalAnnounceDue(r.sent != 0 ? r.has : 0, r.cats, readOk, cats) == 0) return 0;
    coopsquad::SquadLeadMsg m; m.squadKey = r.key; m.acting = r.acting; m.formal = r.formal; m.members = r.members; m.seq = NextLeadSeq();
    m.cats = cats; m.hasCats = 1;
    if (!net::SendSquadLead(m)) return 0;   /* the 1-Hz pass sends it */
    ++g_squadLeadSent; ++g_catsSent; r.cats = cats; r.seq = m.seq;   /* T-1239: an announcement change */
    return 1;
}
/* T-1 B3 fold (M3): at the ACK, before `uid` is released - a squad whose money this game announces through that formal leader gets
   its pot re-read and a final announcement when it moved inside the hand-over round trip (the taker adds it as a delta; an ACK
   inside a second used to beat the 1-Hz pass and the change was lost). */
void SquadCatsFinalBeforeRelease(unsigned int uid)
{
    if (!coop::PlayersPresent()) return;
    LeadLinkEdge();
    for (std::map<void*, MyLead>::iterator r = g_myLead.begin(); r != g_myLead.end(); ++r)
        if (r->second.sent != 0 && r->second.has != 0 && r->second.formal == uid && CatsResendRecord(r->second) != 0) ++g_catsFinalSent;
}
/* T-1 B3 fold (L4): `uid` released - a record naming it formal leader no longer carries cats here, even when its n = 0 / has-0
   announcement is still waiting on the 64-per-pass cap (a quick hand-back is then adopted, not refused as "announced here"). */
void SquadCatsReleased(unsigned int uid)
{
    for (std::map<void*, MyLead>::iterator r = g_myLead.begin(); r != g_myLead.end(); ++r)
    {
        const int has = coopsquad::LeadHasAfterRelease(r->second.formal, uid, r->second.has);
        if (has != r->second.has) { r->second.has = has; r->second.cats = 0; }
    }
}
/* My squads not seen this pass: the other game hears n = 0 and forgets them (this replaces the former-leader CONTEXT re-send, N3). */
void LeadDropUnseen(const std::set<void*>& seen, int* sends)
{
    for (std::map<void*, MyLead>::iterator it = g_myLead.begin(); it != g_myLead.end(); )
    {
        if (seen.count(it->first) != 0) { ++it; continue; }
        if (it->second.sent == 0) { g_myLead.erase(it++); continue; }   // the other game never heard of it
        if (*sends >= kLeadSendsPerPass) { ++it; continue; }
        coopsquad::SquadLeadMsg m; m.squadKey = it->second.key; m.seq = NextLeadSeq();
        if (!net::SendSquadLead(m)) { ++it; continue; }                  // due again next second
        ++*sends; ++g_squadLeadSent;
        g_myLead.erase(it++);
    }
}
void BuildMembers(const std::vector<unsigned int>& uids, std::vector<net::XferMember>* out)
{
    for (size_t k = 0; k < uids.size() && (int)out->size() < kMaxSquad; ++k)
    {
        ::Character* c = FindSpawned(uids[k]);
        if (!Plaus(c)) continue;
        net::XferMember m; std::memset(&m, 0, sizeof(m));
        m.uid = uids[k];
        ReadPos(c, &m.x, &m.y, &m.z);
        ReadAuthorityFacing(c, &m.fx, &m.fz);
        ReadAuthorityIntent(c, &m.intentType, &m.intentSubject, &m.ix, &m.iy, &m.iz);   /* T-1416 fix attempt 2: sent, never issued by the taker (counted / probed only) - later clean-up */
        out->push_back(m);
    }
}
long long g_nudges = 0, g_keptUntaken = 0, g_xferInOutsideRing = 0;
long long g_tick = 0, g_squadsSeen = 0, g_xferOut = 0, g_xferResent = 0, g_xferAbandoned = 0, g_ackIn = 0, g_released = 0;
long long g_xferIn = 0, g_taken = 0, g_takeCarriedNotIssued = 0, g_takeOrdersDropped = 0, g_takeOrdersDropUnread = 0, g_ackOut = 0, g_xferInUnknownUid = 0, g_teleports = 0;
std::set<void*> g_moneyKnown;   /* T-799 D1: the squad pots (Ownerships) whose money is known on this game - see coopsquad::MoneyKnownAfter */
long long g_moneyKnownMarks = 0, g_catsWithheldNow = 0;
long long g_moneyEv[coopsquad::kMoneyEvKinds] = { 0, 0, 0, 0, 0, 0, 0, 0 }, g_moneyKnownCleared = 0;   /* T-799 D1: marks asked, by event kind; marks cleared */
long long g_recMoneyCarried = 0, g_recMoneyNotCarried = 0;                     /* T-799 D2: records written with / without the money */
long long g_keeperPotMismatch = 0;   /* keepers whose pot (KeeperPot) is not their squad's embedded pot - the one store.cpp marks at a load */
long long g_moneyListWritten = 0, g_moneyListWriteFailed = 0, g_moneyListRead = 0, g_moneyListBad = 0;   /* the list beside a save (store.cpp) */
struct HolderRestockPend { int cats; long long delta; };
std::map<unsigned int, HolderRestockPend> g_holderRestock;   /* T-799 D4: formal leader -> the restock that arrived: its money and the change owed (coopsquad::HolderRestockDelta), added at the safe point */
std::map<unsigned int, int> g_hrResends;   /* T-799 D4: formal leader -> times a restock for it was sent on (coopsquad::HolderRestockResend) since one was written */
std::set<unsigned int> g_hrSentLogged, g_hrAppliedLogged;   /* T-799 D4: the leaders already logged */
long long g_hrSent = 0, g_hrSendFailed = 0, g_hrArrived = 0, g_hrApplied = 0, g_hrNotRunner = 0, g_hrUnread = 0, g_hrResent = 0, g_hrAdoptSuperseded = 0;

} // namespace
/* T-799 D1: the money-known mark of a squad pot (items.cpp's restock detour, this file's copy write and take-over adoption).
   Cleared with the world (items.cpp ItemsWorldTeardown): a pot of the next world is another object. MAIN THREAD. */
void KeeperMoneyKnownMark(void* own, int ev)
{
    if (own == 0) return;
    if (ev > 0 && ev < coopsquad::kMoneyEvKinds) ++g_moneyEv[ev];
    const int was = (g_moneyKnown.find(own) != g_moneyKnown.end()) ? 1 : 0;
    if (coopsquad::MoneyKnownAfter(was, ev) != 0) { if (g_moneyKnown.insert(own).second) ++g_moneyKnownMarks; }
    else if (was != 0) { g_moneyKnown.erase(own); ++g_moneyKnownCleared; }   /* a record without the money, or a freed pot */
}
int KeeperMoneyKnown(void* own) { return (own != 0 && g_moneyKnown.find(own) != g_moneyKnown.end()) ? 1 : 0; }
void KeeperMoneyKnownClear() { g_moneyKnown.clear(); g_catsWithheldNow = 0; g_holderRestock.clear(); g_hrResends.clear(); g_hrSentLogged.clear(); g_hrAppliedLogged.clear(); }
/* items.cpp: a keeper whose pot is not its squad's embedded pot (the mark store.cpp sets at a load would name another object) */
void KeeperPotMismatchNote() { ++g_keeperPotMismatch; }
/* store.cpp: the list beside a save - 0 written, 1 not written, 2 read at a load, 3 present but not a list (read as empty) */
void KeeperMoneyListNote(int kind)
{
    if (kind == 0) ++g_moneyListWritten; else if (kind == 1) ++g_moneyListWriteFailed; else if (kind == 2) ++g_moneyListRead; else ++g_moneyListBad;
}
/* T-799 D3: 1 = a take-over adoption of the other game's announced money is due for squad `ap` led by `leader` (this game runs it) -
   the keeper repair waits for that write, which is the known value. */
int SquadCatsAdoptDue(void* ap, unsigned int leader)
{
    long long delta = 0; unsigned int seq = 0; int cats = 0;
    return (CatsAdoptAction(ap, leader, &delta, &seq, &cats) != coopsquad::kCatsNone) ? 1 : 0;
}
/* T-799 D2: a record store.cpp wrote with (1) or without (0) the group's money - counted on the keeperMoney report. MAIN THREAD. */
void KeeperRecordMoneyNote(int carried) { if (carried != 0) ++g_recMoneyCarried; else ++g_recMoneyNotCarried; }
/* T-799 D4: the engine's restock ran on this game (the area's holder) for a keeper whose formal leader `leaderUid` may be another
   game's - then the money it set goes to that game (MSG_KEEPER_RESTOCK, to the leader's owner). MAIN THREAD (the restock detour). */
void KeeperRestockToRunner(unsigned int leaderUid, int cats)
{
    if (coopsquad::HolderRestockSend(1, (leaderUid != 0 && net::IsUidMine(leaderUid)) ? 1 : 0, leaderUid != 0 ? 1 : 0) == 0) return;
    coopsquad::KeeperRestockMsg m; m.leader = leaderUid; m.cats = cats;
    if (!net::SendKeeperRestock(m)) { ++g_hrSendFailed; return; }
    ++g_hrSent;
    if (g_hrSentLogged.insert(leaderUid).second)
        DebugLog("[KEEPER] holder restock sent: leader=" + N((long long)leaderUid) + " money=" + N((long long)cats)
                 + " (to the game that runs the keeper; once per leader, counted holderRestock[sent])");
}
/* T-799 D4: a restock for a leader this game does not run (a hand-over in flight) goes on to the leader's owner as this game knows
   it, a limited number of times per leader between two written restocks; past that, or with no owner known, it is dropped (counted
   holderRestock[notRunner]). MAIN THREAD. */
void KeeperRestockResend(const coopsquad::KeeperRestockMsg& m)
{
    int& n = g_hrResends[m.leader];
    if (coopsquad::HolderRestockResend(n) == 0) { ++g_hrNotRunner; return; }
    ++n;
    if (net::SendKeeperRestock(m)) ++g_hrResent; else ++g_hrNotRunner;
}
/* T-799 D4: a restock the area's holder sent. Taken at its arrival (the drain, MAIN THREAD): the change owed is the restocked money
   less the pot as this game announces it now (coopsquad::HolderRestockDelta - this game's priced requests still in flight on the pot
   are applied by the holder after its restock, as their answers come by the same ordered road behind it); the change is added at
   the next safe point (SquadCatsSafePointDrain). A newer restock replaces one not yet added: the engine's restock SETS the money. */
void KeeperRestockArrive(const coopsquad::KeeperRestockMsg& m)
{
    ++g_hrArrived;
    ::Character* c = FindSpawned(m.leader);
    const int mine = net::IsUidMine(m.leader) ? 1 : 0;
    void* own = 0; int announced = 0;
    const int read = (mine != 0 && Plaus(c) && KeeperPotAnnounced((void*)c, &own, &announced) == 1) ? 1 : 0;
    const int act = coopsquad::HolderRestockApply(mine, read);
    if (act == coopsquad::kHrNotRunner) { KeeperRestockResend(m); return; }
    if (act != coopsquad::kHrApply) { ++g_hrUnread; return; }
    HolderRestockPend& p = g_holderRestock[m.leader];
    p.cats = m.cats; p.delta = coopsquad::HolderRestockDelta(m.cats, announced);
    g_catsDue = 1;
}

// par23b: Teleport above, unchanged, reachable from items.cpp (packbuytest goto).
bool TeleportCharacter(::Character* c, float x, float y, float z) { return Teleport(c, x, y, z); }

// T-1 B1: the squad reader (handoff.h).
int ReadSquadAt(void* ap, SquadView* o)
{
    std::memset(o, 0, sizeof(*o));
    if (!Plaus(ap)) return 0;
    const int r = SquadReadPod(ap, o);
    if (r != 1) { std::memset(o, 0, sizeof(*o)); return r; }
    o->posUsable = coopsquad::SquadPosUsable(o->x, o->y, o->z);
    return 1;
}
int ReadSquadOf(::Character* c, SquadView* o)
{
    std::memset(o, 0, sizeof(*o));
    if (!Plaus(c)) return 0;
    void* ap = 0;
    if (SquadApOfPod(c, &ap) == 0) return -1;
    if (!Plaus(ap)) return 0;
    return ReadSquadAt(ap, o);
}
void* SquadActivePlatoonOf(::Character* c)
{
    SquadView sv;
    return ReadSquadOf(c, &sv) == 1 ? sv.ap : 0;
}
/* T-1122 awake hand (store.h): the awake squad of uid when this game runs uid (*mine 1), else 0 */
void* HandoffMySquadApOf(unsigned int uid, int* mine)
{
    *mine = (uid != 0 && net::IsUidMine(uid)) ? 1 : 0;
    if (*mine == 0) return 0;
    ::Character* c = FindSpawned(uid);
    return Plaus(c) ? SquadActivePlatoonOf(c) : 0;
}
/* T-164 B4-3 (M4, owner 125-130): a trader squad whose formal leader has a home building is decided by the home building's
   position (ItTraderHomePos, items.cpp) - where its shop stock and its restock are decided - so one game runs the keeper, holds
   its stock and restocks it; every other squad by its own position (coopsquad::SquadDecisionChoice). */
int SquadDecisionPosAt(const SquadView& sv, float* x, float* z)
{
    float hx = 0, hz = 0;
    const int home = ItTraderHomePos(sv.ap, sv.platoon, (void*)sv.leader, &hx, &hz);
    const int choice = coopsquad::SquadDecisionChoice(home, sv.posUsable);
    if (choice == coopsquad::kDecideHome) { *x = hx; *z = hz; return 1; }
    if (choice == coopsquad::kDecideSquad) { *x = sv.x; *z = sv.z; return 1; }
    return 0;
}
int SquadDecisionPos(::Character* c, float* x, float* z)
{
    SquadView sv;
    if (ReadSquadOf(c, &sv) != 1) return 0;
    return SquadDecisionPosAt(sv, x, z);
}

/* M7a A1 build 2 [a1b2-hd0]: the teardown's and the peer-gone requests, honoured on the main thread before any use of the release / index
   state. The other player gone: the squad index goes (as the marks did). World teardown: the open releases, the frame's batch and the index
   go (their faction pointers belong to the freed world; an open release's held UNLOADs are not sent - its world is gone). g_pending is
   kept: a late ACK still releases. */
/* T-1046 leg-N fold 2: a person leaves the squad index on a bulk road (a player or the old peer gone, the area rule's adoption, the
   re-wake drop, teardown) - its uid row and its hand go together, and the keep question's cached answers are asked again (g_idxSeq) */
static void IdxDropUid(unsigned int uid) { g_idxOf.erase(uid); g_givenHandOf.erase(uid); g_keptHandOf.erase(uid); ++g_idxSeq; }   /* T-1318 a5 fold 1: its kept hand too */
static void IdxDropAll() { g_squadIdx.clear(); g_idxOf.clear(); g_givenHandOf.clear(); g_keptHandOf.clear(); g_otherHandsOf.clear(); ++g_idxSeq; g_keptStayAp.clear(); }   /* T-1318 attempt 5: the kept and other games' hands go too */
/* T-1318 attempt 5 (D1-R3): another game's living copy of squad k, its hand and its uid - noted (the taken test reads its owner then), only for
   a squad with people given away (a5 fold 1: no notes for other squads, so the cap is never filled by them; the cap logged once) */
static void OtherHandNote(const HoKey& k, const std::string& hand, unsigned int uid)
{
    if (hand.empty() || k.second.empty() || uid == 0) return;
    const std::map<HoKey, SquadIdx>::const_iterator si = g_squadIdx.find(k);
    if (si == g_squadIdx.end() || si->second.given.empty()) return;
    std::map<HoKey, std::map<std::string, unsigned int> >::iterator o = g_otherHandsOf.find(k);
    if (o == g_otherHandsOf.end())
    {
        if (g_otherHandsOf.size() >= kOtherHandsSquadCap)
        {
            if (g_wwOtherHandsCapHit++ == 0) DebugLog("[RELEASE] another game's copies' hands: the " + N((long long)kOtherHandsSquadCap) + "-squad cap is reached - squad id='" + k.second + "' and later ones are not noted (T-1318 a5 fold 1; worldWhole8 otherHandsCapHit, logged once)");
            return;
        }
        o = g_otherHandsOf.insert(std::make_pair(k, std::map<std::string, unsigned int>())).first;
    }
    std::map<std::string, unsigned int>::iterator h = o->second.find(hand);
    if (h != o->second.end()) { if (h->second != uid) { h->second = uid; ++g_idxSeq; } return; }
    if (o->second.size() >= kOtherHandsPerSquadCap) { ++g_wwOtherHandsCapHit; return; }
    o->second[hand] = uid; ++g_wwOtherHandsNoted; ++g_idxSeq;   /* the sleeping squads' taken answers are asked again */
}
/* T-1318 a5 fold 1 (review MEDIUM-2, LOW): a squad's index entry goes - its kept / asleep people's hands, its other-game notes and its
   kept-stay note go with it */
static void IdxEntryGone(const HoKey& k, const SquadIdx& e)
{
    for (std::set<unsigned int>::const_iterator u = e.keptHere.begin(); u != e.keptHere.end(); ++u) g_keptHandOf.erase(*u);
    for (std::set<unsigned int>::const_iterator u = e.asleepHere.begin(); u != e.asleepHere.end(); ++u) g_keptHandOf.erase(*u);
    if (g_otherHandsOf.erase(k) != 0) ++g_idxSeq;
    g_keptStayAp.erase(k);
}
/* T-1318 a5 fold 1 (review MEDIUM-2): uid taken back - its other-game notes go (the taken test also skips a uid this game runs now) */
static void OtherHandsForgetUid(unsigned int uid)
{
    for (std::map<HoKey, std::map<std::string, unsigned int> >::iterator o = g_otherHandsOf.begin(); o != g_otherHandsOf.end(); ++o)
        for (std::map<std::string, unsigned int>::iterator h = o->second.begin(); h != o->second.end(); )
        {
            if (h->second == uid) { o->second.erase(h++); ++g_idxSeq; } else ++h;
        }
}
/* T-1046 attempt 2 (design a'): THE ENGINE DISCARDED squad k here (store.cpp destroy hook, kDelDiscard road, via HandedOverUnmark why 2) -
   its kept part goes with it (the index's keptHere and asleepHere, the not-yet-indexed kept list, their hands), so a later squad under the
   same id does not inherit "kept" (HandoffSquadKeptPart) and is offered again. People given to another game stay given (that game runs
   them); the entry goes when nobody is given. Under a live walk the key waits for the next drain. Counted idxDiscard[dropped,deferred]. */
static std::set<HoKey> g_idxDiscardReq;
static long long g_idxDiscardDropped = 0, g_idxDiscardDeferred = 0;
static void IdxDiscardDrop(const HoKey& k)
{
    long long kept = 0, asleep = 0, recent = 0, given = 0;
    std::map<HoKey, KeptRecent>::iterator kr = g_keptRecent.find(k);
    if (kr != g_keptRecent.end())
    {
        recent = (long long)kr->second.uids.size();
        for (std::set<unsigned int>::const_iterator u = kr->second.uids.begin(); u != kr->second.uids.end(); ++u) g_keptHandOf.erase(*u);
        g_keptRecent.erase(kr); ++g_idxSeq;
    }
    std::map<HoKey, SquadIdx>::iterator s = g_squadIdx.find(k);
    if (s != g_squadIdx.end())
    {
        kept = (long long)s->second.keptHere.size(); asleep = (long long)s->second.asleepHere.size(); given = (long long)s->second.given.size();
        for (std::set<unsigned int>::const_iterator a = s->second.asleepHere.begin(); a != s->second.asleepHere.end(); ++a) IdxDropUid(*a);
        if (s->second.given.empty()) { IdxEntryGone(s->first, s->second); g_squadIdx.erase(s); }
        else
        {
            for (std::set<unsigned int>::const_iterator u = s->second.keptHere.begin(); u != s->second.keptHere.end(); ++u) g_keptHandOf.erase(*u);
            s->second.keptHere.clear(); s->second.asleepHere.clear(); ++g_idxSeq;
        }
    }
    if (kept + asleep + recent == 0) return;
    ++g_idxDiscardDropped;
    if (g_idxDiscardDropped <= 40)
        DebugLog("[RELEASE] squad id='" + k.second + "' - the engine discarded it here: its kept part leaves the squad index (kept " + N(kept) + ", asleep here " + N(asleep) + ", not yet indexed " + N(recent)
                 + "; given " + N(given) + " stay) - a later squad under this id starts with nothing kept (T-1046 attempt 2; idxDiscard dropped " + N(g_idxDiscardDropped) + ", deferred " + N(g_idxDiscardDeferred) + "; the first 40)");
}
/* one discarded squad key: dropped now, or under a live walk at the next drain (the key waits in g_idxDiscardReq) */
static void IdxDiscardKey(const HoKey& k)
{
    if (g_relWalkDepth > 0) { if (g_idxDiscardReq.size() < 4096) g_idxDiscardReq.insert(k); ++g_idxDiscardDeferred; return; }
    IdxDiscardDrop(k);
}
static void ReleaseForgetDrain()
{
    if (g_relWalkDepth > 0)
    {   /* [a1b2f1-hd3] [review F1]: a nested call under a live walk / reference - never cleared under it; the outermost entry's next drain
           (HandoffTick's, every frame) honours the request */
        if (g_relForgetReq != 0 || g_idxPeerGoneReq != 0 || !g_relRevokeGoneSlots.empty() || !g_idxGoneSlots.empty() || !g_idxDiscardReq.empty()) ++g_relDrainDeferred;   /* [a1b2f2-hd8] */
        return;
    }
    if (!g_idxDiscardReq.empty())
    {   /* T-1046 attempt 2: discards that came under a live walk */
        std::set<HoKey> req; req.swap(g_idxDiscardReq);
        for (std::set<HoKey>::const_iterator k = req.begin(); k != req.end(); ++k) IdxDiscardDrop(*k);
    }
    if (::InterlockedCompareExchange(&g_idxPeerGoneReq, 0, 1) == 1)
    {
        const long long n = (long long)g_squadIdx.size();
        IdxDropAll(); g_keptRecent.clear(); g_keyCache.clear(); g_idxGoneSlots.clear(); ++g_idxPeerGoneCleared;   /* [a1b2f1-hd3] */
        DebugLog("[RELEASE] the old link's peer is gone (its slot never known): squad index cleared (" + N(n) + " squads) - this game runs its own copies of those squads again");
    }
    if (!g_idxGoneSlots.empty())
    {   /* a player left: the people this game handed to IT leave the index; a squad with nobody given any more leaves it whole (its asleep and
           kept people too) - this game runs its own copies of that squad again. People given to every other player stay given. */
        std::vector<int> gone; gone.swap(g_idxGoneSlots);
        long long squads = 0, people = 0;
        for (std::map<HoKey, SquadIdx>::iterator s = g_squadIdx.begin(); s != g_squadIdx.end(); )
        {
            int dropped = 0;
            for (std::map<unsigned int, unsigned int>::iterator g = s->second.given.begin(); g != s->second.given.end(); )
            {
                const int toSlot = net::PlayerSlotOfKey(g->second);
                int takes = 0;
                for (size_t gi = 0; gi < gone.size() && takes == 0; ++gi) if (cooppg::PlayerGoneTakesRow(toSlot, gone[gi])) takes = 1;
                if (takes == 0) { ++g; continue; }
                IdxDropUid(g->first);
                s->second.given.erase(g++);
                ++dropped; ++people;
            }
            if (dropped == 0 || !s->second.given.empty()) { ++s; continue; }
            for (std::set<unsigned int>::const_iterator a = s->second.asleepHere.begin(); a != s->second.asleepHere.end(); ++a) IdxDropUid(*a);
            IdxEntryGone(s->first, s->second);   /* T-1318 a5 fold 1 */
            g_squadIdx.erase(s++);
            ++squads;
        }
        g_idxPlayerGoneSquads += squads; g_idxPlayerGonePeople += people;
        if (people > 0) DebugLog("[RELEASE] a player gone: " + N(people) + " people handed to it left the squad index, " + N(squads) + " squads left it whole - this game runs its own copies of those squads again");
    }
    if (!g_relRevokeGoneSlots.empty())
    {   /* [a1b2f2-hd4] [review G2]: a player gone - the REVOKEs waiting FROM it and owed TO it name its copies; a later session of it knows none */
        std::vector<int> gone; gone.swap(g_relRevokeGoneSlots);
        long long nw = 0, no = 0;
        for (size_t gi = 0; gi < gone.size(); ++gi)
        {
            for (size_t i = 0; i < g_relRevokeWait.size(); ) { if (g_relRevokeWait[i].from == gone[gi]) { g_relRevokeWait.erase(g_relRevokeWait.begin() + (long)i); ++nw; } else ++i; }
            for (size_t i = 0; i < g_relRevokeOwed.size(); ) { if (g_relRevokeOwed[i].slot == gone[gi]) { g_relRevokeOwed.erase(g_relRevokeOwed.begin() + (long)i); ++no; } else ++i; }
        }
        long long nr = 0;
        for (size_t gi = 0; gi < gone.size(); ++gi)
            for (size_t i = 0; i < g_relRewakeWait.size(); ++i)
            {   /* T-1268 fold 2: a waiting member's adopters that left go; with none left it is decided at the next tick (RelRewakeWaitTick) */
                RelRewakeWait& w = g_relRewakeWait[i];
                for (size_t j = 0; j < w.late.size(); ) { if (w.late[j].slot == gone[gi]) { w.late.erase(w.late.begin() + (long)j); ++nr; } else ++j; }
                const int k = cooplo::RewakeOnGone(w.kind, w.from, gone[gi]);
                if (k != w.kind) { w.kind = k; w.from = -1; ++nr; }
                if (cooplo::RewakeWaitNeeded(w.kind, (int)w.late.size()) == 0) w.seq = AreaMapApplySeqTS() - 1u;   /* due at the next tick */
            }
        g_relRewakeWaitPlayerGone += nr;
        if (nr > 0) DebugLog("[RELEASE] a player gone: " + N(nr) + " adopter row(s) of members waiting for their area's naming dropped (T-1268)");
        g_relRevokeWaitPlayerGone += nw; g_relRevokeOwedPlayerGone += no;
        if (nw + no > 0) DebugLog("[RELEASE] a player gone: REVOKEs dropped - waiting from it " + N(nw) + ", owed to it " + N(no));
    }
    if (::InterlockedCompareExchange(&g_relForgetReq, 0, 1) != 1) return;
    const long long r = (long long)g_release.size(), b = (long long)g_relBatch.size(), s = (long long)g_squadIdx.size();
    for (std::map<unsigned int, ReleaseRec>::const_iterator it = g_release.begin(); it != g_release.end(); ++it)
        DebugLog("[RELEASE] id=" + N((long long)it->first) + " END cleared by the world teardown (" + N((long long)it->second.mem.size()) + " member(s) open)");
    g_relRewakeWaitWorldCleared += (long long)g_relRewakeWait.size(); g_relRewakeWait.clear();   /* T-1268 fold 1: they name uids of the world being freed */
    if (!g_relHeldSq.empty()) DebugLog("[RELEASE] the world teardown cleared " + N((long long)g_relHeldSq.size()) + " squad(s) held until the squad sleeps (T-1318 attempt 3c)");
    g_relHeldSq.clear();   /* T-1318 attempt 3c: no hold outlives its world */
    g_deadHandsOf.clear(); g_wokeWithGiven.clear(); g_otherHandsOf.clear(); g_keptHandOf.clear(); g_keptStayAp.clear();   /* T-1318 a4 fold 1: they name squads of the world being freed; attempt 5: and these */
    g_release.clear(); g_relBatch.clear(); g_relLate.clear(); IdxDropAll(); g_keptRecent.clear(); g_keyCache.clear(); g_keepNoted.clear(); g_niwNoted.clear(); g_forcedTry.clear();   /* [a1b2f1-hd3]; T-650 fold 1: the forced hand-over's notes and back-offs */
    g_sameIdWd.clear();   /* the same-id retire's marks name uids of the world being freed */
    /* [a1b2f2-hd3] [review G2]: the REVOKEs waiting to be applied and owed to be sent name uids of the world being freed - never applied in the next */
    const long long rw = (long long)g_relRevokeWait.size(), ro = (long long)g_relRevokeOwed.size();
    g_relRevokeWait.clear(); g_relRevokeOwed.clear(); g_relRevokeWaitWorldCleared += rw; g_relRevokeOwedWorldCleared += ro;
    if (r + b + s + rw + ro == 0) return;
    ++g_relWorldCleared;
    DebugLog("[RELEASE] world teardown: open releases cleared=" + N(r) + " batches=" + N(b) + " indexedSquads=" + N(s) + " revokesWaiting=" + N(rw) + " revokesOwed=" + N(ro)
             + " (g_pending kept: a late ACK still releases)");
}
/* M7a3f3 T-425 [m7a3f3-hc7]: the squad key of a character - the verified squad reader's Platoon* (never getSquad), the engine's id of it
   (store.cpp's guarded read) and the faction (a guarded read). No __try here (std::string locals): the reads that can fault are in PODs. */
static void* HoFactionPod(::Character* c)
{
    __try { return (void*)c->getOwnerFactionDirect(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool SquadKeyOf(::Character* c, void** faction, std::string* id)
{
    *faction = 0; id->clear();
    if (!Plaus(c)) return false;
    SquadView sv;
    if (ReadSquadOf(c, &sv) != 1 || !Plaus(sv.platoon)) return false;
    void* fc = HoFactionPod(c);
    if (!Plaus(fc)) return false;
    std::string s = StoreWorldIdOf(sv.platoon);
    if (s.empty()) return false;
    *faction = fc; id->swap(s);
    return true;
}
/* M7a A1 build 2 [a1b2-hd1] (design 2.3) - THE SQUAD INDEX. A person enters `given` when it leaves this game alive (an XFER ACK naming it,
   an accepted RELEASE adoption), keyed by the squad key read while its body was whole; `asleepHere` when its release settles asleep. It
   leaves at a take back (net::TakeLocalOwner), at its recorded owner's DESPAWN, NOT-LIVE answer or an UNLOAD saying it runs the person nowhere (an announce-pass UNLOAD keeps it) [review F9], at the sweep's
   adoption of the squad, the other player gone and teardown. The six kept consumers ask HandedOverHas / HasId / Any (A2 retires them). */
static void IdxErase(unsigned int uid)
{
    std::map<unsigned int, HoKey>::iterator o = g_idxOf.find(uid);
    if (o == g_idxOf.end()) return;
    ++g_idxSeq; g_givenHandOf.erase(uid); g_keptHandOf.erase(uid);   /* T-1318 attempt 5: its kept hand goes with it */
    std::map<HoKey, SquadIdx>::iterator s = g_squadIdx.find(o->second);
    if (s != g_squadIdx.end())
    {
        s->second.given.erase(uid); s->second.asleepHere.erase(uid);
        if (s->second.given.empty() && s->second.asleepHere.empty()) { IdxEntryGone(s->first, s->second); g_squadIdx.erase(s); }   /* T-1318 a5 fold 1 */
    }
    g_idxOf.erase(o);
}
static void IdxPut(unsigned int uid, const HoKey& k, int asleep, unsigned int owner)
{
    IdxErase(uid);
    ++g_idxSeq;
    g_givenHandOf.erase(uid);   /* T-1046 leg-N fold 2: no hand rides over onto the new entry (a given entry's caller sets its own after this) */
    SquadIdx& s = g_squadIdx[k];
    if (asleep != 0) { s.asleepHere.insert(uid); ++g_idxAsleepHere; } else { s.given[uid] = owner; ++g_idxGiven; g_keptHandOf.erase(uid); }
    g_idxOf[uid] = k;
    std::map<HoKey, KeptRecent>::iterator kr = g_keptRecent.find(k);   /* [a1b2f1-hd4] [review F5]: kept people of it noted before it was indexed */
    if (kr != g_keptRecent.end()) { s.keptHere.insert(kr->second.uids.begin(), kr->second.uids.end()); g_keptRecent.erase(kr); }
    s.keptHere.erase(uid);
}
/* [a1b2f1-hd5] [review F5]: uid, a person of this game put away and NOT offered (or its offer cancelled by a re-wake), stays in this game's
   world data only - kept here. The key: as read from the whole body (have 1), else the 1 Hz scan's cache; unknown -> counted (a leftover). */
static void IdxKeepKey(unsigned int uid, const HoKey& k, int have, const std::string& hand = std::string())   /* T-1318 attempt 5: hand - its hand when read */
{
    if (have == 0)
    {
        std::map<unsigned int, HoKey>::const_iterator ck = g_keyCache.find(uid);
        if (ck == g_keyCache.end()) { ++g_idxKeptUnknown; return; }
        const HoKey cached(ck->second);
        IdxKeepKey(uid, cached, 1, hand);
        return;
    }
    ++g_idxKept;
    KeptHandNote(uid, hand);   /* T-1318 attempt 5 (D1-R1): a re-wake erases it only when this hand is in the woken squad's list */
    std::map<HoKey, SquadIdx>::iterator s = g_squadIdx.find(k);
    if (s != g_squadIdx.end()) { s->second.keptHere.insert(uid); return; }
    KeptRecent& r = g_keptRecent[k]; r.uids.insert(uid); r.at = NowSec();
}
static void IdxKeep(unsigned int uid, ::Character* c)   /* c: a WHOLE body, or 0 (nothing may be read) */
{
    void* f = 0; std::string id, hand;
    { ObjId oid; if (c != 0 && Plaus(c) && CaptureObjId(c, &oid)) hand = cooplo::HandKey(oid.serial, oid.type); }   /* T-1318 attempt 5 (D1-R1) */
    if (c != 0 && Plaus(c) && SquadKeyOf(c, &f, &id)) { IdxKeepKey(uid, HoKey(f, id), 1, hand); return; }
    IdxKeepKey(uid, HoKey(), 0, hand);
}
/* T-1318 attempt 3 (owner 1615 a; F-T2324-WHOLEKEPT): the people of squad k (the index key: faction + squad id) this game keeps asleep
   un-offered - the index's keptHere and asleepHere, the not-yet-indexed kept list and (attempt 3c) the held-undecided put-aways (g_relHeldSq),
   each person once; people named in `except` (the ones
   the hand-over itself is about to send or offer) are not counted. The XFER's and the RELEASE's whole tests read it. MAIN THREAD. */
static int KeptAsleepOf(const HoKey& k, const std::set<unsigned int>& except)
{
    std::set<unsigned int> u;
    const std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.find(k);
    if (s != g_squadIdx.end()) { u.insert(s->second.keptHere.begin(), s->second.keptHere.end()); u.insert(s->second.asleepHere.begin(), s->second.asleepHere.end()); }
    const std::map<HoKey, KeptRecent>::const_iterator kr = g_keptRecent.find(k);
    if (kr != g_keptRecent.end()) u.insert(kr->second.uids.begin(), kr->second.uids.end());
    const std::map<HoKey, RelHeld>::const_iterator hd = g_relHeldSq.find(k);   /* T-1318 attempt 3c: held undecided - asleep here, this game's */
    if (hd != g_relHeldSq.end()) for (size_t m = 0; m < hd->second.b.mem.size(); ++m) u.insert(hd->second.b.mem[m].m.uid);
    int n = 0;
    for (std::set<unsigned int>::const_iterator i = u.begin(); i != u.end(); ++i) if (except.count(*i) == 0) ++n;
    return n;
}
/* T-1318 attempt 4 (owner 1626, design A): THE ENGINE'S OWN MEMBER LIST. The living members of a read list (sv) that are not people this game
   runs of the squad (inRun: a uid named in run; run 0 - any uid of this game's), one at a time through cooplo::SquadMemberUnaccounted (dead
   left out by the engine's flag, StoreIsDeadPod; unreadable counted alive); skip - a body to leave out (the one being put away). *known:
   members with any uid. Objects go to *objs when given. MAIN THREAD (reads under the guarded readers). */
/* T-1318 attempt 3 fold 2 (re-check LOW-2), attempt 4: THE ONE READ of a body's engine dead flag (Character::hasDied under SEH - 1 dead, 0
   alive, -1 unreadable) for every rule in this file: the awake rule (SquadBodyAwake), the engine list's living members (SquadUnaccountedIn,
   HandoffMembersTakenBy) */
static int BodyDeadFlag(const void* c) { return coop::StoreIsDeadPod((void*)c); }
/* T-1318 a4 fold 1 (review HIGH-2): a member with ANOTHER game's uid is run there already - never outside (cooplo::SquadMemberUnaccounted's
   otherGame); outside: living with no uid, or with this game's uid not in run. deadHands / otherHands (when given): the hands of the list's
   corpses / of another game's living people - accounted, never kept by the squad's sleeping records. */
static int SquadUnaccountedIn(const SquadView& sv, const std::set<unsigned int>* run, const void* skip, int* known, std::set<const void*>* objs, std::set<std::string>* deadHands = 0, std::set<std::string>* otherHands = 0, std::map<std::string, unsigned int>* otherUids = 0)   /* T-1318 attempt 5 (D1-R3): otherUids - another game's living people's hands -> their uid */
{
    int n = 0;
    if (known != 0) *known = 0;
    for (int i = 0; i < sv.stored && i < kSquadViewCap; ++i)
    {
        ::Character* m = sv.members[i];
        if (m == 0 || (const void*)m == skip) continue;
        const int plaus = Plaus(m) ? 1 : 0;
        const unsigned int uid = plaus != 0 ? FindSpawnedUid(m) : 0;
        if (uid != 0 && known != 0) ++*known;
        const int mine = (uid != 0 && net::IsUidMine(uid)) ? 1 : 0;
        const int inRun = (mine != 0 && (run == 0 || run->count(uid) != 0)) ? 1 : 0;
        const int otherGame = (uid != 0 && mine == 0) ? 1 : 0;
        const int dead = plaus != 0 ? BodyDeadFlag(m) : -1;
        std::set<std::string>* hs = dead == 1 ? deadHands : (otherGame != 0 ? otherHands : 0);
        const int slotNote = (otherUids != 0 && otherGame != 0 && dead != 1) ? 1 : 0;
        if ((hs != 0 || slotNote != 0) && plaus != 0)
        {
            ObjId oid;
            if (CaptureObjId(m, &oid))
            {
                const std::string hk = cooplo::HandKey(oid.serial, oid.type);
                if (hs != 0) hs->insert(hk);
                if (slotNote != 0) (*otherUids)[hk] = uid;   /* T-1318 attempt 5 (D1-R3): its owner is read at the taken test (a5 fold 1) */
            }
        }
        if (cooplo::SquadMemberUnaccounted(dead, inRun, otherGame) == 0) continue;
        ++n;
        if (objs != 0) objs->insert((const void*)m);
    }
    return n;
}
/* the awake engine list of squad k now (the faction's ACTIVE platoon of that id - never a context platoon: its people are another game's),
   read: 1, else 0 */
static int SquadAwakeListOf(const HoKey& k, SquadView* sv)
{
    if (!Plaus(k.first) || k.second.empty()) return 0;
    void* pl = FindAwakePlatoon((::Faction*)k.first, k.second);
    if (!Plaus(pl) || coop::IsContextPlatoon(pl) == 1) return 0;
    void* ap = 0;
    if (PlatoonApPod(pl, &ap) != 1 || !Plaus(ap)) return 0;
    return ReadSquadAt(ap, sv) == 1 ? 1 : 0;
}
/* the same for an awake squad this game runs (uids: its people here) - the squad key read from its first member whose key reads; no key read
   (a squad of one, or unread bodies): 0 - nothing can be indexed under a key that does not read */
/* T-1318 attempt 3b: this game's LIVE people of squad k - awake and run here (net::IsUidMine; this game's copies of another game's people are
   not), the batch being put away (except) left out; read now from the mirror (cooplo::ReleaseAwakeHolds). MAIN THREAD. */
/* T-1318 attempt 3 fold 2 (re-check LOW-2): THE AWAKE RULE for one plausible body of this game's mirror - 1 = a live person of this game's
   of the squad in question (same), not in the batch (inBatch 0). One helper for the hold (AwakeOfSquadHere) and the tripwire's drain
   (PartGivenDrain), so both read a corpse alike. MAIN THREAD. */
static int SquadBodyAwake(unsigned int uid, ::Character* c, int mine, int inBatch, int same)
{
    /* T-1318 attempt 3 fold 1 (review MED-2b): a corpse is not awake - the engine's own dead flag (Character::hasDied under SEH,
       StoreIsDeadPod; the mirror's "dead" means a retired pointer, not a dead person). A world faction's corpses stay in its squad's
       member list (store.cpp: declareDead removes them only for the player's faction), so a body of the squad left by a fight would
       hold the rest. Unreadable (-1) counts as awake: the hold is the safe side. */
    const int dead = same != 0 ? BodyDeadFlag(c) : 0;
    return cooplo::ReleaseAwakeHolds(uid, mine, inBatch, same, dead);
}
static int AwakeOfSquadHere(const HoKey& k, const std::set<unsigned int>& except)
{
    int n = 0;
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int uid = 0; ::Character* c = 0;
        if (!MirrorSlot(i, &uid, &c) || !Plaus(c)) continue;
        const int mine = net::IsUidMine(uid) ? 1 : 0;
        if (mine == 0 || except.count(uid) != 0) continue;   /* the cheap tests first; the rule itself is ReleaseAwakeHolds */
        void* f = 0; std::string id;
        const int same = (SquadKeyOf(c, &f, &id) && HoKey(f, id) == k) ? 1 : 0;
        n += SquadBodyAwake(uid, c, mine, 0, same);   /* fold 2 (LOW-2): the one awake rule - a corpse is not awake */
    }
    return n;
}
static int KeptAsleepOfRun(const std::vector<unsigned int>& uids)
{
    const std::set<unsigned int> run(uids.begin(), uids.end());
    for (size_t i = 0; i < uids.size(); ++i)
    {
        void* f = 0; std::string id;
        if (SquadKeyOf(FindSpawned(uids[i]), &f, &id)) return KeptAsleepOf(HoKey(f, id), run);
    }
    return 0;
}
/* T-1318 attempt 2 (D3) THE TRIPWIRE, fold 1 (review MEDIUM-1): after an accepted hand-over's index write (RelAccept, the XFER ACK's
   IdxGiveAtAck), the settle of an offer that had an accepted ACK (RelSettleAll), or a wholly given squad's not-offered put-away that kept
   people of this game's own (ReleaseFlush) - the squad is noted (PartGivenNote) and judged at the frame's drain (PartGivenDrain, the top of
   HandoffTick: after the whole ACK is applied, so its own members are no longer this game's). Given in part = people of it given AND people
   asleep or kept here, or AND people of it still awake here (this game runs them live - read at the drain, cooplo::PartGivenAt). Counted
   once per squad (partGiven - the last field of worldWhole[..]; those with people awake here also partGivenAwake in worldWhole2[..]) with
   one line, the first 20. Whole or none leaves no road to it; where part-giving could still arise (each Inferred, rare) and where it shows:
   - a whole refusal on the receiver that still names rows it ALREADY runs (RELEASE nDup / XFER alreadyHere - a dual run or a lost ACK's
     resend): the ACK gives those rows, the rest stay asleep / kept (RELEASE) or awake (XFER) here -> partGiven (awake: partGivenAwake);
   - a member that stopped being this game's while its offer was open (RelAdvance's not-mine erase) -> partGiven at the settle;
   - a member in flight on an XFER at its squad's put-away (ReleaseOnPutAway's kPadLeaveToAck: neither offered nor kept) -> partGiven at
     that XFER's ACK (the put-away people asleep here);
   - a re-woken body accepted at a RELEASE ACK (rewokeAcceptAtAck: its duplicate goes to the orphan clean-up) -> partGivenAwake while it lives;
   - a wholly given squad's put-away person whose hand is not a given hand (ownKeptAsleep / ownKeptHandUnread) -> partGiven;
   - a kept person whose squad key was NOT known (IdxKeepKey without a key): never indexed under any squad, so no squad can show it here -
     it shows ONLY as worldWhole2 keptUnknown (the same count as indexFold1 keptUnknown). */
static std::set<HoKey> g_wwPartGivenNoted;
static std::map<HoKey, std::string> g_wwPartGivenDue;   /* squads to judge at the next drain -> the road that noted it */
static void PartGivenNote(const HoKey& k, const char* where)
{
    if (g_wwPartGivenNoted.count(k) != 0) return;   /* already counted */
    if (g_wwPartGivenDue.size() < 4096) g_wwPartGivenDue.insert(std::make_pair(k, std::string(where)));
}
static void PartGivenDrain()
{
    if (g_wwPartGivenDue.empty()) return;
    std::map<HoKey, std::string> due; due.swap(g_wwPartGivenDue);
    std::map<HoKey, int> awake;   /* this game's live people of each due squad, read now */
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int uid = 0; ::Character* c = 0;
        if (!MirrorSlot(i, &uid, &c) || !net::IsUidMine(uid) || !Plaus(c)) continue;
        void* f = 0; std::string id;
        if (!SquadKeyOf(c, &f, &id)) continue;
        const HoKey k(f, id);
        if (due.count(k) != 0) awake[k] += SquadBodyAwake(uid, c, 1, 0, 1);   /* T-1318 attempt 3 fold 2 (re-check LOW-2): a corpse is not awake here (the hold's own rule) */
    }
    for (std::map<HoKey, std::string>::const_iterator d = due.begin(); d != due.end(); ++d)
    {
        std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.find(d->first);
        if (s == g_squadIdx.end()) continue;
        const std::map<HoKey, int>::const_iterator a = awake.find(d->first);
        const int awakeHere = a != awake.end() ? a->second : 0;
        const int pg = cooplo::PartGivenAt((int)s->second.given.size(), (int)s->second.asleepHere.size(), (int)s->second.keptHere.size(), awakeHere);
        if (pg == cooplo::kPgNone) continue;
        if (g_wwPartGivenNoted.size() >= 4096 || !g_wwPartGivenNoted.insert(d->first).second) continue;
        ++g_wwPartGiven; if ((pg & cooplo::kPgAwake) != 0) ++g_wwPartGivenAwake;
        if (g_wwPartGiven > 20) continue;
        DebugLog(std::string("[RELEASE] PART-GIVEN squad id='") + d->first.second + "' at " + d->second + ": given " + N((long long)s->second.given.size()) + ", asleep here " + N((long long)s->second.asleepHere.size())
                 + ", kept here " + N((long long)s->second.keptHere.size()) + ", awake here " + N((long long)awakeHere) + " - whole or none was not kept (T-1318 D3 tripwire; partGiven=" + N(g_wwPartGiven)
                 + " partGivenAwake=" + N(g_wwPartGivenAwake) + "; the first 20)");
    }
}
/* the XFER ACK's release: the key from the body when it is still here, else as read when the XFER was built (Pending::keyOf) */
/* T-850 (T2230): the squad id of a person an ACK names - read from the body (SquadKeyOf), else as read at the send (p.keyOf); "" unknown */
static std::string HoSquadIdOf(unsigned int uid, ::Character* c, const Pending& p)
{
    void* f = 0; std::string id;
    if (SquadKeyOf(c, &f, &id)) return id;
    const std::map<unsigned int, HoKey>::const_iterator k = p.keyOf.find(uid);
    return k != p.keyOf.end() ? k->second.second : std::string();
}
/* T-1046 leg-N fold 2: the hand the ACK's given entry carries - from the body when it is still whole, else as read at the send
   (Pending::handOf); neither read: no hand (the sleeping squad's test then reads that person as not given) */
static void IdxGiveHand(unsigned int uid, ::Character* rel, const Pending& p)
{
    std::string body;
    if (Plaus(rel)) { ObjId oid; if (CaptureObjId(rel, &oid)) body = cooplo::HandKey(oid.serial, oid.type); }
    const std::map<unsigned int, std::string>::const_iterator h = p.handOf.find(uid);
    const std::string hand = cooplo::AckHand(body, h != p.handOf.end() ? h->second : std::string());
    if (!hand.empty() && g_givenHandOf.size() < 65536) g_givenHandOf[uid] = hand;
}
static void IdxGiveAtAck(unsigned int uid, ::Character* rel, const Pending& p, unsigned int to)
{
    void* f = 0; std::string id;
    if (Plaus(rel) && SquadKeyOf(rel, &f, &id)) { IdxPut(uid, HoKey(f, id), 0, to); IdxGiveHand(uid, rel, p); PartGivenNote(HoKey(f, id), "an XFER ACK"); return; }   /* T-1318 D3 tripwire */
    std::map<unsigned int, HoKey>::const_iterator k = p.keyOf.find(uid);
    if (k != p.keyOf.end()) { IdxPut(uid, k->second, 0, to); IdxGiveHand(uid, rel, p); PartGivenNote(k->second, "an XFER ACK"); return; }
    ++g_idxNoKey;
}
/* an XFER's members' squad keys and hands, read at the send while the bodies are whole (the ACK may come after the engine put them away) */
static void PendingKeysRead(Pending* p)
{
    for (size_t k = 0; k < p->members.size(); ++k)
    {
        void* f = 0; std::string id;
        ::Character* c = FindSpawned(p->members[k].uid);
        if (SquadKeyOf(c, &f, &id)) p->keyOf[p->members[k].uid] = HoKey(f, id);
        if (Plaus(c)) { ObjId oid; if (CaptureObjId(c, &oid)) p->handOf[p->members[k].uid] = cooplo::HandKey(oid.serial, oid.type); }   /* T-1046 leg-N fold 2 */
    }
}
/* the XFER in flight (other than exceptKey) naming uid, else 0 */
static unsigned int FlightHolding(unsigned int uid, unsigned int exceptKey)
{
    for (std::map<unsigned int, Pending>::const_iterator it = g_pending.begin(); it != g_pending.end(); ++it)
    {
        if (it->first == exceptKey) continue;
        for (size_t k = 0; k < it->second.members.size(); ++k) if (it->second.members[k].uid == uid) return it->first;
    }
    return 0;
}
/* [a1b2f1-hd5] [review F5]: people of squad k still in an open offer or this frame's batch (they are this game's until adopted) */
static int IdxOpenHere(const HoKey& k)
{
    int n = 0;
    for (std::map<void*, RelBatch>::const_iterator b = g_relBatch.begin(); b != g_relBatch.end(); ++b)
        for (size_t m = 0; m < b->second.mem.size(); ++m) if (b->second.mem[m].hasKey != 0 && b->second.mem[m].key == k) ++n;
    for (std::map<unsigned int, ReleaseRec>::const_iterator r = g_release.begin(); r != g_release.end(); ++r)
        for (size_t m = 0; m < r->second.mem.size(); ++m) if (r->second.mem[m].hasKey != 0 && r->second.mem[m].key == k) ++n;
    const std::map<HoKey, RelHeld>::const_iterator hd = g_relHeldSq.find(k);   /* T-1318 attempt 3c: held undecided - still this game's */
    if (hd != g_relHeldSq.end()) n += (int)hd->second.b.mem.size();
    return n;
}
/* T-1318 attempt 3 fold 2 (re-check MED-A): people of squad k in a hand-over of this game still OPEN - an offer record waiting for its
   answer (g_release; a member leaves it at its ACK's accept or its settle) or an XFER waiting for its ACK (g_pending, each member's key read
   at the send) - HandoffSquadOpen's two open lists, counted per person by the exact key. While it is non-zero a new put-away of the squad is
   HELD (RelHeldDecide), never a second offer. MAIN THREAD. */
static int RelOpenOfSquad(const HoKey& k)
{
    int n = 0;
    for (std::map<unsigned int, ReleaseRec>::const_iterator r = g_release.begin(); r != g_release.end(); ++r)
        for (size_t m = 0; m < r->second.mem.size(); ++m) if (r->second.mem[m].hasKey != 0 && r->second.mem[m].key == k) ++n;
    for (std::map<unsigned int, Pending>::const_iterator p = g_pending.begin(); p != g_pending.end(); ++p)
        for (std::map<unsigned int, HoKey>::const_iterator q = p->second.keyOf.begin(); q != p->second.keyOf.end(); ++q) if (q->second == k) ++n;
    return n;
}
static int IdxWholeGiven(const HoKey& k, const SquadIdx& s)
{
    return cooplo::SquadWholeGiven((int)s.given.size(), (int)s.asleepHere.size(), (int)s.keptHere.size(), IdxOpenHere(k), s.liveHere);
}
bool HandedOverHas(void* faction, const std::string& id)
{
    ReleaseForgetDrain();
    if (g_squadIdx.empty()) return false;
    std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.find(HoKey(faction, id));
    return s != g_squadIdx.end() && IdxWholeGiven(s->first, s->second) != 0;   /* [a1b2f1-hd5] [review F5]: only a WHOLLY given squad */
}
bool HandedOverHasId(const std::string& id)
{
    ReleaseForgetDrain();
    for (std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.begin(); s != g_squadIdx.end(); ++s)
        if (s->first.second == id && IdxWholeGiven(s->first, s->second) != 0) return true;   /* [a1b2f1-hd5] [review F5] */
    return false;
}
/* the one slot every given person of a wholly given squad went to (the squad's runner now); -1 = not wholly given, or given to several
   games. faction 0 = any faction, by id. */
int HandedOverSlot(void* faction, const std::string& id)
{
    ReleaseForgetDrain();
    for (std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.begin(); s != g_squadIdx.end(); ++s)
    {
        if (s->first.second != id || (faction != 0 && s->first.first != faction) || IdxWholeGiven(s->first, s->second) == 0) continue;
        int slot = -1;
        for (std::map<unsigned int, unsigned int>::const_iterator g = s->second.given.begin(); g != s->second.given.end(); ++g)
        {
            const int to = net::PlayerSlotOfKey(g->second);
            if (to < 0 || (slot >= 0 && to != slot)) return -1;
            slot = to;
        }
        return slot;
    }
    return -1;
}
bool HandedOverAny()
{
    ReleaseForgetDrain();
    for (std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.begin(); s != g_squadIdx.end(); ++s) if (!s->second.given.empty()) return true;
    return false;
}
/* T-1318 fold 2 (re-check B1): THE OPEN HAND-OVER - an offer of squad (faction, id) sent by this game and not yet answered: a person of
   it in this frame's put-away batch or an open RELEASE record (the asleep road - IdxOpenHere's own two lists; a record leaves g_release
   at its END, a member at its ACK's accept or its settle), or in an XFER still waiting for its ACK (the awake road - Pending::keyOf, each
   member's key read at the send; the flight leaves g_pending at its ACK or abandonment). faction 0 = any faction, by id. 1 open, 0 not.
   MAIN THREAD. */
int HandoffSquadOpen(void* faction, const std::string& id)
{
    ReleaseForgetDrain();
    if (id.empty()) return 0;
    for (std::map<void*, RelBatch>::const_iterator b = g_relBatch.begin(); b != g_relBatch.end(); ++b)
        for (size_t m = 0; m < b->second.mem.size(); ++m)
            if (b->second.mem[m].hasKey != 0 && b->second.mem[m].key.second == id && (faction == 0 || b->second.mem[m].key.first == faction)) return 1;
    for (std::map<unsigned int, ReleaseRec>::const_iterator r = g_release.begin(); r != g_release.end(); ++r)
        for (size_t m = 0; m < r->second.mem.size(); ++m)
            if (r->second.mem[m].hasKey != 0 && r->second.mem[m].key.second == id && (faction == 0 || r->second.mem[m].key.first == faction)) return 1;
    for (std::map<unsigned int, Pending>::const_iterator p = g_pending.begin(); p != g_pending.end(); ++p)
        for (std::map<unsigned int, HoKey>::const_iterator k = p->second.keyOf.begin(); k != p->second.keyOf.end(); ++k)
            if (k->second.second == id && (faction == 0 || k->second.first == faction)) return 1;
    return 0;
}
int HandoffOpenAny() { ReleaseForgetDrain(); return (!g_relBatch.empty() || !g_release.empty() || !g_pending.empty()) ? 1 : 0; }
long long HandoffOpenSeq() { return g_hoOpenSeq; }
/* T-1318 fold 2 (re-check B2): the one slot every given person of squad id (any faction) went to; -1 = nothing given, or several slots */
int HandoffGivenSlotId(const std::string& id)
{
    ReleaseForgetDrain();
    int slot = -1;
    for (std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.begin(); s != g_squadIdx.end(); ++s)
    {
        if (s->first.second != id) continue;
        for (std::map<unsigned int, unsigned int>::const_iterator g = s->second.given.begin(); g != s->second.given.end(); ++g)
        {
            const int to = net::PlayerSlotOfKey(g->second);
            if (to < 0 || (slot >= 0 && to != slot)) return -1;
            slot = to;
        }
    }
    return slot;
}
/* the sweep adopted the squad's rebuilt people under the area rule (a fresh map says no other game holds the area and no other game's
   copy of it is live here): the squad is this game's again - its index entry goes */
void HandedOverUnmark(void* faction, const std::string& id, int why)
{
    ReleaseForgetDrain();
    if (why == 2)
    {   /* T-1046 attempt 2: the engine discarded the squad here - its kept part goes (IdxDiscardDrop); under a live walk at the next drain */
        IdxDiscardKey(HoKey(faction, id));
        return;
    }
    std::map<HoKey, SquadIdx>::iterator s = g_squadIdx.find(HoKey(faction, id));
    if (s == g_squadIdx.end()) return;
    for (std::map<unsigned int, unsigned int>::const_iterator g = s->second.given.begin(); g != s->second.given.end(); ++g) IdxDropUid(g->first);
    for (std::set<unsigned int>::const_iterator a = s->second.asleepHere.begin(); a != s->second.asleepHere.end(); ++a) IdxDropUid(*a);
    IdxEntryGone(s->first, s->second);   /* T-1318 a5 fold 1 */
    g_squadIdx.erase(s); ++g_idxSquadCleared;
    DebugLog("[RELEASE] squad id='" + id + "' out of the squad index - " + (why == 0 ? std::string("taken back") : std::string("its rebuilt people are adopted here (the area rule)"))
             + " (index squadCleared=" + N(g_idxSquadCleared) + ")");
}
void SquadIdxTaken(unsigned int uid)
{
    OtherHandsForgetUid(uid);   /* T-1318 a5 fold 1 (review MEDIUM-2): this game runs it again */
    if (g_idxOf.find(uid) == g_idxOf.end()) return;
    IdxErase(uid); ++g_idxTakenBack;
}
void SquadIdxOwnerWithdrew(unsigned int uid, int unload)
{
    std::map<unsigned int, HoKey>::iterator o = g_idxOf.find(uid);
    if (o == g_idxOf.end()) return;
    std::map<HoKey, SquadIdx>::iterator s = g_squadIdx.find(o->second);
    if (s == g_squadIdx.end() || s->second.given.count(uid) == 0) return;
    IdxErase(uid);
    if (unload != 0) ++g_idxClearedByOwnerUnload; else ++g_idxClearedByOwnerDespawn;
}
/* an UNLOAD from the recorded owner that still runs the person (its announce pass: this game's player left the area) - `given` stays, so
   this game's wake of the squad is still refused and its people are never re-woken here from this game's own world data; counted */
void SquadIdxOwnerStillRuns(unsigned int uid)
{
    std::map<unsigned int, HoKey>::const_iterator o = g_idxOf.find(uid);
    if (o == g_idxOf.end()) return;
    std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.find(o->second);
    if (s != g_squadIdx.end() && s->second.given.count(uid) != 0) ++g_idxKeptOnAnnounce;
}
/* M7a2 fold 3 [m7a2h-hp0] (T807 GAP 1): the per-second pass's own XFER (forced, or follow-leader) - a member another XFER in flight names
   (other than `key`, this XFER's own) is left out: never two flights for one person (the second ACK's release finds no member note -
   noKey). Flights only (re-check 2). Counted (passHeld) and logged (the first 20) once per (person, holding flight) - not every second
   the hold lasts (re-check 3); the seen pairs of a flight that has settled are pruned, so a later hold behind a new flight counts again. */
static void PassHoldElsewhere(unsigned int key, std::vector<net::XferMember>* mem, const char* where)
{
    for (std::set<std::pair<unsigned int, unsigned int> >::iterator e = g_passHeldSeen.begin(); e != g_passHeldSeen.end(); )
    {
        if (g_pending.find(e->second) == g_pending.end()) g_passHeldSeen.erase(e++); else ++e;
    }
    long long heldNew = 0; unsigned int firstUid = 0, firstHolder = 0;
    for (size_t k = 0; k < mem->size(); )
    {
        const unsigned int u = (*mem)[k].uid;
        const unsigned int fl = FlightHolding(u, key);
        if (coopsquad::HandoverAddAction(1, 0, fl != 0 ? 1 : 0) != coopsquad::kHoAddHold) { ++k; continue; }
        if (g_passHeldSeen.insert(std::make_pair(u, fl)).second) { ++heldNew; if (firstUid == 0) { firstUid = u; firstHolder = fl; } }
        mem->erase(mem->begin() + k);
    }
    if (heldNew == 0) return;
    g_passHeld += heldNew;
    if (mem->empty()) ++g_passHeldEmpty;
    if (g_passHeldLogged >= 20) return;
    ++g_passHeldLogged;
    DebugLog("[XFER] " + std::string(where) + " XFER leader=" + N(key) + ": " + N(heldNew) + " member(s) newly held back (first uid=" + N(firstUid)
             + ", named by the XFER in flight leader=" + N(firstHolder) + ") - one person, one hand-over;"
             + (mem->empty() ? std::string(" every member held - this XFER is not sent") : " sent with " + N((long long)mem->size()) + " member(s)")
             + " (handoverOnce passHeld=" + N(g_passHeld) + " passHeldEmpty=" + N(g_passHeldEmpty) + ", once per person per holding flight; the first 20 logged, then counted)");
}
/* ==== M7a A1 build 2 [a1b2-hd2] (design 2.4, 2.5) - THE RECEIVERS AND THE RELEASE. The giver no longer predicts another game's engine: an
   engine put-away of this game's own people is offered (RELEASE) to the IN_WORLD games the world server's area map shows with the squad's
   area LOADED, one at a time - the area's holder first when it is one of them, then the lower slot (T-650 fold 1, owner decision 580); the
   first that adopts at the CURRENT offer's gen runs them; nobody -> they sleep in this game's world data (their UNLOAD goes then). A late
   adopter is revoked (it becomes the winner's puppet). MAIN THREAD, all of it. ==== */
/* the receivers of sector s for this game's next offer, in offer order (cooplo::AreaReceiverOrder over ONE locked read of the area map,
   AreaSquadViewTS - review L1). k >= 1 = order holds k slots; 0 = no other in-world game has the area loaded; -1 = no road to decide on
   (this game's notebook link down, the area map not fresh, or this game has no slot yet: no hand-over without the world server).
   *holderOut = the holder the map names (-1 none); *notInWorldOut = games with the area loaded skipped as not IN_WORLD. */
/* T-650 fold 2 (T1056): (px, pz) = the squad's decision position; a game is a receiver only when its engine would KEEP the squad there -
   cooplo::EngineKeepsAt over its map bits for the area and the area's four side neighbours, from the same locked read. *notKeepOut = games
   with the area loaded passed over because they would not (the squad within kEngineEdgeStrip + kKeepMarginUnits of a side they have not
   loaded: their engine would put it away 4 s after the take). */
/* asSlot >= 0: the order asked in that slot's place (a final leaver's - HandoffGoneFirstReceiver): that slot is left out and this game
   is one of the candidates, counted in world through this link's roster like every other game. */
static int ReceiversFor(const Sector& s, float px, float pz, const std::vector<int>& tried, std::vector<int>* order, int* holderOut, int* notInWorldOut, int* notKeepOut, int asSlot = -1)
{
    order->clear();
    if (holderOut != 0) *holderOut = -1;
    if (notInWorldOut != 0) *notInWorldOut = 0;
    if (notKeepOut != 0) *notKeepOut = 0;
    if (!StoreLiveReady() || s.x < 0 || s.y < 0) return -1;
    if (asSlot >= 0 && StoreRosterSlotInWorld(StoreMySlot()) < 0) return -1;   /* the leaver's order counts this game through this link's roster: none here yet is a hold */
    std::vector<int> sl(256), bit(256), side(256 * 4);
    int holder = -1, n = 0;
    const int fresh = AreaSquadViewTS(s, &holder, &sl[0], &bit[0], (int)sl.size(), &n, &side[0]);
    if (holderOut != 0) *holderOut = holder;
    std::vector<cooplo::AreaRow> rows;
    for (int i = 0; i < n; ++i)
    {
        cooplo::AreaRow r; r.slot = sl[(size_t)i]; r.loaded = bit[(size_t)i]; r.inWorld = StoreRosterSlotInWorld(sl[(size_t)i]) == 1 ? 1 : 0;
        const size_t q = (size_t)i * 4;
        r.keeps = cooplo::EngineKeepsAt(px, pz, cooplo::AreaMinUnits(s.x), cooplo::AreaMinUnits(s.y), cooplo::kAreaSizeUnits, r.loaded,
                                        side[q], side[q + 1], side[q + 2], side[q + 3], cooplo::kKeepMarginUnits);
        rows.push_back(r);
    }
    std::vector<int> ord(rows.size() + 1);
    int niw = 0, nkp = 0;
    const int k = cooplo::AreaReceiverOrder(rows.empty() ? 0 : &rows[0], (int)rows.size(), fresh == 1 ? 1 : 0, holder, asSlot >= 0 ? asSlot : StoreMySlot(),
                                            tried.empty() ? 0 : &tried[0], (int)tried.size(), &ord[0], (int)ord.size(), &niw, &nkp);
    if (notInWorldOut != 0) *notInWorldOut = niw;
    if (notKeepOut != 0) *notKeepOut = nkp;
    if (k < 0) return -1;
    for (int i = 0; i < k; ++i) order->push_back(ord[(size_t)i]);
    return k;
}
/* the receiver's take test - this game's engine keeps the squad (or the member) at that spot, read live at the take;
   unreadable = not taken (counted) - the giver's release moves on or ends asleep, never a take its engine puts away */
static int KeepTakeAt(float x, float z)
{
    const int k = EngineKeepsHere(x, z);
    if (k < 0) { ++g_keepReadFailTake; return 0; }
    return k;
}
static int PlayerTableFresh() { int a = 0, b = 0, c = 0; return PeerPlayerSectorsTS(&a, &b, &c, 0) >= 0 ? 1 : 0; }
/* the XFER take path, factored (design 2.5 item 3) - used by the XFER and the RELEASE: one member this game takes from another game -
   owner record at `gen`, the giver's copy announced (its later withdrawal is this game's), not a sweep adoption, the puppet made this
   game's own, the squad's money snapshot; the carried goal never issued (T-1416). MAIN THREAD (the drain - engine writes allowed). */
static void TakeMemberFromPeer(const net::XferMember& mm, unsigned int gen, int* reRun, int awakeBlock = 0, int* skipped = 0)   /* T-1122 awake hand: awakeBlock / skipped; T-1416: reRun */
{
    net::TakeLocalOwner(mm.uid, gen);
    NoteAnnouncedOnTake(mm.uid);   /* M7a3f3 [m7a3f3-mg1]: the giver keeps a copy - its later withdrawal is owed by this game */
    ForgetSweepAdopt(mm.uid);      /* inv7e2: received by hand-over - never a reloaded copy */
    UnpuppetForOwnership(mm.uid);
    P323AiLine(mm.uid, "take");   // PROBE P323
    ++g_taken;
    g_catsDue = 1;   /* T-1 B3 restructure (F1): a taken squad's money is adopted at the next safe point */
    {   /* T-1 B3 fold (M2): the giver's newest announcement of this leader, kept for a take-over write held past its n = 0 */
        const coopsquad::PeerLeadEntry* te = g_peerLead.ForLeader(mm.uid);
        if (te != 0 && g_catsAdopt.find(mm.uid) == g_catsAdopt.end()) { CatsAdopt& s = g_catsTakeSnap[mm.uid]; s.seq = te->seq; s.cats = te->cats; }
    }
    P323TakeOrderBefore(mm.uid);   // PROBE P323
    {   /* T-1416 fix attempt 2 review fold 1: no order from the member's copy life survives the take (replicate.cpp DropOrdersOnTake) */
        ::Character* tc = FindSpawned(mm.uid);
        if (tc != 0 && DropOrdersOnTake(tc) == 1) ++g_takeOrdersDropped; else ++g_takeOrdersDropUnread;
    }
    /* T-1416 fix attempt 2 (F1556): no take issues the giver's carried goal. Issued, it was a player-style order (InjectOrder, 'clear old
       orders') that the engine's goal chooser takes first and that never finishes - a struck member went back to it, never fighting
       (F1555). The raid's campaign drives a squad whose raid came with it (T2094); every other taken member's AI re-chooses from its
       refilled goals and the squad's package, as the engine does for its own squads. The giver still sends the goal (net::XferMember's
       intent fields - no protocol change); here it is only counted and probed. */
    const int carried = raidsq::AwakeTakeOrder(awakeBlock, mm.intentType);
    if (carried == raidsq::kTakeOrderSkip) { if (skipped != 0) ++*skipped; }   /* T-1122 awake hand: the raid's campaign drives the squad (T2094: the carried goal pinned it) */
    else if (carried == raidsq::kTakeOrderReRun) { if (reRun != 0) ++*reRun; ++g_takeCarriedNotIssued; }   /* not issued - the take re-runs the AI */
    P323TakeOrder(mm.uid, mm.intentType, mm.intentSubject, carried == raidsq::kTakeOrderSkip ? 3 : carried == raidsq::kTakeOrderReRun ? 5 : 0);   // PROBE P323
}
static void RelLog(const std::string& s) { if (g_relLogged >= kRelLogCap) return; ++g_relLogged; DebugLog(s); }
static std::string SlotText(int s) { return s >= 0 ? "slot " + N((long long)s) : std::string("no slot"); }
static unsigned int RelNewId() { if (++g_releaseNextId == 0) ++g_releaseNextId; return g_releaseNextId; }
static unsigned int RelOfferGen(const ReleaseRec& r, const RelMember& m) { return cooplo::ReleaseOfferGen(m.baseGen, (int)r.tried.size()); }
static int RelIndexOf(const ReleaseRec& r, unsigned int uid) { for (size_t k = 0; k < r.mem.size(); ++k) if (r.mem[k].m.uid == uid) return (int)k; return -1; }
static void RelEnd(const ReleaseRec& r, const std::string& how)
{
    ++g_hoOpenSeq;   /* T-1318 fold 2: an offer closed */
    RelLog("[RELEASE] id=" + N((long long)r.id) + " END " + how + " (adopted " + N(r.nAdopted) + ", asleep " + N(r.nAsleep) + ", cancelled " + N(r.nCancelled)
           + ", moved to a split " + N(r.nMoved) + "; candidates tried " + N((long long)r.tried.size()) + ")");
}
static bool RelSendOffer(ReleaseRec& r, int resend)
{
    cooplo::ReleaseMsg msg; msg.id = r.id; msg.keyUid = r.keyUid; msg.flags = cooplo::kRelFlagPutAway | cooplo::kRelFlagWorldWhole | (r.lentWhole != 0 ? cooplo::kRelFlagLentWhole : 0u);   /* T-1122 lent flag (protocol 224); T-1318 attempt 2 (D3, protocol 226): every RELEASE offer is a world squad's, whole or none */
    msg.sectorKey = (r.sector.x >= 0 && r.sector.x < 64 && r.sector.y >= 0 && r.sector.y < 64) ? (unsigned int)(r.sector.y * 64 + r.sector.x) : cooplo::kNoSector;
    for (size_t k = 0; k < r.mem.size(); ++k)
    {
        cooplo::ReleaseRow row; row.uid = r.mem[k].m.uid; row.gen = RelOfferGen(r, r.mem[k]);
        std::memcpy(row.body, &r.mem[k].m, sizeof(row.body));
        msg.rows.push_back(row);
    }
    std::vector<char> b;
    if (!cooplo::ReleaseEncode(&b, msg) || !net::SendRelease(r.candidate, b)) return false;
    r.sentAt = NowSec(); ++r.sends;
    if (resend != 0) ++g_relResent; else ++g_relOffers;
    return true;
}
/* T-1122 awake hand (owner 1524): before each offer to the candidate (and its resend), the squad's raid block on the same road - store.cpp
   RaidAwakeHandSend sends it only when the squad's raid campaign was made on this game, read live there by the squad's faction and id */
static int RelAwakeBlock(const ReleaseRec& r)   /* T-1122 whole or none (owner 1581 a): RaidAwakeHandSend's gate (-1: a lent squad not offered) */
{
    if (r.candidate < 0) return 0;
    for (size_t k = 0; k < r.mem.size(); ++k)
        if (r.mem[k].hasKey != 0) { return RaidAwakeHandSend(0, r.mem[k].key.first, r.mem[k].key.second, r.awakeKey != 0 ? r.awakeKey : r.keyUid, r.candidate); }   /* T-1122 re-check fold 2: a release from a half-taken XFER sends its block under the XFER's leader */
    return 0;
}
/* REVOKE (design 2.5 item 4 [review F4]): to a late adopter - the winner's slot (-1: none, asleep / cancelled) and gen */
static bool RelRevokeSendOnce(int slot, unsigned int uid, int winnerSlot, unsigned int winnerGen)
{
    cooplo::ReleaseMsg msg; msg.id = RelNewId(); msg.keyUid = uid; msg.flags = cooplo::kRelFlagRevoke;
    msg.winnerSlot = winnerSlot >= 0 ? (unsigned int)winnerSlot : cooplo::kRelNoWinner;
    cooplo::ReleaseRow row; row.uid = uid; row.gen = winnerGen; msg.rows.push_back(row);
    std::vector<char> b;
    return cooplo::ReleaseEncode(&b, msg) && net::SendRelease(slot, b);
}
static void RelSendRevoke(int slot, unsigned int uid, int winnerSlot, unsigned int winnerGen, unsigned int lateGen)
{
    const bool ok = RelRevokeSendOnce(slot, uid, winnerSlot, winnerGen);
    ++g_relRevoked;
    size_t at = 0;
    for (; at < g_relRevokeOwed.size(); ++at) if (g_relRevokeOwed[at].slot == slot && g_relRevokeOwed[at].uid == uid) break;
    if (ok) { if (at < g_relRevokeOwed.size()) g_relRevokeOwed.erase(g_relRevokeOwed.begin() + (long)at); }   /* this newer outcome went: an older owed one is void */
    else
    {   /* [a1b2f1-hd7] [review F4]: OWED - the 1 Hz release tick sends it again until it goes (one per (slot, uid): the newest outcome) */
        RelRevokeOwed o; o.slot = slot; o.uid = uid; o.winnerSlot = winnerSlot; o.winnerGen = winnerGen; o.lateGen = lateGen;
        if (at < g_relRevokeOwed.size()) g_relRevokeOwed[at] = o;
        else
        {
            if (g_relRevokeOwed.size() >= 4096) { g_relRevokeOwed.erase(g_relRevokeOwed.begin()); ++g_relRevokeCapDropped; }
            g_relRevokeOwed.push_back(o); ++g_relRevokeOwedAdded;
        }
    }
    RelLog("[RELEASE] REVOKE uid=" + N((long long)uid) + " -> " + SlotText(slot) + " (it adopted late, gen " + N((long long)lateGen) + ") winner="
           + (winnerSlot >= 0 ? SlotText(winnerSlot) : std::string("none (asleep here)")) + " gen=" + N((long long)winnerGen)
           + (ok ? std::string() : std::string(" - NOT SENT (no road): OWED, sent again each second until it goes")) + " (release revoked " + N(g_relRevoked) + ")");
}
/* [a1b2f1-hd7] [review F4]: 1 Hz (ReleaseTick) - every owed REVOKE sent again; gone from the list only once sent */
static void RelRevokeOwedTick()
{
    for (size_t i = 0; i < g_relRevokeOwed.size(); )
    {
        const RelRevokeOwed o = g_relRevokeOwed[i];
        if (!RelRevokeSendOnce(o.slot, o.uid, o.winnerSlot, o.winnerGen)) { ++i; continue; }
        g_relRevokeOwed.erase(g_relRevokeOwed.begin() + (long)i); ++g_relRevokeRetrySent;
        RelLog("[RELEASE] REVOKE uid=" + N((long long)o.uid) + " -> " + SlotText(o.slot) + " SENT (owed since the road was down; it adopted late, gen " + N((long long)o.lateGen) + ") winner="
               + (o.winnerSlot >= 0 ? SlotText(o.winnerSlot) : std::string("none (asleep / cancelled here)")) + " gen=" + N((long long)o.winnerGen) + " (revokeRetrySent " + N(g_relRevokeRetrySent) + ")");
    }
}
/* [a1b2f1-hd8] [review F3]: is the put-away person rm awake in this game's engine again? 1 yes, 0 no, -1 unreadable (taken as no). Guarded
   reads only. T-1318 attempt 3 (owner 1615 a; T2324, F-T2324-WHOLEKEPT): judged PER PERSON - a body of THAT person (this game runs it under
   rm's uid, or a fresh body carries rm's hand: cooplo::RewokeBodyIs) in an active squad of its faction with that id. It asked "any awake
   member of the squad id" before: the squad's own awake people (and this game's copies of people another game runs) made every put-away of
   the rest read "re-woken" - CANCELLED / REVOKED, left asleep here. found = the squad the body is in (its area decides G1). */
static int PersonAwakeHere(const RelMember& rm, SquadView* found = 0, int note = 1)   /* [a1b2f2-hd5]: found = the awake squad (its area decides G1); note 0: not counted (the 1 Hz held re-check) */
{
    const HoKey& k = rm.key;
    if (rm.hasKey == 0 || !Plaus(k.first) || k.second.empty()) return -1;
    void** items = 0; unsigned int n = 0;
    if (FactionActiveListPod(k.first, &items, &n) == 0 || !Plaus(items) || n > 4096) return -1;
    int idAwake = 0;
    for (unsigned int i = 0; i < n; ++i)
    {
        void* ap = 0;
        if (ActiveOfListPod(items, i, &ap) == 0 || !Plaus(ap)) continue;
        SquadView sv;
        if (ReadSquadAt(ap, &sv) != 1 || sv.count <= 0 || !Plaus(sv.platoon)) continue;
        if (StoreIsWakeDup(sv.platoon) == 1) continue;   /* T-1318 fold 2: a copy whose wake was refused (a duplicate, or a given or open hand-over) is being removed - not this engine running the squad again */
        if (StoreWorldIdOf(sv.platoon) != k.second) continue;   /* [a1b2f2-hd9] */
        idAwake = 1;
        for (int m = 0; m < sv.stored; ++m)
        {
            if (!Plaus(sv.members[m])) continue;
            const unsigned int bu = FindSpawnedUid(sv.members[m]);
            std::string bh;
            if (!rm.hand.empty()) { ObjId oid; if (CaptureObjId(sv.members[m], &oid)) bh = cooplo::HandKey(oid.serial, oid.type); }
            if (cooplo::RewokeBodyIs(bu, (bu != 0 && net::IsUidMine(bu)) ? 1 : 0, rm.m.uid, bh, rm.hand) == 1) { if (found != 0) *found = sv; return 1; }
        }
    }
    if (idAwake != 0 && note != 0) ++g_wwRewokeNotPerson;   /* the squad id is awake here, this person is not (the old test said "re-woken") */
    return 0;
}
/* fold 2 [a1b2f2-hd5] [review G1]: the re-woken squad's area, read at this moment the way the sweep reads it (worldsync.cpp
   AnnounceAreaDecide: HeldByOtherTS of the sector of the squad's decision position, the position SquadDecisionPos gives the sweep).
   *awake 1 = the squad is awake in this game's engine again. Returns 1 another game holds the area, 0 this game (the sweep adopts the
   re-woken body), -1 unknown (no key, not awake, no readable position, or no fresh area map).
   T-1268: the sweep now adopts only where the map NAMES this game (coopor::AnnounceArea's kAnnounceNobodyYet is skipped), so the read
   is that same answer, and 0 means only "the map names this game". REVOKE stays exactly "the sweep adopts the re-woken body".
   T-1268 fold 1 (review M2): an area nobody holds yet answers -2 (coopor::RewakeAreaOf) and the late adopter WAITS
   (cooplo::AdoptAfterRewakeWait -> RelRewakeWaitPut), decided again by the next applied area map (RelRewakeWaitTick). */
static int RewakeAreaHeldByOther(const RelMember& rm, int* awake)
{
    *awake = 0;
    if (rm.hasKey == 0) return -1;
    SquadView sv;
    if (PersonAwakeHere(rm, &sv) != 1) return -1;   /* T-1318 attempt 3: this person, not any member of the squad id */
    *awake = 1;
    float x = 0.0f, z = 0.0f;
    if (SquadDecisionPosAt(sv, &x, &z) != 1) return -1;
    int held = -1, mine = -1;
    AreaViewTS(SectorOf(x, z), &held, &mine, 0, 0, 0, 0);
    return coopor::RewakeAreaOf(coopor::AnnounceArea(held, mine));   /* T-1268 fold 1 (M2): -2 = nobody holds it yet - the settle waits */
}
/* [a1b2f1-hd13] [review F6]: the released person becomes the new owner's puppet when its body is here; a put-away has no body - nothing to
   puppet: a counted debug line (the first 20), never an ErrorLog per person */
static void PuppetIfBody(unsigned int uid, ::Character* c, const char* where)
{
    if (Plaus(c)) { AdoptRemotePuppet(uid); return; }
    ++g_puppetNoBody;
    if (g_puppetNoBodyLogged >= 20) return;
    ++g_puppetNoBodyLogged;
    DebugLog(std::string("[RELEASE] ") + where + " uid=" + N((long long)uid) + " released with no body here (put away) - nothing to puppet (puppetNoBody " + N(g_puppetNoBody) + "; the first 20 logged)");
}
/* the uid's release ended in an adoption by slot (peer key `peer`) at gen: released here (no body - a put-away), the index's given, late
   adopters revoked */
static void RelAccept(const RelMember& rm, int slot, unsigned int peer, unsigned int gen)
{
    const unsigned int uid = rm.m.uid;
    ++g_hoOpenSeq;   /* T-1318 fold 2: a member's offer closed (accepted) */
    SquadCatsFinalBeforeRelease(uid);
    net::ReleaseLocalOwner(uid, peer, gen);
    SquadCatsReleased(uid);
    WorldStateOnOwnershipReleased(uid, 0);
    if (rm.hasKey != 0) { IdxPut(uid, rm.key, 0, peer); if (!rm.hand.empty() && g_givenHandOf.size() < 65536) g_givenHandOf[uid] = rm.hand; PartGivenNote(rm.key, "an accepted RELEASE ACK"); } else ++g_idxNoKey;   /* T-1318 D3 tripwire */   /* T-1046 fold 2: the hand goes with the given entry */
    ++g_relAdopted; ++g_released;
    RelDone& d = g_relDone[uid]; d.slot = slot; d.gen = gen; d.at = NowSec();
    StoreRunHeldSettle(uid);   /* T-850 fold 1b: taken by another game - out of every record, the owner record names it: a delete held for its squad is decided now (kept) */
    std::map<unsigned int, std::vector<RelLate> >::iterator l = g_relLate.find(uid);
    if (l == g_relLate.end()) return;
    const std::vector<RelLate> late(l->second);
    g_relLate.erase(l);
    for (size_t i = 0; i < late.size(); ++i) if (late[i].slot != slot) RelSendRevoke(late[i].slot, uid, slot, gen, late[i].gen);
}
/* one member settles with no candidate left (cancelled 0) or because this game's engine re-woke its squad (cancelled 1). cancelled 0: a late
   adopter is accepted instead of the sleep (a person another game runs is never put to sleep); cancelled 1: every late adopter is revoked
   (this engine runs the squad again under fresh uids). Otherwise asleep here (the index's asleepHere) and the put-away's UNLOAD (with
   receipts) goes now, so the other games' copies go. The member must already be out of every record. true = a late adopter accepted. */
static int RelAsleep(const RelMember& rm, int cancelled, unsigned int settleGen)   /* [a1b2f1-hd9]: 0 asleep, 1 a late adopter accepted, 2 cancelled; T-1268 fold 1: 3 waiting for the naming */
{
    const unsigned int uid = rm.m.uid;
    ++g_hoOpenSeq;   /* T-1318 fold 2: a member's offer closed (settled) */
    int awake = 0;
    const int heldByOther = RewakeAreaHeldByOther(rm, &awake);   /* [a1b2f2-hd1] [review G1]: the sweep's area read of the re-woken squad */
    if (cancelled == 0 && awake != 0)
    {   /* [a1b2f1-hd9] [review F3]: this game's engine re-woke its squad before the sweep met it - cancelled (a late adopter: the area rule below) */
        cancelled = 1; ++g_relRewokeAtSettle;
        RelLog("[RELEASE] uid=" + N((long long)uid) + " - its squad is awake here again (this game's engine re-woke it): CANCELLED, not put to sleep (rewokeAtSettle " + N(g_relRewokeAtSettle) + ")");
    }
    std::vector<RelLate> late;
    std::map<unsigned int, std::vector<RelLate> >::iterator l = g_relLate.find(uid);
    if (l != g_relLate.end()) { late = l->second; g_relLate.erase(l); }
    /* [a1b2f2-hd1] [review G1]: cancelled (re-woken here) - a late adopter is still accepted unless THIS game holds the re-woken squad's area
       (cooplo::AdoptAfterRewake: another game holds it, or its holder is unknown): the adopter's copy lives and the re-woken body here is the
       duplicate the orphan clean-up removes - never a revoke that leaves nobody running the person */
    if (cancelled != 0 && !late.empty() && net::IsUidMine(uid) && cooplo::AdoptAfterRewakeWait(1, heldByOther) == cooplo::kSettleWait)
    {   /* T-1268 fold 1 (review M2): nobody holds the re-woken squad's area yet - neither accepted nor revoked; the next applied area map decides */
        RelRewakeWait w; w.kind = 1; w.rm = rm; w.from = -1; w.fromPeer = 0; w.gen = 0; w.settleGen = settleGen; w.seq = AreaMapApplySeqTS(); w.late = late;
        g_relRewakeWait.push_back(w); ++g_relRewakeWaitPut;
        RelLog("[RELEASE] uid=" + N((long long)uid) + " - re-woken here and " + N((long long)late.size()) + " late adopter(s), but nobody holds its area yet: WAITS for the area map"
               " that names a holder (T-1268; rewakeWait put " + N(g_relRewakeWaitPut) + ")");
        return 3;
    }
    const int lateOk = cancelled == 0 ? 1 : (cooplo::AdoptAfterRewake(1, heldByOther) == cooplo::kSettleAccept ? 1 : 0);
    if (!late.empty() && lateOk != 0 && net::IsUidMine(uid))
    {
        if (cancelled != 0)
        {
            ++g_relRewokeLateAccepted; if (heldByOther < 0) ++g_relRewokeHolderUnknown;
            RelLog("[RELEASE] uid=" + N((long long)uid) + " - re-woken here, but " + (heldByOther < 0 ? std::string("its area's holder is unknown") : std::string("another game holds its area"))
                   + ": a late adopter is ACCEPTED, not revoked (the re-woken body here goes to the orphan clean-up) (rewokeLateAccepted " + N(g_relRewokeLateAccepted) + ")");
        }
        std::vector<unsigned int> gens; std::vector<int> slots;
        for (size_t i = 0; i < late.size(); ++i) { gens.push_back(late[i].gen); slots.push_back(late[i].slot); }
        const int pick = cooplo::LateAdopterPick(&gens[0], &slots[0], (int)gens.size());
        const RelLate w = late[(size_t)pick];
        late.erase(late.begin() + pick);
        if (!late.empty()) g_relLate[uid] = late;   /* RelAccept revokes the rest */
        ++g_relLateAccepted;
        RelLog("[RELEASE] uid=" + N((long long)uid) + " - no candidate took its current offer; " + SlotText(w.slot) + " adopted it LATE (gen " + N((long long)w.gen)
               + ") and runs it: accepted instead of the sleep (release lateAccepted " + N(g_relLateAccepted) + ")");
        RelAccept(rm, w.slot, w.peer, w.gen);
        return 1;
    }
    for (size_t i = 0; i < late.size(); ++i) RelSendRevoke(late[i].slot, uid, -1, settleGen, late[i].gen);
    if (cancelled != 0) { ++g_relCancelled; IdxKeepKey(uid, rm.key, rm.hasKey, rm.hand); }   /* [a1b2f1-hd9] [review F2]: in this game's world data only - kept here */
    else { ++g_relAsleep; if (rm.hasKey != 0) { IdxPut(uid, rm.key, 1, 0); KeptHandNote(uid, rm.hand); } else ++g_idxNoKey; }   /* T-1318 attempt 5 (D1-R1): its hand */
    RelDone& d = g_relDone[uid]; d.slot = -1; d.gen = settleGen; d.at = NowSec();
    if (net::IsUidMine(uid)) WithdrawHeldUnload(uid);   /* the UNLOAD held since the put-away (ReleasePendingHas is false for it now) */
    StoreRunHeldSettle(uid);   /* T-850 fold 1b: ended with nobody taking it (asleep here, or cancelled): a delete held for its squad is decided now */
    return cancelled != 0 ? 2 : 0;
}
/* endLine 0: a part of a record that stays open (no END line - each id has exactly one) */
static long long RelSettleAll(ReleaseRec& r, int cancelled, const std::string& how, int endLine)   /* [a1b2f2-hd2]: returns the members cancelled (kept here) */
{
    long long nc = 0;
    const std::vector<RelMember> mem(r.mem);
    r.mem.clear();   /* first: ReleasePendingHas must be false when each UNLOAD goes */
    for (size_t k = 0; k < mem.size(); ++k)
    {
        const unsigned int settleGen = mem[k].baseGen + 2u + (unsigned int)r.tried.size();   /* above every gen offered */
        const int endedAs = RelAsleep(mem[k], cancelled, settleGen);
        if (endedAs == 1) ++r.nAdopted; else if (endedAs == 2) { ++r.nCancelled; ++nc; } else if (endedAs == 0) ++r.nAsleep;   /* [a1b2f2-hd2b]; 3 = set aside (T-1268 fold 1) */
    }
    if (r.nAdopted > 0)
    {   /* T-1318 D3 tripwire: an offer that had an accepted ACK now settles people of the same squad asleep / kept here */
        std::set<HoKey> keys;
        for (size_t k = 0; k < mem.size(); ++k) if (mem[k].hasKey != 0) keys.insert(mem[k].key);
        for (std::set<HoKey>::const_iterator k = keys.begin(); k != keys.end(); ++k) PartGivenNote(*k, "the settle of an offer with an accepted ACK");
    }
    if (endLine != 0) RelEnd(r, how);
    else RelLog("[RELEASE] id=" + N((long long)r.id) + " " + how + " (" + N((long long)mem.size()) + " member(s): adopted late " + N(r.nAdopted) + ", cancelled " + N(r.nCancelled) + ")");
    return nc;   /* [a1b2f2-hd2c] */
}
/* T-1122 whole or none (owner 1581 a, 2026-11-09): a lent squad's people whose raid block does not go with the offer are never offered one by
   one - they stay asleep here, whole; the lent copy's own ending (its put-away's RETURN, or KEEPOWN) gives the squad back. The record ends. */
static void RelLentAsleep(ReleaseRec& r)
{
    r.candidate = -1;
    RaidLentWholeNote(raidsq::kLwRelHeld);
    RelSettleAll(r, 0, "ASLEEP - a lent raid squad is not offered one by one (whole or none, T-1122): its people stay asleep here and its own ending gives it back (key=" + N((long long)r.keyUid) + ")", 1);
}
/* one open record's next step (cooplo::ReleaseStep). false = the record ended (the caller erases it). */
static bool RelAdvance(ReleaseRec& r, double now)
{
    const int linkUp = StoreLiveReady() ? 1 : 0;
    const int fresh = PlayerTableFresh();
    if (r.candidate >= 0 && linkUp != 0 && fresh != 0 && r.lastTick > 0.0 && now > r.lastTick) r.clock += now - r.lastTick;   /* [review F5] */
    r.lastTick = now;
    for (int guard = 0; guard < 300; ++guard)
    {
        for (size_t k = 0; k < r.mem.size(); )
        {
            if (net::IsUidMine(r.mem[k].m.uid)) { ++k; continue; }
            { const unsigned int gone = r.mem[k].m.uid; ++g_relNotMine; r.mem.erase(r.mem.begin() + (long)k); StoreRunHeldSettle(gone); }   /* no longer this game's (a hire, a dual run's yield): nothing to offer; T-850 fold 1b: its release ended */
        }
        if (r.mem.empty()) { RelEnd(r, "emptied - no member is this game's any more"); return false; }
        std::vector<int> order; int holder = -1, niw = 0, nk = 0;
        const int nc = r.candidate < 0 ? ReceiversFor(r.sector, r.px, r.pz, r.tried, &order, &holder, &niw, &nk) : 0;
        const int candIn = r.candidate >= 0 ? (StoreRosterSlotInWorld(r.candidate) == 1 ? 1 : 0) : 0;
        const int st = cooplo::ReleaseStep(r.candidate >= 0 ? 1 : 0, nc > 0 ? nc : 0, linkUp, r.candidate < 0 ? (nc >= 0 ? 1 : 0) : fresh, candIn, r.clock, r.defers, now - r.sentAt);
        if (st == cooplo::kRsHold)
        {
            if (r.held == 0)
            {
                r.held = 1; ++g_relHeld; ++g_holderHold;
                RelLog("[RELEASE] id=" + N((long long)r.id) + " key=" + N((long long)r.keyUid) + " HELD - this game's notebook link is down or the area map is not fresh (no candidate clock runs; its UNLOADs stay held; releaseHolder hold "
                       + N(g_holderHold) + ")");
            }
            return true;
        }
        r.held = 0;
        if (st == cooplo::kRsWait) return true;
        if (st == cooplo::kRsResend)
        {   /* T-1122 awake hand: the raid's block said again first; whole-or-none review fold MED-1 (b): the resend honours the gate */
            const int rsGate = RelAwakeBlock(r);
            if (raidsq::LentGateHolds(rsGate) != 0) { RelLentAsleep(r); return false; }
            r.lentWhole |= raidsq::LentWholeFlagOf(rsGate);   /* T-1122 lent flag: the offer says "whole or none" itself */
            RelSendOffer(r, 1); return true;
        }
        if (st == cooplo::kRsOffer)
        {
            r.candidate = order[0]; r.clock = 0.0; r.defers = 0; r.sends = 0; r.sentAt = now;
            const int lentGate = RelAwakeBlock(r);   /* T-1122 awake hand: the raid's block before the offer */
            if (raidsq::LentGateHolds(lentGate) != 0) { RelLentAsleep(r); return false; }   /* T-1122 whole or none */
            r.lentWhole |= raidsq::LentWholeFlagOf(lentGate);   /* T-1122 lent flag (protocol 224): the offer says "whole or none" itself */
            const bool ok = RelSendOffer(r, 0);
            ++g_holderOffer; if (niw > 0) ++g_holderNotInWorld; if (nk > 0) ++g_keepNotKeep;
            RelLog("[RELEASE] id=" + N((long long)r.id) + " -> " + SlotText(r.candidate) + " key=" + N((long long)r.keyUid) + " n=" + N((long long)r.mem.size()) + " lentWhole=" + N((long long)r.lentWhole) + " worldWhole=1" /* T-1318 D3 fold 1: every offer carries the world bit (RelSendOffer) */ + " sector=" + SectorString(r.sector)
                   + (r.candidate == holder ? std::string(" (has the area loaded, the area's holder") : " (has the area loaded; holder " + (holder < 0 ? std::string("none") : SlotText(holder)))
                   + "; releaseHolder offer " + N(g_holderOffer) + ")"
                   + " gen=" + N((long long)RelOfferGen(r, r.mem[0])) + " candidate " + N((long long)r.tried.size() + 1) + " of " + N((long long)(r.tried.size() + order.size()))
                   + (r.flight != 0 ? std::string(" (from an XFER flight)") : std::string()) + (ok ? std::string() : std::string(" - NOT SENT yet (no road to that slot; sent again on the clock)")));
            return true;
        }
        if (st == cooplo::kRsNext)
        {
            const std::string why = candIn == 0 ? std::string("it left the world") : (r.defers > cooplo::kReleaseDeferMax ? std::string("it deferred too often") : std::string("no answer in 5 s of link-up time"));
            RelLog("[RELEASE] id=" + N((long long)r.id) + " " + SlotText(r.candidate) + " SKIPPED - " + why + "; the next candidate");
            r.tried.push_back(r.candidate); r.candidate = -1; r.clock = 0.0; r.defers = 0; ++g_relNext;
            continue;
        }
        /* kRsAsleep: no candidate (left) */
        std::string why = "ASLEEP - every candidate dropped, deferred or stayed silent";
        if (nk > 0)
        {   /* games have the area loaded, but none would keep the squad at its spot - an offer would only be put away again by
               that game's engine 4 s after the take (T1056's ~7 s bounce) */
            ++g_keepNotKeep; ++g_keepAsleepNoKeep; if (r.tried.empty()) ++g_holderNone;
            why = "ASLEEP - no game's engine keeps it at this spot (" + N((long long)nk) + " in-world game(s) with the area " + SectorString(r.sector) + " loaded; the squad at "
                  + N((long long)r.px) + "," + N((long long)r.pz) + " is within " + N((long long)(cooplo::kEngineEdgeStrip + cooplo::kKeepMarginUnits))
                  + " of a side they have not loaded; key=" + N((long long)r.keyUid) + "; keep asleepNoKeep " + N(g_keepAsleepNoKeep) + ")";
        }
        else if (r.tried.empty())
        {
            ++g_holderNone; if (niw > 0) ++g_holderNotInWorld;
            why = "ASLEEP - no other in-world game has the area " + SectorString(r.sector) + " loaded (holder " + (holder < 0 ? std::string("none") : SlotText(holder))
                  + (niw > 0 ? ", " + N((long long)niw) + " game(s) with it loaded not in the world" : std::string()) + ")";
            why += " (key=" + N((long long)r.keyUid) + "; releaseHolder none " + N(g_holderNone) + ")";
        }
        RelSettleAll(r, 0, why, 1);
        return false;
    }
    return true;
}
/* is uid in an open record or this frame's batch */
static bool RelHolds(unsigned int uid)
{
    for (std::map<void*, RelBatch>::const_iterator b = g_relBatch.begin(); b != g_relBatch.end(); ++b)
        for (size_t k = 0; k < b->second.mem.size(); ++k) if (b->second.mem[k].m.uid == uid) return true;
    for (std::map<unsigned int, ReleaseRec>::const_iterator r = g_release.begin(); r != g_release.end(); ++r)
        if (RelIndexOf(r->second, uid) >= 0) return true;
    for (std::map<HoKey, RelHeld>::const_iterator h = g_relHeldSq.begin(); h != g_relHeldSq.end(); ++h)   /* T-1318 attempt 3c: held undecided - its UNLOAD stays held */
        for (size_t k = 0; k < h->second.b.mem.size(); ++k) if (h->second.b.mem[k].m.uid == uid) return true;
    return false;
}
/* T-1122 review fold 3 (store.h): 1 = uid is in an open RELEASE offer (or this frame's batch) of this game - MAIN THREAD */
int HandoffReleaseHolds(unsigned int uid) { return RelHolds(uid) ? 1 : 0; }
/* M7a A1 build 2 [a1b2-hd3]: uid is still being handed on by this game - its UNLOAD is held (worldsync.cpp's three withdrawal paths) and
   a ROSTER CHECK is answered PENDING (design 2.5 item 8 [review F1]) */
bool ReleasePendingHas(unsigned int uid)
{
    ReleaseForgetDrain();
    return RelHolds(uid) || HandoffPendingHas(uid);
}
/* T-1318 attempt 3c (owner 1617 a): one decision for squad wk's held people and (cur, 0 at the 1 Hz re-check) its new batch - all of them
   together (cooplo::HeldReleaseDecide): kHdHold - held on (cur joins the held set); kHdOffer - *out = held + cur, the held set goes, the
   caller offers it as ONE whole put-away; kHdKeptOut / kHdOver64 - every person settles asleep here now (their UNLOADs go). Returns the
   decision. Inside a RelWalk. */
/* T-1318 attempt 3c: squad wk's held people re-checked (the 1 Hz RelHeldTick; fold 1, review L2: also RelHeldDecide before a new batch joins
   them) - a held person no longer this game's leaves the hold; a held person awake here again (PersonAwakeHere) leaves it CANCELLED, never
   offered. The held set may be erased (nobody left). Inside a RelWalk. */
static void RelHeldRecheck(const HoKey& wk)
{
    std::map<HoKey, RelHeld>::iterator h = g_relHeldSq.find(wk);
    if (h == g_relHeldSq.end()) return;
    std::vector<RelMember> keep, woken; std::vector<unsigned int> gone;
    for (size_t m = 0; m < h->second.b.mem.size(); ++m)
    {
        const RelMember& rm = h->second.b.mem[m];
        const int mine = net::IsUidMine(rm.m.uid) ? 1 : 0;
        const int step = cooplo::HeldMemberStep(mine, mine != 0 && PersonAwakeHere(rm, 0, 0) == 1 ? 1 : 0);
        if (step == cooplo::kHmGone) gone.push_back(rm.m.uid); else if (step == cooplo::kHmWoken) woken.push_back(rm); else keep.push_back(rm);
    }
    if (gone.empty() && woken.empty()) return;
    const RelBatch last(h->second.b);
    h->second.b.mem = keep;   /* first: the people leaving are out of RelHolds before their settle */
    if (keep.empty()) g_relHeldSq.erase(h);
    for (size_t g = 0; g < gone.size(); ++g) { ++g_relSent; ++g_relNotMine; ++g_wwHeldNotMine; StoreRunHeldSettle(gone[g]); }   /* as RelAdvance's not-mine erase */
    if (!woken.empty())
    {
        ReleaseRec r; r.id = RelNewId(); r.keyUid = last.keyUid; r.sector = last.sector; r.px = last.px; r.pz = last.pz; r.mem = woken; r.awakeKey = last.awakeKey; r.flight = last.flight;
        g_relSent += (long long)woken.size(); g_wwDroppedOnWake += (long long)woken.size();
        RelSettleAll(r, 1, "CANCELLED - held until the squad sleeps, but this game's engine runs the person(s) again (T-1318 attempt 3c; worldWhole4 droppedOnWake=" + N(g_wwDroppedOnWake) + ")", 1);
    }
}
/* T-1318 a4 fold 1 (review HIGH-1): the RELEASE's KEPT people of squad wk from its OWN SLEEPING RECORDS (the faction's sleeping platoon of
   that id - never a context platoon; StoreSleepersHands): a record that is not one of all's people (the batch and held people, by hand),
   a given person of the squad, a person of it in an open offer or XFER of this game, a corpse or another game's person seen in its list is
   KEPT here (cooplo::SleepRecordsKept - a never-numbered person among them). *records - the records read. -1 = no sleeping platoon or no
   records read. MAIN THREAD (inside a RelWalk). T-1318 a4 fold 2: *sleeping - a sleeping platoon of the squad found (LOW-A's 'squad
   gone'); a record whose body reads dead in the records is neither kept nor given (LOW-B, coopown::GdcCharDeadOf). */
static int RelRecordsKept(const HoKey& wk, const RelBatch& all, int* records, int* sleeping)
{
    *records = -1; *sleeping = 0;
    if (!Plaus(wk.first) || wk.second.empty()) return -1;
    void* pl = FindSleepingPlatoon((::Faction*)wk.first, wk.second);
    if (Plaus(pl)) *sleeping = 1;
    if (!Plaus(pl) || coop::IsContextPlatoon(pl) == 1) return -1;
    std::vector<std::string> recs; std::vector<int> recDead;
    if (StoreSleepersHands(pl, &recs, &recDead) != 1) return -1;
    *records = (int)recs.size();
    std::set<std::string> acc(all.handsAcc);
    for (size_t k = 0; k < all.mem.size(); ++k) if (!all.mem[k].hand.empty()) acc.insert(all.mem[k].hand);
    const std::map<HoKey, std::set<std::string> >::const_iterator dh = g_deadHandsOf.find(wk);
    if (dh != g_deadHandsOf.end()) acc.insert(dh->second.begin(), dh->second.end());
    const std::map<HoKey, SquadIdx>::const_iterator gs = g_squadIdx.find(wk);
    if (gs != g_squadIdx.end())
        for (std::map<unsigned int, unsigned int>::const_iterator g = gs->second.given.begin(); g != gs->second.given.end(); ++g)
        {
            const std::map<unsigned int, std::string>::const_iterator h = g_givenHandOf.find(g->first);
            if (h != g_givenHandOf.end() && !h->second.empty()) acc.insert(h->second);
        }
    for (std::map<unsigned int, ReleaseRec>::const_iterator r = g_release.begin(); r != g_release.end(); ++r)
        for (size_t m = 0; m < r->second.mem.size(); ++m) if (r->second.mem[m].hasKey != 0 && r->second.mem[m].key == wk && !r->second.mem[m].hand.empty()) acc.insert(r->second.mem[m].hand);
    for (std::map<unsigned int, Pending>::const_iterator pp = g_pending.begin(); pp != g_pending.end(); ++pp)
        for (std::map<unsigned int, std::string>::const_iterator q = pp->second.handOf.begin(); q != pp->second.handOf.end(); ++q)
        {
            const std::map<unsigned int, HoKey>::const_iterator ko = pp->second.keyOf.find(q->first);
            if (ko != pp->second.keyOf.end() && ko->second == wk && !q->second.empty()) acc.insert(q->second);
        }
    int dSkip = 0, dUnread = 0;
    const int kept = cooplo::SleepRecordsKept(recs, acc, &recDead, &dSkip, &dUnread);   /* a4 fold 2 (re-check LOW-B): a corpse's record neither kept nor given */
    g_wwRecDeadSkipped += dSkip; g_wwRecDeadUnread += dUnread;
    return kept;
}
static int RelHeldDecide(const HoKey& wk, const RelBatch* cur, RelBatch* out)
{
    if (cur != 0) RelHeldRecheck(wk);   /* T-1318 attempt 3 fold 1 (review L2): the held people re-checked as the 1 Hz tick does, before a new batch is decided with them */
    RelBatch all; int heldBatches = 0;
    std::map<HoKey, RelHeld>::iterator h = g_relHeldSq.find(wk);
    if (h != g_relHeldSq.end()) { all = h->second.b; heldBatches = h->second.batches; }
    const int prevRecTries = h != g_relHeldSq.end() ? h->second.recTries : 0;   /* T-1318 a4 fold 2 (re-check LOW-A) */
    if (cur != 0)
    {
        all.mem.insert(all.mem.end(), cur->mem.begin(), cur->mem.end()); all.keyUid = cur->keyUid; all.sector = cur->sector; all.px = cur->px; all.pz = cur->pz;
        if (cur->awakeKey != 0) all.awakeKey = cur->awakeKey;   /* fold 1 (MED-1): an ended flight's leader and mark go with the squad */
        if (cur->flight != 0) all.flight = 1;
        all.unaccAwake.insert(cur->unaccAwake.begin(), cur->unaccAwake.end());   /* T-1318 attempt 4 */
        all.handsAcc.insert(cur->handsAcc.begin(), cur->handsAcc.end()); if (cur->ap != 0) all.ap = cur->ap;   /* T-1318 a4 fold 1 */
        if (cur->over64 != 0) all.over64 = 1;
        if (cur->chars > all.chars) { all.chars = cur->chars; all.known = cur->known; }
    }
    std::set<unsigned int> uids;
    for (size_t k = 0; k < all.mem.size(); ++k) uids.insert(all.mem[k].m.uid);
    const int kept = KeptAsleepOf(wk, uids), awake = AwakeOfSquadHere(wk, uids);
    /* T-1318 attempt 3 fold 2 (re-check MED-A): an offer / XFER of the squad still open holds the batch (never a second offer); a squad
       another game took whole while these waited settles them asleep here (cooplo::kHdGiven) */
    const int open = RelOpenOfSquad(wk);
    int wholeGiven = 0;
    {
        const std::map<HoKey, SquadIdx>::const_iterator gs = g_squadIdx.find(wk);
        if (gs != g_squadIdx.end()) wholeGiven = cooplo::GivenSquadNotOffered((int)gs->second.given.size(), (int)gs->second.asleepHere.size(), (int)gs->second.keptHere.size());
    }
    /* T-1318 attempt 4 (owner 1626, design A, site 4), a4 fold 1 (review HIGH-1): the engine's own member list. unaccAwake - living members
       with no uid, or this game's uid not put away, in the squad's awake engine list now (SquadAwakeListOf) -> awake: held. NO AWAKE LIST
       (the squad asleep, or put away this frame - at a new batch and at the 1 Hz re-check alike): the squad's OWN SLEEPING RECORDS decide
       (RelRecordsKept: every record not accounted for - a never-numbered person among them - is KEPT -> the squad settles asleep here,
       whole); records unread: every living member its put-aways saw listed outside them was put away with it - KEPT too, never 0 (the
       numbered part is never offered as whole without them). A list over 64 counts as one kept. */
    int unaccPut = all.over64 != 0 ? 1 : 0, unaccAwake = 0, listChars = all.chars, listKnown = all.known, records = -1, fromRecords = 0, recUnread = 0, recSleeping = 0;
    {
        SquadView lv;
        if (SquadAwakeListOf(wk, &lv) == 1) { int kn = 0; unaccAwake = SquadUnaccountedIn(lv, 0, 0, &kn, 0); listChars = lv.count; listKnown = kn; if (lv.count > kSquadViewCap || lv.stored < lv.count) ++unaccPut; }
        else
        {
            const int rk = RelRecordsKept(wk, all, &records, &recSleeping);
            if (rk >= 0) { unaccPut += rk; fromRecords = 1; ++g_wwRecRead; }
            else { unaccPut += (int)all.unaccAwake.size(); ++g_wwRecUnread; }
            if (rk < 0) recUnread = 1;
        }
    }
    const int hdNumbered = all.mem.empty() ? (int)cooplo::kHdOffer : cooplo::HeldReleaseDecide((int)all.mem.size(), kept, awake, open, wholeGiven);
    int hd = all.mem.empty() ? (int)cooplo::kHdOffer : cooplo::HeldReleaseDecideUnacc((int)all.mem.size(), kept, awake, unaccPut, unaccAwake, open, wholeGiven);
    /* T-1318 a4 fold 2 (re-check LOW-A): no awake list and the records unread, where the numbered people would go whole - HELD, the 1 Hz
       re-decide reads the records again; never offered as whole on that road (T2343's Herbivore_9 / Starving Bandits_19 road). Ended by the
       records read, the squad gone or the try cap (cooplo::RecordsUnreadStep: settled asleep here, whole), or the world teardown. */
    int recStep = -1;
    if (recUnread != 0 && hd == cooplo::kHdOffer && !all.mem.empty())
    {
        recStep = cooplo::RecordsUnreadStep(recSleeping, prevRecTries, cooplo::kRuTryCap);
        if (recStep == cooplo::kRuHold) { hd = cooplo::kHdHold; ++g_wwRecHold; }
        else { hd = cooplo::kHdKeptOut; if (recStep == cooplo::kRuKeptGone) ++g_wwRecHoldGone; else ++g_wwRecHoldCap; }
        if (g_unaccLines < kUnaccLineCap && (recStep != cooplo::kRuHold || prevRecTries == 0))
        {
            ++g_unaccLines;
            DebugLog("[RELEASE] squad id='" + wk.second + "' key=" + N((long long)all.keyUid) + (recStep == cooplo::kRuHold ? " NOT OFFERED yet - held: no awake list and its own sleeping records not read; the 1 Hz re-decide reads them again"
                     : recStep == cooplo::kRuKeptGone ? " NOT OFFERED - settled asleep here, whole: its records never read and the squad gone (no sleeping platoon)" : " NOT OFFERED - settled asleep here, whole: its records not read in the tries allowed")
                     + " (T-1318 a4 fold 2; " + N((long long)all.mem.size()) + " numbered put-away people; tries=" + N((long long)prevRecTries) + " sleeping=" + N((long long)recSleeping)
                     + "; worldWhole7 recUnreadHeld=" + N(g_wwRecHold) + " recUnreadGone=" + N(g_wwRecHoldGone) + " recUnreadCap=" + N(g_wwRecHoldCap) + ")");
        }
    }
    else if (prevRecTries > 0 && fromRecords != 0) ++g_wwRecHoldRead;   /* a records-unread hold decided by its records now */
    if (recStep < 0 && hd != hdNumbered && (hd == cooplo::kHdKeptOut || hd == cooplo::kHdHold))
    {   /* a hold or a settle the numbered people alone would not have made: counted (worldWhole6), the first lines named */
        if (hd == cooplo::kHdKeptOut) ++g_wwRelUnaccKeptOut; else if (cur != 0) ++g_wwRelUnaccHeld;
        if ((hd == cooplo::kHdKeptOut || cur != 0) && g_unaccLines < kUnaccLineCap)
        {
            ++g_unaccLines;
            DebugLog("[RELEASE] squad id='" + wk.second + "' key=" + N((long long)all.keyUid) + (hd == cooplo::kHdKeptOut ? " NOT OFFERED - settled asleep here, whole" : " NOT OFFERED yet - held")
                     + " (T-1318 attempt 4, the engine's member list): chars=" + N((long long)listChars) + " known=" + N((long long)listKnown) + " - " + N((long long)unaccPut) + " person(s) of it kept asleep here "
                     + (fromRecords != 0 ? "by its own sleeping records (" + N((long long)records) + " records; a4 fold 1)" : std::string("seen listed outside the numbered people (its records not read)"))
                     + (all.over64 != 0 ? " (its list held over 64)" : "") + ", " + N((long long)unaccAwake) + " living member(s) outside the numbered people still listed; " + N((long long)all.mem.size())
                     + " numbered put-away people (worldWhole6 relUnaccKeptOut=" + N(g_wwRelUnaccKeptOut) + " relUnaccHeld=" + N(g_wwRelUnaccHeld) + "; the first " + N(kUnaccLineCap) + " unaccounted lines)");
        }
    }
    if (hd == cooplo::kHdHold)
    {
        RelHeld& H = g_relHeldSq[wk];
        H.b = all; H.batches = heldBatches + (cur != 0 ? 1 : 0);
        H.recTries = recStep == cooplo::kRuHold ? prevRecTries + 1 : 0;   /* T-1318 a4 fold 2 (re-check LOW-A) */
        if (cur != 0)
        {
            ++g_wwHeldBatches; g_wwHeldPeople += (long long)cur->mem.size(); if (open > 0) ++g_wwHeldOpen;
            if (g_wwLines < kWwLineCap && recStep != cooplo::kRuHold)
            {
                ++g_wwLines;
                DebugLog("[RELEASE] squad id='" + wk.second + "' key=" + N((long long)cur->keyUid) + " NOT OFFERED yet - held until the squad sleeps (T-1318 attempt 3c): awake here " + N((long long)awake)
                         + " person(s) of it (run by this game)" + (open > 0 ? ", " + N((long long)open) + " in an offer / XFER of it still open (T-1318 fold 2: never a second offer; worldWhole5 heldOpen=" + N(g_wwHeldOpen) + ")" : std::string()) + "; its " + N((long long)cur->mem.size()) + " put-away people wait undecided with " + N((long long)(all.mem.size() - cur->mem.size()))
                         + " held before (" + N((long long)H.batches) + " batch(es); worldWhole4 heldBatches=" + N(g_wwHeldBatches) + " heldPeople=" + N(g_wwHeldPeople) + ")");
            }
        }
        return hd;
    }
    if (h != g_relHeldSq.end()) g_relHeldSq.erase(h);   /* decided: out of the hold before any settle (ReleasePendingHas must be false at each UNLOAD) */
    if (all.mem.empty()) return cooplo::kHdKeptOut;   /* nothing left to decide */
    if (hd == cooplo::kHdOffer)
    {
        if (heldBatches > 0)
        {
            ++g_wwOfferedTogether;
            RelLog("[RELEASE] squad id='" + wk.second + "' - nobody of it awake here any more: its " + N((long long)heldBatches) + " held batch(es)" + std::string(cur != 0 ? " and this put-away" : "")
                   + " go as ONE whole offer of " + N((long long)all.mem.size()) + " people (T-1318 attempt 3c; worldWhole4 offeredTogether=" + N(g_wwOfferedTogether) + ")");
        }
        *out = all;
        return hd;
    }
    if (heldBatches > 0) ++g_wwSettledAfterHold;
    ReleaseRec r; r.id = RelNewId(); r.keyUid = all.keyUid; r.sector = all.sector; r.px = all.px; r.pz = all.pz; r.mem = all.mem; r.awakeKey = all.awakeKey; r.flight = all.flight;
    g_relSent += (long long)r.mem.size();
    if (hd == cooplo::kHdKeptOut) ++g_wwRelKeptOut; else if (hd == cooplo::kHdGiven) ++g_wwHeldGivenSettled; else ++g_wwRelOver64;
    const std::string why = recStep == cooplo::kRuKeptGone || recStep == cooplo::kRuKeptCap ? std::string("its own sleeping records never read (the squad gone, or the tries allowed spent) - never offered as whole without them (T-1318 a4 fold 2)")
                          : hd == cooplo::kHdKeptOut ? "this game keeps " + N((long long)(kept + unaccPut)) + " person(s) of it asleep un-offered (kept or asleep here; " + N((long long)unaccPut) + " of them by the engine's own list / records)"
                          : hd == cooplo::kHdGiven ? "another game took the squad whole while these waited for that offer's answer - never a second offer (T-1318 fold 2; worldWhole5 heldGivenSettled=" + N(g_wwHeldGivenSettled) + ")"
                          : std::string("it holds over 64 people (one message holds 64)");
    if (g_wwLines < kWwLineCap)
    {
        ++g_wwLines;
        DebugLog("[RELEASE] squad id='" + wk.second + "' key=" + N((long long)all.keyUid) + " NOT OFFERED (whole or none, T-1318 D3): " + why + " - its " + N((long long)all.mem.size())
                 + " put-away people (" + N((long long)heldBatches) + " batch(es) held before) stay asleep here, whole (worldWhole relKeptOut=" + N(g_wwRelKeptOut) + " relOver64=" + N(g_wwRelOver64)
                 + "; worldWhole4 settledAfterHold=" + N(g_wwSettledAfterHold) + ")");
    }
    RelSettleAll(r, 0, "ASLEEP - NOT OFFERED, whole or none (T-1318 D3): " + why + " (key=" + N((long long)r.keyUid) + ")", 1);
    if (hd == cooplo::kHdGiven) PartGivenNote(wk, "held put-aways of a squad another game took while they waited (T-1318 fold 2)");   /* the tripwire judges it */
    return hd;
}
/* every frame (HandoffTick, before the 1 Hz gate): the last frame's put-aways become records - at most 64 people each - and are offered */
static void ReleaseFlush(double now)
{
    if (g_relBatch.empty()) return;
    RelWalk walk;   /* [a1b2f1-hd10] [review F1] */
    std::map<void*, RelBatch> batch;
    batch.swap(g_relBatch);
    /* T-1318 attempt 3 fold 2 (re-check MED-A): ONE SQUAD, ONE DECISION. Every road that files a put-away (ReleaseOnPutAway under its engine
       squad, ReleaseOpenFromFlight under its squad's batch or a placeholder) ends here, the only place a batch is decided - so every batch of
       one squad key (its first keyed member's, as the decision below reads it) joins one base batch first (cooplo::ReleaseMergeBase: the
       squad's first engine batch, else its first), whatever road and order filed them. Two batches decided alone could each find nobody
       awake and nobody kept and go as two whole-or-none offers of one squad. */
    {
        std::vector<std::map<void*, RelBatch>::iterator> its; std::vector<int> sq, fl; std::map<HoKey, int> ids;
        for (std::map<void*, RelBatch>::iterator it = batch.begin(); it != batch.end(); ++it)
        {
            int id = -1;
            for (size_t k = 0; k < it->second.mem.size(); ++k)
                if (it->second.mem[k].hasKey != 0) { const int next = (int)ids.size(); id = ids.insert(std::make_pair(it->second.mem[k].key, next)).first->second; break; }
            its.push_back(it); sq.push_back(id); fl.push_back(it->second.flight != 0 ? 1 : 0);
        }
        std::vector<int> base(its.size(), 0);
        if (its.size() > 1 && cooplo::ReleaseMergeBase(&sq[0], &fl[0], (int)its.size(), &base[0]) > 0)
            for (size_t i = 0; i < its.size(); ++i)
            {
                if (base[i] == (int)i) continue;
                RelBatch& to = its[(size_t)base[i]]->second;
                const RelBatch& from = its[i]->second;
                to.mem.insert(to.mem.end(), from.mem.begin(), from.mem.end());
                if (to.awakeKey == 0) to.awakeKey = from.awakeKey;   /* an ended flight's leader (the raid's AWAKE block key) goes with the squad */
                to.unaccAwake.insert(from.unaccAwake.begin(), from.unaccAwake.end()); if (from.over64 != 0) to.over64 = 1;   /* T-1318 attempt 4 */
                to.handsAcc.insert(from.handsAcc.begin(), from.handsAcc.end()); if (to.ap == 0) to.ap = from.ap;   /* T-1318 a4 fold 1 */
                if (from.chars > to.chars) { to.chars = from.chars; to.known = from.known; }
                if (from.flight != 0) to.flight = 1;
                ++g_relBatchesMerged;
                RelLog("[RELEASE] squad id='" + (from.mem.empty() ? std::string() : from.mem[0].key.second) + "' - " + N((long long)from.mem.size()) + " put-away(s) of a second batch of it this frame join its batch (now "
                       + N((long long)to.mem.size()) + "): one squad, one decision (T-1318 fold 2; worldWhole5 batchesMerged=" + N(g_relBatchesMerged) + ")");
                batch.erase(its[i]);
            }
    }
    for (std::map<void*, RelBatch>::iterator it = batch.begin(); it != batch.end(); ++it)
    {
        const RelBatch& b = it->second;
        /* T-1318 fold 2 (re-check B1): a squad another game took at an accepted ACK and still wholly given (cooplo::GivenSquadNotOffered over
           the squad index; the batch is one engine squad, read by its first keyed member) is never offered again - these people are bodies
           this game made of a squad it gave away (T2316 Western Hive_13: offered a second time at 443.542). They settle asleep here
           (RelSettleAll: their UNLOADs go), counted, the first 20 logged. */
        int notOffer = 0; std::string notOfferId;
        for (size_t k = 0; k < b.mem.size(); ++k)
        {
            if (b.mem[k].hasKey == 0) continue;
            const std::map<HoKey, SquadIdx>::const_iterator gs = g_squadIdx.find(b.mem[k].key);
            if (gs != g_squadIdx.end() && cooplo::GivenSquadNotOffered((int)gs->second.given.size(), (int)gs->second.asleepHere.size(), (int)gs->second.keptHere.size()) == 1)
            { notOffer = 1; notOfferId = b.mem[k].key.second; }
            break;
        }
        /* T-1318 attempt 2 (design D3): A WORLD SQUAD IS OFFERED WHOLE OR NOT AT ALL (cooplo::WorldReleaseOffer). The batch is one engine
           squad's put-away; people of that squad this game put away and did NOT offer (kept / asleep here) mean the batch is not the whole
           squad; over 64 people would need two messages. Either way the squad is NOT offered: every person settles asleep here, whole,
           counted, a capped line. Every offer that goes says "whole or none" (kRelFlagWorldWhole, RelSendOffer).
           T-1318 attempt 3c (owner 1617 a): with people of the squad still awake here the batch is HELD UNDECIDED (RelHeldDecide); the
           batch that finds none awake goes with every held batch of the squad as ONE whole offer. */
        if (notOffer == 0)
        {
            HoKey wk; int haveWk = 0;
            for (size_t k = 0; k < b.mem.size() && haveWk == 0; ++k) if (b.mem[k].hasKey != 0) { wk = b.mem[k].key; haveWk = 1; }
            if (haveWk != 0)
            {
                RelBatch whole;
                if (RelHeldDecide(wk, &it->second, &whole) != cooplo::kHdOffer) continue;   /* held undecided, or settled asleep here */
                it->second = whole;   /* b: the squad's held batches and this one - one whole put-away */
            }
            else if (cooplo::WorldReleaseOffer((int)b.mem.size(), 0) != cooplo::kWroOffer)
            {   /* no squad key (never batched today - ReleaseOnPutAway keeps a keyless person): over 64, settled whole */
                ReleaseRec r; r.id = RelNewId(); r.keyUid = b.keyUid; r.sector = b.sector; r.px = b.px; r.pz = b.pz; r.mem = b.mem; r.awakeKey = b.awakeKey; r.flight = b.flight;
                g_relSent += (long long)r.mem.size(); ++g_wwRelOver64;
                RelSettleAll(r, 0, "ASLEEP - NOT OFFERED, whole or none (T-1318 D3): it holds over 64 people (key=" + N((long long)r.keyUid) + ")", 1);
                continue;
            }
        }
        for (size_t at = 0; at < b.mem.size(); at += (size_t)cooplo::kReleaseMaxMembers)
        {
            const size_t to = (at + (size_t)cooplo::kReleaseMaxMembers < b.mem.size()) ? at + (size_t)cooplo::kReleaseMaxMembers : b.mem.size();
            ReleaseRec r; r.id = RelNewId(); r.keyUid = b.keyUid; r.sector = b.sector; r.px = b.px; r.pz = b.pz; r.awakeKey = b.awakeKey; r.flight = b.flight;   /* fold 1 (MED-1): an ended flight's put-aways keep its leader and mark */
            for (size_t k = at; k < to; ++k) r.mem.push_back(b.mem[k]);
            g_relSent += (long long)r.mem.size();
            if (notOffer != 0)
            {
                ++g_relGivenNotOffered;
                const int toSlot = HandedOverSlot(0, notOfferId);
                if (g_relGivenNotOfferedLogged < 20)
                {
                    ++g_relGivenNotOfferedLogged;
                    DebugLog("[RELEASE] squad id='" + notOfferId + "' NOT OFFERED - " + SlotText(toSlot) + " took it at an accepted ACK and it is still wholly given; these " + N((long long)r.mem.size())
                             + " people are bodies this game made of it again (key=" + N((long long)r.keyUid) + "): kept asleep here (T-1318 fold 2; givenNotOffered=" + N(g_relGivenNotOffered) + "; the first 20)");
                }
                const std::vector<RelMember> settled(r.mem);
                RelSettleAll(r, 0, "ASLEEP - NOT OFFERED: its squad is still wholly given to " + SlotText(toSlot) + " (T-1318 fold 2; key=" + N((long long)r.keyUid) + ")", 1);
                /* T-1318 attempt 2: these bodies are this game's copies of people another game runs - they do not join the squad's asleepHere
                   (their UNLOADs went at the settle). Indexed asleep they made the squad part-given: its next wake was not refused, the
                   sweep's re-wake drop took the whole entry and the given people were adopted and offered again (T2315's road). */
                /* fold 1 (review HIGH-1 b): only a person whose hand is one of the squad's GIVEN hands is such a copy (cooplo::GivenNotOfferedRow);
                   any other stays asleep here, indexed, counted and noted for the tripwire - this game's own people are never dropped silently */
                std::set<std::string> givenHands;
                if (!settled.empty() && settled[0].hasKey != 0)
                {
                    const std::map<HoKey, SquadIdx>::const_iterator gs = g_squadIdx.find(settled[0].key);
                    if (gs != g_squadIdx.end())
                        for (std::map<unsigned int, unsigned int>::const_iterator g = gs->second.given.begin(); g != gs->second.given.end(); ++g)
                        { const std::map<unsigned int, std::string>::const_iterator h = g_givenHandOf.find(g->first); if (h != g_givenHandOf.end() && !h->second.empty()) givenHands.insert(h->second); }
                }
                long long ownKept = 0;
                for (size_t q = 0; q < settled.size(); ++q)
                {
                    if (g_idxOf.find(settled[q].m.uid) == g_idxOf.end()) continue;
                    const int row = cooplo::GivenNotOfferedRow(settled[q].hand.empty() ? 0 : 1, givenHands.count(settled[q].hand) != 0 ? 1 : 0);
                    if (row == cooplo::kNoRowErase) { IdxErase(settled[q].m.uid); ++g_wwGivenAsleepKept; continue; }
                    if (row == cooplo::kNoRowOwn) ++g_wwOwnKeptAsleep; else ++g_wwOwnKeptHandUnread;
                    ++ownKept;
                    if (settled[q].hasKey != 0) PartGivenNote(settled[q].key, "a wholly given squad's put-away that kept this game's own people asleep");
                }
                if (ownKept > 0 && g_wwOwnKeptLogged < 20)
                {
                    ++g_wwOwnKeptLogged;
                    DebugLog("[RELEASE] squad id='" + notOfferId + "' NOT OFFERED, " + N(ownKept) + " of its " + N((long long)settled.size()) + " put-away people are NOT bodies of given people (hand not given, or not read) - kept asleep here, indexed (T-1318 D3 fold 1; worldWhole2 ownKeptAsleep="
                             + N(g_wwOwnKeptAsleep) + " ownKeptHandUnread=" + N(g_wwOwnKeptHandUnread) + "; the first 20)");
                }
                continue;
            }
            RelLog("[RELEASE] id=" + N((long long)r.id) + " OPEN key=" + N((long long)r.keyUid) + " n=" + N((long long)r.mem.size()) + " sector=" + SectorString(r.sector)
                   + " - this game's engine put them away (release sent " + N(g_relSent) + ")");
            if (RelAdvance(r, now)) g_release[r.id] = r;
        }
    }
}
/* T-1318 attempt 3c (owner 1617 a): 1 Hz re-check of every held squad - events read as state, no timer. A held person no longer this
   game's (it died, or another road took it) leaves the hold; a held person awake here again (PersonAwakeHere - the engine runs it again)
   leaves it CANCELLED, never offered (its UNLOAD goes, kept here: the sweep's re-wake clears it); then the squad is decided again with no new
   batch (RelHeldDecide): its awake people gone by any road (put away into a batch, died, taken) -> the held people go as one whole offer, or
   settle. A hold so never outlives its people; the world teardown clears it (ReleaseForgetDrain). */
static void RelHeldTick(double now)
{
    if (g_relHeldSq.empty()) return;
    RelWalk walk;
    std::vector<HoKey> keys;
    for (std::map<HoKey, RelHeld>::const_iterator h = g_relHeldSq.begin(); h != g_relHeldSq.end(); ++h) keys.push_back(h->first);
    for (size_t i = 0; i < keys.size(); ++i)
    {
        if (g_relHeldSq.find(keys[i]) == g_relHeldSq.end()) continue;
        RelHeldRecheck(keys[i]);   /* gone / re-woken held people leave the hold (fold 1: shared with RelHeldDecide's new-batch road, review L2) */
        if (g_relHeldSq.find(keys[i]) == g_relHeldSq.end()) continue;
        RelBatch whole;
        if (RelHeldDecide(keys[i], 0, &whole) != cooplo::kHdOffer || whole.mem.empty()) continue;
        ReleaseRec r; r.id = RelNewId(); r.keyUid = whole.keyUid; r.sector = whole.sector; r.px = whole.px; r.pz = whole.pz; r.mem = whole.mem; r.awakeKey = whole.awakeKey; r.flight = whole.flight;   /* <= 64: HeldReleaseDecide */
        g_relSent += (long long)r.mem.size();
        RelLog("[RELEASE] id=" + N((long long)r.id) + " OPEN key=" + N((long long)r.keyUid) + " n=" + N((long long)r.mem.size()) + " sector=" + SectorString(r.sector)
               + " - this game's engine put them away; held until the squad slept (release sent " + N(g_relSent) + ")");
        if (RelAdvance(r, now)) g_release[r.id] = r;
    }
}
/* T-1268 fold 1 (review M2): each set-aside member is decided again once an area map was applied after it was set aside (the event that
   ends the sweep's own 'nobody yet' wait): still nobody - waits on; otherwise AdoptAfterRewake's rule, exactly as at the ACK / settle.
   A member no longer this game's is dropped (another road settled it). Inside ReleaseTick's walk. */
static void RelRewakeWaitTick()
{
    if (g_relRewakeWait.empty()) return;
    const unsigned int seq = AreaMapApplySeqTS();
    std::vector<RelRewakeWait> keep, due;
    for (size_t i = 0; i < g_relRewakeWait.size(); ++i)
    {
        if (cooplo::AdoptAfterRewakeDue(g_relRewakeWait[i].seq, seq) == 0) keep.push_back(g_relRewakeWait[i]); else due.push_back(g_relRewakeWait[i]);
    }
    g_relRewakeWait.swap(keep);
    for (size_t i = 0; i < due.size(); ++i)
    {
        RelRewakeWait& w = due[i];
        const unsigned int uid = w.rm.m.uid;
        if (!net::IsUidMine(uid)) { ++g_relRewakeWaitNotMine; continue; }
        int awake = 0;
        const int area = RewakeAreaHeldByOther(w.rm, &awake);
        if (cooplo::RewakeWaitNeeded(w.kind, (int)w.late.size()) != 0 && cooplo::AdoptAfterRewakeWait(awake, area) == cooplo::kSettleWait)
        { w.seq = seq; g_relRewakeWait.push_back(w); ++g_relRewakeWaitStillNobody; continue; }
        ++g_relRewakeWaitDecided;
        if (!w.late.empty())
        {   /* T-1268 fold 2: its late adopters back where the settle reads them - RelAccept revokes all but the winner, a cancel all */
            std::vector<RelLate>& l = g_relLate[uid];
            l.insert(l.end(), w.late.begin(), w.late.end());
        }
        if (w.kind == 1)
        {   /* the settle, decided by RelAsleep's own rule now that the area answers (no adopter left: cancelled, kept here) */
            const int r = RelAsleep(w.rm, 1, w.settleGen);
            RelLog("[RELEASE] uid=" + N((long long)uid) + " - the area map now answers for its re-woken squad: settled " + std::string(r == 1 ? "(a late adopter accepted)" : r == 2 ? "(cancelled, kept here)" : r == 3 ? "(waits again)" : "(asleep)")
                   + " (T-1268; rewakeWait decided " + N(g_relRewakeWaitDecided) + ")");
            continue;
        }
        if (cooplo::AdoptAfterRewake(awake, area) == cooplo::kSettleRevoke)
        {
            ++g_relRewokeAtAck;
            RelLog("[RELEASE] uid=" + N((long long)uid) + " adopted by " + SlotText(w.from) + " after this game's engine re-woke its squad; the area map now names THIS game - REVOKED, cancelled here"
                   " (T-1268; rewakeWait decided " + N(g_relRewakeWaitDecided) + ")");
            RelSendRevoke(w.from, uid, -1, w.settleGen, w.gen);
            RelAsleep(w.rm, 1, w.settleGen);
            continue;
        }
        ++g_relRewokeAcceptAtAck; if (area < 0) ++g_relRewokeHolderUnknown;
        RelLog("[RELEASE] uid=" + N((long long)uid) + " adopted by " + SlotText(w.from) + " after this game's engine re-woke its squad; the area map now answers "
               + std::string(awake == 0 ? "(asleep again here)" : area < 0 ? "(no fresh map)" : "(another game holds it)") + " - ACCEPTED (T-1268; rewakeWait decided " + N(g_relRewakeWaitDecided) + ")");
        RelAccept(w.rm, w.from, w.fromPeer, w.gen);
    }
}
/* 1 Hz: every open record's next step; the settled-uid book pruned */
static void ReleaseTick(double now)
{
    RelWalk walk;   /* [a1b2f1-hd10] [review F1]: the walk of g_release below - a nested drain (RelAsleep -> WithdrawHeldUnload -> ReleasePendingHas) defers */
    RelRevokeOwedTick();   /* [a1b2f1-hd7] [review F4] */
    RelRewakeWaitTick();   /* T-1268 fold 1 (review M2) */
    for (std::map<unsigned int, ReleaseRec>::iterator it = g_release.begin(); it != g_release.end(); )
    {
        if (RelAdvance(it->second, now)) ++it; else g_release.erase(it++);
    }
    for (std::map<unsigned int, RelDone>::iterator d = g_relDone.begin(); d != g_relDone.end(); )
    {
        if (now - d->second.at > kRelDoneKeepSec || now < d->second.at) g_relDone.erase(d++); else ++d;
    }
}
/* design 2.5 item 1: members of this game's XFER that were not taken (`taken` lists the taken), or of an abandoned one, that this game still
   runs but whose body its engine put away while the XFER flew - they become a release (their UNLOAD stays held) */
static void ReleaseOpenFromFlight(unsigned int leader, const Pending& p, const std::vector<unsigned int>& taken, int abandoned)
{
    RelWalk walk;   /* [a1b2f1-hd10] [review F1] */
    ReleaseRec r; r.flight = 1;
    for (size_t k = 0; k < p.members.size(); ++k)
    {
        const unsigned int u = p.members[k].uid;
        std::map<unsigned int, int>::iterator sw = g_sameIdWd.find(u);
        const int sameIdWd = (sw != g_sameIdWd.end()) ? 1 : 0;
        if (sameIdWd != 0) g_sameIdWd.erase(sw);   /* its flight ends here: the mark leaves */
        if (std::find(taken.begin(), taken.end(), u) != taken.end()) continue;
        if (!net::IsUidMine(u) || Plaus(FindSpawned(u)) || RelHolds(u)) continue;   /* live here: still this game's - the pass decides */
        RelMember rm; rm.m = p.members[k]; rm.baseGen = net::MineGenOf(u);
        std::map<unsigned int, HoKey>::const_iterator kk = p.keyOf.find(u);
        if (kk != p.keyOf.end()) { rm.key = kk->second; rm.hasKey = 1; }
        { const std::map<unsigned int, std::string>::const_iterator hh = p.handOf.find(u); if (hh != p.handOf.end()) rm.hand = hh->second; }   /* T-1046 leg-N fold 2: its hand as read at the send - an adoption's given entry carries it */
        const int fpr = cooplo::FlightPutAwayRoad(sameIdWd, rm.hasKey);
        if (fpr == cooplo::kFprUnloadSameId)
        {   /* a same-id retire withdrew it: another game runs this person - its held UNLOAD goes, never offered */
            ++g_relFlightSameId; WithdrawHeldUnload(u);
            continue;
        }
        if (fpr == cooplo::kFprUnloadNoKey)
        {   /* [a1b2f1-hd11] [review F3]: no squad key - never offered (a re-wake could not cancel it): it sleeps here, its held UNLOAD goes now */
            ++g_relFlightNoKey; IdxKeepKey(u, HoKey(), 0, rm.hand); WithdrawHeldUnload(u);
            continue;
        }
        r.mem.push_back(rm);
    }
    if (r.mem.empty()) return;
    if (abandoned != 0) g_relFlightAbandoned += (long long)r.mem.size(); else g_relFlightNotTaken += (long long)r.mem.size();
    /* T-1318 attempt 3 fold 1 (review MED-1, owner 1617 a): ONE ROAD FOR EVERY RELEASE. These put-aways were not batched while the XFER flew
       (ReleaseOnPutAway's kPadLeaveToAck); they are not offered here any more - they join this frame's put-away batch of their squad (the
       same squad key: one batch, one decision) or open one, and ReleaseFlush decides them exactly as an engine put-away: a wholly given
       squad's not-offered settle, then RelHeldDecide (people of the squad kept / asleep here -> settled asleep; awake here -> HELD until the
       squad sleeps; none -> one whole offer). Before, they were offered at once with no whole test and no hold while the rest of the squad
       was awake here - a squad split across games. Their UNLOADs stay held (RelHolds reads the batch). */
    std::map<HoKey, std::vector<RelMember> > byKey;
    for (size_t k = 0; k < r.mem.size(); ++k) byKey[r.mem[k].key].push_back(r.mem[k]);   /* every member here has a key (kFprRelease) */
    const unsigned int awakeKey = leader != 0 ? leader : r.mem[0].m.uid;   /* T-1122 re-check fold 2: the raid's AWAKE block goes under the XFER's leader */
    for (std::map<HoKey, std::vector<RelMember> >::const_iterator g = byKey.begin(); g != byKey.end(); ++g)
    {
        void* slot = 0;
        for (std::map<void*, RelBatch>::const_iterator b = g_relBatch.begin(); b != g_relBatch.end() && slot == 0; ++b)
            for (size_t m = 0; m < b->second.mem.size(); ++m) if (b->second.mem[m].hasKey != 0) { if (b->second.mem[m].key == g->first) slot = b->first; break; }
        for (int s = 0; s < 256 && slot == 0; ++s) if (g_relBatch.find((void*)&g_relFlightBatchKey[s]) == g_relBatch.end()) slot = (void*)&g_relFlightBatchKey[s];
        if (slot == 0)
        {   /* 256 flight batches in one frame (never expected): kept asleep here, their UNLOADs go - never offered outside the one road */
            ++g_relFlightBatchFull;
            for (size_t m = 0; m < g->second.size(); ++m) { IdxKeepKey(g->second[m].m.uid, g->first, 1, g->second[m].hand); WithdrawHeldUnload(g->second[m].m.uid); }
            DebugLog("[RELEASE] squad id='" + g->first.second + "' - " + N((long long)g->second.size()) + " member(s) of XFER leader=" + N((long long)leader) + " put away while it flew: NO batch slot free - kept asleep here (relFlightBatchFull " + N(g_relFlightBatchFull) + ")");
            continue;
        }
        RelBatch& b = g_relBatch[slot];
        if (b.mem.empty())
        {   /* the offer keeps its own key (a uid this game runs, else one in the offer - the receiver reads the squad's spot by it) */
            b.keyUid = net::IsUidMine(leader) ? leader : g->second[0].m.uid; b.sector = SectorOf(g->second[0].m.x, g->second[0].m.z); b.px = g->second[0].m.x; b.pz = g->second[0].m.z;
        }
        if (b.awakeKey == 0) b.awakeKey = awakeKey;
        b.flight = 1;
        b.mem.insert(b.mem.end(), g->second.begin(), g->second.end());
        g_relFlightBatched += (long long)g->second.size();
        RelLog("[RELEASE] squad id='" + g->first.second + "' - " + N((long long)g->second.size()) + " member(s) of XFER leader=" + N((long long)leader) + (abandoned != 0 ? std::string(" (abandoned, no ACK)") : std::string(" (not taken)"))
               + " whose body this game's engine put away while it flew join its put-away batch (now " + N((long long)b.mem.size()) + "), decided as a put-away: held while people of it are awake here, else whole or none (T-1318 attempt 3 fold 1; relFlightBatched " + N(g_relFlightBatched) + ")");
    }
}
/* an adoption that is not the current offer's (design 2.5 items 4, 6): a late adopter of an open record waits for the settle (accepted then
   if nobody else adopts); after the settle it is revoked by the outcome; a repeat of the winner's own ACK is ignored */
static void RelLateAdopted(unsigned int uid, int from, unsigned int peer)
{
    for (std::map<unsigned int, ReleaseRec>::iterator it = g_release.begin(); it != g_release.end(); ++it)
    {
        const int k = RelIndexOf(it->second, uid);
        if (k < 0) continue;
        int t = -1;
        for (size_t i = 0; i < it->second.tried.size(); ++i) if (it->second.tried[i] == from) t = (int)i;
        if (t < 0 || from < 0) { ++g_relAckWrongSender; return; }
        const unsigned int lateGen = cooplo::ReleaseOfferGen(it->second.mem[(size_t)k].baseGen, t);
        const unsigned int curGen = RelOfferGen(it->second, it->second.mem[(size_t)k]);
        if (cooplo::GiverSettleOnAck(cooplo::kVerdictAdopted, lateGen, curGen, it->second.candidate >= 0 ? 1 : 0) != cooplo::kSettleRevoke) return;
        std::vector<RelLate>& v = g_relLate[uid];
        for (size_t i = 0; i < v.size(); ++i) if (v[i].slot == from) return;
        RelLate l; l.slot = from; l.peer = peer; l.gen = lateGen; v.push_back(l);
        RelLog("[RELEASE] id=" + N((long long)it->first) + " uid=" + N((long long)uid) + " adopted LATE by " + SlotText(from) + " at gen " + N((long long)lateGen)
               + " (the current offer is gen " + N((long long)curGen) + ") - its REVOKE waits for the settle");
        return;
    }
    for (size_t wi = 0; wi < g_relRewakeWait.size(); ++wi)
    {   /* T-1268 fold 2: the member waits for the naming - the late adopter joins it and the wait's decision answers it */
        RelRewakeWait& w = g_relRewakeWait[wi];
        if (w.rm.m.uid != uid) continue;
        int listed = 0;
        for (size_t i = 0; i < w.late.size(); ++i) if (w.late[i].slot == from) listed = 1;
        if (from < 0 || cooplo::RewakeLateAck(w.from, listed, from) == cooplo::kRwLateDup) { ++g_relDupAck; return; }
        RelLate l; l.slot = from; l.peer = peer; l.gen = 0; w.late.push_back(l); ++g_relRewakeWaitLateAdded;
        RelLog("[RELEASE] uid=" + N((long long)uid) + " adopted LATE by " + SlotText(from) + " while it waits for its area's naming - answered by the wait's decision (T-1268; rewakeWait lateAdded "
               + N(g_relRewakeWaitLateAdded) + ")");
        return;
    }
    std::map<unsigned int, RelDone>::const_iterator d = g_relDone.find(uid);
    if (d != g_relDone.end())
    {
        if (d->second.slot == from && from >= 0) { ++g_relDupAck; return; }
        RelSendRevoke(from, uid, d->second.slot, d->second.gen, 0);
        return;
    }
    if (net::IsUidMine(uid) && Plaus(FindSpawned(uid))) { RelSendRevoke(from, uid, StoreMySlot(), net::MineGenOf(uid), 0); return; }
    ++g_relAckUnknown;
}
void ApplyRemoteReleaseAck(const cooplo::ReleaseAckMsg& a, unsigned int fromPeer)
{
    ReleaseForgetDrain();
    RelWalk walk;   /* [a1b2f1-hd10] [review F1]: holds a ReleaseRec& across RelAccept / RelAdvance / the split */
    const int from = net::PeerSlotOfKey(fromPeer);
    const double now = NowSec();
    std::map<unsigned int, ReleaseRec>::iterator it = g_release.find(a.id);
    const bool current = it != g_release.end() && it->second.candidate >= 0 && it->second.candidate == from;
    if (!current)
    {   /* not the current offer of that record: only an adoption needs an answer (REVOKE); a stale drop / defer is ignored */
        for (size_t i = 0; i < a.adopted.size(); ++i) RelLateAdopted(a.adopted[i], from, fromPeer);
        if (!a.dropped.empty() || !a.deferred.empty()) ++g_relAckStale;
        return;
    }
    ReleaseRec& r = it->second;
    long long na = 0, nd = 0, nf = 0;
    for (size_t i = 0; i < a.adopted.size(); ++i)
    {
        const int k = RelIndexOf(r, a.adopted[i]);
        if (k < 0) { RelLateAdopted(a.adopted[i], from, fromPeer); continue; }
        const unsigned int g = RelOfferGen(r, r.mem[(size_t)k]);
        if (cooplo::GiverSettleOnAck(cooplo::kVerdictAdopted, g, g, 1) != cooplo::kSettleAccept) continue;
        const RelMember rm = r.mem[(size_t)k];
        r.mem.erase(r.mem.begin() + k);
        int awake = 0;
        const int heldByOther = RewakeAreaHeldByOther(rm, &awake);   /* [a1b2f2-hd6] [review G1]: the sweep's own area read, as the ACK is answered */
        if (cooplo::AdoptAfterRewakeWait(awake, heldByOther) == cooplo::kSettleWait)
        {   /* T-1268 fold 1 (review M2): nobody holds the re-woken squad's area yet - the adoption waits for the area map that names a holder */
            RelRewakeWait w; w.kind = 0; w.rm = rm; w.from = from; w.fromPeer = fromPeer; w.gen = g; w.settleGen = rm.baseGen + 2u + (unsigned int)r.tried.size();
            w.seq = AreaMapApplySeqTS(); g_relRewakeWait.push_back(w); ++g_relRewakeWaitPut;
            RelLog("[RELEASE] id=" + N((long long)r.id) + " uid=" + N((long long)rm.m.uid) + " adopted by " + SlotText(from) + " after this game's engine re-woke its squad, but nobody"
                   " holds its area yet: WAITS for the area map that names a holder (T-1268; rewakeWait put " + N(g_relRewakeWaitPut) + ")");
            continue;
        }
        if (cooplo::AdoptAfterRewake(awake, heldByOther) == cooplo::kSettleRevoke)
        {   /* [a1b2f1-hd9] [review F3] / [review G1]: this game's engine re-woke the squad before the sweep met it AND this game holds its area (the
               sweep adopts the re-woken body) - the adoption is REVOKED (no winner: the old uid is run by nobody, this engine runs the person under
               a fresh uid) and the member cancelled (kept here) */
            const unsigned int settleGen = rm.baseGen + 2u + (unsigned int)r.tried.size();
            ++g_relRewokeAtAck;
            RelLog("[RELEASE] id=" + N((long long)r.id) + " uid=" + N((long long)rm.m.uid) + " adopted by " + SlotText(from) + " AFTER this game's engine re-woke its squad - REVOKED, cancelled here (rewokeAtAck " + N(g_relRewokeAtAck) + ")");
            RelSendRevoke(from, rm.m.uid, -1, settleGen, g);
            RelAsleep(rm, 1, settleGen); ++r.nCancelled;
            continue;
        }
        if (awake != 0)
        {   /* [a1b2f2-hd6] [review G1]: re-woken here, but another game (the adopter) holds the area, or its holder is unknown - ACCEPTED: the
               adopter's copy lives; the re-woken body here is the duplicate the sweep's area rule sends to the orphan clean-up */
            ++g_relRewokeAcceptAtAck; if (heldByOther < 0) ++g_relRewokeHolderUnknown;
            RelLog("[RELEASE] id=" + N((long long)r.id) + " uid=" + N((long long)rm.m.uid) + " adopted by " + SlotText(from) + " after this game's engine re-woke its squad - ACCEPTED ("
                   + (heldByOther < 0 ? std::string("area holder unknown") : std::string("another game holds the area")) + "; the re-woken body here goes to the orphan clean-up) (rewokeAcceptAtAck "
                   + N(g_relRewokeAcceptAtAck) + ")");
        }
        RelAccept(rm, from, fromPeer, g); ++r.nAdopted; ++na;
    }
    std::vector<RelMember> moved;
    for (size_t i = 0; i < a.dropped.size(); ++i)
    {
        const int k = RelIndexOf(r, a.dropped[i]);
        if (k < 0) continue;
        const unsigned int g = RelOfferGen(r, r.mem[(size_t)k]);
        if (cooplo::GiverSettleOnAck(cooplo::kVerdictDropped, g, g, 1) != cooplo::kSettleNext) continue;
        moved.push_back(r.mem[(size_t)k]); r.mem.erase(r.mem.begin() + k); ++nd; ++g_relDropped;
    }
    for (size_t i = 0; i < a.deferred.size(); ++i)
    {
        const int k = RelIndexOf(r, a.deferred[i]);
        if (k < 0) continue;
        const unsigned int g = RelOfferGen(r, r.mem[(size_t)k]);
        if (cooplo::GiverSettleOnAck(cooplo::kVerdictDeferred, g, g, 1) == cooplo::kSettleRetry) { ++nf; ++g_relDeferred; }
    }
    if (nf > 0) { ++r.defers; r.clock = 0.0; r.sentAt = now; }   /* re-offered on the defer clock, at most kReleaseDeferMax times [review F5] */
    RelLog("[RELEASE] id=" + N((long long)r.id) + " <- ACK " + SlotText(from) + " adopted=" + N(na) + " dropped=" + N(nd) + " deferred=" + N(nf));
    if (!moved.empty())
    {
        if (r.mem.empty())
        {   /* every remaining member dropped: this record moves on to the next candidate */
            r.mem.swap(moved);
            r.tried.push_back(r.candidate); r.candidate = -1; r.clock = 0.0; r.defers = 0; ++g_relNext;
        }
        else
        {   /* others deferred: the dropped ones move to a new record for the next candidate */
            ReleaseRec nr; nr.id = RelNewId(); nr.keyUid = r.keyUid; nr.sector = r.sector; nr.px = r.px; nr.pz = r.pz; nr.mem = moved; nr.tried = r.tried; nr.tried.push_back(r.candidate); nr.flight = r.flight;
            r.nMoved += (long long)moved.size();
            RelLog("[RELEASE] id=" + N((long long)nr.id) + " OPEN split from id=" + N((long long)r.id) + " n=" + N((long long)nr.mem.size()) + " - dropped by " + SlotText(r.candidate) + " while others deferred");
            if (RelAdvance(nr, now)) g_release[nr.id] = nr;
        }
    }
    if (r.mem.empty()) { RelEnd(r, "SETTLED"); g_release.erase(it); return; }
    if (!RelAdvance(r, now)) g_release.erase(it);
}
/* 2.5 item 4b: a REVOKE - this game adopted uid late; the winner runs it (puppet - never a removal of a body another game runs) or nobody
   does (removed: the giver's world data holds the person asleep) */
static void RelApplyRevokeRow(unsigned int uid, unsigned int wgen, unsigned int winnerSlot, unsigned int fromPeer, int from, int waited)
{
    if (cooplo::RevokeWhen(EngineWritesBlocked() ? 1 : 0) == cooplo::kRvNowDefer)
    {   /* [a1b2f1-hd12] [review F4]: never dropped - it WAITS for the next safe point (HandoffTick with writes unblocked); the newest per uid */
        RelRevokeWait w; w.uid = uid; w.wgen = wgen; w.winnerSlot = winnerSlot; w.fromPeer = fromPeer; w.from = from;
        size_t i = 0;
        for (; i < g_relRevokeWait.size(); ++i) if (g_relRevokeWait[i].uid == uid) break;
        if (i < g_relRevokeWait.size()) { g_relRevokeWait[i] = w; return; }
        if (g_relRevokeWait.size() >= 4096) { g_relRevokeWait.erase(g_relRevokeWait.begin()); ++g_relRevokeCapDropped; }
        g_relRevokeWait.push_back(w); ++g_relInRevokeWaited;
        RelLog("[RELEASE] <- REVOKE uid=" + N((long long)uid) + " from " + SlotText(from) + " WAITS - engine writes blocked; applied at the next safe point (inRevokeWaited " + N(g_relInRevokeWaited) + ")");
        return;
    }
    if (waited != 0) ++g_relInRevokeWaitApplied;
    const int me = StoreMySlot();
    const int none = winnerSlot == cooplo::kRelNoWinner ? 1 : 0;
    const int act = cooplo::RevokeAction(net::IsUidMine(uid) ? 1 : 0, net::MineGenOf(uid), wgen, (none == 0 && me >= 0 && (int)winnerSlot == me) ? 1 : 0, none);
    if (act == cooplo::kRvIgnore)
    {
        ++g_relInRevokeIgnored;
        RelLog("[RELEASE] <- REVOKE uid=" + N((long long)uid) + " from " + SlotText(from) + " IGNORED - not run here, this game is the winner, or it holds a newer gen");
        return;
    }
    ::Character* c = FindSpawned(uid);
    if (act == cooplo::kRvPuppet)
    {
        net::ReleaseLocalOwner(uid, cooplive::RelayPeerId(winnerSlot), wgen);
        WorldStateOnOwnershipReleased(uid, c);
        PuppetIfBody(uid, c, "REVOKE");   /* [a1b2f1-hd13] [review F6] */
        ++g_relInPuppeted;
        RelLog("[RELEASE] <- REVOKE uid=" + N((long long)uid) + " from " + SlotText(from) + " -> PUPPET of " + SlotText((int)winnerSlot) + " at gen " + N((long long)wgen) + " (releaseIn puppeted " + N(g_relInPuppeted) + ")");
        return;
    }
    net::ReleaseLocalOwner(uid, fromPeer, wgen);
    WorldStateOnOwnershipReleased(uid, c);
    ApplyRemoteUnload(uid, twinsleep::kKindNeverTake);   /* the giver keeps it asleep in its world data: a save-born twin here is put to sleep, taken only as the last resort */
    net::ForgetCopyRecord(uid);
    ++g_relInRemoved;
    RelLog("[RELEASE] <- REVOKE uid=" + N((long long)uid) + " from " + SlotText(from) + " -> REMOVED (no game runs it: it sleeps in the giver's world data) (releaseIn removed " + N(g_relInRemoved) + ")");
}
static void RelApplyRevoke(const cooplo::ReleaseMsg& msg, unsigned int fromPeer, int from)
{
    for (size_t i = 0; i < msg.rows.size(); ++i) { ++g_relInRevokes; RelApplyRevokeRow(msg.rows[i].uid, msg.rows[i].gen, msg.winnerSlot, fromPeer, from, 0); }
}
/* [a1b2f1-hd12] [review F4]: the safe point - HandoffTick, main thread, engine writes not blocked: the REVOKEs that waited, applied */
static void RelRevokeWaitDrain()
{
    if (g_relRevokeWait.empty() || EngineWritesBlocked()) return;
    std::vector<RelRevokeWait> w; w.swap(g_relRevokeWait);
    for (size_t i = 0; i < w.size(); ++i) RelApplyRevokeRow(w[i].uid, w[i].wgen, w[i].winnerSlot, w[i].fromPeer, w[i].from, 1);
}
/* [a1b2f1-hd6] [review F5]: 1 Hz - each indexed squad's live people run here (liveHere) and their keys (g_keyCache); kept notes of squads
   never indexed are pruned after kRelDoneKeepSec */
static void IdxLiveScan(double now)
{
    for (std::map<HoKey, KeptRecent>::iterator r = g_keptRecent.begin(); r != g_keptRecent.end(); )
    {
        if (now - r->second.at > kRelDoneKeepSec || now < r->second.at)
        {
            for (std::set<unsigned int>::const_iterator u = r->second.uids.begin(); u != r->second.uids.end(); ++u) g_keptHandOf.erase(*u);   /* T-1318 a5 fold 1 */
            g_keptRecent.erase(r++);
        }
        else ++r;
    }
    for (std::map<HoKey, SquadIdx>::iterator s = g_squadIdx.begin(); s != g_squadIdx.end(); ++s) s->second.liveHere = 0;
    std::map<unsigned int, HoKey> cache;
    if (!g_squadIdx.empty())
    {
        ++g_idxScans;
        const int cap = MirrorCapacity();
        for (int i = 0; i < cap; ++i)
        {
            unsigned int uid = 0; ::Character* c = 0;
            if (!MirrorSlot(i, &uid, &c) || !net::IsUidMine(uid) || !Plaus(c)) continue;
            void* f = 0; std::string id;
            if (!SquadKeyOf(c, &f, &id)) continue;
            std::map<HoKey, SquadIdx>::iterator s = g_squadIdx.find(HoKey(f, id));
            if (s == g_squadIdx.end()) continue;
            ++s->second.liveHere; cache[uid] = s->first;
        }
    }
    g_keyCache.swap(cache);
}
/* 2.5 item 3, the candidate: AdoptDecide per member - adopt (the XFER take path), drop (this game's copy goes now and its record), stale
   (refused, the copy kept), defer (link down / writes blocked); one RELEASE_ACK. MAIN THREAD (the drain). */
void ApplyRemoteRelease(const cooplo::ReleaseMsg& msg, unsigned int fromPeer)
{
    ReleaseForgetDrain();
    RelWalk walk;   /* [a1b2f1-hd10] [review F1] */
    const int from = net::PeerSlotOfKey(fromPeer);
    if (msg.flags == cooplo::kRelFlagRevoke) { RelApplyRevoke(msg, fromPeer, from); return; }   /* [a1b2f1-hd16] [review F6]: the decoder admits exactly PUT_AWAY or REVOKE */
    ++g_relInOffers;
    const int writesBlocked = EngineWritesBlocked() ? 1 : 0, linkUp = StoreLiveReady() ? 1 : 0;
    std::vector<net::XferMember> mm(msg.rows.size());
    const unsigned int leader = msg.keyUid;
    int leaderTaking = 0; float tlx = 0, tlz = 0;
    for (size_t i = 0; i < msg.rows.size(); ++i)
    {
        std::memcpy(&mm[i], msg.rows[i].body, sizeof(net::XferMember)); mm[i].uid = msg.rows[i].uid;
        if (mm[i].uid == leader && leaderTaking == 0) { leaderTaking = 1; tlx = mm[i].x; tlz = mm[i].z; }
    }
    const int leaderHere = (net::IsUidMine(leader) || leaderTaking != 0) ? 1 : 0;
    int squadKnown = 0; float sqx = 0, sqz = 0;
    if (leaderHere != 0)
    {
        ::Character* lc = FindSpawned(leader);
        if (net::IsUidMine(leader) && Plaus(lc) && SquadDecisionPos(lc, &sqx, &sqz) == 1) squadKnown = 1;
        else if (leaderTaking != 0) { sqx = tlx; sqz = tlz; squadKnown = 1; }
    }
    const int squadInRing = (squadKnown != 0 && KeepTakeAt(sqx, sqz) == 1) ? 1 : 0;   /* this game's engine keeps the squad at its spot (live), not the once-a-second loaded list */
    cooplo::ReleaseAckMsg ack; ack.id = msg.id;
    int reRun = 0; long long stale = 0;
    const int awakeBlock = RaidAwakeBlockHas(leader); int awakeSkipped = 0;   /* T-1122 awake hand: the squad's raid came first - no carried goal issued */
    /* T-1122 whole or none (owner 1581 a): every row judged first (no change made), then a lent squad's rows are all adopted or none */
    std::vector<int> verdict(mm.size(), (int)cooplo::kDrop), haveRow(mm.size(), 0);
    int nAdoptable = 0, nDropLike = 0, nDup = 0;   /* nDup (T-1122 lent flag): rows this game already runs - ACKed adopted under any judge */
    for (size_t i = 0; i < mm.size(); ++i)
    {
        const unsigned int uid = mm[i].uid;
        ::Character* c = FindSpawned(uid);
        const int mine = net::IsUidMine(uid) ? 1 : 0;
        const int have = (mine == 0 && Plaus(c)) ? 1 : 0;
        const unsigned int myGen = mine != 0 ? net::MineGenOf(uid) : net::CopyGenOf(uid);
        const int memberInRing = KeepTakeAt(mm[i].x, mm[i].z) == 1 ? 1 : 0;   /* the live keep read */
        const int takeRing = coopsquad::XferTakeMember(leaderHere, squadKnown, squadInRing, memberInRing);
        /* T-650 fold 1 (manager decision on review L2): no corner refusal - takeRing reads this game's engine-loaded list, which is the
           whole answer to "can this game run it"; a corner sector it has loaded is taken like any other (no corner, keepMargin 0) */
        const int v = cooplo::AdoptDecide(have, net::IsRecordedOwner(uid, fromPeer) ? 1 : 0, mine, myGen, msg.rows[i].gen, takeRing, 0, linkUp, writesBlocked, 0);
        verdict[i] = v; haveRow[i] = have;
        if (v == cooplo::kAdopt || v == cooplo::kAdoptDup) ++nAdoptable; else if (v != cooplo::kDefer) ++nDropLike;
        if (v == cooplo::kAdoptDup) ++nDup;
    }
    const int lentWhole = cooplo::ReleaseLentWhole(msg.flags);   /* T-1122 lent flag (protocol 224): the offer itself says "whole or none" */
    const int worldWhole = cooplo::ReleaseWorldWhole(msg.flags);   /* T-1318 attempt 2 (D3, protocol 226): a world squad's offer - whole or none */
    const int judge = cooplo::WholeJudgeOf((lentWhole != 0 ? (int)cooplo::kWholeBitLent : 0) | (worldWhole != 0 ? (int)cooplo::kWholeBitWorld : 0), awakeBlock != 0 ? 1 : 0);
    int wholeRefused = 0;   /* T-1122 whole or none: 1 - the adoptable rows go back dropped (a row was dropped); 2 - deferred (only defers stood in the way) */
    {
        const int tw = raidsq::LentTakeWhole(judge != cooplo::kWjEach ? 1 : 0, nAdoptable, (int)mm.size());   /* T-1122 lent judge (flag or block) / T-1318 D3 world bit: all rows or none */
        if (tw == raidsq::kTwAll) { if (judge == cooplo::kWjLent) RaidLentWholeNote(raidsq::kLwRelAll); else ++g_wwRelAll; }   /* the lent counters keep counting lent squads only */
        if (tw == raidsq::kTwNone)
        {
            wholeRefused = nDropLike > 0 ? 1 : 2;
            if (judge == cooplo::kWjLent) RaidLentWholeNote(raidsq::kLwRelNone); else { ++g_wwRelNone; if (leaderHere == 0) ++g_wwRelNoneLeaderless; }   /* T-1318 D3: a refused batch without its leader counted apart (read-T1318-spread risk b) */
            RelLog("[RELEASE] <- id=" + N((long long)msg.id) + (judge == cooplo::kWjLent ? " lent squad" : " world squad") + " key=" + N((long long)leader) + (leaderHere != 0 ? std::string() : std::string(" (its leader not in the batch)")) + " worldWhole=" + N((long long)worldWhole) + " WHOLE OR NONE: " + N((long long)nAdoptable) + " of " + N((long long)mm.size())
                   + " row(s) adoptable here (lentWhole=" + N((long long)lentWhole) + " block=" + N((long long)(awakeBlock != 0 ? 1 : 0)) + ") - NONE adopted; " + N((long long)nDup) + " already run here ACKed adopted - this game's (" + (wholeRefused == 1 ? std::string("all dropped: the giver tries the next candidate or keeps them asleep") : std::string("all deferred")) + ")"
                   + (judge == cooplo::kWjWorld ? " (worldWhole relNone=" + N(g_wwRelNone) + " leaderless=" + N(g_wwRelNoneLeaderless) + ")" : std::string()));
        }
    }
    for (size_t i = 0; i < mm.size(); ++i)
    {
        const unsigned int uid = mm[i].uid;
        const int have = haveRow[i];
        int v = verdict[i];
        if (v == cooplo::kAdopt && wholeRefused != 0) v = wholeRefused == 1 ? (int)cooplo::kDrop : (int)cooplo::kDefer;   /* T-1122 whole or none */
        if (v == cooplo::kAdopt) { TakeMemberFromPeer(mm[i], msg.rows[i].gen, &reRun, awakeBlock, &awakeSkipped); ack.adopted.push_back(uid); ++g_relInAdopted; continue; }
        if (v == cooplo::kAdoptDup)
        {
            if (net::MineGenOf(uid) < msg.rows[i].gen) net::TakeLocalOwner(uid, msg.rows[i].gen);   /* a repeat (or a dual run): run at the offer's gen */
            ack.adopted.push_back(uid); ++g_relInDup; continue;
        }
        if (v == cooplo::kDefer) { ack.deferred.push_back(uid); ++g_relInDeferred; continue; }
        ack.dropped.push_back(uid);
        if (v == cooplo::kStale) { ++g_relInStale; ++stale; continue; }   /* refused - the copy is another game's (or newer): kept */
        ++g_relInDropped;
        if (have != 0) { ApplyRemoteUnload(uid); net::ForgetCopyRecord(uid); }   /* dropped: this game's copy goes now */
    }
    std::vector<char> b;
    const bool sent = cooplo::ReleaseAckEncode(&b, ack) && net::SendReleaseAck(fromPeer, b);
    if (!ack.adopted.empty() && wholeRefused == 0) RaidAwakeTakeNote(leader, ack.adopted.size() == mm.size() ? 1 : 0);   /* T-1122 fix A re-check fold (2): the take's fullness, block or not; review LOW-B: not under a whole refusal (no row was adopted) */
    if (awakeBlock != 0 && !ack.adopted.empty() && wholeRefused == 0)   /* review LOW-B: a whole refusal starts no campaign here */
    {   /* T-1122 awake hand: the squad came with its raid - the block applied to the squad adopted here (its campaign starts) */
        int mine = 0; void* ap = HandoffMySquadApOf(leader, &mine);
        RaidAwakeTaken(leader, ap, ack.adopted.size() == mm.size() ? 1 : 0);   /* T-1122 fix A review fold 1: every row of the offer adopted */
        DebugLog("[XFER] <- awake raid squad key=" + N(leader) + " by RELEASE id=" + N((long long)msg.id) + " adopted=" + N((long long)ack.adopted.size()) + " - the carried order skipped for "
                 + N((long long)awakeSkipped) + " member(s) (its raid's plan came with it; runs the key " + N((long long)mine) + ")");
    }
    RelLog("[RELEASE] <- id=" + N((long long)msg.id) + " from " + SlotText(from) + " n=" + N((long long)mm.size()) + " lentWhole=" + N((long long)lentWhole) + " worldWhole=" + N((long long)worldWhole) + " alreadyHere=" + N((long long)nDup) + " adopted=" + N((long long)ack.adopted.size())
           + " dropped=" + N((long long)ack.dropped.size()) + " (stale " + N(stale) + ") deferred=" + N((long long)ack.deferred.size()) + " carriedNotIssued=" + N((long long)reRun)
           + (sent ? std::string() : std::string(" - ACK NOT SENT (no road; the giver resends)")));
}

/* 1. resend / abandon pending transfers (recurrence-covered: nothing is consumed until the ACK). T-1 B3 fold (L3): run at the END of
   a pass that ran its squads, each gated like a first send (coopsquad::ResendMayGo over HandoverXferMayGo: the squad's cats
   announcement went, or is unchanged) - after a reconnect the re-announcements precede the resends on the one ordered channel.
   A pass that did not run its squads (no player sector) resends nothing; nothing is abandoned by waiting (sends count, not time). */
static void P119KeepSample(const std::map<void*, std::vector<unsigned int> >& squads, double now);   // PROBE P119
/* T-1467 (owner 1863 a / 1864 a / 1865 a; src/common/carryhand.h): A CARRIED BODY HANDED, ALONE, TO ITS CARRIER'S GAME. The body's game's
   state of each carry's hand-over (body uid -> carryhand::kHand*; goes when the carry ends here, HandoffCarryForget), the carrier's game's
   refusals (body uid -> the carrier that held it, 0 = whichever; go when that carry ends, HandoffCarryRefusedSweep) and the carriers the
   announce pass is holding (their first hold logged). MAIN THREAD, all of it. */
static std::map<unsigned int, int> g_carryHand;
static std::map<unsigned int, unsigned int> g_carryRefusedHere;
static std::set<unsigned int> g_carryHolding;
static std::map<unsigned int, unsigned int> g_carryTakenBy;   /* the carrier's game: body uid taken by a carry hand-over -> the carrier that held it */
static long long g_carryKeptOutOfPass = 0, g_carryTakenEnded = 0;
static long long g_carryOffered = 0, g_carryGiven = 0, g_carryRefused = 0, g_carryAbandoned = 0, g_carryTakenHere = 0, g_carryRefusedHereN = 0;
static long long g_carryHeld = 0, g_carryHoldStarts = 0, g_carryHoldEnds = 0, g_carryHandLines = 0;
static const long long kCarryHandLineCap = 200;
static void CarryHandLine(const std::string& s) { if (g_carryHandLines >= kCarryHandLineCap) return; ++g_carryHandLines; DebugLog(s); }
/* the body's game: the carrier's game's answer (or none) settles this carry's hand-over */
static void CarryHandSettle(unsigned int body, int taken, const char* why)
{
    std::map<unsigned int, int>::iterator h = g_carryHand.find(body);
    if (h == g_carryHand.end()) return;
    h->second = carryhand::HandSettle(taken);
    if (taken != 0) ++g_carryGiven; else ++g_carryRefused;
    CarryHandLine("[CARRYHAND] body uid=" + N(body) + " " + std::string(carryhand::HandStateName(h->second)) + " - " + why
                  + " (carryHand offered " + N(g_carryOffered) + " given " + N(g_carryGiven) + " refused " + N(g_carryRefused) + " abandoned " + N(g_carryAbandoned) + ")");
}
int HandoffCarryState(unsigned int body)
{
    std::map<unsigned int, int>::const_iterator h = g_carryHand.find(body);
    return h == g_carryHand.end() ? (int)carryhand::kHandNone : h->second;
}
void HandoffCarryForget(unsigned int body) { g_carryHand.erase(body); }
/* the carrier's game: a body taken by a carry hand-over and still carried by the carrier that held it at the take (carryhand::TakenKeep, read
   live) - left out of the squad pass; once that carry ends the entry goes and the squad's ordinary roads judge the body again */
static bool CarryTakenStillCarried(unsigned int uid)
{
    if (g_carryTakenBy.empty()) return false;
    std::map<unsigned int, unsigned int>::iterator t = g_carryTakenBy.find(uid);
    if (t == g_carryTakenBy.end()) return false;
    const unsigned int now = CarrierOfBodyHere(uid);
    if (carryhand::TakenKeep(net::IsUidMine(uid) ? 1 : 0, (now != 0 && now == t->second) ? 1 : 0, net::IsUidMine(t->second) ? 1 : 0) != 0) return true;
    ++g_carryTakenEnded;
    CarryHandLine("[CARRYHAND] body uid=" + N(uid) + " taken by carry - its carry by carrier uid=" + N(t->second) + " ended (" + std::string(now == 0 ? "carried by nobody here" : "another carrier") + ", run here "
                  + N((long long)(net::IsUidMine(uid) ? 1 : 0)) + "): its squad's ordinary roads judge it again (carryTakenEnded " + N(g_carryTakenEnded) + ")");
    g_carryTakenBy.erase(t);
    return false;
}
int HandoffCarryRefusedHere(unsigned int body, unsigned int carrier)
{
    if (body == 0) return 0;
    std::map<unsigned int, unsigned int>::const_iterator r = g_carryRefusedHere.find(body);
    return (r != g_carryRefusedHere.end() && (r->second == 0 || r->second == carrier)) ? 1 : 0;
}
void HandoffCarryRefusedSweep()
{
    for (std::map<unsigned int, unsigned int>::iterator r = g_carryRefusedHere.begin(); r != g_carryRefusedHere.end(); )
    {
        const unsigned int now = CarrierOfBodyHere(r->first);
        if (now == 0 || (r->second != 0 && now != r->second)) g_carryRefusedHere.erase(r++); else ++r;   /* that carry ended: a later carry is judged afresh */
    }
    for (std::map<unsigned int, unsigned int>::iterator t = g_carryTakenBy.begin(); t != g_carryTakenBy.end(); )
        if (FindSpawned(t->first) == 0) g_carryTakenBy.erase(t++); else ++t;   /* fold LOW: the body is gone here - its after-take entry goes */
    for (std::set<unsigned int>::iterator h = g_carryHolding.begin(); h != g_carryHolding.end(); )
    {
        const unsigned int body = CarryingUidOf(*h);
        const int ended = (body == 0) ? 1 : (net::IsUidMine(body) ? 2 : (HandoffCarryRefusedHere(body, *h) != 0 ? 3 : 0));
        if (ended != 0)
        {
            ++g_carryHoldEnds;
            CarryHandLine("[CARRYHAND] hold ENDS carrier uid=" + N(*h) + " - " + std::string(ended == 1 ? "it carries nothing now" : (ended == 2 ? "the body it carries is this game's now (handed over)" : "this game refused the body's hand-over"))
                          + " (carryHoldEnds " + N(g_carryHoldEnds) + ")");
            g_carryHolding.erase(h++);
        }
        else ++h;
    }
}
void HandoffCarryHoldNote(unsigned int carrier, unsigned int body)
{
    ++g_carryHeld;
    if (!g_carryHolding.insert(carrier).second) return;
    ++g_carryHoldStarts;
    CarryHandLine("[CARRYHAND] hold carrier uid=" + N(carrier) + " body uid=" + N(body) + " (run by slot " + N((long long)net::OwnerSlotOf(body)) + ") - no other game has the carrier's area loaded,"
                  " but it carries a body another game runs: its UNLOAD waits for the body's hand-over (carryHoldStarts " + N(g_carryHoldStarts) + ")");
}

static void HandoffResends(double now, const std::map<void*, int>& gate)
{
    for (std::map<unsigned int, Pending>::iterator it = g_pending.begin(); it != g_pending.end(); )
    {
        Pending& p = it->second;
        if (now - p.sentAt >= kResendSec)
        {
            if (p.sends >= kMaxResends) { ++g_xferAbandoned; DebugLog("[XFER] ABANDONED leader=" + N(it->first) + " after " + N(p.sends) + " sends - no ACK");
                const Pending flown(p); const unsigned int flownKey = it->first;   /* [a1b2-hd5] [a1b2-hd15]: opened AFTER the erase - an UNLOAD settled at once must not be held by this flight */
                std::vector<unsigned int> ab; for (size_t k = 0; k < p.members.size(); ++k) ab.push_back(p.members[k].uid);
                g_pending.erase(it++); ++g_hoOpenSeq;   /* T-1318 fold 2: the flight ended */
                ReleaseOpenFromFlight(flownKey, flown, std::vector<unsigned int>(), 1);   /* design 2.5 item 1: members put away while it flew go to a release */
                for (size_t k = 0; k < ab.size(); ++k) WorldsyncCatchupHandoverSettled(ab[k]);   /* M7a2 item 2 [m7a2-ho3]: the aborted hand-over keeps them here */
                if (flown.reason == (unsigned int)coopsquad::kXferReasonCarry) { ++g_carryAbandoned; CarryHandSettle(flownKey, 0, "the carrier's game never answered (ABANDONED after its resends): the carry goes on as before this change"); }   /* T-1467 */
                continue; }
            std::map<void*, int>::const_iterator g = gate.find(p.ap);
            const int seen = (g != gate.end()) ? 1 : 0;
            const int resendGo = coopsquad::ResendMayGo(1, seen, seen != 0 ? g->second : 1);
            /* T-1122 whole-or-none review fold MED-1 (b): a lent squad whose block does not go is not resent; counted as a send, so the flight
               ends by its ABANDON (the squad stays here, whole) instead of waiting for ever */
            const int lentGate = resendGo != 0 ? RaidAwakeHandSend(p.ap, 0, std::string(), it->first, p.targetSlot, p.awakeWhole) : 0;   /* T-1122 awake hand: the raid's block said again before the resend */
            if (resendGo != 0) p.lentWhole |= raidsq::LentWholeFlagOf(lentGate);   /* T-1122 lent flag: the resend says "whole or none" as its first send did */
            if (resendGo != 0 && raidsq::LentGateHolds(lentGate) != 0) { RaidLentWholeNote(raidsq::kLwSendHeld); ++p.sends; p.sentAt = now; }
            else if (resendGo != 0
                && net::SendXfer(it->first, p.reason, &p.members[0], (int)p.members.size(), p.targetSlot, p.lentWhole | p.worldWhole)) { ++p.sends;   /* [a1b1-hd5]: the first send's slot only */ ++g_xferResent; p.sentAt = now; }
        }
        ++it;
    }
}

/* T-1318 attempt 4 (owner 1626, design A, site 6): an XFER's runHere from the engine's own member list (cooplo::XferRunHereOf) - the people
   this game runs of the squad (run: the mirror's group, its corpses too) plus the list's living members outside them (never numbered, or
   this game's not in the run - a4 fold 1 HIGH-2: another game's copies are run there, not outside); a list over 64: -1 (never whole). Not a squad (a squad of one): the group's size, as before. *outside / *known:
   for the line. MAIN THREAD. */
static int XferRunHereFor(bool isSquad, const SquadView& sv, const std::vector<unsigned int>& run, int* outside, int* known)
{
    *outside = 0; *known = -1;
    if (!isSquad) return (int)run.size();
    const std::set<unsigned int> inRun(run.begin(), run.end());
    *outside = SquadUnaccountedIn(sv, &inRun, 0, known, 0);
    return cooplo::XferRunHereOf((int)run.size(), *outside, sv.count, sv.stored);
}
/* the capped line of an XFER pass the engine's list held back (worldWhole6 xferUnacc / followUnacc), one per squad and road */
static void XferUnaccLine(int follow, unsigned int key, const SquadView& sv, int built, int runHere, int outside, int known)
{
    if (g_unaccLines >= kUnaccLineCap || g_wwHeldLogged.size() >= 4096 || !g_wwHeldLogged.insert(std::make_pair(follow != 0 ? 5 : 4, key)).second) return;
    ++g_unaccLines;
    DebugLog(std::string("[XFER] ") + (follow != 0 ? "follow leader=" : "forced leader=") + N((long long)key) + " squad id='" + StoreWorldIdOf(sv.platoon) + "' NOT SENT (whole or none, T-1318 attempt 4 - the engine's member list): chars="
             + N((long long)sv.count) + " known=" + N((long long)known) + ", " + N((long long)built) + " people could go, " + N((long long)outside) + " living member(s) outside them (never numbered, or this game's and not sent; another game's people not counted - a4 fold 1)"
             + (runHere < 0 ? ", its list over 64" : "") + " - the squad stays here (worldWhole6 xferUnacc=" + N(g_wwXferUnacc) + " followUnacc=" + N(g_wwFollowUnacc) + "; once per squad)");
}
/* ---- T-1442 step 6 (F1480 design a): A SQUAD'S LEADER CHANGE GOES OUT WHEN THE ENGINE MAKES IT ------------------------------------
   The engine's writers note what they changed after their original returns (src/common/leadmark.h): set-leader 0x790C00 and
   remove-member 0x794C00 the squad (detour_leadSet / detour_leadRemove below), the knock-out calls (medical.cpp) and the death call
   (worldstate.cpp) the character (LeadNoteChar). Each frame, before HandoffTick's 1 s gate, LeadEventTick takes the notes, finds each
   one's squad among the squads HandoffTick's pass groups (one mirror walk, the pass's own grouping: a member this game runs, not a
   player's), and runs SquadPassOne - the pass's own per-squad function - on it: the announcement with live reads and, when it went out
   (or the other game's announcement for that squad arrived, LeadArrivalNote), the follow-leader hand-over decision. A noted squad
   stays watched for coopleadmark::kWatchSec: a knock-out's fall (the downed read) and the engine's acting-leader write (+0xA8, the
   getter 0x791FF0) can land frames after the call; AnnounceLead sends only on change, so a watched frame with nothing new sends
   nothing. The 1 s pass is the backstop: a send it makes is classified (coopleadmark::LeadBackstopKind) - missed = a leader change no
   note announced. Squads this game only copies are never found (no member run here) - the other game announces them; the mod's own
   set-leader call on a copy (SquadLeaderSafePointDrain) is not noted at all (its thread mark, g_leadOwnCallTid). */
enum { kSqPassTick = 0, kSqPassEvent = 1, kSqPassArrival = 2 };
static coopleadmark::LeadMarkTable g_leadMarks;   /* zero-initialised static: empty. ANY THREAD adds, the main thread takes */
struct LeadWatch { double since; int kind; unsigned long tick; };
static std::map<void*, LeadWatch> g_leadWatch;     /* squad (ActivePlatoon) -> its newest note, while its window holds. MAIN THREAD */
static std::set<unsigned int> g_leadArrivalUids;   /* uids an arrived announcement named since the last frame (LeadArrivalNote). MAIN THREAD */
static volatile LONG g_leadOwnCallTid = 0;         /* the thread inside the mod's own set-leader call on a copy squad */
static volatile LONG g_leadSetHook = 0, g_leadRemoveHook = 0, g_leadGetterHook = 0;   /* 0 not installed, 1 installed, -1 failed / no row */
static volatile LONG64 g_leadOwnCallSkipped = 0;   /* set-leader calls of the mod's own, not noted */
static volatile LONG g_leadDrainDue = 0;           /* an announcement arrived: SquadLeaderSafePointDrain runs at the next safe point */
static long long g_leadDrainEarly = 0;             /* drain runs an arrival brought forward of its 1 s throttle */
static long long g_leadEvNotRun = 0, g_leadEvRuns = 0, g_leadEvArrivalRuns = 0, g_leadEvSent = 0, g_leadEvDropped = 0;
static long long g_leadBackstop[coopleadmark::kBsKinds] = { 0, 0, 0, 0, 0 };
static long long g_leadEvSentBy[coopleadmark::kNoteKinds + 1] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };   /* review fold S3: event-road sends by the note that named the squad (+ arrival) */
static long long g_leadEvLines = 0, g_leadBsLines = 0, g_leadArrLines = 0;
static const long long kLeadEvLineCap = 60, kLeadBsLineCap = 30, kLeadArrLineCap = 60;

/* a send SquadPassOne's announcement made: the event road's capped line, or the backstop's classification */
static void LeadSentNote(void* ap, int mode, int noteKind, unsigned long noteTick, int prevSent, unsigned int prevActing, unsigned int prevFormal,
                         unsigned int acting, unsigned int formal, ::Character* formalChar)
{
    std::map<void*, MyLead>::const_iterator ml = g_myLead.find(ap);
    const unsigned int key = ml != g_myLead.end() ? ml->second.key : 0u;
    if (mode != kSqPassTick)
    {
        ++g_leadEvSent;
        ++g_leadEvSentBy[(noteKind >= 0 && noteKind < (int)coopleadmark::kNoteKinds) ? noteKind : (int)coopleadmark::kNoteKinds];
        if (g_leadEvLines >= kLeadEvLineCap) return;
        ++g_leadEvLines;
        const std::string why = noteKind >= 0 ? std::string(coopleadmark::NoteName(noteKind)) : std::string("arrival");
        const std::string ago = noteKind >= 0 ? ", " + N((long long)(DWORD)(::GetTickCount() - (DWORD)noteTick)) + " ms after the engine call" : std::string();
        DebugLog("[LEAD] event -> squad=" + N((long long)key) + " lead=" + N((long long)acting) + " formal=" + N((long long)formal) + " (" + why + ") sent" + ago
                 + " (was lead=" + N((long long)prevActing) + " formal=" + N((long long)prevFormal) + "; leadEvent sent " + N(g_leadEvSent) + ")");
        return;
    }
    const int watched = g_leadWatch.find(ap) != g_leadWatch.end() ? 1 : 0;
    const int k = coopleadmark::LeadBackstopKind(prevSent, prevActing, prevFormal, acting, formal, watched, (formalChar != 0 && !LeaderOut(formalChar)) ? 1 : 0);
    ++g_leadBackstop[k];
    if (k != coopleadmark::kBsMissed && k != coopleadmark::kBsWake && k != coopleadmark::kBsWatched) return;
    if (g_leadBsLines >= kLeadBsLineCap) return;
    ++g_leadBsLines;
    DebugLog("[LEAD] backstop -> squad=" + N((long long)key) + " lead " + N((long long)prevActing) + " -> " + N((long long)acting) + " formal " + N((long long)prevFormal) + " -> " + N((long long)formal)
             + (k == coopleadmark::kBsMissed ? " MISSED - no note announced it" : (k == coopleadmark::kBsWake ? " (wake: the formal leader stood up - no engine call to note)" : " (watched: its note's send was held back)"))
             + " (leadBackstop missed " + N(g_leadBackstop[coopleadmark::kBsMissed]) + ")");
}

/* HandoffTick's per-squad work, one squad: the announcement (with live reads), the follow-leader hand-over decision and, on the 1 s pass
   only, the position road (the forced hand-over). mode kSqPassTick = the 1 s pass; kSqPassEvent = a noted squad (its decision only when
   its announcement went out); kSqPassArrival = the other game's announcement for it arrived (the decision always). noteKind / noteTick:
   the event road's note (-1 none) for its line. leadSeen / xferGate / leadSends: the 1 s pass's own; the event road passes its own. */
static void SquadPassOne(void* ap, const std::vector<unsigned int>& run, bool solo, double now, const Sector& mine, int mode, int noteKind, unsigned long noteTick,
                         std::set<void*>* leadSeen, std::map<void*, int>* xferGate, int* leadSends)
{
    SquadView sv;
    const bool isSquad = !solo && ReadSquadAt(ap, &sv) == 1;
    ::Character* leader = (isSquad && Plaus(sv.leader)) ? sv.leader : 0;
    const unsigned int leaderUid = leader ? FindSpawnedUid(leader) : 0;
    const bool leaderMine = leaderUid != 0 && net::IsUidMine(leaderUid);
    /* T-1 B3 restructure (protocol 89): the squad's money rides this squad's announcement when this game runs its formal leader
       (KeeperPot: the leader's getOwnerships()->money). Point 3 (F1): a squad this game has just TAKEN is not announced (and not
       handed on) until SquadCatsSafePointDrain has written the other game's last announced cats into it - the next safe point. */
    int haveCats = 0, cats = 0, announceCurrent = 1;
    if (isSquad && leaderMine)
    {
        void* potOwn = 0;
        haveCats = (KeeperPotAnnounced((void*)leader, &potOwn, &cats) == 1) ? 1 : 0;   /* T-1 B3 fold (M2): net of this game's in-flight predictions */
        if (haveCats == 0) CatsPotUnread(leaderUid, 0);
        long long adoptDelta = 0; unsigned int adoptSeq = 0; int adoptCats = 0;
        if (haveCats != 0 && CatsAdoptAction(ap, leaderUid, &adoptDelta, &adoptSeq, &adoptCats) != coopsquad::kCatsNone)
        { leadSeen->insert(ap); (*xferGate)[ap] = 0; g_catsDue = 1; return; }
        /* T-799 D1: a pot whose money is not known here goes out with no money - the copies keep their value */
        if (haveCats != 0 && coopsquad::AnnounceCarriesMoney(1, KeeperMoneyKnown(potOwn)) == 0) { haveCats = 0; cats = 0; if (mode == kSqPassTick) ++g_catsWithheldNow; }
    }
    // T-1 B1 restructure (re-check N1-N5 of ff655c5): ONE AUTHORITY, ONE ANNOUNCED VALUE. This game's engine's acting leader of
    // the squad (EngineLeaderChoice: +0xA0 if standing, else +0xA8 - computed ONCE, here) is announced with the formal leader
    // (+0xA0) and the members this game runs (MSG_SQUAD_LEAD), and it is the very value this game decides with. The other
    // game's announced acting leader is used AS IS. Both games so feed AgreedSquadLeader the same pair (named beats none,
    // differing -> the lower uid), and only the game that does NOT run the agreed leader sends its members (follow-leader
    // XFER). An agreed leader neither game runs here is "leader unknown": the position rule. SquadFollowLeader's engine-only
    // fallback is gone - the announced value already names the engine's leader when the other game runs it.
    unsigned int followUid = 0;
    coopsquad::FollowAnnounceMark annNow;   /* T-1239: both announcements of this squad as they stand this pass */
    if (isSquad)
    {
        unsigned int uids[kSquadViewCap];
        for (int k = 0; k < sv.stored; ++k) uids[k] = Plaus(sv.members[k]) ? FindSpawnedUid(sv.members[k]) : 0u;
        int actingUsable = 0;
        for (int k = 0; k < sv.stored; ++k) if (Plaus(sv.acting) && sv.members[k] == sv.acting) { actingUsable = LeaderOut(sv.acting) ? 0 : 1; break; }
        const int choice = coopsquad::EngineLeaderChoice(leader != 0 ? 1 : 0, (leader != 0 && LeaderOut(leader)) ? 1 : 0, actingUsable);
        ::Character* eng = choice == coopsquad::kEngineLeadA0 ? leader : (choice == coopsquad::kEngineLeadA8 ? sv.acting : 0);
        if (choice == coopsquad::kEngineLeadA8 && mode == kSqPassTick) ++g_leaderActing;
        const unsigned int myLead = eng != 0 ? FindSpawnedUid(eng) : 0u;
        leadSeen->insert(ap);
        /* T-1442 step 6: what was last announced, so a send is told apart (the event road's line; the backstop's classification) */
        std::map<void*, MyLead>::const_iterator pv = g_myLead.find(ap);
        const int prevSent = (pv != g_myLead.end()) ? pv->second.sent : 0;
        const unsigned int prevActing = prevSent != 0 ? pv->second.acting : 0u, prevFormal = prevSent != 0 ? pv->second.formal : 0u;
        const int sendsBefore = *leadSends;
        announceCurrent = AnnounceLead(ap, myLead, leaderUid, run, haveCats, cats, leadSends);   /* T-1 B3 restructure: with the money */
        const int sentNow = (*leadSends > sendsBefore) ? 1 : 0;
        if (sentNow != 0) LeadSentNote(ap, mode, noteKind, noteTick, prevSent, prevActing, prevFormal, myLead, leaderUid, leader);
        /* T-1442 step 6: the event road decides the hand-over only when its announcement went out or the other game's arrived */
        if (mode != kSqPassTick && coopleadmark::LeadEventDecides(sentNow, mode == kSqPassArrival ? 1 : 0) == 0) return;
        const coopsquad::PeerLeadEntry* pe = PeerAnnouncedFor(sv, uids);
        const unsigned int peerLead = pe != 0 ? pe->acting : 0u;
        if (mode == kSqPassTick && myLead != 0 && peerLead != 0 && myLead != peerLead) ++g_agreedDiffered;
        followUid = coopsquad::AgreedSquadLeader(myLead, peerLead);
        if (pe != 0) { annNow.peerSender = pe->sender; annNow.peerSeq = pe->seq; }
        std::map<void*, MyLead>::const_iterator ml = g_myLead.find(ap);
        if (ml != g_myLead.end() && ml->second.sent != 0) { annNow.myKey = ml->second.key; annNow.mySeq = ml->second.seq; }
    }
    (*xferGate)[ap] = coopsquad::HandoverXferMayGo(haveCats, announceCurrent);   /* T-1 B3 fold (L3) */
    const bool followMine = followUid != 0 && net::IsUidMine(followUid);
    const bool followPeer = followUid != 0 && !followMine && HasPuppet(followUid);   // the other game runs it (H1: a live puppet here)
    std::map<unsigned int, double>::const_iterator bo = g_followBackoff.find(followUid);
    const int held = (followPeer && (g_pending.find(followUid) != g_pending.end() || bo != g_followBackoff.end())) ? 1 : 0;
    int act = coopsquad::FollowAction((followMine || followPeer) ? 1 : 0, followMine ? 1 : 0, held);
    /* T-1239 fix attempt 1: the game running that leader refused a member of this group as not in the leader's group on its engine -
       the leader is unknown for this group (the position rule below) until either game's announcement for it changes. */
    std::map<unsigned int, FollowGroupBlock>::iterator gb = followPeer ? g_followGroupBlock.find(followUid) : g_followGroupBlock.end();
    if (gb != g_followGroupBlock.end())
    {
        int refusedHere = 0;
        for (size_t k = 0; k < run.size() && refusedHere == 0; ++k) if (gb->second.members.count(run[k]) != 0) refusedHere = 1;
        if (refusedHere != 0 && coopsquad::FollowAnnounceSame(gb->second.mark, annNow) == 0)
        {
            ++g_followUnblocked;
            DebugLog("[XFER] follow leader=" + N(followUid) + " block LIFTED - an announcement for its group changed (other game " + N((long long)gb->second.mark.peerSeq) + " -> " + N((long long)annNow.peerSeq)
                     + ", this game " + N((long long)gb->second.mark.mySeq) + " -> " + N((long long)annNow.mySeq) + "); follow-leader may send again (followUnblocked " + N(g_followUnblocked) + ")");
            g_followGroupBlock.erase(gb);
        }
        else if (refusedHere != 0)
        {
            const int blocked = coopsquad::FollowActionBlocked(act, g_pending.find(followUid) == g_pending.end() ? 1 : 0);
            if (blocked != act) { if (mode == kSqPassTick) ++g_followBlockedPass; act = blocked; }
        }
    }
    if (act == coopsquad::kFollowWait) { if (mode == kSqPassTick) ++g_waitedLeader; return; }
    if (coopsquad::HandoverXferMayGo(haveCats, announceCurrent) == 0) return;   /* T-1 B3 restructure (F1): the squad's cats go out before its XFER - next second */
    if (act == coopsquad::kFollowSend)
    {
        Pending p; p.reason = coopsquad::kXferReasonFollowLeader; p.sentAt = now; p.sends = 1; p.ap = ap; p.logged = 0;
        p.mark = annNow;   /* T-1239: a 'not in the leader's group' refusal blocks until either announcement differs from this */
        BuildMembers(run, &p.members);
        const size_t followBuilt = p.members.size();
        PassHoldElsewhere(followUid, &p.members, "follow-leader");   /* M7a2 fold 3 [m7a2h-hp1] (T807 GAP 1) */
        p.awakeWhole = p.members.size() == followBuilt ? 1 : 0;   /* T-1122 re-check fold 5: a member held back - a held raid is not sent onward */
        const int followKept = KeptAsleepOfRun(run);   /* T-1318 attempt 3: people of the squad this game keeps asleep un-offered */
        int followOutside = 0, followKnown = -1;
        const int followRunHere = XferRunHereFor(isSquad, sv, run, &followOutside, &followKnown);   /* T-1318 attempt 4 (design A, site 6): the squad's living members, the engine's list */
        const int followGo = cooplo::WorldXferGo((int)p.members.size(), followRunHere, followKept);
        if (followGo != cooplo::kWxGo && !p.members.empty() && cooplo::WorldXferGo((int)p.members.size(), (int)run.size(), followKept) == cooplo::kWxGo)
        { ++g_wwFollowUnacc; XferUnaccLine(1, followUid, sv, (int)p.members.size(), followRunHere, followOutside, followKnown); }
        if (followGo != cooplo::kWxGo)
        {   /* T-1318 attempt 2 fold 1 (review HIGH-1 a): the follow-leader XFER also goes whole or not at all - every person this game runs of
               the squad, or none this pass (counted each pass; one line per squad, review LOW-1). Attempt 3 (owner 1615 a): "whole" also
               counts the people of the squad kept asleep here - with any, not sent this pass */
            if (!p.members.empty())
            {
                if (followGo == cooplo::kWxKeptAsleep) ++g_wwFollowKeptAsleep; else ++g_wwFollowHeldPart;
                if (g_wwLines < kWwLineCap && g_wwHeldLogged.size() < 4096 && g_wwHeldLogged.insert(std::make_pair(followGo == cooplo::kWxKeptAsleep ? 3 : 1, followUid)).second)
                {
                    ++g_wwLines;
                    DebugLog("[XFER] follow leader=" + N(followUid) + " NOT SENT (whole or none, T-1318 D3): " + N((long long)p.members.size()) + " of " + N((long long)run.size())
                             + " people could go, kept asleep here " + N((long long)followKept) + " - the squad stays here (worldWhole2 followHeldPart=" + N(g_wwFollowHeldPart)
                             + ", worldWhole3 followKeptAsleep=" + N(g_wwFollowKeptAsleep) + "; once per squad)");
                }
            }
            return;
        }
        p.worldWhole = cooplo::kWholeBitWorld;   /* T-1318 attempt 2 fold 1: "world squad - whole or none" on the follow-leader XFER too, kept over its resends */
        PendingKeysRead(&p);   /* [a1b2-hd8]: each member's squad key while whole - the ACK's index entry */
        if (coopsquad::PassSendAfterHold((int)p.members.size()) != coopsquad::kPassSendGo) return;
        p.targetSlot = net::OwnerSlotOf(followUid);   /* M7a A1 build 1 [a1b1-hd6]: the game that runs the leader */
        const int lentGate = RaidAwakeHandSend(ap, 0, std::string(), followUid, p.targetSlot, p.awakeWhole);   /* T-1122 awake hand: the raid's block first, on the same road */
        if (raidsq::LentGateHolds(lentGate) != 0) { RaidLentWholeNote(raidsq::kLwSendHeld); return; }   /* T-1122 whole or none: a lent squad not sent */
        p.lentWhole = raidsq::LentWholeFlagOf(lentGate);   /* T-1122 lent flag (protocol 224) */
        for (size_t p323i = 0; p323i < p.members.size(); ++p323i) P323AiLine(p.members[p323i].uid, "give-follow");   // PROBE P323
        if (!net::SendXfer(followUid, p.reason, &p.members[0], (int)p.members.size(), p.targetSlot, p.lentWhole | p.worldWhole)) return;
        ++g_xferOut; ++g_followedLeader; g_xferFollowSent += (long long)p.members.size();
        p.logged = FollowLogChanged(0, followUid, (long long)p.members.size()) ? 1 : 0;
        g_pending[followUid] = p;
        if (p.logged != 0)
        {
            const std::string q(1, (char)34);
            DebugLog("[XFER] -> squad leader=" + N(followUid) + " members=" + N((long long)p.members.size()) + " reason=follow-leader lentWhole=" + N((long long)p.lentWhole) + " worldWhole=" + N((long long)(p.worldWhole != 0 ? 1 : 0)));
            SW_DEVLOG("[VERDICT] {" + q + "ev" + q + ":" + q + "xfer_out" + q + "," + q + "leader" + q + ":" + N(followUid) + "," + q + "members" + q + ":" + N((long long)p.members.size())
                     + "," + q + "forced" + q + ":0," + q + "reason" + q + ":" + q + "follow-leader" + q + "," + q + "tick" + q + ":" + N(g_tick) + "}");
        }
        return;
    }
    if (mode != kSqPassTick) return;   /* T-1442 step 6: the position road (the forced hand-over) stays on the 1 s pass */
    if (act == coopsquad::kFollowByPosition && isSquad) ++g_leaderUnknownFellBack;
    // N4: the position path's XFER is keyed on a uid this game RUNS - the agreed leader when mine, else +0xA0 when mine, else the
    // first member run here - never on a character this game does not run.
    const unsigned int key = followMine ? followUid : (leaderMine ? leaderUid : run[0]);
    if (g_pending.find(key) != g_pending.end()) return;
    // T-1 B1 step 2: the SQUAD position decides (the engine sleeps the squad by it); a squad of one, or a squad whose
    // position is not usable yet, falls back to the leader's (else the first member's) own position.
    // T-164 B4-3 (M4): a trader squad with a home building by its HOME's position - the one the adoption decided by
    // (SquadDecisionPosAt), so the keeper is not handed away while its home, stock and restock stay here.
    float lx = 0, ly = 0, lz = 0;
    if (isSquad && SquadDecisionPosAt(sv, &lx, &lz) == 1) ly = sv.y;
    else
    {
        ::Character* pc = leader ? leader : FindSpawned(run[0]);
        if (!Plaus(pc) || !ReadPos(pc, &lx, &ly, &lz)) return;
    }
    const Sector s = SectorOf(lx, lz);
    bool forced = true; const char* reason = "FORCED";
    // Decision 15 / owner decision 580 (2026-10-09): ownership is STICKY - a squad stays with the game that runs it while this game's
    // engine keeps it at its spot (EngineKeepsHere). Only once it would not is it offered, and only to an IN_WORLD game the world server's
    // loaded map shows with the area loaded (the area's holder first when it is one of them, then the lower slot); the receiver's own
    // loaded-list rule decides the take. Nobody loaded there, or no fresh map: it stays, this game's engine puts it away and the
    // RELEASE decides. The wire's reason code is 1 (forced) here.
    /* T-650 fold 2 (T1056 F2): the question is whether THIS game's engine keeps the squad at its spot, read live (EngineKeepsHere: the
       engine's own +0xB1/+0xB0 flags for the area and its side neighbours) - not the once-a-second loaded list, which still named the
       area a second after the announce pass had withdrawn the people, and names areas whose edge strip the engine puts away.
       -1 (unreadable): this pass skips the squad; the next tick decides. */
    const int keepHere = EngineKeepsHere(lx, lz);
    if (keepHere < 0) { ++g_keepReadFailForced; return; }
    if (keepHere == 1) { g_keepNoted.erase(key); g_niwNoted.erase(key); return; }
    const int listedHere = SectorLoadedHere(s) ? 1 : 0;   /* the loaded list still names the area (the squad stands in its edge strip): logged for the run's check */
    std::vector<int> rcv; int holderSeen = -1, notInWorld = 0, notKeep = 0;
    const int nr = ReceiversFor(s, lx, lz, std::vector<int>(), &rcv, &holderSeen, &notInWorld, &notKeep);
    if (notInWorld > 0 && g_niwNoted.insert(key).second) ++g_holderNotInWorld;   /* a game with the area loaded but not IN_WORLD was passed over */
    if (nr <= 0)
    {
        if (g_keepNoted.insert(key).second)
        {
            if (nr == 0) ++g_holderKeep; else ++g_holderKeepUnknown;
            if (nr == 0 && notKeep > 0) ++g_keepNotKeep;
            RelLog("[RELEASE] forced XFER leader=" + N((long long)key) + " KEEP: this game's engine would not keep it at " + N((long long)lx) + "," + N((long long)lz) + " (sector " + SectorString(s)
                   + (listedHere != 0 ? ", still listed" : ", not listed") + "; player " + SectorString(mine) + ") but "
                   + (nr == 0 ? (notKeep > 0 ? "no other in-world game's engine would keep it there either (" + N((long long)notKeep) + " with the area loaded)" : std::string("no other in-world game has it loaded"))
                              : std::string("the area map is not fresh (or this game's link is down)"))
                   + " - it stays here; this game's engine puts it away and the RELEASE decides (holder " + N((long long)holderSeen) + ", loaded but not in the world "
                   + N((long long)notInWorld) + "; releaseHolder keep " + N(g_holderKeep) + " keepUnknown " + N(g_holderKeepUnknown) + "; once per squad)");
        }
        return;
    }
    const int target = rcv[0];
    std::map<unsigned int, ForcedTry>::iterator ftry = g_forcedTry.find(key);
    if (ftry != g_forcedTry.end() && ftry->second.refused != 0)
    {
        if (ftry->second.target == target && ftry->second.x == s.x && ftry->second.y == s.y) return;   /* review MED-1: backed off - that receiver refused it in this sector */
        g_forcedTry.erase(ftry);   /* the squad's sector or its receiver changed: offered again */
    }
    Pending p; p.reason = coopsquad::kXferReasonForced; p.sentAt = now; p.sends = 1; p.ap = ap; p.logged = 1;
    BuildMembers(run, &p.members);
    const size_t forcedBuilt = p.members.size();
    PassHoldElsewhere(key, &p.members, "forced");   /* M7a2 fold 3 [m7a2h-hp2] (T807 GAP 1) */
    p.awakeWhole = p.members.size() == forcedBuilt ? 1 : 0;   /* T-1122 re-check fold 5 */
    const int forcedKept = KeptAsleepOfRun(run);   /* T-1318 attempt 3: people of the squad this game keeps asleep un-offered */
    int forcedOutside = 0, forcedKnown = -1;
    const int forcedRunHere = XferRunHereFor(isSquad, sv, run, &forcedOutside, &forcedKnown);   /* T-1318 attempt 4 (design A, site 6): the squad's living members, the engine's list */
    const int forcedGo = cooplo::WorldXferGo((int)p.members.size(), forcedRunHere, forcedKept);
    if (forcedGo != cooplo::kWxGo && !p.members.empty() && cooplo::WorldXferGo((int)p.members.size(), (int)run.size(), forcedKept) == cooplo::kWxGo)
    { ++g_wwXferUnacc; XferUnaccLine(0, key, sv, (int)p.members.size(), forcedRunHere, forcedOutside, forcedKnown); }
    if (forcedGo != cooplo::kWxGo)
    {   /* T-1318 attempt 2 (D3): a world squad goes whole or not at all - a member held back (or over the 64 cap): not sent this pass; the
           squad stays here, and its engine's put-away offers it whole (RELEASE) or a later pass sends it whole. Attempt 3 (owner 1615 a;
           T2324 Traders Guild_1: 7 awake sent as "whole" while 5 were kept asleep here): "whole" also counts the people of the squad kept
           asleep here - with any, not sent this pass */
        if (!p.members.empty()) { if (forcedGo == cooplo::kWxKeptAsleep) ++g_wwXferKeptAsleep; else ++g_wwXferHeldPart; }   /* each pass */
        if (!p.members.empty() && g_wwLines < kWwLineCap && g_wwHeldLogged.size() < 4096 && g_wwHeldLogged.insert(std::make_pair(forcedGo == cooplo::kWxKeptAsleep ? 2 : 0, key)).second)   /* fold 1 (review LOW-1): one line per squad */
        {
            ++g_wwLines;
            DebugLog("[XFER] forced leader=" + N(key) + " NOT SENT (whole or none, T-1318 D3): " + N((long long)p.members.size()) + " of " + N((long long)run.size())
                     + " people could go, kept asleep here " + N((long long)forcedKept) + " - the squad stays here (worldWhole xferHeldPart=" + N(g_wwXferHeldPart)
                     + ", worldWhole3 xferKeptAsleep=" + N(g_wwXferKeptAsleep) + "; once per squad)");
        }
        return;
    }
    p.worldWhole = cooplo::kWholeBitWorld;   /* T-1318 attempt 2 (D3, protocol 226): "world squad - whole or none" on the wire, kept over its resends */
    PendingKeysRead(&p);   /* [a1b2-hd8] */
    if (coopsquad::PassSendAfterHold((int)p.members.size()) != coopsquad::kPassSendGo) return;
    p.targetSlot = target;   /* the first game with the area loaded (the holder first) */
    const int lentGate = RaidAwakeHandSend(ap, 0, std::string(), key, p.targetSlot, p.awakeWhole);   /* T-1122 awake hand: the raid's block first, on the same road */
    if (raidsq::LentGateHolds(lentGate) != 0) { RaidLentWholeNote(raidsq::kLwSendHeld); return; }   /* T-1122 whole or none: a lent squad not sent */
    p.lentWhole = raidsq::LentWholeFlagOf(lentGate);   /* T-1122 lent flag (protocol 224) */
    for (size_t p323i = 0; p323i < p.members.size(); ++p323i) P323AiLine(p.members[p323i].uid, "give-forced");   // PROBE P323
    if (!net::SendXfer(key, p.reason, &p.members[0], (int)p.members.size(), p.targetSlot, p.lentWhole | p.worldWhole)) return;
    ++g_xferOut; ++g_holderLeave; if (listedHere != 0) ++g_keepForcedListed;
    g_pending[key] = p;
    { ForcedTry f; f.target = target; f.x = s.x; f.y = s.y; f.refused = 0; g_forcedTry[key] = f; }   /* its ACK says whether to back off */
    const std::string q(1, (char)34);
    DebugLog("[XFER] -> squad leader=" + N(key) + " members=" + N((long long)p.members.size()) + " sector=" + SectorString(s) + " (my player sector " + SectorString(mine) + ") " + reason + " to slot " + N((long long)target) + " keepHere=0 listed=" + N((long long)listedHere) + " lentWhole=" + N((long long)p.lentWhole) + " worldWhole=" + N((long long)(p.worldWhole != 0 ? 1 : 0)));
    SW_DEVLOG("[VERDICT] {" + q + "ev" + q + ":" + q + "xfer_out" + q + "," + q + "leader" + q + ":" + N(key) + "," + q + "members" + q + ":" + N((long long)p.members.size())
             + "," + q + "sector" + q + ":" + q + SectorString(s) + q + "," + q + "forced" + q + ":" + (forced ? "1" : "0") + "," + q + "reason" + q + ":" + q + reason + q + "," + q + "tick" + q + ":" + N(g_tick) + "}");
}

/* MAIN THREAD, every frame (HandoffTick, before its 1 s gate) */
static void LeadEventTick()
{
    const double now = NowSec();
    if (EngineWritesBlocked())
    {   /* the world is going: its squads with it */
        g_leadEvDropped += (long long)coopleadmark::LeadMarkReset(&g_leadMarks);
        g_leadWatch.clear(); g_leadArrivalUids.clear();
        return;
    }
    unsigned long long addrs[coopleadmark::kLeadMarkSlots]; int kinds[coopleadmark::kLeadMarkSlots]; unsigned long ticks[coopleadmark::kLeadMarkSlots];
    const int n = coopleadmark::LeadMarkTake(&g_leadMarks, addrs, kinds, ticks, (int)coopleadmark::kLeadMarkSlots);
    for (std::map<void*, LeadWatch>::iterator w = g_leadWatch.begin(); w != g_leadWatch.end(); ) { if (coopleadmark::LeadWatchHolds(w->second.since, now) == 0) g_leadWatch.erase(w++); else ++w; }
    if (n == 0 && g_leadWatch.empty() && g_leadArrivalUids.empty()) return;   /* the every-frame fast path */
    const Sector mine = MyPlayerSector();
    if (mine.x < 0) { g_leadEvNotRun += (long long)n; g_leadWatch.clear(); g_leadArrivalUids.clear(); return; }   /* HandoffTick's own condition: no squad is grouped */
    LeadLinkEdge();
    std::map<void*, LeadWatch> noted;   /* this frame's notes: a squad's own, or a noted character's squad (found by the walk below) */
    std::map<void*, LeadWatch> charNoted;
    for (int i = 0; i < n; ++i)
    {
        LeadWatch lw; lw.since = now; lw.kind = kinds[i]; lw.tick = ticks[i];
        std::map<void*, LeadWatch>& into = coopleadmark::NoteIsSquad(kinds[i]) != 0 ? noted : charNoted;
        if (into.find((void*)(uintptr_t)addrs[i]) == into.end()) into[(void*)(uintptr_t)addrs[i]] = lw;   /* the first note of a frame names it */
    }
    /* ONE mirror walk: HandoffTick's grouping (this game's characters, not a player's, by squad - a character with no squad has no leader to
       announce), each noted character's squad (an address compare, then its squad read as the pass reads it) and each arrived uid's squad */
    std::map<void*, std::vector<unsigned int> > squads;
    std::set<void*> arrivalAps;
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int uid = 0; ::Character* c = 0;
        if (!MirrorSlot(i, &uid, &c) || !Plaus(c)) continue;
        const bool mineRun = net::IsUidMine(uid) && !coop::IsPlayerFaction(c->getOwnerFactionDirect()) && !CarryTakenStillCarried(uid);   /* T-1467: a body taken by carry, still carried here */
        const std::map<void*, LeadWatch>::iterator cn = charNoted.empty() ? charNoted.end() : charNoted.find((void*)c);
        const bool arrived = !g_leadArrivalUids.empty() && g_leadArrivalUids.count(uid) != 0;
        if (!mineRun && cn == charNoted.end() && !arrived) continue;
        void* ap = SquadActivePlatoonOf(c);
        if (ap == 0) continue;
        if (mineRun) squads[ap].push_back(uid);
        if (cn != charNoted.end()) { if (noted.find(ap) == noted.end()) noted[ap] = cn->second; charNoted.erase(cn); }
        if (arrived) arrivalAps.insert(ap);
    }
    g_leadArrivalUids.clear();
    g_leadEvNotRun += (long long)charNoted.size();   /* a noted character not in the mirror (never numbered) or with no squad */
    for (std::map<void*, LeadWatch>::const_iterator t = noted.begin(); t != noted.end(); ++t)
    {
        if (coopleadmark::LeadMarkVerdict(0, squads.find(t->first) != squads.end() ? 1 : 0) != coopleadmark::kMarkGo) { ++g_leadEvNotRun; continue; }   /* a squad this game only copies (or a player's): not this game's to announce */
        g_leadWatch[t->first] = t->second;
    }
    for (std::map<unsigned int, double>::iterator b = g_followBackoff.begin(); b != g_followBackoff.end(); ) { if (now >= b->second) g_followBackoff.erase(b++); else ++b; }   /* as the 1 s pass prunes */
    std::set<void*> seen; std::map<void*, int> gate; int sends = 0;   /* the event road's own: LeadDropUnseen and HandoffResends stay the 1 s pass's */
    for (std::map<void*, LeadWatch>::iterator w = g_leadWatch.begin(); w != g_leadWatch.end(); )
    {
        const std::map<void*, std::vector<unsigned int> >::const_iterator sq = squads.find(w->first);
        if (sq == squads.end()) { g_leadWatch.erase(w++); continue; }   /* no member run here any more */
        const int arrival = arrivalAps.erase(w->first) != 0 ? 1 : 0;
        ++g_leadEvRuns;
        SquadPassOne(w->first, sq->second, false, now, mine, arrival != 0 ? kSqPassArrival : kSqPassEvent, w->second.kind, w->second.tick, &seen, &gate, &sends);
        ++w;
    }
    for (std::set<void*>::const_iterator a = arrivalAps.begin(); a != arrivalAps.end(); ++a)
    {
        const std::map<void*, std::vector<unsigned int> >::const_iterator sq = squads.find(*a);
        if (sq == squads.end()) continue;   /* a squad this game only copies: SquadLeaderSafePointDrain's (brought forward) */
        ++g_leadEvArrivalRuns;
        SquadPassOne(*a, sq->second, false, now, mine, kSqPassArrival, -1, 0, &seen, &gate, &sends);
    }
}
void HandoffTick()
{
    ReleaseForgetDrain();   /* [a1b2-hd6]: a world teardown's / the other player gone request first */
    RelRevokeWaitDrain();   /* [a1b2f1-hd12] [review F4]: REVOKEs that waited for unblocked engine writes */
    ReleaseFlush(NowSec());   /* [a1b2-hd6] design 2.5 item 2: last frame's put-aways become RELEASE offers - every frame, before the 1 Hz gate */
    PartGivenDrain();   /* T-1318 D3 fold 1 (review MEDIUM-1): the tripwire's squads noted since the last frame, judged with a fresh awake-here count */
    P119Flush();   // PROBE P119
    LeadEventTick();   /* T-1442 step 6: the noted squads announced with live reads, every frame, before the 1 s gate */
    if (++g_tick % kTickEvery != 0) return;
    const double now = NowSec();
    /* 1. resend / abandon pending transfers: T-1 B3 fold (L3) - at the END of this pass (HandoffResends), after the announcements */
    LeadLinkEdge();   /* T-1 B1 restructure: a new link hears every announcement again */
    ReleaseTick(now);   /* [a1b2-hd7] design 2.5: every open release's next step - resend, next candidate, asleep */
    RelHeldTick(now);   /* T-1318 attempt 3c: the held-undecided squads re-checked (gone / re-woken people, nobody awake any more) */
    IdxLiveScan(now);   /* [a1b2f1-hd6] [review F5] */
    // 2. group my owned characters by squad (T-1 B1: the verified reader - never getSquad, which can create a squad)
    const Sector mine = MyPlayerSector();
    if (mine.x < 0) return;
    for (std::map<unsigned int, double>::iterator b = g_followBackoff.begin(); b != g_followBackoff.end(); ) { if (now >= b->second) g_followBackoff.erase(b++); else ++b; }
    for (std::map<unsigned int, double>::iterator b = g_crossRefused.begin(); b != g_crossRefused.end(); ) { if (now >= b->second) g_crossRefused.erase(b++); else ++b; }
    std::map<void*, std::vector<unsigned int> > squads;
    std::set<void*> solo;   // keys that are a character with no squad
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int uid = 0; ::Character* c = 0;
        if (!MirrorSlot(i, &uid, &c)) continue;
        if (!net::IsUidMine(uid) || !Plaus(c)) continue;
        if (coop::IsPlayerFaction(c->getOwnerFactionDirect())) continue;   // P3: a player's own squad never changes hands (decision 23)
        if (CarryTakenStillCarried(uid)) { ++g_carryKeptOutOfPass; continue; }   /* T-1467: a body taken by carry, still carried here - no hand-over straight back */
        void* ap = SquadActivePlatoonOf(c);
        if (ap == 0) { ap = (void*)c; solo.insert(ap); }   // no squad: a squad of one, keyed by the character
        squads[ap].push_back(uid);
    }
    P119KeepSample(squads, now);   // PROBE P119
    /* T-1 B3 restructure (protocol 89): MSG_KEEPER retired - each squad's money rides its announcement below */
    std::set<void*> leadSeen;   /* T-1 B1 restructure: my squads announced (or unchanged) this pass - LeadDropUnseen retires the rest */
    std::map<void*, int> xferGate;   /* T-1 B3 fold (L3): per squad seen, HandoverXferMayGo - its resends are gated the same way */
    int leadSends = 0;
    g_catsWithheldNow = 0;   /* T-799 D1: counted again by this pass */
    for (std::map<void*, std::vector<unsigned int> >::iterator it = squads.begin(); it != squads.end(); ++it)
    {
        ++g_squadsSeen;
        SquadPassOne(it->first, it->second, solo.count(it->first) != 0, now, mine, kSqPassTick, -1, 0, &leadSeen, &xferGate, &leadSends);   /* T-1442 step 6: one function, the event road's too */
    }
    for (std::map<unsigned int, FollowGroupBlock>::iterator gb = g_followGroupBlock.begin(); gb != g_followGroupBlock.end(); )   /* T-1239: none of its members run here any more */
    {
        int anyMine = 0;
        for (std::set<unsigned int>::const_iterator u = gb->second.members.begin(); u != gb->second.members.end() && anyMine == 0; ++u) if (net::IsUidMine(*u)) anyMine = 1;
        if (anyMine == 0) g_followGroupBlock.erase(gb++); else ++gb;
    }
    LeadDropUnseen(leadSeen, &leadSends);   /* squads this game no longer runs any member of: n = 0, the other game forgets them */
    HandoffResends(now, xferGate);          /* T-1 B3 fold (L3): after this pass's announcements */
}

/* T-1467 THE OFFER (the body's game): one XFER naming only the carried body (its squad stays here, owner 1865 a), keyed on the body's uid,
   to the game that runs its carrier. No squad ap (no follow crossing, no raid block); its ACK and resends are the ordinary flight's. */
int HandoffCarryOffer(unsigned int body, unsigned int carrier, int byCountdown)
{
    if (body == 0 || carrier == 0 || !net::IsUidMine(body) || g_pending.find(body) != g_pending.end() || HandoffPendingHas(body)) return 0;
    if (CarryBodyIsPlayers(body) != 0) return 0;   /* fold HIGH-2: a player's character never changes hands (decision 23, as HandoffTick) */
    Pending p;
    BuildMembers(std::vector<unsigned int>(1, body), &p.members);
    if (p.members.size() != 1) return 0;
    p.reason = (unsigned int)coopsquad::kXferReasonCarry; p.ap = 0; p.logged = 1;   /* ap 0: RaidAwakeHandSend (first send none, resends) sends no raid block and holds nothing for it (store.cpp RaidAwakeSendImpl: no ap, no squad id -> 0) */
    p.targetSlot = net::OwnerSlotOf(carrier);
    if (p.targetSlot < 0) return 0;
    PendingKeysRead(&p);   /* the squad key and hand while whole - the ACK's index entry (a split squad: this game keeps the rest) */
    if (!net::SendXfer(body, p.reason, &p.members[0], 1, p.targetSlot, 0)) return 0;
    p.sentAt = NowSec(); p.sends = 1;
    ++g_xferOut; ++g_carryOffered;
    g_pending[body] = p;
    g_carryHand[body] = carryhand::kHandOpen;
    CarryHandLine("[CARRYHAND] OFFER body uid=" + N(body) + " carrier uid=" + N(carrier) + " -> slot " + N((long long)p.targetSlot) + " at " + N((long long)p.members[0].x) + "," + N((long long)p.members[0].z)
                  + (byCountdown != 0 ? " - this game's engine started its countdown to put the copy carrier's squad away (T-1467 attempt 2): handed alone to the carrier's game, the countdown held until the answer"
                                      : " - the carried body's own spot left the areas this game has loaded")
                  + " (its squad stays here; carryHand offered " + N(g_carryOffered) + ")");
    return 1;
}
/* T-1467 THE TAKE (the carrier's game): taken only when this game has a copy of the body, a character this game runs carries that copy here,
   and this game's engine keeps the body's spot - each read live now. A refusal ends the announce pass's hold for this carry. */
static void ApplyRemoteCarryXfer(unsigned int body, const net::XferMember* m, int count, unsigned int fromPeer, const unsigned int* gens)
{
    const int one = (count == 1 && m[0].uid == body) ? 1 : 0;
    ::Character* bc = one != 0 ? FindSpawned(body) : 0;
    const int alreadyMine = (one != 0 && net::IsUidMine(body)) ? 1 : 0;
    const unsigned int carrier = Plaus(bc) ? CarrierOfBodyHere(body) : 0u;
    const int carrierMine = (carrier != 0 && net::IsUidMine(carrier)) ? 1 : 0;
    float bx = 0, by = 0, bz = 0;
    const int keep = (Plaus(bc) && ReadPos(bc, &bx, &by, &bz) && KeepTakeAt(bx, bz) == 1) ? 1 : 0;
    const int players = (one != 0 && CarryBodyIsPlayers(body) != 0) ? 1 : 0;   /* fold HIGH-2: a player's character is refused (decision 23) */
    const int take = carryhand::HandTake(one != 0 ? 1 : (count == 1 ? 2 : count), alreadyMine, Plaus(bc) ? 1 : 0, carrierMine, keep, players);
    if (take != 0 && alreadyMine == 0)
    {
        int reRun = 0;
        TakeMemberFromPeer(m[0], net::GenForTake(body, gens != 0 ? gens[0] : 0u), &reRun);
        ++g_carryTakenHere;
        if (carrier != 0) g_carryTakenBy[body] = carrier;   /* left out of the squad pass while that carrier carries it */
    }
    if (take == 0) { ++g_carryRefusedHereN; if (one != 0) g_carryRefusedHere[body] = carrier; }
    const unsigned int named = body;
    if (net::SendXferAck(body, take != 0 ? &named : 0, take != 0 ? 1 : 0, fromPeer)) ++g_ackOut;
    CarryHandLine("[CARRYHAND] <- carry XFER body uid=" + N(body) + " members=" + N((long long)count) + " carrier uid=" + N(carrier) + " carrierMine=" + N((long long)carrierMine)
                  + " copyHere=" + N((long long)(Plaus(bc) ? 1 : 0)) + " keepHere=" + N((long long)keep) + " alreadyMine=" + N((long long)alreadyMine) + " players=" + N((long long)players)
                  + (take != 0 ? " - TAKEN: this game runs the body now (its squad stays with its game)" : " - REFUSED: the sender keeps it; this carry's hold ends")
                  + " (carryTakenHere " + N(g_carryTakenHere) + " carryRefusedHere " + N(g_carryRefusedHereN) + ")");
}

void ApplyRemoteXfer(unsigned int leader, unsigned int reason, const net::XferMember* m, int count, unsigned int fromPeer, const unsigned int* gens, int wholeBits)
{
    ++g_xferIn;
    if (reason == (unsigned int)coopsquad::kXferReasonCarry) { ApplyRemoteCarryXfer(leader, m, count, fromPeer, gens); return; }   /* T-1467: one carried body, its own take */
    const int lentWhole = (wholeBits & cooplo::kWholeBitLent) != 0 ? 1 : 0, worldWhole = (wholeBits & cooplo::kWholeBitWorld) != 0 ? 1 : 0;   /* T-1122 lent flag; T-1318 attempt 2 (D3, protocol 226): the world bit */
    /* T-1 B1 restructure (point 4): CROSSING follow transfers. A follow-leader XFER arriving while my own follow-leader XFER for the
       SAME squad (my squad holds its leader or one of its members) still waits for its ACK: coopsquad::CrossingTakeIncoming - the
       transfer whose destination runs the lower leader uid proceeds; the other game evaluates the same test with the keys swapped,
       so exactly one of the two moves. A refused one is ACKed naming none (the sender keeps its members and backs off), and its
       resends are refused for the same backoff. */
    if (reason == (unsigned int)coopsquad::kXferReasonFollowLeader)
    {
        const double tnow = NowSec();
        std::map<unsigned int, double>::const_iterator cr = g_crossRefused.find(leader);
        int refuse = (cr != g_crossRefused.end() && tnow < cr->second) ? 1 : 0;
        if (refuse == 0)
        {
            std::set<void*> inAps;
            ::Character* lc0 = FindSpawned(leader);
            if (Plaus(lc0)) { void* a0 = SquadActivePlatoonOf(lc0); if (a0 != 0) inAps.insert(a0); }
            for (int i = 0; i < count; ++i) { ::Character* c0 = FindSpawned(m[i].uid); if (Plaus(c0)) { void* a0 = SquadActivePlatoonOf(c0); if (a0 != 0) inAps.insert(a0); } }
            unsigned int minePending = 0;
            for (std::map<unsigned int, Pending>::const_iterator pp = g_pending.begin(); pp != g_pending.end(); ++pp)
                if (pp->second.reason == (unsigned int)coopsquad::kXferReasonFollowLeader && inAps.count(pp->second.ap) != 0) { minePending = pp->first; break; }
            if (minePending != 0)
            {
                ++g_crossingResolved;
                if (coopsquad::CrossingTakeIncoming(leader, minePending) == 0) { refuse = 1; g_crossRefused[leader] = tnow + kFollowBackoffSec; }
                DebugLog("[XFER] crossing follow transfers: incoming leader=" + N(leader) + " mine in flight leader=" + N(minePending)
                         + (refuse != 0 ? " - incoming REFUSED (the transfer to the lower leader, mine, proceeds)" : " - incoming taken (the lower leader runs here; mine will be refused)"));
            }
        }
        if (refuse != 0)
        {
            ++g_crossingRefused;
            if (net::SendXferAck(leader, 0, 0, fromPeer)) ++g_ackOut;
            return;
        }
    }
    int taken = 0, reRun = 0, unknown = 0;
    std::vector<unsigned int> takenUids;   // F447: the ACK names exactly these
    std::vector<int> takeIdx;   /* T-1122 whole or none (owner 1581 a): the members this game can take, taken after the whole squad is judged */
    const int awakeBlock = RaidAwakeBlockHas(leader); int awakeSkipped = 0;   /* T-1122 awake hand: the squad's raid came first - no carried goal issued */
    // T-1 B1 (F448 corrected): the engine sleeps a squad WHOLE by its squad position, and a member outside the loaded area
    // whose leader is inside stays live. So when this game runs the leader, or takes it in this message, the SQUAD position
    // (this game's copy squad's +0xB4, else the leader's own position in the message) is checked against my ring for every
    // member; otherwise each member's own position, as before.
    int leaderTaking = 0; float tlx = 0, tlz = 0;
    for (int i = 0; i < count; ++i) if (m[i].uid == leader) { leaderTaking = 1; tlx = m[i].x; tlz = m[i].z; break; }
    const int leaderHere = (net::IsUidMine(leader) || leaderTaking != 0) ? 1 : 0;
    int squadKnown = 0; float sqx = 0, sqz = 0;
    if (leaderHere != 0)
    {
        ::Character* lc = FindSpawned(leader);
        if (Plaus(lc) && SquadDecisionPos(lc, &sqx, &sqz) == 1) squadKnown = 1;
        else if (leaderTaking != 0) { sqx = tlx; sqz = tlz; squadKnown = 1; }
    }
    const int squadInRing = (squadKnown != 0 && KeepTakeAt(sqx, sqz) == 1) ? 1 : 0;   /* this game's engine keeps the squad at its spot (live), not the once-a-second loaded list */
    const bool isFollow = reason == (unsigned int)coopsquad::kXferReasonFollowLeader;
    const bool logIn = !isFollow || FollowLogChanged(2, leader, (long long)count * 8 + leaderHere * 4 + squadKnown * 2 + squadInRing);   // review M1
    /* T-1239 fix attempt 1: a follow-leader member is taken only when this game's own copy of it stands in the named leader's group on
       this game's engine (read live here, at the take); otherwise refused as 'not in the leader's group here' - the ACK names it so
       (its tail), and the sender goes by position until an announcement for the group changes. */
    void* leaderGroupHere = 0;
    if (isFollow) { ::Character* lg = FindSpawned(leader); if (Plaus(lg)) leaderGroupHere = SquadActivePlatoonOf(lg); }
    std::vector<unsigned int> notInGroup;
    for (int i = 0; i < count; ++i)
    {
        ::Character* c = FindSpawned(m[i].uid);
        if (!Plaus(c)) { ++unknown; ++g_xferInUnknownUid; if (logIn) DebugLog("[XFER] member uid=" + N(m[i].uid) + " has no copy here - NOT taken (the sender keeps it)"); continue; }   /* M7a3f6 [m7a3f6-hb1] M7a3f7: no void here - an offer (even a forced one: the ordinary position hand-over is sent forced too, H1) says nothing of whether the sender still runs the person; only its RELEASE does (A1 build 2) */
        if (isFollow && !net::IsUidMine(m[i].uid))   /* already ours (a resend after our ACK was lost): never refused here */
        {
            void* const memberGroupHere = SquadActivePlatoonOf(c);
            if (coopsquad::FollowMemberInLeaderGroup(leaderGroupHere, memberGroupHere) == 0)
            {
                ++unknown; ++g_followNotInGroup; notInGroup.push_back(m[i].uid);
                DebugLog("[XFER] member uid=" + N(m[i].uid) + " - this game's copy is not in leader " + N(leader) + "'s group here ("
                         + (leaderGroupHere == 0 ? std::string("the leader has no group here") : std::string(memberGroupHere == 0 ? "the copy has no group" : "the copy is in another group"))
                         + ") - NOT taken: not in the leader's group here (followNotInGroup " + N(g_followNotInGroup) + ")");
                continue;
            }
        }
        if (net::IsUidMine(m[i].uid)) { ++taken; takenUids.push_back(m[i].uid); continue; }   // duplicate XFER (resend after our ACK was lost): already ours - read before the keep test (T-1122 review item 4: named even where this game's engine would no longer keep it)
        const int memberInRing = KeepTakeAt(m[i].x, m[i].z) == 1 ? 1 : 0;   /* the live keep read */
        if (coopsquad::XferTakeMember(leaderHere, squadKnown, squadInRing, memberInRing) == 0)
        {
            ++unknown; ++g_xferInOutsideRing;
            if (!logIn) continue;
            if (leaderHere != 0 && squadKnown != 0)
                DebugLog("[XFER] member uid=" + N(m[i].uid) + " - its squad (leader " + N(leader) + ") stands in " + SectorString(SectorOf(sqx, sqz)) + ", at a spot this game's engine would not keep - NOT taken (my engine would sleep the squad)");
            else
                DebugLog("[XFER] member uid=" + N(m[i].uid) + " stands in " + SectorString(SectorOf(m[i].x, m[i].z)) + ", at a spot this game's engine would not keep - NOT taken (my engine would unload it)");
            continue;
        }
        takeIdx.push_back(i);   /* T-1122 whole or none: taken below, once the whole squad is judged */
    }
    const int alreadyHere = taken;   /* T-1122 lent flag: members this game already runs - this game's, named in the ACK under any judge (LOW-2 reverted) */
    int tw = raidsq::kTwEach;   /* T-1122 review LOW-B: the judge, kept for the take note and the block below */
    {   /* T-1122 whole or none (owner 1581 a, run T2274; raidsquad.h's section): a hand-over carrying the squad's raid block is a lent squad's -
           all of its people are taken here or none; none: the ACK names none, the sender keeps the whole squad and backs off */
        const int judge = cooplo::WholeJudgeOf(wholeBits, awakeBlock != 0 ? 1 : 0);   /* T-1318 D3: lent (T-1122, its counters), else world (the bit every XFER carries - forced, and follow-leader since fold 1 (review HIGH-1 a): a T-1239 'not in the leader's group' or 'no copy here' row then refuses the WHOLE message, not one row) */
        tw = raidsq::LentTakeWhole(judge != cooplo::kWjEach ? 1 : 0, taken + (int)takeIdx.size(), count);   /* T-1122 lent flag (with or without the block) / T-1318 D3 world bit */
        if (tw == raidsq::kTwAll) { if (judge == cooplo::kWjLent) RaidLentWholeNote(raidsq::kLwXferAll); else ++g_wwXferAll; }
        if (tw == raidsq::kTwNone)
        {
            if (judge == cooplo::kWjLent) RaidLentWholeNote(raidsq::kLwXferNone); else { ++g_wwXferNone; if (leaderHere == 0) ++g_wwXferNoneLeaderless; }   /* T-1318 D3: a refused batch without its leader counted apart */
            if (logIn) DebugLog("[XFER] " + std::string(judge == cooplo::kWjLent ? "lent" : "world") + " squad leader=" + N(leader) + (leaderHere != 0 ? std::string() : std::string(" (its leader not in the message)")) + " worldWhole=" + N((long long)worldWhole) + " lentWhole=" + N((long long)lentWhole) + " block=" + N((long long)(awakeBlock != 0 ? 1 : 0)) + " WHOLE OR NONE: " + N((long long)(taken + (int)takeIdx.size())) + " of " + N(count) + " member(s) can be taken here - NONE taken"
                     + " (the sender keeps the whole squad with its raid; " + N((long long)takeIdx.size()) + " take(s) not made; " + N((long long)alreadyHere) + " already run here - named in the ACK, this game's)");
            takeIdx.clear();
        }
    }
    for (size_t t = 0; t < takeIdx.size(); ++t)
    {
        const int i = takeIdx[t];
        takenUids.push_back(m[i].uid);
        TakeMemberFromPeer(m[i], net::GenForTake(m[i].uid, gens != 0 ? gens[i] : 0u), &reRun, awakeBlock, &awakeSkipped);   /* [a1b2-hd4] design 2.5 item 3: the take path shared with RELEASE (the giver's gen + 1, or above this game's copy record) */
        ++taken;
        if (reason == (unsigned int)coopsquad::kXferReasonFollowLeader) ++g_xferFollowTaken;
    }
    if (net::SendXferAck(leader, takenUids.empty() ? 0 : &takenUids[0], (int)takenUids.size(), fromPeer, notInGroup.empty() ? 0 : &notInGroup[0], (int)notInGroup.size())) ++g_ackOut;
    if (taken > 0 && tw != raidsq::kTwNone) RaidAwakeTakeNote(leader, taken == count ? 1 : 0);   /* T-1122 fix A re-check fold (2): the take's fullness, block or not; review LOW-B: not under a whole refusal (no take was made) */
    if (awakeBlock != 0 && taken > 0 && tw != raidsq::kTwNone)   /* review LOW-B: a whole refusal starts no campaign here */
    {   /* T-1122 awake hand: the squad came with its raid - the block applied to the squad taken here (its campaign starts) */
        int mine = 0; void* ap = HandoffMySquadApOf(leader, &mine);
        RaidAwakeTaken(leader, ap, taken == count ? 1 : 0);   /* T-1122 fix A review fold 1: every member the message named taken here */
        DebugLog("[XFER] <- awake raid squad leader=" + N(leader) + " taken=" + N(taken) + " - the carried order skipped for " + N((long long)awakeSkipped)
                 + " member(s) (its raid's plan came with it; runs the leader " + N((long long)mine) + ")");
    }
    if (isFollow && taken > 0) FollowLogReset(2, leader);   // settled: the next refusal logs again
    if (!logIn && taken == 0) return;
    const std::string q(1, (char)34);
    DebugLog("[XFER] <- squad leader=" + N(leader) + " members=" + N(count) + " taken=" + N(taken) + " carriedNotIssued=" + N(reRun) + " unknown=" + N(unknown) + " lentWhole=" + N((long long)lentWhole) + " alreadyHere=" + N((long long)alreadyHere) + " worldWhole=" + N((long long)worldWhole) + (reason == (unsigned int)coopsquad::kXferReasonForced ? std::string(" FORCED") : (reason == (unsigned int)coopsquad::kXferReasonNone ? std::string() : " reason=" + std::string(coopsquad::XferReasonName(reason)))));
    SW_DEVLOG("[VERDICT] {" + q + "ev" + q + ":" + q + "xfer_in" + q + "," + q + "leader" + q + ":" + N(leader) + "," + q + "members" + q + ":" + N(count) + "," + q + "taken" + q + ":" + N(taken)
             + "," + q + "carriedNotIssued" + q + ":" + N(reRun) + "," + q + "unknown" + q + ":" + N(unknown) + ",\"tick\":" + N(g_tick) + "}");
}

static long long g_ackNotMine = 0;   /* fold 1 [a1b1f1-hd3] [F7]: members an ACK named that this game no longer runs (a dual run's yield released them) */
void ApplyRemoteXferAck(unsigned int leader, const unsigned int* takenUids, int count, unsigned int fromPeer, const unsigned int* notInGroup, int notInGroupCount)
{
    ++g_ackIn;
    ReleaseForgetDrain();
    RelWalk walk;   /* [a1b2f1-hd10] [review F1] */
    std::map<unsigned int, Pending>::iterator it = g_pending.find(leader);
    if (it == g_pending.end()) return;   // late duplicate ACK
    const Pending& p = it->second;
    if (p.targetSlot >= 0 && net::PeerSlotOfKey(fromPeer) != p.targetSlot)   /* M7a A1 build 1 [a1b1-hd9] [review F11]: only the XFER's target answers it */
    {
        ++g_ackWrongSender;
        DebugLog("[XFER] ACK leader=" + N(leader) + " from slot " + N((long long)net::PeerSlotOfKey(fromPeer)) + " IGNORED - the XFER went to slot " + N((long long)p.targetSlot) + " (ackWrongSender " + N(g_ackWrongSender) + ")");
        return;
    }
    long long released = 0, kept = 0;
    const bool isFollow = p.reason == (unsigned int)coopsquad::kXferReasonFollowLeader;
    std::set<unsigned int> refusedOutOfGroup;   /* T-1239 */
    std::map<std::string, std::pair<long long, long long> > hoSquads;   /* T-850 (T2230): each squad this ACK names people of -> (released, kept): the writer re-check below */
    const bool logAck = !isFollow || p.logged != 0;   /* N5: an ACK line is written exactly when its send's xfer_out line was - the two stay paired */
    for (size_t k = 0; k < p.members.size(); ++k)
    {
        const unsigned int uid = p.members[k].uid;
        // F447 (T134): the receiver takes only uids it has a live copy of; releasing an untaken uid left a character
        // that nobody simulated. What the receiver did not name stays ours: a forced hand-over then backs off from that receiver
        // until the squad's sector or its receiver changes (g_forcedTry); a follow-leader one waits kFollowBackoffSec.
        bool taken = false;
        for (int i = 0; i < count; ++i) if (takenUids[i] == uid) { taken = true; break; }
        if (!taken)
        {
            WorldsyncCatchupHandoverSettled(uid); /* M7a2 item 2 [m7a2-ho1]: still ours - a catch-up that held it back is answered */ ++kept; ++g_keptUntaken;
            { const std::string hk = HoSquadIdOf(uid, FindSpawned(uid), p); if (!hk.empty()) ++hoSquads[hk].second; }   /* T-850: kept here - its squad is not released by record */
            bool outOfGroup = false;   /* T-1239: the ACK's tail - refused as not in the leader's group on the receiver */
            for (int j = 0; isFollow && j < notInGroupCount; ++j) if (notInGroup[j] == uid) { outOfGroup = true; break; }
            if (outOfGroup) { refusedOutOfGroup.insert(uid); DebugLog("[XFER] member uid=" + N(uid) + " NOT taken by the receiver: not in the leader's group there - still ours"); }
            else if (logAck) DebugLog("[XFER] member uid=" + N(uid) + " NOT taken by the receiver (no copy there, or its engine would not keep it there) - still ours");
            continue;
        }
        if (!net::IsUidMine(uid))   /* [a1b1f1-hd3] [F7]: already released (a dual run's yield) - never released again with gen 0 */
        {
            WorldsyncCatchupHandoverSettled(uid); ++g_ackNotMine;
            if (logAck) DebugLog("[XFER] member uid=" + N(uid) + " taken by the receiver, no longer run here (released before this ACK) - not released again (ackNotMine " + N(g_ackNotMine) + ")");
            continue;
        }
        // E27 / review-p5q HIGH-3: resolved BEFORE the release, and told to the world-state shadow straight after
        // it. Its `ownedHere` flag is monotone upward otherwise, and it is what the send gate falls back on once
        // this character is no longer loaded here - so leaving it set publishes for a character the peer now owns.
        WorldsyncCatchupHandoverSettled(uid);   /* M7a2 item 2 [m7a2-ho2]: BEFORE the release - a held-back asker gets its state from the owner of the moment, then OWNER_MOVED */
        ::Character* rel = FindSpawned(uid);
        IdxGiveAtAck(uid, rel, p, fromPeer);   /* [a1b2-hd10] design 2.3: the index's given - the squad key from the body, else as read at the send */
        { const std::string hk = HoSquadIdOf(uid, rel, p); if (!hk.empty()) ++hoSquads[hk].first; }   /* T-850 (T2230): this person's squad - read before the release, as the probe below reads it */
        SquadCatsFinalBeforeRelease(uid);        /* T-1 B3 fold (M3): the round trip's own change, announced before the release */
        net::ReleaseLocalOwner(uid, fromPeer);   // the streams stop here (IsUidMine false)
        SquadCatsReleased(uid);                  /* T-1 B3 fold (L4): no stale carries-cats record */
        WorldStateOnOwnershipReleased(uid, rel);   // E27: "we owned this record" is no longer true   /* E32 (verify-p5u MEDIUM-1): the UID leads, because `rel` is 0 for every squad whose row the engine has already retired - which is most of them, since a hand-off is offered exactly when this game is walking away from them */
        PuppetIfBody(uid, rel, "XFER ACK");   /* [a1b2f1-hd13] [review F6]: our character becomes the puppet of the new owner (no body: a counted debug line) */
        ++g_released; ++released;
    }
    if (kept > 0 && p.reason == (unsigned int)coopsquad::kXferReasonFollowLeader) g_followBackoff[leader] = NowSec() + kFollowBackoffSec;   // T-1 B1
    if (!refusedOutOfGroup.empty())   /* T-1239 fix attempt 1: the leader is unknown for this group until an announcement for it changes */
    {
        FollowGroupBlock& b = g_followGroupBlock[leader];
        b.members.insert(refusedOutOfGroup.begin(), refusedOutOfGroup.end()); b.mark = p.mark; ++g_followBlockSet;
        DebugLog("[XFER] follow leader=" + N(leader) + " refused " + N((long long)refusedOutOfGroup.size()) + " member(s) as not in its group on the receiver - BLOCKED: the group goes by position"
                 " (keep check, area hand-over) and is not sent by follow-leader again until either game's announcement for it changes (followBlockSet " + N(g_followBlockSet) + ")");
    }
    if (p.reason == (unsigned int)coopsquad::kXferReasonForced)   /* T-650 fold 1 (review MED-1): a refused forced hand-over backs off */
    {
        std::map<unsigned int, ForcedTry>::iterator ftry = g_forcedTry.find(leader);
        if (ftry != g_forcedTry.end())
        {
            if (kept > 0 && ftry->second.target == p.targetSlot)
            {
                ftry->second.refused = 1; ++g_holderBackoff;
                for (size_t fu = 0; fu < p.members.size(); ++fu) g_forcedRefusedUid[p.members[fu].uid] = leader;   /* the announce pass may withdraw them now */
                DebugLog("[XFER] forced leader=" + N(leader) + " BACKED OFF from slot " + N((long long)p.targetSlot) + " - it kept " + N(kept) + " member(s) at sector "
                         + N((long long)ftry->second.x) + "," + N((long long)ftry->second.y) + "; offered again only when the squad's sector or its receiver changes (releaseHolder backoff "
                         + N(g_holderBackoff) + ")");
            }
            else g_forcedTry.erase(ftry);
        }
    }
    if (isFollow && kept == 0) { FollowLogReset(0, leader); FollowLogReset(1, leader); }   // settled: the next follow for it logs again
    if (logAck)
    {
        const std::string q(1, (char)34);
        DebugLog("[XFER] ACK leader=" + N(leader) + " released=" + N(released) + " kept=" + N(kept) + " after " + N(p.sends) + " send(s)");
        SW_DEVLOG("[VERDICT] {" + q + "ev" + q + ":" + q + "xfer_ack" + q + "," + q + "leader" + q + ":" + N(leader) + "," + q + "released" + q + ":" + N(released) + "," + q + "kept" + q + ":" + N(kept) + "," + q + "sends" + q + ":" + N(p.sends) + ",\"tick\":" + N(g_tick) + "}");
    }
    /* T-850 (T2230): the hand-over sends no SQUAD_RELEASE, and the world server moves a squad's writer only on one - each squad whose people
       this ACK passed gets the heartbeat's writer re-check now (store.cpp StoreHandOverJudge), so its release goes out. MAIN THREAD. */
    for (std::map<std::string, std::pair<long long, long long> >::const_iterator hs = hoSquads.begin(); hs != hoSquads.end(); ++hs)
        if (hs->second.first > 0) coop::StoreHandOverJudge(hs->first, p.targetSlot, hs->second.first, hs->second.second);
    if (p.reason == (unsigned int)coopsquad::kXferReasonCarry)   /* T-1467: the carrier's game's answer settles the carry's hand-over */
        CarryHandSettle(leader, released > 0 ? 1 : 0, released > 0 ? "the carrier's game took it (ACK): it runs the body now; its squad stays here"
                                                                   : "the carrier's game kept none (ACK): the carry goes on as before this change");
    const Pending flown(p);
    g_pending.erase(it); ++g_hoOpenSeq;   /* T-1318 fold 2: the flight ended (its ACK) */
    { const std::vector<unsigned int> tk(takenUids, takenUids + count); ReleaseOpenFromFlight(leader, flown, tk, 0); }   /* [a1b2-hd11] design 2.5 item 1: a member not taken whose body was put away while it flew goes to a release - after the erase, so an UNLOAD settled at once is not held by this flight */
}

/* M4 fold 2 (re-check L-C): is a hand-over of uid in flight from this game (an XFER naming it, no ACK yet)? AnnouncePass
   skips such a uid. MAIN THREAD, like g_pending's other readers. */
bool HandoffPendingHas(unsigned int uid)
{
    for (std::map<unsigned int, Pending>::const_iterator it = g_pending.begin(); it != g_pending.end(); ++it)
        for (size_t k = 0; k < it->second.members.size(); ++k)
            if (it->second.members[k].uid == uid) return true;
    return false;
}

/* M7a A1 build 1 [a1b1-hd12], build 2 [a1b2-hd12] (design 2.5 item 8 [review F1]): "still being handed on" - an open release, this frame's
   release batch or an XFER awaiting its ACK: a ROSTER CHECK is answered PENDING (the copy elsewhere is kept, frozen). */
bool HandoffRosterPending(unsigned int uid) { return ReleasePendingHas(uid); }
/* [review F7]: the squads this game runs whose last announcement went to one of a catch-up ask's sectors are announced again at the next
   1-Hz pass (route AREA) - the ask's newly delivered games. Fold 1 [a1b1f1-hd2] [F5]: only those (cooplo::LeadReannounceThis); a squad
   that moved since is re-sent anyway (F3), and a WORLD announcement (-1) reached every game. */
static long long g_leadReannounced = 0;
void SquadLeadReannounce(const std::vector<int>& sectorKeys)
{
    if (sectorKeys.empty()) return;
    for (std::map<void*, MyLead>::iterator it = g_myLead.begin(); it != g_myLead.end(); ++it)
        if (cooplo::LeadReannounceThis(it->second.areaKey, &sectorKeys[0], sectorKeys.size())) it->second.sent = 0;
    ++g_leadReannounced;
}
long long HandoffAckWrongSender() { return g_ackWrongSender; }
/* M7a3f1 #1/#2: ANY THREAD, possibly inside TeardownBroadcastLateFlags' __finally - POD only: no allocation, no logging, no map. */
/* M7a3f2 GAP 1: POD reads of a character the engine is destroying (the detour runs before GameWorld::destroy, which defers the
   free - verify-p6b.md sec. 1 - so the object is whole; its uid row was retired a moment earlier by RetireOnDestroy, so it is
   read through the pointer, never through FindSpawned). */
static int PutAwayPlayerPod(::Character* c)
{
    __try { return coop::IsPlayerFaction(c->getOwnerFactionDirect()) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static int PutAwayFacingIntentPod(::Character* c, net::XferMember* m)
{
    __try { ReadAuthorityFacing(c, &m->fx, &m->fz); ReadAuthorityIntent(c, &m->intentType, &m->intentSubject, &m->ix, &m->iy, &m->iz); return 1; }   /* the intent: sent, never issued by the taker since T-1416 (counted / probed only) - later clean-up */
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// PROBE-START: P119 (M7a A1 P-a, 2026-10-02) - the engine's put-away condition (squad position vs the loaded edge)
/* Question: at what squad position does THIS game's engine put a squad away, against this game's player sector and the edge of its loaded
   area - so AdoptDecide's keepMargin (build 2) is set from data. Log only, no engine write, POD reads under SEH (ReadPos / ReadSquadOf /
   the reason copy below). Per squad per frame (keyed by the engine squad); the first 200 logged, then counted. */
struct P119Row { unsigned int key; int n; float x, z; int leaderLive; char reason[48]; };
static std::map<void*, P119Row> g_p119Batch;
static long long g_p119Put[4] = { 0, 0, 0, 0 }, g_p119Keep[4] = { 0, 0, 0, 0 }, g_p119KeepEdge[4] = { 0, 0, 0, 0 }, g_p119Logged = 0, g_p119Counted = 0;
static double g_p119KeepAt = -1.0;
static void P119CopyReason(const char* r, char* out, int cap)   /* no C++ object in here (C2712) */
{
    __try { int i = 0; for (; r != 0 && i + 1 < cap && r[i] != 0; ++i) out[i] = (r[i] == '\'' ? '"' : r[i]); out[i] = 0; if (r == 0) { out[0] = '-'; out[1] = 0; } }
    __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = '?'; out[1] = 0; }
}
static int P119Kind(const Sector& s, const Sector& p)   /* 0 self, 1 side, 2 corner, 3 out */
{
    const int dx = s.x > p.x ? s.x - p.x : p.x - s.x, dy = s.y > p.y ? s.y - p.y : p.y - s.y;
    if (dx == 0 && dy == 0) return 0;
    if (dx <= 1 && dy <= 1) return (dx == 0 || dy == 0) ? 1 : 2;
    return 3;
}
static const char* P119KindName(int k) { return k == 0 ? "self" : (k == 1 ? "side" : (k == 2 ? "corner" : "out")); }
static int P119EdgeBin(float e) { return e < 0.0f ? 0 : (e < 500.0f ? 0 : (e < 1000.0f ? 1 : (e < 2000.0f ? 2 : 3))); }
void P119NotePutAway(unsigned int uid, const void* obj, const char* reason)
{
    ::Character* c = (::Character*)obj;
    if (uid == 0 || !Plaus(c) || PutAwayPlayerPod(c) != 0) return;   /* own non-player people only */
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!ReadPos(c, &x, &y, &z)) return;
    SquadView sv; std::memset(&sv, 0, sizeof(sv));
    const int haveSquad = ReadSquadOf(c, &sv) == 1 ? 1 : 0;
    void* key = haveSquad != 0 && sv.ap != 0 ? sv.ap : (void*)c;
    P119Row& r = g_p119Batch[key];
    if (r.n == 0)
    {
        r.key = uid; r.x = x; r.z = z;
        r.leaderLive = (haveSquad != 0 && Plaus(sv.leader) && (void*)sv.leader != (void*)c) ? 1 : 0;
        P119CopyReason(reason, r.reason, (int)sizeof(r.reason));
    }
    ++r.n;
}
void P119Flush()
{
    if (g_p119Batch.empty()) return;
    const Sector p = MyPlayerSector();
    for (std::map<void*, P119Row>::const_iterator it = g_p119Batch.begin(); it != g_p119Batch.end(); ++it)
    {
        const P119Row& r = it->second;
        const Sector s = SectorOf(r.x, r.z);
        const int kind = p.x >= 0 ? P119Kind(s, p) : 3;
        ++g_p119Put[kind];
        if (g_p119Logged >= 200) { ++g_p119Counted; continue; }
        ++g_p119Logged;
        const int cheb = p.x < 0 ? -1 : ((s.x > p.x ? s.x - p.x : p.x - s.x) > (s.y > p.y ? s.y - p.y : p.y - s.y) ? (s.x > p.x ? s.x - p.x : p.x - s.x) : (s.y > p.y ? s.y - p.y : p.y - s.y));
        const float pcx = p.x * 4608.0f - 147456.0f + 2304.0f, pcz = p.y * 4608.0f - 147456.0f + 2304.0f;   /* the player SECTOR's centre (zones.cpp kSectorSize / kSectorOrigin) */
        const float pd = sqrtf((r.x - pcx) * (r.x - pcx) + (r.z - pcz) * (r.z - pcz));
        SW_DEVLOG("[PROBE] P119 putAway key=" + N(r.key) + " n=" + N((long long)r.n) + " reason='" + std::string(r.reason) + "' pos=" + N((long long)r.x) + "," + N((long long)r.z)
                 + " sector=" + SectorString(s) + " player=" + SectorString(p) + " cheb=" + (cheb < 0 ? std::string("?") : (cheb >= 2 ? std::string("2+") : N((long long)cheb)))
                 + " kind=" + P119KindName(kind) + " edge=" + N((long long)P119EdgeDist(r.x, r.z)) + " playerSecDist=" + N((long long)pd)
                 + " zone='" + P119ZoneText(s.x, s.y) + "' leaderLive=" + N((long long)r.leaderLive));
    }
    g_p119Batch.clear();
}
/* every 10 s: one keep sample of every own non-player squad still live (HandoffTick's walk) - the denominator of a put-away RATE per class */
static void P119KeepSample(const std::map<void*, std::vector<unsigned int> >& squads, double now)
{
    if (g_p119KeepAt >= 0.0 && now - g_p119KeepAt < 10.0 && now >= g_p119KeepAt) return;
    g_p119KeepAt = now;
    const Sector p = MyPlayerSector();
    long long k[4] = { 0, 0, 0, 0 }, e[4] = { 0, 0, 0, 0 };
    for (std::map<void*, std::vector<unsigned int> >::const_iterator it = squads.begin(); it != squads.end(); ++it)
    {
        if (it->second.empty()) continue;
        ::Character* c = FindSpawned(it->second[0]);
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!Plaus(c) || !ReadPos(c, &x, &y, &z)) continue;
        const int kind = p.x >= 0 ? P119Kind(SectorOf(x, z), p) : 3;
        ++k[kind]; ++g_p119Keep[kind];
        const int b = P119EdgeBin(P119EdgeDist(x, z)); ++e[b]; ++g_p119KeepEdge[b];
    }
    SW_DEVLOG("[PROBE] P119 keep self=" + N(k[0]) + " side=" + N(k[1]) + " corner=" + N(k[2]) + " out=" + N(k[3]) + " edge[<500,<1000,<2000,>=2000]=" + N(e[0]) + "," + N(e[1]) + "," + N(e[2]) + "," + N(e[3])
             + " P119[putAway self,side,corner,out; keep self,side,corner,out; logged,counted]=" + N(g_p119Put[0]) + "," + N(g_p119Put[1]) + "," + N(g_p119Put[2]) + "," + N(g_p119Put[3])
             + ";" + N(g_p119Keep[0]) + "," + N(g_p119Keep[1]) + "," + N(g_p119Keep[2]) + "," + N(g_p119Keep[3]) + ";" + N(g_p119Logged) + "," + N(g_p119Counted));
}
// PROBE-END: P119
/* M7a A1 build 2 [a1b2-hd13] (design 2.5 item 1): NotifyDespawn's UNLOAD branch, this game's own uid - collected into this frame's batch for
   the engine squad (ReleaseFlush opens it next frame). No judgement here. Not collected: a person in an XFER awaiting its ACK (the ACK's),
   an off-thread drain (objWhole 0: nothing read), an unreadable object, a player's person, one the other games were never told about
   (nobody holds a copy) - their UNLOAD goes as before. */
void SameIdWithdrawMark(const unsigned int* uids, int n, int kind)
{
    ReleaseForgetDrain();   /* a teardown's pending clean-up runs now, never between these marks and the retire's put-aways */
    for (int i = 0; i < n; ++i)
    {
        const unsigned int u = uids[i];
        if (u == 0 || !net::IsUidMine(u)) continue;
        if (g_sameIdWd.find(u) == g_sameIdWd.end() && g_sameIdWd.size() >= kSameIdWdCap) { ++g_sameIdWdFull; continue; }
        g_sameIdWd[u] = kind; ++g_sameIdWdMarked;
    }
}
void SameIdWithdrawDone()
{
    for (std::map<unsigned int, int>::iterator it = g_sameIdWd.begin(); it != g_sameIdWd.end(); )
    {
        /* kept only for a person whose body the retire put away while its XFER is in flight (ReleaseOpenFromFlight drops it); a person
           the retire did not put away loses the mark, so a later ordinary put-away is offered as usual */
        if (HandoffPendingHas(it->first) && !Plaus(FindSpawned(it->first))) ++it; else g_sameIdWd.erase(it++);
    }
}
/* the TEST-ONLY clearnear lever: a squad it removes leaves the world - its own-uid people are marked before its platoonwipe, so their
   put-away is unloaded and never offered (the lever calls SameIdWithdrawDone right after) */
void SameIdWithdrawMarkClearNear(const unsigned int* uids, int n) { SameIdWithdrawMark(uids, n, kSameIdClearNear); }
long long SameIdClearNearUnoffered() { return g_relSkipClearNear; }
void ReleaseOnPutAway(unsigned int uid, const void* obj, int objWhole)
{
    ReleaseForgetDrain();
    if (uid == 0 || !net::IsUidMine(uid)) return;
    const std::map<unsigned int, int>::const_iterator sw = g_sameIdWd.find(uid);
    const int padRoad = cooplo::PutAwayRoad(sw != g_sameIdWd.end() ? 1 : 0, HandoffPendingHas(uid) ? 1 : 0);
    if (padRoad == cooplo::kPadUnloadSameId)
    {   /* a same-id retire withdrew it: the other game runs this person - unloaded, never offered */
        if (sw->second == kSameIdReload) ++g_relSkipReload; else if (sw->second == kSameIdClearNear) ++g_relSkipClearNear; else ++g_relSkipYield;
        return;
    }
    if (padRoad == cooplo::kPadLeaveToAck) { ++g_relSkipInFlight; return; }
    /* [a1b2f1-hd14] [review F5]: a person not offered stays in this game's world data only - kept here (its key from the body when whole,
       else the 1 Hz cache), so a partly given squad is never treated as wholly given */
    if (objWhole == 0) { ++g_relSkipOffThread; IdxKeep(uid, 0); return; }
    ::Character* c = (::Character*)obj;
    if (!Plaus(c)) { ++g_relSkipUnread; IdxKeep(uid, 0); return; }
    const int player = PutAwayPlayerPod(c);
    if (player != 0) { if (player == 1) ++g_relSkipPlayer; else { ++g_relSkipUnread; IdxKeep(uid, 0); } return; }
    if (coopsquad::AnnouncedToOthers(AnnouncedState(uid)) == 0) { ++g_relSkipNotAnnounced; IdxKeep(uid, c); return; }
    RelMember rm; rm.m.uid = uid; rm.baseGen = net::MineGenOf(uid);
    { void* f = 0; std::string id; if (SquadKeyOf(c, &f, &id)) { rm.key = HoKey(f, id); rm.hasKey = 1; } }
    { ObjId oid; if (CaptureObjId(c, &oid)) rm.hand = cooplo::HandKey(oid.serial, oid.type); }   /* T-1046 fold 2: the hand its sleeping record will carry */   /* read once, while the body is whole (design 2.1) */
    if (!ReadPos(c, &rm.m.x, &rm.m.y, &rm.m.z) || PutAwayFacingIntentPod(c, &rm.m) != 1) { ++g_relSkipUnread; IdxKeepKey(uid, rm.key, rm.hasKey, rm.hand); return; }
    if (rm.hasKey == 0) { ++g_relSkipNoKey; IdxKeepKey(uid, rm.key, 0, rm.hand); return; }   /* [a1b2f1-hd15] [review F3]: no squad key - never offered (a re-wake could not cancel it); its UNLOAD goes as before */
    SquadView sv; void* ap = 0; unsigned int leaderUid = 0; float dx = rm.m.x, dz = rm.m.z;
    std::set<const void*> unaccSeen; int over64 = 0, listChars = -1, listKnown = -1; std::set<std::string> deadSeen, otherSeen; std::map<std::string, unsigned int> otherUidSeen;
    if (ReadSquadOf(c, &sv) == 1)
    {
        ap = sv.ap;
        if (Plaus(sv.leader)) leaderUid = FindSpawnedUidForDestroy(sv.leader);
        if (sv.posUsable != 0) { dx = sv.x; dz = sv.z; }   /* the squad decision position - the engine sleeps the squad by it */
        /* T-1318 attempt 4 (owner 1626, design A, site 2): the engine's own member list at the put-away - its living members other than this
           person with no uid of this game's (never numbered, or another game's copy) are unaccounted; a list over 64 is never whole */
        SquadUnaccountedIn(sv, 0, (const void*)c, &listKnown, &unaccSeen, &deadSeen, &otherSeen, &otherUidSeen);   /* a4 fold 1: corpses' / another game's hands; attempt 5 (D1-R3): with their owner slots */
        for (std::map<std::string, unsigned int>::const_iterator o = otherUidSeen.begin(); o != otherUidSeen.end(); ++o) OtherHandNote(rm.key, o->first, o->second);
        listChars = sv.count;
        if (sv.count > kSquadViewCap || sv.stored < sv.count) over64 = 1;
    }
    if (ap == 0) ap = (void*)c;
    RelBatch& b = g_relBatch[ap];
    if (b.mem.empty()) { b.keyUid = (leaderUid != 0 && net::IsUidMine(leaderUid)) ? leaderUid : uid; b.sector = SectorOf(dx, dz); b.px = dx; b.pz = dz; }
    b.mem.push_back(rm);
    b.unaccAwake.insert(unaccSeen.begin(), unaccSeen.end());
    b.handsAcc.insert(deadSeen.begin(), deadSeen.end()); b.handsAcc.insert(otherSeen.begin(), otherSeen.end());   /* T-1318 a4 fold 1 (HIGH-1 / MEDIUM-1) */
    if (b.ap == 0 && ap != (void*)c) b.ap = ap;   /* T-1318 a4 fold 1 (LOW-1): the engine squad, only when its list read */
    if (!deadSeen.empty() && (g_deadHandsOf.count(rm.key) != 0 || g_deadHandsOf.size() < kDeadHandsSquadCap))
    {   /* T-1318 a4 fold 1 (MEDIUM-1): the squad's corpses - neither given nor kept by its sleeping records later */
        std::set<std::string>& dh = g_deadHandsOf[rm.key];
        for (std::set<std::string>::const_iterator d = deadSeen.begin(); d != deadSeen.end() && dh.size() < kDeadHandsPerSquadCap; ++d) dh.insert(*d);
    }
    if (over64 != 0) b.over64 = 1;
    if (listChars >= 0 && listChars > b.chars) { b.chars = listChars; b.known = listKnown + 1; }   /* + this person (numbered) */
}
/* T-1318 attempt 5 (D1-R1): the capped line for kept people a re-wake left kept - not in the woken squad's engine list (counted keptStay) */
/* a5 fold 1 (review LOW): the people of squad k kept here with no recorded hand - ghosts no re-wake can show re-made */
static long long KeptGhostOf(const HoKey& k)
{
    std::set<unsigned int> u;
    const std::map<HoKey, KeptRecent>::const_iterator kr = g_keptRecent.find(k);
    if (kr != g_keptRecent.end()) u.insert(kr->second.uids.begin(), kr->second.uids.end());
    const std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.find(k);
    if (s != g_squadIdx.end()) { u.insert(s->second.keptHere.begin(), s->second.keptHere.end()); u.insert(s->second.asleepHere.begin(), s->second.asleepHere.end()); }
    long long n = 0;
    for (std::set<unsigned int>::const_iterator i = u.begin(); i != u.end(); ++i)
    {
        const std::map<unsigned int, std::string>::const_iterator h = g_keptHandOf.find(*i);
        if (h == g_keptHandOf.end() || h->second.empty()) ++n;
    }
    return n;
}
/* a5 fold 1 (review LOW): counted and logged once per squad per wake (wakeAp: the woken squad; the sweep meets every re-made body) */
static void KeptStayLine(const HoKey& k, const void* wakeAp, const std::string& id, long long stay, long long remade, long long listed)
{
    if (stay <= 0) { g_keptStayAp.erase(k); return; }
    const std::map<HoKey, const void*>::const_iterator seen = g_keptStayAp.find(k);
    if (seen != g_keptStayAp.end() && seen->second == wakeAp) return;
    if (g_keptStayAp.size() >= 4096) g_keptStayAp.clear();
    g_keptStayAp[k] = wakeAp;
    const long long ghost = KeptGhostOf(k);
    ++g_wwKeptStay; g_wwKeptGhost += ghost;
    if (g_wwKeptStayLines >= 40) return;
    ++g_wwKeptStayLines;
    RelLog("[RELEASE] squad id='" + id + "' re-woken here: kept " + N(stay) + " stay - not re-made by the engine's list (" + N(ghost) + " of them with no hand recorded; " + N(remade) + " re-made, " + N(listed)
           + " living listed; T-1318 attempt 5; worldWhole8 keptStay " + N(g_wwKeptStay) + ", keptGhost " + N(g_wwKeptGhost) + ", keptRemade " + N(g_wwKeptRemade) + "; one per squad per wake, the first 40)");
}
/* the sweep met a rebuilt person of squad (faction, id) - this game's engine re-created it from its own world data: the squad's asleepHere
   people are live here again (they leave the index), and an OPEN release of its people is CANCELLED (design 2.5 item 7: the engine runs
   them again under fresh uids - un-adopted members settle asleep-superseded, their UNLOAD goes, a late adopter is revoked). */
/* T-1318 a4 fold 1 (review LOW-1): personAp - the met person's awake engine squad; the same squad its held put-aways came from (never
   slept: cooplo::RebuiltIsRewake 0) is no re-wake - nothing changes (counted worldWhole6 rebuiltSameSquad). */
void HandedOverLeftBehindRebuilt(void* faction, const std::string& id, const void* personAp)
{
    ReleaseForgetDrain();
    RelWalk walk;   /* [a1b2f1-hd10] [review F1]: its own walk of g_release and the index entry below */
    const HoKey k(faction, id);
    {
        const std::map<HoKey, RelHeld>::const_iterator sh = g_relHeldSq.find(k);
        if (sh != g_relHeldSq.end() && cooplo::RebuiltIsRewake(sh->second.b.ap, personAp) == 0) { ++g_wwRebuiltSameSquad; return; }
    }
    long long cancelledNow = 0;
    for (std::map<unsigned int, ReleaseRec>::iterator r = g_release.begin(); r != g_release.end(); )
    {
        std::vector<RelMember> out;
        for (size_t m = 0; m < r->second.mem.size(); )
        {
            if (r->second.mem[m].hasKey != 0 && r->second.mem[m].key == k) { out.push_back(r->second.mem[m]); r->second.mem.erase(r->second.mem.begin() + (long)m); }
            else ++m;
        }
        if (out.empty()) { ++r; continue; }
        /* [a1b2f2-hd2] [review G1]: cancelledNow counts the members actually cancelled (one a late adopter took by the area rule is given) */
        ReleaseRec part(r->second); part.mem = out;
        const bool ended = r->second.mem.empty();
        if (ended) g_release.erase(r++); else ++r;
        if (!ended) part.nAdopted = part.nAsleep = part.nCancelled = part.nMoved = 0;
        cancelledNow += RelSettleAll(part, 1, ended ? std::string("CANCELLED - this game's engine re-woke the squad (fresh uids)") : std::string("CANCELLED in part - this game's engine re-woke the squad; the rest stay offered"), ended ? 1 : 0);
    }
    /* [a1b2f1-hd17] [review F2]: a person of the squad only in this game's world data (asleep, kept, or just cancelled) -> that part of the
       entry goes: the re-woken people are adopted by the sweep, never noted as orphans and deleted. T-1318 attempt 3 (owner 1615 a;
       F-T2324-WHOLEKEPT): the GIVEN part stays recorded as given (cooplo::SquadRebuiltAction) - it went too before, and a re-made body of a
       given person was then adopted and offered again (T2315's road); the sweep still skips such a body by its hand (HandoffPersonGiven) */
    {   /* T-1318 attempt 3c: the squad's held-undecided people - the engine runs the squad again: CANCELLED, never offered (kept here, cleared below) */
        std::map<HoKey, RelHeld>::iterator hh = g_relHeldSq.find(k);
        if (hh != g_relHeldSq.end())
        {
            ReleaseRec part; part.id = RelNewId(); part.keyUid = hh->second.b.keyUid; part.sector = hh->second.b.sector; part.px = hh->second.b.px; part.pz = hh->second.b.pz; part.mem = hh->second.b.mem;
            g_relHeldSq.erase(hh);
            g_relSent += (long long)part.mem.size(); g_wwDroppedOnWake += (long long)part.mem.size();
            cancelledNow += RelSettleAll(part, 1, "CANCELLED - held until the squad sleeps, but this game's engine re-woke the squad (fresh uids) (T-1318 attempt 3c; worldWhole4 droppedOnWake=" + N(g_wwDroppedOnWake) + ")", 1);
        }
    }
    /* T-1318 attempt 5 (owner 1626, D1-R1): the re-wake re-made only the people its engine list holds - the woken squad's LIVING members'
       hands (read as HandoffMembersTakenBy reads them). A kept / asleep person leaves only when its hand (read when it was kept) is among
       them; one with no hand, or not listed, stays kept (cooplo::KeptRemadeByHands) and the entry goes only when nobody is left. Before,
       every kept person went here on the false assumption 'a re-wake re-made the whole squad', and the whole test then offered the rest as
       whole (T2359: five squads, Herbivore_9's kept 2 - 0 of them in the engine's list). An unread list re-made nobody. */
    std::set<std::string> woken;
    {
        SquadView wv;
        if (personAp != 0 && Plaus(personAp) && ReadSquadAt((void*)personAp, &wv) == 1)
            for (int i = 0; i < wv.stored && i < kSquadViewCap; ++i)
            {
                ::Character* m = wv.members[i];
                if (m == 0 || !Plaus(m) || BodyDeadFlag(m) == 1) continue;
                ObjId oid; if (CaptureObjId(m, &oid)) woken.insert(cooplo::HandKey(oid.serial, oid.type));
            }
    }
    long long stay = 0, remadeNow = 0;
    {
        std::map<HoKey, KeptRecent>::iterator kr = g_keptRecent.find(k);   /* kept people of it not yet indexed */
        if (kr != g_keptRecent.end())
        {
            std::set<unsigned int> rk;
            cooplo::KeptRemadeByHands(kr->second.uids, g_keptHandOf, woken, &rk);
            for (std::set<unsigned int>::const_iterator u = rk.begin(); u != rk.end(); ++u) { kr->second.uids.erase(*u); g_keptHandOf.erase(*u); }
            remadeNow += (long long)rk.size(); stay += (long long)kr->second.uids.size();
            if (kr->second.uids.empty()) g_keptRecent.erase(kr);
        }
    }
    std::map<HoKey, SquadIdx>::iterator s = g_squadIdx.find(k);
    if (s == g_squadIdx.end()) { g_wwKeptRemade += remadeNow; KeptStayLine(k, personAp, id, stay, remadeNow, (long long)woken.size()); return; }
    std::set<unsigned int> ra, rk;
    cooplo::KeptRemadeByHands(s->second.asleepHere, g_keptHandOf, woken, &ra);
    cooplo::KeptRemadeByHands(s->second.keptHere, g_keptHandOf, woken, &rk);
    const int act = cooplo::SquadRebuiltAction((int)s->second.given.size(), (int)ra.size(), (int)rk.size(), cancelledNow > 0 ? 1 : 0);
    const long long ng = (long long)s->second.given.size(), na = (long long)ra.size(), nk = (long long)rk.size();
    for (std::set<unsigned int>::const_iterator a = ra.begin(); a != ra.end(); ++a) { s->second.asleepHere.erase(*a); IdxDropUid(*a); g_keptHandOf.erase(*a); }
    for (std::set<unsigned int>::const_iterator a = rk.begin(); a != rk.end(); ++a) { s->second.keptHere.erase(*a); g_keptHandOf.erase(*a); }
    remadeNow += na + nk; g_wwKeptRemade += remadeNow;
    stay += (long long)(s->second.asleepHere.size() + s->second.keptHere.size());
    if (act == cooplo::kSrKeep) { KeptStayLine(k, personAp, id, stay, remadeNow, (long long)woken.size()); return; }
    const int dropEntry = (act == cooplo::kSrDropEntry && s->second.asleepHere.empty() && s->second.keptHere.empty()) ? 1 : 0;
    if (dropEntry != 0) { IdxEntryGone(s->first, s->second); g_squadIdx.erase(s); }   /* T-1318 a5 fold 1 */
    else { ++g_idxSeq; if (act == cooplo::kSrDropOwn) ++g_wwRebuiltKeptGiven; }   /* T-1318 attempt 3: the given people (and their hands) stay; attempt 5: and the kept people not re-made */
    g_idxAsleepRebuilt += na; ++g_idxRebuiltDropped;
    RelLog("[RELEASE] squad id='" + id + "' - the sweep met a person of it this game's engine re-woke: " + std::string(dropEntry != 0 ? "its index entry goes" : (act == cooplo::kSrDropOwn ? "its re-made own part of the index entry goes, its GIVEN part stays" : "its re-made own part of the index entry goes, its kept part stays"))
           + " (given " + N(ng) + (act == cooplo::kSrDropOwn ? " kept as given" : "") + ", asleep " + N(na) + ", kept " + N(nk) + " re-made by the engine's list, " + N(stay) + " stay kept, cancelled now " + N(cancelledNow)
           + ") - its re-woken people are adopted here (indexMore asleepRebuilt " + N(g_idxAsleepRebuilt) + ", rebuiltDropped " + N(g_idxRebuiltDropped) + "; worldWhole3 rebuiltKeptGiven " + N(g_wwRebuiltKeptGiven) + ")");
    KeptStayLine(k, personAp, id, stay, remadeNow, (long long)woken.size());
}
void HandoffNoteWokeWithGiven(void* faction, const std::string& id)
{
    ReleaseForgetDrain();
    const HoKey k(faction, id);
    if (g_wokeWithGiven.count(k) != 0) return;
    if (g_wokeWithGiven.size() >= 4096) { ++g_wokeWithGivenFull; return; }
    g_wokeWithGiven.insert(k); ++g_wokeWithGivenNoted;
}
int HandoffWokeWithGiven(void* faction, const std::string& id)
{
    ReleaseForgetDrain();
    return g_wokeWithGiven.count(HoKey(faction, id)) != 0 ? 1 : 0;
}
bool HandedOverLeftBehindAny()
{
    ReleaseForgetDrain();
    if (!g_release.empty()) return true;   /* an open release may need cancelling (HandedOverLeftBehindRebuilt) */
    for (std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.begin(); s != g_squadIdx.end(); ++s) if (!s->second.asleepHere.empty() || !s->second.keptHere.empty()) return true;
    if (!g_relHeldSq.empty()) return true;   /* T-1318 attempt 3c: held-undecided people - their squad's re-wake drops them */
    if (!g_keptRecent.empty()) return true;   /* T-1318 attempt 3: kept people (indexed or not) hold the squad's XFER (KeptAsleepOf) - their re-wake must clear them */
    return false;
}
/* MAIN THREAD - a player left: the squads this game handed to that player are dropped from the index at the next drain
   (ReleaseForgetDrain); every other player's stay. slot < 0 (the old link's peer that never announced its slot): the whole index. */
void HandedOverForgetPlayer(int slot)
{
    if (slot < 0) { ::InterlockedExchange(&g_idxPeerGoneReq, 1); return; }
    for (size_t i = 0; i < g_idxGoneSlots.size(); ++i) if (g_idxGoneSlots[i] == slot) return;
    g_idxGoneSlots.push_back(slot);
}
void HandoffForgetWorldRequest()
{
    ::InterlockedExchange(&g_relForgetReq, 1);
}
/* fold 2 [a1b2f2-hd4] [review G2]: MAIN THREAD - a player left (the session peer's down edge, or PLAYER_GONE): the REVOKEs waiting FROM it and
   owed TO it go at the next drain (ReleaseForgetDrain). An unknown slot (-1) drops nothing (counted); the world teardown still clears all. */
void HandoffRevokesPlayerGone(int slot)
{
    if (slot < 0) { ++g_relRevokeGoneSlotUnknown; return; }
    for (size_t i = 0; i < g_relRevokeGoneSlots.size(); ++i) if (g_relRevokeGoneSlots[i] == slot) return;
    g_relRevokeGoneSlots.push_back(slot);
}
/* [a1b2-hd14] the [net] REPORT tokens of design 5 row 2 (they replace putAway[, putAwayFold[, putAwayBounce[, handedOver[, handedOverFold[,
   handoverOnce[, putAwayCover[, unloadHeld[, owedFold[, handoverOwed[) */
std::string HandoffReleaseReportToken()
{
    ReleaseForgetDrain();
    long long given = 0, asleep = 0;
    for (std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.begin(); s != g_squadIdx.end(); ++s) { given += (long long)s->second.given.size(); asleep += (long long)s->second.asleepHere.size(); }
    return "release[sent,adopted,dropped,deferred,asleep,resent,revoked,cancelled]=" + N(g_relSent) + "," + N(g_relAdopted) + "," + N(g_relDropped) + "," + N(g_relDeferred)
           + "," + N(g_relAsleep) + "," + N(g_relResent) + "," + N(g_relRevoked) + "," + N(g_relCancelled) + " releaseOpen=" + N((long long)g_release.size())
           + " releaseMore[offers,held,next,lateAccepted,ackWrongSender,ackStale,ackUnknown,dupAck,notMine,worldCleared,logged]=" + N(g_relOffers) + "," + N(g_relHeld)
           + "," + N(g_relNext) + "," + N(g_relLateAccepted) + "," + N(g_relAckWrongSender) + "," + N(g_relAckStale) + "," + N(g_relAckUnknown) + "," + N(g_relDupAck) + "," + N(g_relNotMine)
           + "," + N(g_relWorldCleared) + "," + N(g_relLogged)
           + " releaseHolder[offer,none,hold,keep,keepUnknown,leave,notInWorld,backoff]=" + N(g_holderOffer) + "," + N(g_holderNone) + "," + N(g_holderHold) + "," + N(g_holderKeep)
           + "," + N(g_holderKeepUnknown) + "," + N(g_holderLeave) + "," + N(g_holderNotInWorld) + "," + N(g_holderBackoff)
           + " releaseSkip[inFlight,offThread,unread,player,notAnnounced,yield,reloadRetire]=" + N(g_relSkipInFlight) + "," + N(g_relSkipOffThread) + "," + N(g_relSkipUnread) + "," + N(g_relSkipPlayer) + "," + N(g_relSkipNotAnnounced) + "," + N(g_relSkipYield) + "," + N(g_relSkipReload)
           + " releaseFlight[notTaken,abandoned,sameIdUnload]=" + N(g_relFlightNotTaken) + "," + N(g_relFlightAbandoned) + "," + N(g_relFlightSameId)
           + " sameIdWithdrawn[marked,full,open]=" + N(g_sameIdWdMarked) + "," + N(g_sameIdWdFull) + "," + N((long long)g_sameIdWd.size())
           + " releaseIn[offers,adopted,dup,dropped,stale,deferred,revokes,puppeted,removed,revokeIgnored]=" + N(g_relInOffers) + "," + N(g_relInAdopted) + "," + N(g_relInDup) + "," + N(g_relInDropped)
           + "," + N(g_relInStale) + "," + N(g_relInDeferred) + "," + N(g_relInRevokes) + "," + N(g_relInPuppeted) + "," + N(g_relInRemoved) + "," + N(g_relInRevokeIgnored)
           + " index[given,asleepHere,noKey,givenClearedByOwnerUnload]=" + N(g_idxGiven) + "," + N(g_idxAsleepHere) + "," + N(g_idxNoKey) + "," + N(g_idxClearedByOwnerUnload) + " givenKeptOnAnnounce=" + N(g_idxKeptOnAnnounce)
           + " indexMore[clearedByOwnerDespawn,takenBack,squadCleared,asleepRebuilt,peerGoneCleared]=" + N(g_idxClearedByOwnerDespawn) + "," + N(g_idxTakenBack) + "," + N(g_idxSquadCleared)
           + "," + N(g_idxAsleepRebuilt) + "," + N(g_idxPeerGoneCleared)
           + " indexPlayerGone[squads,people]=" + N(g_idxPlayerGoneSquads) + "," + N(g_idxPlayerGonePeople)
           + " indexNow[squads,given,asleepHere]=" + N((long long)g_squadIdx.size()) + "," + N(given) + "," + N(asleep)
           + " idxDiscard[dropped,deferred]=" + N(g_idxDiscardDropped) + "," + N(g_idxDiscardDeferred)   /* T-1046 attempt 2 */
           + " passHold[held,empty]=" + N(g_passHeld) + "," + N(g_passHeldEmpty)
           + " releaseFold1[revokeOwedAdded,revokeRetrySent,revokeOwedNow,inRevokeWaited,inRevokeWaitApplied,inRevokeWaitNow,capDropped,rewokeAtAck,rewokeAtSettle,skipNoKey,flightNoKey,puppetNoBody,drainDeferred]="
           + N(g_relRevokeOwedAdded) + "," + N(g_relRevokeRetrySent) + "," + N((long long)g_relRevokeOwed.size()) + "," + N(g_relInRevokeWaited) + "," + N(g_relInRevokeWaitApplied)
           + "," + N((long long)g_relRevokeWait.size()) + "," + N(g_relRevokeCapDropped) + "," + N(g_relRewokeAtAck) + "," + N(g_relRewokeAtSettle) + "," + N(g_relSkipNoKey)
           + "," + N(g_relFlightNoKey) + "," + N(g_puppetNoBody) + "," + N(g_relDrainDeferred)
           + " indexFold1[kept,keptUnknown,rebuiltDropped,scans,keptRecentNow,keyCacheNow]=" + N(g_idxKept) + "," + N(g_idxKeptUnknown) + "," + N(g_idxRebuiltDropped)
           + "," + N(g_idxScans) + "," + N((long long)g_keptRecent.size()) + "," + N((long long)g_keyCache.size())   /* [a1b2f1-hd18] */
           + " releaseFold2[rewokeAcceptAtAck,rewokeLateAccepted,rewokeHolderUnknown,revokeWaitWorldCleared,revokeOwedWorldCleared,revokeWaitPlayerGone,revokeOwedPlayerGone,goneSlotUnknown]="
           + N(g_relRewokeAcceptAtAck) + "," + N(g_relRewokeLateAccepted) + "," + N(g_relRewokeHolderUnknown) + "," + N(g_relRevokeWaitWorldCleared) + "," + N(g_relRevokeOwedWorldCleared)
           + "," + N(g_relRevokeWaitPlayerGone) + "," + N(g_relRevokeOwedPlayerGone) + "," + N(g_relRevokeGoneSlotUnknown)   /* [a1b2f2-hd7] */
           + " rewakeWait[put,decided,stillNobody,notMine,worldCleared,now]=" + N(g_relRewakeWaitPut) + "," + N(g_relRewakeWaitDecided) + "," + N(g_relRewakeWaitStillNobody)
           + "," + N(g_relRewakeWaitNotMine) + "," + N(g_relRewakeWaitWorldCleared) + "," + N((long long)g_relRewakeWait.size())   /* T-1268 fold 1 (review M2) */
           + " rewakeWait2[lateAdded,playerGone]=" + N(g_relRewakeWaitLateAdded) + "," + N(g_relRewakeWaitPlayerGone)   /* T-1268 fold 2 */
           + " givenNotOffered=" + N(g_relGivenNotOffered)   /* T-1318 fold 2 (re-check B1) */
           + " worldWhole[relKeptOut,relOver64,xferHeldPart,relAll,relNone,relNoneLeaderless,xferAll,xferNone,xferNoneLeaderless,givenAsleepKept,partGiven]="
           + N(g_wwRelKeptOut) + "," + N(g_wwRelOver64) + "," + N(g_wwXferHeldPart) + "," + N(g_wwRelAll) + "," + N(g_wwRelNone) + "," + N(g_wwRelNoneLeaderless)
           + "," + N(g_wwXferAll) + "," + N(g_wwXferNone) + "," + N(g_wwXferNoneLeaderless) + "," + N(g_wwGivenAsleepKept) + "," + N(g_wwPartGiven)   /* T-1318 attempt 2 (D3): partGiven must stay 0 */
           + " worldWhole3[xferKeptAsleep,followKeptAsleep,rebuiltKeptGiven,rewokeNotPerson]=" + N(g_wwXferKeptAsleep) + "," + N(g_wwFollowKeptAsleep) + "," + N(g_wwRebuiltKeptGiven) + "," + N(g_wwRewokeNotPerson)
           + " worldWhole4[heldBatches,heldPeople,offeredTogether,settledAfterHold,droppedOnWake,heldNotMine,heldNow]=" + N(g_wwHeldBatches) + "," + N(g_wwHeldPeople) + "," + N(g_wwOfferedTogether)
           + "," + N(g_wwSettledAfterHold) + "," + N(g_wwDroppedOnWake) + "," + N(g_wwHeldNotMine) + "," + N((long long)g_relHeldSq.size())
           + " worldWhole5[batchesMerged,heldOpen,heldGivenSettled]=" + N(g_relBatchesMerged) + "," + N(g_wwHeldOpen) + "," + N(g_wwHeldGivenSettled)   /* T-1318 attempt 3 fold 2 (re-check MED-A) */
           + " worldWhole6[recordsRead,recordsUnread,relUnaccKeptOut,relUnaccHeld,xferUnacc,followUnacc,rebuiltSameSquad,supersedeKeptNotGiven,wokeWithGiven,wokeWithGivenFull,deadHandSquads]=" + N(g_wwRecRead) + "," + N(g_wwRecUnread)
           + "," + N(g_wwRelUnaccKeptOut) + "," + N(g_wwRelUnaccHeld) + "," + N(g_wwXferUnacc) + "," + N(g_wwFollowUnacc) + "," + N(g_wwRebuiltSameSquad) + "," + N(CtxSupersedeKeptNotGiven())
           + "," + N(g_wokeWithGivenNoted) + "," + N(g_wokeWithGivenFull) + "," + N((long long)g_deadHandsOf.size())   /* T-1318 attempt 4 (design A), a4 fold 1 */
           + " worldWhole7[recDeadSkipped,recDeadUnread,recUnreadHeld,recUnreadRead,recUnreadGone,recUnreadCap]=" + N(g_wwRecDeadSkipped) + "," + N(g_wwRecDeadUnread) + "," + N(g_wwRecHold)
           + "," + N(g_wwRecHoldRead) + "," + N(g_wwRecHoldGone) + "," + N(g_wwRecHoldCap)   /* T-1318 a4 fold 2 (re-check LOW-A / LOW-B) */
           + " worldWhole8[keptStay,keptRemade,keptGhost,otherHands,otherHandSquads,otherHandsCapHit,copyJoinGiven,copyJoinCtxGiven,dupKeptGiven]=" + N(g_wwKeptStay) + "," + N(g_wwKeptRemade) + "," + N(g_wwKeptGhost)
           + "," + N(g_wwOtherHandsNoted) + "," + N((long long)g_otherHandsOf.size()) + "," + N(g_wwOtherHandsCapHit) + "," + N(CopyJoinGivenCount()) + "," + N(CopyJoinCtxGivenCount()) + "," + N(WakeDupKeptGivenCount())   /* T-1318 attempt 5 (owner 1626, D1) + fold 1 */
           + " worldWhole9[copyJoinSkippedIgnored]=" + N(CopyJoinSkippedIgnoredCount())   /* owner 1640: ignored repeat SPAWNs, no placement read */
           + " worldWhole2[followHeldPart,partGivenAwake,keptUnknown,ownKeptAsleep,ownKeptHandUnread]=" + N(g_wwFollowHeldPart) + "," + N(g_wwPartGivenAwake) + "," + N(g_idxKeptUnknown)
           + "," + N(g_wwOwnKeptAsleep) + "," + N(g_wwOwnKeptHandUnread);   /* T-1318 attempt 2 fold 1 (review): keptUnknown = indexFold1's keptUnknown (no squad can show those) */
}

/* the announce pass's two inputs (handoff.h) */
int HandoffOtherInWorldHasArea(float x, float z)
{
    std::vector<int> sl(256), bit(256);
    int holder = -1, n = 0;
    if (AreaSquadViewTS(SectorOf(x, z), &holder, &sl[0], &bit[0], (int)sl.size(), &n) != 1) return -1;
    const int me = StoreMySlot();
    for (int i = 0; i < n; ++i)
        if (sl[(size_t)i] != me && bit[(size_t)i] == 1 && StoreRosterSlotInWorld(sl[(size_t)i]) == 1) return 1;
    return 0;
}
long long g_keepRcvSkipped = 0, g_keepRcvAsked = 0;   /* T-1046 fix 2 review fold: receiver tests skipped (no other game has the area loaded) / run */
/* T-1046 fix 2 (owner 1591 a): THE KEEP QUESTION'S RECEIVER TEST - the RELEASE's own (ReceiversFor: every other IN_WORLD game with the
   area loaded whose engine would keep the squad at (px, pz), cooplo::EngineKeepsAt with kKeepMarginUnits), asked with nobody tried, so
   "kept for another game" always means "a game the put-away would offer it to". Returns ReceiversFor's k (-1 hold, 0 nobody, >= 1);
   *namesSlot (may be 0) = 1 when wantSlot (>= 0) is among the games named; *holderOut (may be 0) = the area's holder the map names. MAIN THREAD. */
int HandoffKeepReceivers(float px, float pz, int wantSlot, int* namesSlot, int* holderOut)
{
    if (namesSlot != 0) *namesSlot = 0;
    if (holderOut != 0) *holderOut = -1;
    if (cooplo::KeepReceiversKnownNone(StoreLiveReady() ? 1 : 0, StoreMySlot(), OtherLoadedTS(SectorOf(px, pz))) != 0) { ++g_keepRcvSkipped; return 0; }   /* review fold (cost): no other game has the area loaded - k is 0 */
    ++g_keepRcvAsked;
    std::vector<int> order;
    int holder = -1, niw = 0, nkp = 0;
    const int k = ReceiversFor(SectorOf(px, pz), px, pz, std::vector<int>(), &order, &holder, &niw, &nkp);
    if (holderOut != 0) *holderOut = holder;
    if (k >= 1 && wantSlot >= 0 && namesSlot != 0)
        for (size_t i = 0; i < order.size(); ++i) if (order[i] == wantSlot) { *namesSlot = 1; break; }
    return k;
}
void HandoffKeepReceiverCounts(long long* skipped, long long* asked) { *skipped = g_keepRcvSkipped; *asked = g_keepRcvAsked; }
/* T-1046 fold 2 (T2305 leg N): the sleeping squad (faction, id) is TAKEN by one slot when every person of its own records (hands =
   one per 0x24 record of its sleeping container) is in the squad index's given to that slot - person by person (cooplo::SleepersTakenSlot);
   people kept here or given who are not among the sleepers do not block it (SquadWholeGiven, for its other callers, is unchanged).
   -1 = not taken. HandoffSquadHasGiven: the index has the squad with at least one given person (the cheap test first). MAIN THREAD. */
int HandoffSquadHasGiven(void* faction, const std::string& id)
{
    ReleaseForgetDrain();
    std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.find(HoKey(faction, id));
    return (s != g_squadIdx.end() && !s->second.given.empty()) ? 1 : 0;
}
/* T-1046 attempt 2 (design a', manager 2026-11-14; T2369): THE SQUAD'S KEPT PART - the people of squad (faction, id) this game keeps
   un-offered: the index's keptHere and asleepHere and the not-yet-indexed kept list (g_keptRecent), each person once; NOT the held,
   undecided put-aways (g_relHeldSq - their first offer is still to come). While it is > 0 whole or none refuses every later offer of the
   squad (KeptAsleepOf), so the keep question gives the engine's own answer (squadbirth.h OneKeepRule). MAIN THREAD. */
int HandoffSquadKeptPart(void* faction, const std::string& id)
{
    ReleaseForgetDrain();
    const HoKey k(faction, id);
    std::set<unsigned int> u;
    const std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.find(k);
    if (s != g_squadIdx.end()) { u.insert(s->second.keptHere.begin(), s->second.keptHere.end()); u.insert(s->second.asleepHere.begin(), s->second.asleepHere.end()); }
    const std::map<HoKey, KeptRecent>::const_iterator kr = g_keptRecent.find(k);
    if (kr != g_keptRecent.end()) u.insert(kr->second.uids.begin(), kr->second.uids.end());
    return (int)u.size();
}
/* T-1046 attempt 2 fold 1 (review MEDIUM): the engine discarded the squad of world id `id` on a road that no longer has its faction -
   the held delete's settle (store.cpp RunHeldSettle: T2369's every discard) and the off-thread destroy's drain (DrainGoneEntry, its platoon
   freed). The index key's id is the squad's world id (SquadKeyOf: StoreWorldIdOf), and a world id names its faction ("<faction>_<n>",
   SplitGroupId), so every key of this id is that one squad's: each one's kept part goes as at HandedOverUnmark why 2 (IdxDiscardKey -
   under a live walk at the next drain). No ReleaseForgetDrain here: it is called from inside store.cpp's walk of its held deletes
   (RunHeldSettle), and a drain from there could reach the release's settle roads; the index read below needs none. MAIN THREAD. */
void HandoffSquadDiscardedId(const std::string& id)
{
    std::set<HoKey> keys;
    for (std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.begin(); s != g_squadIdx.end(); ++s) if (s->first.second == id) keys.insert(s->first);
    for (std::map<HoKey, KeptRecent>::const_iterator r = g_keptRecent.begin(); r != g_keptRecent.end(); ++r) if (r->first.second == id) keys.insert(r->first);
    for (std::set<HoKey>::const_iterator k = keys.begin(); k != keys.end(); ++k) IdxDiscardKey(*k);
}
/* T-1318: the index has a squad of this id (any faction) with at least one given person - the given-wait's test (store.cpp
   GivenWaitTick: a squad whose copy here was refused its wake and removed is built from its record once nothing of it is given). MAIN THREAD. */
int HandoffSquadHasGivenId(const std::string& id)
{
    ReleaseForgetDrain();
    for (std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.begin(); s != g_squadIdx.end(); ++s)
        if (s->first.second == id && !s->second.given.empty()) return 1;
    return 0;
}
/* T-1318 fold 1 (review F2): one person of a squad, by its own hand (the engine's handle, as the ACK's given entry and the sleeping
   records carry it) - 1 = the squad index has that person given, 0 = not, -1 = its hand did not read. The sweep's person-by-person
   test: a rebuilt person of a squad given in part is skipped when IT is given; the squad's people kept here are not. MAIN THREAD. */
int HandoffPersonGiven(void* faction, const std::string& id, const void* character)
{
    ReleaseForgetDrain();
    std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.find(HoKey(faction, id));
    if (s == g_squadIdx.end() || s->second.given.empty()) return 0;
    ObjId oid;
    if (!Plaus(character) || !CaptureObjId(character, &oid)) return -1;
    const std::string hand = cooplo::HandKey(oid.serial, oid.type);
    int handless = 0;
    for (std::map<unsigned int, unsigned int>::const_iterator g = s->second.given.begin(); g != s->second.given.end(); ++g)
    {
        std::map<unsigned int, std::string>::const_iterator h = g_givenHandOf.find(g->first);
        if (h != g_givenHandOf.end() && h->second == hand) return 1;
        if (h == g_givenHandOf.end() || h->second.empty()) handless = 1;
    }
    /* T-1318 attempt 4 (design A, site 9): a given person whose hand was never read could be this one - not known (-1), never "not given":
       the sweep then numbers a re-made body of a wholly given squad only when every given hand is known (coopsquad::SweepGivenPerson) */
    return handless != 0 ? -1 : 0;
}
/* T-1318 fold 1 (review F2): the slot that took every person of an AWAKE squad - its members' own hands (n of them, read from the bodies)
   through the sleeping squad's test (cooplo::SleepersTakenSlot); -1 = not taken, or a member's hand did not read. The off-thread wake's
   drain asks it: the wake already built the bodies, so the sleeping records may no longer read. MAIN THREAD. */
/* T-1318 attempt 4 (owner 1626, design A, site 7): over the squad's LIVING members (a corpse left out by the engine's dead flag; an
   unreadable flag counted alive); a member that does not read, or no living member: coopsquad::kTakenUnread (-2 - the index's whole answer
   then stands); read and not all given to one slot: -1. */
/* T-1318 attempt 5 (owner 1626, D1-R3): another game's living copy put away by this engine (spawn.cpp's despawn hook, 'this engine put the
   copy away', the body whole): its hand is noted under its squad with its owner slot, when that squad has people this game gave away - the
   sleeping squad's records then count it as taken by that game (HandoffSleepersTakenBy). MAIN THREAD. */
void HandoffNoteOtherCopyPutAway(unsigned int uid, const void* obj)
{
    ReleaseForgetDrain();
    if (uid == 0 || net::IsUidMine(uid) || !Plaus(obj)) return;
    ::Character* c = (::Character*)obj;
    if (BodyDeadFlag(c) == 1) return;
    void* f = 0; std::string id;
    if (!SquadKeyOf(c, &f, &id) || id.empty()) return;
    const std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.find(HoKey(f, id));
    if (s == g_squadIdx.end() || s->second.given.empty()) return;
    ObjId oid;
    if (!CaptureObjId(c, &oid)) return;
    OtherHandNote(HoKey(f, id), cooplo::HandKey(oid.serial, oid.type), uid);
}
int HandoffMembersTakenBy(void* faction, const std::string& id, void* const* members, int n)
{
    if (members == 0 || n <= 0 || n > 256) return coopsquad::kTakenUnread;
    std::vector<std::string> hands;
    for (int i = 0; i < n; ++i)
    {
        ObjId oid;
        if (!Plaus(members[i])) return coopsquad::kTakenUnread;
        if (BodyDeadFlag(members[i]) == 1) continue;
        if (!CaptureObjId(members[i], &oid)) return coopsquad::kTakenUnread;
        hands.push_back(cooplo::HandKey(oid.serial, oid.type));
    }
    if (hands.empty()) return coopsquad::kTakenUnread;
    return HandoffSleepersTakenBy(faction, id, hands);
}
int HandoffSleepersTakenBy(void* faction, const std::string& id, const std::vector<std::string>& hands)
{
    ReleaseForgetDrain();
    std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.find(HoKey(faction, id));
    if (s == g_squadIdx.end()) return -1;
    std::map<std::string, int> slotOfHand;
    int handless = 0;   /* T-1318 a4 fold 1 (review MEDIUM-1): a given person whose hand was never recorded - a record not found could be it (-2) */
    for (std::map<unsigned int, unsigned int>::const_iterator g = s->second.given.begin(); g != s->second.given.end(); ++g)
    {
        std::map<unsigned int, std::string>::const_iterator h = g_givenHandOf.find(g->first);
        if (h != g_givenHandOf.end() && !h->second.empty()) slotOfHand[h->second] = net::PlayerSlotOfKey(g->second);
        else handless = 1;
    }
    {   /* T-1318 attempt 5 (owner 1626, D1-R3): another game's copies seen in the squad count as taken by that game, never as 'not given' */
        const std::map<HoKey, std::map<std::string, unsigned int> >::const_iterator oh = g_otherHandsOf.find(HoKey(faction, id));
        if (oh != g_otherHandsOf.end())
        {   /* a5 fold 1 (review MEDIUM-2 / LOW): each uid read NOW - taken back here or no owner record: skipped; else its owner's player slot
               (the given hands' own source, net::PlayerSlotOfKey) */
            std::map<std::string, int> otherNow;
            for (std::map<std::string, unsigned int>::const_iterator h = oh->second.begin(); h != oh->second.end(); ++h)
            {
                const int sl = cooplo::OtherHandSlotNow(net::IsUidMine(h->second) ? 1 : 0, net::OwnerPlayerSlotOf(h->second));
                if (sl != cooplo::kOtherHandSkip) otherNow[h->first] = sl;
            }
            cooplo::SlotOfHandAddOther(&slotOfHand, otherNow);
        }
    }
    const std::map<HoKey, std::set<std::string> >::const_iterator dh = g_deadHandsOf.find(HoKey(faction, id));   /* its corpses: neither given nor kept */
    return cooplo::SleepersTakenSlot(hands, slotOfHand, handless, dh != g_deadHandsOf.end() ? &dh->second : 0);
}
long long HandoffIdxSeq() { return g_idxSeq; }
/* the position the put-away's RELEASE decides an awake squad at (SquadDecisionPosAt over the squad at its ActivePlatoon): 1 read, else 0 */
int HandoffSquadKeepPos(void* activePlatoon, float* x, float* z)
{
    SquadView sv;
    if (!Plaus(activePlatoon) || ReadSquadAt(activePlatoon, &sv) != 1) return 0;
    return SquadDecisionPosAt(sv, x, z) == 1 ? 1 : 0;
}
bool HandoffForcedRefusedFor(unsigned int uid)
{
    std::map<unsigned int, unsigned int>::iterator u = g_forcedRefusedUid.find(uid);
    if (u == g_forcedRefusedUid.end()) return false;
    std::map<unsigned int, ForcedTry>::const_iterator f = g_forcedTry.find(u->second);
    if (f != g_forcedTry.end() && f->second.refused != 0) return true;
    g_forcedRefusedUid.erase(u);   /* the back-off ended (the squad's sector or receiver changed): it may be offered again */
    return false;
}

/* A FINAL LEAVE'S NPC (net/session.cpp OnPlayerGone). HandoffGoneFirstReceiver: the area receiver order for this
   game's copy of uid, asked in the leaver's place (liveowner.h AreaReceiverOrder: every other in-world game with the area loaded whose
   engine would keep the NPC at its spot, the holder first, then the lower slot - this game among them). Returns k >= 1 (*firstOut =
   the first slot), 0 = no game has the area loaded, -1 = no answer now (no readable copy, no slot of this game's own, no fresh map).
   HandoffTakeGone: this game runs the copy from now on - the take a hand-over's receiver makes: the owner record at a gen above every
   record here, the taker's OWNER_MOVED to every other game, the copy's later withdrawal owed by this game, the local AI drives it.
   MAIN THREAD, engine writes allowed. */
/* The order is decided at the squad's decision position (SquadDecisionPos - the leader's squad, as the live hand-over decides), so
   every member of a squad reads one order and gets one verdict; a copy with no readable squad is decided at its own spot. `tried`:
   the receivers already passed over (they did not take it in time) - the order is asked without them. */
int HandoffGoneFirstReceiver(int goneSlot, unsigned int uid, const std::vector<int>& tried, int* firstOut)
{
    if (firstOut != 0) *firstOut = -1;
    if (goneSlot < 0 || StoreMySlot() < 0) return -1;
    ::Character* c = FindSpawned(uid);
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!Plaus(c) || !ReadPos(c, &x, &y, &z)) return -1;
    float px = x, pz = z;
    if (SquadDecisionPos(c, &px, &pz) != 1) { px = x; pz = z; }
    std::vector<int> order;
    int holder = -1, niw = 0, nkp = 0;
    const int k = ReceiversFor(SectorOf(px, pz), px, pz, tried, &order, &holder, &niw, &nkp, goneSlot);
    if (k >= 1 && firstOut != 0 && !order.empty()) *firstOut = order[0];
    return k;
}
void HandoffTakeGone(unsigned int uid)
{
    net::TakeLocalOwner(uid, 0);   /* 0: a gen above every record here, the leaver's last one included */
    NoteAnnouncedOnTake(uid);      /* the other games keep their copies: this game's later withdrawal is owed to them */
    ForgetSweepAdopt(uid);
    UnpuppetForOwnership(uid);
}

/* ---- T-1442 step 6: the engine writers' notes and the arrival's ----------------------------------------------------------------------
   set-leader 0x790C00 (this, Character*) and remove-member 0x794C00 (this, Character*; it returns a value in RAX - decomp_794c00 - kept
   whole) note their squad AFTER the original, so the main thread's read sees what they wrote. ANY THREAD: interlocked only. */
typedef void (*LeadSetHookFn)(void* ap, ::Character* c);
typedef unsigned long long (*LeadRemoveHookFn)(void* ap, ::Character* c);
static LeadSetHookFn orig_leadSet = 0;
static LeadRemoveHookFn orig_leadRemove = 0;
static void detour_leadSet(void* ap, ::Character* c)
{
    orig_leadSet(ap, c);
    const LONG own = g_leadOwnCallTid;
    if (coopleadmark::LeadMarkVerdict((own != 0 && own == (LONG)::GetCurrentThreadId()) ? 1 : 0, 1) == coopleadmark::kMarkOwnCall) { ::InterlockedIncrement64(&g_leadOwnCallSkipped); return; }
    coopleadmark::LeadMarkAdd(&g_leadMarks, (unsigned long long)(uintptr_t)ap, coopleadmark::kNoteSetLeader, (unsigned long)::GetTickCount());
}
static unsigned long long detour_leadRemove(void* ap, ::Character* c)
{
    const unsigned long long r = orig_leadRemove(ap, c);
    coopleadmark::LeadMarkAdd(&g_leadMarks, (unsigned long long)(uintptr_t)ap, coopleadmark::kNoteRemoveMember, (unsigned long)::GetTickCount());
    return r;
}
/* review fold S2 (the step-5 lesson: every writer): the acting-leader getter 0x791FF0 (this) writes +0xA8 when the formal leader cannot
   lead (decomp_791ff0:36) - +0xA8 read before and after the original, the squad noted ONLY when it changed. A getter the AI calls
   often on its worker threads: two guarded reads and a compare, nothing else unless it changed (then the interlocked note). */
typedef void* (*LeadGetterFn)(void* ap);
static LeadGetterFn orig_leadGetter = 0;
static int LeadActingPod(const void* ap, void** out)
{
    __try { *out = *(void* const*)((const char*)ap + 0xA8); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *out = 0; return 0; }
}
static void* detour_leadGetter(void* ap)
{
    void* before = 0;
    const int okBefore = LeadActingPod(ap, &before);
    void* const r = orig_leadGetter(ap);
    void* after = 0;
    if (okBefore != 0 && LeadActingPod(ap, &after) != 0 && after != before)
        coopleadmark::LeadMarkAdd(&g_leadMarks, (unsigned long long)(uintptr_t)ap, coopleadmark::kNoteActingGetter, (unsigned long)::GetTickCount());
    return r;
}
/* review fold S3: the death path detaches the character from its squad, so its squad is read BEFORE the death call (Character+0x658,
   the guarded POD SquadApOfPod - any thread) and noted after it with the character. ANY THREAD: interlocked only. */
void* LeadDeadSquadOf(const void* character)
{
    void* ap = 0;
    if (character == 0 || SquadApOfPod((::Character*)character, &ap) == 0) return 0;
    return ap;
}
void LeadNoteDeath(const void* character, void* apBefore)
{
    const unsigned long tick = (unsigned long)::GetTickCount();
    coopleadmark::LeadMarkAdd(&g_leadMarks, (unsigned long long)(uintptr_t)character, coopleadmark::kNoteDead, tick);
    if (apBefore != 0) coopleadmark::LeadMarkAdd(&g_leadMarks, (unsigned long long)(uintptr_t)apBefore, coopleadmark::kNoteDeadSquad, tick);
}
void LeadNoteChar(const void* character, int dead)
{
    coopleadmark::LeadMarkAdd(&g_leadMarks, (unsigned long long)(uintptr_t)character, dead != 0 ? coopleadmark::kNoteDead : coopleadmark::kNoteKo, (unsigned long)::GetTickCount());
}
/* T-1442 step 6 fix a (F1506, cause a Confirmed twice in T2646): the adoption sweep (worldsync.cpp, right after net::SetLocalOwner) gave
   `c` its mod uid. When `c` is its squad's formal leader (+0xA0), a 1 s pass may have just announced that squad with leader 0 (the
   uid was not given yet) - the squad is noted on the set-leader hook's road, so the leader goes out the next frame (LeadEventTick;
   a squad this game does not run is dropped there, LeadMarkVerdict). The 1 s pass stays the backstop. MAIN THREAD. */
void LeadNoteUidGiven(::Character* c)
{
    if (!Plaus(c)) return;
    SquadView sv;
    const int have = ReadSquadOf(c, &sv) == 1 ? 1 : 0;
    if (coopleadmark::LeadUidGivenNotes(have, (have != 0 && sv.leader == c) ? 1 : 0) == 0 || sv.ap == 0) return;
    coopleadmark::LeadMarkAdd(&g_leadMarks, (unsigned long long)(uintptr_t)sv.ap, coopleadmark::kNoteUidLeader, (unsigned long)::GetTickCount());
}
/* T-1442 step 6 fix attempt 3 (F1514: T2648's caged leader - the cage-use start set +0x2F8 0 -> 2 and no note followed). Every engine
   writer of the facts LeaderOut reads besides death is hooked: the character's can't-lead answer (LeaderOutAnswer - the same function
   LeaderOut asks, no second copy of the rule) and its squad's +0xA0 / +0xA8 are read before the original and again after it, and the
   squad is noted (kNoteHeld) when the character leads it and its answer changed - into and out of a bed or cage, down and up, picked
   up and put down, the physics ragdoll on and off (review fold: ragdollModeUT 0x5B8F80). ANY THREAD: guarded reads, the engine's
   read-only inRagdoll and the interlocked note only. Every argument register is passed through whole and RAX returned (a void original leaves it unused). */
struct LeadOutRead { int ok; int out; void* ap; int isFormal; int isActing; };
static volatile LONG g_leadOutHook[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };   /* bed, prison, pose, dropped, pickedUp, outOfBed, floorStop, floorEnd, ragdoll: 1 / -1 / 0 */
static volatile LONG64 g_leadOutSeen = 0, g_leadOutChanged = 0;       /* hooked calls on a readable character; of them, 'out' changed */
static void LeadOutReadPod(const void* c, LeadOutRead* o)
{
    o->ok = 0; o->out = 0; o->ap = 0; o->isFormal = o->isActing = 0;
    if (c == 0 || ((uintptr_t)c & 7) != 0) return;
    const int out = LeaderOutAnswer((::Character*)c);                  /* THE can't-lead rule - LeaderOut's own answer */
    if (out < 0) return;
    o->out = out;
    __try
    {
        const char* p = (const char*)c;
        void* ap = *(void* const*)(p + 0x658);                         /* ActivePlatoon (SquadApOfPod) */
        o->ap = ap;
        if (ap != 0)
        {
            o->isFormal = *(void* const*)((const char*)ap + 0xA0) == c ? 1 : 0;
            o->isActing = *(void* const*)((const char*)ap + 0xA8) == c ? 1 : 0;
        }
        o->ok = 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { o->ok = 0; }
}
/* the squad of `b` (read before the call) and of `a` (after it, when the call moved the character to another squad) */
static void LeadOutNote(const LeadOutRead& b, const LeadOutRead& a)
{
    if (b.ok == 0 || a.ok == 0) return;
    ::InterlockedIncrement64(&g_leadOutSeen);
    const int outB = b.out, outA = a.out;
    if (outB == outA) return;
    ::InterlockedIncrement64(&g_leadOutChanged);
    const unsigned long tick = (unsigned long)::GetTickCount();
    if (coopleadmark::LeadHeldNotes(1, b.ap != 0 ? 1 : 0, b.isFormal, b.isActing, outB, outA) != 0)
        coopleadmark::LeadMarkAdd(&g_leadMarks, (unsigned long long)(uintptr_t)b.ap, coopleadmark::kNoteHeld, tick);
    if (a.ap != b.ap && coopleadmark::LeadHeldNotes(1, a.ap != 0 ? 1 : 0, a.isFormal, a.isActing, outB, outA) != 0)
        coopleadmark::LeadMarkAdd(&g_leadMarks, (unsigned long long)(uintptr_t)a.ap, coopleadmark::kNoteHeld, tick);
}
/* the character a task's 'who' names: who->vt+0x48, as the tasks themselves ask it (0x348010 / 0x347E80, decomp_357fe0) */
typedef void* (*LeadWhoGetFn)(void* who);
static void* LeadWhoCharPod(void* who)
{
    if (who == 0 || ((uintptr_t)who & 7) != 0) return 0;
    __try { void** vt = *(void***)who; return vt == 0 ? 0 : ((LeadWhoGetFn)vt[0x48 / 8])(who); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
typedef unsigned long long (*LeadOut3Fn)(void* a, unsigned long long b, unsigned long long c);
typedef unsigned long long (*LeadOut2Fn)(void* a, void* b);
typedef unsigned long long (*LeadOut6Fn)(void* anim, unsigned long long on, void* pos, unsigned long long mask, void* bone, void* c);
static LeadOut3Fn orig_lhBed = 0, orig_lhPrison = 0, orig_lhPose = 0, orig_lhDropped = 0;
static LeadOut2Fn orig_lhPickedUp = 0, orig_lhOutOfBed = 0, orig_lhFloorStop = 0, orig_lhFloorEnd = 0;
static LeadOut6Fn orig_lhRagdoll = 0;
/* the physics ragdoll on / off (0x5B8F80): the Character is the 6th argument (Character::_ragdollMode passes this) */
static unsigned long long detour_lhRagdoll(void* anim, unsigned long long on, void* pos, unsigned long long mask, void* bone, void* c)
{
    LeadOutRead b, a; LeadOutReadPod(c, &b);
    const unsigned long long r = orig_lhRagdoll(anim, on, pos, mask, bone, c);
    LeadOutReadPod(c, &a); LeadOutNote(b, a);
    return r;
}
static unsigned long long detour_lhBed(void* c, unsigned long long on, unsigned long long u)
{
    LeadOutRead b, a; LeadOutReadPod(c, &b);
    const unsigned long long r = orig_lhBed(c, on, u);
    LeadOutReadPod(c, &a); LeadOutNote(b, a);
    return r;
}
static unsigned long long detour_lhPrison(void* c, unsigned long long on, unsigned long long u)
{
    LeadOutRead b, a; LeadOutReadPod(c, &b);
    const unsigned long long r = orig_lhPrison(c, on, u);
    LeadOutReadPod(c, &a); LeadOutNote(b, a);
    return r;
}
static unsigned long long detour_lhPose(void* c, unsigned long long pose, unsigned long long x)
{
    LeadOutRead b, a; LeadOutReadPod(c, &b);
    const unsigned long long r = orig_lhPose(c, pose, x);
    LeadOutReadPod(c, &a); LeadOutNote(b, a);
    return r;
}
static unsigned long long detour_lhDropped(void* c, unsigned long long f1, unsigned long long f2)
{
    LeadOutRead b, a; LeadOutReadPod(c, &b);
    const unsigned long long r = orig_lhDropped(c, f1, f2);
    LeadOutReadPod(c, &a); LeadOutNote(b, a);
    return r;
}
static unsigned long long detour_lhPickedUp(void* c, void* carrier)
{
    LeadOutRead b, a; LeadOutReadPod(c, &b);
    const unsigned long long r = orig_lhPickedUp(c, carrier);
    LeadOutReadPod(c, &a); LeadOutNote(b, a);
    return r;
}
static unsigned long long detour_lhOutOfBed(void* task, void* c)
{
    LeadOutRead b, a; LeadOutReadPod(c, &b);
    const unsigned long long r = orig_lhOutOfBed(task, c);
    LeadOutReadPod(c, &a); LeadOutNote(b, a);
    return r;
}
static unsigned long long detour_lhFloorStop(void* task, void* who)
{
    void* const c = LeadWhoCharPod(who);
    LeadOutRead b, a; LeadOutReadPod(c, &b);
    const unsigned long long r = orig_lhFloorStop(task, who);
    LeadOutReadPod(c, &a); LeadOutNote(b, a);
    return r;
}
static unsigned long long detour_lhFloorEnd(void* task, void* who)
{
    void* const c = LeadWhoCharPod(who);
    LeadOutRead b, a; LeadOutReadPod(c, &b);
    const unsigned long long r = orig_lhFloorEnd(task, who);
    LeadOutReadPod(c, &a); LeadOutNote(b, a);
    return r;
}
static LONG LeadOutHookOne(unsigned long long rva, void* detour, void** orig)
{
    if (rva == 0) return -1;
    return coop::AddHook((void*)((uintptr_t)::GetModuleHandleA(0) + (uintptr_t)rva), detour, orig) == coop::SUCCESS ? 1 : -1;
}
static void LeadOutHooks()
{
    g_leadOutHook[0] = LeadOutHookOne(kLhBedModeRva, (void*)&detour_lhBed, (void**)&orig_lhBed);
    g_leadOutHook[1] = LeadOutHookOne(kLhPrisonModeRva, (void*)&detour_lhPrison, (void**)&orig_lhPrison);
    g_leadOutHook[2] = LeadOutHookOne(kLhPoseRva, (void*)&detour_lhPose, (void**)&orig_lhPose);
    g_leadOutHook[3] = LeadOutHookOne(kLhDroppedRva, (void*)&detour_lhDropped, (void**)&orig_lhDropped);
    g_leadOutHook[4] = LeadOutHookOne(kLhPickedUpRva, (void*)&detour_lhPickedUp, (void**)&orig_lhPickedUp);
    g_leadOutHook[5] = LeadOutHookOne(kLhOutOfBedRva, (void*)&detour_lhOutOfBed, (void**)&orig_lhOutOfBed);
    g_leadOutHook[6] = LeadOutHookOne(kLhFloorStopRva, (void*)&detour_lhFloorStop, (void**)&orig_lhFloorStop);
    g_leadOutHook[7] = LeadOutHookOne(kLhFloorEndRva, (void*)&detour_lhFloorEnd, (void**)&orig_lhFloorEnd);
    g_leadOutHook[8] = LeadOutHookOne(kLhRagdollRva, (void*)&detour_lhRagdoll, (void**)&orig_lhRagdoll);
    std::string w;
    for (int i = 0; i < 9; ++i) w += std::string(i == 0 ? "" : ",") + N((long long)g_leadOutHook[i]);
    DebugLog("[LEAD] T-1442 step 6 fix attempt 3 hooks [setBedMode 0x32E2B0, setPrisonMode 0x3305C0, setPoseState 0x5C7390, getDropped 0x5CC110, getPickedUp 0x5CE300,"
             " Task_GetOutOfBed step 0x3455C0, Task_SleepOnFloor 0x347E80 / 0x348010, ragdollModeUT 0x5B8F80]=" + w
             + " (1 installed, -1 failed or no row): a squad's leader going into / out of a bed or cage, down / up, picked up / put down, ragdolled / not is noted at the engine's call");
}
void InstallLeadHooks()
{
    LeadOutHooks();
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    if (kApSetSquadLeaderRva != 0)
        g_leadSetHook = (coop::AddHook((void*)(base + (uintptr_t)kApSetSquadLeaderRva), (void*)&detour_leadSet, (void**)&orig_leadSet) == coop::SUCCESS) ? 1 : -1;
    else g_leadSetHook = -1;
    if (kApRemoveMemberRva != 0)
        g_leadRemoveHook = (coop::AddHook((void*)(base + (uintptr_t)kApRemoveMemberRva), (void*)&detour_leadRemove, (void**)&orig_leadRemove) == coop::SUCCESS) ? 1 : -1;
    else g_leadRemoveHook = -1;
    if (kApLeaderGetRva != 0)
        g_leadGetterHook = (coop::AddHook((void*)(base + (uintptr_t)kApLeaderGetRva), (void*)&detour_leadGetter, (void**)&orig_leadGetter) == coop::SUCCESS) ? 1 : -1;
    else g_leadGetterHook = -1;
    DebugLog(std::string("[LEAD] T-1442 step 6 hooks: set-leader 0x790C00 ") + (g_leadSetHook == 1 ? "installed" : (kApSetSquadLeaderRva == 0 ? "NOT IN THE TABLE" : "FAILED"))
             + ", remove-member 0x794C00 " + (g_leadRemoveHook == 1 ? "installed" : (kApRemoveMemberRva == 0 ? "NOT IN THE TABLE" : "FAILED"))
             + ", acting-leader getter 0x791FF0 " + (g_leadGetterHook == 1 ? "installed" : (kApLeaderGetRva == 0 ? "NOT IN THE TABLE" : "FAILED"))
             + " (a squad's leader change is announced the frame after the engine makes it; the 1 s look is the backstop)");
}
/* MAIN THREAD (ApplyRemoteSquadLead, an applied announcement): SquadLeaderSafePointDrain runs at the next safe point instead of on its 1 s
   throttle, and the squads here that hold a person it names run their hand-over decision at the next frame (LeadEventTick) */
static void LeadArrivalNote(const coopsquad::SquadLeadMsg& m, unsigned int sender)
{
    ::InterlockedExchange(&g_leadDrainDue, 1);
    if (g_leadArrivalUids.size() < 4096)
    {
        for (size_t i = 0; i < m.members.size(); ++i) if (m.members[i] != 0) g_leadArrivalUids.insert(m.members[i]);
        if (m.acting != 0) g_leadArrivalUids.insert(m.acting);
        if (m.formal != 0) g_leadArrivalUids.insert(m.formal);
    }
    if (g_leadArrLines >= kLeadArrLineCap) return;
    ++g_leadArrLines;
    DebugLog("[LEAD] arrival <- squad=" + N((long long)m.squadKey) + " lead=" + N((long long)m.acting) + " formal=" + N((long long)m.formal) + " members=" + N((long long)m.members.size())
             + " seq=" + N((long long)m.seq) + " from=" + N((long long)sender) + " applied (copy leader at the next safe point; the hand-over decision at the next frame)");
}
static std::string LeadEventReport()
{
    const coopleadmark::LeadMarkTable& t = g_leadMarks;
    return " leadEvent[notedSetLeader,notedRemoveMember,notedKo,notedDead,notedDeadSquad,notedActingGetter,notedUidLeader,notedHeld,already,overflow,bad,ownCallSkipped,notRun,runs,arrivalRuns,sent,dropped,drainEarly,setHook,removeHook,getterHook]="
           + N((long long)t.noted[coopleadmark::kNoteSetLeader]) + "," + N((long long)t.noted[coopleadmark::kNoteRemoveMember]) + "," + N((long long)t.noted[coopleadmark::kNoteKo])
           + "," + N((long long)t.noted[coopleadmark::kNoteDead]) + "," + N((long long)t.noted[coopleadmark::kNoteDeadSquad]) + "," + N((long long)t.noted[coopleadmark::kNoteActingGetter]) + "," + N((long long)t.noted[coopleadmark::kNoteUidLeader]) + "," + N((long long)t.noted[coopleadmark::kNoteHeld]) + "," + N((long long)t.already) + "," + N((long long)t.overflow) + "," + N((long long)t.bad)
           + "," + N((long long)g_leadOwnCallSkipped) + "," + N(g_leadEvNotRun) + "," + N(g_leadEvRuns) + "," + N(g_leadEvArrivalRuns) + "," + N(g_leadEvSent)
           + "," + N(g_leadEvDropped) + "," + N(g_leadDrainEarly) + "," + N((long long)g_leadSetHook) + "," + N((long long)g_leadRemoveHook) + "," + N((long long)g_leadGetterHook)
           + " leadEventSentBy[setLeader,removeMember,ko,dead,deadSquad,actingGetter,uidLeader,held,arrival]=" + N(g_leadEvSentBy[0]) + "," + N(g_leadEvSentBy[1]) + "," + N(g_leadEvSentBy[2])
           + "," + N(g_leadEvSentBy[3]) + "," + N(g_leadEvSentBy[4]) + "," + N(g_leadEvSentBy[5]) + "," + N(g_leadEvSentBy[6]) + "," + N(g_leadEvSentBy[7]) + "," + N(g_leadEvSentBy[8])
           + " leadOutHooks[bed,prison,pose,dropped,pickedUp,outOfBed,floorStop,floorEnd]=" + N((long long)g_leadOutHook[0]) + "," + N((long long)g_leadOutHook[1]) + "," + N((long long)g_leadOutHook[2])
           + "," + N((long long)g_leadOutHook[3]) + "," + N((long long)g_leadOutHook[4]) + "," + N((long long)g_leadOutHook[5]) + "," + N((long long)g_leadOutHook[6]) + "," + N((long long)g_leadOutHook[7])
           + " leadOut[seen,changed]=" + N((long long)g_leadOutSeen) + "," + N((long long)g_leadOutChanged)
           + " leadBackstop[first,other,watched,wake,missed]=" + N(g_leadBackstop[coopleadmark::kBsFirst]) + "," + N(g_leadBackstop[coopleadmark::kBsOther])
           + "," + N(g_leadBackstop[coopleadmark::kBsWatched]) + "," + N(g_leadBackstop[coopleadmark::kBsWake]) + "," + N(g_leadBackstop[coopleadmark::kBsMissed]);
}
void ReportHandoff()
{
    const std::string q(1, (char)34);
    DebugLog("[XFER] REPORT squadsSeen=" + N(g_squadsSeen) + " xferOut=" + N(g_xferOut) + " resent=" + N(g_xferResent) + " abandoned=" + N(g_xferAbandoned) + " ackIn=" + N(g_ackIn) + " released=" + N(g_released)
             + " | xferIn=" + N(g_xferIn) + " taken=" + N(g_taken) + " carriedNotIssued=" + N(g_takeCarriedNotIssued) + " takeOrdersDropped=" + N(g_takeOrdersDropped) + " takeOrdersUnread=" + N(g_takeOrdersDropUnread) + " unknownUids=" + N(g_xferInUnknownUid) + " ackOut=" + N(g_ackOut)
             + " pending=" + N((long long)g_pending.size()) + " teleports=" + N(g_teleports)
             + " carryHand[offered,given,refused,abandoned,takenHere,refusedHere,heldPasses,holdStarts,holdEnds]=" + N(g_carryOffered) + "," + N(g_carryGiven) + "," + N(g_carryRefused) + "," + N(g_carryAbandoned)
             + "," + N(g_carryTakenHere) + "," + N(g_carryRefusedHereN) + "," + N(g_carryHeld) + "," + N(g_carryHoldStarts) + "," + N(g_carryHoldEnds) + " carryTaken[keptOutOfPass,ended,open]=" + N(g_carryKeptOutOfPass) + "," + N(g_carryTakenEnded) + "," + N((long long)g_carryTakenBy.size())   /* T-1467 */
             + " | nudges=" + N(g_nudges) + " keptUntaken=" + N(g_keptUntaken) + " xferInOutsideRing=" + N(g_xferInOutsideRing)
             + " | squadWriter[followedLeader,leaderUnknownFellBack,waitedLeader,xferFollowSent,xferFollowTaken,leaderSet,leaderSetMiss,keepAwakeRefresh]="
             + N(g_followedLeader) + "," + N(g_leaderUnknownFellBack) + "," + N(g_waitedLeader) + "," + N(g_xferFollowSent) + "," + N(g_xferFollowTaken)
             + "," + N(g_leaderSet) + "," + N(g_leaderSetMiss) + "," + N(g_keepAwakeRefresh)
             + " followBackoff=" + N((long long)g_followBackoff.size())
             + " followGroup[notInGroup,blockSet,blockedPasses,unblocked,blocks]=" + N(g_followNotInGroup) + "," + N(g_followBlockSet) + "," + N(g_followBlockedPass)
             + "," + N(g_followUnblocked) + "," + N((long long)g_followGroupBlock.size())   /* T-1239 */
             + " | fold[agreedDiffered,leaderActing,leaderSetPrecond,followLogQuiet]="
             + N(g_agreedDiffered) + "," + N(g_leaderActing) + "," + N(g_leaderSetPrecond) + "," + N(g_followLogQuiet)
             + " | squadLead[sent,recv,applied,stale,crossingResolved,crossingRefused]="
             + N(g_squadLeadSent) + "," + N(g_squadLeadRecv) + "," + N(g_squadLeadApplied) + "," + N(g_squadLeadStale) + "," + N(g_crossingResolved) + "," + N(g_crossingRefused)
             + " myLeadSquads=" + N((long long)g_myLead.size()) + " peerLeadSquads=" + N((long long)g_peerLead.Squads()) + " peerLeadFull=" + N(g_squadLeadFull)
             + SquadCatsReport()   /* T-1 B3 restructure */
             + LeadEventReport());   /* T-1442 step 6: the event road, then the backstop - missed last */
    /* releaseMore / releaseHolder / releaseSkip ride the [net] REPORT token, which T1056's report lines did not show - printed
       here as well, with the keep counters */
    DebugLog("[XFER] REPORT releaseMore[offers,held,next,lateAccepted,ackWrongSender,ackStale,ackUnknown,dupAck,notMine,worldCleared,logged]=" + N(g_relOffers) + "," + N(g_relHeld)
             + "," + N(g_relNext) + "," + N(g_relLateAccepted) + "," + N(g_relAckWrongSender) + "," + N(g_relAckStale) + "," + N(g_relAckUnknown) + "," + N(g_relDupAck) + "," + N(g_relNotMine)
             + "," + N(g_relWorldCleared) + "," + N(g_relLogged)
             + " releaseHolder[offer,none,hold,keep,keepUnknown,leave,notInWorld,backoff]=" + N(g_holderOffer) + "," + N(g_holderNone) + "," + N(g_holderHold) + "," + N(g_holderKeep)
             + "," + N(g_holderKeepUnknown) + "," + N(g_holderLeave) + "," + N(g_holderNotInWorld) + "," + N(g_holderBackoff)
             + " keep[notKeep,asleepNoKeep,readFailTake,readFailForced,forcedListed]=" + N(g_keepNotKeep) + "," + N(g_keepAsleepNoKeep) + "," + N(g_keepReadFailTake)
             + "," + N(g_keepReadFailForced) + "," + N(g_keepForcedListed)
             + " releaseSkip[inFlight,offThread,unread,player,notAnnounced,yield,reloadRetire]=" + N(g_relSkipInFlight) + "," + N(g_relSkipOffThread) + "," + N(g_relSkipUnread) + "," + N(g_relSkipPlayer)
             + "," + N(g_relSkipNotAnnounced) + "," + N(g_relSkipYield) + "," + N(g_relSkipReload)
             + " release[sent,adopted,asleep]=" + N(g_relSent) + "," + N(g_relAdopted) + "," + N(g_relAsleep) + " releaseOpen=" + N((long long)g_release.size())
             + " releaseClearNear=" + N(g_relSkipClearNear));
    SW_DEVLOG("[VERDICT] {" + q + "ev" + q + ":" + q + "xfer_report" + q + "," + q + "xferOut" + q + ":" + N(g_xferOut) + "," + q + "ackIn" + q + ":" + N(g_ackIn) + "," + q + "released" + q + ":" + N(g_released)
             + "," + q + "abandoned" + q + ":" + N(g_xferAbandoned) + "," + q + "xferIn" + q + ":" + N(g_xferIn) + "," + q + "taken" + q + ":" + N(g_taken) + "," + q + "ackOut" + q + ":" + N(g_ackOut) + "," + q + "pending" + q + ":" + N((long long)g_pending.size()) + "}");
}

// T-1 B1 restructure (protocol 85): the other game's announcement. MAIN THREAD (the session drain).
void ApplyRemoteSquadLead(const coopsquad::SquadLeadMsg& m, unsigned int sender)
{
    LeadLinkEdge();
    ++g_squadLeadRecv;
    const int r = g_peerLead.Apply(m, sender);   /* M7a A1 build 1 [a1b1-hd10]: keyed by (sender, squad key) */
    if (r == coopsquad::kLeadStale) { ++g_squadLeadStale; return; }
    if (r == coopsquad::kLeadFull)
    {
        if (g_squadLeadFull++ == 0) DebugLog("[XFER] SQUAD_LEAD book full (" + N((long long)coopsquad::kLeadBookCap) + " squads) - a new squad's announcement was ignored");
        return;
    }
    ++g_squadLeadApplied;
    g_catsDue = 1;   /* T-1 B3 restructure: its money is compared with the copy's pot at the next safe point */
    LeadArrivalNote(m, sender);   /* T-1442 step 6: the copy squad's leader at the next safe point; the hand-over decision at the next frame */
}
// T-1 B1 step 5, restructured (protocol 85): at the K2 safe point (worker paused), once a second - each copy squad (no member run
// here, not a player's or a stand-in's) whose announced FORMAL leader (the other game's newest MSG_SQUAD_LEAD among its members,
// a live puppet here - review H1) is one of its members but not its +0xA0 gets it through the engine's own
// ActivePlatoon::setSquadLeader, so this engine derives the same acting leader from the replicated states. T528 (Confirmed) saw
// the copy game's engine pick its own leader otherwise. A split squad is the follow pass's to settle; once whole here it drains.
void SquadLeaderSafePointDrain()
{
    static DWORD last = 0; const DWORD nowMs = ::GetTickCount();
    if (nowMs - last < 1000 && g_leadDrainDue == 0) return;   /* T-1442 step 6: an arrived announcement brings it forward */
    if (nowMs - last < 1000) ++g_leadDrainEarly;
    last = nowMs;
    ::InterlockedExchange(&g_leadDrainDue, 0);
    if (g_peerLead.Squads() == 0 || kApSetSquadLeaderRva == 0) return;
    const uintptr_t fn = (uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kApSetSquadLeaderRva;
    std::set<void*> seen;
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int uid = 0; ::Character* c = 0;
        if (!MirrorSlot(i, &uid, &c) || uid == 0 || !Plaus(c) || net::IsUidMine(uid)) continue;
        if (g_peerLead.ForUid(uid) == 0) continue;   // the other game announced no squad for it
        ::Faction* f = c->getOwnerFactionDirect();
        if (coop::IsPlayerFaction(f) || coop::IsStandInFaction(f)) continue;
        void* ap = SquadActivePlatoonOf(c);
        if (ap == 0 || !seen.insert(ap).second) continue;
        SquadView sv;
        if (ReadSquadAt(ap, &sv) != 1) continue;
        unsigned int uids[kSquadViewCap]; int mineHere = 0;
        for (int k = 0; k < sv.stored; ++k)
        {
            uids[k] = Plaus(sv.members[k]) ? FindSpawnedUid(sv.members[k]) : 0u;
            if (uids[k] != 0 && net::IsUidMine(uids[k])) mineHere = 1;
        }
        if (mineHere != 0) continue;   // a squad this game runs part of is the follow pass's to settle
        const coopsquad::PeerLeadEntry* pe = PeerAnnouncedFor(sv, uids);
        if (pe == 0 || pe->formal == 0 || net::IsUidMine(pe->formal) || !HasPuppet(pe->formal)) continue;
        int li = -1;
        for (int k = 0; k < sv.stored; ++k) if (uids[k] == pe->formal) { li = k; break; }
        if (li < 0) continue;   // the announced formal leader is not a member of this copy squad
        ::Character* lead = sv.members[li];
        if (sv.leader == lead) continue;   // already the owner's leader
        const unsigned int was = Plaus(sv.leader) ? FindSpawnedUid(sv.leader) : 0;
        if (LeaderAiPlausiblePod(lead) != 1)
        {   // review L2: 0x620B30 would dereference an absent AI after the old leader's type was already reset
            ++g_leaderSetMiss; ++g_leaderSetPrecond;
            if (g_leaderSetLogged < 32) { ++g_leaderSetLogged; DebugLog("[XFER] copy squad leader NOT set (the named leader's AI [c+0x650] / [[c+0x650]+0x20] is not plausible) leader=" + N(uids[li]) + " was=" + N(was)); }
            continue;
        }
        ::InterlockedExchange(&g_leadOwnCallTid, (LONG)::GetCurrentThreadId());   /* T-1442 step 6: the set-leader hook does not note the mod's own call */
        const int r = SetSquadLeaderPod(ap, lead, fn);
        ::InterlockedExchange(&g_leadOwnCallTid, 0);
        if (r == 1) ++g_leaderSet; else ++g_leaderSetMiss;
        if (g_leaderSetLogged < 32)
        {
            ++g_leaderSetLogged;
            DebugLog("[XFER] copy squad leader " + std::string(r == 1 ? "set" : (r == 0 ? "MISSED (+0xA0 not the named leader after the call)" : "MISSED (the call faulted)"))
                     + " leader=" + N(uids[li]) + " was=" + N(was) + " members=" + N(sv.count));
        }
    }
}
// Owner 110 (see handoff.h). "Ground loaded here" = IsPositionLoadedHere (zones.cpp): the engine's own
// ZoneManager::isZoneLoadedT answer for the member's position - the mod's existing loaded-position test. The same walk whatever
// the number of players in the world: a copy squad is one whose members are all copies of characters another game runs.
void CopySquadKeepAwake(void* faction)
{
    if (faction == 0) return;
    const DWORD nowMs = ::GetTickCount();
    std::map<void*, DWORD>::iterator t = g_keepAwakeAt.find(faction);
    if (t != g_keepAwakeAt.end() && nowMs - t->second < 1000) return;
    if (g_keepAwakeAt.size() > 4096) g_keepAwakeAt.clear();
    g_keepAwakeAt[faction] = nowMs;
    ::Faction* f = (::Faction*)faction;
    if (coop::IsPlayerFaction(f) || coop::IsStandInFaction(f)) return;   // player squads and stand-ins untouched
    void** items = 0; unsigned int n = 0;
    if (FactionActiveListPod(faction, &items, &n) == 0 || !Plaus(items) || n > 4096) return;
    for (unsigned int i = 0; i < n; ++i)
    {
        void* ap = 0;
        if (ActiveOfListPod(items, i, &ap) == 0 || !Plaus(ap)) continue;
        SquadView sv;
        if (ReadSquadAt(ap, &sv) != 1 || sv.count <= 0 || sv.stored < sv.count) continue;
        int allCopies = 1, anyLoaded = 0;
        for (int k = 0; k < sv.stored; ++k)
        {
            ::Character* c = sv.members[k];
            const unsigned int u = Plaus(c) ? FindSpawnedUid(c) : 0;
            // review H1: a live copy = a character this game PUPPETS for the other game (HasPuppet - the puppet set that
            // UnpuppetForOwnership clears). A shared-save twin withdrawn by UNLOAD keeps its uid row, but is neither mine nor a copy.
            if (u == 0 || net::IsUidMine(u) || !HasPuppet(u)) { allCopies = 0; break; }
            float x = 0, y = 0, z = 0;
            if (anyLoaded == 0 && ReadPos(c, &x, &y, &z) && IsPositionLoadedHere(x, y, z)) anyLoaded = 1;
        }
        if (coopsquad::KeepCopySquadAwake(allCopies, anyLoaded, sv.playerFlag) == 0) continue;
        if (StayAwakeWritePod(ap) == 1) ++g_keepAwakeRefresh;
    }
}

// M-D step 2 harness lever: the first squad I own (mirror order) moves by (dx, dz), every owned member, with the
// engine's own placement - the way to put a client-owned squad into a host-owned sector both sides hold.
bool SquadNudge(float dx, float dz)
{
    // T144: the lever once picked a knocked-out squad, which the engine cannot place (F422) - the run's premise failed
    // silently. Standing squads only, and the move is VERIFIED by re-reading the first member's position.
    // T144/T146: the first owned squad was a sitting ('Relaxing') character the teleport could not move - the run's premise
    // failed twice. Now: try owned squads in mirror order until one VERIFIABLY moves (re-read after the teleport), up to 8.
    const int cap = MirrorCapacity(); int skippedDowned = 0, tried = 0; std::vector<void*> rejected;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        void* chosen = 0; int moved = 0; unsigned int leaderUid = 0; float vx0 = 0, vz0 = 0; ::Character* verify = 0;
        for (int i = 0; i < cap; ++i)
        {
            unsigned int uid = 0; ::Character* c = 0;
            if (!MirrorSlot(i, &uid, &c)) continue;
            if (!net::IsUidMine(uid) || !Plaus(c)) continue;
            void* ap = SquadActivePlatoonOf(c); if (!Plaus(ap)) continue;   /* T-1 B1: the verified reader */
            bool rej = false; for (size_t r = 0; r < rejected.size(); ++r) if (rejected[r] == ap) rej = true;
            if (rej) continue;
            if (chosen == 0)
            {
                if (IsDownedCharacter(c)) { ++skippedDowned; rejected.push_back(ap); continue; }
                chosen = ap; ::Character* l = LeaderOf(ap); leaderUid = FindSpawnedUid(Plaus(l) ? l : c); if (leaderUid == 0) leaderUid = uid;
            }
            if (ap != chosen) continue;
            float x = 0, y = 0, z = 0;
            if (ReadPos(c, &x, &y, &z) && Teleport(c, x + dx, y, z + dz)) { if (verify == 0) { verify = c; vx0 = x; vz0 = z; } ++moved; }
        }
        if (chosen == 0) break;
        ++tried;
        float ax = 0, ay = 0, az = 0; if (verify) ReadPos(verify, &ax, &ay, &az);
        const float actual = verify ? sqrtf((ax - vx0) * (ax - vx0) + (az - vz0) * (az - vz0)) : 0.0f;
        if (moved > 0 && actual > 100.0f)
        {
            ++g_nudges;
            DebugLog("[XFER] squad nudged: leader=" + N(leaderUid) + " members moved=" + N(moved) + " by " + F1(dx) + "," + F1(dz) + " (verified " + F1(actual) + " units; tried=" + N(tried) + " skippedDowned=" + N(skippedDowned) + ")");
            return true;
        }
        DebugLog("[XFER] squad nudge: leader=" + N(leaderUid) + " did not move (verified " + F1(actual) + " units) - trying the next squad");
        rejected.push_back(chosen);
    }
    DebugLog("[XFER] squad nudge: no owned squad could be moved (tried=" + N(tried) + " skippedDowned=" + N(skippedDowned) + ")");
    return false;
    return true;
}

static unsigned long long kHoFocusCameraOnRva = 0; static coop::AddrReg kHoFocusCameraOnRva_reg("FocusCameraOn", &kHoFocusCameraOnRva);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x7F1F10 */
typedef void (*FocusCameraFn)(void* playerInterface, const float* xyz);
int FocusCameraPod(float x, float y, float z)
{
    __try
    {
        if (!Plaus(coop::GameWorldPtr())) return 0;   // review-p3h H6
        void* pi = *(void**)((char*)coop::GameWorldPtr() + 0x580);
        if (!Plaus(pi)) return 0;
        const float v[3] = { x, y, z };
        if (kHoFocusCameraOnRva == 0) return 0;
        ((FocusCameraFn)coop::AddrAbs(kHoFocusCameraOnRva))(pi, v);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* T-890 TEST-ONLY lever `camturn` (command_channel.cpp; the rig and the setter: src/common/camturn.h). MAIN THREAD.
   The engine's own setter is the PlayerInterface function right after focusCamera (Steam 1.0.65 0x7F1F20 = FocusCameraOn + 0x10;
   the FocusCameraOn row's byte pattern in signatures.sig already spans its first 8 bytes, so it sits there in every exe that row
   matches): (this, const Quaternion& rot, float zoom) -> the camera object (+0x30) 0x6AF0C0 -> target node setOrientation(rot),
   camera node setPosition(0, 0, zoom). Before the call its first 11 bytes are checked (refused if they differ). No new address row.
   The read-back goes through Kenshi's own Ogre exports (Node::getOrientation / Node::getPosition, by value: this, hidden out). */
typedef void (*CamSetFn)(void* playerInterface, const float* quatWxyz, float zoom);
typedef float* (*CamNodeGetOriFn)(const void* node, float* out);   /* Ogre::Node::getOrientation() const - a Quaternion by value */
typedef float* (*CamNodeGetPosFn)(const void* node, float* out);   /* Ogre::Node::getPosition() const - a Vector3 by value */
static const unsigned char kCamSetHead[11] = { 0x48, 0x83, 0xEC, 0x38, 0x0F, 0x10, 0x02, 0x48, 0x8B, 0x49, 0x30 };   /* sub rsp,38h; movups xmm0,[rdx]; mov rcx,[rcx+30h] */
static CamNodeGetOriFn g_camGetOri = 0;
static CamNodeGetPosFn g_camGetPos = 0;
static int g_camOgreLooked = 0;
static void* CamPlayerInterface()
{
    if (!Plaus(coop::GameWorldPtr())) return 0;
    void* pi = *(void**)((char*)coop::GameWorldPtr() + 0x580);
    return Plaus(pi) ? pi : 0;
}
/* 1 = q (w,x,y,z) and the camera node's local z read; 0 = no PlayerInterface / camera / Ogre export; -1 = a fault */
int CameraReadPod(float* q, float* zoom)
{
    if (!g_camOgreLooked)
    {
        g_camOgreLooked = 1;
        HMODULE og = ::GetModuleHandleA("OgreMain_x64.dll");
        if (og != 0)
        {
            g_camGetOri = (CamNodeGetOriFn)::GetProcAddress(og, "?getOrientation@Node@Ogre@@QEBA?AVQuaternion@2@XZ");
            g_camGetPos = (CamNodeGetPosFn)::GetProcAddress(og, "?getPosition@Node@Ogre@@QEBA?AVVector3@2@XZ");
        }
    }
    if (g_camGetOri == 0 || g_camGetPos == 0) return 0;
    __try
    {
        void* pi = CamPlayerInterface();
        if (pi == 0) return 0;
        void* cam = *(void**)((char*)pi + 0x30);
        if (!Plaus(cam)) return 0;
        void* target = *(void**)((char*)cam + 0x58);
        void* camNode = *(void**)((char*)cam + 0x70);
        if (!Plaus(target) || !Plaus(camNode)) return 0;
        float o[4] = { 0, 0, 0, 0 }, p[3] = { 0, 0, 0 };
        g_camGetOri(target, o);
        g_camGetPos(camNode, p);
        q[0] = o[0]; q[1] = o[1]; q[2] = o[2]; q[3] = o[3];
        *zoom = p[2];
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* 1 = the engine's setter called; 0 = no PlayerInterface / no FocusCameraOn row; 2 = the setter's first bytes are not the expected
   ones (another exe or another mod) - not called; -1 = a fault */
int CameraTurnPod(const float* q, float zoom)
{
    __try
    {
        void* pi = CamPlayerInterface();
        if (pi == 0 || kHoFocusCameraOnRva == 0) return 0;
        const unsigned char* fn = (const unsigned char*)(uintptr_t)(coop::AddrAbs(kHoFocusCameraOnRva) + 0x10);
        for (int i = 0; i < (int)sizeof(kCamSetHead); ++i) if (fn[i] != kCamSetHead[i]) return 2;
        ((CamSetFn)fn)(pi, q, zoom);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
bool PlayerTeleportAbs(float x, float z)
{
    ::Character* pc = GetTarget();
    float cx = 0, cy = 0, cz = 0;
    if (!Plaus(pc) || !ReadPos(pc, &cx, &cy, &cz)) return false;
    return PlayerTeleport(x - cx, z - cz);   // the same path (camera focus, log) as a relative move
}
bool PlayerTeleport(float dx, float dz)
{
    ::Character* pc = GetTarget();
    float x = 0, y = 0, z = 0;
    if (!Plaus(pc) || !ReadPos(pc, &x, &y, &z)) return false;
    if (IsDownedCharacter(pc))   // T1338 / T1342: a downed character's ragdoll stays behind and pulls it back when time runs - the move would not hold
    {
        DebugLog("[XFER] player teleport REFUSED: the watched character is down (knocked out / playing dead) - the engine's teleport does not move its ragdoll, it would snap back");
        return false;
    }
    if (!Teleport(pc, x + dx, y, z + dz)) return false;
    ++g_teleports;
    // F479: the teleport moved the character only; a zone stays active while its CAMERA countdown is alive, and the client's camera
    // did not follow (the host's did). Focus the camera on the destination as a following camera would - PlayerInterface::focusCamera
    // (const Vector3&) 0x7F1F10 (DLL export; the header's 0x7F1850 is stale), PlayerInterface* at GameWorld+0x580.
    const int cam = FocusCameraPod(x + dx, y, z + dz);
    if (cam != 1) DebugLog("[XFER] camera focus " + std::string(cam == 0 ? "skipped (no PlayerInterface)" : "FAULTED"));
    DebugLog("[XFER] player teleported by " + F1(dx) + "," + F1(dz) + " to " + F1(x + dx) + "," + F1(y) + "," + F1(z + dz) + " (sector " + SectorString(SectorOf(x + dx, z + dz)) + ")");
    return true;
}
// P25 fold 2 (T780, TEST LEVER). `playerteleport abs <x> <z> <y | ground>` / `playerteleport ground`: the same teleport, camera
// follow and [XFER] line as PlayerTeleport, at a GIVEN height (T776 kept the player's own and landed B on an upper floor, Y 1645.3)
// or the terrain's height at x,z.
bool PlayerTeleportAbsY(float x, float z, bool ground, float y)
{
    ::Character* pc = GetTarget();
    float cx = 0, cy = 0, cz = 0;
    if (!Plaus(pc) || !ReadPos(pc, &cx, &cy, &cz)) return false;
    if (IsDownedCharacter(pc))   // T1338 / T1342: a downed character's ragdoll stays behind and pulls it back when time runs - the move would not hold
    {
        DebugLog("[XFER] player teleport REFUSED: the watched character is down (knocked out / playing dead) - the engine's teleport does not move its ragdoll, it would snap back");
        return false;
    }
    float ty = y;
    if (ground)
    {
        float th = 0.0f;
        const int r = GroundTerrainHeightAt(x, z, &th);
        if (r != 1)
        {
            DebugLog("[XFER] player teleport to the ground REFUSED at " + F1(x) + "," + F1(z) + std::string(r == 0 ? " (no terrain there)" : " (no terrain row, or the read faulted)"));
            return false;
        }
        ty = th;
    }
    if (!Teleport(pc, x, ty, z)) return false;
    ++g_teleports;
    const int cam = FocusCameraPod(x, ty, z);
    if (cam != 1) DebugLog("[XFER] camera focus " + std::string(cam == 0 ? "skipped (no PlayerInterface)" : "FAULTED"));
    DebugLog("[XFER] player teleported by " + F1(x - cx) + "," + F1(z - cz) + " to " + F1(x) + "," + F1(ty) + "," + F1(z) + " (sector " + SectorString(SectorOf(x, z))
             + ") height " + std::string(ground ? "the terrain's" : "given") + ", was Y " + F1(cy));
    return true;
}
bool PlayerTeleportGround()
{
    ::Character* pc = GetTarget();
    float x = 0, y = 0, z = 0;
    if (!Plaus(pc) || !ReadPos(pc, &x, &y, &z)) return false;
    return PlayerTeleportAbsY(x, z, true, 0.0f);
}
// T-1205 THE ADMIN TOOLBOX'S MOVES (admin.cpp /bring /tp /goto /home /jail): ONE character this game owns to the map position x, z,
// through the engine's own whole-character teleport (Character::teleport, the same call PlayerTeleport makes), at the terrain's
// height there (GroundTerrainHeightAt), else at `y` when haveY (a height the position was read at), else refused. A knocked-out
// (downed) character is refused (2): the engine's teleport leaves its ragdoll behind and it snaps back (T1338 / T1342). focus: the
// camera goes to the destination as PlayerTeleport's does (the moved character is this player's lead), so the area there loads.
// The other games' copies follow through this game's own move records. 1 moved (*yOut = the height), 0 refused or faulted, 2 down.
// MAIN THREAD, engine writes allowed (the K2 safe point).
int AdminMoveCharacter(::Character* c, float x, float z, bool haveY, float y, bool focus, float* yOut)
{
    float cx = 0, cy = 0, cz = 0;
    if (!Plaus(c) || !ReadPos(c, &cx, &cy, &cz)) { DebugLog("[ADMIN] move refused: the character's position did not read"); return 0; }
    if (IsDownedCharacter(c)) return 2;
    float th = 0.0f, ty = y;
    const int tr = GroundTerrainHeightAt(x, z, &th);
    if (tr == 1) ty = th;
    else if (!haveY) { DebugLog("[ADMIN] move refused: no ground height read at " + F1(x) + "," + F1(z) + " and no height given"); return 0; }
    if (!Teleport(c, x, ty, z)) { ErrorLog("[ADMIN] move FAILED: the engine's teleport faulted (to " + F1(x) + "," + F1(ty) + "," + F1(z) + ")"); return 0; }
    ++g_teleports;
    const int cam = focus ? FocusCameraPod(x, ty, z) : 0;
    if (focus && cam != 1) DebugLog("[ADMIN] camera focus " + std::string(cam == 0 ? "skipped (no PlayerInterface)" : "FAULTED"));
    DebugLog("[ADMIN] moved from " + F1(cx) + "," + F1(cy) + "," + F1(cz) + " to " + F1(x) + "," + F1(ty) + "," + F1(z) + " (sector "
             + SectorString(SectorOf(x, z)) + ") height " + (tr == 1 ? "the terrain's" : "the one given") + (focus ? ", camera followed" : ""));
    *yOut = ty;
    return 1;
}
// T-1 B0 (TEST LEVER). `playerteleport peer`: the watched player (GetTarget, the character PlayerTeleport moves) to about 5 m
// from this game's COPY of the other player's lead character. A copy = a mirror slot whose uid this game does not own
// (net::IsUidMine false) in the other player's own faction (PeerFactionPod 1, the test PeerPlayerPosition uses); the lead =
// the copy that is its own squad's leader (ActivePlatoon::squadleader, LeaderOf), else the lowest uid. Same engine teleport and
// camera follow as PlayerTeleport. 50 world units = about 5 m (packbuytest's 15 = ~1.5 m scale; Inferred).
// Returns 1 done (*out = "target uid=N lead=L dist=D"), 0 refused (*out = why, logged), 2 no copy of the other player's
// characters is loaded here yet (*out = why, NOT logged: the command's wait retries every 500 ms and logs once itself).
int PlayerTeleportPeer(std::string* out)
{
    const float kPeerOffset = 50.0f;
    out->clear();
    ::Character* pc = GetTarget();
    float x = 0, y = 0, z = 0;
    if (!Plaus(pc) || !ReadPos(pc, &x, &y, &z))
    { *out = "this game has no watched player character, or its position could not be read"; DebugLog("[TRADER] teleport peer MISSED reason=" + *out); return 0; }
    ::Character* best = 0; unsigned int bestUid = 0; int bestLead = 0, copies = 0;
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int uid = 0; ::Character* c = 0;
        if (!MirrorSlot(i, &uid, &c)) continue;
        if (uid == 0 || !Plaus(c) || c == pc || net::IsUidMine(uid)) continue;
        if (PeerFactionPod(c) != 1) continue;
        ++copies;
        void* ap = SquadActivePlatoonOf(c);   /* T-1 B1: the verified reader */
        const int lead = (Plaus(ap) && LeaderOf(ap) == c) ? 1 : 0;
        if (best == 0 || lead > bestLead || (lead == bestLead && uid < bestUid)) { best = c; bestUid = uid; bestLead = lead; }
    }
    if (best == 0)
    { *out = "no copy of the other player's characters is loaded here (peerCopies=0)"; return 2; }
    float tx = 0, ty = 0, tz = 0;
    if (!ReadPos(best, &tx, &ty, &tz))
    { *out = "the target's position could not be read target uid=" + N(bestUid); DebugLog("[TRADER] teleport peer MISSED reason=" + *out); return 0; }
    float ux = x - tx, uz = z - tz;
    const float len = sqrtf(ux * ux + uz * uz);
    if (len < 0.01f) { ux = 1.0f; uz = 0.0f; } else { ux /= len; uz /= len; }
    const float nx = tx + ux * kPeerOffset, nz = tz + uz * kPeerOffset;
    if (!Teleport(pc, nx, ty, nz))
    { *out = "the engine's teleport 0x5C9BF0 faulted target uid=" + N(bestUid); DebugLog("[TRADER] teleport peer MISSED reason=" + *out); return 0; }
    ++g_teleports;
    const int cam = FocusCameraPod(nx, ty, nz);
    DebugLog("[TRADER] teleport peer from " + F1(x) + "," + F1(y) + "," + F1(z) + " to " + F1(nx) + "," + F1(ty) + "," + F1(nz)
             + " target uid=" + N(bestUid) + " lead=" + N(bestLead) + " peerCopies=" + N(copies) + " dist=" + F1(len)
             + " camera=" + N(cam));
    *out = "target uid=" + N(bestUid) + " lead=" + N(bestLead) + " dist=" + F1(len);
    return 1;
}

/* ================= T-1 B3 restructure (review F1-F5 of 2534248, T540; protocol 89): A SQUAD'S MONEY RIDES MSG_SQUAD_LEAD =================
   The pot is the squad's Ownerships::money through the formal leader's getOwnerships (items.cpp KeeperPot, R2 of 2534248); the game
   that runs the formal leader announces it with the squad (HandoffTick, SquadCatsAnnounceNow). K2 SAFE POINT, MAIN THREAD, the AI
   worker paused. Once a second (at once after an announcement, a take, or the last priced request settling):
   (a) TAKEN squads (this game runs the leader the other game still announces): its last value written first, its changes inside the
       hand-over round trip added as deltas (F1); (b) COPY squads: the newest announcement whose formal leader is a copy here gives
       the value of that copy's squad pot, written whenever it differs - no pending list, nothing settled (F2, F3; T540: a copy engine
       that re-seeds its pot after the write is corrected on the next pass) - and held while this game's own priced requests against
       that pot are in flight (F5; an unresolvable one holds every copy write, at most the request's 10 s). */
/* T-1 B3 fold (M1): a member of c's squad is run here (read now, and this game's own record of the squad from the last pass). */
static int SquadMemberRunHere(::Character* c)
{
    SquadView sv;
    if (ReadSquadOf(c, &sv) != 1) return 0;
    std::map<void*, MyLead>::const_iterator r = g_myLead.find(sv.ap);
    if (r != g_myLead.end() && !r->second.members.empty()) return 1;
    for (int k = 0; k < sv.stored; ++k)
    {
        const unsigned int u = Plaus(sv.members[k]) ? FindSpawnedUid(sv.members[k]) : 0u;
        if (u != 0 && net::IsUidMine(u)) return 1;
    }
    return 0;
}
void SquadCatsSafePointDrain()
{
    LeadLinkEdge();
    const int inFlight = KeeperPotsInFlight(0, 0);
    if (g_catsInFlightLast > 0 && inFlight == 0) g_catsDue = 1;   /* point 4: the priced requests settled - a held write goes now */
    g_catsInFlightLast = inFlight;
    const DWORD now = ::GetTickCount();
    if (g_catsDue == 0 && g_catsDrainAt != 0 && now - g_catsDrainAt < 1000) return;
    g_catsDue = 0; g_catsDrainAt = (now == 0) ? 1 : now;
    for (std::map<unsigned int, CatsAdopt>::iterator a = g_catsAdopt.begin(); a != g_catsAdopt.end(); )
    {
        if (!net::IsUidMine(a->first) || g_peerLead.ForLeader(a->first) == 0) g_catsAdopt.erase(a++);   /* the hand-over is over */
        else ++a;
    }
    for (std::map<unsigned int, CatsAdopt>::iterator s = g_catsTakeSnap.begin(); s != g_catsTakeSnap.end(); )
    {
        if (!net::IsUidMine(s->first) || g_catsAdopt.find(s->first) != g_catsAdopt.end()) g_catsTakeSnap.erase(s++);   /* T-1 B3 fold (M2) */
        else ++s;
    }
    /* T-799 D4: restocks the area's holder sent for keepers this game runs - the change taken at the arrival (KeeperRestockArrive)
       is added to the pot, so the moves made here since stay in it. A take-over adoption still due for the squad is superseded: the
       holder sent the restock to this game as the keeper's runner, after the hand-over, and the restock SETS the money, so the older
       value is not written over it (recorded as adopted, as a written adoption is). A hand-over away from this game since the arrival:
       the restock goes on to the new owner. */
    for (std::map<unsigned int, HolderRestockPend>::iterator h = g_holderRestock.begin(); h != g_holderRestock.end(); )
    {
        const unsigned int L = h->first; const HolderRestockPend pd = h->second;
        g_holderRestock.erase(h++);
        ::Character* c = FindSpawned(L);
        const int mine = net::IsUidMine(L) ? 1 : 0;
        void* own = 0; int cur = 0;
        const int read = (mine != 0 && Plaus(c) && KeeperPot((void*)c, &own, &cur) == 1) ? 1 : 0;
        const int act = coopsquad::HolderRestockApply(mine, read);
        if (act == coopsquad::kHrNotRunner) { coopsquad::KeeperRestockMsg m; m.leader = L; m.cats = pd.cats; KeeperRestockResend(m); continue; }
        if (act != coopsquad::kHrApply) { ++g_hrUnread; continue; }
        long long dd = 0; unsigned int ds = 0; int dc = 0;
        if (CatsAdoptAction(SquadActivePlatoonOf(c), L, &dd, &ds, &dc) != coopsquad::kCatsNone)
        {
            CatsAdopt& r = g_catsAdopt[L]; r.seq = ds; r.cats = dc;
            g_catsTakeSnap.erase(L);
            ++g_hrAdoptSuperseded;
        }
        if (coop::ShopKeeperRecorded((void*)c, 0) != 0) continue;   /* the shop record carries this keeper's money (items.cpp ShopTraderTick sets the pot): the restock message writes nothing */
        int want = 0;
        if (coopsquad::HolderRestockWant(cur, pd.delta, &want) == 0 || (want != cur && KeeperPotWritePod(own, want) == 0)) { ++g_hrUnread; continue; }
        KeeperMoneyKnownMark(own, coopsquad::kMoneyEvPeerWritten);
        ++g_hrApplied;
        g_hrResends.erase(L);
        if (g_hrAppliedLogged.insert(L).second)
            DebugLog("[KEEPER] holder restock applied: leader=" + N((long long)L) + " money " + N((long long)cur) + "->" + N((long long)want)
                     + " (restocked to " + N((long long)pd.cats) + "; the moves made here since its arrival kept; once per leader, counted holderRestock[applied])");
    }
    std::vector<unsigned int> leaders;
    g_peerLead.CatsLeaders(&leaders);
    for (std::map<unsigned int, CatsAdopt>::const_iterator s = g_catsTakeSnap.begin(); s != g_catsTakeSnap.end(); ++s)
        if (std::find(leaders.begin(), leaders.end(), s->first) == leaders.end()) leaders.push_back(s->first);   /* held past the giver's n = 0 */
    if (leaders.empty()) return;
    std::vector<void*> flight; int unresolved = 0;
    if (inFlight > 0) KeeperPotsInFlight(&flight, &unresolved);
    /* T-1 B3 fold (M1): the pots this game announces cats for - never written from the other game's value */
    std::vector<void*> ownPots;
    for (std::map<void*, MyLead>::const_iterator r = g_myLead.begin(); r != g_myLead.end(); ++r)
    {
        if (r->second.sent == 0 || r->second.has == 0) continue;
        ::Character* f = FindSpawned(r->second.formal);
        void* o = 0; int oc = 0;
        if (Plaus(f) && KeeperPot((void*)f, &o, &oc) == 1) ownPots.push_back(o);
    }
    std::vector<CatsCopyPot> pots;
    for (size_t i = 0; i < leaders.size(); ++i)
    {
        const unsigned int L = leaders[i];
        ::Character* c = FindSpawned(L);
        if (!Plaus(c)) continue;                     /* not here: nothing to write; the next pass looks again */
        void* own = 0; int cur = 0;
        if (net::IsUidMine(L))
        {
            long long delta = 0; unsigned int eSeq = 0; int eCats = 0;
            const int act = CatsAdoptAction(SquadActivePlatoonOf(c), L, &delta, &eSeq, &eCats);
            if (act == coopsquad::kCatsNone) continue;
            if (coop::ShopKeeperRecorded((void*)c, 0) != 0)
            {   /* the shop record carries this keeper's money (items.cpp ShopTraderTick sets the pot from it): the hand-over's value is
                   not written; taken as adopted so it is not asked again */
                CatsAdopt& r = g_catsAdopt[L]; r.seq = eSeq; r.cats = eCats;
                g_catsTakeSnap.erase(L);
                continue;
            }
            if (KeeperPot((void*)c, &own, &cur) != 1) { CatsPotUnread(L, eSeq); continue; }
            const int potFlight = (unresolved > 0 || std::find(flight.begin(), flight.end(), own) != flight.end()) ? 1 : 0;
            if (coopsquad::TakeoverAdoptHeld(act, potFlight) != 0) { ++g_catsAdoptHeld; continue; }   /* T-1 B3 fold (M2): after they settle */
            const long long want = (act == coopsquad::kCatsAbsolute) ? (long long)eCats : (long long)cur + delta;
            if (want > 2147483647LL || want < -2147483647LL - 1LL) { CatsPotUnread(L, eSeq); continue; }
            if (want != (long long)cur && KeeperPotWritePod(own, (int)want) == 0) { CatsPotUnread(L, eSeq); continue; }
            KeeperMoneyKnownMark(own, coopsquad::kMoneyEvPeerWritten);   /* T-799 D1: the other game's value is this pot's now */
            CatsAdopt& r = g_catsAdopt[L]; r.seq = eSeq; r.cats = eCats;
            g_catsTakeSnap.erase(L);
            if (act == coopsquad::kCatsAbsolute) ++g_catsHandoverAdopted; else if (delta != 0) ++g_catsHandoverDelta;
            if (act == coopsquad::kCatsAbsolute || delta != 0)
                DebugLog("[XFER] squad cats " + std::string(act == coopsquad::kCatsAbsolute ? "taken over" : "hand-over change")
                         + ": leader=" + N(L) + " cats=" + N(want) + " was=" + N((long long)cur)
                         + (act == coopsquad::kCatsDelta ? " delta=" + N(delta) : std::string()) + " seq=" + N(eSeq)
                         + (act == coopsquad::kCatsAbsolute ? " (the other game's last announcement, written before this game announces the squad)"
                                                            : " (the giving game's own change inside the hand-over round trip)"));
            continue;
        }
        const coopsquad::PeerLeadEntry* e = g_peerLead.ForLeader(L);
        if (e == 0) continue;
        if (KeeperPot((void*)c, &own, &cur) != 1) { CatsPotUnread(L, e->seq); continue; }
        const int announcedHere = (std::find(ownPots.begin(), ownPots.end(), own) != ownPots.end()) ? 1 : 0;
        if (coopsquad::CopyPotOwnSkip(announcedHere, announcedHere != 0 ? 1 : SquadMemberRunHere(c)) != 0) { ++g_catsOwnPotSkipped; continue; }   /* T-1 B3 fold (M1) */
        size_t k = 0;
        while (k < pots.size() && pots[k].own != own) ++k;
        if (k == pots.size()) { CatsCopyPot p; p.own = own; p.leader = L; p.seq = e->seq; p.cats = e->cats; p.cur = cur; pots.push_back(p); }
        else if (e->seq > pots[k].seq) { pots[k].leader = L; pots[k].seq = e->seq; pots[k].cats = e->cats; }   /* the newest wins */
    }
    for (size_t k = 0; k < pots.size(); ++k)
    {
        const CatsCopyPot& p = pots[k];
        if (coop::ShopKeeperRecorded((void*)FindSpawned(p.leader), 1) != 0) continue;   /* the shop record carries this keeper's money: it sets every copy (items.cpp ShopTraderTick), the runner's announcement does not */
        const int held = (unresolved > 0 || std::find(flight.begin(), flight.end(), p.own) != flight.end()) ? 1 : 0;
        const int act = coopsquad::CopyPotAction(p.cats, p.cur, held);
        if (act == coopsquad::kPotEqual) { KeeperMoneyKnownMark(p.own, coopsquad::kMoneyEvPeerWritten); continue; }   /* T-799 D1: it holds the runner's value */
        if (act == coopsquad::kPotHold) { ++g_catsHeld; continue; }   /* F5: written by the first pass after they settle */
        if (KeeperPotWritePod(p.own, p.cats) == 0) { CatsPotUnread(p.leader, p.seq); continue; }
        KeeperMoneyKnownMark(p.own, coopsquad::kMoneyEvPeerWritten);   /* T-799 D1 */
        ++g_catsApplied; ++g_catsLogIn;
        // PROBE-START: P133 - this copy write changed the keeper group's money
        P133KeeperCopyWrite(p.leader, p.cur, p.cats);
        // PROBE-END: P133
        if (g_catsLogIn <= 40 || g_catsLogIn % 20 == 0)
            DebugLog("[KEEPER] <- squad cats leader=" + N(p.leader) + " cats=" + N((long long)p.cats) + " applied was=" + N((long long)p.cur)
                     + " seq=" + N(p.seq));
    }
}
/* The request road moved the pot of a squad this game runs (ItKeeperPay; F4's undo): its announcement goes at once with the new
   cats - the same record re-sent (key, leaders, members), so it is on the reliable channel before the CONFIRM. A squad not yet
   announced with its money is left to the 1-Hz pass. */
void SquadCatsAnnounceNow(void* keeper)
{
    if (!Plaus(keeper) || !coop::PlayersPresent()) return;
    LeadLinkEdge();
    void* ap = SquadActivePlatoonOf((::Character*)keeper);
    if (ap == 0) return;
    std::map<void*, MyLead>::iterator it = g_myLead.find(ap);
    if (it == g_myLead.end() || it->second.sent == 0 || it->second.has == 0) return;
    CatsResendRecord(it->second);   /* T-1 B3 fold (M2): the pot as announced - net of this game's in-flight predictions */
}
void SquadCatsNoteUndo(int kind, int shortfall)   /* T-1 B3 fold (M2, L1) */
{
    if (kind == 1) { ++g_catsUndoRefused; if (shortfall > 0) g_catsUndoShort += shortfall; }
    else if (kind == 2) ++g_catsUndoNotRunner;
    else if (kind == 3) ++g_catsUndoPotChanged;
    else ++g_catsUndoFailed;
}
std::string SquadCatsReport()
{
    return " squadCats[sent,applied,held,handoverAdopted,undoRefused,potUnread]=" + N(g_catsSent) + "," + N(g_catsApplied) + ","
           + N(g_catsHeld) + "," + N(g_catsHandoverAdopted) + "," + N(g_catsUndoRefused) + "," + N(g_catsPotUnread)
           + " squadCats2[handoverDelta,undoFailed]=" + N(g_catsHandoverDelta) + "," + N(g_catsUndoFailed)
           + " adopting=" + N((long long)g_catsAdopt.size())
           + " squadCats3[ownPotSkipped,adoptHeld,finalSent,undoNotRunner,undoPotChanged,undoShortCats]=" + N(g_catsOwnPotSkipped) + ","
           + N(g_catsAdoptHeld) + "," + N(g_catsFinalSent) + "," + N(g_catsUndoNotRunner) + "," + N(g_catsUndoPotChanged) + "," + N(g_catsUndoShort)
           + " takeSnaps=" + N((long long)g_catsTakeSnap.size())   /* T-1 B3 fold */
           + " keeperMoney[knownPots,marks,withheldNow,engineLoaded,recordApplied,recordNoMoney,gone,cleared,recordCarried,recordNotCarried,savedUnknown,potMismatch,listWritten,listWriteFailed,listRead,listBad]="
           + N((long long)g_moneyKnown.size()) + "," + N(g_moneyKnownMarks) + "," + N(g_catsWithheldNow) + ","
           + N(g_moneyEv[coopsquad::kMoneyEvEngineLoaded]) + "," + N(g_moneyEv[coopsquad::kMoneyEvRecordApplied]) + ","
           + N(g_moneyEv[coopsquad::kMoneyEvRecordNoMoney]) + "," + N(g_moneyEv[coopsquad::kMoneyEvGone]) + "," + N(g_moneyKnownCleared) + ","
           + N(g_recMoneyCarried) + "," + N(g_recMoneyNotCarried) + "," + N(g_moneyEv[coopsquad::kMoneyEvSavedUnknown]) + "," + N(g_keeperPotMismatch) + ","
           + N(g_moneyListWritten) + "," + N(g_moneyListWriteFailed) + "," + N(g_moneyListRead) + "," + N(g_moneyListBad)   /* T-799 D1 / D2 */
           + " holderRestock[sent,sendFailed,arrived,applied,notRunner,unread,resent,adoptSuperseded]=" + N(g_hrSent) + "," + N(g_hrSendFailed) + ","
           + N(g_hrArrived) + "," + N(g_hrApplied) + "," + N(g_hrNotRunner) + "," + N(g_hrUnread) + "," + N(g_hrResent) + "," + N(g_hrAdoptSuperseded);   /* T-799 D4 */
}

} // namespace coop
