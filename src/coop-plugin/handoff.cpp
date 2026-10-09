// handoff.cpp - M-D step 1. See handoff.h.
#include "handoff.h"
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
namespace coop { int KeeperPot(void* character, void** ownOut, int* catsOut); int KeeperPotWritePod(void* own, int cats);
                 int KeeperPotsInFlight(std::vector<void*>* pots, int* unresolved); }   /* T-1 B3 restructure: items.cpp - the squad's pot */
namespace coop { int KeeperPotAnnounced(void* character, void** ownOut, int* catsOut); }
namespace coop { std::string StoreWorldIdOf(void* platoon); }   /* M7a3f3 T-425 [m7a3f3-hc0]: store.cpp (store.h) - the squad's engine id, "" if unreadable */
namespace coop { int GroundTerrainHeightAt(float x, float z, float* out); }   /* P25 fold 2: combat.cpp - 1 height, 0 no terrain, -1 no row / faulted. MAIN THREAD */
namespace coop { int ItTraderHomePos(void* ap, void* platoon, void* leader, float* x, float* z); }   /* T-164 B4-3 (M4): items.cpp - a trader squad's home building position */   /* T-1 B3 fold (M2): items.cpp - net of this game's in-flight predictions */
namespace coop { bool StoreLiveReady(); int StoreRosterSlotInWorld(int slot); int StoreMySlot(); bool EngineWritesBlocked(); }   /* M7a A1 build 2 [a1b2-hd9]: store.cpp (store.h) */
#include <algorithm>

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

typedef std::pair<void*, std::string> HoKey;   /* (faction, the engine's squad id): a squad's key (SquadKeyOf) */
struct Pending { std::vector<net::XferMember> members; std::map<unsigned int, HoKey> keyOf; /* [a1b2-hc1]: each member's squad key, read at the send */ unsigned int reason; double sentAt; int sends; void* ap; int logged; int targetSlot; Pending() : reason(0), sentAt(0.0), sends(0), ap(0), logged(0), targetSlot(-1) {} };   /* M7a A1 build 1 [a1b1-hd1] [review F11]: targetSlot - fixed at the first send; every resend and the ACK's sender test use it */   /* protocol 84: reason = coopsquad::kXferReason*. T-1 B1 restructure: ap = my squad the members were read from (the crossing test), logged = its xfer_out line was written (N5: its ACK line is written exactly then) */
long long g_ackWrongSender = 0;   /* M7a A1 build 1 [a1b1-hd1]: XFER_ACKs from a game that was not the XFER's target - ignored */
std::map<unsigned int, Pending> g_pending;   // leader uid -> squad in flight (owner side)
/* ==== M7a A1 build 2 [a1b2-hc0] (design 2.2, 2.3, 2.5) - RETIRED here: the owed hand-overs, the held UNLOADs, the put-away batch and
   flush and their cover judgement, the member notes, the handed-over marks, the left-behind notes, the receiver's hand-back record and
   MSG_HANDBACK. In their place: THE SQUAD INDEX (per person) and THE RELEASE RECORDS (one open offer each). MAIN THREAD, all of it. ==== */
/* 2.3 the squad index: per squad key, the people of it another game runs (given: uid -> that game's key) and the people of it this
   game's engine put away that nobody adopted (asleepHere); g_idxOf: uid -> its squad key (one entry per indexed person) */
struct SquadIdx { std::map<unsigned int, unsigned int> given; std::set<unsigned int> asleepHere; std::set<unsigned int> keptHere; int liveHere; SquadIdx() : liveHere(0) {} };   /* [a1b2f1-hd0]: keptHere, liveHere */
std::map<HoKey, SquadIdx> g_squadIdx;
std::map<unsigned int, HoKey> g_idxOf;
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
struct RelMember { net::XferMember m; HoKey key; int hasKey; unsigned int baseGen; RelMember() : hasKey(0), baseGen(0) { std::memset(&m, 0, sizeof(m)); } };
struct ReleaseRec
{
    unsigned int id, keyUid; Sector sector; std::vector<RelMember> mem; std::vector<int> tried;
    int candidate; double sentAt, clock, lastTick; int sends, defers, flight, held;
    float px, pz;   /* the squad's decision position at the put-away - the receivers' keep rule is asked there */
    long long nAdopted, nAsleep, nCancelled, nMoved;
    ReleaseRec() : id(0), keyUid(0), candidate(-1), sentAt(0.0), clock(0.0), lastTick(0.0), sends(0), defers(0), flight(0), held(0),
                   nAdopted(0), nAsleep(0), nCancelled(0), nMoved(0), px(0.0f), pz(0.0f) { sector.x = -1; sector.y = -1; }
};
std::map<unsigned int, ReleaseRec> g_release;   /* id -> open record */
unsigned int g_releaseNextId = 0;
/* the frame's put-aways per engine squad (NotifyDespawn's main-thread branch), opened by ReleaseFlush at the next frame */
struct RelBatch { unsigned int keyUid; Sector sector; std::vector<RelMember> mem; float px, pz; RelBatch() : keyUid(0), px(0.0f), pz(0.0f) { sector.x = -1; sector.y = -1; } };
std::map<void*, RelBatch> g_relBatch;
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
/* T-1 B1 restructure (re-check N1-N5 of ff655c5): MSG_SQUAD_LEAD (src/common/squadlead.h). SENDER: one record per squad this game
   runs members of (keyed by its ActivePlatoon*) - the key it is announced under and what was last SENT; it is announced again when
   any of that changes, to every new link, and (n = 0) when this game runs no member of it any more. A send that fails stays due and
   goes again next second (N3a). RECEIVER: g_peerLead, the other game's announcements (the newest seq per its key). */
struct MyLead { unsigned int key; unsigned int acting, formal; std::vector<unsigned int> members; int sent; int has; int cats; int areaKey; };   /* T-1 B3 restructure: has / cats - the money last sent. Fold 1 [a1b1f1-hd0] [F3]: areaKey - the leader's area key the last announcement went with (-1 none: WORLD) */
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
bool LeaderOut(::Character* c)
{
    if (!Plaus(c)) return true;
    if (LeaderDeadOrHeldPod(c) != 0) return true;   // unreadable fails closed
    return IsDownedCharacter(c);   // knocked out / ragdolled: the mod's one downed predicate (replicate.cpp), for the engine's vt+0x30 test
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
        r.key = g_myLeadKeyNext; r.acting = 0; r.formal = 0; r.sent = 0; r.has = 0; r.cats = 0; r.areaKey = -1;
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
    r.acting = acting; r.formal = formal; r.members.swap(mem); r.sent = 1; r.has = has; r.cats = money; r.areaKey = areaKey;
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
    ++g_squadLeadSent; ++g_catsSent; r.cats = cats;
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
        ReadAuthorityIntent(c, &m.intentType, &m.intentSubject, &m.ix, &m.iy, &m.iz);
        out->push_back(m);
    }
}
long long g_nudges = 0, g_keptUntaken = 0, g_xferInOutsideRing = 0;
long long g_tick = 0, g_squadsSeen = 0, g_xferOut = 0, g_xferResent = 0, g_xferAbandoned = 0, g_ackIn = 0, g_released = 0;
long long g_xferIn = 0, g_taken = 0, g_intentsGiven = 0, g_intentsSubjectUnresolved = 0, g_ackOut = 0, g_xferInUnknownUid = 0, g_teleports = 0;

} // namespace

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
static void ReleaseForgetDrain()
{
    if (g_relWalkDepth > 0)
    {   /* [a1b2f1-hd3] [review F1]: a nested call under a live walk / reference - never cleared under it; the outermost entry's next drain
           (HandoffTick's, every frame) honours the request */
        if (g_relForgetReq != 0 || g_idxPeerGoneReq != 0 || !g_relRevokeGoneSlots.empty() || !g_idxGoneSlots.empty()) ++g_relDrainDeferred;   /* [a1b2f2-hd8] */
        return;
    }
    if (::InterlockedCompareExchange(&g_idxPeerGoneReq, 0, 1) == 1)
    {
        const long long n = (long long)g_squadIdx.size();
        g_squadIdx.clear(); g_idxOf.clear(); g_keptRecent.clear(); g_keyCache.clear(); g_idxGoneSlots.clear(); ++g_idxPeerGoneCleared;   /* [a1b2f1-hd3] */
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
                g_idxOf.erase(g->first);
                s->second.given.erase(g++);
                ++dropped; ++people;
            }
            if (dropped == 0 || !s->second.given.empty()) { ++s; continue; }
            for (std::set<unsigned int>::const_iterator a = s->second.asleepHere.begin(); a != s->second.asleepHere.end(); ++a) g_idxOf.erase(*a);
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
        g_relRevokeWaitPlayerGone += nw; g_relRevokeOwedPlayerGone += no;
        if (nw + no > 0) DebugLog("[RELEASE] a player gone: REVOKEs dropped - waiting from it " + N(nw) + ", owed to it " + N(no));
    }
    if (::InterlockedCompareExchange(&g_relForgetReq, 0, 1) != 1) return;
    const long long r = (long long)g_release.size(), b = (long long)g_relBatch.size(), s = (long long)g_squadIdx.size();
    for (std::map<unsigned int, ReleaseRec>::const_iterator it = g_release.begin(); it != g_release.end(); ++it)
        DebugLog("[RELEASE] id=" + N((long long)it->first) + " END cleared by the world teardown (" + N((long long)it->second.mem.size()) + " member(s) open)");
    g_release.clear(); g_relBatch.clear(); g_relLate.clear(); g_squadIdx.clear(); g_idxOf.clear(); g_keptRecent.clear(); g_keyCache.clear(); g_keepNoted.clear(); g_niwNoted.clear(); g_forcedTry.clear();   /* [a1b2f1-hd3]; T-650 fold 1: the forced hand-over's notes and back-offs */
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
    std::map<HoKey, SquadIdx>::iterator s = g_squadIdx.find(o->second);
    if (s != g_squadIdx.end())
    {
        s->second.given.erase(uid); s->second.asleepHere.erase(uid);
        if (s->second.given.empty() && s->second.asleepHere.empty()) g_squadIdx.erase(s);
    }
    g_idxOf.erase(o);
}
static void IdxPut(unsigned int uid, const HoKey& k, int asleep, unsigned int owner)
{
    IdxErase(uid);
    SquadIdx& s = g_squadIdx[k];
    if (asleep != 0) { s.asleepHere.insert(uid); ++g_idxAsleepHere; } else { s.given[uid] = owner; ++g_idxGiven; }
    g_idxOf[uid] = k;
    std::map<HoKey, KeptRecent>::iterator kr = g_keptRecent.find(k);   /* [a1b2f1-hd4] [review F5]: kept people of it noted before it was indexed */
    if (kr != g_keptRecent.end()) { s.keptHere.insert(kr->second.uids.begin(), kr->second.uids.end()); g_keptRecent.erase(kr); }
    s.keptHere.erase(uid);
}
/* [a1b2f1-hd5] [review F5]: uid, a person of this game put away and NOT offered (or its offer cancelled by a re-wake), stays in this game's
   world data only - kept here. The key: as read from the whole body (have 1), else the 1 Hz scan's cache; unknown -> counted (a leftover). */
static void IdxKeepKey(unsigned int uid, const HoKey& k, int have)
{
    if (have == 0)
    {
        std::map<unsigned int, HoKey>::const_iterator ck = g_keyCache.find(uid);
        if (ck == g_keyCache.end()) { ++g_idxKeptUnknown; return; }
        const HoKey cached(ck->second);
        IdxKeepKey(uid, cached, 1);
        return;
    }
    ++g_idxKept;
    std::map<HoKey, SquadIdx>::iterator s = g_squadIdx.find(k);
    if (s != g_squadIdx.end()) { s->second.keptHere.insert(uid); return; }
    KeptRecent& r = g_keptRecent[k]; r.uids.insert(uid); r.at = NowSec();
}
static void IdxKeep(unsigned int uid, ::Character* c)   /* c: a WHOLE body, or 0 (nothing may be read) */
{
    void* f = 0; std::string id;
    if (c != 0 && Plaus(c) && SquadKeyOf(c, &f, &id)) { IdxKeepKey(uid, HoKey(f, id), 1); return; }
    IdxKeepKey(uid, HoKey(), 0);
}
/* the XFER ACK's release: the key from the body when it is still here, else as read when the XFER was built (Pending::keyOf) */
static void IdxGiveAtAck(unsigned int uid, ::Character* rel, const Pending& p, unsigned int to)
{
    void* f = 0; std::string id;
    if (Plaus(rel) && SquadKeyOf(rel, &f, &id)) { IdxPut(uid, HoKey(f, id), 0, to); return; }
    std::map<unsigned int, HoKey>::const_iterator k = p.keyOf.find(uid);
    if (k != p.keyOf.end()) { IdxPut(uid, k->second, 0, to); return; }
    ++g_idxNoKey;
}
/* an XFER's members' squad keys, read at the send while the bodies are whole (the ACK may come after the engine put them away) */
static void PendingKeysRead(Pending* p)
{
    for (size_t k = 0; k < p->members.size(); ++k)
    {
        void* f = 0; std::string id;
        if (SquadKeyOf(FindSpawned(p->members[k].uid), &f, &id)) p->keyOf[p->members[k].uid] = HoKey(f, id);
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
bool HandedOverAny()
{
    ReleaseForgetDrain();
    for (std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.begin(); s != g_squadIdx.end(); ++s) if (!s->second.given.empty()) return true;
    return false;
}
/* the sweep adopted the squad's rebuilt people under the area rule (a fresh map says no other game holds the area and no other game's
   copy of it is live here): the squad is this game's again - its index entry goes */
void HandedOverUnmark(void* faction, const std::string& id, int why)
{
    ReleaseForgetDrain();
    std::map<HoKey, SquadIdx>::iterator s = g_squadIdx.find(HoKey(faction, id));
    if (s == g_squadIdx.end()) return;
    for (std::map<unsigned int, unsigned int>::const_iterator g = s->second.given.begin(); g != s->second.given.end(); ++g) g_idxOf.erase(g->first);
    for (std::set<unsigned int>::const_iterator a = s->second.asleepHere.begin(); a != s->second.asleepHere.end(); ++a) g_idxOf.erase(*a);
    g_squadIdx.erase(s); ++g_idxSquadCleared;
    DebugLog("[RELEASE] squad id='" + id + "' out of the squad index - " + (why == 0 ? std::string("taken back") : std::string("its rebuilt people are adopted here (the area rule)"))
             + " (index squadCleared=" + N(g_idxSquadCleared) + ")");
}
void SquadIdxTaken(unsigned int uid)
{
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
   game's own, the squad's money snapshot, the intent. MAIN THREAD (the drain - engine writes allowed). */
static void TakeMemberFromPeer(const net::XferMember& mm, unsigned int gen, int* intents, int* unresolved)
{
    ::Character* c = FindSpawned(mm.uid);
    net::TakeLocalOwner(mm.uid, gen);
    NoteAnnouncedOnTake(mm.uid);   /* M7a3f3 [m7a3f3-mg1]: the giver keeps a copy - its later withdrawal is owed by this game */
    ForgetSweepAdopt(mm.uid);      /* inv7e2: received by hand-over - never a reloaded copy */
    UnpuppetForOwnership(mm.uid);
    ++g_taken;
    g_catsDue = 1;   /* T-1 B3 restructure (F1): a taken squad's money is adopted at the next safe point */
    {   /* T-1 B3 fold (M2): the giver's newest announcement of this leader, kept for a take-over write held past its n = 0 */
        const coopsquad::PeerLeadEntry* te = g_peerLead.ForLeader(mm.uid);
        if (te != 0 && g_catsAdopt.find(mm.uid) == g_catsAdopt.end()) { CatsAdopt& s = g_catsTakeSnap[mm.uid]; s.seq = te->seq; s.cats = te->cats; }
    }
    if (mm.intentType > 0 && Plaus(c))
    {
        ::Character* subj = mm.intentSubject ? FindSpawned(mm.intentSubject) : 0;
        if (mm.intentSubject && !Plaus(subj)) { ++*unresolved; ++g_intentsSubjectUnresolved; }
        else if (InjectOrder(c, mm.intentType, subj, Ogre::Vector3(mm.ix, mm.iy, mm.iz))) { ++*intents; ++g_intentsGiven; }
    }
}
static void RelLog(const std::string& s) { if (g_relLogged >= kRelLogCap) return; ++g_relLogged; DebugLog(s); }
static std::string SlotText(int s) { return s >= 0 ? "slot " + N((long long)s) : std::string("no slot"); }
static unsigned int RelNewId() { if (++g_releaseNextId == 0) ++g_releaseNextId; return g_releaseNextId; }
static unsigned int RelOfferGen(const ReleaseRec& r, const RelMember& m) { return cooplo::ReleaseOfferGen(m.baseGen, (int)r.tried.size()); }
static int RelIndexOf(const ReleaseRec& r, unsigned int uid) { for (size_t k = 0; k < r.mem.size(); ++k) if (r.mem[k].m.uid == uid) return (int)k; return -1; }
static void RelEnd(const ReleaseRec& r, const std::string& how)
{
    RelLog("[RELEASE] id=" + N((long long)r.id) + " END " + how + " (adopted " + N(r.nAdopted) + ", asleep " + N(r.nAsleep) + ", cancelled " + N(r.nCancelled)
           + ", moved to a split " + N(r.nMoved) + "; candidates tried " + N((long long)r.tried.size()) + ")");
}
static bool RelSendOffer(ReleaseRec& r, int resend)
{
    cooplo::ReleaseMsg msg; msg.id = r.id; msg.keyUid = r.keyUid; msg.flags = cooplo::kRelFlagPutAway;
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
/* [a1b2f1-hd8] [review F3]: is squad k awake in this game's engine again - an active squad of its faction with that id and a member?
   1 yes, 0 no, -1 unreadable (taken as no). Guarded reads only. */
static int SquadAwakeHere(const HoKey& k, SquadView* found = 0)   /* [a1b2f2-hd5]: found = the awake squad (its area decides G1) */
{
    if (!Plaus(k.first) || k.second.empty()) return -1;
    void** items = 0; unsigned int n = 0;
    if (FactionActiveListPod(k.first, &items, &n) == 0 || !Plaus(items) || n > 4096) return -1;
    for (unsigned int i = 0; i < n; ++i)
    {
        void* ap = 0;
        if (ActiveOfListPod(items, i, &ap) == 0 || !Plaus(ap)) continue;
        SquadView sv;
        if (ReadSquadAt(ap, &sv) != 1 || sv.count <= 0 || !Plaus(sv.platoon)) continue;
        if (StoreWorldIdOf(sv.platoon) == k.second) { if (found != 0) *found = sv; return 1; }   /* [a1b2f2-hd9] */
    }
    return 0;
}
/* fold 2 [a1b2f2-hd5] [review G1]: the re-woken squad's area, read at this moment the way the sweep reads it (worldsync.cpp
   AnnounceAreaDecide: HeldByOtherTS of the sector of the squad's decision position, the position SquadDecisionPos gives the sweep).
   *awake 1 = the squad is awake in this game's engine again. Returns 1 another game holds the area, 0 this game (the sweep adopts the
   re-woken body), -1 unknown (no key, not awake, no readable position, or no fresh area map). */
static int RewakeAreaHeldByOther(const RelMember& rm, int* awake)
{
    *awake = 0;
    if (rm.hasKey == 0) return -1;
    SquadView sv;
    if (SquadAwakeHere(rm.key, &sv) != 1) return -1;
    *awake = 1;
    float x = 0.0f, z = 0.0f;
    if (SquadDecisionPosAt(sv, &x, &z) != 1) return -1;
    return HeldByOtherTS(SectorOf(x, z));
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
    SquadCatsFinalBeforeRelease(uid);
    net::ReleaseLocalOwner(uid, peer, gen);
    SquadCatsReleased(uid);
    WorldStateOnOwnershipReleased(uid, 0);
    if (rm.hasKey != 0) IdxPut(uid, rm.key, 0, peer); else ++g_idxNoKey;
    ++g_relAdopted; ++g_released;
    RelDone& d = g_relDone[uid]; d.slot = slot; d.gen = gen; d.at = NowSec();
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
static int RelAsleep(const RelMember& rm, int cancelled, unsigned int settleGen)   /* [a1b2f1-hd9]: 0 asleep, 1 a late adopter accepted, 2 cancelled */
{
    const unsigned int uid = rm.m.uid;
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
    if (cancelled != 0) { ++g_relCancelled; IdxKeepKey(uid, rm.key, rm.hasKey); }   /* [a1b2f1-hd9] [review F2]: in this game's world data only - kept here */
    else { ++g_relAsleep; if (rm.hasKey != 0) IdxPut(uid, rm.key, 1, 0); else ++g_idxNoKey; }
    RelDone& d = g_relDone[uid]; d.slot = -1; d.gen = settleGen; d.at = NowSec();
    if (net::IsUidMine(uid)) WithdrawHeldUnload(uid);   /* the UNLOAD held since the put-away (ReleasePendingHas is false for it now) */
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
        if (endedAs == 1) ++r.nAdopted; else if (endedAs == 2) { ++r.nCancelled; ++nc; } else ++r.nAsleep;   /* [a1b2f2-hd2b] */
    }
    if (endLine != 0) RelEnd(r, how);
    else RelLog("[RELEASE] id=" + N((long long)r.id) + " " + how + " (" + N((long long)mem.size()) + " member(s): adopted late " + N(r.nAdopted) + ", cancelled " + N(r.nCancelled) + ")");
    return nc;   /* [a1b2f2-hd2c] */
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
            ++g_relNotMine; r.mem.erase(r.mem.begin() + (long)k);   /* no longer this game's (a hire, a dual run's yield): nothing to offer */
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
        if (st == cooplo::kRsResend) { RelSendOffer(r, 1); return true; }
        if (st == cooplo::kRsOffer)
        {
            r.candidate = order[0]; r.clock = 0.0; r.defers = 0; r.sends = 0; r.sentAt = now;
            const bool ok = RelSendOffer(r, 0);
            ++g_holderOffer; if (niw > 0) ++g_holderNotInWorld; if (nk > 0) ++g_keepNotKeep;
            RelLog("[RELEASE] id=" + N((long long)r.id) + " -> " + SlotText(r.candidate) + " key=" + N((long long)r.keyUid) + " n=" + N((long long)r.mem.size()) + " sector=" + SectorString(r.sector)
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
    return false;
}
/* M7a A1 build 2 [a1b2-hd3]: uid is still being handed on by this game - its UNLOAD is held (worldsync.cpp's three withdrawal paths) and
   a ROSTER CHECK is answered PENDING (design 2.5 item 8 [review F1]) */
bool ReleasePendingHas(unsigned int uid)
{
    ReleaseForgetDrain();
    return RelHolds(uid) || HandoffPendingHas(uid);
}
/* every frame (HandoffTick, before the 1 Hz gate): the last frame's put-aways become records - at most 64 people each - and are offered */
static void ReleaseFlush(double now)
{
    if (g_relBatch.empty()) return;
    RelWalk walk;   /* [a1b2f1-hd10] [review F1] */
    std::map<void*, RelBatch> batch;
    batch.swap(g_relBatch);
    for (std::map<void*, RelBatch>::iterator it = batch.begin(); it != batch.end(); ++it)
    {
        const RelBatch& b = it->second;
        for (size_t at = 0; at < b.mem.size(); at += (size_t)cooplo::kReleaseMaxMembers)
        {
            const size_t to = (at + (size_t)cooplo::kReleaseMaxMembers < b.mem.size()) ? at + (size_t)cooplo::kReleaseMaxMembers : b.mem.size();
            ReleaseRec r; r.id = RelNewId(); r.keyUid = b.keyUid; r.sector = b.sector; r.px = b.px; r.pz = b.pz;
            for (size_t k = at; k < to; ++k) r.mem.push_back(b.mem[k]);
            g_relSent += (long long)r.mem.size();
            RelLog("[RELEASE] id=" + N((long long)r.id) + " OPEN key=" + N((long long)r.keyUid) + " n=" + N((long long)r.mem.size()) + " sector=" + SectorString(r.sector)
                   + " - this game's engine put them away (release sent " + N(g_relSent) + ")");
            if (RelAdvance(r, now)) g_release[r.id] = r;
        }
    }
}
/* 1 Hz: every open record's next step; the settled-uid book pruned */
static void ReleaseTick(double now)
{
    RelWalk walk;   /* [a1b2f1-hd10] [review F1]: the walk of g_release below - a nested drain (RelAsleep -> WithdrawHeldUnload -> ReleasePendingHas) defers */
    RelRevokeOwedTick();   /* [a1b2f1-hd7] [review F4] */
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
        if (std::find(taken.begin(), taken.end(), u) != taken.end()) continue;
        if (!net::IsUidMine(u) || Plaus(FindSpawned(u)) || RelHolds(u)) continue;   /* live here: still this game's - the pass decides */
        RelMember rm; rm.m = p.members[k]; rm.baseGen = net::MineGenOf(u);
        std::map<unsigned int, HoKey>::const_iterator kk = p.keyOf.find(u);
        if (kk != p.keyOf.end()) { rm.key = kk->second; rm.hasKey = 1; }
        if (rm.hasKey == 0)
        {   /* [a1b2f1-hd11] [review F3]: no squad key - never offered (a re-wake could not cancel it): it sleeps here, its held UNLOAD goes now */
            ++g_relFlightNoKey; IdxKeepKey(u, HoKey(), 0); WithdrawHeldUnload(u);
            continue;
        }
        r.mem.push_back(rm);
    }
    if (r.mem.empty()) return;
    r.id = RelNewId(); r.keyUid = net::IsUidMine(leader) ? leader : r.mem[0].m.uid; r.sector = SectorOf(r.mem[0].m.x, r.mem[0].m.z); r.px = r.mem[0].m.x; r.pz = r.mem[0].m.z;
    g_relSent += (long long)r.mem.size();
    if (abandoned != 0) g_relFlightAbandoned += (long long)r.mem.size(); else g_relFlightNotTaken += (long long)r.mem.size();
    RelLog("[RELEASE] id=" + N((long long)r.id) + " OPEN key=" + N((long long)r.keyUid) + " n=" + N((long long)r.mem.size()) + " sector=" + SectorString(r.sector)
           + " - members of XFER leader=" + N((long long)leader) + (abandoned != 0 ? std::string(" (abandoned, no ACK)") : std::string(" (not taken)")) + " whose body this game's engine put away while it flew");
    if (RelAdvance(r, NowSec())) g_release[r.id] = r;
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
    ApplyRemoteUnload(uid);
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
        if (now - r->second.at > kRelDoneKeepSec || now < r->second.at) g_keptRecent.erase(r++); else ++r;
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
    int intents = 0, unresolved = 0; long long stale = 0;
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
        if (v == cooplo::kAdopt) { TakeMemberFromPeer(mm[i], msg.rows[i].gen, &intents, &unresolved); ack.adopted.push_back(uid); ++g_relInAdopted; continue; }
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
    RelLog("[RELEASE] <- id=" + N((long long)msg.id) + " from " + SlotText(from) + " n=" + N((long long)mm.size()) + " adopted=" + N((long long)ack.adopted.size())
           + " dropped=" + N((long long)ack.dropped.size()) + " (stale " + N(stale) + ") deferred=" + N((long long)ack.deferred.size()) + " intents=" + N((long long)intents)
           + (sent ? std::string() : std::string(" - ACK NOT SENT (no road; the giver resends)")));
}

/* 1. resend / abandon pending transfers (recurrence-covered: nothing is consumed until the ACK). T-1 B3 fold (L3): run at the END of
   a pass that ran its squads, each gated like a first send (coopsquad::ResendMayGo over HandoverXferMayGo: the squad's cats
   announcement went, or is unchanged) - after a reconnect the re-announcements precede the resends on the one ordered channel.
   A pass that did not run its squads (no player sector) resends nothing; nothing is abandoned by waiting (sends count, not time). */
static void P119KeepSample(const std::map<void*, std::vector<unsigned int> >& squads, double now);   // PROBE P119
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
                g_pending.erase(it++);
                ReleaseOpenFromFlight(flownKey, flown, std::vector<unsigned int>(), 1);   /* design 2.5 item 1: members put away while it flew go to a release */
                for (size_t k = 0; k < ab.size(); ++k) WorldsyncCatchupHandoverSettled(ab[k]);   /* M7a2 item 2 [m7a2-ho3]: the aborted hand-over keeps them here */
                continue; }
            std::map<void*, int>::const_iterator g = gate.find(p.ap);
            const int seen = (g != gate.end()) ? 1 : 0;
            if (coopsquad::ResendMayGo(1, seen, seen != 0 ? g->second : 1) != 0
                && net::SendXfer(it->first, p.reason, &p.members[0], (int)p.members.size(), p.targetSlot)) { ++p.sends;   /* [a1b1-hd5]: the first send's slot only */ ++g_xferResent; p.sentAt = now; }
        }
        ++it;
    }
}

void HandoffTick()
{
    ReleaseForgetDrain();   /* [a1b2-hd6]: a world teardown's / the other player gone request first */
    RelRevokeWaitDrain();   /* [a1b2f1-hd12] [review F4]: REVOKEs that waited for unblocked engine writes */
    ReleaseFlush(NowSec());   /* [a1b2-hd6] design 2.5 item 2: last frame's put-aways become RELEASE offers - every frame, before the 1 Hz gate */
    P119Flush();   // PROBE P119
    if (++g_tick % kTickEvery != 0) return;
    const double now = NowSec();
    /* 1. resend / abandon pending transfers: T-1 B3 fold (L3) - at the END of this pass (HandoffResends), after the announcements */
    LeadLinkEdge();   /* T-1 B1 restructure: a new link hears every announcement again */
    ReleaseTick(now);   /* [a1b2-hd7] design 2.5: every open release's next step - resend, next candidate, asleep */
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
        void* ap = SquadActivePlatoonOf(c);
        if (ap == 0) { ap = (void*)c; solo.insert(ap); }   // no squad: a squad of one, keyed by the character
        squads[ap].push_back(uid);
    }
    P119KeepSample(squads, now);   // PROBE P119
    /* T-1 B3 restructure (protocol 89): MSG_KEEPER retired - each squad's money rides its announcement below */
    std::set<void*> leadSeen;   /* T-1 B1 restructure: my squads announced (or unchanged) this pass - LeadDropUnseen retires the rest */
    std::map<void*, int> xferGate;   /* T-1 B3 fold (L3): per squad seen, HandoverXferMayGo - its resends are gated the same way */
    int leadSends = 0;
    for (std::map<void*, std::vector<unsigned int> >::iterator it = squads.begin(); it != squads.end(); ++it)
    {
        ++g_squadsSeen;
        SquadView sv;
        const bool isSquad = solo.count(it->first) == 0 && ReadSquadAt(it->first, &sv) == 1;
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
            if (haveCats != 0 && CatsAdoptAction(it->first, leaderUid, &adoptDelta, &adoptSeq, &adoptCats) != coopsquad::kCatsNone)
            { leadSeen.insert(it->first); xferGate[it->first] = 0; g_catsDue = 1; continue; }
        }
        // T-1 B1 restructure (re-check N1-N5 of ff655c5): ONE AUTHORITY, ONE ANNOUNCED VALUE. This game's engine's acting leader of
        // the squad (EngineLeaderChoice: +0xA0 if standing, else +0xA8 - computed ONCE, here) is announced with the formal leader
        // (+0xA0) and the members this game runs (MSG_SQUAD_LEAD), and it is the very value this game decides with. The other
        // game's announced acting leader is used AS IS. Both games so feed AgreedSquadLeader the same pair (named beats none,
        // differing -> the lower uid), and only the game that does NOT run the agreed leader sends its members (follow-leader
        // XFER). An agreed leader neither game runs here is "leader unknown": the position rule. SquadFollowLeader's engine-only
        // fallback is gone - the announced value already names the engine's leader when the other game runs it.
        unsigned int followUid = 0;
        if (isSquad)
        {
            unsigned int uids[kSquadViewCap];
            for (int k = 0; k < sv.stored; ++k) uids[k] = Plaus(sv.members[k]) ? FindSpawnedUid(sv.members[k]) : 0u;
            int actingUsable = 0;
            for (int k = 0; k < sv.stored; ++k) if (Plaus(sv.acting) && sv.members[k] == sv.acting) { actingUsable = LeaderOut(sv.acting) ? 0 : 1; break; }
            const int choice = coopsquad::EngineLeaderChoice(leader != 0 ? 1 : 0, (leader != 0 && LeaderOut(leader)) ? 1 : 0, actingUsable);
            ::Character* eng = choice == coopsquad::kEngineLeadA0 ? leader : (choice == coopsquad::kEngineLeadA8 ? sv.acting : 0);
            if (choice == coopsquad::kEngineLeadA8) ++g_leaderActing;
            const unsigned int myLead = eng != 0 ? FindSpawnedUid(eng) : 0u;
            leadSeen.insert(it->first);
            announceCurrent = AnnounceLead(it->first, myLead, leaderUid, it->second, haveCats, cats, &leadSends);   /* T-1 B3 restructure: with the money */
            const coopsquad::PeerLeadEntry* pe = PeerAnnouncedFor(sv, uids);
            const unsigned int peerLead = pe != 0 ? pe->acting : 0u;
            if (myLead != 0 && peerLead != 0 && myLead != peerLead) ++g_agreedDiffered;
            followUid = coopsquad::AgreedSquadLeader(myLead, peerLead);
        }
        xferGate[it->first] = coopsquad::HandoverXferMayGo(haveCats, announceCurrent);   /* T-1 B3 fold (L3) */
        const bool followMine = followUid != 0 && net::IsUidMine(followUid);
        const bool followPeer = followUid != 0 && !followMine && HasPuppet(followUid);   // the other game runs it (H1: a live puppet here)
        std::map<unsigned int, double>::const_iterator bo = g_followBackoff.find(followUid);
        const int held = (followPeer && (g_pending.find(followUid) != g_pending.end() || bo != g_followBackoff.end())) ? 1 : 0;
        const int act = coopsquad::FollowAction((followMine || followPeer) ? 1 : 0, followMine ? 1 : 0, held);
        if (act == coopsquad::kFollowWait) { ++g_waitedLeader; continue; }
        if (coopsquad::HandoverXferMayGo(haveCats, announceCurrent) == 0) continue;   /* T-1 B3 restructure (F1): the squad's cats go out before its XFER - next second */
        if (act == coopsquad::kFollowSend)
        {
            Pending p; p.reason = coopsquad::kXferReasonFollowLeader; p.sentAt = now; p.sends = 1; p.ap = it->first; p.logged = 0;
            BuildMembers(it->second, &p.members);
            PassHoldElsewhere(followUid, &p.members, "follow-leader");   /* M7a2 fold 3 [m7a2h-hp1] (T807 GAP 1) */
            PendingKeysRead(&p);   /* [a1b2-hd8]: each member's squad key while whole - the ACK's index entry */
            if (coopsquad::PassSendAfterHold((int)p.members.size()) != coopsquad::kPassSendGo) continue;
            p.targetSlot = net::OwnerSlotOf(followUid);   /* M7a A1 build 1 [a1b1-hd6]: the game that runs the leader */
            if (!net::SendXfer(followUid, p.reason, &p.members[0], (int)p.members.size(), p.targetSlot)) continue;
            ++g_xferOut; ++g_followedLeader; g_xferFollowSent += (long long)p.members.size();
            p.logged = FollowLogChanged(0, followUid, (long long)p.members.size()) ? 1 : 0;
            g_pending[followUid] = p;
            if (p.logged != 0)
            {
                const std::string q(1, (char)34);
                DebugLog("[XFER] -> squad leader=" + N(followUid) + " members=" + N((long long)p.members.size()) + " reason=follow-leader");
                DebugLog("[VERDICT] {" + q + "ev" + q + ":" + q + "xfer_out" + q + "," + q + "leader" + q + ":" + N(followUid) + "," + q + "members" + q + ":" + N((long long)p.members.size())
                         + "," + q + "forced" + q + ":0," + q + "reason" + q + ":" + q + "follow-leader" + q + "," + q + "tick" + q + ":" + N(g_tick) + "}");
            }
            continue;
        }
        if (act == coopsquad::kFollowByPosition && isSquad) ++g_leaderUnknownFellBack;
        // N4: the position path's XFER is keyed on a uid this game RUNS - the agreed leader when mine, else +0xA0 when mine, else the
        // first member run here - never on a character this game does not run.
        const unsigned int key = followMine ? followUid : (leaderMine ? leaderUid : it->second[0]);
        if (g_pending.find(key) != g_pending.end()) continue;
        // T-1 B1 step 2: the SQUAD position decides (the engine sleeps the squad by it); a squad of one, or a squad whose
        // position is not usable yet, falls back to the leader's (else the first member's) own position.
        // T-164 B4-3 (M4): a trader squad with a home building by its HOME's position - the one the adoption decided by
        // (SquadDecisionPosAt), so the keeper is not handed away while its home, stock and restock stay here.
        float lx = 0, ly = 0, lz = 0;
        if (isSquad && SquadDecisionPosAt(sv, &lx, &lz) == 1) ly = sv.y;
        else
        {
            ::Character* pc = leader ? leader : FindSpawned(it->second[0]);
            if (!Plaus(pc) || !ReadPos(pc, &lx, &ly, &lz)) continue;
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
        if (keepHere < 0) { ++g_keepReadFailForced; continue; }
        if (keepHere == 1) { g_keepNoted.erase(key); g_niwNoted.erase(key); continue; }
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
            continue;
        }
        const int target = rcv[0];
        std::map<unsigned int, ForcedTry>::iterator ftry = g_forcedTry.find(key);
        if (ftry != g_forcedTry.end() && ftry->second.refused != 0)
        {
            if (ftry->second.target == target && ftry->second.x == s.x && ftry->second.y == s.y) continue;   /* review MED-1: backed off - that receiver refused it in this sector */
            g_forcedTry.erase(ftry);   /* the squad's sector or its receiver changed: offered again */
        }
        Pending p; p.reason = coopsquad::kXferReasonForced; p.sentAt = now; p.sends = 1; p.ap = it->first; p.logged = 1;
        BuildMembers(it->second, &p.members);
        PassHoldElsewhere(key, &p.members, "forced");   /* M7a2 fold 3 [m7a2h-hp2] (T807 GAP 1) */
        PendingKeysRead(&p);   /* [a1b2-hd8] */
        if (coopsquad::PassSendAfterHold((int)p.members.size()) != coopsquad::kPassSendGo) continue;
        p.targetSlot = target;   /* the first game with the area loaded (the holder first) */
        if (!net::SendXfer(key, p.reason, &p.members[0], (int)p.members.size(), p.targetSlot)) continue;
        ++g_xferOut; ++g_holderLeave; if (listedHere != 0) ++g_keepForcedListed;
        g_pending[key] = p;
        { ForcedTry f; f.target = target; f.x = s.x; f.y = s.y; f.refused = 0; g_forcedTry[key] = f; }   /* its ACK says whether to back off */
        const std::string q(1, (char)34);
        DebugLog("[XFER] -> squad leader=" + N(key) + " members=" + N((long long)p.members.size()) + " sector=" + SectorString(s) + " (my player sector " + SectorString(mine) + ") " + reason + " to slot " + N((long long)target) + " keepHere=0 listed=" + N((long long)listedHere));
        DebugLog("[VERDICT] {" + q + "ev" + q + ":" + q + "xfer_out" + q + "," + q + "leader" + q + ":" + N(key) + "," + q + "members" + q + ":" + N((long long)p.members.size())
                 + "," + q + "sector" + q + ":" + q + SectorString(s) + q + "," + q + "forced" + q + ":" + (forced ? "1" : "0") + "," + q + "reason" + q + ":" + q + reason + q + "," + q + "tick" + q + ":" + N(g_tick) + "}");
    }
    LeadDropUnseen(leadSeen, &leadSends);   /* squads this game no longer runs any member of: n = 0, the other game forgets them */
    HandoffResends(now, xferGate);          /* T-1 B3 fold (L3): after this pass's announcements */
}

void ApplyRemoteXfer(unsigned int leader, unsigned int reason, const net::XferMember* m, int count, unsigned int fromPeer, const unsigned int* gens)
{
    ++g_xferIn;
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
    int taken = 0, intents = 0, unresolved = 0, unknown = 0;
    std::vector<unsigned int> takenUids;   // F447: the ACK names exactly these
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
    for (int i = 0; i < count; ++i)
    {
        ::Character* c = FindSpawned(m[i].uid);
        if (!Plaus(c)) { ++unknown; ++g_xferInUnknownUid; if (logIn) DebugLog("[XFER] member uid=" + N(m[i].uid) + " has no copy here - NOT taken (the sender keeps it)"); continue; }   /* M7a3f6 [m7a3f6-hb1] M7a3f7: no void here - an offer (even a forced one: the ordinary position hand-over is sent forced too, H1) says nothing of whether the sender still runs the person; only its RELEASE does (A1 build 2) */
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
        if (net::IsUidMine(m[i].uid)) { ++taken; takenUids.push_back(m[i].uid); continue; }   // duplicate XFER (resend after our ACK was lost): already ours
        takenUids.push_back(m[i].uid);
        TakeMemberFromPeer(m[i], net::GenForTake(m[i].uid, gens != 0 ? gens[i] : 0u), &intents, &unresolved);   /* [a1b2-hd4] design 2.5 item 3: the take path shared with RELEASE (the giver's gen + 1, or above this game's copy record) */
        ++taken;
        if (reason == (unsigned int)coopsquad::kXferReasonFollowLeader) ++g_xferFollowTaken;
    }
    if (net::SendXferAck(leader, takenUids.empty() ? 0 : &takenUids[0], (int)takenUids.size(), fromPeer)) ++g_ackOut;
    if (isFollow && taken > 0) FollowLogReset(2, leader);   // settled: the next refusal logs again
    if (!logIn && taken == 0) return;
    const std::string q(1, (char)34);
    DebugLog("[XFER] <- squad leader=" + N(leader) + " members=" + N(count) + " taken=" + N(taken) + " intents=" + N(intents) + " unresolvedSubjects=" + N(unresolved) + " unknown=" + N(unknown) + (reason == (unsigned int)coopsquad::kXferReasonForced ? std::string(" FORCED") : (reason == (unsigned int)coopsquad::kXferReasonNone ? std::string() : " reason=" + std::string(coopsquad::XferReasonName(reason)))));
    DebugLog("[VERDICT] {" + q + "ev" + q + ":" + q + "xfer_in" + q + "," + q + "leader" + q + ":" + N(leader) + "," + q + "members" + q + ":" + N(count) + "," + q + "taken" + q + ":" + N(taken)
             + "," + q + "intents" + q + ":" + N(intents) + "," + q + "unresolved" + q + ":" + N(unresolved) + "," + q + "unknown" + q + ":" + N(unknown) + ",\"tick\":" + N(g_tick) + "}");
}

static long long g_ackNotMine = 0;   /* fold 1 [a1b1f1-hd3] [F7]: members an ACK named that this game no longer runs (a dual run's yield released them) */
void ApplyRemoteXferAck(unsigned int leader, const unsigned int* takenUids, int count, unsigned int fromPeer)
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
    const bool logAck = !isFollow || p.logged != 0;   /* N5: an ACK line is written exactly when its send's xfer_out line was - the two stay paired */
    for (size_t k = 0; k < p.members.size(); ++k)
    {
        const unsigned int uid = p.members[k].uid;
        // F447 (T134): the receiver takes only uids it has a live copy of; releasing an untaken uid left a character
        // that nobody simulated. What the receiver did not name stays ours: a forced hand-over then backs off from that receiver
        // until the squad's sector or its receiver changes (g_forcedTry); a follow-leader one waits kFollowBackoffSec.
        bool taken = false;
        for (int i = 0; i < count; ++i) if (takenUids[i] == uid) { taken = true; break; }
        if (!taken) { WorldsyncCatchupHandoverSettled(uid); /* M7a2 item 2 [m7a2-ho1]: still ours - a catch-up that held it back is answered */ ++kept; ++g_keptUntaken; if (logAck) DebugLog("[XFER] member uid=" + N(uid) + " NOT taken by the receiver (no copy there) - still ours"); continue; }
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
        SquadCatsFinalBeforeRelease(uid);        /* T-1 B3 fold (M3): the round trip's own change, announced before the release */
        net::ReleaseLocalOwner(uid, fromPeer);   // the streams stop here (IsUidMine false)
        SquadCatsReleased(uid);                  /* T-1 B3 fold (L4): no stale carries-cats record */
        WorldStateOnOwnershipReleased(uid, rel);   // E27: "we owned this record" is no longer true   /* E32 (verify-p5u MEDIUM-1): the UID leads, because `rel` is 0 for every squad whose row the engine has already retired - which is most of them, since a hand-off is offered exactly when this game is walking away from them */
        PuppetIfBody(uid, rel, "XFER ACK");   /* [a1b2f1-hd13] [review F6]: our character becomes the puppet of the new owner (no body: a counted debug line) */
        ++g_released; ++released;
    }
    if (kept > 0 && p.reason == (unsigned int)coopsquad::kXferReasonFollowLeader) g_followBackoff[leader] = NowSec() + kFollowBackoffSec;   // T-1 B1
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
        DebugLog("[VERDICT] {" + q + "ev" + q + ":" + q + "xfer_ack" + q + "," + q + "leader" + q + ":" + N(leader) + "," + q + "released" + q + ":" + N(released) + "," + q + "kept" + q + ":" + N(kept) + "," + q + "sends" + q + ":" + N(p.sends) + ",\"tick\":" + N(g_tick) + "}");
    }
    const Pending flown(p);
    g_pending.erase(it);
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
    __try { ReadAuthorityFacing(c, &m->fx, &m->fz); ReadAuthorityIntent(c, &m->intentType, &m->intentSubject, &m->ix, &m->iy, &m->iz); return 1; }
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
        DebugLog("[PROBE] P119 putAway key=" + N(r.key) + " n=" + N((long long)r.n) + " reason='" + std::string(r.reason) + "' pos=" + N((long long)r.x) + "," + N((long long)r.z)
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
    DebugLog("[PROBE] P119 keep self=" + N(k[0]) + " side=" + N(k[1]) + " corner=" + N(k[2]) + " out=" + N(k[3]) + " edge[<500,<1000,<2000,>=2000]=" + N(e[0]) + "," + N(e[1]) + "," + N(e[2]) + "," + N(e[3])
             + " P119[putAway self,side,corner,out; keep self,side,corner,out; logged,counted]=" + N(g_p119Put[0]) + "," + N(g_p119Put[1]) + "," + N(g_p119Put[2]) + "," + N(g_p119Put[3])
             + ";" + N(g_p119Keep[0]) + "," + N(g_p119Keep[1]) + "," + N(g_p119Keep[2]) + "," + N(g_p119Keep[3]) + ";" + N(g_p119Logged) + "," + N(g_p119Counted));
}
// PROBE-END: P119
/* M7a A1 build 2 [a1b2-hd13] (design 2.5 item 1): NotifyDespawn's UNLOAD branch, this game's own uid - collected into this frame's batch for
   the engine squad (ReleaseFlush opens it next frame). No judgement here. Not collected: a person in an XFER awaiting its ACK (the ACK's),
   an off-thread drain (objWhole 0: nothing read), an unreadable object, a player's person, one the other games were never told about
   (nobody holds a copy) - their UNLOAD goes as before. */
void ReleaseOnPutAway(unsigned int uid, const void* obj, int objWhole)
{
    ReleaseForgetDrain();
    if (uid == 0 || !net::IsUidMine(uid)) return;
    if (cooplo::PutAwayDuringFlight(HandoffPendingHas(uid) ? 1 : 0) == cooplo::kPadLeaveToAck) { ++g_relSkipInFlight; return; }
    /* [a1b2f1-hd14] [review F5]: a person not offered stays in this game's world data only - kept here (its key from the body when whole,
       else the 1 Hz cache), so a partly given squad is never treated as wholly given */
    if (objWhole == 0) { ++g_relSkipOffThread; IdxKeep(uid, 0); return; }
    ::Character* c = (::Character*)obj;
    if (!Plaus(c)) { ++g_relSkipUnread; IdxKeep(uid, 0); return; }
    const int player = PutAwayPlayerPod(c);
    if (player != 0) { if (player == 1) ++g_relSkipPlayer; else { ++g_relSkipUnread; IdxKeep(uid, 0); } return; }
    if (coopsquad::AnnouncedToOthers(AnnouncedState(uid)) == 0) { ++g_relSkipNotAnnounced; IdxKeep(uid, c); return; }
    RelMember rm; rm.m.uid = uid; rm.baseGen = net::MineGenOf(uid);
    { void* f = 0; std::string id; if (SquadKeyOf(c, &f, &id)) { rm.key = HoKey(f, id); rm.hasKey = 1; } }   /* read once, while the body is whole (design 2.1) */
    if (!ReadPos(c, &rm.m.x, &rm.m.y, &rm.m.z) || PutAwayFacingIntentPod(c, &rm.m) != 1) { ++g_relSkipUnread; IdxKeepKey(uid, rm.key, rm.hasKey); return; }
    if (rm.hasKey == 0) { ++g_relSkipNoKey; IdxKeepKey(uid, rm.key, 0); return; }   /* [a1b2f1-hd15] [review F3]: no squad key - never offered (a re-wake could not cancel it); its UNLOAD goes as before */
    SquadView sv; void* ap = 0; unsigned int leaderUid = 0; float dx = rm.m.x, dz = rm.m.z;
    if (ReadSquadOf(c, &sv) == 1)
    {
        ap = sv.ap;
        if (Plaus(sv.leader)) leaderUid = FindSpawnedUidForDestroy(sv.leader);
        if (sv.posUsable != 0) { dx = sv.x; dz = sv.z; }   /* the squad decision position - the engine sleeps the squad by it */
    }
    if (ap == 0) ap = (void*)c;
    RelBatch& b = g_relBatch[ap];
    if (b.mem.empty()) { b.keyUid = (leaderUid != 0 && net::IsUidMine(leaderUid)) ? leaderUid : uid; b.sector = SectorOf(dx, dz); b.px = dx; b.pz = dz; }
    b.mem.push_back(rm);
}
/* the sweep met a rebuilt person of squad (faction, id) - this game's engine re-created it from its own world data: the squad's asleepHere
   people are live here again (they leave the index), and an OPEN release of its people is CANCELLED (design 2.5 item 7: the engine runs
   them again under fresh uids - un-adopted members settle asleep-superseded, their UNLOAD goes, a late adopter is revoked). */
void HandedOverLeftBehindRebuilt(void* faction, const std::string& id)
{
    ReleaseForgetDrain();
    RelWalk walk;   /* [a1b2f1-hd10] [review F1]: its own walk of g_release and the index entry below */
    const HoKey k(faction, id);
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
    /* [a1b2f1-hd17] [review F2]: a person of the squad only in this game's world data (asleep, kept, or just cancelled) -> the WHOLE entry goes
       (given too, as the retired unmark did): the re-woken people are adopted by the sweep, never noted as orphans and deleted */
    std::map<HoKey, SquadIdx>::iterator s = g_squadIdx.find(k);
    if (s == g_squadIdx.end()) return;
    if (cooplo::SquadRebuiltDropsEntry((int)s->second.asleepHere.size(), (int)s->second.keptHere.size(), cancelledNow > 0 ? 1 : 0) == 0) return;
    const long long ng = (long long)s->second.given.size(), na = (long long)s->second.asleepHere.size(), nk = (long long)s->second.keptHere.size();
    for (std::map<unsigned int, unsigned int>::const_iterator g = s->second.given.begin(); g != s->second.given.end(); ++g) g_idxOf.erase(g->first);
    for (std::set<unsigned int>::const_iterator a = s->second.asleepHere.begin(); a != s->second.asleepHere.end(); ++a) g_idxOf.erase(*a);
    g_squadIdx.erase(s);
    g_idxAsleepRebuilt += na; ++g_idxRebuiltDropped;
    RelLog("[RELEASE] squad id='" + id + "' - the sweep met a person of it this game's engine re-woke: its WHOLE index entry goes (given " + N(ng) + ", asleep " + N(na)
           + ", kept " + N(nk) + ", cancelled now " + N(cancelledNow) + ") - its re-woken people are adopted here (indexMore asleepRebuilt " + N(g_idxAsleepRebuilt)
           + ", rebuiltDropped " + N(g_idxRebuiltDropped) + ")");
}
bool HandedOverLeftBehindAny()
{
    ReleaseForgetDrain();
    if (!g_release.empty()) return true;   /* an open release may need cancelling (HandedOverLeftBehindRebuilt) */
    for (std::map<HoKey, SquadIdx>::const_iterator s = g_squadIdx.begin(); s != g_squadIdx.end(); ++s) if (!s->second.asleepHere.empty()) return true;
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
           + " releaseSkip[inFlight,offThread,unread,player,notAnnounced]=" + N(g_relSkipInFlight) + "," + N(g_relSkipOffThread) + "," + N(g_relSkipUnread) + "," + N(g_relSkipPlayer) + "," + N(g_relSkipNotAnnounced)
           + " releaseFlight[notTaken,abandoned]=" + N(g_relFlightNotTaken) + "," + N(g_relFlightAbandoned)
           + " releaseIn[offers,adopted,dup,dropped,stale,deferred,revokes,puppeted,removed,revokeIgnored]=" + N(g_relInOffers) + "," + N(g_relInAdopted) + "," + N(g_relInDup) + "," + N(g_relInDropped)
           + "," + N(g_relInStale) + "," + N(g_relInDeferred) + "," + N(g_relInRevokes) + "," + N(g_relInPuppeted) + "," + N(g_relInRemoved) + "," + N(g_relInRevokeIgnored)
           + " index[given,asleepHere,noKey,givenClearedByOwnerUnload]=" + N(g_idxGiven) + "," + N(g_idxAsleepHere) + "," + N(g_idxNoKey) + "," + N(g_idxClearedByOwnerUnload) + " givenKeptOnAnnounce=" + N(g_idxKeptOnAnnounce)
           + " indexMore[clearedByOwnerDespawn,takenBack,squadCleared,asleepRebuilt,peerGoneCleared]=" + N(g_idxClearedByOwnerDespawn) + "," + N(g_idxTakenBack) + "," + N(g_idxSquadCleared)
           + "," + N(g_idxAsleepRebuilt) + "," + N(g_idxPeerGoneCleared)
           + " indexPlayerGone[squads,people]=" + N(g_idxPlayerGoneSquads) + "," + N(g_idxPlayerGonePeople)
           + " indexNow[squads,given,asleepHere]=" + N((long long)g_squadIdx.size()) + "," + N(given) + "," + N(asleep)
           + " passHold[held,empty]=" + N(g_passHeld) + "," + N(g_passHeldEmpty)
           + " releaseFold1[revokeOwedAdded,revokeRetrySent,revokeOwedNow,inRevokeWaited,inRevokeWaitApplied,inRevokeWaitNow,capDropped,rewokeAtAck,rewokeAtSettle,skipNoKey,flightNoKey,puppetNoBody,drainDeferred]="
           + N(g_relRevokeOwedAdded) + "," + N(g_relRevokeRetrySent) + "," + N((long long)g_relRevokeOwed.size()) + "," + N(g_relInRevokeWaited) + "," + N(g_relInRevokeWaitApplied)
           + "," + N((long long)g_relRevokeWait.size()) + "," + N(g_relRevokeCapDropped) + "," + N(g_relRewokeAtAck) + "," + N(g_relRewokeAtSettle) + "," + N(g_relSkipNoKey)
           + "," + N(g_relFlightNoKey) + "," + N(g_puppetNoBody) + "," + N(g_relDrainDeferred)
           + " indexFold1[kept,keptUnknown,rebuiltDropped,scans,keptRecentNow,keyCacheNow]=" + N(g_idxKept) + "," + N(g_idxKeptUnknown) + "," + N(g_idxRebuiltDropped)
           + "," + N(g_idxScans) + "," + N((long long)g_keptRecent.size()) + "," + N((long long)g_keyCache.size())   /* [a1b2f1-hd18] */
           + " releaseFold2[rewokeAcceptAtAck,rewokeLateAccepted,rewokeHolderUnknown,revokeWaitWorldCleared,revokeOwedWorldCleared,revokeWaitPlayerGone,revokeOwedPlayerGone,goneSlotUnknown]="
           + N(g_relRewokeAcceptAtAck) + "," + N(g_relRewokeLateAccepted) + "," + N(g_relRewokeHolderUnknown) + "," + N(g_relRevokeWaitWorldCleared) + "," + N(g_relRevokeOwedWorldCleared)
           + "," + N(g_relRevokeWaitPlayerGone) + "," + N(g_relRevokeOwedPlayerGone) + "," + N(g_relRevokeGoneSlotUnknown);   /* [a1b2f2-hd7] */
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

void ReportHandoff()
{
    const std::string q(1, (char)34);
    DebugLog("[XFER] REPORT squadsSeen=" + N(g_squadsSeen) + " xferOut=" + N(g_xferOut) + " resent=" + N(g_xferResent) + " abandoned=" + N(g_xferAbandoned) + " ackIn=" + N(g_ackIn) + " released=" + N(g_released)
             + " | xferIn=" + N(g_xferIn) + " taken=" + N(g_taken) + " intents=" + N(g_intentsGiven) + " unresolvedSubjects=" + N(g_intentsSubjectUnresolved) + " unknownUids=" + N(g_xferInUnknownUid) + " ackOut=" + N(g_ackOut)
             + " pending=" + N((long long)g_pending.size()) + " teleports=" + N(g_teleports)
             + " | nudges=" + N(g_nudges) + " keptUntaken=" + N(g_keptUntaken) + " xferInOutsideRing=" + N(g_xferInOutsideRing)
             + " | squadWriter[followedLeader,leaderUnknownFellBack,waitedLeader,xferFollowSent,xferFollowTaken,leaderSet,leaderSetMiss,keepAwakeRefresh]="
             + N(g_followedLeader) + "," + N(g_leaderUnknownFellBack) + "," + N(g_waitedLeader) + "," + N(g_xferFollowSent) + "," + N(g_xferFollowTaken)
             + "," + N(g_leaderSet) + "," + N(g_leaderSetMiss) + "," + N(g_keepAwakeRefresh)
             + " followBackoff=" + N((long long)g_followBackoff.size())
             + " | fold[agreedDiffered,leaderActing,leaderSetPrecond,followLogQuiet]="
             + N(g_agreedDiffered) + "," + N(g_leaderActing) + "," + N(g_leaderSetPrecond) + "," + N(g_followLogQuiet)
             + " | squadLead[sent,recv,applied,stale,crossingResolved,crossingRefused]="
             + N(g_squadLeadSent) + "," + N(g_squadLeadRecv) + "," + N(g_squadLeadApplied) + "," + N(g_squadLeadStale) + "," + N(g_crossingResolved) + "," + N(g_crossingRefused)
             + " myLeadSquads=" + N((long long)g_myLead.size()) + " peerLeadSquads=" + N((long long)g_peerLead.Squads()) + " peerLeadFull=" + N(g_squadLeadFull)
             + SquadCatsReport());   /* T-1 B3 restructure */
    /* releaseMore / releaseHolder / releaseSkip ride the [net] REPORT token, which T1056's report lines did not show - printed
       here as well, with the keep counters */
    DebugLog("[XFER] REPORT releaseMore[offers,held,next,lateAccepted,ackWrongSender,ackStale,ackUnknown,dupAck,notMine,worldCleared,logged]=" + N(g_relOffers) + "," + N(g_relHeld)
             + "," + N(g_relNext) + "," + N(g_relLateAccepted) + "," + N(g_relAckWrongSender) + "," + N(g_relAckStale) + "," + N(g_relAckUnknown) + "," + N(g_relDupAck) + "," + N(g_relNotMine)
             + "," + N(g_relWorldCleared) + "," + N(g_relLogged)
             + " releaseHolder[offer,none,hold,keep,keepUnknown,leave,notInWorld,backoff]=" + N(g_holderOffer) + "," + N(g_holderNone) + "," + N(g_holderHold) + "," + N(g_holderKeep)
             + "," + N(g_holderKeepUnknown) + "," + N(g_holderLeave) + "," + N(g_holderNotInWorld) + "," + N(g_holderBackoff)
             + " keep[notKeep,asleepNoKeep,readFailTake,readFailForced,forcedListed]=" + N(g_keepNotKeep) + "," + N(g_keepAsleepNoKeep) + "," + N(g_keepReadFailTake)
             + "," + N(g_keepReadFailForced) + "," + N(g_keepForcedListed)
             + " releaseSkip[inFlight,offThread,unread,player,notAnnounced]=" + N(g_relSkipInFlight) + "," + N(g_relSkipOffThread) + "," + N(g_relSkipUnread) + "," + N(g_relSkipPlayer)
             + "," + N(g_relSkipNotAnnounced)
             + " release[sent,adopted,asleep]=" + N(g_relSent) + "," + N(g_relAdopted) + "," + N(g_relAsleep) + " releaseOpen=" + N((long long)g_release.size()));
    DebugLog("[VERDICT] {" + q + "ev" + q + ":" + q + "xfer_report" + q + "," + q + "xferOut" + q + ":" + N(g_xferOut) + "," + q + "ackIn" + q + ":" + N(g_ackIn) + "," + q + "released" + q + ":" + N(g_released)
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
}
// T-1 B1 step 5, restructured (protocol 85): at the K2 safe point (worker paused), once a second - each copy squad (no member run
// here, not a player's or a stand-in's) whose announced FORMAL leader (the other game's newest MSG_SQUAD_LEAD among its members,
// a live puppet here - review H1) is one of its members but not its +0xA0 gets it through the engine's own
// ActivePlatoon::setSquadLeader, so this engine derives the same acting leader from the replicated states. T528 (Confirmed) saw
// the copy game's engine pick its own leader otherwise. A split squad is the follow pass's to settle; once whole here it drains.
void SquadLeaderSafePointDrain()
{
    static DWORD last = 0; const DWORD nowMs = ::GetTickCount();
    if (nowMs - last < 1000) return;
    last = nowMs;
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
        const int r = SetSquadLeaderPod(ap, lead, fn);
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
// ZoneManager::isZoneLoadedT answer for the member's position - the mod's existing loaded-position test.
void CopySquadKeepAwake(void* faction)
{
    if (faction == 0 || !coop::PlayersPresent()) return;
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
// T-1 B0 (TEST LEVER). `playerteleport peer`: the watched player (GetTarget, the character PlayerTeleport moves) to about 5 m
// from this game's COPY of the other player's lead character. A copy = a mirror slot whose uid this game does not own
// (net::IsUidMine false) in the other player's own faction (PeerFactionPod 1, the test PeerPlayerPosition uses); the lead =
// the copy that is its own squad's leader (ActivePlatoon::squadleader, LeaderOf), else the lowest uid. Same engine teleport and
// camera follow as PlayerTeleport. 50 world units = about 5 m (packbuytest's 15 = ~1.5 m scale; Inferred).
bool PlayerTeleportPeer()
{
    const float kPeerOffset = 50.0f;
    ::Character* pc = GetTarget();
    float x = 0, y = 0, z = 0;
    if (!Plaus(pc) || !ReadPos(pc, &x, &y, &z))
    { DebugLog("[TRADER] teleport peer MISSED reason=this game has no watched player character, or its position could not be read"); return false; }
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
    { DebugLog("[TRADER] teleport peer MISSED reason=no copy of the other player's characters is loaded here (peerCopies=0)"); return false; }
    float tx = 0, ty = 0, tz = 0;
    if (!ReadPos(best, &tx, &ty, &tz))
    { DebugLog("[TRADER] teleport peer MISSED reason=the target's position could not be read target uid=" + N(bestUid)); return false; }
    float ux = x - tx, uz = z - tz;
    const float len = sqrtf(ux * ux + uz * uz);
    if (len < 0.01f) { ux = 1.0f; uz = 0.0f; } else { ux /= len; uz /= len; }
    const float nx = tx + ux * kPeerOffset, nz = tz + uz * kPeerOffset;
    if (!Teleport(pc, nx, ty, nz))
    { DebugLog("[TRADER] teleport peer MISSED reason=the engine's teleport 0x5C9BF0 faulted target uid=" + N(bestUid)); return false; }
    ++g_teleports;
    const int cam = FocusCameraPod(nx, ty, nz);
    DebugLog("[TRADER] teleport peer from " + F1(x) + "," + F1(y) + "," + F1(z) + " to " + F1(nx) + "," + F1(ty) + "," + F1(nz)
             + " target uid=" + N(bestUid) + " lead=" + N(bestLead) + " peerCopies=" + N(copies) + " dist=" + F1(len)
             + " camera=" + N(cam));
    return true;
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
            if (KeeperPot((void*)c, &own, &cur) != 1) { CatsPotUnread(L, eSeq); continue; }
            const int potFlight = (unresolved > 0 || std::find(flight.begin(), flight.end(), own) != flight.end()) ? 1 : 0;
            if (coopsquad::TakeoverAdoptHeld(act, potFlight) != 0) { ++g_catsAdoptHeld; continue; }   /* T-1 B3 fold (M2): after they settle */
            const long long want = (act == coopsquad::kCatsAbsolute) ? (long long)eCats : (long long)cur + delta;
            if (want > 2147483647LL || want < -2147483647LL - 1LL) { CatsPotUnread(L, eSeq); continue; }
            if (want != (long long)cur && KeeperPotWritePod(own, (int)want) == 0) { CatsPotUnread(L, eSeq); continue; }
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
        const int held = (unresolved > 0 || std::find(flight.begin(), flight.end(), p.own) != flight.end()) ? 1 : 0;
        const int act = coopsquad::CopyPotAction(p.cats, p.cur, held);
        if (act == coopsquad::kPotEqual) continue;
        if (act == coopsquad::kPotHold) { ++g_catsHeld; continue; }   /* F5: written by the first pass after they settle */
        if (KeeperPotWritePod(p.own, p.cats) == 0) { CatsPotUnread(p.leader, p.seq); continue; }
        ++g_catsApplied; ++g_catsLogIn;
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
           + " takeSnaps=" + N((long long)g_catsTakeSnap.size());   /* T-1 B3 fold */
}

} // namespace coop
